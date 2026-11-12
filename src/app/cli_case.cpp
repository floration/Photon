// Photon - case runner for verification and FDS comparison runs.
//
//   photon_cli --case <file.case> [--resume <state>] [--export-fds <out.fds>]
//
// A case file (metres, seconds) names a scene, profile, burner, devices and
// outputs. Lines:
//   photon_case 1
//   title "<text>"
//   scene <path>                 profile realtime|reference      dx <m>
//   duration <s>                 output <prefix>                 output_interval <s>
//   checkpoint <s>               (writes <prefix>_<t>s.state)
//   wind <m/s at 10 m> <from deg>
//   no_leakage | no_radiation | adiabatic
//   open "<opening name>"
//   burner "<label>" x0 y0 x1 y1 z peak_kW ramp_s
//   device temp|u|v|w|o2|co2|co|soot|pressure "<id>" x y z
//   layer "<id>" x y z0 z1       (layer height, upper and lower temperature)
//   tree "<id>" x y z0 z1 dz     (thermocouple tree)
//   vprofile "<id>" u|v x y z0 z1 dz   (velocity profile, e.g. across a doorway)
#include "scene/materials.h"
#include "scene/scene.h"
#include "scene/textfile.h"
#include "sim/devices.h"
#include "sim/fds_export.h"
#include "sim/solver.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>

using namespace photon;

int runCase(int argc, char** argv) {
    std::string casePath = argv[2], resume, fdsOut;
    for (int a = 3; a + 1 < argc; a += 2) {
        std::string k = argv[a];
        if (k == "--resume") resume = argv[a + 1];
        else if (k == "--export-fds") fdsOut = argv[a + 1];
    }
    TextFile cf;
    std::string err;
    if (!cf.load(casePath, err) || cf.lines.empty() || cf.lines[0].tok[0] != "photon_case") {
        std::fprintf(stderr, "case: %s\n", err.empty() ? "not a photon_case file" : err.c_str());
        return 1;
    }
    std::string scenePath = "scenes/room_burner.txt", prefix = "case", title = casePath;
    FidelityProfile prof = FidelityProfile::realtime();
    float duration = 60, checkpoint = 0, outDt = 1.0f, dxOverride = 0;
    bool noLeak = false, noRad = false, adiabatic = false, stopWhenOut = false;
    Wind wind;
    std::vector<std::string> opens;
    std::vector<Burner> burners;
    struct Ign { std::string item; float kw, dur; std::string where; };   // where: top (flame on top) or side (at the lower side)
    std::vector<Ign> ignites;
    std::vector<Device> devs;
    bool ok = true;
    auto num = [&](const std::string& s) { return (float)toNumber(s, ok); };
    for (size_t i = 1; i < cf.lines.size(); ++i) {
        const auto& t = cf.lines[i].tok;
        const std::string& k = t[0];
        if (k == "scene" && t.size() > 1) scenePath = t[1];
        else if (k == "title" && t.size() > 1) title = t[1];
        else if (k == "profile" && t.size() > 1) prof = t[1] == "reference" ? FidelityProfile::reference() : FidelityProfile::realtime();
        else if (k == "dx" && t.size() > 1) dxOverride = num(t[1]);
        else if (k == "pressure_tol" && t.size() > 2) { prof.pressureTol = num(t[1]); prof.pressureMaxIters = (int)num(t[2]); }
        else if (k == "duration" && t.size() > 1) duration = num(t[1]);
        else if (k == "output" && t.size() > 1) prefix = t[1];
        else if (k == "output_interval" && t.size() > 1) outDt = num(t[1]);
        else if (k == "checkpoint" && t.size() > 1) checkpoint = num(t[1]);
        else if (k == "wind" && t.size() > 2) { wind.speed = num(t[1]); wind.fromDeg = num(t[2]); }
        else if (k == "no_leakage") noLeak = true;
        else if (k == "no_radiation") noRad = true;
        else if (k == "adiabatic") adiabatic = true;
        else if (k == "stop_when_out") stopWhenOut = true;
        else if (k == "open" && t.size() > 1) opens.push_back(t[1]);
        else if (k == "burner" && t.size() > 8) {
            Burner b;
            b.label = t[1];
            b.x0 = num(t[2]); b.y0 = num(t[3]); b.x1 = num(t[4]); b.y1 = num(t[5]); b.z = num(t[6]);
            b.peakW = num(t[7]) * 1000.0f;
            b.rampS = num(t[8]);
            burners.push_back(b);
        } else if (k == "ignite" && t.size() > 3) {   // ignite "<item>" kW seconds: small flame on an item
            ignites.push_back({t[1], num(t[2]), num(t[3]), t.size() > 4 ? t[4] : std::string("top")});
        } else if (k == "device" && t.size() > 5) {
            static const std::pair<const char*, DeviceKind> kinds[] = {
                {"temp", DeviceKind::Temperature}, {"u", DeviceKind::VelU}, {"v", DeviceKind::VelV}, {"w", DeviceKind::VelW},
                {"o2", DeviceKind::O2}, {"co2", DeviceKind::CO2}, {"co", DeviceKind::CO}, {"soot", DeviceKind::Soot},
                {"pressure", DeviceKind::Pressure}};
            Device d;
            bool found = false;
            for (auto& kd : kinds) if (t[1] == kd.first) { d.kind = kd.second; found = true; }
            if (!found) { std::fprintf(stderr, "case line %zu: unknown device kind %s\n", i + 1, t[1].c_str()); return 1; }
            d.id = t[2]; d.x = num(t[3]); d.y = num(t[4]); d.z = num(t[5]);
            devs.push_back(d);
        } else if (k == "layer" && t.size() > 5) {
            for (int q = 0; q < 3; ++q) {
                Device d;
                d.kind = q == 0 ? DeviceKind::LayerHeight : q == 1 ? DeviceKind::UpperTemp : DeviceKind::LowerTemp;
                d.id = t[1] + (q == 0 ? "_height" : q == 1 ? "_upper" : "_lower");
                d.x = num(t[2]); d.y = num(t[3]); d.z0 = num(t[4]); d.z1 = num(t[5]);
                devs.push_back(d);
            }
        } else if (k == "tree" && t.size() > 6) {
            float x = num(t[2]), y = num(t[3]), z0 = num(t[4]), z1 = num(t[5]), dz = num(t[6]);
            for (float z = z0; z <= z1 + 1e-4f && dz > 0; z += dz) {
                Device d;
                d.kind = DeviceKind::Temperature;
                d.id = t[1] + "_" + std::to_string((int)std::lround(z * 100));
                d.x = x; d.y = y; d.z = z;
                devs.push_back(d);
            }
        } else if (k == "vprofile" && t.size() > 7) {
            float x = num(t[3]), y = num(t[4]), z0 = num(t[5]), z1 = num(t[6]), dz = num(t[7]);
            for (float z = z0; z <= z1 + 1e-4f && dz > 0; z += dz) {
                Device d;
                d.kind = t[2] == "u" ? DeviceKind::VelU : DeviceKind::VelV;
                d.id = t[1] + "_" + std::to_string((int)std::lround(z * 100));
                d.x = x; d.y = y; d.z = z;
                devs.push_back(d);
            }
        } else {
            std::fprintf(stderr, "case line %zu: cannot read '%s'\n", i + 1, k.c_str());
            return 1;
        }
    }
    if (!ok) { std::fprintf(stderr, "case: bad number\n"); return 1; }
    if (dxOverride > 0) prof.dx = dxOverride;
    MaterialLibrary lib;
    Scene scene;
    if (!lib.loadGases("data/gases.txt", err) || !lib.loadMaterials("data/materials.txt", err) || !lib.loadAssemblies("data/assemblies.txt", err) ||
        !lib.loadComponents("data/components.txt", err) || !lib.loadAssets("data/assets.txt", err) || !scene.load(scenePath, lib, err)) {
        std::fprintf(stderr, "load: %s\n", err.c_str());
        return 1;
    }
    Solver sim;
    sim.profile = prof;
    sim.wind = wind;
    sim.radiationOn = !noRad;
    sim.adiabaticSurfaces = adiabatic;
    if (!sim.build(scene, lib, err)) { std::fprintf(stderr, "build: %s\n", err.c_str()); return 1; }
    if (noLeak) sim.clearLeakage();
    for (const std::string& o : opens) {
        bool found = false;
        for (size_t q = 0; q < scene.openings.size(); ++q)
            if (scene.openings[q].name == o) {
                found = true;
                bool isOpen = false;
                for (const Barrier& b : sim.barriers) if (b.opening == (int)q && (b.userOpen || b.failed)) isOpen = true;
                if (!isOpen) sim.toggleOpening((int)q);
            }
        if (!found) { std::fprintf(stderr, "case: no opening named '%s'\n", o.c_str()); return 1; }
    }
    if (!resume.empty()) {
        if (!sim.loadState(resume, err)) { std::fprintf(stderr, "resume: %s\n", err.c_str()); return 1; }
        std::printf("resumed from %s at t = %.2f s\n", resume.c_str(), sim.time);
    } else {
        for (const Burner& b : burners)
            if (sim.addBurner(b) < 0) { std::fprintf(stderr, "burner '%s' covers no gas cell\n", b.label.c_str()); return 1; }
        for (const Ign& g : ignites) {
            int cell = -1;
            if (g.where == "side")   // lowest item cell with open air beside it
                for (int c = 0; c < sim.N && cell < 0; ++c) {
                    if (sim.itemOf[c] < 0 || scene.items[sim.itemOf[c]].name != g.item || !sim.blocked[c]) continue;
                    int i, j, k;
                    sim.cellIJK(c, i, j, k);
                    const int nb[4] = {i > 0 ? c - 1 : -1, i < sim.nx - 1 ? c + 1 : -1, j > 0 ? c - sim.nx : -1, j < sim.ny - 1 ? c + sim.nx : -1};
                    for (int q = 0; q < 4 && cell < 0; ++q) if (nb[q] >= 0 && !sim.blocked[nb[q]]) cell = nb[q];
                }
            for (int c = 0; c < sim.N && cell < 0; ++c)   // porous fuel of the item first (needles)
                if (sim.itemOf[c] >= 0 && scene.items[sim.itemOf[c]].name == g.item && !sim.blocked[c] && sim.fuelMass[c] > 0) cell = c;
            for (int c = 0; c < sim.N && cell < 0; ++c) {
                if (sim.itemOf[c] < 0 || scene.items[sim.itemOf[c]].name != g.item) continue;
                int up = c + sim.nx * sim.ny;
                if (!sim.blocked[c]) cell = c;                       // porous item: inside it
                else if (up < sim.N && !sim.blocked[up]) cell = up;   // solid item: on top of it
            }
            if (cell < 0) { std::fprintf(stderr, "no item named '%s'\n", g.item.c_str()); return 1; }
            std::printf("ignition cell %d: blocked %d porous %d fuel %.3f kg area %.3f m2 mat %d\n", cell, sim.blocked[cell], sim.porous[cell], sim.fuelMass[cell], sim.fuelArea[cell], sim.fuelMat[cell]);
            sim.addIgnition(cell, g.kw * 1000.0f, g.dur, "Small flame on " + g.item);
        }
    }
    std::printf("case %s: %s profile, grid %d x %d x %d = %d cells at %.0f cm, %zu devices\n", casePath.c_str(),
                sim.profile.name.c_str(), sim.nx, sim.ny, sim.nz, sim.N, sim.dx * 100, devs.size());
    if (!fdsOut.empty()) {
        std::string chid = fdsOut.substr(fdsOut.find_last_of("/\\") + 1);
        chid = chid.substr(0, chid.find('.'));
        if (!exportFds(sim, fdsOut, chid, title, duration, devs, err)) { std::fprintf(stderr, "fds: %s\n", err.c_str()); return 1; }
        std::printf("wrote %s\n", fdsOut.c_str());
        return 0;
    }
    std::ofstream dc(prefix + "_devc.csv", resume.empty() ? std::ios::trunc : std::ios::app);
    std::ofstream hc(prefix + "_hrr.csv", resume.empty() ? std::ios::trunc : std::ios::app);
    if (resume.empty()) {
        dc << "Time";
        for (const Device& d : devs) dc << "," << d.id;
        dc << "\n";
        hc << "Time,HRR_kW,MaxGas_C,MaxVel,PressureIters,PressureResidual,Zones,LeakFaces\n";
    }
    // per-item heat release from its pyrolysis (kW, fuel energy given off), the
    // quantity a furniture calorimeter measures for that item
    std::ofstream ic(prefix + "_items.csv", resume.empty() ? std::ios::trunc : std::ios::app);
    if (resume.empty()) {
        ic << "Time";
        for (const Item& it : scene.items) ic << "," << it.name;
        ic << "\n";
    }
    std::vector<double> lastHeat(sim.itemHeat);
    float lastT = sim.time;
    auto row = [&] {
        float dtr = std::max(1e-6f, sim.time - lastT);
        ic << sim.time;
        for (size_t q = 0; q < sim.itemHeat.size(); ++q) ic << "," << (sim.itemHeat[q] - lastHeat[q]) / dtr / 1000.0;
        ic << "\n";
        lastHeat = sim.itemHeat;
        lastT = sim.time;
        dc << sim.time;
        for (const Device& d : devs) dc << "," << sampleDevice(sim, d);
        dc << "\n";
        hc << sim.time << "," << sim.stats.hrr / 1000 << "," << sim.stats.maxGasT - 273.15f << "," << sim.maxVel << ","
           << sim.pressureIters << "," << sim.pressureResidual << "," << sim.zoneP.size() << "," << sim.leakFaces << "\n";
    };
    auto t0 = std::chrono::steady_clock::now();
    float ignEnd = 0;
    for (const Ign& g : ignites) ignEnd = std::max(ignEnd, g.dur);
    float nextOut = resume.empty() ? 0 : std::floor(sim.time / outDt + 0.5f) * outDt + outDt;
    float nextCk = checkpoint > 0 ? std::floor(sim.time / checkpoint + 0.5f) * checkpoint + checkpoint : 1e30f;
    float nextPrint = std::floor(sim.time / 10 + 1) * 10;
    long steps = 0;
    if (resume.empty()) { row(); nextOut = outDt; }
    while (sim.time < duration - 1e-5f) {
        // land exactly on output and checkpoint times so resumed runs sample the same instants
        float target = std::min({nextOut, duration, nextCk});
        sim.step(std::max(1e-4f, target - sim.time));
        ++steps;
        if (sim.time >= nextOut - 1e-5f) { row(); nextOut += outDt; }
        if (stopWhenOut && sim.time > 90.0f && sim.time > ignEnd + 30.0f && sim.stats.hrr < 1000.0f && sim.stats.pyrolysis < 1e-5f) { std::printf("fire went out at %.1f s\n", sim.time); break; }
        if (sim.time >= nextCk - 1e-5f) {
            std::string ck = prefix + "_" + std::to_string((int)std::lround(sim.time)) + "s.state";
            if (!sim.saveState(ck, err)) std::fprintf(stderr, "checkpoint: %s\n", err.c_str());
            else std::printf("  checkpoint %s\n", ck.c_str());
            nextCk += checkpoint;
        }
        if (sim.time >= nextPrint) {
            nextPrint += 10;
            double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("t=%6.1f s  HRR %7.1f kW  maxT %5.0f C  vmax %5.2f  p-iters %3d res %.4f  zones %zu  | %.1f ms/step\n",
                        sim.time, sim.stats.hrr / 1000, sim.stats.maxGasT - 273.15f, sim.maxVel, sim.pressureIters,
                        sim.pressureResidual, sim.zoneP.size(), wall * 1000 / steps);
            std::printf("          fuel in %.4f kg, burned %.4f, in grid %.4f, left the grid unburned %.4f\n", sim.stats.fuelIn,
                        sim.stats.fuelBurned, sim.stats.fuelInDomain, sim.stats.fuelIn - sim.stats.fuelBurned - sim.stats.fuelInDomain);
            {   // hottest fuel surface per item (calibration diagnostics)
                std::vector<float> hot(scene.items.size(), 0.0f);
                std::vector<float> burn(scene.items.size(), 0.0f);
                for (int c : sim.solidCells)
                    if (sim.itemOf[c] >= 0) {
                        hot[sim.itemOf[c]] = std::max(hot[sim.itemOf[c]], sim.fuelT[c] - 273.15f);
                        burn[sim.itemOf[c]] = std::max(burn[sim.itemOf[c]], sim.burnFrac[c]);
                    }
                std::printf("          surfaces:");
                for (size_t q = 0; q < hot.size(); ++q) if (hot[q] > 60) std::printf(" %s %.0f C (burning %.2f)", scene.items[q].name.c_str(), hot[q], burn[q]);
                std::printf("\n");
            }
            if (sim.maxVel > 15.0f) {   // locate the fastest face (stability diagnostics)
                float best = 0; int ax = 0, bf = 0;
                const std::vector<float>* V[3] = {&sim.u, &sim.v, &sim.w};
                const std::vector<float>* F[3] = {&sim.fx, &sim.fy, &sim.fz};
                for (int a = 0; a < 3; ++a)
                    for (size_t f = 0; f < V[a]->size(); ++f)
                        if (std::fabs((*V[a])[f]) * (*F[a])[f] > best) { best = std::fabs((*V[a])[f]) * (*F[a])[f]; ax = a; bf = (int)f; }
                int dX = sim.nx + (ax == 0), dY = sim.ny + (ax == 1);
                int i = bf % dX, j = (bf / dX) % dY, k = bf / (dX * dY);
                std::printf("          fastest face: axis %d at (%.2f, %.2f, %.2f) m, %.1f m/s, open %.3f\n", ax,
                            sim.ox + i * sim.dx, sim.oy + j * sim.dx, sim.oz + k * sim.dx, (*V[ax])[bf], (*F[ax])[bf]);
            }
            std::fflush(stdout);
        }
    }
    for (const SimEvent& e : sim.events) std::printf("  [%6.1f s] %s\n", e.time, e.text.c_str());
    std::ofstream ev(prefix + "_events.txt");
    for (const SimEvent& e : sim.events) ev << e.time << "\t" << e.text << "\n";
    std::printf("done: %ld steps, %.1f s wall\n", steps, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    return 0;
}
