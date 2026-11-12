// Photon - room-scale thermal radiation (Phase 4 first piece, needed for
// flashover). Ray-traced finite-angle method, grey:
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
constexpr float KAPPA_SOOT_R = 1200.0f; // m2/kg, grey soot absorption (prototype; Phase 4 data job: spectral)
constexpr float KAPPA_GAS_R = 0.15f;    // 1/m, grey CO2/H2O band absorption (prototype)
constexpr float PI = 3.14159265f;

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
                e.sphere = true; e.area = area;
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
    // per-cell absorption coefficient and source function (relative to ambient)
    std::vector<float>& kap = tmp_[10];
    std::vector<float>& src = tmp_[11];
    std::vector<uint8_t>& flame = radFlame_;
    flame.assign(N, 0);
    pool_->parallelFor(N, [&](int b, int e) {
        for (int c = b; c < e; ++c) {
            if (blocked[c]) { kap[c] = 0; src[c] = 0; continue; }
            float rho = gasDensity(c);
            float k = KAPPA_SOOT_R * rho * Ys[c] + KAPPA_GAS_R;
            float j = k * SIGMA * p4m(T[c]) / PI;                     // W/m3/sr
            float jf = hrr[c] > 0 ? CHI_R * hrr[c] / (4.0f * PI) : 0.0f;
            // flame cells: the radiative fraction was already taken from the gas
            // in combustion(), so the gas does not pay again for what it sends
            flame[c] = jf > j ? 1 : 0;
            j = std::max(j, jf);
            kap[c] = k;
            src[c] = j / k;                                           // W/m2/sr
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
                float Is = eps * SIGMA * p4m(elemTemp(el)) / PI;   // own emission, relative to ambient
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
                    float t = 0, tau = 1, I = 0;
                    const float tEnd = RAD_MAXLEN / dx;
                    int c = cell(ci[0], ci[1], ci[2]);
                    // fuel inside a gas cell: its own cell's flame heats it through flame contact (solver.cpp), not here
                    const int skip = el.kind == 2 ? c : -1;
                    float Iend = 0;
                    for (;;) {
                        int a = tMax[0] < tMax[1] ? (tMax[0] < tMax[2] ? 0 : 2) : (tMax[1] < tMax[2] ? 1 : 2);
                        float tn = std::min(tMax[a], tEnd);
                        float ds = (tn - t) * dx;
                        if (ds > 0 && !blocked[c] && c != skip) {
                            float x = kap[c] * ds;
                            float att = x < 1e-3f ? x * (1 - 0.5f * x) : 1.0f - std::exp(-x);
                            float fromCell = tau * src[c] * att;                 // reaches the element
                            I += fromCell;
                            gb[c] += w * (Is * tau * att - (flame[c] ? 0.0f : eps * fromCell));   // gains the element's emission, loses what it absorbed
                            tau *= 1.0f - att;
                        }
                        t = tn;
                        if (t >= tEnd || tau < 1e-3f) break;
                        // cross the face into the next cell along axis a
                        int dirIdx = 2 * a + (stp[a] > 0 ? 1 : 0);
                        int bar = barrierOn(c, dirIdx);
                        if (barrierIntact(bar)) {
                            const Barrier& b = barriers[bar];
                            int side = b.cellOn(0) == c ? 0 : 1;
                            Iend = 0.9f * SIGMA * p4m(b.T[side]) / PI;
                            break;
                        }
                        ci[a] += stp[a];
                        if (ci[a] < 0 || ci[a] >= dim[a]) break;   // open boundary or ground: ambient
                        int n = cell(ci[0], ci[1], ci[2]);
                        if (blocked[n]) { Iend = 0.9f * SIGMA * p4m(fuelT[n]) / PI; break; }
                        c = n;
                        tMax[a] += tDel[a];
                    }
                    I += tau * Iend;
                    absorbed += eps * w * I;
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
