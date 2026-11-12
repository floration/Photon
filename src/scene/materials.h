// Photon - material library and layered assemblies (walls, ceilings, roofs,
// doors, windows). Both are loaded from text data files; every value there
// carries a source line.
#pragma once
#include <string>
#include <vector>

namespace photon {

// A gas species a material can give off (data/gases.txt).
struct Gas {
    std::string id, name, formula, source;
    float molarMass = 0;            // g/mol
    float heatOfCombustion = 0;     // J/kg, 0 = does not burn
    float autoignition = 0;         // K, 0 = n/a
    float lfl = 0, ufl = 0;         // volume fraction in air
    float idlh = 0;                 // ppm, 0 = not listed
};

struct Offgas {
    int gas = -1;
    float massFraction = 0;         // of the material's off-gas
};

struct Material {
    std::string id, name;
    // Solid thermal properties
    float density = 500;          // kg/m3
    float conductivity = 0.1f;    // W/(m K)
    float specificHeat = 1000;    // J/(kg K)
    float emissivity = 0.9f;
    // Burning (prototype model)
    bool combustible = false;
    float pyrolysisTemp = 600;      // K, surface temperature at which off-gassing starts
    float heatOfGasification = 2e6; // J/kg
    float heatOfCombustion = 15e6;  // J/kg of off-gas, effective
    float sootYield = 0.01f;        // kg soot / kg burned
    float aerosolYield = 0.02f;     // kg light pyrolysis aerosol / kg off-gassed
    float charYield = 0.0f;         // kg char (or inert residue) / kg original
    float surfaceDepth = 0.005f;    // m, heated surface layer used by the lumped node
    // Porous fuel (needles, excelsior, loose fill): an item part of this material
    // fills its cells as fuel in open air instead of a solid block. Exposed
    // surface area per kg of fuel (m2/kg); 0 = solid.
    float porousArea = 0;
    // Sub-grid flame spread over a surface inside one grid cell (MODELED, see
    // docs/FLASHOVER.md): m/s sideways/downward and upward with no external heat.
    float spreadLateral = 0.003f, spreadUp = 0.03f;
    // Pyrolysis kinetics (Phase 4): one first-order Arrhenius reaction whose
    // mass-loss rate peaks at pyrolysis_temp in a 5 K/min TGA run and spans
    // pyrolysis_range kelvin (A and E derived as in the FDS User Guide).
    float pyrolysisRange = 80.0f;
    // Thermoplastics melt and slump as they burn: the flaming area is this
    // fraction of the item's outline (1 = chars in place, like wood).
    float meltBurnArea = 1.0f;
    float criticalFlux = 0;         // W/m2 a lining needs to keep burning (0 = none)
    // Non-combustible failure (fall-off, crack-out, melt); 0 = never
    float failTemp = 0;             // K
    float failTime = 0;             // s above failTemp before the layer is gone
    float color[3] = {0.7f, 0.7f, 0.7f};
    std::vector<Offgas> offgas;     // what it gives off when heated, by mass fraction
    // Structural values (0 = not a structural material). Reference design values
    // as published; the structure model converts them to mean ultimate strength.
    float designFb = 0, designFc = 0, designFcPerp = 0;   // Pa: bending, compression parallel, compression perpendicular
    float modulusE = 0, modulusEmin = 0;                  // Pa
    float panelLoad = 0, panelRefThickness = 0;           // Pa ultimate uniform load at the reference thickness (panels)
    float fastenerPull = 0, fastenerDensity = 0;          // N per fastener pull-through, fasteners per m2 (attached boards)
    std::string source;
};

struct Layer {
    int material = -1;
    float thickness = 0;    // m
    float fill = 1;         // volume fraction of the layer that is material (studs in a cavity)
    float rValue = -1;      // m2 K / W; <0 = thickness / conductivity
};

struct Assembly {
    std::string id, name;
    std::vector<Layer> layers;  // ordered from the "inside" face to the "outside" face
    float leakage = 0;          // open area fraction while intact (cracks, undercuts)
    std::string source;
};

// A standard component at its actual size (from data/components.txt).
struct Component {
    std::string id, name, source;
    float thickness = 0, width = 0;   // m
};

// A premade object (furniture, appliance, fixture) from data/assets.txt.
// Parts are boxes in a local frame: origin at the footprint center on the
// floor, x across the front, y front (-) to back (+), z up. Meters.
struct AssetPart { int material = -1; float mass = 0; float box[6]; };
struct Asset {
    std::string id, name, source;
    std::vector<AssetPart> parts;
};

// Versioned text formats: each file starts with "photon_<kind> <version>".
constexpr int ASSETS_VERSION = 1, GASES_VERSION = 1, MATERIALS_VERSION = 2, ASSEMBLIES_VERSION = 1, COMPONENTS_VERSION = 1, SCENE_VERSION = 1;

struct MaterialLibrary {
    std::vector<Gas> gases;
    std::vector<Material> materials;
    std::vector<Assembly> assemblies;
    std::vector<Component> components;
    std::vector<Asset> assets;

    bool loadGases(const std::string& path, std::string& err);       // load before materials
    bool loadMaterials(const std::string& path, std::string& err);
    int findGas(const std::string& id) const;
    bool loadAssemblies(const std::string& path, std::string& err);
    bool loadComponents(const std::string& path, std::string& err);
    const Component* component(const std::string& id) const;
    bool loadAssets(const std::string& path, std::string& err);     // load after materials
    const Asset* asset(const std::string& id) const;
    int findMaterial(const std::string& id) const;
    int findAssembly(const std::string& id) const;
    float arealMass(const Layer& l) const;
    float layerR(const Layer& l) const;
};

} // namespace photon
