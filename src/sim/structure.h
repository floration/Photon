// Photon - structure: weight, support and gravity for the house's objects.
//
// FIRST VERSION (prototype, not validated). Each physical object gets its real
// mass, a list of the objects that hold it up (fastened to framing or resting
// on what is below), and an integrity read from the fire solver (how much of
// it has been consumed). Weight is summed down the support graph; a piece fails
// when its remaining section cannot carry its load, and anything left with no
// standing support falls under gravity and lands as debris. Falling pieces
// open holes in the fire grid and carry their fuel with them.
//
// Not yet: beam bending and sag, connection (gusset plate) pull-out, load
// sharing, lateral stability. Those are the Phase 12 structural model.
#pragma once
#include "scene/scene.h"
#include "sim/solver.h"
#include <string>
#include <vector>

namespace photon {

enum class PieceState : uint8_t { Standing, Falling, Fallen };

struct Piece {
    int object = -1;              // index into Scene::objects
    float mass = 0;               // kg
    bool ground = false;          // slab: always supported
    bool framing = false;         // carries load for others
    std::vector<int> supports;    // pieces that hold this one up
    float integrity = 1;          // remaining fraction of section / material
    float load = 0;               // kg carried, own weight included
    float capacity = 0;           // kg it can carry when intact (prototype)
    PieceState state = PieceState::Standing;
    float dz = 0, vz = 0;         // fall offset (m) and velocity (m/s)
    float zmin = 0, zmax = 0, bb[4] = {0, 0, 0, 0};   // world bounds (x0, y0, x1, y1)
    // where this piece lives in the fire grid
    std::vector<int> cells;       // porous / solid cells it occupies
    std::vector<int> barriers;    // barrier faces it is part of
    std::vector<int> layers;      // the assembly layer for each barrier
};

class Structure {
public:
    bool build(const Scene& scene, const MaterialLibrary& lib, const Solver& sim);
    // Reads integrity from the solver, computes loads, fails and drops pieces,
    // and pushes the consequences (holes, moved fuel) back into the solver.
    void update(Solver& sim, float dt);
    const std::vector<Piece>& pieces() const { return pieces_; }
    int pieceOfObject(int obj) const { return obj >= 0 && obj < (int)byObject_.size() ? byObject_[obj] : -1; }
    int fallenCount() const { return fallen_; }

private:
    const Scene* scene_ = nullptr;
    const MaterialLibrary* lib_ = nullptr;
    std::vector<Piece> pieces_;
    std::vector<int> byObject_;
    std::vector<int> order_;      // pieces sorted from the top down (load summation)
    int fallen_ = 0;
    std::vector<std::string> reported_;
    void readIntegrity(const Solver& sim);
    void report(Solver& sim, const std::string& key, const std::string& text);
};

} // namespace photon
