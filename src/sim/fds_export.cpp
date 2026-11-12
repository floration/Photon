// Photon - FDS exporter: writes the solver's discretized scene as an FDS 6
// input file on the same grid, so both codes solve the same problem
// (code-to-code verification, handoff "Validation" level 2).
//
// What is exported: one mesh matching the Photon grid; solid cells as OBST
// boxes; every intact barrier face as a thin OBST (cracks closed: FDS has no
// per-face leakage, so comparison cases run with leakage cleared); open
// domain sides and top; burners as fuel vents with the same t-squared ramp;
// the generic fuel reaction with the same soot and CO yields; devices with
// the matching FDS quantities. Vents (soffit / ridge open fractions) become
// fully open faces, which is noted in the file.
#include "sim/fds_export.h"
#include <cstdio>
#include <fstream>

namespace photon {

namespace {
std::string f3(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.3f", v);
    return b;
}
} // namespace

bool exportFds(const Solver& s, const std::string& path, const std::string& chid, const std::string& title, float tEnd,
               const std::vector<Device>& devs, std::string& err) {
    std::ofstream f(path);
    if (!f) { err = "cannot write " + path; return false; }
    const float X0 = s.ox, Y0 = s.oy, Z0 = s.oz, d = s.dx;
    const float X1 = X0 + s.nx * d, Y1 = Y0 + s.ny * d, Z1 = Z0 + s.nz * d;
    f << "Written by Photon (FDS exporter, Phase 3) from the discretized scene.\n"
      << "Grid, geometry, burner, reaction and devices match the Photon case.\n"
      << "Profile: " << s.profile.name << ", cell " << d * 100 << " cm.\n\n";
    f << "&HEAD CHID='" << chid << "', TITLE='" << title << "' /\n";
    f << "&MESH IJK=" << s.nx << "," << s.ny << "," << s.nz << ", XB=" << f3(X0) << "," << f3(X1) << "," << f3(Y0) << ","
      << f3(Y1) << "," << f3(Z0) << "," << f3(Z1) << " /\n";
    f << "&TIME T_END=" << tEnd << " /\n";
    f << "&DUMP DT_DEVC=1.0, DT_HRR=1.0 /\n";
    if (!s.radiationOn) f << "&RADI RADIATION=.FALSE. /\n";
    if (s.wind.speed > 0)
        f << "&WIND SPEED=" << s.wind.speed << ", DIRECTION=" << s.wind.fromDeg << ", PLE=" << s.wind.exponent << " /\n";
    f << "&REAC FUEL='PHOTON_FUEL', C=1, H=1.7, O=0.4, SOOT_YIELD=0.01, CO_YIELD=" << Solver::CO_YIELD
      << ", HEAT_OF_COMBUSTION=" << Solver::DHC_FUEL / 1000.0f << " /\n";
    if (s.adiabaticSurfaces) f << "&SURF ID='WALL', ADIABATIC=.TRUE., DEFAULT=.TRUE., COLOR='GRAY' /\n";
    else f << "&SURF ID='WALL', DEFAULT=.TRUE., COLOR='GRAY' /   INERT (fixed 20 C); Photon surface heat transfer is prototype until Phase 4\n";
    // burners
    for (size_t bi = 0; bi < s.burners.size(); ++bi) {
        const Burner& b = s.burners[bi];
        // the burner area FDS sees: the Photon cells it releases into
        float area = b.cells.size() * d * d;
        std::string id = "BURNER" + std::to_string(bi + 1);
        f << "&SURF ID='" << id << "', HRRPUA=" << b.peakW / 1000.0f / area << ", RAMP_Q='" << id << "_RAMP', COLOR='RED' /\n";
        f << "&RAMP ID='" << id << "_RAMP', T=0, F=0 /\n";
        if (b.start > 0) f << "&RAMP ID='" << id << "_RAMP', T=" << b.start << ", F=0 /\n";
        if (b.rampS > 0)
            for (int q = 1; q <= 10; ++q) {
                float t = b.rampS * q / 10.0f;
                f << "&RAMP ID='" << id << "_RAMP', T=" << b.start + t << ", F=" << (t / b.rampS) * (t / b.rampS) << " /\n";
            }
        else
            f << "&RAMP ID='" << id << "_RAMP', T=" << b.start + 0.01f << ", F=1 /\n";
        if (b.duration < 1e8f) {
            f << "&RAMP ID='" << id << "_RAMP', T=" << b.start + b.duration << ", F=1 /\n";
            f << "&RAMP ID='" << id << "_RAMP', T=" << b.start + b.duration + 0.01f << ", F=0 /\n";
        }
        // one vent per burner cell, on the cell's floor face (solid below or the ground)
        for (int c : b.cells) {
            int i, j, k;
            s.cellIJK(c, i, j, k);
            float x0 = X0 + i * d, y0 = Y0 + j * d, z0 = Z0 + k * d;
            if (k > 0)   // a burner off the ground stands on a block
                f << "&OBST XB=" << f3(x0) << "," << f3(x0 + d) << "," << f3(y0) << "," << f3(y0 + d) << "," << f3(Z0) << "," << f3(z0)
                  << " / burner stand\n";
            f << "&VENT XB=" << f3(x0) << "," << f3(x0 + d) << "," << f3(y0) << "," << f3(y0 + d) << "," << f3(z0) << "," << f3(z0)
              << ", SURF_ID='" << id << "' /\n";
        }
    }
    // solid cells: runs in x merged over y
    int nObst = 0;
    std::vector<uint8_t> done(s.N, 0);
    for (int k = 0; k < s.nz; ++k)
        for (int j = 0; j < s.ny; ++j)
            for (int i = 0; i < s.nx; ++i) {
                int c = s.cell(i, j, k);
                if (!s.blocked[c] || done[c]) continue;
                int i1 = i;
                while (i1 + 1 < s.nx && s.blocked[s.cell(i1 + 1, j, k)] && !done[s.cell(i1 + 1, j, k)]) ++i1;
                int j1 = j;
                for (;;) {
                    if (j1 + 1 >= s.ny) break;
                    bool ok = true;
                    for (int ii = i; ii <= i1 && ok; ++ii) ok = s.blocked[s.cell(ii, j1 + 1, k)] && !done[s.cell(ii, j1 + 1, k)];
                    if (!ok) break;
                    ++j1;
                }
                for (int jj = j; jj <= j1; ++jj)
                    for (int ii = i; ii <= i1; ++ii) done[s.cell(ii, jj, k)] = 1;
                f << "&OBST XB=" << f3(X0 + i * d) << "," << f3(X0 + (i1 + 1) * d) << "," << f3(Y0 + j * d) << "," << f3(Y0 + (j1 + 1) * d)
                  << "," << f3(Z0 + k * d) << "," << f3(Z0 + (k + 1) * d) << " /\n";
                ++nObst;
            }
    // thin barriers: intact faces that block flow (cracks count as closed)
    auto closedFace = [&](int axis, int fidx) {
        int b = axis == 0 ? s.bx[fidx] : axis == 1 ? s.by[fidx] : s.bz[fidx];
        if (b < 0 || !s.barrierIntact(b)) return false;
        float fo = axis == 0 ? s.fx[fidx] : axis == 1 ? s.fy[fidx] : s.fz[fidx];
        return fo < 0.05f;   // vents stay open
    };
    int nThin = 0;
    for (int axis = 0; axis < 3; ++axis) {
        // plane index p along axis, 2D extent (a, b) over the other two axes
        int np = (axis == 0 ? s.nx : axis == 1 ? s.ny : s.nz) + 1;
        int na = axis == 0 ? s.ny : s.nx, nb = axis == 2 ? s.ny : s.nz;
        auto faceIdx = [&](int p, int a, int b) {
            if (axis == 0) return s.xface(p, a, b);
            if (axis == 1) return s.yface(a, p, b);
            return s.zface(a, b, p);
        };
        std::vector<uint8_t> used((size_t)na * nb);
        for (int p = 0; p < np; ++p) {
            std::fill(used.begin(), used.end(), 0);
            for (int b = 0; b < nb; ++b)
                for (int a = 0; a < na; ++a) {
                    if (used[a + (size_t)na * b] || !closedFace(axis, faceIdx(p, a, b))) continue;
                    int a1 = a;
                    while (a1 + 1 < na && !used[a1 + 1 + (size_t)na * b] && closedFace(axis, faceIdx(p, a1 + 1, b))) ++a1;
                    int b1 = b;
                    for (;;) {
                        if (b1 + 1 >= nb) break;
                        bool ok = true;
                        for (int aa = a; aa <= a1 && ok; ++aa) ok = !used[aa + (size_t)na * (b1 + 1)] && closedFace(axis, faceIdx(p, aa, b1 + 1));
                        if (!ok) break;
                        ++b1;
                    }
                    for (int bb = b; bb <= b1; ++bb)
                        for (int aa = a; aa <= a1; ++aa) used[aa + (size_t)na * bb] = 1;
                    float lo[3], hi[3];
                    int ax2[2] = {axis == 0 ? 1 : 0, axis == 2 ? 1 : 2};
                    const float org[3] = {X0, Y0, Z0};
                    lo[axis] = hi[axis] = org[axis] + p * d;
                    lo[ax2[0]] = org[ax2[0]] + a * d; hi[ax2[0]] = org[ax2[0]] + (a1 + 1) * d;
                    lo[ax2[1]] = org[ax2[1]] + b * d; hi[ax2[1]] = org[ax2[1]] + (b1 + 1) * d;
                    f << "&OBST XB=" << f3(lo[0]) << "," << f3(hi[0]) << "," << f3(lo[1]) << "," << f3(hi[1]) << "," << f3(lo[2]) << ","
                      << f3(hi[2]) << " /\n";
                    ++nThin;
                }
        }
    }
    f << "&VENT MB='XMIN', SURF_ID='OPEN' /\n&VENT MB='XMAX', SURF_ID='OPEN' /\n"
      << "&VENT MB='YMIN', SURF_ID='OPEN' /\n&VENT MB='YMAX', SURF_ID='OPEN' /\n&VENT MB='ZMAX', SURF_ID='OPEN' /\n";
    for (const Device& dv : devs) {
        f << "&DEVC ID='" << dv.id << "', QUANTITY='" << fdsQuantity(dv.kind) << "'";
        if (dv.kind == DeviceKind::O2) f << ", SPEC_ID='OXYGEN'";
        if (dv.kind == DeviceKind::CO2) f << ", SPEC_ID='CARBON DIOXIDE'";
        if (dv.kind == DeviceKind::CO) f << ", SPEC_ID='CARBON MONOXIDE'";
        if (dv.kind == DeviceKind::Soot) f << ", SPEC_ID='SOOT'";
        if (dv.kind == DeviceKind::LayerHeight || dv.kind == DeviceKind::UpperTemp || dv.kind == DeviceKind::LowerTemp)
            f << ", XB=" << f3(dv.x) << "," << f3(dv.x) << "," << f3(dv.y) << "," << f3(dv.y) << "," << f3(dv.z0) << "," << f3(dv.z1);
        else
            f << ", XYZ=" << f3(dv.x) << "," << f3(dv.y) << "," << f3(dv.z);
        f << " /\n";
    }
    f << "&TAIL /\n";
    f << "\n" << nObst << " solid boxes, " << nThin << " thin barrier panels.\n";
    if (!f) { err = "write failed: " + path; return false; }
    return true;
}

} // namespace photon
