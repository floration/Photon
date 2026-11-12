#include "scene/materials.h"
#include "scene/textfile.h"

namespace photon {

static std::string where(const TextFile& f, const TextLine& l) {
    return f.path + ":" + std::to_string(l.number) + ": ";
}

bool MaterialLibrary::loadMaterials(const std::string& path, std::string& err) {
    TextFile f;
    if (!f.load(path, err)) return false;
    Material* cur = nullptr;
    for (const TextLine& l : f.lines) {
        const std::string& k = l.tok[0];
        auto num = [&](int i, float& out) -> bool {
            if ((int)l.tok.size() <= i) { err = where(f, l) + "missing value for " + k; return false; }
            bool ok;
            out = (float)toNumber(l.tok[i], ok);
            if (!ok) { err = where(f, l) + "bad number '" + l.tok[i] + "'"; return false; }
            return true;
        };
        if (k == "material") {
            if (l.tok.size() < 2) { err = where(f, l) + "material needs an id"; return false; }
            materials.push_back(Material{});
            cur = &materials.back();
            cur->id = l.tok[1];
            continue;
        }
        if (!cur) { err = where(f, l) + "value outside a material block"; return false; }
        if (k == "end") { cur = nullptr; continue; }
        if (k == "name") { cur->name = l.tok.size() > 1 ? l.tok[1] : ""; continue; }
        if (k == "source") { cur->source += (cur->source.empty() ? "" : " | ") + (l.tok.size() > 1 ? l.tok[1] : ""); continue; }
        if (k == "combustible") { cur->combustible = l.tok.size() > 1 && l.tok[1] == "yes"; continue; }
        if (k == "color") {
            for (int i = 0; i < 3; ++i) if (!num(i + 1, cur->color[i])) return false;
            continue;
        }
        float* dst = nullptr;
        if (k == "density") dst = &cur->density;
        else if (k == "conductivity") dst = &cur->conductivity;
        else if (k == "specific_heat") dst = &cur->specificHeat;
        else if (k == "emissivity") dst = &cur->emissivity;
        else if (k == "pyrolysis_temp") dst = &cur->pyrolysisTemp;
        else if (k == "heat_of_gasification") dst = &cur->heatOfGasification;
        else if (k == "heat_of_combustion") dst = &cur->heatOfCombustion;
        else if (k == "soot_yield") dst = &cur->sootYield;
        else if (k == "aerosol_yield") dst = &cur->aerosolYield;
        else if (k == "char_yield") dst = &cur->charYield;
        else if (k == "surface_depth") dst = &cur->surfaceDepth;
        else if (k == "fail_temp") dst = &cur->failTemp;
        else if (k == "fail_time") dst = &cur->failTime;
        if (!dst) { err = where(f, l) + "unknown key '" + k + "'"; return false; }
        if (!num(1, *dst)) return false;
    }
    return true;
}

bool MaterialLibrary::loadAssemblies(const std::string& path, std::string& err) {
    TextFile f;
    if (!f.load(path, err)) return false;
    Assembly* cur = nullptr;
    for (const TextLine& l : f.lines) {
        const std::string& k = l.tok[0];
        bool ok = true;
        if (k == "assembly") {
            if (l.tok.size() < 2) { err = where(f, l) + "assembly needs an id"; return false; }
            assemblies.push_back(Assembly{});
            cur = &assemblies.back();
            cur->id = l.tok[1];
            continue;
        }
        if (!cur) { err = where(f, l) + "value outside an assembly block"; return false; }
        if (k == "end") { cur = nullptr; continue; }
        if (k == "name") { cur->name = l.tok.size() > 1 ? l.tok[1] : ""; continue; }
        if (k == "source") { cur->source += (cur->source.empty() ? "" : " | ") + (l.tok.size() > 1 ? l.tok[1] : ""); continue; }
        if (k == "leakage") {
            if (l.tok.size() < 2) { err = where(f, l) + "leakage needs a value"; return false; }
            cur->leakage = (float)toNumber(l.tok[1], ok);
            if (!ok) { err = where(f, l) + "bad number"; return false; }
            continue;
        }
        if (k == "layer") {
            // layer <material> <thickness_m> [fill <f>] [r <m2K/W>]
            if (l.tok.size() < 3) { err = where(f, l) + "layer <material> <thickness_m>"; return false; }
            Layer ly;
            ly.material = findMaterial(l.tok[1]);
            if (ly.material < 0) { err = where(f, l) + "unknown material '" + l.tok[1] + "'"; return false; }
            ly.thickness = (float)toNumber(l.tok[2], ok);
            if (!ok) { err = where(f, l) + "bad thickness"; return false; }
            for (size_t i = 3; i + 1 < l.tok.size(); i += 2) {
                float v = (float)toNumber(l.tok[i + 1], ok);
                if (!ok) { err = where(f, l) + "bad number"; return false; }
                if (l.tok[i] == "fill") ly.fill = v;
                else if (l.tok[i] == "r") ly.rValue = v;
                else { err = where(f, l) + "unknown layer option '" + l.tok[i] + "'"; return false; }
            }
            cur->layers.push_back(ly);
            continue;
        }
        err = where(f, l) + "unknown key '" + k + "'";
        return false;
    }
    return true;
}

int MaterialLibrary::findMaterial(const std::string& id) const {
    for (size_t i = 0; i < materials.size(); ++i) if (materials[i].id == id) return (int)i;
    return -1;
}
int MaterialLibrary::findAssembly(const std::string& id) const {
    for (size_t i = 0; i < assemblies.size(); ++i) if (assemblies[i].id == id) return (int)i;
    return -1;
}
float MaterialLibrary::arealMass(const Layer& l) const {
    return materials[l.material].density * l.thickness * l.fill;
}
float MaterialLibrary::layerR(const Layer& l) const {
    if (l.rValue >= 0) return l.rValue;
    return l.thickness / materials[l.material].conductivity;
}

} // namespace photon
