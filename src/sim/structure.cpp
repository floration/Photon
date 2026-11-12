// Photon - structure: weight, support and gravity (first version, prototype).
#include "sim/structure.h"
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace photon {

namespace {
bool isFraming(const std::string& k) {
    return k == "stud" || k == "plate" || k == "header" || k == "floor joist" || k == "rafter" || k == "ridge board" ||
           k == "collar tie" || k == "truss chord" || k == "truss web" || k == "stair tread" || k == "subfloor panel" ||
           k == "slab" || k == "gusset plate";
}
bool isSheet(const std::string& k) {
    return k.find("sheet") != std::string::npos || k == "roof deck panel" || k == "roofing" || k == "siding course" ||
           k == "subfloor panel" || k == "soffit" || k == "insulation" || k == "insulation batt";
}
// Strength (see docs/DATA_REGISTER.md):
//  - wood members: NDS reference design values from the material file, size
//    factor CF from NDS Supplement Table 4A, converted to mean ultimate strength
//    with the NDS 2018 Table 16.2.2 factors (2.85 bending, 2.58 compression);
//    columns use the Ylinen column curve (NDS Eq. 3.7-1, c = 0.8) with Euler
//    buckling about the unbraced (depth) axis.
//  - fire: the remaining section comes from the consumed fraction, less a 7 mm
//    zero-strength layer on each side once charring has started (Eurocode 5,
//    EN 1995-1-2, reduced cross-section method).
//  - panels: ultimate uniform load from the material file, scaled by thickness^2.
//  - attached boards (gypsum): fastener pull-through times fastener density.
constexpr float NDS_ULT_BENDING = 2.85f, NDS_ULT_COMPRESSION = 2.58f;
constexpr float ZERO_STRENGTH_LAYER = 0.007f;
constexpr float FAIL_INTEGRITY = 0.05f;   // below this a piece is treated as burned away
constexpr float IN_M = 0.0254f;
Role roleFor(const std::string& k) {
    if (k == "stud" || k == "truss chord" || k == "truss web" || k == "collar tie") return Role::Column;
    if (k == "floor joist" || k == "rafter" || k == "header" || k == "ridge board" || k == "stair tread") return Role::Beam;
    if (k == "plate") return Role::Bearing;
    if (k == "subfloor panel" || k == "roof deck panel") return Role::Panel;
    if (k.find("gypsum") != std::string::npos) return Role::Attached;
    return Role::None;
}
constexpr float G = 9.81f;
} // namespace

bool Structure::build(const Scene& sc, const MaterialLibrary& lib, const Solver& sim) {
    scene_ = &sc;
    lib_ = &lib;
    pieces_.clear();
    reported_.clear();
    fallen_ = 0;
    byObject_.assign(sc.objects.size(), -1);
    for (size_t oi = 0; oi < sc.objects.size(); ++oi) {
        const SceneObject& o = sc.objects[oi];
        if (o.stage == STAGE_MEP_LAYOUT) continue;
        Piece p;
        p.object = (int)oi;
        p.ground = o.kind == "slab";
        p.framing = isFraming(o.kind);
        float vol = 8.0f * o.hu * o.hv * o.hn;
        p.mass = lib.materials[o.material].density * vol;
        if (o.kind == "furniture part" && o.item >= 0) {   // furniture uses its catalogued mass
            for (const Part& pt : sc.items[o.item].parts) {
                float cx = 0.5f * (pt.box.x0 + pt.box.x1), cy = 0.5f * (pt.box.y0 + pt.box.y1), cz = 0.5f * (pt.box.z0 + pt.box.z1);
                if (std::fabs(cx - o.c[0]) < 1e-3f && std::fabs(cy - o.c[1]) < 1e-3f && std::fabs(cz - o.c[2]) < 1e-3f) { p.mass = pt.mass; break; }
            }
        }
        float ex[3];
        for (int a = 0; a < 3; ++a) ex[a] = std::fabs(o.u[a]) * o.hu + std::fabs(o.v[a]) * o.hv + std::fabs(o.n[a]) * o.hn;
        p.bb[0] = o.c[0] - ex[0]; p.bb[1] = o.c[1] - ex[1]; p.bb[2] = o.c[0] + ex[0]; p.bb[3] = o.c[1] + ex[1];
        p.zmin = o.c[2] - ex[2];
        p.zmax = o.c[2] + ex[2];
        p.role = roleFor(o.kind);
        p.len = 2 * o.hu;
        p.b = 2 * std::min(o.hv, o.hn);
        p.d = 2 * std::max(o.hv, o.hn);
        p.area = 4 * o.hu * o.hv;
        byObject_[oi] = (int)pieces_.size();
        pieces_.push_back(p);
    }
    // ---- support graph, with a coarse spatial hash
    const float cellSz = 0.6f;
    auto key = [&](int i, int j) { return (long long)i * 100003LL + j; };
    std::unordered_map<long long, std::vector<int>> grid;
    for (size_t i = 0; i < pieces_.size(); ++i) {
        const Piece& p = pieces_[i];
        for (int a = (int)std::floor(p.bb[0] / cellSz); a <= (int)std::floor(p.bb[2] / cellSz); ++a)
            for (int b = (int)std::floor(p.bb[1] / cellSz); b <= (int)std::floor(p.bb[3] / cellSz); ++b) grid[key(a, b)].push_back((int)i);
    }
    auto overlapXY = [&](const Piece& a, const Piece& b, float pad) {
        return a.bb[0] - pad < b.bb[2] && b.bb[0] - pad < a.bb[2] && a.bb[1] - pad < b.bb[3] && b.bb[1] - pad < a.bb[3];
    };
    std::vector<int> cand;
    int anchored = 0;
    for (size_t i = 0; i < pieces_.size(); ++i) {
        Piece& A = pieces_[i];
        if (A.ground) continue;
        const std::string& kind = sc.objects[A.object].kind;
        bool sheet = isSheet(kind) && !A.framing;
        cand.clear();
        for (int a = (int)std::floor((A.bb[0] - 0.15f) / cellSz); a <= (int)std::floor((A.bb[2] + 0.15f) / cellSz); ++a)
            for (int b = (int)std::floor((A.bb[1] - 0.15f) / cellSz); b <= (int)std::floor((A.bb[3] + 0.15f) / cellSz); ++b) {
                auto it = grid.find(key(a, b));
                if (it != grid.end()) cand.insert(cand.end(), it->second.begin(), it->second.end());
            }
        std::sort(cand.begin(), cand.end());
        cand.erase(std::unique(cand.begin(), cand.end()), cand.end());
        for (int bi : cand) {
            if (bi == (int)i) continue;
            const Piece& B = pieces_[bi];
            bool ok = false;
            if (A.framing) {
                // framing bears on framing (or the slab) that reaches lower than it does
                ok = (B.framing || B.ground) && overlapXY(A, B, 0.03f) && B.zmin < A.zmin - 0.005f && B.zmax > A.zmin - 0.03f;
            } else if (sheet) {
                // sheets, siding, decking and insulation are fastened to the framing they touch
                ok = (B.framing || B.ground || (kind == "roofing" && sc.objects[B.object].kind == "roof deck panel")) &&
                     overlapXY(A, B, 0.12f) && B.zmax > A.zmin - 0.12f && B.zmin < A.zmax + 0.12f;
            } else {
                // everything else rests on what is directly below it, or hangs on adjacent framing
                bool rests = overlapXY(A, B, 0.0f) && B.zmax <= A.zmin + 0.05f && B.zmax >= A.zmin - 0.08f;
                bool hangs = B.framing && overlapXY(A, B, 0.1f) && B.zmin < A.zmax && B.zmax > A.zmin;
                ok = rests || hangs;
            }
            if (ok) A.supports.push_back(bi);
        }
        if (A.supports.empty()) { A.ground = true; ++anchored; }   // geometry gap: treat as anchored rather than collapse at t=0
    }
    order_.resize(pieces_.size());
    for (size_t i = 0; i < order_.size(); ++i) order_[i] = (int)i;
    std::sort(order_.begin(), order_.end(), [&](int a, int b) { return pieces_[a].zmin > pieces_[b].zmin; });

    // ---- where each piece lives in the fire grid
    for (Piece& p : pieces_) {
        const SceneObject& o = sc.objects[p.object];
        auto addCell = [&](int c) { if (c >= 0 && std::find(p.cells.begin(), p.cells.end(), c) == p.cells.end()) p.cells.push_back(c); };
        auto addBarrier = [&](int b, int l) {
            for (size_t q = 0; q < p.barriers.size(); ++q) if (p.barriers[q] == b && p.layers[q] == l) return;
            p.barriers.push_back(b);
            p.layers.push_back(l);
        };
        if (o.kind == "furniture part" && o.item >= 0) {
            for (int c : sim.solidCells) {
                if (sim.itemOf[c] != o.item) continue;
                int i, j, k;
                sim.cellIJK(c, i, j, k);
                float pt[3] = {sim.ox + (i + 0.5f) * sim.dx, sim.oy + (j + 0.5f) * sim.dx, sim.oz + (k + 0.5f) * sim.dx};
                if (o.contains(pt, 0.11f)) addCell(c);
            }
            continue;
        }
        // sample points across the object
        int nu = std::clamp((int)(2 * o.hu / 0.3f), 1, 12), nv = std::clamp((int)(2 * o.hv / 0.3f), 1, 12);
        for (int a = 0; a < nu; ++a)
            for (int b = 0; b < nv; ++b) {
                float su = -o.hu + (a + 0.5f) * 2 * o.hu / nu, sv = -o.hv + (b + 0.5f) * 2 * o.hv / nv;
                float pt[3] = {o.c[0] + o.u[0] * su + o.v[0] * sv, o.c[1] + o.u[1] * su + o.v[1] * sv, o.c[2] + o.u[2] * su + o.v[2] * sv};
                int c = sim.cellAt(pt[0], pt[1], pt[2]);
                if (c < 0) continue;
                if (sim.porous[c] && sim.fuelMat[c] == o.material) { addCell(c); continue; }
                // an assembly layer of this material on one of the cell's faces
                int bestB = -1, bestL = -1;
                float bestD = 1e9f;
                int ci, cj, ck;
                sim.cellIJK(c, ci, cj, ck);
                for (int d = 0; d < 6; ++d) {
                    int bidx = sim.barrierOn(c, d);
                    if (bidx < 0) continue;
                    const Barrier& br = sim.barriers[bidx];
                    const Assembly& as = lib.assemblies[br.assembly];
                    float fc[3] = {sim.ox + (ci + 0.5f) * sim.dx, sim.oy + (cj + 0.5f) * sim.dx, sim.oz + (ck + 0.5f) * sim.dx};
                    fc[d / 2] += (d % 2 ? 0.5f : -0.5f) * sim.dx;
                    // which side of the face the object is on picks the matching layer (walls carry gypsum on both sides)
                    bool onA = pt[d / 2] < fc[d / 2];
                    bool fromFront = onA == br.aIsInside;
                    int lay = -1;
                    for (int l = 0; l < (int)as.layers.size(); ++l)
                        if (as.layers[l].material == o.material) { lay = l; if (fromFront) break; }
                    if (lay < 0) continue;
                    float dd = std::fabs(fc[d / 2] - pt[d / 2]);
                    if (dd < bestD) { bestD = dd; bestB = bidx; bestL = lay; }
                }
                if (bestB >= 0) addBarrier(bestB, bestL);
            }
    }
    (void)anchored;
    return true;
}

float Structure::strength(const Piece& p) const {
    const SceneObject& o = scene_->objects[p.object];
    const Material& m = lib_->materials[o.material];
    float r = std::clamp(p.integrity, 0.0f, 1.0f);
    switch (p.role) {
    case Role::Column: case Role::Beam: case Role::Bearing: {
        if (m.designFb <= 0) return 1e12f;
        float s = std::sqrt(r), zl = r < 0.995f ? 2 * ZERO_STRENGTH_LAYER : 0.0f;
        float b = std::max(0.0f, p.b * s - zl), d = std::max(0.0f, p.d * s - zl);
        if (b <= 0 || d <= 0) return 0;
        float din = p.d / IN_M;   // size factor from the nominal depth
        float cfb = din <= 3.6f ? 1.5f : din <= 5.6f ? 1.3f : din <= 7.3f ? 1.2f : din <= 9.3f ? 1.1f : 1.0f;
        float cfc = din <= 3.6f ? 1.15f : din <= 5.6f ? 1.1f : din <= 7.3f ? 1.05f : din <= 9.3f ? 1.0f : 0.9f;
        if (p.role == Role::Beam) {
            float fb = NDS_ULT_BENDING * m.designFb * cfb;
            float M = fb * b * d * d / 6.0f;
            return 8.0f * M / std::max(p.len, 0.3f);   // total uniform load on a simple span
        }
        if (p.role == Role::Bearing) return 2.5f * m.designFcPerp * b * std::max(p.len, 0.1f);
        float fc = NDS_ULT_COMPRESSION * m.designFc * cfc;
        float le = o.kind == "stud" ? p.len : 0.5f * p.len;   // truss and collar members braced at mid-length
        float fce = 3.14159265f * 3.14159265f * m.modulusE * d * d / (12.0f * le * le);
        float a = fce / fc, cc = 0.8f;
        float cp = (1 + a) / (2 * cc) - std::sqrt(std::max(0.0f, ((1 + a) / (2 * cc)) * ((1 + a) / (2 * cc)) - a / cc));
        return fc * cp * b * d;
    }
    case Role::Panel: {
        if (m.panelLoad <= 0) return 1e12f;
        float t = 2 * o.hn, tr = m.panelRefThickness > 0 ? m.panelRefThickness : t;
        return m.panelLoad * (t / tr) * (t / tr) * p.area * r;
    }
    case Role::Attached:
        if (m.fastenerPull <= 0) return 1e12f;
        return m.fastenerPull * m.fastenerDensity * p.area * r;
    default:
        return 1e12f;   // furniture, wiring, pipes: fail only by burning or losing their support
    }
}

void Structure::readIntegrity(const Solver& sim) {
    for (Piece& p : pieces_) {
        if (p.state == PieceState::Fallen && p.landCell >= 0) {   // debris keeps burning where it landed
            int c = p.landCell;
            if (sim.fuelMat[c] >= 0 && sim.fuelMass0[c] > 0) {
                const Material& m = lib_->materials[sim.fuelMat[c]];
                p.integrity = std::min(p.integrity, std::clamp(sim.fuelMass[c] / (sim.fuelMass0[c] * (1 - m.charYield)), 0.0f, 1.0f));
            } else p.integrity = 0;
            if (p.integrity < FAIL_INTEGRITY) p.state = PieceState::Gone;
            continue;
        }
        if (p.state != PieceState::Standing) continue;
        float rem = 0, tot = 0;
        for (int c : p.cells) {
            if (sim.fuelMat[c] < 0) continue;
            const Material& m = lib_->materials[sim.fuelMat[c]];
            if (!m.combustible) { rem += 1; tot += 1; continue; }
            float init = sim.fuelMass0[c] * (1 - m.charYield);
            if (init <= 0) continue;
            rem += std::clamp(sim.fuelMass[c] / init, 0.0f, 1.0f);
            tot += 1;
        }
        for (size_t q = 0; q < p.barriers.size(); ++q) {
            const Barrier& b = sim.barriers[p.barriers[q]];
            int l = p.layers[q];
            tot += 1;
            if (b.failed || l < b.front || l > b.back) continue;
            const Assembly& as = lib_->assemblies[b.assembly];
            const Material& m = lib_->materials[as.layers[l].material];
            if (m.combustible) {
                float init = lib_->arealMass(as.layers[l]) * (1 - m.charYield);
                rem += init > 0 ? std::clamp(b.mass[l] / init, 0.0f, 1.0f) : 1.0f;
            } else {
                rem += 1.0f - std::clamp(b.damage[l], 0.0f, 1.0f) * 0.5f;
            }
        }
        p.integrity = tot > 0 ? rem / tot : 1.0f;
    }
}

void Structure::report(Solver& sim, const std::string& key, const std::string& text) {
    for (const auto& r : reported_) if (r == key) return;
    reported_.push_back(key);
    sim.addEvent("structure-" + key, text);
}

void Structure::update(Solver& sim, float dt) {
    if (!scene_) return;
    readIntegrity(sim);
    // loads: own weight plus what rests on it, shared among standing supports
    for (Piece& p : pieces_) p.load = p.state == PieceState::Standing ? p.mass : 0;
    for (int i : order_) {
        Piece& p = pieces_[i];
        if (p.state != PieceState::Standing || p.ground) continue;
        int n = 0;
        for (int s : p.supports) n += pieces_[s].state == PieceState::Standing;
        if (n == 0) continue;
        for (int s : p.supports)
            if (pieces_[s].state == PieceState::Standing) pieces_[s].load += p.load / n;
    }
    // failures: consumed, or overloaded for what is left of the section
    auto roomName = [&](const Piece& p) {
        const Room* r = scene_->roomAt(0.5f * (p.bb[0] + p.bb[2]), 0.5f * (p.bb[1] + p.bb[3]), p.zmin + 0.05f);
        return r ? r->name : std::string("the attic");
    };
    auto startFall = [&](int i) {
        Piece& p = pieces_[i];
        p.state = PieceState::Falling;
        p.vz = 0;
        const SceneObject& o = scene_->objects[p.object];
        // the hole it leaves
        bool panel = o.kind == "subfloor panel" || o.kind == "roof deck panel" || o.kind == "roofing";
        for (size_t q = 0; q < p.barriers.size(); ++q) {
            if (panel) sim.failBarrier(p.barriers[q]);
            else sim.removeLayer(p.barriers[q], p.layers[q]);
        }
        if (o.kind == "roof deck panel") report(sim, "roof-" + roomName(p), "Roof deck collapsing over " + roomName(p));
        else if (o.kind == "subfloor panel") report(sim, "floor-" + roomName(p), "Floor collapsing in " + roomName(p));
        else if (o.kind == "rafter" || o.kind == "truss chord" || o.kind == "truss web") report(sim, "roofframe", "Roof framing failing - collapse hazard");
        else if (o.kind == "floor joist") report(sim, "joists", "Floor joists failing - collapse hazard");
        else if (o.kind.find("gypsum") != std::string::npos && o.c[2] > p.zmin + 0.0f && std::fabs(o.n[2]) > 0.9f)
            report(sim, "ceiling-" + roomName(p), "Ceiling falling in " + roomName(p));
        else if (o.kind == "furniture part" && o.item >= 0) report(sim, "item-" + scene_->items[o.item].name, scene_->items[o.item].name + " collapsed");
    };
    for (int iter = 0; iter < 8; ++iter) {
        bool changed = false;
        for (size_t i = 0; i < pieces_.size(); ++i) {
            Piece& p = pieces_[i];
            if (p.state != PieceState::Standing || p.ground) continue;
            bool consumed = p.integrity < FAIL_INTEGRITY && (!p.cells.empty() || !p.barriers.empty());
            if (consumed) {   // burned away: nothing left to fall
                p.state = PieceState::Gone;
                changed = true;
                continue;
            }
            p.capacity = strength(p);
            bool overloaded = p.load * G > p.capacity;
            bool unsupported = true;
            for (int s : p.supports)
                if (pieces_[s].state == PieceState::Standing) { unsupported = false; break; }
            if (overloaded || unsupported) {
                startFall((int)i);
                changed = true;
            }
        }
        if (!changed) break;
    }
    // gravity: falling pieces drop until they land on something standing or fallen
    const float sub = 0.02f;
    for (size_t i = 0; i < pieces_.size(); ++i) {
        Piece& p = pieces_[i];
        if (p.state != PieceState::Falling) continue;
        float floorZ = 0.0f;
        float cx = 0.5f * (p.bb[0] + p.bb[2]), cy = 0.5f * (p.bb[1] + p.bb[3]);
        for (const Piece& q : pieces_) {
            if (&q == &p || q.state == PieceState::Falling) continue;
            if (!(cx >= q.bb[0] && cx <= q.bb[2] && cy >= q.bb[1] && cy <= q.bb[3])) continue;
            float top = q.zmax + q.dz;
            if (top <= p.zmin + p.dz + 0.02f) floorZ = std::max(floorZ, top);
        }
        for (float t = 0; t < dt; t += sub) {
            p.vz -= G * sub;
            p.dz += p.vz * sub;
            if (p.zmin + p.dz <= floorZ) {
                p.dz = floorZ - p.zmin;
                p.vz = 0;
                p.state = PieceState::Fallen;
                ++fallen_;
                // burning framing lands with its fuel
                int land = sim.cellAt(cx, cy, floorZ + 0.05f);
                p.landCell = p.cells.empty() ? -1 : land;
                if (land >= 0 && !p.cells.empty()) {
                    float each = p.mass / p.cells.size();
                    for (int c : p.cells) sim.moveFuel(c, land, each);
                }
                break;
            }
        }
    }
}

} // namespace photon
