// Photon - headless runner. Builds the solver without any window or GPU code,
// starts a fire and prints a time history. Used for batch runs and tuning.
//
//   photon_cli [scene] [seconds] [item-name-to-ignite] [power_kW]
#include "scene/materials.h"
#include "scene/scene.h"
#include "sim/solver.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace photon;

int main(int argc, char** argv) {
    std::string scenePath = argc > 1 ? argv[1] : "scenes/model_home.txt";
    float seconds = argc > 2 ? (float)std::atof(argv[2]) : 300.0f;
    std::string target = argc > 3 ? argv[3] : "Sofa";
    float kw = argc > 4 ? (float)std::atof(argv[4]) : 10.0f;

    MaterialLibrary lib;
    std::string err;
    if (!lib.loadMaterials("data/materials.txt", err) || !lib.loadAssemblies("data/assemblies.txt", err)) {
        std::fprintf(stderr, "data: %s\n", err.c_str());
        return 1;
    }
    Scene scene;
    if (!scene.load(scenePath, lib, err)) { std::fprintf(stderr, "scene: %s\n", err.c_str()); return 1; }
    Solver sim;
    if (!sim.build(scene, lib, err)) { std::fprintf(stderr, "build: %s\n", err.c_str()); return 1; }
    std::printf("PROTOTYPE PHYSICS - NOT VALIDATED\n");
    std::printf("grid %d x %d x %d = %d cells at %.0f cm, %zu barriers, %zu solid/porous fuel cells, %zu members\n", sim.nx,
                sim.ny, sim.nz, sim.N, sim.dx * 100, sim.barriers.size(), sim.solidCells.size(), scene.members.size());

    // ignite: gas cell beside the first exposed solid cell of the target item, on its top
    int ign = -1;
    for (int c = 0; c < sim.N && ign < 0; ++c) {
        if (sim.itemOf[c] < 0 || scene.items[sim.itemOf[c]].name != target) continue;
        int up = c + sim.nx * sim.ny;
        if (up < sim.N && !sim.blocked[up]) ign = up;
    }
    if (ign < 0) { std::fprintf(stderr, "no ignition cell for '%s'\n", target.c_str()); return 1; }
    sim.addIgnition(ign, kw * 1000.0f, 120.0f, "Small open flame");

    auto t0 = std::chrono::steady_clock::now();
    float nextPrint = 0;
    size_t ev = 0;
    long steps = 0;
    while (sim.time < seconds) {
        sim.step(0.05f);
        ++steps;
        for (; ev < sim.events.size(); ++ev)
            std::printf("  [%6.1f s] %s\n", sim.events[ev].time, sim.events[ev].text.c_str());
        if (sim.time >= nextPrint) {
            nextPrint += 10.0f;
            double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            std::printf("t=%6.1f s  HRR %8.1f kW  maxT %6.0f C  burning %5d  pyro %.3f kg/s  vmax %5.2f  "
                        "win %d ceil %d  | %ld steps, %.1f ms/step, %.2fx realtime\n",
                        sim.time, sim.stats.hrr / 1000, sim.stats.maxGasT - 273.15f, sim.stats.burningCells,
                        sim.stats.pyrolysis, sim.maxVel, sim.stats.failedWindows, sim.stats.failedCeilings, steps,
                        wall * 1000 / steps, sim.time / wall);
            std::fflush(stdout);
        }
    }
    return 0;
}
