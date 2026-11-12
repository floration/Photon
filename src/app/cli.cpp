// Photon - headless runner. Builds the solver without any window or GPU code,
// starts a fire and prints a time history. Used for batch runs and tuning.
//
//   photon_cli [scene] [seconds] [items to ignite, comma separated] [power_kW] [max wall seconds]
#include "scene/materials.h"
#include "scene/textfile.h"
#include "scene/scene.h"
#include "sim/solver.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>

using namespace photon;

// ---------------------------------------------------------------------------
// Background job mode:  photon_cli --job <file.job>
// The viewer writes the job file; this process runs it at low priority and
// reports through <file>.status. Writing "pause", "resume" or "cancel" into
// <file>.control steers it. Results go to <file>.csv and <file>.events.txt.
static int runJob(const std::string& jobPath) {
    auto status = [&](const std::string& state, const Solver* s, double wall) {
        std::ofstream f(jobPath + ".status.tmp");
        f << "state " << state << "\n";
        if (s) {
            f << "sim_time " << s->time << "\nwall " << wall << "\nhrr_kw " << s->stats.hrr / 1000
              << "\nmax_c " << s->stats.maxGasT - 273.15f << "\nevents " << s->events.size() << "\n";
        }
        f.close();
        std::remove((jobPath + ".status").c_str());
        std::rename((jobPath + ".status.tmp").c_str(), (jobPath + ".status").c_str());
    };
    TextFile jf;
    std::string err;
    if (!jf.load(jobPath, err) || jf.lines.empty() || jf.lines[0].tok[0] != "photon_job") {
        status("failed: cannot read job file", nullptr, 0);
        return 1;
    }
    std::string scenePath = "scenes/model_home.txt";
    float duration = 600;
    int threads = std::max(1, (int)std::thread::hardware_concurrency() / 2);
    struct Ign { int cell; float power, dur; std::string label; };
    std::vector<Ign> igns;
    std::vector<int> toggles;
    bool ok = true;
    for (size_t i = 1; i < jf.lines.size(); ++i) {
        const auto& t = jf.lines[i].tok;
        if (t[0] == "scene" && t.size() > 1) scenePath = t[1];
        else if (t[0] == "duration" && t.size() > 1) duration = (float)toNumber(t[1], ok);
        else if (t[0] == "threads" && t.size() > 1) threads = (int)toNumber(t[1], ok);
        else if (t[0] == "ignite" && t.size() > 4)
            igns.push_back({(int)toNumber(t[1], ok), (float)toNumber(t[2], ok), (float)toNumber(t[3], ok), t[4]});
        else if (t[0] == "toggle" && t.size() > 1) toggles.push_back((int)toNumber(t[1], ok));
    }
    MaterialLibrary lib;
    Scene scene;
    if (!lib.loadGases("data/gases.txt", err) || !lib.loadMaterials("data/materials.txt", err) || !lib.loadAssemblies("data/assemblies.txt", err) ||
        !lib.loadComponents("data/components.txt", err) || !lib.loadAssets("data/assets.txt", err) || !scene.load(scenePath, lib, err)) {
        status("failed: " + err, nullptr, 0);
        return 1;
    }
    Solver sim(threads);
    if (!sim.build(scene, lib, err)) { status("failed: " + err, nullptr, 0); return 1; }
    for (int o : toggles) sim.toggleOpening(o);
    for (const Ign& g : igns) sim.addIgnition(g.cell, g.power, g.dur, g.label);
    std::ofstream csv(jobPath + ".csv");
    csv << "# PROTOTYPE PHYSICS - NOT VALIDATED\ntime_s,hrr_kw,max_gas_c,burning_cells,pyrolysis_kg_s\n";
    auto t0 = std::chrono::steady_clock::now();
    auto lastStatus = t0;
    float nextRow = 0;
    bool paused = false;
    std::string state = "running";
    while (sim.time < duration) {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - lastStatus).count() > 0.5) {
            lastStatus = now;
            std::ifstream ctl(jobPath + ".control");
            std::string cmd;
            if (ctl >> cmd) {
                if (cmd == "pause") paused = true;
                else if (cmd == "resume") paused = false;
                else if (cmd == "cancel") { state = "cancelled"; break; }
            }
            status(paused ? "paused" : "running", &sim, std::chrono::duration<double>(now - t0).count());
        }
        if (paused) { std::this_thread::sleep_for(std::chrono::milliseconds(200)); continue; }
        sim.step(0.05f);
        if (sim.time >= nextRow) {
            nextRow += 1.0f;
            csv << sim.time << "," << sim.stats.hrr / 1000 << "," << sim.stats.maxGasT - 273.15f << ","
                << sim.stats.burningCells << "," << sim.stats.pyrolysis << "\n";
        }
    }
    std::ofstream ev(jobPath + ".events.txt");
    for (const SimEvent& e : sim.events) ev << e.time << "\t" << e.text << "\n";
    if (state == "running") state = "done";
    status(state, &sim, std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 2 && std::string(argv[1]) == "--job") return runJob(argv[2]);
    std::string scenePath = argc > 1 ? argv[1] : "scenes/model_home.txt";
    float seconds = argc > 2 ? (float)std::atof(argv[2]) : 300.0f;
    std::string target = argc > 3 ? argv[3] : "Sofa";
    float kw = argc > 4 ? (float)std::atof(argv[4]) : 10.0f;
    double wallLimit = argc > 5 ? std::atof(argv[5]) : 1e9;

    MaterialLibrary lib;
    std::string err;
    if (!lib.loadGases("data/gases.txt", err) || !lib.loadMaterials("data/materials.txt", err) || !lib.loadAssemblies("data/assemblies.txt", err) ||
        !lib.loadComponents("data/components.txt", err) || !lib.loadAssets("data/assets.txt", err)) {
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

    // ignite: gas cell above the first exposed solid cell of each target item
    size_t start = 0;
    while (start <= target.size()) {
        size_t comma = target.find(',', start);
        std::string name = target.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        start = comma == std::string::npos ? target.size() + 1 : comma + 1;
        int ign = -1;
        for (int c = 0; c < sim.N && ign < 0; ++c) {
            if (sim.itemOf[c] < 0 || scene.items[sim.itemOf[c]].name != name) continue;
            int up = c + sim.nx * sim.ny;
            if (up < sim.N && !sim.blocked[up]) ign = up;
        }
        if (ign < 0) { std::fprintf(stderr, "no ignition cell for '%s'\n", name.c_str()); return 1; }
        sim.addIgnition(ign, kw * 1000.0f, 120.0f, "Open flame");
    }

    auto t0 = std::chrono::steady_clock::now();
    float nextPrint = 0;
    size_t ev = 0;
    long steps = 0;
    const char* phaseNames[10] = {"setup", "combustion", "flame radiation", "solids", "barriers", "energy+buoyancy", "velocity advect", "pressure", "scalar advect", "stats"};
    auto breakdown = [&] {
        float tot = 0;
        for (float m : sim.phaseMs) tot += m;
        std::printf("  per-step ms:");
        for (int i = 0; i < 10; ++i) std::printf(" %s %.2f |", phaseNames[i], sim.phaseMs[i] / std::max(1L, sim.phaseSteps));
        std::printf(" total %.2f\n", tot / std::max(1L, sim.phaseSteps));
        for (float& m : sim.phaseMs) m = 0;
        sim.phaseSteps = 0;
    };
    while (sim.time < seconds && std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < wallLimit) {
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
            breakdown();
            std::fflush(stdout);
        }
    }
    breakdown();
    return 0;
}
