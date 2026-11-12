// Photon - discretizer: converts the exact scene into the prototype solver's
// uniform grid, thin barriers and fuel cells.
#include "sim/solver.h"
#include <algorithm>
#include <cmath>

namespace photon {

namespace {
constexpr float MARGIN_XY = 1.2f;   // outside air around the eaves (m)
constexpr float MARGIN_TOP = 1.4f;  // outside air above the ridge (m)
} // namespace

bool Solver::build(const Scene& sc, const MaterialLibrary& lb, std::string& err) {
    scene = &sc;
    lib = &lb;
    dx = 0.2f;
    ox = -MARGIN_XY;
    oy = -MARGIN_XY;
    oz = 0;
    nx = (int)std::ceil((sc.width + 2 * MARGIN_XY) / dx);
    ny = (int)std::ceil((sc.depth + 2 * MARGIN_XY) / dx);
    nz = (int)std::ceil((sc.ridgeHeight() + MARGIN_TOP) / dx);
    N = nx * ny * nz;
    if (N <= 0 || N > 20000000) { err = "grid size out of range"; return false; }

    auto fill = [](auto& vec, size_t n, auto val) { vec.assign(n, val); };
    size_t NX = (size_t)(nx + 1) * ny * nz, NY = (size_t)nx * (ny + 1) * nz, NZ = (size_t)nx * ny * (nz + 1);
    fill(u, NX, 0.f); fill(v, NY, 0.f); fill(w, NZ, 0.f);
    fill(fx, NX, 1.f); fill(fy, NY, 1.f); fill(fz, NZ, 1.f);
    fill(bx, NX, -1); fill(by, NY, -1); fill(bz, NZ, -1);
    fill(p, N, 0.f); fill(S, N, 0.f); fill(T, N, T0); fill(Yo, N, YO2_AMB);
    fill(Yf, N, 0.f); fill(Ys, N, 0.f); fill(Ya, N, 0.f); fill(Yp, N, 0.f);
    fill(Q, N, 0.f); fill(Qr, N, 0.f); fill(hrr, N, 0.f);
    fill(lit, N, (uint8_t)0); fill(pilot, N, (uint8_t)0);
    fill(blocked, N, (uint8_t)0); fill(porous, N, (uint8_t)0);
    fill(fuelMat, N, (int16_t)-1); fill(itemOf, N, (int16_t)-1);
    fill(fuelMass, N, 0.f); fill(fuelMass0, N, 0.f); fill(charMass, N, 0.f);
    fill(fuelT, N, T0); fill(fuelArea, N, 0.f); fill(radQ, N, 0.f); fill(condQ, N, 0.f);
    for (auto& t : tmp_) t.assign(N, 0.f);
    u2_.assign(NX, 0.f); v2_.assign(NY, 0.f); w2_.assign(NZ, 0.f); rhs_.assign(N, 0.f);
    barriers.clear(); ignitions.clear(); events.clear(); eventKeys_.clear(); solidCells.clear();
    time = 0;
    stats = SimStats{};

    auto fi = [&](float x) { return (int)std::lround((x - ox) / dx); };   // nearest face index
    auto fj = [&](float y) { return (int)std::lround((y - oy) / dx); };
    auto fk = [&](float z) { return (int)std::lround((z - oz) / dx); };
    auto cx = [&](int i) { return ox + (i + 0.5f) * dx; };              // cell center
    auto cy = [&](int j) { return oy + (j + 0.5f) * dx; };
    auto cz = [&](int k) { return oz + (k + 0.5f) * dx; };

    const int iW0 = fi(0), iW1 = fi(sc.width), jS0 = fj(0), jN1 = fj(sc.depth);
    kFloorS.assign(sc.stories, 0);
    kCeilS.assign(sc.stories, 0);
    for (int s = 0; s < sc.stories; ++s) {
        kFloorS[s] = fk(sc.storyBase(s));
        bool halfTop = sc.halfStory && s == sc.stories - 1;
        kCeilS[s] = fk(sc.storyBase(s) + (halfTop ? sc.collarHeight : sc.ceilingHeight));
        if (s > 0 && kFloorS[s] <= kCeilS[s - 1]) kFloorS[s] = kCeilS[s - 1] + 1;  // keep a joist-space layer
        if (kCeilS[s] <= kFloorS[s]) { err = "story too short for the grid"; return false; }
    }
    kCeil = kCeilS[sc.stories - 1];
    const int top = sc.stories - 1;
    const bool half = sc.halfStory;
    const int kRoof0 = half ? fk(sc.roofBase()) : kCeil;   // rafter plate / truss heel level
    const int kSoffit = kRoof0 - 1;
    const int nEave = std::max(1, (int)std::lround(sc.eaveOverhang / dx));
    // roof profile (needed early: half-story walls stop at the roof)
    auto inFoot = [&](int j) { return j >= jS0 && j < jN1; };
    auto inRoofJ = [&](int j) { return j >= jS0 - nEave && j < jN1 + nEave; };
    auto ktop = [&](int j) -> int {
        if (!inFoot(j)) return kRoof0 + 1;  // eave: soffit at kSoffit, deck above
        int dj = std::min(j - jS0, jN1 - 1 - j);
        float rise = sc.pitch * (dj + 0.5f) * dx;
        int n = std::max(1, (int)std::ceil(rise / dx - 0.5f));
        return kRoof0 + n;
    };
    auto dFromEave = [&](int j) { return (std::min(j - jS0, jN1 - 1 - j) + 0.5f) * dx; };   // plan distance in from the eave wall
    // a half-story cell that is finished room space (between the knee walls, below the flat ceiling)
    auto roomRegion = [&](int j, int k) { return half && inFoot(j) && k >= kFloorS[top] && k < kCeil && dFromEave(j) >= sc.kneeInset; };
    auto storyOfK = [&](int k) {
        int s = 0;
        while (s + 1 < sc.stories && k >= kFloorS[s + 1]) ++s;
        return s;
    };

    auto roomOf = [&](float x, float y, int k) -> int {
        int s = storyOfK(k);
        if (k >= kCeilS[s]) return -1;  // joist space or attic
        for (size_t r = 0; r < sc.rooms.size(); ++r) {
            const Room& rm = sc.rooms[r];
            if (rm.story == s && x >= rm.x0 && x <= rm.x1 && y >= rm.y0 && y <= rm.y1) return (int)r;
        }
        return -1;
    };

    auto makeBarrier = [&](int axis, int i, int j, int k, int assembly, BarrierKind kind, bool aInside) -> int {
        int face, ca, cb;
        if (axis == 0) { face = xface(i, j, k); ca = i > 0 ? cell(i - 1, j, k) : -1; cb = i < nx ? cell(i, j, k) : -1; }
        else if (axis == 1) { face = yface(i, j, k); ca = j > 0 ? cell(i, j - 1, k) : -1; cb = j < ny ? cell(i, j, k) : -1; }
        else { face = zface(i, j, k); ca = k > 0 ? cell(i, j, k - 1) : -1; cb = k < nz ? cell(i, j, k) : -1; }
        std::vector<int>& bv = axis == 0 ? bx : axis == 1 ? by : bz;
        if (bv[face] >= 0) return -1;  // already has a barrier (wall junction)
        const Assembly& as = lb.assemblies[assembly];
        Barrier b;
        b.assembly = assembly;
        b.axis = (uint8_t)axis;
        b.face = face;
        b.cellA = ca;
        b.cellB = cb;
        b.aIsInside = aInside;
        b.kind = kind;
        b.front = 0;
        b.back = (int)as.layers.size() - 1;
        b.leak = as.leakage;
        for (int l = 0; l < (int)as.layers.size() && l < MAX_LAYERS; ++l) {
            const Material& m = lb.materials[as.layers[l].material];
            float am = lb.arealMass(as.layers[l]);
            b.mass[l] = m.combustible ? am * (1 - m.charYield) : am;
        }
        int inside = b.cellOn(0);
        if (inside >= 0) {
            int ii, jj, kk;
            cellIJK(inside, ii, jj, kk);
            b.room = roomOf(cx(ii), cy(jj), kk);
        }
        bv[face] = (int)barriers.size();
        barriers.push_back(b);
        return bv[face];
    };

    // ---- walls (each story; exterior walls also close the floor-joist band) ----
    for (const Wall& wl : sc.walls) {
        int k0 = kFloorS[wl.story];
        int k1 = wl.exterior && wl.story + 1 < sc.stories ? kFloorS[wl.story + 1] : kCeilS[wl.story];
        bool alongX = wl.y0 == wl.y1;
        if (half && wl.story == top) {
            if (wl.exterior && alongX) k1 = kRoof0;           // eave wall up to the rafter plate
            else if (wl.exterior) {                           // gable end: full height to the roof line
                int ifc = fi(wl.x0);
                bool aInside = !(wl.x0 <= 0.0f);
                for (int j = jS0; j < jN1; ++j)
                    for (int k = k0; k < ktop(j); ++k) {
                        bool room = roomRegion(j, k);
                        makeBarrier(0, ifc, j, k, room ? wl.assembly : sc.gable, room ? BarrierKind::Wall : BarrierKind::Gable, aInside);
                    }
                continue;
            } else {                                          // partitions stop at the flat or sloped ceiling
                float a = alongX ? std::min(wl.x0, wl.x1) : std::min(wl.y0, wl.y1);
                float b = alongX ? std::max(wl.x0, wl.x1) : std::max(wl.y0, wl.y1);
                if (alongX) {
                    int jf = fj(wl.y0);
                    int kk = std::min({kCeil, ktop(jf - 1), ktop(jf)});
                    for (int i = 0; i < nx; ++i) {
                        float x = cx(i);
                        if (x < a || x > b) continue;
                        for (int k = k0; k < kk; ++k) makeBarrier(1, i, jf, k, wl.assembly, BarrierKind::Wall, true);
                    }
                } else {
                    int ifc = fi(wl.x0);
                    for (int j = 0; j < ny; ++j) {
                        float y = cy(j);
                        if (y < a || y > b) continue;
                        for (int k = k0; k < std::min(kCeil, ktop(j)); ++k) makeBarrier(0, ifc, j, k, wl.assembly, BarrierKind::Wall, true);
                    }
                }
                continue;
            }
        }
        float a = alongX ? std::min(wl.x0, wl.x1) : std::min(wl.y0, wl.y1);
        float b = alongX ? std::max(wl.x0, wl.x1) : std::max(wl.y0, wl.y1);
        if (alongX) {
            int jf = fj(wl.y0);
            bool aInside = !(wl.exterior && wl.y0 <= 0.0f);  // south wall: inside is +y (cellB)
            for (int i = 0; i < nx; ++i) {
                float x = cx(i);
                if (x < a || x > b) continue;
                for (int k = k0; k < k1; ++k) makeBarrier(1, i, jf, k, wl.assembly, BarrierKind::Wall, aInside);
            }
        } else {
            int ifc = fi(wl.x0);
            bool aInside = !(wl.exterior && wl.x0 <= 0.0f);  // west wall: inside is +x (cellB)
            for (int j = 0; j < ny; ++j) {
                float y = cy(j);
                if (y < a || y > b) continue;
                for (int k = k0; k < k1; ++k) makeBarrier(0, ifc, j, k, wl.assembly, BarrierKind::Wall, aInside);
            }
        }
    }
    // ---- knee walls (half story): between the room and the knee-wall attics ----
    if (half) {
        int jKs = fj(sc.kneeInset), jKn = fj(sc.depth - sc.kneeInset);
        for (int i = iW0; i < iW1; ++i) {
            for (int k = kFloorS[top]; k < ktop(jKs - 1); ++k) makeBarrier(1, i, jKs, k, sc.kneeWall, BarrierKind::Wall, false);
            for (int k = kFloorS[top]; k < ktop(jKn); ++k) makeBarrier(1, i, jKn, k, sc.kneeWall, BarrierKind::Wall, true);
        }
    }
    // ---- openings replace wall faces --------------------------------------------
    for (size_t oi = 0; oi < sc.openings.size(); ++oi) {
        const Opening& o = sc.openings[oi];
        bool alongX = o.y0 == o.y1;
        float a = alongX ? std::min(o.x0, o.x1) : std::min(o.y0, o.y1);
        float b = alongX ? std::max(o.x0, o.x1) : std::max(o.y0, o.y1);
        int line = alongX ? fj(o.y0) : fi(o.x0);
        for (int s = 0; s < (alongX ? nx : ny); ++s) {
            float c = alongX ? cx(s) : cy(s);
            if (c < a || c > b) continue;
            for (int k = kFloorS[o.story]; k < kCeilS[o.story]; ++k) {
                float z = cz(k) - sc.storyBase(o.story);
                if (z < o.sill || z > o.head) continue;
                int face = alongX ? yface(s, line, k) : xface(line, s, k);
                std::vector<int>& bv = alongX ? by : bx;
                bool aInside = true;
                int room = -1;
                if (bv[face] >= 0) { aInside = barriers[bv[face]].aIsInside; room = barriers[bv[face]].room; }
                // Remove the wall barrier on this face (swap-remove keeps indices dense).
                if (bv[face] >= 0) {
                    int idx = bv[face];
                    int last = (int)barriers.size() - 1;
                    if (idx != last) {
                        barriers[idx] = barriers[last];
                        Barrier& mv = barriers[idx];
                        std::vector<int>& mvv = mv.axis == 0 ? bx : mv.axis == 1 ? by : bz;
                        mvv[mv.face] = idx;
                    }
                    barriers.pop_back();
                    bv[face] = -1;
                }
                if (o.kind == OpeningKind::Cased) continue;
                int bi = alongX ? makeBarrier(1, s, line, k, o.assembly,
                                              o.kind == OpeningKind::Door ? BarrierKind::Door : BarrierKind::Window, aInside)
                                : makeBarrier(0, line, s, k, o.assembly,
                                              o.kind == OpeningKind::Door ? BarrierKind::Door : BarrierKind::Window, aInside);
                if (bi >= 0) {
                    barriers[bi].opening = (int)oi;
                    barriers[bi].userOpen = o.kind == OpeningKind::Door && o.open;
                    if (room >= 0) barriers[bi].room = room;
                }
            }
        }
    }
    // ---- ceilings and floors between stories (stairwells left open) -----------
    auto inStair = [&](int s, int i, int j) {
        float x = cx(i), y = cy(j);
        for (const Stair& st : sc.stairs)
            if (st.story == s && x > st.x0 && x < st.x1 && y > st.y0 && y < st.y1) return true;
        return false;
    };
    for (int s = 0; s < sc.stories; ++s) {
        bool top = s == sc.stories - 1;
        for (int j = jS0; j < jN1; ++j)
            for (int i = iW0; i < iW1; ++i) {
                if (!top && inStair(s, i, j)) continue;
                if (top && half && (dFromEave(j) < sc.slopedRun() || ktop(j) <= kCeilS[s])) continue;   // sloped zone: the roof is the ceiling
                makeBarrier(2, i, j, kCeilS[s], top ? sc.ceiling : sc.floorCeiling, BarrierKind::Ceiling, true);
                if (!top) makeBarrier(2, i, j, kFloorS[s + 1], sc.subfloor, BarrierKind::Floor, false);
            }
    }

    // ---- roof, eaves, soffits, gables ---------------------------------------
    int ridgeTop = 0;
    for (int j = jS0; j < jN1; ++j) ridgeTop = std::max(ridgeTop, ktop(j));
    if (ridgeTop >= nz) { err = "roof does not fit in the grid"; return false; }
    std::vector<int> ridgeFaces, soffitFaces;
    for (int i = iW0; i < iW1; ++i) {
        for (int j = jS0 - nEave; j < jN1 + nEave; ++j) {
            int t = ktop(j);
            // a half story's finished rooms have the roof assembly with the sloped ceiling on its underside
            auto roofAsm = [&](int jj, int kk) { return roomRegion(jj, kk) ? sc.slopedCeiling : sc.roof; };
            int bi = makeBarrier(2, i, j, t, roofAsm(j, t - 1), BarrierKind::Roof, true);
            if (bi >= 0 && t == ridgeTop) ridgeFaces.push_back(bi);
            if (!inFoot(j)) {
                int sb = makeBarrier(2, i, j, kSoffit, sc.soffit, BarrierKind::Soffit, false);
                if (sb >= 0) soffitFaces.push_back(sb);
            }
            if (inRoofJ(j + 1)) {  // risers between neighboring columns
                int t2 = ktop(j + 1);
                if (t2 != t) {
                    bool upperIsJ = t > t2;  // attic is in the taller column
                    for (int k = std::min(t, t2); k < std::max(t, t2); ++k)
                        makeBarrier(1, i, j + 1, k, roofAsm(upperIsJ ? j : j + 1, k), BarrierKind::Roof, upperIsJ);
                }
            }
        }
        // fascia at the outer eave edges (eave cells kSoffit..kRoof0)
        for (int k = kSoffit; k <= kRoof0; ++k) {
            makeBarrier(1, i, jS0 - nEave, k, sc.fascia, BarrierKind::Fascia, false);
            makeBarrier(1, i, jN1 + nEave, k, sc.fascia, BarrierKind::Fascia, true);
        }
    }
    for (int j = jS0 - nEave; j < jN1 + nEave; ++j) {
        if (half && inFoot(j)) continue;   // half-story gable walls were built with the walls
        int t = ktop(j);
        int k0 = inFoot(j) ? kCeil : kSoffit;
        BarrierKind kind = inFoot(j) ? BarrierKind::Gable : BarrierKind::Fascia;
        int as = inFoot(j) ? sc.gable : sc.fascia;
        for (int k = k0; k < t; ++k) {
            makeBarrier(0, iW0, j, k, as, kind, false);
            makeBarrier(0, iW1, j, k, as, kind, true);
        }
    }
    // Attic ventilation: IRC R806.2 net free area split between soffit and ridge.
    float atticArea = sc.width * sc.depth;
    float nfa = atticArea / sc.atticVentRatio;
    if (!soffitFaces.empty()) {
        float f = std::min(1.0f, 0.5f * nfa / (soffitFaces.size() * dx * dx));
        for (int b : soffitFaces) barriers[b].leak = f;
    }
    if (!ridgeFaces.empty()) {
        float f = std::min(1.0f, 0.5f * nfa / (ridgeFaces.size() * dx * dx));
        for (int b : ridgeFaces) barriers[b].leak = f;
    }

    // ---- floor coverings (porous fuel in the first cell layer) -----------------
    for (const Room& rm : sc.rooms) {
        if (rm.floorMaterial < 0) continue;
        const Material& m = lb.materials[rm.floorMaterial];
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                float x = cx(i), y = cy(j);
                if (x <= rm.x0 || x >= rm.x1 || y <= rm.y0 || y >= rm.y1) continue;
                int c = cell(i, j, kFloorS[rm.story]);
                fuelMat[c] = (int16_t)rm.floorMaterial;
                porous[c] = 1;
                fuelMass0[c] = rm.floorArealMass * dx * dx;
                fuelMass[c] = fuelMass0[c] * (m.combustible ? 1 - m.charYield : 1);
                fuelArea[c] = dx * dx;
            }
    }
    // ---- truss lumber (porous fuel in attic cells) ------------------------------
    for (const Member& mb : sc.members) {
        bool truss = mb.kind == MemberKind::TrussChord || mb.kind == MemberKind::TrussWeb;
        bool rafter = mb.kind == MemberKind::Rafter || mb.kind == MemberKind::RidgeBoard || mb.kind == MemberKind::CollarTie;
        if (!truss && !rafter && mb.kind != MemberKind::Joist && mb.kind != MemberKind::Tread) continue;
        float d[3] = {mb.p1[0] - mb.p0[0], mb.p1[1] - mb.p0[1], mb.p1[2] - mb.p0[2]};
        float L = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        const Material& m = lb.materials[mb.material];
        int steps = std::max(1, (int)(L / 0.02f));
        float seg = L / steps;
        for (int s = 0; s < steps; ++s) {
            float t = (s + 0.5f) / steps;
            float x = mb.p0[0] + d[0] * t, y = mb.p0[1] + d[1] * t, z = mb.p0[2] + d[2] * t;
            int i = (int)std::floor((x - ox) / dx), j = (int)std::floor((y - oy) / dx), k = (int)std::floor((z - oz) / dx);
            if (i < 0 || j < 0 || i >= nx || j >= ny) continue;
            if (truss) k = std::clamp(k, kCeil, std::max(kCeil, ktop(j) - 1));
            else if (rafter) {
                k = std::min(k, ktop(j) - 1);
                if (mb.kind == MemberKind::CollarTie) k = std::max(k, kCeil);
                if (roomRegion(j, k)) continue;   // inside the finished sloped ceiling: part of that assembly
            }
            else if (mb.kind == MemberKind::Joist) { int s = storyOfK(k); k = std::clamp(k, kCeilS[s], std::max(kCeilS[s], (s + 1 < sc.stories ? kFloorS[s + 1] : kCeilS[s] + 1) - 1)); }
            if (k >= nz) continue;
            int c = cell(i, j, k);
            float mass = m.density * mb.width * mb.depth * seg;
            if (fuelMat[c] < 0) { fuelMat[c] = (int16_t)mb.material; porous[c] = 1; }
            fuelMass0[c] += mass;
            fuelMass[c] += mass * (1 - m.charYield);
            fuelArea[c] += 2 * (mb.width + mb.depth) * seg;
        }
    }
    // ---- contents (solid cells) -------------------------------------------------
    itemMass0.assign(sc.items.size(), 0.f);
    itemPyro.assign(sc.items.size(), 0.f);
    itemFlags.assign(sc.items.size(), 0);
    std::vector<float> cellMatMass(N, 0.f);
    for (size_t it = 0; it < sc.items.size(); ++it) {
        for (const Part& pt : sc.items[it].parts) {
            std::vector<int> cells;
            for (int k = 0; k < nz; ++k)
                for (int j = 0; j < ny; ++j)
                    for (int i = 0; i < nx; ++i) {
                        float x = cx(i), y = cy(j), z = cz(k);
                        if (x >= pt.box.x0 && x <= pt.box.x1 && y >= pt.box.y0 && y <= pt.box.y1 && z >= pt.box.z0 && z <= pt.box.z1)
                            cells.push_back(cell(i, j, k));
                    }
            if (cells.empty()) {
                int c = cellAt(0.5f * (pt.box.x0 + pt.box.x1), 0.5f * (pt.box.y0 + pt.box.y1), 0.5f * (pt.box.z0 + pt.box.z1));
                if (c >= 0) cells.push_back(c);
            }
            if (cells.empty()) continue;
            float share = pt.mass / cells.size();
            const Material& m = lb.materials[pt.material];
            for (int c : cells) {
                bool fresh = !blocked[c];
                if (fresh) {  // replaces any floor covering in this cell
                    fuelMass0[c] = fuelMass[c] = charMass[c] = 0;
                    fuelArea[c] = 0;
                    porous[c] = 0;
                    cellMatMass[c] = 0;
                }
                if (fresh || share > cellMatMass[c]) { fuelMat[c] = (int16_t)pt.material; cellMatMass[c] = share; }
                blocked[c] = 1;
                itemOf[c] = (int16_t)it;
                fuelMass0[c] += share;
                fuelMass[c] += share * (m.combustible ? 1 - m.charYield : 1);
            }
            itemMass0[it] += pt.mass;
        }
    }
    for (int c = 0; c < N; ++c)
        if (fuelMat[c] >= 0) solidCells.push_back(c);

    // ---- face open fractions ----------------------------------------------------
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i <= nx; ++i) updateFace(0, xface(i, j, k));
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j <= ny; ++j)
            for (int i = 0; i < nx; ++i) updateFace(1, yface(i, j, k));
    for (int k = 0; k <= nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) updateFace(2, zface(i, j, k));
    for (int c = 0; c < N; ++c)
        if (blocked[c]) T[c] = T0;
    return true;
}

} // namespace photon
