// Photon - room-scale thermal radiation (Phase 4). Ray-traced finite-angle
// method in four wavelength bands (Phase 4 step 4):
//
//   band 0  0 - 2.7 um   soot only (gas nearly transparent)
//   band 1  2.7 - 8 um   soot + H2O 2.7/6.3 um and CO2 2.7/4.3 um bands
//   band 2  8 - 14 um    the thermal-imaging window: weak soot, almost no gas
//   band 3  > 14 um      CO2 15 um and H2O rotational bands, strong
//
// Soot absorbs as kappa = C fv / lambda (Rayleigh limit, C = 5.5, fv the soot
// volume fraction); gas absorbs with a per-band coefficient per atmosphere of
// H2O + CO2. Emission in each band is the band's Planck fraction of sigma T^4.
// PROTOTYPE band coefficients (verify against RADCAL wide-band values).
// Surfaces are grey (one emissivity) for now.
//
//  * Every surface that sees gas is a radiating element: each exposed side of
//    a wall/ceiling/floor barrier, each exposed face of a solid fuel cell,
//    each porous fuel cell (needles, trusses; floor coverings face up).
//  * From each element, rays go out over its hemisphere (or the full sphere
//    for porous fuel). Along a ray the gas absorbs and emits with
//    kappa = kappa_soot rho Y_soot + kappa_gas; burning cells emit at least
//    their radiative fraction of the heat release (the FDS flame correction,
//    chi_r q''' / 4 pi), because a 20 cm cell cannot resolve the flame sheet
//    temperature. A ray ends on a wall or solid (its emission) or leaves the
//    domain (ambient).
//  * Everything is written relative to ambient (T^4 - T0^4), so a room at
//    ambient exchanges exactly nothing.
//  * Energy: an element absorbs eps * incident; the gas cells along the ray
//    lose what reached the element from them and gain what they absorbed of
//    the element's own emission. Each element loses eps sigma (T^4 - T0^4)
//    per unit area every step (solidsStep / barriersStep).
//
// Rays are traced every RAD_INTERVAL of simulated time and the resulting
// powers are applied every step until the next trace. Elements are processed
// in a fixed number of blocks, each with its own gas buffer, summed in a fixed
// order, so results do not depend on thread timing.
#include "sim/solver.h"
#include <cmath>

namespace photon {

namespace {
constexpr int RAD_DIRS = 32;            // full sphere; about 16 per hemisphere
constexpr int RAD_BLOCKS = 24;
constexpr float RAD_MAXLEN = 8.0f;      // m
constexpr float PI = 3.14159265f;
constexpr int NB = 4;
const float BAND_EDGE[NB + 1] = {0.0f, 2.7f, 8.0f, 14.0f, 1e9f};   // um
const float BAND_LAMBDA[NB] = {1.5f, 4.5f, 11.0f, 25.0f};          // um, band center for soot
const float BAND_GAS[NB] = {0.05f, 3.0f, 0.1f, 6.0f};               // 1/(m atm) of H2O + CO2 (PROTOTYPE)
constexpr float SOOT_C = 5.5f, SOOT_DENSITY = 1800.0f;
constexpr float T_FLAME_RAD = 1400.0f;                              // K, spectrum of the sub-grid flame's emission

// Fraction of blackbody emission below lambda (um) at T (K): series of Chang & Rhee.
float planckBelow(float lambdaT) {
    if (lambdaT <= 0) return 0;
    if (lambdaT > 1e8f) return 1;
    double x = 14388.0 / lambdaT, s = 0;
    for (int n = 1; n <= 12; ++n) {
        double xn = x * n;
        s += std::exp(-xn) / n * (x * x * x + 3 * x * x / n + 6 * x / (double(n) * n) + 6 / (double(n) * n * n));
    }
    return (float)std::clamp(1.0 - 15.0 / (PI * PI * PI * PI) * s, 0.0, 1.0);
}
void bandFractions(float T, float f[NB]) {
    float prev = 0;
    for (int b = 0; b < NB; ++b) {
        float up = b == NB - 1 ? 1.0f : planckBelow(BAND_EDGE[b + 1] * T);
        f[b] = up - prev;
        prev = up;
    }
}
// band emissive power relative to ambient, W/m2
void bandPower(float T, float out[NB]) {
    static float f0[NB];
    static bool init = false;
    if (!init) { bandFractions(Solver::T0, f0); init = true; }
    float f[NB];
    bandFractions(T, f);
    float a = T * T, a0 = Solver::T0 * Solver::T0;
    for (int b = 0; b < NB; ++b) out[b] = Solver::SIGMA * (f[b] * a * a - f0[b] * a0 * a0);
}

struct Dir { float x, y, z; };
const std::vector<Dir>& directions() {
    static std::vector<Dir> d = [] {
        std::vector<Dir> v;
        const float ga = PI * (3.0f - std::sqrt(5.0f));
        for (int i = 0; i < RAD_DIRS; ++i) {
            float z = 1.0f - 2.0f * (i + 0.5f) / RAD_DIRS;
            float r = std::sqrt(std::max(0.0f, 1 - z * z));
            v.push_back({r * std::cos(ga * i), r * std::sin(ga * i), z});
        }
        return v;
    }();
    return d;
}
inline float p4m(float T) { float a = T * T; return a * a - Solver::T0 * Solver::T0 * Solver::T0 * Solver::T0; }
} // namespace

void Solver::buildRadElements() {
    radElems_.clear();
    const float A = dx * dx;
    // barrier sides facing gas
    for (int bi = 0; bi < (int)barriers.size(); ++bi) {
        const Barrier& b = barriers[bi];
        if (!barrierIntact(bi)) continue;
        for (int s = 0; s < 2; ++s) {
            int g = b.cellOn(s);
            if (g < 0 || blocked[g]) continue;
            int i, j, k;
            int f = b.face, ax = b.axis;
            if (ax == 0) { i = f % (nx + 1); j = (f / (nx + 1)) % ny; k = f / ((nx + 1) * ny); }
            else if (ax == 1) { i = f % nx; j = (f / nx) % (ny + 1); k = f / (nx * (ny + 1)); }
            else { i = f % nx; j = (f / nx) % ny; k = f / (nx * ny); }
            float o[3] = {ox + (i + 0.5f) * dx, oy + (j + 0.5f) * dx, oz + (k + 0.5f) * dx};
            o[ax] -= 0.5f * dx;   // the face plane
            float sg = (g == b.cellB) ? 1.0f : -1.0f;
            RadElem e;
            e.kind = 0; e.idx = bi; e.side = (int8_t)s; e.sphere = false; e.area = A;
            e.n[0] = e.n[1] = e.n[2] = 0; e.n[ax] = sg;
            for (int q = 0; q < 3; ++q) e.o[q] = o[q] + e.n[q] * 1e-3f;
            radElems_.push_back(e);
        }
    }
    // solid fuel faces and porous fuel
    const int off[6] = {-1, 1, -nx, nx, -nx * ny, nx * ny};
    for (int c : solidCells) {
        if (fuelMat[c] < 0) continue;
        int i, j, k;
        cellIJK(c, i, j, k);
        float cc[3] = {ox + (i + 0.5f) * dx, oy + (j + 0.5f) * dx, oz + (k + 0.5f) * dx};
        if (blocked[c]) {
            for (int d = 0; d < 6; ++d) {
                if ((d == 0 && i == 0) || (d == 1 && i == nx - 1) || (d == 2 && j == 0) || (d == 3 && j == ny - 1) ||
                    (d == 4 && k == 0) || (d == 5 && k == nz - 1)) continue;
                int n = c + off[d];
                if (blocked[n] || barrierIntact(barrierOn(c, d))) continue;
                RadElem e;
                e.kind = 1; e.idx = c; e.side = (int8_t)d; e.sphere = false; e.area = A;
                e.n[0] = e.n[1] = e.n[2] = 0; e.n[d >> 1] = (d & 1) ? 1.0f : -1.0f;
                for (int q = 0; q < 3; ++q) e.o[q] = cc[q] + e.n[q] * (0.5f * dx + 1e-3f);
                radElems_.push_back(e);
            }
        } else if (fuelMass[c] + charMass[c] > 0) {
            float total = fuelMass[c] + charMass[c];
            float area = fuelMass0[c] > 0 ? fuelArea[c] * std::cbrt(std::pow(std::max(0.0f, total / fuelMass0[c]), 2.0f)) : 0;
            if (area <= 0) continue;
            RadElem e;
            e.kind = 2; e.idx = c; e.side = 0;
            bool floorCover = itemOf[c] < 0 && (k == 0 || barrierIntact(barrierOn(c, 4)) || (k > 0 && blocked[c - nx * ny]));
            if (floorCover) {   // carpet / vinyl: a surface facing up
                e.sphere = false; e.area = area;
                e.n[0] = e.n[1] = 0; e.n[2] = 1;
                e.o[0] = cc[0]; e.o[1] = cc[1]; e.o[2] = cc[2] - 0.5f * dx + 1e-3f;
            } else {            // fuel spread through the cell: absorbs on A/4, emits on A
                e.sphere = true; e.area = std::min(area, 4.0f * A);   // needles shade each other: a cell intercepts at most its own cross-section from each direction
                e.n[0] = e.n[1] = e.n[2] = 0;
                e.o[0] = cc[0]; e.o[1] = cc[1]; e.o[2] = cc[2];
            }
            radElems_.push_back(e);
        }
    }
    radAbs_.assign(radElems_.size(), 0.0f);
}

float Solver::elemTemp(const RadElem& e) const {
    if (e.kind == 0) return barriers[e.idx].T[e.side];
    return fuelT[e.idx];
}
float Solver::elemEmissivity(const RadElem& e) const {
    if (e.kind == 0) {
        const Barrier& b = barriers[e.idx];
        int layer = e.side == 0 ? b.front : b.back;
        const Assembly& as = lib->assemblies[b.assembly];
        if (layer >= 0 && layer < (int)as.layers.size()) return lib->materials[as.layers[layer].material].emissivity;
        return 0.9f;
    }
    return fuelMat[e.idx] >= 0 ? lib->materials[fuelMat[e.idx]].emissivity : 0.9f;
}

void Solver::traceRadiation() {
    buildRadElements();
    const std::vector<Dir>& dirs = directions();
    const float dW = 4.0f * PI / RAD_DIRS;
    const int nE = (int)radElems_.size();
    if ((int)radBuf_.size() != RAD_BLOCKS) radBuf_.assign(RAD_BLOCKS, std::vector<float>());
    { float warm[4]; bandPower(T0, warm); }   // initialize the ambient table before the threads start
    // per-cell, per-band absorption coefficient and source function (relative to ambient)
    for (int bnd = 0; bnd < NB; ++bnd) { radKap_[bnd].resize(N); radSrc_[bnd].resize(N); }
    std::vector<uint8_t>& flame = radFlame_;
    flame.assign(N, 0);
    float fFlame[NB];
    bandFractions(T_FLAME_RAD, fFlame);
    pool_->parallelFor(N, [&](int b, int e) {
        for (int c = b; c < e; ++c) {
            if (blocked[c]) { for (int q = 0; q < NB; ++q) { radKap_[q][c] = 0; radSrc_[q][c] = 0; } continue; }
            float rho = gasDensity(c);
            float fv = rho * Ys[c] / SOOT_DENSITY;
            float pg = (Yw[c] * 28.96f / 18.02f + Yc[c] * 28.96f / 44.01f) * Pb[c] / P0;   // atm of H2O + CO2
            float Eb[NB];
            bandPower(T[c], Eb);
            float jfTot = hrr[c] > 0 ? CHI_R * hrr[c] / (4.0f * PI) : 0.0f;
            float jTot = 0;
            float k[NB];
            for (int q = 0; q < NB; ++q) {
                k[q] = SOOT_C * fv / (BAND_LAMBDA[q] * 1e-6f) + BAND_GAS[q] * pg + 1e-4f;
                jTot += k[q] * Eb[q] / PI;
            }
            // flame cells: the radiative fraction was already taken from the gas
            // in combustion(), so the gas does not pay again for what it sends
            bool fl = jfTot > jTot;
            flame[c] = fl ? 1 : 0;
            for (int q = 0; q < NB; ++q) {
                float j = fl ? jfTot * fFlame[q] : k[q] * Eb[q] / PI;   // W/m3/sr
                radKap_[q][c] = k[q];
                radSrc_[q][c] = j / k[q];                                // W/m2/sr
            }
        }
    }, 4096);
    pool_->parallelFor(RAD_BLOCKS, [&](int bb, int be) {
        for (int blk = bb; blk < be; ++blk) {
            std::vector<float>& gb = radBuf_[blk];
            gb.assign(N, 0.0f);
            int e0 = (int)((long)nE * blk / RAD_BLOCKS), e1 = (int)((long)nE * (blk + 1) / RAD_BLOCKS);
            for (int ei = e0; ei < e1; ++ei) {
                const RadElem& el = radElems_[ei];
                float eps = elemEmissivity(el);
                float Is[NB];
                bandPower(elemTemp(el), Is);
                for (int q = 0; q < NB; ++q) Is[q] *= eps / PI;   // own emission, relative to ambient
                // hemisphere weights normalized so sum(cos dW) = pi exactly
                float wsum = 0;
                if (!el.sphere)
                    for (const Dir& d : dirs) { float cs = d.x * el.n[0] + d.y * el.n[1] + d.z * el.n[2]; if (cs > 0) wsum += cs * dW; }
                float norm = el.sphere ? 1.0f : (wsum > 0 ? PI / wsum : 0.0f);
                float absorbed = 0;
                for (const Dir& d : dirs) {
                    float w;
                    if (el.sphere) w = dW * 0.25f * el.area;
                    else {
                        float cs = d.x * el.n[0] + d.y * el.n[1] + d.z * el.n[2];
                        if (cs <= 0) continue;
                        w = cs * dW * norm * el.area;
                    }
                    // march (Amanatides-Woo) from the element origin
                    float p[3] = {(el.o[0] - ox) / dx, (el.o[1] - oy) / dx, (el.o[2] - oz) / dx};
                    float dv[3] = {d.x, d.y, d.z};
                    int ci[3] = {(int)std::floor(p[0]), (int)std::floor(p[1]), (int)std::floor(p[2])};
                    const int dim[3] = {nx, ny, nz};
                    if (ci[0] < 0 || ci[1] < 0 || ci[2] < 0 || ci[0] >= nx || ci[1] >= ny || ci[2] >= nz) continue;
                    int stp[3];
                    float tMax[3], tDel[3];
                    for (int a = 0; a < 3; ++a) {
                        if (dv[a] > 1e-9f) { stp[a] = 1; tMax[a] = (ci[a] + 1 - p[a]) / dv[a]; tDel[a] = 1 / dv[a]; }
                        else if (dv[a] < -1e-9f) { stp[a] = -1; tMax[a] = (p[a] - ci[a]) / -dv[a]; tDel[a] = -1 / dv[a]; }
                        else { stp[a] = 0; tMax[a] = 1e30f; tDel[a] = 1e30f; }
                    }
                    float t = 0, tau[NB] = {1, 1, 1, 1}, I[NB] = {0, 0, 0, 0};
                    const float tEnd = RAD_MAXLEN / dx;
                    int c = cell(ci[0], ci[1], ci[2]);
                    // fuel inside a gas cell: its own cell's flame heats it through flame contact (solver.cpp), not here
                    const int skip = el.kind == 2 ? c : -1;
                    float Iend[NB] = {0, 0, 0, 0};
                    for (;;) {
                        int a = tMax[0] < tMax[1] ? (tMax[0] < tMax[2] ? 0 : 2) : (tMax[1] < tMax[2] ? 1 : 2);
                        float tn = std::min(tMax[a], tEnd);
                        float ds = (tn - t) * dx;
                        if (ds > 0 && !blocked[c] && c != skip) {
                            float g = 0;
                            for (int q = 0; q < NB; ++q) {
                                float x = radKap_[q][c] * ds;
                                float att = x < 1e-3f ? x * (1 - 0.5f * x) : 1.0f - std::exp(-x);
                                float fromCell = tau[q] * radSrc_[q][c] * att;   // reaches the element
                                I[q] += fromCell;
                                g += Is[q] * tau[q] * att - (flame[c] ? 0.0f : eps * fromCell);
                                tau[q] *= 1.0f - att;
                            }
                            gb[c] += w * g;   // gains the element's emission, loses what it absorbed
                        }
                        t = tn;
                        if (t >= tEnd || (tau[0] < 1e-3f && tau[1] < 1e-3f && tau[2] < 1e-3f && tau[3] < 1e-3f)) break;
                        // cross the face into the next cell along axis a
                        int dirIdx = 2 * a + (stp[a] > 0 ? 1 : 0);
                        int bar = barrierOn(c, dirIdx);
                        if (barrierIntact(bar)) {
                            const Barrier& br = barriers[bar];
                            int side = br.cellOn(0) == c ? 0 : 1;
                            bandPower(br.T[side], Iend);
                            for (int q = 0; q < NB; ++q) Iend[q] *= 0.9f / PI;
                            break;
                        }
                        ci[a] += stp[a];
                        if (ci[a] < 0 || ci[a] >= dim[a]) break;   // open boundary or ground: ambient
                        int n = cell(ci[0], ci[1], ci[2]);
                        if (blocked[n]) {
                            bandPower(fuelT[n], Iend);
                            for (int q = 0; q < NB; ++q) Iend[q] *= 0.9f / PI;
                            break;
                        }
                        c = n;
                        tMax[a] += tDel[a];
                    }
                    float It = 0;
                    for (int q = 0; q < NB; ++q) It += I[q] + tau[q] * Iend[q];
                    absorbed += eps * w * It;
                }
                radAbs_[ei] = absorbed;   // W
            }
        }
    }, 1);
    radGas_.assign(N, 0.0f);
    for (int blk = 0; blk < RAD_BLOCKS; ++blk) {
        const std::vector<float>& gb = radBuf_[blk];
        pool_->parallelFor(N, [&](int b, int e) {
            for (int c = b; c < e; ++c) radGas_[c] += gb[c];
        }, 8192);
    }
}

// Apply the last trace's powers for this step (J), retracing when due.
void Solver::radiationStep(float dt) {
    if (!radiationOn || adiabaticSurfaces) return;
    radClock_ -= dt;
    if (radClock_ <= 0 || radElems_.empty()) {
        // nothing hot anywhere: skip the trace
        bool hot = stats.maxGasT > T0 + 30.0f;
        for (int c : solidCells) if (fuelT[c] > T0 + 30.0f) { hot = true; break; }
        if (hot) traceRadiation();
        else { radElems_.clear(); radAbs_.clear(); radGas_.assign(N, 0.0f); }
        radClock_ = RAD_INTERVAL;
    }
    for (size_t q = 0; q < radElems_.size(); ++q) {
        const RadElem& e = radElems_[q];
        float E = radAbs_[q] * dt;
        if (e.kind == 0) {
            if (e.idx < (int)barriers.size()) barriers[e.idx].radIn[e.side] += E;
        } else if (e.idx < N && fuelMat[e.idx] >= 0) radQ[e.idx] += E;
    }
    if ((int)radGas_.size() == N)
        pool_->parallelFor(N, [&](int b, int e) {
            for (int c = b; c < e; ++c) if (!blocked[c]) Q[c] += radGas_[c] * dt;
        }, 4096);
}

} // namespace photon
