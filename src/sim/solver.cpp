// Photon - Phase 1 prototype solver (see solver.h and docs/PROTOTYPE_PHYSICS.md).
#include "sim/solver.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>

namespace photon {

namespace {
constexpr float KAPPA_SOOT = 1200.0f;   // m2/kg, prototype grey IR absorption of soot
constexpr float KAPPA_GAS = 0.15f;      // 1/m, prototype CO2/H2O band absorption, grey
constexpr float RAD_PATH = 0.5f;        // m, local path length for gas-to-surface exchange
constexpr float H_CONV_MIN = 10.0f;     // W/(m2 K)
constexpr float CHAR_K = 0.02f;         // kg/(m2 s) per unit O2 mass fraction, prototype
constexpr float CHAR_DH = 25.0e6f;      // J/kg char oxidized
constexpr float CHAR_O2 = 2.0f;         // kg O2 per kg char
constexpr float BURNER_SOOT = 0.01f;    // soot yield of ignition-source fuel
constexpr float T_MIN = 200.0f, T_MAX = 2300.0f;
constexpr float LEAK_OPEN = 0.05f;      // open fraction at or above which a vent counts as an opening
inline float sq(float a) { return a * a; }
inline float p4(float a) { float b = a * a; return b * b; }
} // namespace

const char* barrierKindName(BarrierKind k) {
    switch (k) {
    case BarrierKind::Wall: return "wall";
    case BarrierKind::Ceiling: return "ceiling";
    case BarrierKind::Floor: return "floor";
    case BarrierKind::Roof: return "roof";
    case BarrierKind::Soffit: return "soffit";
    case BarrierKind::Gable: return "gable";
    case BarrierKind::Fascia: return "fascia";
    case BarrierKind::Door: return "door";
    case BarrierKind::Window: return "window";
    }
    return "?";
}

// Leave two hardware threads for the viewer's UI/render thread and the OS.
Solver::Solver() : pool_(std::make_unique<JobPool>(std::max(1, (int)std::thread::hardware_concurrency() - 2))) {}
Solver::Solver(int threads) : pool_(std::make_unique<JobPool>(std::max(1, threads))) {}
Solver::~Solver() = default;

int Solver::cellAt(float x, float y, float z) const {
    int i = (int)std::floor((x - ox) / dx), j = (int)std::floor((y - oy) / dx), k = (int)std::floor((z - oz) / dx);
    if (i < 0 || j < 0 || k < 0 || i >= nx || j >= ny || k >= nz) return -1;
    return cell(i, j, k);
}

int Solver::barrierOn(int c, int d) const {
    int i, j, k;
    cellIJK(c, i, j, k);
    switch (d) {
    case 0: return bx[xface(i, j, k)];
    case 1: return bx[xface(i + 1, j, k)];
    case 2: return by[yface(i, j, k)];
    case 3: return by[yface(i, j + 1, k)];
    case 4: return bz[zface(i, j, k)];
    default: return bz[zface(i, j, k + 1)];
    }
}

// Open fraction of a face: 0 if either side is solid or it is the ground,
// barrier leakage while intact, 1 otherwise. Domain sides and top are open.
void Solver::updateFace(int axis, int f) {
    int i, j, k, ca = -1, cb = -1;
    if (axis == 0) {
        i = f % (nx + 1); j = (f / (nx + 1)) % ny; k = f / ((nx + 1) * ny);
        if (i > 0) ca = cell(i - 1, j, k);
        if (i < nx) cb = cell(i, j, k);
    } else if (axis == 1) {
        i = f % nx; j = (f / nx) % (ny + 1); k = f / (nx * (ny + 1));
        if (j > 0) ca = cell(i, j - 1, k);
        if (j < ny) cb = cell(i, j, k);
    } else {
        i = f % nx; j = (f / nx) % ny; k = f / (nx * ny);
        if (k > 0) ca = cell(i, j, k - 1);
        if (k < nz) cb = cell(i, j, k);
        if (k == 0) { fz[f] = 0; return; }  // slab / ground
    }
    float val = 1.0f;
    if ((ca >= 0 && blocked[ca]) || (cb >= 0 && blocked[cb])) val = 0;
    else {
        int b = axis == 0 ? bx[f] : axis == 1 ? by[f] : bz[f];
        if (b >= 0 && barrierIntact(b)) val = barriers[b].leak;
    }
    (axis == 0 ? fx : axis == 1 ? fy : fz)[f] = val;
    if (val == 0) (axis == 0 ? u : axis == 1 ? v : w)[f] = 0;
    // An intact barrier with a small open fraction is a crack: its flow comes
    // from the orifice equation, not the pressure solve. Vents and openings
    // (open fraction >= LEAK_OPEN) join the pressure solve.
    int bb = axis == 0 ? bx[f] : axis == 1 ? by[f] : bz[f];
    bool leak = val > 0 && val < LEAK_OPEN && bb >= 0 && barrierIntact(bb);
    float a = val > 0 && !leak ? val : 0.0f;
    std::vector<float>& av = axis == 0 ? ax_ : axis == 1 ? ay_ : az_;
    std::vector<uint8_t>& lv = axis == 0 ? lx_ : axis == 1 ? ly_ : lz_;
    if (av[f] != a || lv[f] != (uint8_t)leak) zonesDirty_ = true;
    av[f] = a;
    lv[f] = (uint8_t)leak;
}

void Solver::updateCellFaces(int c) {
    int i, j, k;
    cellIJK(c, i, j, k);
    updateFace(0, xface(i, j, k)); updateFace(0, xface(i + 1, j, k));
    updateFace(1, yface(i, j, k)); updateFace(1, yface(i, j + 1, k));
    updateFace(2, zface(i, j, k)); updateFace(2, zface(i, j, k + 1));
}

std::string Solver::roomName(int room) const {
    if (room < 0 || !scene || room >= (int)scene->rooms.size()) return "outside";
    return scene->rooms[room].name;
}

void Solver::logEvent(const std::string& key, const std::string& text) {
    for (const auto& k : eventKeys_) if (k == key) return;
    eventKeys_.push_back(key);
    events.push_back({time, text});
}

void Solver::addIgnition(int c, float power, float duration, const std::string& label) {
    if (c < 0 || c >= N) return;
    ignitions.push_back({c, power, time, duration, label});
    int i, j, k;
    cellIJK(c, i, j, k);
    int room = -1;
    if (scene) {
        const Room* r = scene->roomAt(ox + (i + 0.5f) * dx, oy + (j + 0.5f) * dx, oz + (k + 0.5f) * dx);
        if (r) room = (int)(r - scene->rooms.data());
    }
    events.push_back({time, label + " placed in " + roomName(room)});
}

bool Solver::toggleOpening(int opening) {
    bool state = false;
    bool any = false;
    std::string name = (scene && opening >= 0 && opening < (int)scene->openings.size()) ? scene->openings[opening].name : "opening";
    for (Barrier& b : barriers) {
        if (b.opening != opening) continue;
        if (b.kind == BarrierKind::Door) {
            b.userOpen = !b.userOpen;
            state = b.userOpen;
        } else if (!b.failed) {
            b.failed = true;  // window broken out
            state = true;
        } else continue;
        any = true;
        updateFace(b.axis, b.face);
    }
    if (any) {
        bool isDoor = scene && scene->openings[opening].kind == OpeningKind::Door;
        events.push_back({time, name + (isDoor ? (state ? " opened" : " closed") : " broken out")});
    }
    return state;
}

void Solver::removeLayer(int bi, int layer) {
    if (bi < 0 || bi >= (int)barriers.size()) return;
    Barrier& b = barriers[bi];
    if (b.failed || layer < b.front || layer > b.back) return;
    if (layer == b.front) b.front++;
    else if (layer == b.back) b.back--;
    else b.mass[layer] = 0;   // an inner layer: leave its slot, nothing left to burn or insulate
    if (b.front > b.back) { b.failed = true; updateFace(b.axis, b.face); }
}

void Solver::clearLeakage() {
    for (Barrier& b : barriers) {
        if (b.leak <= 0 || b.leak >= LEAK_OPEN) continue;
        b.leak = 0;
        updateFace(b.axis, b.face);
    }
}

void Solver::failBarrier(int bi) {
    if (bi < 0 || bi >= (int)barriers.size() || barriers[bi].failed) return;
    barriers[bi].failed = true;
    updateFace(barriers[bi].axis, barriers[bi].face);
}

void Solver::moveFuel(int from, int to, float mass) {
    if (from < 0 || to < 0 || from >= N || to >= N || fuelMat[from] < 0 || blocked[to]) return;
    float take = std::min(mass, fuelMass[from]);
    if (take <= 0) return;
    float frac = fuelMass0[from] > 0 ? take / std::max(fuelMass[from], 1e-9f) : 0;
    float take0 = fuelMass0[from] * std::min(1.0f, frac);
    float area = fuelArea[from] * std::min(1.0f, frac);
    fuelMass[from] -= take;
    fuelMass0[from] -= take0;
    fuelArea[from] -= area;
    if (fuelMat[to] < 0) {
        fuelMat[to] = fuelMat[from];
        porous[to] = 1;
        fuelT[to] = fuelT[from];
        solidCells.push_back(to);
    }
    fuelMass[to] += take;
    fuelMass0[to] += take0;
    fuelArea[to] += area;
}

float Solver::cellSpeed(int c) const {
    int i, j, k;
    cellIJK(c, i, j, k);
    float uu = 0.5f * (u[xface(i, j, k)] + u[xface(i + 1, j, k)]);
    float vv = 0.5f * (v[yface(i, j, k)] + v[yface(i, j + 1, k)]);
    float ww = 0.5f * (w[zface(i, j, k)] + w[zface(i, j, k + 1)]);
    return std::sqrt(uu * uu + vv * vv + ww * ww);
}

void Solver::addGasSpecies(int c, float fuelGen, float aerosol, float sootPot) {
    float m = gasDensity(c) * dx * dx * dx;
    float madd = fuelGen + aerosol;
    if (madd <= 0) return;
    float mt = m + madd, sc = m / mt;
    Yo[c] *= sc; Ys[c] *= sc; Yc[c] *= sc; Yw[c] *= sc; Yco[c] *= sc;
    Yf[c] = Yf[c] * sc + fuelGen / mt;
    Ya[c] = Ya[c] * sc + aerosol / mt;
    Yp[c] = Yp[c] * sc + sootPot / mt;
    massSrc[c] += madd;
}

// ---------------------------------------------------------------------------
float Solver::step(float dtMax) {
    auto t0 = std::chrono::steady_clock::now();
    float dt = stableDt(dtMax);
    if (zonesDirty_) computeZones();

    pool_->parallelFor(N, [&](int b, int e) {
        for (int c = b; c < e; ++c) { Q[c] = 0; Qr[c] = 0; pilot[c] = 0; massSrc[c] = 0; }
    }, 4096);
    for (int c : solidCells) radQ[c] = 0;
    for (Barrier& b : barriers) b.radIn[0] = b.radIn[1] = 0;

    stats.pyrolysis = 0;
    auto tp = std::chrono::steady_clock::now();
    auto mark = [&](int i) {
        auto n = std::chrono::steady_clock::now();
        phaseMs[i] += std::chrono::duration<float, std::milli>(n - tp).count();
        tp = n;
    };
    mark(0);
    injectIgnitions(dt);
    injectBurners(dt);
    combustion(dt);        mark(1);
    flameRadiation();      mark(2);
    solidsStep(dt);        mark(3);
    barriersStep(dt);      mark(4);
    turbulence();
    heatDiffusion(dt);
    energyAndExpansion(dt); mark(5);
    momentum(dt);          mark(6);
    leakage(dt);
    project(dt);           mark(7);
    advectScalars(dt);     mark(8);
    // closed zones: background pressure follows its rise rate
    for (size_t z = 0; z < zoneP.size(); ++z)
        if (!zoneOpen[z]) zoneP[z] = std::clamp(zoneP[z] + dt * zoneDP_[z], P0 - 20000.0f, P0 + 50000.0f);
    pool_->parallelFor(N, [&](int b, int e) {
        for (int c = b; c < e; ++c) if (zone[c] >= 0) Pb[c] = zoneP[zone[c]];
    }, 4096);
    gatherStats(dt);       mark(9);
    ++phaseSteps;
    time += dt;
    stats.stepMs = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return dt;
}

void Solver::injectIgnitions(float dt) {
    for (const Ignition& ig : ignitions) {
        if (time < ig.start || time > ig.start + ig.duration) continue;
        if (blocked[ig.cell]) continue;
        float mf = ig.power * dt / DHC_FUEL;
        addGasSpecies(ig.cell, mf, 0, mf * BURNER_SOOT);
        stats.fuelIn += mf;
        pilot[ig.cell] = 1;
    }
}

// Mixing-limited burning of the generic fuel gas, with a critical flame
// temperature test for ignition and extinction (FDS-style, simplified).
void Solver::combustion(float dt) {
    const float V = dx * dx * dx;
    const float s = S_O2;                // kg O2 per kg fuel
    const float frac = 1.0f - std::exp(-dt / TAU_MIX);
    const float chiR = radiationOn ? CHI_R : 0.0f;
    const int sx = 1, sy = nx, sz = nx * ny;
    auto& litNew = tmp_[0];
    pool_->parallelFor(N, [&](int b, int e) {
        for (int c = b; c < e; ++c) {
            litNew[c] = 0;
            hrr[c] = 0;
            if (blocked[c]) continue;
            float yf = Yf[c];
            if (yf < 1e-6f) continue;
            float Tc = T[c];
            bool ign = pilot[c] || Tc > T_AUTO;
            if (!ign) {
                int i = c % nx, j = (c / nx) % ny, k = c / sz;
                ign = (i > 0 && lit[c - sx]) || (i < nx - 1 && lit[c + sx]) || (j > 0 && lit[c - sy]) ||
                      (j < ny - 1 && lit[c + sy]) || (k > 0 && lit[c - sz]) || (k < nz - 1 && lit[c + sz]);
            }
            if (!ign) continue;
            float burnable = std::min(yf, Yo[c] / s);
            if (burnable <= 1e-7f) continue;
            // Extinction: limiting O2 falls linearly from 0.15 at ambient to 0 at the
            // critical flame temperature (the simple FDS 5 style criterion).
            float yoLim = 0.15f * std::clamp((T_CRIT - Tc) / (T_CRIT - T0), 0.0f, 1.0f);
            if (!pilot[c] && Yo[c] < yoLim) continue;
            float m = gasDensity(c) * V;
            float dmf = burnable * m * frac;
            float q = dmf * DHC_FUEL;
            float sootMade = yf > 0 ? Yp[c] * (dmf / m) / yf : 0;   // mass fraction
            float coMade = CO_YIELD * dmf / m;
            float co2Made = std::max(0.0f, (C_FRAC * dmf / m - sootMade - (12.0f / 28.0f) * coMade) * (44.0f / 12.0f));
            float o2Used = s * dmf / m - (32.0f / 12.0f) * sootMade - (16.0f / 28.0f) * coMade;
            Yf[c] = std::max(0.0f, yf - dmf / m);
            Yo[c] = std::max(0.0f, Yo[c] - o2Used);
            Yp[c] = std::max(0.0f, Yp[c] - sootMade);
            Ys[c] += sootMade;
            Yco[c] += coMade;
            Yc[c] += co2Made;
            Yw[c] += Y_H2O * dmf / m;
            Ya[c] *= (1.0f - frac);
            Q[c] += q * (1.0f - chiR);
            Qr[c] = q * chiR;
            hrr[c] = q / (dt * V);
            litNew[c] = q > 1e-3f ? 1.0f : 0.0f;
        }
    }, 2048);
    pool_->parallelFor(N, [&](int b, int e) {
        for (int c = b; c < e; ++c) lit[c] = litNew[c] > 0.5f;
    }, 4096);
}

// Straight-line visibility between two cell centers through the grid.
bool Solver::lineOfSight(int s, int t) const {
    if (s == t) return true;
    int si, sj, sk, ti, tj, tk;
    cellIJK(s, si, sj, sk);
    cellIJK(t, ti, tj, tk);
    int n = 2 * std::max({std::abs(ti - si), std::abs(tj - sj), std::abs(tk - sk)});
    int pi = si, pj = sj, pk = sk;
    for (int q = 1; q <= n; ++q) {
        float f = (float)q / n;
        int ci = (int)std::lround(si + (ti - si) * f), cj = (int)std::lround(sj + (tj - sj) * f),
            ck = (int)std::lround(sk + (tk - sk) * f);
        // step one axis at a time so every crossed face is checked
        while (pi != ci || pj != cj || pk != ck) {
            int cur = cell(pi, pj, pk), d, ni = pi, nj = pj, nk = pk;
            if (pi != ci) { d = ci > pi ? 1 : 0; ni += ci > pi ? 1 : -1; }
            else if (pj != cj) { d = cj > pj ? 3 : 2; nj += cj > pj ? 1 : -1; }
            else { d = ck > pk ? 5 : 4; nk += ck > pk ? 1 : -1; }
            if (barrierIntact(barrierOn(cur, d))) return false;
            pi = ni; pj = nj; pk = nk;
            int nc = cell(pi, pj, pk);
            if (nc == t) return true;
            if (blocked[nc]) return false;
        }
    }
    return true;
}

// PROTOTYPE near-field flame radiation: the radiant fraction of each burning
// cell is shared among visible solid surfaces within two cells by solid
// angle; the rest leaves the near field and is not tracked (no radiation
// transport until Phase 4).
void Solver::flameRadiation() {
    burnList_.clear();
    for (int c = 0; c < N; ++c)
        if (Qr[c] > 0) burnList_.push_back(c);
    const int R = 1;   // 3x3x3 near field: R = 2 cost ~60% of the step in big fires
    const float A = dx * dx;
    struct Target { int kind; int idx; int side; float w; };
    std::mutex mtx;
    // deposits are applied in burn-list order so results do not depend on thread timing
    std::vector<std::pair<int, std::vector<Target>>> chunks;
    pool_->parallelFor((int)burnList_.size(), [&](int lb, int le) {
    std::vector<Target> targets, local;
    for (int li = lb; li < le; ++li) {
        int s = burnList_[li];
        int si, sj, sk;
        cellIJK(s, si, sj, sk);
        targets.clear();
        float wsum = 0;
        for (int dk = -R; dk <= R + 1; ++dk)
            for (int dj = -R; dj <= R + 1; ++dj)
                for (int di = -R; di <= R + 1; ++di) {
                    int i = si + di, j = sj + dj, k = sk + dk;
                    if (i < 0 || j < 0 || k < 0 || i >= nx || j >= ny || k >= nz) continue;
                    int t = cell(i, j, k);
                    bool inCube = di <= R && dj <= R && dk <= R;
                    if (inCube && fuelMat[t] >= 0) {
                        float r = std::max(0.5f, std::sqrt(float(di * di + dj * dj + dk * dk))) * dx;
                        if (lineOfSight(s, t)) {
                            float wt = A / (4.0f * 3.14159265f * r * r);
                            if (t == s) wt = 0.25f;
                            targets.push_back({0, t, 0, wt});
                            wsum += wt;
                        }
                    }
                    // lower faces of cell t: x at i, y at j, z at k
                    for (int ax = 0; ax < 3; ++ax) {
                        int b = ax == 0 ? bx[xface(i, j, k)] : ax == 1 ? by[yface(i, j, k)] : bz[zface(i, j, k)];
                        if (!barrierIntact(b)) continue;
                        float fc[3] = {(float)i + 0.5f, (float)j + 0.5f, (float)k + 0.5f};
                        fc[ax] -= 0.5f;
                        float sc[3] = {(float)si + 0.5f, (float)sj + 0.5f, (float)sk + 0.5f};
                        float d[3] = {fc[0] - sc[0], fc[1] - sc[1], fc[2] - sc[2]};
                        float r2 = (d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) * dx * dx;
                        if (r2 < 1e-6f) continue;
                        float r = std::sqrt(r2);
                        float cosT = std::fabs(d[ax]) * dx / r;
                        const Barrier& br = barriers[b];
                        // the near cell is on the source's side of the face
                        int nearCell = d[ax] > 0 ? br.cellA : br.cellB;
                        if (nearCell < 0 || !lineOfSight(s, nearCell)) continue;
                        int side = (nearCell == br.cellOn(0)) ? 0 : 1;
                        float wt = A * cosT / (4.0f * 3.14159265f * r2);
                        targets.push_back({1, b, side, wt});
                        wsum += wt;
                    }
                }
        if (targets.empty()) continue;
        float scale = wsum > 0.9f ? 0.9f / wsum : 1.0f;
        float P = Qr[s];
        for (const Target& tg : targets) local.push_back({tg.kind, tg.idx, tg.side, P * tg.w * scale});
    }
    std::lock_guard<std::mutex> lk(mtx);
    chunks.emplace_back(lb, std::move(local));
    }, 16);
    std::sort(chunks.begin(), chunks.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& ch : chunks)
    for (const Target& tg : ch.second) {
        if (tg.kind == 0) radQ[tg.idx] += tg.w;
        else barriers[tg.idx].radIn[tg.side] += tg.w;
    }
}

void Solver::unblockCell(int c) {
    blocked[c] = 0;
    porous[c] = 1;
    fuelArea[c] = dx * dx;
    int i, j, k;
    cellIJK(c, i, j, k);
    // fill the new gas cell from its open neighbors
    float sT = 0, sO = 0, n = 0;
    int nb[6] = {i > 0 ? c - 1 : -1, i < nx - 1 ? c + 1 : -1, j > 0 ? c - nx : -1, j < ny - 1 ? c + nx : -1,
                 k > 0 ? c - nx * ny : -1, k < nz - 1 ? c + nx * ny : -1};
    for (int q = 0; q < 6; ++q)
        if (nb[q] >= 0 && !blocked[nb[q]]) { sT += T[nb[q]]; sO += Yo[nb[q]]; n += 1; }
    T[c] = n > 0 ? sT / n : T0;
    Yo[c] = n > 0 ? sO / n : YO2_AMB;
    Yf[c] = Ys[c] = Ya[c] = Yp[c] = Yco[c] = 0;
    Yc[c] = YCO2_AMB;
    Yw[c] = YH2O_AMB;
    nu[c] = 0;
    updateCellFaces(c);
}

// Solid and porous fuel cells: convective + local radiative exchange with the
// adjacent gas, conduction between touching solid cells, flame radiation,
// pyrolysis by heat of gasification, char oxidation, burn-through.
void Solver::solidsStep(float dt) {
    const float A = dx * dx;
    const int off[6] = {-1, 1, -nx, nx, -nx * ny, nx * ny};
    // conduction between touching solid cells
    for (int c : solidCells) condQ[c] = 0;
    for (int c : solidCells) {
        if (!blocked[c]) continue;
        int i, j, k;
        cellIJK(c, i, j, k);
        const Material& m = lib->materials[fuelMat[c]];
        for (int d = 1; d < 6; d += 2) {
            if ((d == 1 && i >= nx - 1) || (d == 3 && j >= ny - 1) || (d == 5 && k >= nz - 1)) continue;
            int n = c + off[d];
            if (!blocked[n] || fuelMat[n] < 0) continue;
            const Material& mn = lib->materials[fuelMat[n]];
            float kh = 2 * m.conductivity * mn.conductivity / (m.conductivity + mn.conductivity);
            float q = kh * A / dx * (fuelT[n] - fuelT[c]) * dt;
            condQ[c] += q;
            condQ[n] -= q;
        }
    }
    std::vector<int> toUnblock;
    for (int c : solidCells) {
        const Material& m = lib->materials[fuelMat[c]];
        float total = fuelMass[c] + charMass[c];
        if (total <= 0 && !blocked[c]) continue;
        int i, j, k;
        cellIJK(c, i, j, k);
        int gas[6], ng = 0;
        if (blocked[c]) {
            for (int d = 0; d < 6; ++d) {
                if ((d == 0 && i == 0) || (d == 1 && i == nx - 1) || (d == 2 && j == 0) || (d == 3 && j == ny - 1) ||
                    (d == 4 && k == 0) || (d == 5 && k == nz - 1)) continue;
                int n = c + off[d];
                if (blocked[n] || barrierIntact(barrierOn(c, d))) continue;
                gas[ng++] = n;
            }
        } else {
            gas[ng++] = c;
        }
        float area;
        if (blocked[c]) area = ng * A;
        else area = fuelMass0[c] > 0 ? fuelArea[c] * std::cbrt(sq(std::max(0.0f, total / fuelMass0[c]))) : 0;
        float heatMass = std::max(1e-3f, std::min(total, m.density * std::max(area, 0.25f * A) * m.surfaceDepth));
        float C = heatMass * m.specificHeat;
        float Qs = radQ[c] + condQ[c];
        float Ts = fuelT[c];
        float perArea = ng > 0 ? area / ng : 0;
        float yoAvg = 0;
        for (int q = 0; q < ng; ++q) {
            int g = gas[q];
            float Tg = T[g];
            float rho = gasDensity(g);
            float epsG = 1.0f - std::exp(-(KAPPA_SOOT * rho * Ys[g] + KAPPA_GAS) * RAD_PATH);
            float h = H_CONV_MIN + 4.0f * cellSpeed(g);
            float flux = adiabaticSurfaces ? 0.0f : h * (Tg - Ts) + m.emissivity * epsG * SIGMA * (p4(Tg) - p4(Ts));
            float e = flux * perArea * dt;
            Qs += e;
            Q[g] -= e;
            yoAvg += Yo[g];
        }
        if (ng > 0) yoAvg /= ng;
        Ts += Qs / C;
        // char surface oxidation (glowing)
        if (charMass[c] > 0 && Ts > 700.0f && ng > 0 && yoAvg > 0.01f) {
            float dm = std::min(charMass[c], CHAR_K * yoAvg * area * dt);
            charMass[c] -= dm;
            Ts += dm * CHAR_DH / C;
            for (int q = 0; q < ng; ++q) {
                int g = gas[q];
                float mg = gasDensity(g) * dx * dx * dx;
                Yo[g] = std::max(0.0f, Yo[g] - CHAR_O2 * dm / ng / mg);
            }
        }
        // pyrolysis: excess heat above the pyrolysis temperature gasifies the solid
        if (m.combustible && fuelMass[c] > 0 && Ts > m.pyrolysisTemp && ng > 0) {
            float E = C * (Ts - m.pyrolysisTemp);
            float dm = std::min(fuelMass[c], E / m.heatOfGasification);
            Ts -= dm * m.heatOfGasification / C;
            fuelMass[c] -= dm;
            if (m.charYield < 0.99f) charMass[c] += dm * m.charYield / (1.0f - m.charYield);
            float gen = dm * (1.0f - m.aerosolYield) * m.heatOfCombustion / DHC_FUEL;
            for (int q = 0; q < ng; ++q)
                addGasSpecies(gas[q], gen / ng, dm * m.aerosolYield / ng, dm * m.sootYield / ng);
            stats.pyrolysis += dm / dt;
            int it = itemOf[c];
            if (it >= 0) {
                itemPyro[it] += dm;
                const std::string& nm = scene->items[it].name;
                if (!(itemFlags[it] & 1) && itemPyro[it] > 0.05f) {
                    itemFlags[it] |= 1;
                    logEvent("item-off-gas-" + nm, nm + " is off-gassing");
                }
                if (!(itemFlags[it] & 2) && itemPyro[it] > 0.5f * itemMass0[it]) {
                    itemFlags[it] |= 2;
                    logEvent("item-half-" + nm, nm + " half consumed");
                }
            }
        }
        fuelT[c] = std::clamp(Ts, T_MIN, T_MAX);
        if (blocked[c] && fuelMass[c] <= 1e-6f && m.combustible) toUnblock.push_back(c);
    }
    for (int c : toUnblock) {
        unblockCell(c);
        int it = itemOf[c];
        if (it >= 0) logEvent("item-through-" + scene->items[it].name, scene->items[it].name + " burning through");
    }
}

// Layered thin barriers: two lumped nodes (the exposed layer on each side)
// coupled by the thermal resistance of the layers in between.
void Solver::barriersStep(float dt) {
    const float A = dx * dx;
    for (int bi = 0; bi < (int)barriers.size(); ++bi) {
        Barrier& b = barriers[bi];
        if (b.failed || b.userOpen) continue;
        const Assembly& as = lib->assemblies[b.assembly];
        int g[2] = {b.cellOn(0), b.cellOn(1)};
        for (int s = 0; s < 2; ++s)
            if (g[s] >= 0 && blocked[g[s]]) g[s] = -1;
        int L[2] = {b.front, b.back};
        float C[2], flux[2] = {0, 0};
        for (int s = 0; s < 2; ++s) {
            const Layer& ly = as.layers[L[s]];
            const Material& m = lib->materials[ly.material];
            float mass = std::max(b.mass[L[s]], 0.05f);
            if (m.combustible) mass = std::min(mass, m.density * m.surfaceDepth);
            C[s] = std::max(200.0f, mass * m.specificHeat);
            // A coating (paint) heats with the board it is bonded to.
            if (ly.thickness < 0.001f && L[0] != L[1]) {
                int inner = s == 0 ? L[s] + 1 : L[s] - 1;
                if (inner != L[1 - s]) {
                    const Material& mi = lib->materials[as.layers[inner].material];
                    float mm = std::max(b.mass[inner], 0.05f);
                    if (mi.combustible) mm = std::min(mm, mi.density * mi.surfaceDepth);
                    C[s] += mm * mi.specificHeat;
                }
            }
            if (g[s] < 0) continue;
            float Tg = T[g[s]], Tn = b.T[s];
            float rho = gasDensity(g[s]);
            float epsG = 1.0f - std::exp(-(KAPPA_SOOT * rho * Ys[g[s]] + KAPPA_GAS) * RAD_PATH);
            float h = H_CONV_MIN + 4.0f * cellSpeed(g[s]);
            flux[s] = adiabaticSurfaces ? 0.0f : h * (Tg - Tn) + m.emissivity * epsG * SIGMA * (p4(Tg) - p4(Tn));
            Q[g[s]] -= flux[s] * A * dt;
        }
        float e0 = (flux[0] * A * dt + b.radIn[0]) / A, e1 = (flux[1] * A * dt + b.radIn[1]) / A;  // J/m2
        if (L[0] == L[1]) {
            float Tn = b.T[0] + (e0 + e1) / C[0];
            b.T[0] = b.T[1] = Tn;
        } else {
            float R = 0.5f * lib->layerR(as.layers[L[0]]) + 0.5f * lib->layerR(as.layers[L[1]]);
            for (int l = L[0] + 1; l < L[1]; ++l) R += lib->layerR(as.layers[l]);
            float U = 1.0f / std::max(R, 1e-3f);
            // implicit-enough for stability: limit the exchange to the equilibrium value
            float qc = U * (b.T[0] - b.T[1]) * dt;
            float eqMax = (b.T[0] - b.T[1]) * C[0] * C[1] / (C[0] + C[1]);
            if (std::fabs(qc) > std::fabs(eqMax)) qc = eqMax;
            b.T[0] += (e0 - qc) / C[0];
            b.T[1] += (e1 + qc) / C[1];
        }
        // per-node layer processes (single-layer: only side 0 acts)
        for (int s = 0; s < (L[0] == L[1] ? 1 : 2); ++s) {
            int li = L[s];
            const Material& m = lib->materials[as.layers[li].material];
            bool gone = false;
            if (m.combustible) {
                if (b.T[s] > m.pyrolysisTemp && b.mass[li] > 0) {
                    float E = C[s] * (b.T[s] - m.pyrolysisTemp);
                    float dm = std::min(b.mass[li], E / m.heatOfGasification);  // kg/m2
                    b.mass[li] -= dm;
                    b.T[s] -= dm * m.heatOfGasification / C[s];
                    if (L[0] == L[1]) b.T[1] = b.T[0];
                    float dmA = dm * A;
                    // release to the side that is burning (or the only gas side)
                    int gc = g[s] >= 0 ? g[s] : g[1 - s];
                    if (gc >= 0) {
                        float gen = dmA * (1.0f - m.aerosolYield) * m.heatOfCombustion / DHC_FUEL;
                        addGasSpecies(gc, gen, dmA * m.aerosolYield, dmA * m.sootYield);
                        stats.pyrolysis += dmA / dt;
                    }
                }
                gone = b.mass[li] <= 1e-5f;
            } else if (m.failTemp > 0 && b.T[s] > m.failTemp) {
                b.damage[li] += dt / std::max(m.failTime, 1e-3f);
                gone = b.damage[li] >= 1.0f;
            }
            if (!gone) continue;
            if (s == 0) b.front++; else b.back--;
            if (b.front > b.back) {
                b.failed = true;
                updateFace(b.axis, b.face);
                std::string where = roomName(b.room);
                switch (b.kind) {
                case BarrierKind::Window:
                    logEvent("win-" + std::to_string(b.opening),
                             (scene ? scene->openings[b.opening].name : std::string("Window")) + " failed (glass out)");
                    break;
                case BarrierKind::Door:
                    logEvent("door-" + std::to_string(b.opening),
                             (scene ? scene->openings[b.opening].name : std::string("Door")) + " burned through");
                    break;
                case BarrierKind::Ceiling: logEvent("ceil-" + where, "Ceiling opened over " + where + " - fire into attic"); break;
                case BarrierKind::Roof: logEvent("roof", "Roof deck burned through - self-venting"); break;
                case BarrierKind::Floor: logEvent("floor-" + where, "Floor burned through above " + where); break;
                case BarrierKind::Soffit: logEvent("soffit", "Soffit failed - eave open to attic"); break;
                case BarrierKind::Wall: logEvent("wall-" + where, "Wall burned through in " + where); break;
                default: logEvent(std::string(barrierKindName(b.kind)) + "-fail", std::string(barrierKindName(b.kind)) + " burned through"); break;
                }
                break;
            }
            // newly exposed layer starts at the old node temperature
            if (s == 0) { L[0] = b.front; } else { L[1] = b.back; }
            if (b.front == b.back) b.T[0] = b.T[1] = std::max(b.T[0], b.T[1]);
            if (b.kind == BarrierKind::Ceiling && s == 0 && !m.combustible)
                logEvent("ceil-gyp-" + roomName(b.room), "Ceiling gypsum falling in " + roomName(b.room));
        }
    }
}

void Solver::gatherStats(float dt) {
    const float V = dx * dx * dx;
    float h = 0, mt = T0;
    int nb = 0;
    double fd = 0;
    for (int c = 0; c < N; ++c) {
        if (blocked[c]) continue;
        fd += (double)Yf[c] * gasDensity(c) * V;
        h += hrr[c] * V;
        if (T[c] > mt) mt = T[c];
        if (hrr[c] > 0) ++nb;
    }
    stats.hrr = h;
    stats.fuelBurned += (double)h * dt / DHC_FUEL;
    stats.fuelInDomain = fd;
    stats.maxGasT = mt;
    stats.burningCells = nb;
    stats.failedWindows = stats.failedCeilings = stats.failedDoors = stats.roofHoles = 0;
    for (const Barrier& b : barriers) {
        if (!b.failed) continue;
        if (b.kind == BarrierKind::Window) ++stats.failedWindows;
        else if (b.kind == BarrierKind::Ceiling) ++stats.failedCeilings;
        else if (b.kind == BarrierKind::Door) ++stats.failedDoors;
        else if (b.kind == BarrierKind::Roof) ++stats.roofHoles;
    }
    (void)dt;
}

} // namespace photon
