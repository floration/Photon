#include "scene/materials.h"
#include "scene/textfile.h"
#include <algorithm>

namespace photon {

static std::string where(const TextFile& f, const TextLine& l) {
    return f.path + ":" + std::to_string(l.number) + ": ";
}

// Every data file starts with "<magic> <version>"; newer versions are refused
// with a clear message instead of being misread.
static bool checkHeader(const TextFile& f, const char* magic, int supported, std::string& err) {
    if (f.lines.empty() || f.lines[0].tok[0] != magic || f.lines[0].tok.size() < 2) {
        err = f.path + ": missing '" + std::string(magic) + " <version>' header";
        return false;
    }
    bool ok;
    int v = (int)toNumber(f.lines[0].tok[1], ok);
    if (!ok || v < 1) { err = f.path + ": bad version"; return false; }
    if (v > supported) {
        err = f.path + ": format version " + std::to_string(v) + " is newer than this build supports (" +
              std::to_string(supported) + "); update Photon";
        return false;
    }
    return true;
}

bool MaterialLibrary::loadGases(const std::string& path, std::string& err) {
    TextFile f;
    if (!f.load(path, err)) return false;
    if (!checkHeader(f, "photon_gases", GASES_VERSION, err)) return false;
    Gas* cur = nullptr;
    for (size_t li = 1; li < f.lines.size(); ++li) {
        const TextLine& l = f.lines[li];
        const std::string& k = l.tok[0];
        if (k == "gas") {
            if (l.tok.size() < 2) { err = where(f, l) + "gas needs an id"; return false; }
            gases.push_back(Gas{});
            cur = &gases.back();
            cur->id = l.tok[1];
            continue;
        }
        if (!cur) { err = where(f, l) + "value outside a gas block"; return false; }
        if (k == "end") { cur = nullptr; continue; }
        std::string val = l.tok.size() > 1 ? l.tok[1] : "";
        if (k == "name") { cur->name = val; continue; }
        if (k == "formula") { cur->formula = val; continue; }
        if (k == "source") { cur->source += (cur->source.empty() ? "" : " | ") + val; continue; }
        float* dst = k == "molar_mass" ? &cur->molarMass : k == "heat_of_combustion" ? &cur->heatOfCombustion
                   : k == "autoignition" ? &cur->autoignition : k == "lfl" ? &cur->lfl : k == "ufl" ? &cur->ufl
                   : k == "idlh" ? &cur->idlh : nullptr;
        if (!dst) { err = where(f, l) + "unknown key '" + k + "'"; return false; }
        bool ok;
        *dst = (float)toNumber(val, ok);
        if (!ok) { err = where(f, l) + "bad number '" + val + "'"; return false; }
    }
    return true;
}

int MaterialLibrary::findGas(const std::string& id) const {
    for (size_t i = 0; i < gases.size(); ++i) if (gases[i].id == id) return (int)i;
    return -1;
}

bool MaterialLibrary::loadMaterials(const std::string& path, std::string& err) {
    TextFile f;
    if (!f.load(path, err)) return false;
    if (!checkHeader(f, "photon_materials", MATERIALS_VERSION, err)) return false;
    Material* cur = nullptr;
    for (size_t li = 1; li < f.lines.size(); ++li) {
        const TextLine& l = f.lines[li];
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
        if (k == "end") {
            // off-gas fractions must describe all of the off-gas
            if (!cur->offgas.empty()) {
                float sum = 0;
                for (const Offgas& o : cur->offgas) sum += o.massFraction;
                if (sum < 0.98f || sum > 1.02f) {
                    err = where(f, l) + "offgas fractions of '" + cur->id + "' sum to " + std::to_string(sum) + ", expected 1";
                    return false;
                }
            } else if (cur->combustible) {
                err = where(f, l) + "combustible material '" + cur->id + "' lists no offgas";
                return false;
            }
            cur = nullptr;
            continue;
        }
        if (k == "offgas") {
            // offgas <gas id> <mass fraction>
            if (l.tok.size() < 3) { err = where(f, l) + "offgas <gas> <mass fraction>"; return false; }
            Offgas o;
            o.gas = findGas(l.tok[1]);
            if (o.gas < 0) { err = where(f, l) + "unknown gas '" + l.tok[1] + "' (load data/gases.txt first)"; return false; }
            bool ok;
            o.massFraction = (float)toNumber(l.tok[2], ok);
            if (!ok) { err = where(f, l) + "bad number"; return false; }
            cur->offgas.push_back(o);
            continue;
        }
        if (k == "name") { cur->name = l.tok.size() > 1 ? l.tok[1] : ""; continue; }
        if (k == "source") { cur->source += (cur->source.empty() ? "" : " | ") + (l.tok.size() > 1 ? l.tok[1] : ""); continue; }
        if (k == "combustible") { cur->combustible = l.tok.size() > 1 && l.tok[1] == "yes"; continue; }
        if (k == "color") {
            for (int i = 0; i < 3; ++i) if (!num(i + 1, cur->color[i])) return false;
            continue;
        }
        float* dst = nullptr;
        if (k == "spread_rate") {
            if (!num(1, cur->spreadLateral) || !num(2, cur->spreadUp)) return false;
            continue;
        }
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
        else if (k == "porous_area") dst = &cur->porousArea;
        else if (k == "fail_temp") dst = &cur->failTemp;
        else if (k == "fail_time") dst = &cur->failTime;
        else if (k == "design_fb") dst = &cur->designFb;
        else if (k == "design_fc") dst = &cur->designFc;
        else if (k == "design_fc_perp") dst = &cur->designFcPerp;
        else if (k == "modulus_e") dst = &cur->modulusE;
        else if (k == "modulus_emin") dst = &cur->modulusEmin;
        else if (k == "panel_load") dst = &cur->panelLoad;
        else if (k == "panel_ref_thickness") dst = &cur->panelRefThickness;
        else if (k == "fastener_pull") dst = &cur->fastenerPull;
        else if (k == "fastener_density") dst = &cur->fastenerDensity;
        if (!dst) { err = where(f, l) + "unknown key '" + k + "'"; return false; }
        if (!num(1, *dst)) return false;
    }
    return true;
}

bool MaterialLibrary::loadAssemblies(const std::string& path, std::string& err) {
    TextFile f;
    if (!f.load(path, err)) return false;
    if (!checkHeader(f, "photon_assemblies", ASSEMBLIES_VERSION, err)) return false;
    Assembly* cur = nullptr;
    for (size_t li = 1; li < f.lines.size(); ++li) {
        const TextLine& l = f.lines[li];
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

bool MaterialLibrary::loadComponents(const std::string& path, std::string& err) {
    TextFile f;
    if (!f.load(path, err)) return false;
    if (!checkHeader(f, "photon_components", COMPONENTS_VERSION, err)) return false;
    for (size_t li = 1; li < f.lines.size(); ++li) {
        const TextLine& l = f.lines[li];
        if (l.tok[0] != "component" || l.tok.size() < 5) { err = where(f, l) + "component <id> <thickness_in> <width_in> \"name\" [source \"...\"]"; return false; }
        Component c;
        bool ok1, ok2;
        c.id = l.tok[1];
        c.thickness = (float)toNumber(l.tok[2], ok1) * 0.0254f;
        c.width = (float)toNumber(l.tok[3], ok2) * 0.0254f;
        if (!ok1 || !ok2) { err = where(f, l) + "bad number"; return false; }
        c.name = l.tok[4];
        if (l.tok.size() >= 7 && l.tok[5] == "source") c.source = l.tok[6];
        if (c.source.empty()) { err = where(f, l) + "component needs a source"; return false; }
        components.push_back(c);
    }
    return true;
}

bool MaterialLibrary::loadAssets(const std::string& path, std::string& err) {
    TextFile f;
    if (!f.load(path, err)) return false;
    if (!checkHeader(f, "photon_assets", ASSETS_VERSION, err)) return false;
    Asset* cur = nullptr;
    for (size_t li = 1; li < f.lines.size(); ++li) {
        const TextLine& l = f.lines[li];
        const std::string& k = l.tok[0];
        if (k == "asset") {
            if (l.tok.size() < 2) { err = where(f, l) + "asset needs an id"; return false; }
            assets.push_back(Asset{});
            cur = &assets.back();
            cur->id = l.tok[1];
            continue;
        }
        if (!cur) { err = where(f, l) + "value outside an asset block"; return false; }
        if (k == "end") {
            if (cur->parts.empty()) { err = where(f, l) + "asset '" + cur->id + "' has no parts"; return false; }
            if (cur->source.empty()) { err = where(f, l) + "asset '" + cur->id + "' needs a source"; return false; }
            cur = nullptr;
            continue;
        }
        if (k == "name") { cur->name = l.tok.size() > 1 ? l.tok[1] : ""; continue; }
        if (k == "source") { cur->source += (cur->source.empty() ? "" : " | ") + (l.tok.size() > 1 ? l.tok[1] : ""); continue; }
        if (k == "size") continue;   // informational
        if (k == "part") {
            // part <material> <mass kg> x0 y0 z0 x1 y1 z1   (inches, local frame)
            if (l.tok.size() < 9) { err = where(f, l) + "part <material> <mass> x0 y0 z0 x1 y1 z1"; return false; }
            AssetPart p;
            p.material = findMaterial(l.tok[1]);
            if (p.material < 0) { err = where(f, l) + "unknown material '" + l.tok[1] + "'"; return false; }
            bool ok;
            p.mass = (float)toNumber(l.tok[2], ok);
            if (!ok) { err = where(f, l) + "bad mass"; return false; }
            float b[6];
            for (int i = 0; i < 6; ++i) {
                b[i] = (float)toNumber(l.tok[3 + i], ok) * 0.0254f;
                if (!ok) { err = where(f, l) + "bad number"; return false; }
            }
            p.box[0] = std::min(b[0], b[3]); p.box[3] = std::max(b[0], b[3]);
            p.box[1] = std::min(b[1], b[4]); p.box[4] = std::max(b[1], b[4]);
            p.box[2] = std::min(b[2], b[5]); p.box[5] = std::max(b[2], b[5]);
            cur->parts.push_back(p);
            continue;
        }
        err = where(f, l) + "unknown key '" + k + "'";
        return false;
    }
    return true;
}

const Asset* MaterialLibrary::asset(const std::string& id) const {
    for (const Asset& a : assets) if (a.id == id) return &a;
    return nullptr;
}

const Component* MaterialLibrary::component(const std::string& id) const {
    for (const Component& c : components) if (c.id == id) return &c;
    return nullptr;
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
