// Photon - Phase 1 prototype solver (see solver.h and docs/PROTOTYPE_PHYSICS.md).
#include "sim/solver.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>

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
constexpr float VMAX = 25.0f;
inline float sq(float a) { return a * a; }
inline float p4(float a) { float b = a * a; return b * b; }
} // namespace

const char* barrierKindName(BarrierKind k) {
    switch (k) {
    case BarrierKind::Wall: return "wall";
    case BarrierKind::Ceiling: return "ceiling";
    case BarrierKind::Roof: return "roof";
    case BarrierKind::Soffit: return "soffit";
    case BarrierKind::Gable: return "gable";
    case BarrierKind::Fascia: return "fascia";
    case BarrierKind::Door: return "door";
    case BarrierKind::Window: return "window";
    }
    return "?";
}

Solver::Solver() : pool_(std::make_unique<JobPool>()) {}
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
        const Room* r = scene->roomAt(ox + (i + 0.5f) * dx, oy + (j + 0.5f) * dx);
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
    Yf[c] = std::min(1.0f, Yf[c] + fuelGen / m);
    Ya[c] = std::min(1.0f, Ya[c] + aerosol / m);
    Yp[c] = std::min(1.0f, Yp[c] + sootPot / m);
}

// ---------------------------------------------------------------------------
float Solver::step(float dtMax) {
    auto t0 = std::chrono::steady_clock::now();
    float dt = std::min(dtMax, 0.25f * dx / std::max(maxVel, 0.5f));
    dt = std::clamp(dt, 0.002f, 0.05f);

    pool_->parallelFor(N, [&](int b, int e) {
        for (int c = b; c < e; ++c) { Q[c] = 0; Qr[c] = 0; pilot[c] = 0; }
    }, 4096);
    for (int c : solidCells) radQ[c] = 0;
    for (Barrier& b : barriers) b.radIn[0] = b.radIn[1] = 0;

    stats.pyrolysis = 0;
    injectIgnitions(dt);
    combustion(dt);
    flameRadiation();
    solidsStep(dt);
    barriersStep(dt);
    energyAndExpansion(dt);
    buoyancy(dt);
    advectVelocity(dt);
    project(dt);
    advectScalars(dt);
    gatherStats(dt);
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
        pilot[ig.cell] = 1;
    }
}

// Mixing-limited burning of the generic fuel gas, with a critical flame
// temperature test for ignition and extinction (FDS-style, simplified).
void Solver::combustion(float dt) {
    const float V = dx * dx * dx;
    const float s = DHC_FUEL / DHC_O2;   // kg O2 per kg fuel
    const float frac = 1.0f - std::exp(-dt / TAU_MIX);
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
            float m = P0 / (RS * Tc) * V;
            float dmf = burnable * m * frac;
            float q = dmf * DHC_FUEL;
            float sootMade = yf > 0 ? Yp[c] * (dmf / m) / yf : 0;
            Yf[c] = std::max(0.0f, yf - dmf / m);
            Yo[c] = std::max(0.0f, Yo[c] - s * dmf / m);
            Yp[c] = std::max(0.0f, Yp[c] - sootMade);
            Ys[c] += sootMade;
            Ya[c] *= (1.0f - frac);
            Q[c] += q * (1.0f - CHI_R);
            Qr[c] = q * CHI_R;
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
    const int R = 2;
    const float A = dx * dx;
    struct Target { int kind; int idx; int side; float w; };
    std::mutex mtx;
    std::vector<Target> deposits;
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
    deposits.insert(deposits.end(), local.begin(), local.end());
    }, 16);
    for (const Target& tg : deposits) {
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
    Yf[c] = Ys[c] = Ya[c] = Yp[c] = 0;
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
            float rho = P0 / (RS * Tg);
            float epsG = 1.0f - std::exp(-(KAPPA_SOOT * rho * Ys[g] + KAPPA_GAS) * RAD_PATH);
            float h = H_CONV_MIN + 4.0f * cellSpeed(g);
            float flux = h * (Tg - Ts) + m.emissivity * epsG * SIGMA * (p4(Tg) - p4(Ts));
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
                float mg = P0 / (RS * T[g]) * dx * dx * dx;
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
            if (g[s] < 0) continue;
            float Tg = T[g[s]], Tn = b.T[s];
            float rho = P0 / (RS * Tg);
            float epsG = 1.0f - std::exp(-(KAPPA_SOOT * rho * Ys[g[s]] + KAPPA_GAS) * RAD_PATH);
            float h = H_CONV_MIN + 4.0f * cellSpeed(g[s]);
            flux[s] = h * (Tg - Tn) + m.emissivity * epsG * SIGMA * (p4(Tg) - p4(Tn));
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
                case BarrierKind::Soffit: logEvent("soffit", "Soffit failed - eave open to attic"); break;
                case BarrierKind::Wall: logEvent("wall-" + where, "Wall burned through in " + where); break;
                default: logEvent(std::string(barrierKindName(b.kind)) + "-fail", std::string(barrierKindName(b.kind)) + " burned through"); break;
                }
                break;
            }
            // newly exposed layer starts at the old node temperature
            if (s == 0) { L[0] = b.front; } else { L[1] = b.back; }
            if (b.front == b.back) b.T[0] = b.T[1] = std::max(b.T[0], b.T[1]);
            if (b.kind == BarrierKind::Ceiling && s == 0)
                logEvent("ceil-gyp-" + roomName(b.room), "Ceiling gypsum falling in " + roomName(b.room));
        }
    }
}

void Solver::energyAndExpansion(float dt) {
    const float V = dx * dx * dx;
    const float sFac = RS / (P0 * CP * V * dt);  // div u = q R / (P0 cp)
    pool_->parallelFor(N, [&](int b, int e) {
        for (int c = b; c < e; ++c) {
            if (blocked[c]) { S[c] = 0; continue; }
            float m = P0 / (RS * T[c]) * V;
            T[c] = std::clamp(T[c] + Q[c] / (m * CP), T_MIN, T_MAX);
            S[c] = std::clamp(Q[c] * sFac, -50.0f, 50.0f);
        }
    }, 4096);
}

// Buoyant acceleration on vertical face velocities, g (rho0 - rho) / rho0.
void Solver::buoyancy(float dt) {
    const int sz = nx * ny;
    pool_->parallelFor(nz + 1, [&](int kb, int ke) {
        for (int k = std::max(kb, 1); k < ke; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    int f = zface(i, j, k);
                    if (fz[f] <= 0) continue;
                    float Tlo = T[cell(i, j, k - 1)];
                    float Thi = k < nz ? T[cell(i, j, k)] : T0;
                    float Tf = 0.5f * (Tlo + Thi);
                    w[f] += dt * G * (1.0f - T0 / Tf);
                }
        (void)sz;
    });
}

namespace {
inline float sampleGrid(const std::vector<float>& f, int gx, int gy, int gz, float x, float y, float z) {
    x = std::clamp(x, 0.0f, (float)(gx - 1));
    y = std::clamp(y, 0.0f, (float)(gy - 1));
    z = std::clamp(z, 0.0f, (float)(gz - 1));
    int i = std::min((int)x, gx - 2 < 0 ? 0 : gx - 2), j = std::min((int)y, gy - 2 < 0 ? 0 : gy - 2),
        k = std::min((int)z, gz - 2 < 0 ? 0 : gz - 2);
    float tx = x - i, ty = y - j, tz = z - k;
    auto at = [&](int a, int b, int c) { return f[a + gx * (b + gy * c)]; };
    float c00 = at(i, j, k) * (1 - tx) + at(i + 1, j, k) * tx;
    float c10 = at(i, j + 1, k) * (1 - tx) + at(i + 1, j + 1, k) * tx;
    float c01 = at(i, j, k + 1) * (1 - tx) + at(i + 1, j, k + 1) * tx;
    float c11 = at(i, j + 1, k + 1) * (1 - tx) + at(i + 1, j + 1, k + 1) * tx;
    return (c00 * (1 - ty) + c10 * ty) * (1 - tz) + (c01 * (1 - ty) + c11 * ty) * tz;
}
} // namespace

// Semi-Lagrangian advection of the staggered velocity (positions in cell units).
void Solver::advectVelocity(float dt) {
    const float s = dt / dx;
    // u: grid (nx+1, ny, nz) at (i, j+.5, k+.5)
    auto U = [&](float x, float y, float z) { return sampleGrid(u, nx + 1, ny, nz, x, y - 0.5f, z - 0.5f); };
    auto Vv = [&](float x, float y, float z) { return sampleGrid(v, nx, ny + 1, nz, x - 0.5f, y, z - 0.5f); };
    auto W = [&](float x, float y, float z) { return sampleGrid(w, nx, ny, nz + 1, x - 0.5f, y - 0.5f, z); };
    pool_->parallelFor(nz, [&](int kb, int ke) {
        for (int k = kb; k < ke; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i <= nx; ++i) {
                    int f = xface(i, j, k);
                    if (fx[f] <= 0) { u2_[f] = 0; continue; }
                    float x = (float)i, y = j + 0.5f, z = k + 0.5f;
                    float bx_ = x - s * u[f], by_ = y - s * Vv(x, y, z), bz_ = z - s * W(x, y, z);
                    u2_[f] = U(bx_, by_, bz_);
                }
        for (int k = kb; k < ke; ++k)
            for (int j = 0; j <= ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    int f = yface(i, j, k);
                    if (fy[f] <= 0) { v2_[f] = 0; continue; }
                    float x = i + 0.5f, y = (float)j, z = k + 0.5f;
                    float bx_ = x - s * U(x, y, z), by_ = y - s * v[f], bz_ = z - s * W(x, y, z);
                    v2_[f] = Vv(bx_, by_, bz_);
                }
    });
    pool_->parallelFor(nz + 1, [&](int kb, int ke) {
        for (int k = kb; k < ke; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    int f = zface(i, j, k);
                    if (fz[f] <= 0) { w2_[f] = 0; continue; }
                    float x = i + 0.5f, y = j + 0.5f, z = (float)k;
                    float bx_ = x - s * U(x, y, z), by_ = y - s * Vv(x, y, z), bz_ = z - s * w[f];
                    w2_[f] = W(bx_, by_, bz_);
                }
    });
    u.swap(u2_);
    v.swap(v2_);
    w.swap(w2_);
}

// Pressure projection: sum_f a_f (p_n - p_c) = (div u* - S) dx^2 / dt with
// red-black SOR, open boundaries held at p = 0, warm-started each step.
void Solver::project(float dt) {
    const float inv = dx * dx / dt;
    pool_->parallelFor(nz, [&](int kb, int ke) {
        for (int k = kb; k < ke; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    int c = cell(i, j, k);
                    if (blocked[c]) { rhs_[c] = 0; continue; }
                    int a0 = xface(i, j, k), a1 = a0 + 1, b0 = yface(i, j, k), b1 = yface(i, j + 1, k), c0 = zface(i, j, k),
                        c1 = zface(i, j, k + 1);
                    float div = (fx[a1] * u[a1] - fx[a0] * u[a0] + fy[b1] * v[b1] - fy[b0] * v[b0] + fz[c1] * w[c1] -
                                 fz[c0] * w[c0]) / dx;
                    rhs_[c] = (div - S[c]) * inv;
                }
    });
    const float omega = 1.8f;
    const int rows = ny * nz;
    for (int it = 0; it < pressureIters; ++it) {
        for (int color = 0; color < 2; ++color) {
            pool_->parallelFor(rows, [&](int rb, int re) {
                for (int r = rb; r < re; ++r) {
                    int j = r % ny, k = r / ny;
                    int i0 = (j + k + color) & 1;
                    for (int i = i0; i < nx; i += 2) {
                        int c = cell(i, j, k);
                        if (blocked[c]) continue;
                        int a0 = xface(i, j, k), a1 = a0 + 1, b0 = yface(i, j, k), b1 = yface(i, j + 1, k),
                            c0 = zface(i, j, k), c1 = zface(i, j, k + 1);
                        float fa0 = fx[a0], fa1 = fx[a1], fb0 = fy[b0], fb1 = fy[b1], fc0 = fz[c0], fc1 = fz[c1];
                        float diag = fa0 + fa1 + fb0 + fb1 + fc0 + fc1;
                        if (diag <= 0) continue;
                        float sum = 0;
                        if (i > 0) sum += fa0 * p[c - 1];
                        if (i < nx - 1) sum += fa1 * p[c + 1];
                        if (j > 0) sum += fb0 * p[c - nx];
                        if (j < ny - 1) sum += fb1 * p[c + nx];
                        if (k > 0) sum += fc0 * p[c - nx * ny];
                        if (k < nz - 1) sum += fc1 * p[c + nx * ny];
                        float pn = (sum - rhs_[c]) / diag;
                        p[c] += omega * (pn - p[c]);
                    }
                }
            });
        }
    }
    const float s = dt / dx;
    auto P = [&](int i, int j, int k) -> float {
        if (i < 0 || j < 0 || i >= nx || j >= ny || k >= nz) return 0.0f;  // open boundary
        return p[cell(i, j, k)];
    };
    std::vector<float> vmaxPart(nz + 1, 0.0f);
    pool_->parallelFor(nz, [&](int kb, int ke) {
        for (int k = kb; k < ke; ++k) {
            float vm = 0;
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i <= nx; ++i) {
                    int f = xface(i, j, k);
                    if (fx[f] <= 0) { u[f] = 0; continue; }
                    u[f] = std::clamp(u[f] - s * (P(i, j, k) - P(i - 1, j, k)), -VMAX, VMAX);
                    vm = std::max(vm, std::fabs(u[f]) * fx[f]);
                }
            for (int j = 0; j <= ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    int f = yface(i, j, k);
                    if (fy[f] <= 0) { v[f] = 0; continue; }
                    v[f] = std::clamp(v[f] - s * (P(i, j, k) - P(i, j - 1, k)), -VMAX, VMAX);
                    vm = std::max(vm, std::fabs(v[f]) * fy[f]);
                }
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    int f = zface(i, j, k + 1);
                    if (fz[f] <= 0) { w[f] = 0; continue; }
                    w[f] = std::clamp(w[f] - s * (P(i, j, k + 1) - P(i, j, k)), -VMAX, VMAX);
                    vm = std::max(vm, std::fabs(w[f]) * fz[f]);
                }
            vmaxPart[k] = vm;
        }
    });
    maxVel = *std::max_element(vmaxPart.begin(), vmaxPart.end());
}

// First-order upwind transport (advective form) of the cell scalars. Fluxes
// only pass through open face area, so nothing leaks through intact barriers.
void Solver::advectScalars(float dt) {
    std::vector<float>* fields[6] = {&T, &Yo, &Yf, &Ys, &Ya, &Yp};
    const float amb[6] = {T0, YO2_AMB, 0, 0, 0, 0};
    const float s = dt / dx;
    pool_->parallelFor(nz, [&](int kb, int ke) {
        for (int k = kb; k < ke; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    int c = cell(i, j, k);
                    if (blocked[c]) {
                        for (int q = 0; q < 6; ++q) tmp_[q][c] = (*fields[q])[c];
                        continue;
                    }
                    int fa0 = xface(i, j, k), fa1 = fa0 + 1, fb0 = yface(i, j, k), fb1 = yface(i, j + 1, k),
                        fc0 = zface(i, j, k), fc1 = zface(i, j, k + 1);
                    float win[6] = {fx[fa0] * std::max(u[fa0], 0.0f) * s, fx[fa1] * std::max(-u[fa1], 0.0f) * s,
                                    fy[fb0] * std::max(v[fb0], 0.0f) * s, fy[fb1] * std::max(-v[fb1], 0.0f) * s,
                                    fz[fc0] * std::max(w[fc0], 0.0f) * s, fz[fc1] * std::max(-w[fc1], 0.0f) * s};
                    int nb[6] = {i > 0 ? c - 1 : -1, i < nx - 1 ? c + 1 : -1, j > 0 ? c - nx : -1, j < ny - 1 ? c + nx : -1,
                                 k > 0 ? c - nx * ny : -1, k < nz - 1 ? c + nx * ny : -1};
                    float wsum = win[0] + win[1] + win[2] + win[3] + win[4] + win[5];
                    float scale = wsum > 1.0f ? 1.0f / wsum : 1.0f;
                    for (int q = 0; q < 6; ++q) {
                        const std::vector<float>& F = *fields[q];
                        float fc = F[c], acc = 0;
                        for (int d = 0; d < 6; ++d) {
                            if (win[d] <= 0) continue;
                            float fn = nb[d] >= 0 ? F[nb[d]] : amb[q];
                            acc += win[d] * (fn - fc);
                        }
                        tmp_[q][c] = fc + acc * scale;
                    }
                }
    });
    for (int q = 0; q < 6; ++q) fields[q]->swap(tmp_[q]);
}

void Solver::gatherStats(float dt) {
    const float V = dx * dx * dx;
    float h = 0, mt = T0;
    int nb = 0;
    for (int c = 0; c < N; ++c) {
        if (blocked[c]) continue;
        h += hrr[c] * V;
        if (T[c] > mt) mt = T[c];
        if (hrr[c] > 0) ++nb;
    }
    stats.hrr = h;
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
