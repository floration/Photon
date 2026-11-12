// Photon - material library and layered assemblies (walls, ceilings, roofs,
// doors, windows). Both are loaded from text data files; every value there
// carries a source line.
#pragma once
#include <string>
#include <vector>

namespace photon {

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
    // Non-combustible failure (fall-off, crack-out, melt); 0 = never
    float failTemp = 0;             // K
    float failTime = 0;             // s above failTemp before the layer is gone
    float color[3] = {0.7f, 0.7f, 0.7f};
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

// Versioned text formats: each file starts with "photon_<kind> <version>".
constexpr int MATERIALS_VERSION = 1, ASSEMBLIES_VERSION = 1, COMPONENTS_VERSION = 1, SCENE_VERSION = 1;

struct MaterialLibrary {
    std::vector<Material> materials;
    std::vector<Assembly> assemblies;
    std::vector<Component> components;

    bool loadMaterials(const std::string& path, std::string& err);
    bool loadAssemblies(const std::string& path, std::string& err);
    bool loadComponents(const std::string& path, std::string& err);
    const Component* component(const std::string& id) const;
    int findMaterial(const std::string& id) const;
    int findAssembly(const std::string& id) const;
    float arealMass(const Layer& l) const;
    float layerR(const Layer& l) const;
};

} // namespace photon
