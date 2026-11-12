// Photon - gas phase (Phase 3). Equations and differences from FDS are in
// docs/GAS_PHASE.md. Summary of one time step (forward Euler, operator split):
//
//   1. Deardorff LES eddy viscosity from the resolved velocity.
//   2. Heat conduction (molecular + turbulent) into the cell heat budget Q.
//   3. Energy: T rises by Q / (m cp); the velocity divergence D the heat and
//      any added gas require (low Mach number constraint).
//   4. Momentum: limited upwind advection, eddy viscosity, buoyancy from the
//      full density difference, baroclinic correction.
//   5. Leakage: crack flow by the orifice equation; background pressure of
//      each closed zone solved implicitly against its leaks.
//   6. Pressure: conjugate gradient Poisson solve so div u = D (minus the
//      zone's pressure rise term); open boundaries at ambient pressure.
//   7. Transport of T and species: second order, Superbee limited, plus
//      turbulent diffusion; result bounded by its neighbors.
#include "sim/solver.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

namespace photon {

namespace {
constexpr float PR_T = 0.5f;      // turbulent Prandtl number (FDS default)
constexpr float SC_T = 0.5f;      // turbulent Schmidt number (FDS default)
constexpr float C_NU = 0.1f;      // Deardorff model constant (FDS default)
constexpr float CD_LEAK = 1.0f;   // leak areas are effective leakage areas (discharge coefficient folded in)
constexpr float LEAK_VMAX = 40.0f;
constexpr float D_MAX = 50.0f;    // 1/s, cap on the divergence source (stability guard)
constexpr float VMAX = 40.0f;
constexpr float T_MIN = 200.0f, T_MAX = 2300.0f;

inline float vanLeer(float a, float b) { return a * b > 0 ? 2 * a * b / (a + b) : 0.0f; }
inline float superbee(float r) { return std::max({0.0f, std::min(2 * r, 1.0f), std::min(r, 2.0f)}); }
inline float nuMol(float T) { return 1.5e-5f * std::pow(T / 293.15f, 1.7f); }
inline float kMol(float T) { return 0.0258f * std::pow(T / 293.15f, 0.8f); }

// Deterministic parallel sum: one partial per slice, summed in order.
template <class F>
double sumSlices(JobPool& pool, int nz, F f) {
    std::vector<double> part(nz, 0.0);
    pool.parallelFor(nz, [&](int kb, int ke) {
        for (int k = kb; k < ke; ++k) part[k] = f(k);
    });
    double s = 0;
    for (double v : part) s += v;
    return s;
}
} // namespace

float Solver::windAt(int axis, int side, float z) const {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    if (wind.speed <= 0 || axis == 2) return nan;
    float th = wind.fromDeg * 3.14159265f / 180.0f;
    float U = wind.speed * std::pow(std::max(z, 0.05f) / 10.0f, wind.exponent);
    float comp = axis == 0 ? -std::sin(th) * U : -std::cos(th) * U;
    if (side == 0 && comp > 0) return comp;   // enters through the low face
    if (side == 1 && comp < 0) return comp;   // enters through the high face
    return nan;
}

float Solver::stableDt(float dtMax) const {
    float dt = std::min(dtMax, profile.dtMax);
    dt = std::min(dt, profile.cfl * dx / std::max(maxVel, 0.1f));
    float alpha = maxNu * (1.0f + 1.0f / PR_T);   // momentum + heat/species diffusion
    if (alpha > 0) dt = std::min(dt, 0.9f * dx * dx / (6.0f * alpha));
    if (maxDiv > 0) dt = std::min(dt, 0.25f / maxDiv);   // a cell may not push out more than a quarter of its gas per step
    return std::max(dt, 1e-3f);
}

int Solver::addBurner(const Burner& b0) {
    Burner b = b0;
    b.cells.clear();
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            float x = ox + (i + 0.5f) * dx, y = oy + (j + 0.5f) * dx;
            if (x < b.x0 || x > b.x1 || y < b.y0 || y > b.y1) continue;
            int c = cellAt(x, y, b.z + 0.5f * dx);
            if (c >= 0 && !blocked[c]) b.cells.push_back(c);
        }
    if (b.cells.empty()) {
        int c = cellAt(0.5f * (b.x0 + b.x1), 0.5f * (b.y0 + b.y1), b.z + 0.5f * dx);
        if (c < 0 || blocked[c]) return -1;
        b.cells.push_back(c);
    }
    burners.push_back(b);
    events.push_back({time, (b.label.empty() ? std::string("Burner") : b.label) + " started"});
    return (int)burners.size() - 1;
}

void Solver::injectBurners(float dt) {
    constexpr float BURNER_SOOT = 0.01f;   // matches the exported FDS reaction
    for (const Burner& b : burners) {
        float P = b.powerAt(time);
        if (P <= 0) continue;
        float mf = P * dt / DHC_FUEL / b.cells.size();
        for (int c : b.cells) {
            if (blocked[c]) continue;
            addGasSpecies(c, mf, 0, mf * BURNER_SOOT);
            stats.fuelIn += mf;
            pilot[c] = 1;
        }
    }
}

// ---------------------------------------------------------------------------
// Pressure zones: gas cells joined through open (non-leak) faces. A zone that
// reaches the open boundary sits at ambient pressure; a closed zone carries
// its own background pressure.
void Solver::computeZones() {
    zonesDirty_ = false;
    mgDirty_ = true;
    std::vector<int> old = zone;
    std::vector<float> oldPb = Pb;
    zone.assign(N, -1);
    zoneP.clear();
    zoneOpen.clear();
    std::vector<double> pSum, vSum;
    std::vector<int> stack;
    const int sxy = nx * ny;
    for (int seed = 0; seed < N; ++seed) {
        if (blocked[seed] || zone[seed] >= 0) continue;
        int z = (int)zoneP.size();
        zoneP.push_back(P0);
        zoneOpen.push_back(0);
        pSum.push_back(0);
        vSum.push_back(0);
        stack.clear();
        stack.push_back(seed);
        zone[seed] = z;
        while (!stack.empty()) {
            int c = stack.back();
            stack.pop_back();
            pSum[z] += oldPb.empty() ? P0 : oldPb[c];
            vSum[z] += 1;
            int i, j, k;
            cellIJK(c, i, j, k);
            float a[6] = {ax_[xface(i, j, k)], ax_[xface(i + 1, j, k)], ay_[yface(i, j, k)], ay_[yface(i, j + 1, k)],
                          az_[zface(i, j, k)], az_[zface(i, j, k + 1)]};
            int nb[6] = {i > 0 ? c - 1 : -1, i < nx - 1 ? c + 1 : -1, j > 0 ? c - nx : -1, j < ny - 1 ? c + nx : -1,
                         k > 0 ? c - sxy : -1, k < nz - 1 ? c + sxy : -1};
            for (int d = 0; d < 6; ++d) {
                if (a[d] <= 0) continue;
                if (nb[d] < 0) { if (d != 4) zoneOpen[z] = 1; continue; }   // side or top boundary
                if (blocked[nb[d]] || zone[nb[d]] >= 0) continue;
                zone[nb[d]] = z;
                stack.push_back(nb[d]);
            }
        }
    }
    for (size_t z = 0; z < zoneP.size(); ++z)
        zoneP[z] = zoneOpen[z] ? P0 : (float)(pSum[z] / std::max(1.0, vSum[z]));
    for (int c = 0; c < N; ++c) Pb[c] = zone[c] >= 0 ? zoneP[zone[c]] : P0;
    zoneVol_.assign(zoneP.size(), 0.0);
    for (size_t z = 0; z < zoneP.size(); ++z) zoneVol_[z] = vSum[z] * dx * dx * dx;
    (void)old;
}

// ---------------------------------------------------------------------------
// Deardorff eddy viscosity: nu_t = C_nu dx sqrt(k_sgs), k_sgs from the
// difference between the resolved and test-filtered cell velocity.
void Solver::turbulence() {
    std::vector<float>& uc = tmp_[0];
    std::vector<float>& vc = tmp_[1];
    std::vector<float>& wc = tmp_[2];
    std::vector<float>* f[3] = {&tmp_[3], &tmp_[4], &tmp_[5]};
    std::vector<float>* g[3] = {&tmp_[6], &tmp_[7], &tmp_[8]};
    pool_->parallelFor(nz, [&](int kb, int ke) {
        for (int k = kb; k < ke; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    int c = cell(i, j, k);
                    if (blocked[c]) { uc[c] = vc[c] = wc[c] = 0; continue; }
                    uc[c] = 0.5f * (u[xface(i, j, k)] + u[xface(i + 1, j, k)]);
                    vc[c] = 0.5f * (v[yface(i, j, k)] + v[yface(i, j + 1, k)]);
                    wc[c] = 0.5f * (w[zface(i, j, k)] + w[zface(i, j, k + 1)]);
                }
    });
    std::vector<float>* src[3] = {&uc, &vc, &wc};
    const int str[3] = {1, nx, nx * ny};
    // separable 1-2-1 filter: x into f, y into g, z into f
    for (int q = 0; q < 3; ++q) {
        for (int axis = 0; axis < 3; ++axis) {
            const std::vector<float>& in = axis == 0 ? *src[q] : axis == 1 ? *f[q] : *g[q];
            std::vector<float>& out = axis == 1 ? *g[q] : *f[q];
            pool_->parallelFor(nz, [&](int kb, int ke) {
                for (int k = kb; k < ke; ++k)
                    for (int j = 0; j < ny; ++j)
                        for (int i = 0; i < nx; ++i) {
                            int c = cell(i, j, k);
                            if (blocked[c]) { out[c] = 0; continue; }
                            int id = axis == 0 ? i : axis == 1 ? j : k, n = axis == 0 ? nx : axis == 1 ? ny : nz;
                            float lo = id > 0 && !blocked[c - str[axis]] ? in[c - str[axis]] : in[c];
                            float hi = id < n - 1 && !blocked[c + str[axis]] ? in[c + str[axis]] : in[c];
                            out[c] = 0.25f * lo + 0.5f * in[c] + 0.25f * hi;
                        }
            });
        }
    }
    std::vector<float> part(nz, 0.0f);
    pool_->parallelFor(nz, [&](int kb, int ke) {
        for (int k = kb; k < ke; ++k) {
            float m = 0;
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    int c = cell(i, j, k);
                    if (blocked[c]) { nu[c] = 0; continue; }
                    float du = uc[c] - (*f[0])[c], dv = vc[c] - (*f[1])[c], dw = wc[c] - (*f[2])[c];
                    float ks = 0.5f * (du * du + dv * dv + dw * dw);
                    nu[c] = std::min(1.0f, C_NU * dx * std::sqrt(ks) + nuMol(T[c]));
                    m = std::max(m, nu[c]);
                }
            part[k] = m;
        }
    });
    maxNu = *std::max_element(part.begin(), part.end());
}

// Molecular + turbulent heat conduction between gas cells, into Q (J).
void Solver::heatDiffusion(float dt) {
    const int sxy = nx * ny;
    pool_->parallelFor(nz, [&](int kb, int ke) {
        for (int k = kb; k < ke; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    int c = cell(i, j, k);
                    if (blocked[c]) continue;
                    float fo[6] = {fx[xface(i, j, k)], fx[xface(i + 1, j, k)], fy[yface(i, j, k)], fy[yface(i, j + 1, k)],
                                   fz[zface(i, j, k)], fz[zface(i, j, k + 1)]};
                    int nb[6] = {i > 0 ? c - 1 : -1, i < nx - 1 ? c + 1 : -1, j > 0 ? c - nx : -1, j < ny - 1 ? c + nx : -1,
                                 k > 0 ? c - sxy : -1, k < nz - 1 ? c + sxy : -1};
                    float rc = gasDensity(c), q = 0;
                    for (int d = 0; d < 6; ++d) {
                        if (fo[d] <= 0) continue;
                        int n = nb[d];
                        if (n >= 0 && blocked[n]) continue;
                        float Tn = n >= 0 ? T[n] : T0;
                        float nun = n >= 0 ? nu[n] : nu[c];
                        float rn = n >= 0 ? gasDensity(n) : P0 / (RS * T0);
                        float Tf = 0.5f * (T[c] + Tn);
                        float kf = 0.5f * (rc + rn) * CP * 0.5f * (nu[c] + nun) / PR_T + kMol(Tf);
                        q += fo[d] * kf * dx * (Tn - T[c]);
                    }
                    Q[c] += q * dt;
                }
    });
}

// Temperature from the heat budget; divergence the heat and added gas require.
void Solver::energyAndExpansion(float dt) {
    const float V = dx * dx * dx;
    std::vector<float> part((N + 4095) / 4096 + 1, 0.0f);
    pool_->parallelFor(N, [&](int b, int e) {
        float md = 0;
        for (int c = b; c < e; ++c) {
            if (blocked[c]) { S[c] = 0; continue; }
            float rho = gasDensity(c);
            float m = rho * V;
            float Tn = std::clamp(T[c] + Q[c] / (m * CP), T_MIN, T_MAX);
            float rhoN = Pb[c] / (RS * Tn);
            // div u = q / (rho cp T) + (added gas volume) / V, per unit time
            float D = Q[c] / (m * CP * T[c] * dt) + massSrc[c] / (rhoN * V * dt);
            T[c] = Tn;
            S[c] = std::clamp(D, -D_MAX, D_MAX);
            md = std::max(md, std::fabs(S[c]));
        }
        part[b / 4096] = std::max(part[b / 4096], md);
    }, 4096);
    maxDiv = *std::max_element(part.begin(), part.end());
}

// ---------------------------------------------------------------------------
// Momentum for one velocity component on its staggered faces.
void Solver::momentum(float dt) {
    const float idx = 1.0f / dx, idx2 = 1.0f / (dx * dx);
    const float rho0Amb = P0 / (RS * T0);
    for (int comp = 0; comp < 3; ++comp) {
        const std::vector<float>& vel = comp == 0 ? u : comp == 1 ? v : w;
        const std::vector<float>& fo = comp == 0 ? fx : comp == 1 ? fy : fz;
        const std::vector<uint8_t>& lk = comp == 0 ? lx_ : comp == 1 ? ly_ : lz_;
        std::vector<float>& out = comp == 0 ? u2_ : comp == 1 ? v2_ : w2_;
        const int dX = nx + (comp == 0), dY = ny + (comp == 1), dZ = nz + (comp == 2);
        const int st[3] = {1, dX, dX * dY};
        const int dim[3] = {dX, dY, dZ};
        pool_->parallelFor(dZ, [&](int kb, int ke) {
            for (int k = kb; k < ke; ++k)
                for (int j = 0; j < dY; ++j)
                    for (int i = 0; i < dX; ++i) {
                        int f = i + dX * (j + dY * k);
                        if (fo[f] <= 0) { out[f] = 0; continue; }
                        if (lk[f]) { out[f] = vel[f]; continue; }   // set by leakage()
                        int id[3] = {i, j, k};
                        int lo = id[comp] - 1;   // cell below/behind the face along comp
                        bool bnd = lo < 0 || id[comp] >= (comp == 0 ? nx : comp == 1 ? ny : nz);
                        // the two cells the face separates (-1 outside the domain)
                        int ca = -1, cb = -1;
                        {
                            int a[3] = {i, j, k};
                            a[comp] -= 1;
                            if (a[comp] >= 0) ca = cell(a[0], a[1], a[2]);
                            if (id[comp] < (comp == 0 ? nx : comp == 1 ? ny : nz)) cb = cell(i, j, k);
                        }
                        float zc = oz + (comp == 2 ? k : k + 0.5f) * dx;
                        if (bnd) {
                            float wv = windAt(comp, lo < 0 ? 0 : 1, zc);
                            if (!std::isnan(wv)) { out[f] = wv; continue; }
                            out[f] = vel[f];   // replaced below by the zero-gradient value
                            continue;
                        }
                        float v0 = vel[f];
                        // neighbor along axis a, direction s: free slip across closed faces
                        auto nbv = [&](int a, int s, float self) -> float {
                            int ni = id[a] + s;
                            if (ni < 0 || ni >= dim[a]) return self;
                            int g = f + s * st[a];
                            return fo[g] > 0 && !lk[g] ? vel[g] : self;
                        };
                        auto nb2 = [&](int a, int s, float n1) -> float {
                            int ni = id[a] + 2 * s;
                            if (ni < 0 || ni >= dim[a]) return n1;
                            int g = f + 2 * s * st[a];
                            int g1 = f + s * st[a];
                            return fo[g] > 0 && fo[g1] > 0 && !lk[g] && !lk[g1] ? vel[g] : n1;
                        };
                        // advecting velocity at this face, per axis
                        float adv[3];
                        adv[comp] = v0;
                        for (int a = 0; a < 3; ++a) {
                            if (a == comp) continue;
                            // average the four faces of component a around this face
                            const std::vector<float>& va = a == 0 ? u : a == 1 ? v : w;
                            int aX = nx + (a == 0), aY = ny + (a == 1);
                            float s = 0;
                            for (int q = 0; q < 4; ++q) {
                                int p[3] = {i, j, k};
                                if (q & 1) p[a] += 1;          // a-faces sit at both ends of the cell along a
                                if (q & 2) p[comp] -= 1;       // and on both cells beside this face
                                p[a] = std::min(p[a], (a == 0 ? nx : a == 1 ? ny : nz));
                                p[comp] = std::max(p[comp], 0);
                                s += va[p[0] + aX * (p[1] + aY * p[2])];
                            }
                            adv[a] = 0.25f * s;
                        }
                        float acc = 0, lap = 0;
                        for (int a = 0; a < 3; ++a) {
                            float m1 = nbv(a, -1, v0), p1 = nbv(a, 1, v0);
                            float m2 = nb2(a, -1, m1), p2 = nb2(a, 1, p1);
                            float fp, fm;
                            if (adv[a] >= 0) {
                                fp = v0 + 0.5f * vanLeer(v0 - m1, p1 - v0);
                                fm = m1 + 0.5f * vanLeer(m1 - m2, v0 - m1);
                            } else {
                                fp = p1 - 0.5f * vanLeer(p1 - v0, p2 - p1);
                                fm = v0 - 0.5f * vanLeer(v0 - m1, p1 - v0);
                            }
                            acc -= adv[a] * (fp - fm) * idx;
                            lap += (m1 - 2 * v0 + p1) * idx2;
                        }
                        float ra = gasDensity(ca), rb = gasDensity(cb);
                        float rf = 0.5f * (ra + rb);
                        float nuf = 0.5f * (nu[ca] + nu[cb]);
                        acc += nuf * lap;
                        if (comp == 2) {
                            float r0 = 0.5f * (Pb[ca] + Pb[cb]) / (RS * T0);
                            acc += G * (r0 - rf) / rf;   // buoyancy, full density difference
                        }
                        // baroclinic correction: + p~ grad(1/rho), p~ = rho H
                        float pt = rf * 0.5f * (p[ca] + p[cb]);
                        acc += pt * (1.0f / rb - 1.0f / ra) * idx;
                        out[f] = std::clamp(v0 + dt * acc, -VMAX, VMAX);
                    }
        });
        // open boundary faces (no wind inflow): zero gradient, the new value of
        // the face one cell inside
        int nAx = comp == 0 ? nx : comp == 1 ? ny : nz;
        int nA = dim[(comp + 1) % 3], nB = dim[(comp + 2) % 3];
        pool_->parallelFor(nB, [&](int bb, int be) {
            for (int b2 = bb; b2 < be; ++b2)
                for (int a2 = 0; a2 < nA; ++a2)
                    for (int side = (comp == 2 ? 1 : 0); side < 2; ++side) {
                        int id[3];
                        id[comp] = side == 0 ? 0 : nAx;
                        id[(comp + 1) % 3] = a2;
                        id[(comp + 2) % 3] = b2;
                        int f = id[0] + dX * (id[1] + dY * id[2]);
                        if (fo[f] <= 0 || lk[f]) continue;
                        float zc = oz + (comp == 2 ? id[2] : id[2] + 0.5f) * dx;
                        if (!std::isnan(windAt(comp, side, zc))) continue;
                        int g = f + (side == 0 ? 1 : -1) * st[comp];
                        out[f] = fo[g] > 0 && !lk[g] ? out[g] : 0.0f;
                    }
        });
        (void)rho0Amb;
    }
    u.swap(u2_);
    v.swap(v2_);
    w.swap(w2_);
}

// ---------------------------------------------------------------------------
// Crack flow and the background pressure of closed zones.
void Solver::leakage(float dt) {
    const size_t nZ = zoneP.size();
    const float V = dx * dx * dx;
    // zone integral of the divergence source
    std::vector<double> dInt(nZ, 0.0);
    for (int c = 0; c < N; ++c)
        if (zone[c] >= 0) dInt[zone[c]] += S[c] * V;
    // leak faces
    struct LeakFace { int axis, f, a, b; };
    std::vector<LeakFace> faces;
    for (int axis = 0; axis < 3; ++axis) {
        const std::vector<uint8_t>& lk = axis == 0 ? lx_ : axis == 1 ? ly_ : lz_;
        const int dX = nx + (axis == 0), dY = ny + (axis == 1), dZ = nz + (axis == 2);
        for (int f = 0; f < (int)lk.size(); ++f) {
            if (!lk[f]) continue;
            int i = f % dX, j = (f / dX) % dY, k = f / (dX * dY);
            int id[3] = {i, j, k}, a[3] = {i, j, k};
            a[axis] -= 1;
            int ca = a[axis] >= 0 ? cell(a[0], a[1], a[2]) : -1;
            int cb = id[axis] < (axis == 0 ? nx : axis == 1 ? ny : nz) ? cell(i, j, k) : -1;
            if ((ca >= 0 && blocked[ca]) || (cb >= 0 && blocked[cb])) continue;
            (void)dZ;
            faces.push_back({axis, f, ca, cb});
        }
    }
    leakFaces = (int)faces.size();
    const float A = dx * dx;
    auto zP = [&](int c, const std::vector<float>& P) { return c >= 0 && zone[c] >= 0 ? P[zone[c]] : P0; };
    auto openFrac = [&](const LeakFace& lf) { return (lf.axis == 0 ? fx : lf.axis == 1 ? fy : fz)[lf.f]; };
    auto flowOf = [&](float dp, float rhoUp) {   // m/s through the crack, positive a -> b
        float s = dp >= 0 ? 1.0f : -1.0f;
        return s * std::min(LEAK_VMAX, CD_LEAK * std::sqrt(2.0f * std::fabs(dp) / std::max(rhoUp, 0.05f)));
    };
    // implicit background pressure of each closed zone against its leaks
    std::vector<float> Pnew = zoneP;
    std::vector<std::vector<int>> zf(nZ);
    for (int q = 0; q < (int)faces.size(); ++q) {
        int za = faces[q].a >= 0 ? zone[faces[q].a] : -1, zb = faces[q].b >= 0 ? zone[faces[q].b] : -1;
        if (za == zb) continue;
        if (za >= 0) zf[za].push_back(q);
        if (zb >= 0) zf[zb].push_back(q);
    }
    for (size_t z = 0; z < nZ; ++z) {
        if (zoneOpen[z]) continue;
        float P = zoneP[z];
        float Vz = (float)zoneVol_[z];
        auto resid = [&](float Pt) {
            double out = 0;   // m3/s leaving the zone
            for (int q : zf[z]) {
                const LeakFace& lf = faces[q];
                bool zIsA = lf.a >= 0 && zone[lf.a] == (int)z;
                int other = zIsA ? lf.b : lf.a;
                float Po = zP(other, zoneP);
                float dp = Pt - Po;
                int up = dp >= 0 ? (zIsA ? lf.a : lf.b) : other;
                float rho = up >= 0 ? gasDensity(up) : P0 / (RS * T0);
                out += openFrac(lf) * A * std::fabs(flowOf(dp, rho)) * (dp >= 0 ? 1 : -1);
            }
            return Pt - P - dt * GAMMA * P / Vz * (float)(dInt[z] - out);
        };
        float lo = P - 20000.0f, hi = P + 20000.0f;
        if (resid(lo) > 0 || resid(hi) < 0) { Pnew[z] = P; continue; }
        for (int it = 0; it < 40; ++it) {
            float mid = 0.5f * (lo + hi);
            (resid(mid) > 0 ? hi : lo) = mid;
        }
        Pnew[z] = 0.5f * (lo + hi);
    }
    // per-face velocity from the end-of-step zone pressures and the local
    // dynamic pressure difference; relaxed to avoid crack-flow ringing
    std::vector<double> out(nZ, 0.0);
    for (const LeakFace& lf : faces) {
        float pa = zP(lf.a, Pnew), pb = zP(lf.b, Pnew);
        float ra = lf.a >= 0 ? gasDensity(lf.a) : P0 / (RS * T0), rb = lf.b >= 0 ? gasDensity(lf.b) : P0 / (RS * T0);
        float Ha = lf.a >= 0 ? p[lf.a] : 0, Hb = lf.b >= 0 ? p[lf.b] : 0;
        float dp = (pa - pb) + 0.5f * (ra + rb) * (Ha - Hb);
        float un = flowOf(dp, dp >= 0 ? ra : rb);
        std::vector<float>& vel = lf.axis == 0 ? u : lf.axis == 1 ? v : w;
        vel[lf.f] = 0.5f * (vel[lf.f] + un);
        float q = openFrac(lf) * A * vel[lf.f];   // m3/s from a to b
        if (lf.a >= 0 && zone[lf.a] >= 0) out[zone[lf.a]] += q;
        if (lf.b >= 0 && zone[lf.b] >= 0) out[zone[lf.b]] -= q;
    }
    // closed zones: pressure rise rate and a compatible divergence source
    zoneDP_.assign(nZ, 0.0f);
    std::vector<float> shift(nZ, 0.0f);
    for (size_t z = 0; z < nZ; ++z) {
        if (zoneOpen[z]) continue;
        float Vz = (float)zoneVol_[z];
        zoneDP_[z] = GAMMA * zoneP[z] * (float)(dInt[z] - out[z]) / Vz;
        shift[z] = (float)(dInt[z] - out[z]) / Vz;
    }
    pool_->parallelFor(N, [&](int b, int e) {
        for (int c = b; c < e; ++c)
            if (zone[c] >= 0 && !zoneOpen[zone[c]]) S[c] -= shift[zone[c]];
    }, 4096);
}

// ---------------------------------------------------------------------------
// Pressure: sum_f a_f (H_c - H_n) = (S - div u*) dx^2 / dt, Jacobi-preconditioned
// conjugate gradient, warm-started from the last step. H is the pressure
// perturbation divided by density (m2/s2); outside the open boundary H = 0.
void Solver::project(float dt) {
    const float inv = dx * dx / dt;
    const int sxy = nx * ny;
    std::vector<float>& diag = tmp_[9];
    std::vector<float>& b = rhs_;
    auto bndOpen = [&](int axis, int side, float z) { return std::isnan(windAt(axis, side, z)); };
    // Outside the open boundary the air is still at ambient pressure. Air
    // flowing in accelerates from rest, so the static pressure on the boundary
    // face is lower by its dynamic pressure (H = -u^2/2, stagnation inflow);
    // outflow leaves at ambient static pressure (H = 0). The inflow condition
    // also keeps boundary faces from running away.
    auto hOut = [](float vel, bool inflow) { return inflow ? -0.5f * vel * vel : 0.0f; };
    // matrix coefficients per cell (6 faces) are read from ax_/ay_/az_
    auto coefs = [&](int i, int j, int k, float a[6]) {
        a[0] = ax_[xface(i, j, k)]; a[1] = ax_[xface(i + 1, j, k)];
        a[2] = ay_[yface(i, j, k)]; a[3] = ay_[yface(i, j + 1, k)];
        a[4] = az_[zface(i, j, k)]; a[5] = az_[zface(i, j, k + 1)];
        float z = oz + (k + 0.5f) * dx;
        if (i == 0 && !bndOpen(0, 0, z)) a[0] = 0;
        if (i == nx - 1 && !bndOpen(0, 1, z)) a[1] = 0;
        if (j == 0 && !bndOpen(1, 0, z)) a[2] = 0;
        if (j == ny - 1 && !bndOpen(1, 1, z)) a[3] = 0;
    };
    pool_->parallelFor(nz, [&](int kb, int ke) {
        for (int k = kb; k < ke; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    int c = cell(i, j, k);
                    if (blocked[c]) { b[c] = 0; diag[c] = 0; p[c] = 0; continue; }
                    int a0 = xface(i, j, k), a1 = a0 + 1, b0 = yface(i, j, k), b1 = yface(i, j + 1, k), c0 = zface(i, j, k),
                        c1 = zface(i, j, k + 1);
                    float div = (fx[a1] * u[a1] - fx[a0] * u[a0] + fy[b1] * v[b1] - fy[b0] * v[b0] + fz[c1] * w[c1] -
                                 fz[c0] * w[c0]) / dx;
                    b[c] = (S[c] - div) * inv;
                    float a[6];
                    coefs(i, j, k, a);
                    // open boundary faces: known outside H (Bernoulli for inflow)
                    if (i == 0 && a[0] > 0) b[c] += a[0] * hOut(u[a0], u[a0] > 0);
                    if (i == nx - 1 && a[1] > 0) b[c] += a[1] * hOut(u[a1], u[a1] < 0);
                    if (j == 0 && a[2] > 0) b[c] += a[2] * hOut(v[b0], v[b0] > 0);
                    if (j == ny - 1 && a[3] > 0) b[c] += a[3] * hOut(v[b1], v[b1] < 0);
                    if (k == nz - 1 && a[5] > 0) b[c] += a[5] * hOut(w[c1], w[c1] < 0);
                    diag[c] = a[0] + a[1] + a[2] + a[3] + a[4] + a[5];
                }
    });
    // y = A x
    auto apply = [&](const std::vector<float>& x, std::vector<float>& y, int k) {
        double dot = 0;
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                int c = cell(i, j, k);
                if (diag[c] <= 0) { y[c] = 0; continue; }
                float a[6];
                coefs(i, j, k, a);
                float s = diag[c] * x[c];
                if (i > 0) s -= a[0] * x[c - 1];
                if (i < nx - 1) s -= a[1] * x[c + 1];
                if (j > 0) s -= a[2] * x[c - nx];
                if (j < ny - 1) s -= a[3] * x[c + nx];
                if (k > 0) s -= a[4] * x[c - sxy];
                if (k < nz - 1) s -= a[5] * x[c + sxy];
                y[c] = s;
                dot += (double)s * x[c];
            }
        return dot;
    };
    std::vector<float>& r = cgR_;
    std::vector<float>& z = cgZ_;
    std::vector<float>& d = cgP_;
    std::vector<float>& q = cgQ_;
    // r = b - A p ; z = M^-1 r (multigrid V-cycle) ; d = z
    pool_->parallelFor(nz, [&](int kb, int ke) {
        for (int k = kb; k < ke; ++k) {
            apply(p, q, k);
            for (int c = k * sxy; c < (k + 1) * sxy; ++c) r[c] = diag[c] > 0 ? b[c] - q[c] : 0.0f;
        }
    });
    auto dotRZ = [&]() {
        return sumSlices(*pool_, nz, [&](int k) {
            double s = 0;
            for (int c = k * sxy; c < (k + 1) * sxy; ++c) s += (double)r[c] * z[c];
            return s;
        });
    };
    pressurePrecondition(r, z);
    d = z;
    double rz = dotRZ();
    auto maxRes = [&]() {
        std::vector<float> part(nz, 0.0f);
        pool_->parallelFor(nz, [&](int kb, int ke) {
            for (int k = kb; k < ke; ++k) {
                float m = 0;
                for (int c = k * sxy; c < (k + 1) * sxy; ++c) m = std::max(m, std::fabs(r[c]));
                part[k] = m;
            }
        });
        return *std::max_element(part.begin(), part.end()) / inv;   // back to 1/s
    };
    int it = 0;
    float res = maxRes();
    while (res > profile.pressureTol && it < profile.pressureMaxIters && rz > 0) {
        double dq = sumSlices(*pool_, nz, [&](int k) { return apply(d, q, k); });
        if (dq <= 0) break;
        double alpha = rz / dq;
        pool_->parallelFor(N, [&](int b0, int e0) {
            for (int c = b0; c < e0; ++c) {
                if (diag[c] <= 0) continue;
                p[c] += (float)(alpha * d[c]);
                r[c] -= (float)(alpha * q[c]);
            }
        }, 4096);
        pressurePrecondition(r, z);
        double rzNew = dotRZ();
        double beta = rzNew / rz;
        rz = rzNew;
        pool_->parallelFor(N, [&](int b0, int e0) {
            for (int c = b0; c < e0; ++c) d[c] = z[c] + (float)(beta * d[c]);
        }, 4096);
        ++it;
        res = maxRes();
    }
    pressureIters = it;
    pressureResidual = maxRes();
    // closed zones: H is defined up to a constant; keep its zone mean at zero
    {
        size_t nZ = zoneP.size();
        std::vector<double> sum(nZ, 0.0), cnt(nZ, 0.0);
        for (int c = 0; c < N; ++c)
            if (zone[c] >= 0 && !zoneOpen[zone[c]]) { sum[zone[c]] += p[c]; cnt[zone[c]] += 1; }
        bool any = false;
        for (size_t zz = 0; zz < nZ; ++zz) if (cnt[zz] > 0) { sum[zz] /= cnt[zz]; any = true; }
        if (any)
            for (int c = 0; c < N; ++c)
                if (zone[c] >= 0 && !zoneOpen[zone[c]]) p[c] -= (float)sum[zone[c]];
    }
    // velocity correction u -= dt grad H on matrix faces
    const float s = dt / dx;
    auto P = [&](int i, int j, int k) -> float { return p[cell(i, j, k)]; };
    std::vector<float> vmaxPart(nz + 1, 0.0f);
    pool_->parallelFor(nz, [&](int kb, int ke) {
        for (int k = kb; k < ke; ++k) {
            float vm = 0;
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i <= nx; ++i) {
                    int f = xface(i, j, k);
                    if (fx[f] <= 0) { u[f] = 0; continue; }
                    if (ax_[f] > 0 && !((i == 0 || i == nx) && !bndOpen(0, i == 0 ? 0 : 1, oz + (k + 0.5f) * dx)))
                        u[f] = std::clamp(u[f] - s * ((i < nx ? P(i, j, k) : hOut(u[f], u[f] < 0)) -
                                                      (i > 0 ? P(i - 1, j, k) : hOut(u[f], u[f] > 0))), -VMAX, VMAX);
                    vm = std::max(vm, std::fabs(u[f]) * fx[f]);
                }
            for (int j = 0; j <= ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    int f = yface(i, j, k);
                    if (fy[f] <= 0) { v[f] = 0; continue; }
                    if (ay_[f] > 0 && !((j == 0 || j == ny) && !bndOpen(1, j == 0 ? 0 : 1, oz + (k + 0.5f) * dx)))
                        v[f] = std::clamp(v[f] - s * ((j < ny ? P(i, j, k) : hOut(v[f], v[f] < 0)) -
                                                      (j > 0 ? P(i, j - 1, k) : hOut(v[f], v[f] > 0))), -VMAX, VMAX);
                    vm = std::max(vm, std::fabs(v[f]) * fy[f]);
                }
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    int f = zface(i, j, k + 1);
                    if (fz[f] <= 0) { w[f] = 0; continue; }
                    if (az_[f] > 0)
                        w[f] = std::clamp(w[f] - s * ((k + 1 < nz ? P(i, j, k + 1) : hOut(w[f], w[f] < 0)) - P(i, j, k)), -VMAX, VMAX);
                    vm = std::max(vm, std::fabs(w[f]) * fz[f]);
                }
            vmaxPart[k] = vm;
        }
    });
    maxVel = *std::max_element(vmaxPart.begin(), vmaxPart.end());
}

// ---------------------------------------------------------------------------
// Transport of T and species: phi -= dt/dx sum_out un (phi_face - phi_c), with
// Superbee-limited upwind face values (the non-conservative remainder of the
// flux divergence is removed, which is exact for the low Mach advective form),
// then turbulent + molecular diffusion of species. The new value is bounded by
// the cell's and its neighbors' old values.
void Solver::advectScalars(float dt) {
    constexpr int NF = 9;
    std::vector<float>* fields[NF] = {&T, &Yo, &Yf, &Ys, &Ya, &Yp, &Yc, &Yw, &Yco};
    const float amb[NF] = {T0, YO2_AMB, 0, 0, 0, 0, YCO2_AMB, YH2O_AMB, 0};
    const float s = dt / dx, sd = dt / (dx * dx);
    const int sxy = nx * ny;
    pool_->parallelFor(nz, [&](int kb, int ke) {
        for (int k = kb; k < ke; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    int c = cell(i, j, k);
                    if (blocked[c]) {
                        for (int q = 0; q < NF; ++q) tmp_[q][c] = (*fields[q])[c];
                        continue;
                    }
                    int fa0 = xface(i, j, k), fa1 = fa0 + 1, fb0 = yface(i, j, k), fb1 = yface(i, j + 1, k),
                        fc0 = zface(i, j, k), fc1 = zface(i, j, k + 1);
                    // outward volume flux per unit area (open fraction included)
                    float un[6] = {-fx[fa0] * u[fa0], fx[fa1] * u[fa1], -fy[fb0] * v[fb0], fy[fb1] * v[fb1],
                                   -fz[fc0] * w[fc0], fz[fc1] * w[fc1]};
                    float fo[6] = {fx[fa0], fx[fa1], fy[fb0], fy[fb1], fz[fc0], fz[fc1]};
                    int id[3] = {i, j, k};
                    const int dim[3] = {nx, ny, nz}, st[3] = {1, nx, sxy};
                    int nb[6], nb2[6], opp[6];
                    for (int d = 0; d < 6; ++d) {
                        int a = d >> 1, sg = (d & 1) ? 1 : -1;
                        int n1 = id[a] + sg, n2 = id[a] + 2 * sg, o1 = id[a] - sg;
                        nb[d] = (n1 >= 0 && n1 < dim[a]) ? c + sg * st[a] : -1;
                        nb2[d] = (n2 >= 0 && n2 < dim[a]) ? c + 2 * sg * st[a] : -1;
                        opp[d] = (o1 >= 0 && o1 < dim[a]) ? c - sg * st[a] : -1;
                        if (nb[d] >= 0 && blocked[nb[d]]) nb[d] = -2;
                        if (nb2[d] >= 0 && blocked[nb2[d]]) nb2[d] = -2;
                        if (opp[d] >= 0 && blocked[opp[d]]) opp[d] = -2;
                    }
                    float nuc = nu[c];
                    // upwind density per face: species move as mass (conservative), so
                    // a cell's new mass fraction is (rho Y V - fluxes) / (rho V - fluxes)
                    // face density from the limited face temperature, rho_f = P / (R T_f):
                    // then rho_f T_f = P / R on every face and the equation-of-state
                    // density after transport equals the transported mass exactly
                    float rc = gasDensity(c), rUp[6], mden = rc;
                    for (int d = 0; d < 6; ++d) {
                        rUp[d] = 0;
                        if (fo[d] <= 0 || nb[d] == -2) continue;
                        float tn = nb[d] >= 0 ? T[nb[d]] : T0, tc = T[c], tf;
                        if (un[d] > 0) {
                            float tu2 = opp[d] >= 0 ? T[opp[d]] : (opp[d] == -1 ? T0 : tc);
                            float den = tn - tc;
                            tf = tc + (std::fabs(den) > 1e-6f ? 0.5f * superbee((tc - tu2) / den) * den : 0.0f);
                        } else {
                            float tu2 = nb2[d] >= 0 ? T[nb2[d]] : (nb2[d] == -1 ? T0 : tn);
                            float den = tc - tn;
                            tf = tn + (std::fabs(den) > 1e-6f ? 0.5f * superbee((tn - tu2) / den) * den : 0.0f);
                        }
                        float pu = un[d] > 0 ? Pb[c] : (nb[d] >= 0 ? Pb[nb[d]] : P0);
                        rUp[d] = pu / (RS * std::max(tf, T_MIN));
                        mden -= s * rUp[d] * un[d];
                    }
                    bool conservative = mden > 0.25f * rc;
                    for (int q = 0; q < NF; ++q) {
                        const std::vector<float>& F = *fields[q];
                        float fc = F[c];
                        float lo = fc, hi = fc, acc = 0, macc = 0;
                        for (int d = 0; d < 6; ++d) {
                            if (fo[d] <= 0 || nb[d] == -2) continue;
                            float fn = nb[d] >= 0 ? F[nb[d]] : amb[q];
                            lo = std::min(lo, fn);
                            hi = std::max(hi, fn);
                            float flux = un[d];
                            float face = fc;
                            if (flux > 0) {   // outflow: upwind is c, its upwind is opp
                                float fu2 = opp[d] >= 0 ? F[opp[d]] : (opp[d] == -1 ? amb[q] : fc);
                                float den = fn - fc;
                                if (std::fabs(den) > 1e-12f) face += 0.5f * superbee((fc - fu2) / den) * den;
                            } else if (flux < 0) {   // inflow: upwind is the neighbor
                                float fu2 = nb2[d] >= 0 ? F[nb2[d]] : (nb2[d] == -1 ? amb[q] : fn);
                                float den = fc - fn;
                                face = fn;
                                if (std::fabs(den) > 1e-12f) face += 0.5f * superbee((fn - fu2) / den) * den;
                            }
                            acc -= flux * (face - fc);              // advective form (temperature)
                            macc -= rUp[d] * flux * face;           // mass form (species)
                            if (q > 0) {   // species diffusion (heat goes through Q in heatDiffusion)
                                float nun = nb[d] >= 0 ? nu[nb[d]] : nuc;
                                float D = 0.5f * (nuc + nun) / SC_T + 2.0e-5f;
                                float rf = nb[d] >= 0 ? 0.5f * (rc + gasDensity(nb[d])) : rc;
                                acc += fo[d] * D * (fn - fc) / dx;
                                macc += fo[d] * rf * D * (fn - fc) / dx;
                            }
                        }
                        // mass-weighted for every field: for an ideal gas at constant pressure
                        // this mixes temperature by mass (energy conserving) and keeps the
                        // equation-of-state density equal to the transported mass
                        float val = conservative ? (rc * fc + s * macc) / mden : fc + s * acc;
                        tmp_[q][c] = std::clamp(val, lo, hi);
                    }
                }
    });
    for (int q = 0; q < NF; ++q) fields[q]->swap(tmp_[q]);
}

} // namespace photon
