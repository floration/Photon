// Photon - scene description. The scene is exact: every dimension is the real
// one (a 2x4 stud is 1.5 x 3.5 in). The solver grid is derived from it by the
// discretizer. Internally all lengths are meters, with the origin at the
// south-west exterior corner of the house at slab level, x east, y north, z up.
#pragma once
#include "scene/materials.h"
#include <string>
#include <vector>

namespace photon {

struct Box3 { float x0, y0, z0, x1, y1, z1; };

struct Wall {
    int assembly = -1;
    float x0, y0, x1, y1;   // centerline, axis-aligned
    bool exterior = false;
};

enum class OpeningKind { Door, Window, Cased };

struct Opening {
    OpeningKind kind = OpeningKind::Door;
    int assembly = -1;      // -1 for a cased (doorless) opening
    std::string name;
    float x0, y0, x1, y1;   // along a wall centerline
    float sill = 0, head = 2.03f;
    bool open = false;
};

struct Room {
    std::string name;
    float x0, y0, x1, y1;
    int floorMaterial = -1;     // floor covering (carpet, vinyl), -1 none
    float floorArealMass = 0;   // kg/m2
};

struct Part {
    int material = -1;
    float mass = 0;   // kg
    Box3 box;
};

struct Item {
    std::string name;
    std::vector<Part> parts;
};

enum class MemberKind { Stud, Plate, Header, TrussChord, TrussWeb, Gusset };

// A straight framing member: centerline p0->p1, cross-section `width` along
// `side` and `depth` along cross(dir, side).
struct Member {
    MemberKind kind;
    int material = -1;
    float p0[3], p1[3];
    float side[3];
    float width, depth;
};

struct Scene {
    std::string name;
    float width = 15.24f, depth = 9.144f;     // footprint (m)
    float ceilingHeight = 2.438f;
    float pitch = 0.5f;                       // rise / run
    float eaveOverhang = 0.406f;
    float trussSpacing = 0.610f, studSpacing = 0.406f;
    float atticVentRatio = 150;               // 1 sq ft net free vent area per N sq ft of attic floor
    int extWall = -1, ceiling = -1, roof = -1, soffit = -1, gable = -1, fascia = -1;
    int studMaterial = -1, trussMaterial = -1, gussetMaterial = -1;
    std::vector<Wall> walls;
    std::vector<Opening> openings;
    std::vector<Room> rooms;
    std::vector<Item> items;
    std::vector<Member> members;              // generated framing

    bool load(const std::string& path, const MaterialLibrary& lib, std::string& err);
    float ridgeHeight() const { return ceilingHeight + pitch * depth * 0.5f; }
    const Room* roomAt(float x, float y) const;

private:
    void generateFraming(const MaterialLibrary& lib);
};

} // namespace photon
