// Photon - Phase 1 prototype solver.
//
// PROTOTYPE PHYSICS - NOT VALIDATED. This is the first, crudest version of the
// one solver. It gets fire behavior roughly right in shape and wrong in
// numbers. Every simplification is listed in docs/PROTOTYPE_PHYSICS.md and is
// replaced by the reference model in later phases.
//
// Uniform grid of cubic cells. Gas: staggered velocities, buoyancy, a pressure
// projection with a heat-release expansion source, open atmospheric
// boundaries, upwind transport of temperature, oxygen, fuel gas, soot and
// pyrolysis aerosol. Walls, ceilings, roof, doors and windows are thin layered
// barriers on cell faces. Furniture is solid cells; floor coverings and roof
// trusses are porous fuel inside gas cells. Solids heat, off-gas, lose mass
// and are removed when consumed; only the off-gases burn.
#pragma once
#include "core/jobs.h"
#include "scene/materials.h"
#include "scene/scene.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace photon {

enum class BarrierKind : uint8_t { Wall, Ceiling, Floor, Roof, Soffit, Gable, Fascia, Door, Window };
const char* barrierKindName(BarrierKind k);

constexpr int MAX_LAYERS = 6;

struct Barrier {
    int assembly = -1;
    uint8_t axis = 0;           // 0 x-face, 1 y-face, 2 z-face
    int face = -1;              // index into that axis' face arrays
    int cellA = -1, cellB = -1; // lower / upper cell along the axis
    bool aIsInside = true;      // assembly layer 0 faces cellA
    BarrierKind kind = BarrierKind::Wall;
    int opening = -1;           // scene opening (door / window), -1 none
    int room = -1;              // scene room on the inside face, -1 none
    int front = 0, back = 0;    // intact layers [front, back] in assembly order
    float T[2] = {293.15f, 293.15f};     // node temperature: inside face, outside face
    float mass[MAX_LAYERS] = {};         // remaining burnable (or failing) kg/m2 per layer
    float damage[MAX_LAYERS] = {};
    float radIn[2] = {0, 0};             // radiant energy received this step (J)
    float leak = 0;             // open fraction while intact
    bool failed = false;        // every layer gone: the face is open
    bool userOpen = false;      // door opened by the user
    int cellOn(int side) const { return (side == 0) == aIsInside ? cellA : cellB; }
};

struct Ignition {
    int cell = -1;
    float power = 0;      // W
    float start = 0, duration = 0;
    std::string label;
};

struct SimEvent {
    float time;
    std::string text;
};

struct SimStats {
    float hrr = 0;           // W, total heat release rate in the gas
    float maxGasT = 293.15f; // K
    float pyrolysis = 0;     // kg/s total off-gas production
    int burningCells = 0;
    int failedWindows = 0, failedCeilings = 0, failedDoors = 0, roofHoles = 0;
    float stepMs = 0;        // wall time of the last step
};

class Solver {
public:
    // --- configuration (constants of the prototype model) -------------------
    static constexpr float T0 = 293.15f;       // ambient K
    static constexpr float P0 = 101325.0f;     // Pa
    static constexpr float RS = 287.0f;        // J/(kg K) air
    static constexpr float CP = 1100.0f;       // J/(kg K) gas
    static constexpr float G = 9.81f;
    static constexpr float YO2_AMB = 0.232f;
    static constexpr float DHC_FUEL = 25.0e6f; // J/kg of the generic prototype fuel gas
    static constexpr float DHC_O2 = 13.1e6f;   // J/kg O2 consumed (Huggett)
    static constexpr float CHI_R = 0.30f;      // radiative fraction of flame heat
    static constexpr float T_CRIT = 1500.0f;   // critical flame temperature, K
    static constexpr float T_AUTO = 800.0f;    // fuel gas auto-ignition, K
    static constexpr float TAU_MIX = 0.1f;     // s, mixing time at 20 cm
    static constexpr float SIGMA = 5.670374e-8f;

    bool build(const Scene& scene, const MaterialLibrary& lib, std::string& err);   // discretize.cpp
    float step(float dtMax);                                                       // returns dt
    void addIgnition(int cell, float power, float duration, const std::string& label);
    bool toggleOpening(int opening);   // door: open/close, window: break; returns new "open" state

    // --- geometry helpers ----------------------------------------------------
    int nx = 0, ny = 0, nz = 0, N = 0;
    float dx = 0.2f;
    float ox = 0, oy = 0, oz = 0;      // world position of cell (0,0,0) corner
    int kCeil = 0;                     // top-story ceiling face (attic floor)
    std::vector<int> kFloorS, kCeilS;  // per story: first cell layer, ceiling face
    int cell(int i, int j, int k) const { return i + nx * (j + ny * k); }
    void cellIJK(int c, int& i, int& j, int& k) const { i = c % nx; j = (c / nx) % ny; k = c / (nx * ny); }
    int xface(int i, int j, int k) const { return i + (nx + 1) * (j + ny * k); }
    int yface(int i, int j, int k) const { return i + nx * (j + (ny + 1) * k); }
    int zface(int i, int j, int k) const { return i + nx * (j + ny * k); }
    int cellAt(float x, float y, float z) const;
    float gasDensity(int c) const { return P0 / (RS * T[c]); }
    // barrier index on the face between cell c and neighbor in direction d (0..5: -x,+x,-y,+y,-z,+z), or -1
    int barrierOn(int c, int d) const;
    bool barrierIntact(int b) const { return b >= 0 && !barriers[b].failed && !barriers[b].userOpen; }

    // --- state -----------------------------------------------------------------
    const Scene* scene = nullptr;
    const MaterialLibrary* lib = nullptr;
    float time = 0;
    std::vector<float> u, v, w;          // face velocities (m/s)
    std::vector<float> fx, fy, fz;       // face open fractions
    std::vector<int> bx, by, bz;         // barrier index per face, -1 none
    std::vector<float> p, S;             // pressure, expansion source (1/s)
    std::vector<float> T, Yo, Yf, Ys, Ya, Yp;   // gas: K, O2, fuel, soot, aerosol, soot potential (mass fractions)
    std::vector<float> Q;                // gas heat this step (J)
    std::vector<float> Qr;               // flame radiant energy this step (J)
    std::vector<float> hrr;              // W/m3, last step
    std::vector<uint8_t> lit, pilot;
    // solids
    std::vector<uint8_t> blocked, porous;
    std::vector<int16_t> fuelMat;        // material of solid / porous fuel, -1 none
    std::vector<int16_t> itemOf;         // scene item, -1 none
    std::vector<float> fuelMass, fuelMass0, charMass, fuelT, fuelArea, radQ, condQ;
    std::vector<int> solidCells;
    std::vector<float> itemMass0, itemPyro;   // per item: initial mass, off-gassed mass
    std::vector<uint8_t> itemFlags;
    std::vector<Barrier> barriers;
    std::vector<Ignition> ignitions;
    std::vector<SimEvent> events;
    SimStats stats;
    float maxVel = 0;
    int pressureIters = 40;
    // profiling: accumulated ms per phase (0 setup, 1 combustion, 2 flame radiation,
    // 3 solids, 4 barriers, 5 energy+buoyancy, 6 velocity advection, 7 pressure,
    // 8 scalar advection, 9 stats)
    float phaseMs[10] = {};
    long phaseSteps = 0;

    Solver();
    explicit Solver(int threads);   // background jobs use fewer threads
    ~Solver();

private:
    std::unique_ptr<JobPool> pool_;
    std::vector<float> tmp_[6];
    std::vector<float> u2_, v2_, w2_, rhs_;
    std::vector<int> burnList_;
    std::vector<std::string> eventKeys_;

    void updateFace(int axis, int f);
    void updateCellFaces(int c);
    void logEvent(const std::string& key, const std::string& text);
    std::string roomName(int room) const;

    void injectIgnitions(float dt);
    void combustion(float dt);
    void flameRadiation();
    void solidsStep(float dt);
    void barriersStep(float dt);
    void energyAndExpansion(float dt);
    void buoyancy(float dt);
    void advectVelocity(float dt);
    void project(float dt);
    void advectScalars(float dt);
    void gatherStats(float dt);
    bool lineOfSight(int s, int t) const;
    void addGasSpecies(int c, float fuelGen, float aerosol, float sootPot);
    float cellSpeed(int c) const;
    void unblockCell(int c);
};

} // namespace photon
