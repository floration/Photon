// Photon - solver core.
//
// GAS PHASE (Phase 3, gasphase.cpp): low Mach number flow on a staggered
// uniform grid. Density follows the ideal gas law at the background pressure
// of each pressure zone; heat release and added gas drive the velocity
// divergence; buoyancy uses the full density difference with the baroclinic
// correction; Deardorff large eddy simulation for sub-grid mixing; second
// order flux-limited transport of temperature and species; a preconditioned
// conjugate gradient pressure solve; open atmospheric boundaries with
// optional wind; leakage through cracks by the orifice equation.
// See docs/GAS_PHASE.md for the equations and what differs from FDS.
//
// HEAT AND BURNING are still the Phase 1 prototype (combustion, near-field
// flame radiation, solids, barriers): PROTOTYPE PHYSICS - NOT VALIDATED, to be
// replaced in Phase 4. Walls, ceilings, roof, doors and windows are thin
// layered barriers on cell faces. Furniture is solid cells; floor coverings
// and roof trusses are porous fuel inside gas cells. Solids heat, off-gas,
// lose mass and are removed when consumed; only the off-gases burn.
#pragma once
#include "core/jobs.h"
#include "scene/materials.h"
#include "scene/scene.h"
#include <algorithm>
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

// One level of the pressure multigrid preconditioner (pressure_mg.cpp).
struct PressureLevel {
    int n = 0;                       // unknowns
    int nx = 0, ny = 0, nz = 0;      // level 0 only: the structured grid
    std::vector<float> ax, ay, az;   // level 0 only: face coefficients
    std::vector<int> rowStart, col;  // coarse levels: sparse couplings (off-diagonal, positive)
    std::vector<float> val;
    std::vector<int> bi, bj, bk;     // coarse levels: block coordinate of each unknown
    std::vector<int> agg;            // unknown -> unknown on the next coarser level
    std::vector<float> diag, x, b, r, t;
};

// Fidelity profile: the one solver run at reference or realtime settings.
struct FidelityProfile {
    std::string name = "realtime";
    float dx = 0.2f;            // m, cell size
    float marginXY = 1.6f;      // m of outside air around the eaves
    float marginTop = 2.6f;     // m of outside air above the ridge
    float cfl = 0.8f;           // Courant number limit
    float dtMax = 0.05f;        // s
    int pressureMaxIters = 80;  // conjugate gradient iterations per step
    float pressureTol = 0.02f;  // 1/s, largest allowed velocity divergence error
    static FidelityProfile realtime() { return FidelityProfile(); }
    static FidelityProfile reference() {
        FidelityProfile p;
        p.name = "reference";
        p.dx = 0.1f;
        p.marginXY = 2.0f;
        p.marginTop = 4.0f;
        p.cfl = 0.5f;
        p.dtMax = 0.02f;
        p.pressureMaxIters = 400;
        p.pressureTol = 0.002f;
        return p;
    }
};

// Wind at the open boundaries: power-law profile, speed given at 10 m height.
struct Wind {
    float speed = 0;            // m/s at 10 m
    float fromDeg = 0;          // direction it blows FROM, degrees clockwise from north (+y)
    float exponent = 0.25f;     // suburban terrain (verify against ASCE 7)
};

// Prescribed gas burner (Phase 3 verification and FDS comparison): fuel gas
// released over a horizontal rectangle with a t-squared ramp to its peak.
struct Burner {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0, z = 0;   // world m
    float peakW = 0;            // W
    float rampS = 0;            // s to reach peak (t-squared); 0 = step
    float start = 0, duration = 1e9f;
    std::string label;
    std::vector<int> cells;     // gas cells it releases into (set by addBurner)
    float powerAt(float t) const {
        if (t < start || t > start + duration) return 0;
        float r = rampS > 0 ? std::min(1.0f, (t - start) / rampS) : 1.0f;
        return peakW * r * r;
    }
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
    double fuelIn = 0;       // kg of fuel gas released by burners and ignitions, cumulative
    double fuelBurned = 0;   // kg burned, cumulative
    double fuelInDomain = 0; // kg of unburned fuel gas in the grid now
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
    // generic fuel gas CH1.7O0.4 (Phase 3; per-material gases come in Phase 4)
    static constexpr float S_O2 = 1.95f;       // kg O2 per kg fuel, stoichiometric
    static constexpr float Y_CO2 = 2.19f;      // kg CO2 per kg fuel, complete combustion
    static constexpr float Y_H2O = 0.761f;     // kg H2O per kg fuel
    static constexpr float C_FRAC = 0.597f;    // carbon mass fraction of the fuel
    static constexpr float CO_YIELD = 0.02f;   // kg CO per kg fuel, well ventilated (SFPE Handbook generic; verify)
    static constexpr float YCO2_AMB = 0.0006f; // ~400 ppm by volume
    static constexpr float YH2O_AMB = 0.0073f; // 50% RH at 20 C
    static constexpr float GAMMA = CP / (CP - RS);   // consistent with cp and R of the gas model
    static constexpr float CHI_R = 0.30f;      // radiative fraction of flame heat
    static constexpr float T_CRIT = 1500.0f;   // critical flame temperature, K
    static constexpr float T_AUTO = 800.0f;    // fuel gas auto-ignition, K
    static constexpr float TAU_MIX = 0.1f;     // s, mixing time at 20 cm
    static constexpr float SIGMA = 5.670374e-8f;

    FidelityProfile profile;   // set before build()
    Wind wind;
    // Gas-phase comparison switches (Phase 3 FDS cases): no radiant fraction
    // (all heat stays in the gas) and no heat exchange with any surface.
    bool radiationOn = true;
    bool adiabaticSurfaces = false;
    void clearLeakage();       // every crack closed (FDS cases without leakage)

    bool build(const Scene& scene, const MaterialLibrary& lib, std::string& err);   // discretize.cpp
    float step(float dtMax);                                                       // returns dt
    void addIgnition(int cell, float power, float duration, const std::string& label);
    int addBurner(const Burner& b);   // returns index, -1 if it covers no gas cell
    // Full solver state, versioned (state.cpp). loadState() needs a solver built
    // from the same scene and profile; it refuses anything else with a message.
    bool saveState(const std::string& path, std::string& err) const;
    bool loadState(const std::string& path, std::string& err);
    static constexpr uint32_t STATE_VERSION = 1;
    bool toggleOpening(int opening);   // door: open/close, window: break; returns new "open" state
    // used by the structure model
    void addEvent(const std::string& key, const std::string& text) { logEvent(key, text); }
    void removeLayer(int barrier, int layer);    // a sheet in this assembly fell away
    void failBarrier(int barrier);               // the whole assembly fell away: open the face
    void moveFuel(int from, int to, float mass); // falling framing carries its fuel

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
    float gasDensity(int c) const { return Pb[c] / (RS * T[c]); }
    // barrier index on the face between cell c and neighbor in direction d (0..5: -x,+x,-y,+y,-z,+z), or -1
    int barrierOn(int c, int d) const;
    double zoneVolume(int z) const { return z >= 0 && z < (int)zoneVol_.size() ? zoneVol_[z] : 0.0; }
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
    std::vector<float> Yc, Yw, Yco;      // CO2, H2O, CO mass fractions
    std::vector<float> nu;               // m2/s, eddy viscosity (Deardorff LES), cell centered
    std::vector<float> Pb;               // Pa, background pressure of the cell's zone
    std::vector<int> zone;               // pressure zone per gas cell, -1 solid
    std::vector<float> zoneP;            // Pa per zone
    std::vector<uint8_t> zoneOpen;       // zone touches the open boundary
    std::vector<float> massSrc;          // kg of gas added to the cell this step (expansion)
    std::vector<Burner> burners;
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
    float maxNu = 0;
    float maxDiv = 0;                    // 1/s, largest expansion source last step
    int pressureIters = 0;               // conjugate gradient iterations used last step
    float pressureResidual = 0;          // 1/s, largest divergence error left last step
    int leakFaces = 0;                   // crack faces handled by the orifice equation
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
    std::vector<float> tmp_[12];
    std::vector<double> zoneVol_;        // m3 per zone
    std::vector<float> zoneDP_;          // Pa/s per zone this step
    std::vector<float> u2_, v2_, w2_, rhs_;
    std::vector<float> ax_, ay_, az_;    // pressure-matrix coefficient per face (open, not leak)
    std::vector<uint8_t> lx_, ly_, lz_;  // leak face: flow by the orifice equation
    std::vector<float> cgR_, cgZ_, cgP_, cgQ_;
    std::vector<PressureLevel> mg_;
    bool mgDirty_ = true;
    void buildPressureLevels();
    void pressurePrecondition(const std::vector<float>& r, std::vector<float>& z);
    bool zonesDirty_ = true;
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
    // gas phase (gasphase.cpp)
    void injectBurners(float dt);
    void turbulence();
    void heatDiffusion(float dt);
    void energyAndExpansion(float dt);
    void momentum(float dt);
    void leakage(float dt);
    void project(float dt);
    void advectScalars(float dt);
    void computeZones();
    float windAt(int axis, int side, float z) const;   // inflow velocity on a boundary face, 0 if none
    float stableDt(float dtMax) const;
    void gatherStats(float dt);
    bool lineOfSight(int s, int t) const;
    void addGasSpecies(int c, float fuelGen, float aerosol, float sootPot);
    float cellSpeed(int c) const;
    void unblockCell(int c);
};

} // namespace photon
