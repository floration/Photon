// Photon - Phase 1 prototype solver (see solver.h and docs/PROTOTYPE_PHYSICS.md).
#include "sim/solver.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace photon {

namespace {
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

void Solver::addGasSpecies(int c, float fuelGen, float aerosol, float sootPot, float ait) {
    float m = gasDensity(c) * dx * dx * dx;
    float madd = fuelGen + aerosol;
    if (madd <= 0) return;
    float mt = m + madd, sc = m / mt;
    Yo[c] *= sc; Ys[c] *= sc; Yc[c] *= sc; Yw[c] *= sc; Yco[c] *= sc;
    Yai[c] = Yai[c] * sc + fuelGen * ait / mt;
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
    radiationStep(dt);     mark(2);
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
            // ignition: a pilot flame, a burning neighbor, or the local fuel
            // mixture's own auto-ignition temperature (mass-weighted over the
            // gases that made it, data/gases.txt)
            float ait = yf > 1e-9f ? Yai[c] / yf : T_AUTO;
            bool ign = pilot[c] || Tc > ait;
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
            // extinction (FDS-style critical flame temperature): the fuel burns
            // only if fuel plus the air carrying the oxygen it needs (with that
            // air's diluents) would reach T_CRIT; vitiated or cold mixtures don't
            float tAd = Tc + DHC_FUEL / (CP * (1.0f + S_O2 / std::max(Yo[c], 1e-4f)));
            if (!pilot[c] && tAd < T_CRIT) continue;
            float m = gasDensity(c) * V;
            float dmf = burnable * m * frac;
            float q = dmf * DHC_FUEL;
            float sootMade = yf > 0 ? Yp[c] * (dmf / m) / yf : 0;   // mass fraction
            float coMade = CO_YIELD * dmf / m;
            float co2Made = std::max(0.0f, (C_FRAC * dmf / m - sootMade - (12.0f / 28.0f) * coMade) * (44.0f / 12.0f));
            float o2Used = s * dmf / m - (32.0f / 12.0f) * sootMade - (16.0f / 28.0f) * coMade;
            Yf[c] = std::max(0.0f, yf - dmf / m);
            Yai[c] = Yf[c] * ait;
            Yo[c] = std::max(0.0f, Yo[c] - o2Used);
            Yp[c] = std::max(0.0f, Yp[c] - sootMade);
            Ys[c] += sootMade;
            Yco[c] += coMade;
            Yc[c] += co2Made;
            Yw[c] += Y_H2O * dmf / m;
            Ya[c] *= (1.0f - frac);
            Q[c] += q * (1.0f - chiR);   // the radiative fraction leaves the flame at once (FDS practice);
            Qr[c] = q * chiR;             // radiation.cpp carries it to the surfaces
            hrr[c] = q / (dt * V);
            litNew[c] = q > 1e-3f ? 1.0f : 0.0f;
        }
    }, 2048);
    pool_->parallelFor(N, [&](int b, int e) {
        for (int c = b; c < e; ++c) lit[c] = litNew[c] > 0.5f;
    }, 4096);
}

// Gas-to-surface heating. Where the adjacent cell is flaming, a 20 cm cell's
// mean temperature is far below the flame's, so the flame-covered part of the
// surface gets the flux of a turbulent flame instead (sub-grid flame heat
// feedback, MODELED: flame 1200 K, emissivity 0.3, h 15 W/m2K, about
// 30-50 kW/m2 to a fuel at its pyrolysis temperature; typical measured
// burning-surface fluxes of furniture, SFPE Handbook ch. on flame spread,
// verify). Flame coverage grows with the cell's heat release rate and is full
// at 1.2 MW/m3. The gas cell pays for the energy.
float Solver::surfaceHeating(int g, float Tg, float Ts, float h, float area) const {
    constexpr float T_FLAME = 1200.0f, EPS_FLAME = 0.3f, H_FLAME = 15.0f, HRRPUV_FULL = 1.2e6f;
    constexpr float FEEDBACK_MAX = 0.3f;   // a flame gives at most this share of its heat back to the fuel beside it
    float conv = h * (Tg - Ts);
    float phi = pilot[g] ? 1.0f : std::min(1.0f, hrr[g] / HRRPUV_FULL);
    if (phi <= 0 || Ts >= T_FLAME) return conv;
    float flame = H_FLAME * (T_FLAME - Ts) + EPS_FLAME * SIGMA * (p4(T_FLAME) - p4(Ts));
    float cap = FEEDBACK_MAX * hrr[g] * dx * dx * dx / std::max(area, 1e-4f);   // W/m2 the flame can afford
    flame = std::min(flame, std::max(conv, cap));
    return (1 - phi) * conv + phi * std::max(conv, flame);
}

// Implicit (backward Euler) 1D conduction from the exposed surface into a
// solid: node thicknesses grow geometrically from 0.5 mm (about 25 mm deep in
// total, deeper than the heat penetrates in the minutes before a surface
// ignites), flux boundary at the surface, adiabatic at the back. Properties
// are the material's (constant for now; temperature-dependent data is a
// Phase 4 data job).
float Solver::conductSlab(int c, const Material& m, float flux, float dt) {
    constexpr int n = SLAB_NODES;
    static const float dz[n] = {0.0005f, 0.0008f, 0.0012f, 0.0018f, 0.0027f, 0.0040f, 0.0060f, 0.0090f};
    float* Tn = &slab[(size_t)c * n];
    // per-node material: the upholstery cover first, then the cell's own fuel
    float kn[n], rcn[n], depth = 0;
    const Material* cov = (c < (int)coverMat.size() && coverMat[c] >= 0) ? &lib->materials[coverMat[c]] : nullptr;
    for (int i = 0; i < n; ++i) {
        const Material& mi = (cov && depth < coverLeft[c]) ? *cov : m;
        kn[i] = std::max(1e-3f, mi.conductivity);
        rcn[i] = std::max(1.0f, mi.density * mi.specificHeat);
        depth += dz[i];
    }
    const float k = kn[0];
    float a[n], b[n], cc[n], d[n];
    for (int i = 0; i < n; ++i) {
        float cap = rcn[i] * dz[i] / dt;
        float gl = i > 0 ? 1.0f / (0.5f * dz[i - 1] / kn[i - 1] + 0.5f * dz[i] / kn[i]) : 0.0f;
        float gr = i < n - 1 ? 1.0f / (0.5f * dz[i] / kn[i] + 0.5f * dz[i + 1] / kn[i + 1]) : 0.0f;
        a[i] = -gl;
        cc[i] = -gr;
        b[i] = cap + gl + gr;
        d[i] = cap * Tn[i] + (i == 0 ? flux : 0.0f);
    }
    for (int i = 1; i < n; ++i) {   // Thomas algorithm
        float w = a[i] / b[i - 1];
        b[i] -= w * cc[i - 1];
        d[i] -= w * d[i - 1];
    }
    Tn[n - 1] = d[n - 1] / b[n - 1];
    for (int i = n - 2; i >= 0; --i) Tn[i] = (d[i] - cc[i] * Tn[i + 1]) / b[i];
    for (int i = 0; i < n; ++i) Tn[i] = std::clamp(Tn[i], 200.0f, 2300.0f);
    // the first node's mean sits half a node in; the surface is hotter by the
    // flux across that half node
    return std::clamp(Tn[0] + flux * 0.5f * dz[0] / k, 200.0f, 2300.0f);
}

// First-order Arrhenius pyrolysis in each slab node. With the FDS User Guide
// construction from the peak temperature Tp of a 5 K/min TGA run and the
// reaction's width dT: r_p = 2 beta / dT, E/R = e r_p Tp^2 / beta, and
// k(T) = e r_p exp((E/R)(1/Tp - 1/T)). The reaction absorbs the material's heat
// of gasification. When the surface node is spent the profile shifts inward
// (the surface recedes into the block).
float Solver::pyrolyzeSlab(int c, const Material& m, float area, float dt) {
    constexpr int n = SLAB_NODES;
    static const float dz[n] = {0.0005f, 0.0008f, 0.0012f, 0.0018f, 0.0027f, 0.0040f, 0.0060f, 0.0090f};
    const float beta = 5.0f / 60.0f, e = 2.718281828f;
    float* Tn = &slab[(size_t)c * n];
    float* F = &slabF[(size_t)c * n];
    const Material* cov = (c < (int)coverMat.size() && coverMat[c] >= 0) ? &lib->materials[coverMat[c]] : nullptr;
    double dm = 0;
    float depth = 0;
    for (int i = 0; i < n; ++i) {
        const Material& mi = (cov && depth < coverLeft[c]) ? *cov : m;
        depth += dz[i];
        if (F[i] <= 0 || Tn[i] < mi.pyrolysisTemp - 3.0f * mi.pyrolysisRange) continue;
        const float rp = 2.0f * beta / std::max(10.0f, mi.pyrolysisRange);
        const float EoR = e * rp * mi.pyrolysisTemp * mi.pyrolysisTemp / beta;
        float k = e * rp * std::exp(std::min(30.0f, EoR * (1.0f / mi.pyrolysisTemp - 1.0f / Tn[i])));
        float df = F[i] * (1.0f - std::exp(-k * dt));
        F[i] -= df;
        dm += (double)mi.density * dz[i] * df * area;
        Tn[i] -= df * mi.heatOfGasification / std::max(1.0f, mi.specificHeat);
    }
    if (F[0] < 0.02f && fuelMass[c] > dm + 1e-4) {   // recede: shift the profile one node inward
        for (int i = 0; i < n - 1; ++i) { Tn[i] = Tn[i + 1]; F[i] = F[i + 1]; }
        F[n - 1] = 1.0f;
        if (cov) coverLeft[c] = std::max(0.0f, coverLeft[c] - dz[0]);
    }
    return (float)dm;
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
    Yf[c] = Ys[c] = Ya[c] = Yp[c] = Yco[c] = Yai[c] = 0;
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
        // Two parts of the surface (sub-grid flame spread, MODELED, docs/FLASHOVER.md):
        //  - the burning part (burnFrac) sits at the pyrolysis temperature; the
        //    flame beside it, the radiation it receives and its own emission set
        //    how fast it gasifies (q''_net / heat of gasification);
        //  - the unburned part heats up (lumped node Ts). It ignites when it
        //    reaches the pyrolysis temperature, or when a flame touching it has
        //    delivered the energy to raise a thin surface layer to that point.
        const float Tp = m.pyrolysisTemp;
        const float bf = m.combustible ? burnFrac[c] : 0.0f;
        float yoAvg = 0, Pburn = 0, flameTouch = 0;
        for (int q = 0; q < ng; ++q) {
            int g = gas[q];
            float Tg = T[g];
            float h = H_CONV_MIN + 4.0f * cellSpeed(g);
            yoAvg += Yo[g];
            if (adiabaticSurfaces) continue;
            // unburned part: convection (and flame contact); emission leaves along the rays
            // porous fuel radiates from its outer layer only (same shading cap as radiation.cpp)
            float radFrac = blocked[c] ? 1.0f : std::min(1.0f, 4.0f * A / std::max(area, 1e-6f));
            float emitU = radiationOn ? radFrac * m.emissivity * SIGMA * (p4(Ts) - p4(T0)) : 0.0f;
            float convU = h * (Tg - Ts);
            Qs += (1 - bf) * (convU - emitU) * perArea * dt;
            Q[g] -= (1 - bf) * convU * perArea * dt;
            // contact flux on the touched patch (not charged to the gas: the patch is small)
            flameTouch = std::max(flameTouch, surfaceHeating(g, Tg, Ts, h, 1e-4f) - convU);
            // burning part: heated by its flame at the pyrolysis temperature
            if (bf > 0) {
                // burning area: for fuel spread through a cell (needles) only the
                // outer layer faces the flame (they shield each other): at most
                // half the area of the cell's six faces (PROTOTYPE, tuned to NIST dry-tree peak HRR)
                float areaB = bf * (blocked[c] ? perArea : std::min(perArea, 3.0f * A));
                float hb = surfaceHeating(g, Tg, Tp, h, 1e-4f);
                float emitB = radiationOn ? m.emissivity * SIGMA * (p4(Tp) - p4(T0)) : 0.0f;
                Pburn += areaB * (hb - emitB);
                // the flame over a burning surface is fed by that surface: the gas
                // cell pays for the feedback up to half of its heat release
                float pay = std::min(areaB * hb, std::max(0.0f, areaB * h * (Tg - Tp)) + 0.5f * hrr[g] * A * dx);
                Q[g] -= pay * dt;
            }
        }
        if (ng > 0) yoAvg /= ng;
        // porous fuel is also touched by flames in the cells around it (a flame
        // below a tree's lowest branches)
        if (!blocked[c] && !adiabaticSurfaces) {
            for (int d = 0; d < 6; ++d) {
                if ((d == 0 && i == 0) || (d == 1 && i == nx - 1) || (d == 2 && j == 0) || (d == 3 && j == ny - 1) ||
                    (d == 4 && k == 0) || (d == 5 && k == nz - 1)) continue;
                int n = c + off[d];
                if (blocked[n] || barrierIntact(barrierOn(c, d))) continue;
                float h = H_CONV_MIN + 4.0f * cellSpeed(n);
                flameTouch = std::max(flameTouch, surfaceHeating(n, T[n], Ts, h, 1e-4f) - h * (T[n] - Ts));
            }
        }
        // radiation and conduction split between the parts
        float radIn = radQ[c];
        Qs -= bf * radIn;
        Pburn += bf * radIn / dt;
        float slabDm = 0;
        if (blocked[c] && ng > 0) {
            // thermally thick solid: everything the surface absorbs (the burning
            // part's flame included) conducts inward; Arrhenius kinetics in each
            // node decide how fast it gasifies
            // the unburned surface heats through the slab (thermal inertia sets
            // ignition and spread); kinetics gasify it as it heats. The flaming
            // patch gasifies directly with the heat it receives (below).
            float aU = std::max((1.0f - bf) * area, 1e-4f);
            Ts = conductSlab(c, m, bf < 0.999f ? Qs / (aU * dt) : 0.0f, dt);
            if (m.combustible && fuelMass[c] > 0) slabDm = pyrolyzeSlab(c, m, (1.0f - bf) * area, dt);
        } else {
            Ts += Qs / C;
        }
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
        float qIn = std::max(0.0f, radIn) / std::max(area, 1e-4f) / dt;   // W/m2 radiation received this step
        if (m.combustible && fuelMass[c] > 0 && ng > 0) {
            // ignition of the unburned part
            bool ignite = false;
            if (Ts >= Tp) { if (!blocked[c]) Ts = Tp; ignite = true; }   // thin fuel: excess heat goes on inward
            if (flameTouch > 0) {
                ignE[c] += flameTouch * dt;
                float need = m.density * m.surfaceDepth * m.specificHeat * (Tp - T0);   // J/m2 to heat the surface layer
                if (ignE[c] >= need) ignite = true;
            }
            if (ignite && burnFrac[c] <= 0) {
                burnFrac[c] = std::min(0.05f, 0.01f / std::max(area, 1e-4f));   // a first burning patch of ~100 cm2
                // Kerber's flashover indicator: the floor covering ignites
                // (only when heat from the room did it, not a flame touching it)
                if (porous[c] && itemOf[c] < 0 && scene && m.spreadUp < 0.01f && flameTouch <= 0) {
                    int i3, j3, k3;
                    cellIJK(c, i3, j3, k3);
                    const Room* r = scene->roomAt(ox + (i3 + 0.5f) * dx, oy + (j3 + 0.5f) * dx, oz + (k3 + 0.5f) * dx);
                    if (r) logEvent("floor-ign-" + r->name, "Flashover: floor covering ignites in " + r->name);
                }
            }
        }
        {
            static int dbg = std::getenv("PHOTON_DEBUG_CELL") ? std::atoi(std::getenv("PHOTON_DEBUG_CELL")) : -1;
            if (c == dbg) std::fprintf(stderr, "t=%.2f c=%d Ts=%.0f bf=%.4f ignE=%.0f touch=%.0f Pburn=%.1f hrr=%.0f pilot=%d Tg=%.0f fuel=%.4f ng=%d g0=%d g1=%d pil0=%d hrr0=%.0f mat=%s\n", time, c, Ts, burnFrac[c], ignE[c], flameTouch, Pburn, hrr[c], pilot[c], T[c], fuelMass[c], ng, ng > 0 ? gas[0] : -1, ng > 1 ? gas[1] : -1, ng > 0 ? pilot[gas[0]] : -1, ng > 0 ? hrr[gas[0]] : 0.0f, m.id.c_str(), (int)blocked[c], (int)porous[c], (int)itemOf[c]);
        }
        float gasified = slabDm + (bf > 0 && Pburn > 0 ? Pburn * dt / m.heatOfGasification : 0.0f);
        if (m.combustible && fuelMass[c] > 0 && gasified > 0 && ng > 0) {
            float dm = std::min(fuelMass[c], gasified);
            fuelMass[c] -= dm;
            if (m.charYield < 0.99f) charMass[c] += dm * m.charYield / (1.0f - m.charYield);
            float gen = dm * (1.0f - m.aerosolYield) * m.heatOfCombustion / DHC_FUEL;
            for (int q = 0; q < ng; ++q)
                addGasSpecies(gas[q], gen / ng, dm * m.aerosolYield / ng, dm * m.sootYield / ng, matAIT[fuelMat[c]]);
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
        if (burnFrac[c] > 0 && burnFrac[c] < 1) {
            int i2, j2, k2;
            cellIJK(c, i2, j2, k2);
            bool fromBelow = k2 > 0 && (hrr[c - nx * ny] > 0 || burnFrac[c - nx * ny] >= 0.5f);
            float v = fromBelow ? m.spreadUp : m.spreadLateral;
            // strong radiation from a hot layer preheats the surface ahead of the
            // flame: spread speeds up with the square of the excess over ~15 kW/m2
            float boost = std::max(1.0f, qIn / 15000.0f);
            burnFrac[c] = std::min(1.0f, burnFrac[c] + dt * v * boost * boost / dx);
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
            (void)rho;
            float h = H_CONV_MIN + 4.0f * cellSpeed(g[s]);
            float emit = radiationOn ? m.emissivity * SIGMA * (p4(Tn) - p4(T0)) : 0.0f;
            float conv = adiabaticSurfaces ? 0.0f : surfaceHeating(g[s], Tg, Tn, h, A);
            flux[s] = adiabaticSurfaces ? 0.0f : conv - emit;
            Q[g[s]] -= conv * A * dt;
        }
        float e0 = (flux[0] * A * dt + b.radIn[0]) / A, e1 = (flux[1] * A * dt + b.radIn[1]) / A;  // J/m2
        // 1D implicit conduction through the intact layers [front, back]. Each
        // layer gets 1-3 nodes by thickness; layers given only as an R-value
        // (stud cavities, batts) conduct through that R. Surfaces take the
        // step's absorbed energy (convection, flame, radiation, emission).
        {
            constexpr int MAXN = MAX_LAYERS * 3;
            float cap[MAXN], gR[MAXN], Tn[MAXN];
            int lay[MAXN], sub[MAXN], nn = 0;
            float prevHalfR = 0;
            for (int l = L[0]; l <= L[1]; ++l) {
                const Layer& ly = as.layers[l];
                const Material& ml = lib->materials[ly.material];
                int nl = ly.thickness < 0.003f ? 1 : ly.thickness < 0.02f ? 2 : 3;
                float am = ml.combustible ? std::max(b.mass[l], 0.01f) : std::max(lib->arealMass(ly), 0.01f);
                float Rl = std::max(lib->layerR(ly), 1e-5f);
                for (int q = 0; q < nl; ++q) {
                    cap[nn] = std::max(50.0f, am / nl * ml.specificHeat);
                    lay[nn] = l;
                    sub[nn] = q;
                    Tn[nn] = b.lt[l][q];
                    // conductance to the previous node: half of each node's resistance
                    if (nn > 0) gR[nn - 1] = 1.0f / std::max(prevHalfR + 0.5f * Rl / nl, 1e-5f);
                    prevHalfR = 0.5f * Rl / nl;
                    ++nn;
                }
            }
            float aa[MAXN], bb[MAXN], cc[MAXN], dd[MAXN];
            for (int i = 0; i < nn; ++i) {
                float gl = i > 0 ? gR[i - 1] : 0.0f, gr = i < nn - 1 ? gR[i] : 0.0f;
                aa[i] = -gl * dt;
                cc[i] = -gr * dt;
                bb[i] = cap[i] + (gl + gr) * dt;
                dd[i] = cap[i] * Tn[i] + (i == 0 ? e0 : 0.0f) + (i == nn - 1 ? e1 : 0.0f);
            }
            for (int i = 1; i < nn; ++i) {
                float w = aa[i] / bb[i - 1];
                bb[i] -= w * cc[i - 1];
                dd[i] -= w * dd[i - 1];
            }
            Tn[nn - 1] = dd[nn - 1] / bb[nn - 1];
            for (int i = nn - 2; i >= 0; --i) Tn[i] = (dd[i] - cc[i] * Tn[i + 1]) / bb[i];
            for (int i = 0; i < nn; ++i) b.lt[lay[i]][sub[i]] = std::clamp(Tn[i], 200.0f, 2300.0f);
            b.T[0] = b.lt[lay[0]][sub[0]];
            b.T[1] = b.lt[lay[nn - 1]][sub[nn - 1]];
            C[0] = cap[0];
            C[1] = cap[nn - 1];
        }
        const int f0 = b.front, k0 = b.back;
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
                        addGasSpecies(gc, gen, dmA * m.aerosolYield, dmA * m.sootYield, matAIT[as.layers[li].material]);
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
            // the newly exposed layer keeps its own computed temperature profile
            if (s == 0) { L[0] = b.front; } else { L[1] = b.back; }
            if (b.kind == BarrierKind::Ceiling && s == 0 && !m.combustible)
                logEvent("ceil-gyp-" + roomName(b.room), "Ceiling gypsum falling in " + roomName(b.room));
        }
        if (b.failed) continue;
        // sync the surface nodes with what the layer processes did
        auto lastNode = [&](int l) { float t = as.layers[l].thickness; return t < 0.003f ? 0 : t < 0.02f ? 1 : 2; };
        if (b.front == f0) b.lt[f0][0] = b.T[0]; else b.T[0] = b.lt[b.front][0];
        if (b.back == k0) { if (b.front != b.back || b.front == f0) b.lt[k0][lastNode(k0)] = b.T[1]; }
        else b.T[1] = b.lt[b.back][lastNode(b.back)];
        if (b.front == b.back && lastNode(b.front) == 0) b.T[1] = b.T[0];
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
    float ms = T0;
    int hc = -1;
    for (int c : solidCells) if (fuelT[c] > ms) { ms = fuelT[c]; hc = c; }
    stats.maxSolidT = ms;
    stats.hotCell = hc;
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
