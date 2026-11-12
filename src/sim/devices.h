// Photon - measurement devices for verification and FDS comparison. Each
// device reads the same quantity FDS reports under the matching QUANTITY.
#pragma once
#include "sim/solver.h"
#include <cmath>
#include <string>
#include <vector>

namespace photon {

enum class DeviceKind { Temperature, VelU, VelV, VelW, LayerHeight, UpperTemp, LowerTemp, O2, CO2, CO, Soot, Pressure };

struct Device {
    std::string id;
    DeviceKind kind = DeviceKind::Temperature;
    float x = 0, y = 0, z = 0;      // point; layer devices use x, y and the column z0..z1
    float z0 = 0, z1 = 0;
};

inline const char* fdsQuantity(DeviceKind k) {
    switch (k) {
    case DeviceKind::Temperature: return "TEMPERATURE";
    case DeviceKind::VelU: return "U-VELOCITY";
    case DeviceKind::VelV: return "V-VELOCITY";
    case DeviceKind::VelW: return "W-VELOCITY";
    case DeviceKind::LayerHeight: return "LAYER HEIGHT";
    case DeviceKind::UpperTemp: return "UPPER TEMPERATURE";
    case DeviceKind::LowerTemp: return "LOWER TEMPERATURE";
    case DeviceKind::O2: return "VOLUME FRACTION";
    case DeviceKind::CO2: return "VOLUME FRACTION";
    case DeviceKind::CO: return "VOLUME FRACTION";
    case DeviceKind::Soot: return "DENSITY";
    case DeviceKind::Pressure: return "PRESSURE";
    }
    return "?";
}

// Two-zone reduction of a temperature column (the method FDS uses, from
// He, Fernando and Luo, Fire Safety Journal 1998): interface height and the
// mean upper and lower layer temperatures. Temperatures in K.
struct LayerResult { float height, upper, lower; };
inline LayerResult layerReduce(const std::vector<float>& zs, const std::vector<float>& Ts, float zBottom, float zTop) {
    LayerResult r{zTop, Ts.empty() ? 293.15f : Ts.front(), Ts.empty() ? 293.15f : Ts.front()};
    if (Ts.size() < 2) return r;
    float H = zTop - zBottom;
    float dz = H / Ts.size();
    double I1 = 0, I2 = 0;
    for (float T : Ts) { I1 += T * dz; I2 += dz / T; }
    float Tl = Ts.front();
    double den = I1 + I2 * Tl * Tl - 2 * Tl * H;
    float zi = den != 0 ? (float)(Tl * (I1 * I2 - (double)H * H) / den) : H;
    zi = std::clamp(zi, 0.0f, H);
    r.height = zBottom + zi;
    r.lower = Tl;
    double up = 0, cnt = 0;
    for (size_t q = 0; q < Ts.size(); ++q) {
        float zc = zBottom + (q + 0.5f) * dz;
        if (zc >= r.height) { up += Ts[q] * dz; cnt += dz; }
    }
    r.upper = cnt > 0 ? (float)(up / cnt) : Ts.back();
    (void)zs;
    return r;
}

inline float sampleDevice(const Solver& s, const Device& d) {
    // volume fraction from mass fraction, mixture molar mass taken as air's
    auto volFrac = [](float Y, float W) { return Y * 28.96f / W; };
    if (d.kind == DeviceKind::LayerHeight || d.kind == DeviceKind::UpperTemp || d.kind == DeviceKind::LowerTemp) {
        std::vector<float> zs, Ts;
        for (float z = d.z0 + 0.5f * s.dx; z < d.z1; z += s.dx) {
            int c = s.cellAt(d.x, d.y, z);
            if (c < 0 || s.blocked[c]) continue;
            zs.push_back(z);
            Ts.push_back(s.T[c]);
        }
        LayerResult r = layerReduce(zs, Ts, d.z0, d.z1);
        if (d.kind == DeviceKind::LayerHeight) return r.height;
        return (d.kind == DeviceKind::UpperTemp ? r.upper : r.lower) - 273.15f;
    }
    int c = s.cellAt(d.x, d.y, d.z);
    if (c < 0) return NAN;
    int i, j, k;
    s.cellIJK(c, i, j, k);
    switch (d.kind) {
    case DeviceKind::Temperature: return s.T[c] - 273.15f;
    case DeviceKind::VelU: return 0.5f * (s.u[s.xface(i, j, k)] + s.u[s.xface(i + 1, j, k)]);
    case DeviceKind::VelV: return 0.5f * (s.v[s.yface(i, j, k)] + s.v[s.yface(i, j + 1, k)]);
    case DeviceKind::VelW: return 0.5f * (s.w[s.zface(i, j, k)] + s.w[s.zface(i, j, k + 1)]);
    case DeviceKind::O2: return volFrac(s.Yo[c], 32.0f);
    case DeviceKind::CO2: return volFrac(s.Yc[c], 44.01f);
    case DeviceKind::CO: return volFrac(s.Yco[c], 28.01f);
    case DeviceKind::Soot: return s.Ys[c] * s.gasDensity(c);   // kg/m3, as FDS DENSITY of SOOT
    case DeviceKind::Pressure: return s.Pb[c] - Solver::P0 + s.gasDensity(c) * s.p[c];
    default: return NAN;
    }
}

} // namespace photon
