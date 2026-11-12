// Photon - multigrid preconditioner for the pressure Poisson solve.
//
// Connected aggregation multigrid. Each coarse unknown is one connected piece
// of a 2x2x2 block of finer unknowns: cells on opposite sides of a wall, a
// closed door or a solid never share a coarse unknown, so the coarse levels
// see the same rooms, doors and zones as the fine grid. Coarse matrices are
// the Galerkin product with piecewise-constant interpolation (sum of the
// couplings between aggregates). The fine level is the structured 7-point
// grid; coarse levels are sparse rows. One symmetric V-cycle (red-black
// Gauss-Seidel on the fine level, symmetric Jacobi on coarse levels, a
// forward-backward Gauss-Seidel solve on the coarsest) preconditions the
// conjugate gradient solve.
#include "sim/solver.h"
#include <cmath>
#include <numeric>

namespace photon {

namespace {
struct UF {
    std::vector<int> p;
    void reset(int n) { p.resize(n); std::iota(p.begin(), p.end(), 0); }
    int find(int a) { while (p[a] != a) a = p[a] = p[p[a]]; return a; }
    void join(int a, int b) { a = find(a); b = find(b); if (a != b) p[std::max(a, b)] = std::min(a, b); }
};
} // namespace

void Solver::buildPressureLevels() {
    mgDirty_ = false;
    mg_.clear();
    auto bndOpen = [&](int axis, int side, float z) { return std::isnan(windAt(axis, side, z)); };
    // ---- level 0: the structured grid with boundary (wind) adjustments
    PressureLevel L0;
    L0.nx = nx; L0.ny = ny; L0.nz = nz;
    L0.ax = ax_; L0.ay = ay_; L0.az = az_;
    for (int k = 0; k < nz; ++k) {
        float z = oz + (k + 0.5f) * dx;
        for (int j = 0; j < ny; ++j) {
            if (!bndOpen(0, 0, z)) L0.ax[xface(0, j, k)] = 0;
            if (!bndOpen(0, 1, z)) L0.ax[xface(nx, j, k)] = 0;
        }
        for (int i = 0; i < nx; ++i) {
            if (!bndOpen(1, 0, z)) L0.ay[yface(i, 0, k)] = 0;
            if (!bndOpen(1, 1, z)) L0.ay[yface(i, ny, k)] = 0;
        }
    }
    const int n0 = N;
    L0.n = n0;
    L0.diag.assign(n0, 0.0f);
    for (int k = 0; k < nz; ++k)
        for (int j = 0; j < ny; ++j)
            for (int i = 0; i < nx; ++i) {
                int c = cell(i, j, k);
                if (blocked[c]) continue;
                L0.diag[c] = L0.ax[xface(i, j, k)] + L0.ax[xface(i + 1, j, k)] + L0.ay[yface(i, j, k)] + L0.ay[yface(i, j + 1, k)] +
                             L0.az[zface(i, j, k)] + L0.az[zface(i, j, k + 1)];
            }
    // ---- level 1: connected pieces of 2x2x2 blocks of the grid
    {
        UF uf;
        uf.reset(n0);
        const int sxy = nx * ny;
        for (int k = 0; k < nz; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    int c = cell(i, j, k);
                    if (L0.diag[c] <= 0) continue;
                    if (i + 1 < nx && (i >> 1) == ((i + 1) >> 1) && L0.ax[xface(i + 1, j, k)] > 0 && L0.diag[c + 1] > 0) uf.join(c, c + 1);
                    if (j + 1 < ny && (j >> 1) == ((j + 1) >> 1) && L0.ay[yface(i, j + 1, k)] > 0 && L0.diag[c + nx] > 0) uf.join(c, c + nx);
                    if (k + 1 < nz && (k >> 1) == ((k + 1) >> 1) && L0.az[zface(i, j, k + 1)] > 0 && L0.diag[c + sxy] > 0) uf.join(c, c + sxy);
                }
        L0.agg.assign(n0, -1);
        std::vector<int> rootId(n0, -1);
        PressureLevel C;
        for (int k = 0; k < nz; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    int c = cell(i, j, k);
                    if (L0.diag[c] <= 0) continue;
                    int r = uf.find(c);
                    if (rootId[r] < 0) {
                        rootId[r] = C.n++;
                        C.bi.push_back(i >> 1); C.bj.push_back(j >> 1); C.bk.push_back(k >> 1);
                    }
                    L0.agg[c] = rootId[r];
                }
        // Galerkin coarse operator from the fine faces
        std::vector<std::vector<std::pair<int, float>>> rows(C.n);
        std::vector<double> dg(C.n, 0.0);
        auto couple = [&](int a, int b, float w) {
            if (w <= 0) return;
            int A = L0.agg[a], B = L0.agg[b];
            if (A < 0 || B < 0 || A == B) return;
            auto add = [&](int r, int col) {
                for (auto& e : rows[r]) if (e.first == col) { e.second += w; return; }
                rows[r].push_back({col, w});
            };
            add(A, B);
            add(B, A);
        };
        for (int k = 0; k < nz; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    int c = cell(i, j, k);
                    if (L0.diag[c] <= 0) continue;
                    if (i + 1 < nx) couple(c, c + 1, L0.ax[xface(i + 1, j, k)]);
                    if (j + 1 < ny) couple(c, c + nx, L0.ay[yface(i, j + 1, k)]);
                    if (k + 1 < nz) couple(c, c + sxy, L0.az[zface(i, j, k + 1)]);
                }
        // diagonal: every fine coefficient leaving the aggregate (to another
        // aggregate or the open boundary)
        for (int c = 0; c < n0; ++c) if (L0.agg[c] >= 0) dg[L0.agg[c]] += L0.diag[c];
        for (int I = 0; I < C.n; ++I) {
            double internal = 0;   // fine diag sum counts internal faces twice; remove them
            (void)internal;
        }
        for (int k = 0; k < nz; ++k)
            for (int j = 0; j < ny; ++j)
                for (int i = 0; i < nx; ++i) {
                    int c = cell(i, j, k);
                    if (L0.diag[c] <= 0) continue;
                    auto inner = [&](int n2, float w) { if (w > 0 && L0.agg[n2] == L0.agg[c]) dg[L0.agg[c]] -= 2.0 * w; };
                    if (i + 1 < nx && L0.diag[c + 1] > 0) inner(c + 1, L0.ax[xface(i + 1, j, k)]);
                    if (j + 1 < ny && L0.diag[c + nx] > 0) inner(c + nx, L0.ay[yface(i, j + 1, k)]);
                    if (k + 1 < nz && L0.diag[c + sxy] > 0) inner(c + sxy, L0.az[zface(i, j, k + 1)]);
                }
        C.diag.resize(C.n);
        C.rowStart.assign(C.n + 1, 0);
        for (int I = 0; I < C.n; ++I) {
            C.diag[I] = (float)std::max(dg[I], 0.0);
            C.rowStart[I + 1] = C.rowStart[I] + (int)rows[I].size();
        }
        C.col.reserve(C.rowStart[C.n]);
        C.val.reserve(C.rowStart[C.n]);
        for (int I = 0; I < C.n; ++I)
            for (auto& e : rows[I]) { C.col.push_back(e.first); C.val.push_back(e.second); }
        mg_.push_back(std::move(L0));
        mg_.push_back(std::move(C));
    }
    // ---- coarser levels: connected pieces of 2x2x2 groups of block coordinates
    while (mg_.back().n > 2000 && mg_.size() < 12) {
        PressureLevel& F = mg_.back();
        UF uf;
        uf.reset(F.n);
        for (int I = 0; I < F.n; ++I)
            for (int e = F.rowStart[I]; e < F.rowStart[I + 1]; ++e) {
                int J = F.col[e];
                if (J > I && (F.bi[I] >> 1) == (F.bi[J] >> 1) && (F.bj[I] >> 1) == (F.bj[J] >> 1) && (F.bk[I] >> 1) == (F.bk[J] >> 1))
                    uf.join(I, J);
            }
        F.agg.assign(F.n, -1);
        std::vector<int> rootId(F.n, -1);
        PressureLevel C;
        for (int I = 0; I < F.n; ++I) {
            int r = uf.find(I);
            if (rootId[r] < 0) {
                rootId[r] = C.n++;
                C.bi.push_back(F.bi[I] >> 1); C.bj.push_back(F.bj[I] >> 1); C.bk.push_back(F.bk[I] >> 1);
            }
            F.agg[I] = rootId[r];
        }
        if (C.n > 0.8 * F.n) { F.agg.clear(); break; }   // not coarsening any more
        std::vector<std::vector<std::pair<int, float>>> rows(C.n);
        std::vector<double> dg(C.n, 0.0);
        for (int I = 0; I < F.n; ++I) dg[F.agg[I]] += F.diag[I];
        for (int I = 0; I < F.n; ++I)
            for (int e = F.rowStart[I]; e < F.rowStart[I + 1]; ++e) {
                int J = F.col[e];
                int A = F.agg[I], B = F.agg[J];
                if (A == B) { dg[A] -= F.val[e]; continue; }   // each internal coupling appears twice (I->J and J->I)
                bool found = false;
                for (auto& x : rows[A]) if (x.first == B) { x.second += F.val[e]; found = true; break; }
                if (!found) rows[A].push_back({B, F.val[e]});
            }
        C.diag.resize(C.n);
        C.rowStart.assign(C.n + 1, 0);
        for (int I = 0; I < C.n; ++I) {
            C.diag[I] = (float)std::max(dg[I], 0.0);
            C.rowStart[I + 1] = C.rowStart[I] + (int)rows[I].size();
        }
        for (int I = 0; I < C.n; ++I)
            for (auto& e : rows[I]) { C.col.push_back(e.first); C.val.push_back(e.second); }
        mg_.push_back(std::move(C));
    }
    for (PressureLevel& L : mg_) {
        L.x.assign(L.n, 0.0f);
        L.b.assign(L.n, 0.0f);
        L.r.assign(L.n, 0.0f);
        L.t.assign(L.n, 0.0f);
    }
}

namespace {
// structured level: one colored Gauss-Seidel half sweep
void gsColor(JobPool& pool, PressureLevel& L, int color) {
    const int sx = L.nx, sxy = L.nx * L.ny;
    pool.parallelFor(L.ny * L.nz, [&](int rb, int re) {
        for (int r = rb; r < re; ++r) {
            int j = r % L.ny, k = r / L.ny;
            int i0 = (j + k + color) & 1;
            for (int i = i0; i < L.nx; i += 2) {
                int c = i + sx * (j + L.ny * k);
                float dg = L.diag[c];
                if (dg <= 0) { L.x[c] = 0; continue; }
                float s = L.b[c];
                if (i > 0) s += L.ax[i + (L.nx + 1) * (j + L.ny * k)] * L.x[c - 1];
                if (i < L.nx - 1) s += L.ax[i + 1 + (L.nx + 1) * (j + L.ny * k)] * L.x[c + 1];
                if (j > 0) s += L.ay[i + L.nx * (j + (L.ny + 1) * k)] * L.x[c - sx];
                if (j < L.ny - 1) s += L.ay[i + L.nx * (j + 1 + (L.ny + 1) * k)] * L.x[c + sx];
                if (k > 0) s += L.az[i + L.nx * (j + L.ny * k)] * L.x[c - sxy];
                if (k < L.nz - 1) s += L.az[i + L.nx * (j + L.ny * (k + 1))] * L.x[c + sxy];
                L.x[c] = s / dg;
            }
        }
    }, 4);
}
void residualGrid(JobPool& pool, PressureLevel& L) {
    const int sx = L.nx, sxy = L.nx * L.ny;
    pool.parallelFor(L.nz, [&](int kb, int ke) {
        for (int k = kb; k < ke; ++k)
            for (int j = 0; j < L.ny; ++j)
                for (int i = 0; i < L.nx; ++i) {
                    int c = i + sx * (j + L.ny * k);
                    if (L.diag[c] <= 0) { L.r[c] = 0; continue; }
                    float s = L.b[c] - L.diag[c] * L.x[c];
                    if (i > 0) s += L.ax[i + (L.nx + 1) * (j + L.ny * k)] * L.x[c - 1];
                    if (i < L.nx - 1) s += L.ax[i + 1 + (L.nx + 1) * (j + L.ny * k)] * L.x[c + 1];
                    if (j > 0) s += L.ay[i + L.nx * (j + (L.ny + 1) * k)] * L.x[c - sx];
                    if (j < L.ny - 1) s += L.ay[i + L.nx * (j + 1 + (L.ny + 1) * k)] * L.x[c + sx];
                    if (k > 0) s += L.az[i + L.nx * (j + L.ny * k)] * L.x[c - sxy];
                    if (k < L.nz - 1) s += L.az[i + L.nx * (j + L.ny * (k + 1))] * L.x[c + sxy];
                    L.r[c] = s;
                }
    });
}
// sparse level: damped Jacobi sweep (symmetric, parallel), and residual
void jacobi(JobPool& pool, PressureLevel& L, float omega) {
    pool.parallelFor(L.n, [&](int b, int e) {
        for (int I = b; I < e; ++I) {
            if (L.diag[I] <= 0) { L.t[I] = 0; continue; }
            float s = L.b[I];
            for (int q = L.rowStart[I]; q < L.rowStart[I + 1]; ++q) s += L.val[q] * L.x[L.col[q]];
            L.t[I] = (1 - omega) * L.x[I] + omega * s / L.diag[I];
        }
    }, 1024);
    L.x.swap(L.t);
}
void residualSparse(JobPool& pool, PressureLevel& L) {
    pool.parallelFor(L.n, [&](int b, int e) {
        for (int I = b; I < e; ++I) {
            if (L.diag[I] <= 0) { L.r[I] = 0; continue; }
            float s = L.b[I] - L.diag[I] * L.x[I];
            for (int q = L.rowStart[I]; q < L.rowStart[I + 1]; ++q) s += L.val[q] * L.x[L.col[q]];
            L.r[I] = s;
        }
    }, 1024);
}
void gsSparseSweep(PressureLevel& L, bool forward) {
    for (int t = 0; t < L.n; ++t) {
        int I = forward ? t : L.n - 1 - t;
        if (L.diag[I] <= 0) { L.x[I] = 0; continue; }
        float s = L.b[I];
        for (int q = L.rowStart[I]; q < L.rowStart[I + 1]; ++q) s += L.val[q] * L.x[L.col[q]];
        L.x[I] = s / L.diag[I];
    }
}
} // namespace

// z = M^-1 r with one symmetric V-cycle
void Solver::pressurePrecondition(const std::vector<float>& r, std::vector<float>& z) {
    if (mgDirty_ || mg_.empty()) buildPressureLevels();
    const int nl = (int)mg_.size();
    mg_[0].b = r;
    for (int l = 0; l < nl; ++l) {
        PressureLevel& L = mg_[l];
        std::fill(L.x.begin(), L.x.end(), 0.0f);
        bool coarsest = l == nl - 1 || L.agg.empty();
        if (coarsest) {
            for (int it = 0; it < 20; ++it) { gsSparseSweep(L, true); gsSparseSweep(L, false); }
            break;
        }
        if (l == 0) { gsColor(*pool_, L, 0); gsColor(*pool_, L, 1); residualGrid(*pool_, L); }
        else { jacobi(*pool_, L, 0.7f); jacobi(*pool_, L, 0.7f); residualSparse(*pool_, L); }
        PressureLevel& C = mg_[l + 1];
        std::fill(C.b.begin(), C.b.end(), 0.0f);
        for (int i = 0; i < L.n; ++i) if (L.agg[i] >= 0) C.b[L.agg[i]] += L.r[i];
    }
    int last = 0;
    while (last + 1 < nl && !mg_[last].agg.empty()) ++last;
    for (int l = last - 1; l >= 0; --l) {
        PressureLevel& L = mg_[l];
        const PressureLevel& C = mg_[l + 1];
        pool_->parallelFor(L.n, [&](int b, int e) {
            for (int i = b; i < e; ++i) if (L.agg[i] >= 0) L.x[i] += C.x[L.agg[i]];
        }, 4096);
        if (l == 0) { gsColor(*pool_, L, 1); gsColor(*pool_, L, 0); }
        else { jacobi(*pool_, L, 0.7f); jacobi(*pool_, L, 0.7f); }
    }
    z = mg_[0].x;
}

} // namespace photon
