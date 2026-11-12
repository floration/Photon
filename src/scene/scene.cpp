#include "scene/scene.h"
#include "scene/textfile.h"
#include <algorithm>
#include <cmath>

namespace photon {

namespace {
// Actual dressed size of nominal 2x lumber, dry: American Softwood Lumber
// Standard, NIST Voluntary Product Standard PS 20 (2x4 = 1-1/2 x 3-1/2 in,
// 2x8 = 1-1/2 x 7-1/4 in, 2x10 = 1-1/2 x 9-1/4 in). See docs/DATA_REGISTER.md.
constexpr float IN = 0.0254f;
// Defaults; replaced from data/components.txt when the library has it.
float T2X = 1.5f * IN;        // 2x thickness
float D2X4 = 3.5f * IN;
float D2X8 = 7.25f * IN;
// Metal connector plate for wood trusses: 20 gauge galvanized steel is the
// common minimum (ANSI/TPI 1); 0.036 in nominal. To verify, see register.
float GUSSET_T = 0.036f * IN;
// Stair treads: 1 in nominal (3/4 in actual) is typical; IRC R311.7.5 limits
// risers to 7-3/4 in and requires treads of at least 10 in.
float TREAD_T = 1.0f * IN;
float TREAD_RUN = 10.0f * IN;

std::string where(const TextFile& f, const TextLine& l) { return f.path + ":" + std::to_string(l.number) + ": "; }
} // namespace

int Scene::storyAt(float z) const {
    int s = (int)std::floor(z / (ceilingHeight + floorDepth));
    return std::clamp(s, 0, stories - 1);
}

const Room* Scene::roomAt(float x, float y, float z) const {
    int s = storyAt(z);
    for (const Room& r : rooms)
        if (r.story == s && x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1) return &r;
    return nullptr;
}

bool Scene::load(const std::string& path, const MaterialLibrary& lib, std::string& err) {
    TextFile f;
    if (!f.load(path, err)) return false;
    float unit = 1.0f;  // meters per scene unit
    Item* item = nullptr;
    bool header = false;
    int curStory = 0;
    for (const TextLine& l : f.lines) {
        const auto& t = l.tok;
        const std::string& k = t[0];
        auto num = [&](size_t i, float& out, bool scale = true) -> bool {
            if (t.size() <= i) { err = where(f, l) + "missing value for '" + k + "'"; return false; }
            bool ok;
            out = (float)toNumber(t[i], ok);
            if (!ok) { err = where(f, l) + "bad number '" + t[i] + "'"; return false; }
            if (scale) out *= unit;
            return true;
        };
        auto mat = [&](size_t i, int& out) -> bool {
            if (t.size() <= i) { err = where(f, l) + "missing material"; return false; }
            out = lib.findMaterial(t[i]);
            if (out < 0) { err = where(f, l) + "unknown material '" + t[i] + "'"; return false; }
            return true;
        };
        auto asmb = [&](size_t i, int& out) -> bool {
            if (t.size() <= i) { err = where(f, l) + "missing assembly"; return false; }
            out = lib.findAssembly(t[i]);
            if (out < 0) { err = where(f, l) + "unknown assembly '" + t[i] + "'"; return false; }
            return true;
        };
        if (k == "photon_scene") {
            bool okv;
            int ver = t.size() > 1 ? (int)toNumber(t[1], okv) : 0;
            if (ver < 1 || ver > SCENE_VERSION) { err = where(f, l) + "scene format version " + (t.size() > 1 ? t[1] : std::string("?")) + " not supported (this build reads " + std::to_string(SCENE_VERSION) + ")"; return false; }
            header = true;
            continue;
        }
        if (!header) { err = where(f, l) + "file must start with 'photon_scene <version>'"; return false; }

        if (item) {
            if (k == "end") { item = nullptr; continue; }
            if (k != "part") { err = where(f, l) + "only 'part' lines inside an item"; return false; }
            // part <material> <mass_kg> x0 y0 z0 x1 y1 z1   (z above the story floor)
            Part p;
            if (!mat(1, p.material) || !num(2, p.mass, false)) return false;
            float b[6];
            for (int i = 0; i < 6; ++i) if (!num(3 + i, b[i])) return false;
            float zb = storyBase(item->story);
            p.box = {std::min(b[0], b[3]), std::min(b[1], b[4]), std::min(b[2], b[5]) + zb,
                     std::max(b[0], b[3]), std::max(b[1], b[4]), std::max(b[2], b[5]) + zb};
            item->parts.push_back(p);
            continue;
        }

        if (k == "units") {
            if (t.size() < 2) { err = where(f, l) + "units ft|m"; return false; }
            if (t[1] == "ft") unit = 0.3048f;
            else if (t[1] == "m") unit = 1.0f;
            else { err = where(f, l) + "unknown unit '" + t[1] + "'"; return false; }
        } else if (k == "name") {
            name = t.size() > 1 ? t[1] : "";
        } else if (k == "footprint") {
            if (!num(1, width) || !num(2, depth)) return false;
        } else if (k == "ceiling_height") {
            if (!num(1, ceilingHeight)) return false;
        } else if (k == "stories") {
            float s;
            if (!num(1, s, false)) return false;
            stories = std::max(1, (int)s);
        } else if (k == "floor_depth") {
            if (!num(1, floorDepth)) return false;
        } else if (k == "roof_type") {
            if (t.size() < 2) { err = where(f, l) + "roof_type full|half_story"; return false; }
            if (t[1] == "half_story") halfStory = true;
            else if (t[1] == "full") halfStory = false;
            else { err = where(f, l) + "unknown roof_type '" + t[1] + "'"; return false; }
        } else if (k == "roof_plate_height") {
            if (!num(1, plateHeight)) return false;
        } else if (k == "knee_wall_inset") {
            if (!num(1, kneeInset)) return false;
        } else if (k == "collar_height") {
            if (!num(1, collarHeight)) return false;
        } else if (k == "rafter_spacing") {
            if (!num(1, rafterSpacing)) return false;
        } else if (k == "assembly_knee_wall") { if (!asmb(1, kneeWall)) return false; }
        else if (k == "assembly_sloped_ceiling") { if (!asmb(1, slopedCeiling)) return false; }
        else if (k == "joist_spacing") {
            if (!num(1, joistSpacing)) return false;
        } else if (k == "joist_depth") {
            if (!num(1, joistDepth)) return false;
        } else if (k == "story") {
            float s;
            if (!num(1, s, false)) return false;
            curStory = (int)s - 1;
            if (curStory < 0 || curStory >= stories) { err = where(f, l) + "story out of range (set 'stories' first)"; return false; }
        } else if (k == "roof_pitch") {
            float rise, run;
            if (!num(1, rise, false) || !num(2, run, false)) return false;
            pitch = rise / run;
        } else if (k == "eave_overhang") {
            if (!num(1, eaveOverhang)) return false;
        } else if (k == "truss_spacing") {
            if (!num(1, trussSpacing)) return false;
        } else if (k == "stud_spacing") {
            if (!num(1, studSpacing)) return false;
        } else if (k == "attic_vent_ratio") {
            if (!num(1, atticVentRatio, false)) return false;
        } else if (k == "assembly_ext_wall") { if (!asmb(1, extWall)) return false; }
        else if (k == "assembly_ceiling") { if (!asmb(1, ceiling)) return false; }
        else if (k == "assembly_floor_ceiling") { if (!asmb(1, floorCeiling)) return false; }
        else if (k == "assembly_subfloor") { if (!asmb(1, subfloor)) return false; }
        else if (k == "assembly_roof") { if (!asmb(1, roof)) return false; }
        else if (k == "assembly_soffit") { if (!asmb(1, soffit)) return false; }
        else if (k == "assembly_gable") { if (!asmb(1, gable)) return false; }
        else if (k == "assembly_fascia") { if (!asmb(1, fascia)) return false; }
        else if (k == "stud_material") { if (!mat(1, studMaterial)) return false; }
        else if (k == "truss_material") { if (!mat(1, trussMaterial)) return false; }
        else if (k == "gusset_material") { if (!mat(1, gussetMaterial)) return false; }
        else if (k == "joist_material") { if (!mat(1, joistMaterial)) return false; }
        else if (k == "room") {
            // room "<name>" x0 y0 x1 y1 [floor <material> <areal_kg_m2>] [paint r g b]
            Room r;
            r.story = curStory;
            r.name = t.size() > 1 ? t[1] : "";
            if (!num(2, r.x0) || !num(3, r.y0) || !num(4, r.x1) || !num(5, r.y1)) return false;
            for (size_t i = 6; i < t.size();) {
                if (t[i] == "floor") {
                    if (!mat(i + 1, r.floorMaterial) || !num(i + 2, r.floorArealMass, false)) return false;
                    i += 3;
                } else if (t[i] == "paint") {
                    for (int c = 0; c < 3; ++c) if (!num(i + 1 + c, r.paint[c], false)) return false;
                    i += 4;
                } else { err = where(f, l) + "unknown room option '" + t[i] + "'"; return false; }
            }
            rooms.push_back(r);
        } else if (k == "wall") {
            Wall w;
            w.story = curStory;
            if (!asmb(1, w.assembly) || !num(2, w.x0) || !num(3, w.y0) || !num(4, w.x1) || !num(5, w.y1)) return false;
            if (w.x0 != w.x1 && w.y0 != w.y1) { err = where(f, l) + "walls must be axis-aligned"; return false; }
            walls.push_back(w);
        } else if (k == "door" || k == "window" || k == "cased") {
            // door <assembly> "<name>" x0 y0 x1 y1 open|closed
            // window <assembly> "<name>" x0 y0 x1 y1 sill <h> head <h>
            // cased "<name>" x0 y0 x1 y1 [head <h>]
            Opening o;
            o.story = curStory;
            size_t i = 1;
            if (k == "cased") o.kind = OpeningKind::Cased;
            else {
                o.kind = k == "door" ? OpeningKind::Door : OpeningKind::Window;
                if (!asmb(i, o.assembly)) return false;
                ++i;
            }
            o.name = t.size() > i ? t[i] : "";
            ++i;
            if (!num(i, o.x0) || !num(i + 1, o.y0) || !num(i + 2, o.x1) || !num(i + 3, o.y1)) return false;
            i += 4;
            o.head = (o.kind == OpeningKind::Cased) ? ceilingHeight : 80 * IN;  // 6'8" standard door height
            for (; i < t.size(); ++i) {
                if (t[i] == "open") o.open = true;
                else if (t[i] == "closed") o.open = false;
                else if (t[i] == "sill") { if (!num(++i, o.sill)) return false; }
                else if (t[i] == "head") { if (!num(++i, o.head)) return false; }
                else { err = where(f, l) + "unknown option '" + t[i] + "'"; return false; }
            }
            if (o.kind == OpeningKind::Cased) o.open = true;
            openings.push_back(o);
        } else if (k == "stair") {
            // stair x0 y0 x1 y1 +x|-x|+y|-y   (rises from this story to the next)
            Stair s;
            s.story = curStory;
            if (!num(1, s.x0) || !num(2, s.y0) || !num(3, s.x1) || !num(4, s.y1)) return false;
            if (t.size() < 6) { err = where(f, l) + "stair needs a direction (+x, -x, +y, -y)"; return false; }
            const std::string& d = t[5];
            s.dir = d == "+x" ? 0 : d == "-x" ? 1 : d == "+y" ? 2 : d == "-y" ? 3 : -1;
            if (s.dir < 0) { err = where(f, l) + "bad stair direction '" + d + "'"; return false; }
            if (s.story >= stories - 1) { err = where(f, l) + "stair on the top story"; return false; }
            float xa = std::min(s.x0, s.x1), xb = std::max(s.x0, s.x1), ya = std::min(s.y0, s.y1), yb = std::max(s.y0, s.y1);
            s.x0 = xa; s.x1 = xb; s.y0 = ya; s.y1 = yb;
            stairs.push_back(s);
        } else if (k == "fixture") {
            // fixture <type> "<name>" x y
            Fixture fx;
            fx.story = curStory;
            if (t.size() < 5) { err = where(f, l) + "fixture <type> \"<name>\" x y"; return false; }
            fx.type = t[1];
            fx.name = t[2];
            if (!num(3, fx.x) || !num(4, fx.y)) return false;
            static const char* known[] = {"panel", "water_heater", "sink", "kitchen_sink", "toilet", "tub", "washer", "range"};
            bool okType = false;
            for (const char* kn : known) okType = okType || fx.type == kn;
            if (!okType) { err = where(f, l) + "unknown fixture type '" + fx.type + "'"; return false; }
            fixtures.push_back(fx);
        } else if (k == "place") {
            // place <asset> "<name>" x y [rot 0|90|180|270] [z <height above this floor>]
            if (t.size() < 5) { err = where(f, l) + "place <asset> \"<name>\" x y [rot deg] [z h]"; return false; }
            const Asset* a = lib.asset(t[1]);
            if (!a) { err = where(f, l) + "unknown asset '" + t[1] + "' (data/assets.txt)"; return false; }
            float px, py, rot = 0, pz = 0;
            if (!num(3, px) || !num(4, py)) return false;
            for (size_t i = 5; i + 1 < t.size(); i += 2) {
                if (t[i] == "rot") { if (!num(i + 1, rot, false)) return false; }
                else if (t[i] == "z") { if (!num(i + 1, pz)) return false; }
                else { err = where(f, l) + "unknown option '" + t[i] + "'"; return false; }
            }
            int r = ((int)std::lround(rot / 90.0f) % 4 + 4) % 4;
            Item it;
            it.name = t[2];
            it.story = curStory;
            float zb = storyBase(curStory) + pz;
            for (const AssetPart& ap : a->parts) {
                float xs[2] = {ap.box[0], ap.box[3]}, ys[2] = {ap.box[1], ap.box[4]};
                float bx0 = 1e9f, by0 = 1e9f, bx1 = -1e9f, by1 = -1e9f;
                for (float xx : xs)
                    for (float yy : ys) {
                        float rx = r == 0 ? xx : r == 1 ? -yy : r == 2 ? -xx : yy;
                        float ry = r == 0 ? yy : r == 1 ? xx : r == 2 ? -yy : -xx;
                        bx0 = std::min(bx0, rx); bx1 = std::max(bx1, rx);
                        by0 = std::min(by0, ry); by1 = std::max(by1, ry);
                    }
                Part p;
                p.material = ap.material;
                p.mass = ap.mass;
                p.box = {px + bx0, py + by0, zb + ap.box[2], px + bx1, py + by1, zb + ap.box[5]};
                it.parts.push_back(p);
            }
            items.push_back(it);
        } else if (k == "item") {
            items.push_back(Item{});
            item = &items.back();
            item->name = t.size() > 1 ? t[1] : "";
            item->story = curStory;
        } else {
            err = where(f, l) + "unknown key '" + k + "'";
            return false;
        }
    }
    if (item) { err = path + ": item '" + item->name + "' missing 'end'"; return false; }
    if (extWall < 0 || ceiling < 0 || roof < 0 || soffit < 0 || gable < 0 || fascia < 0) {
        err = path + ": scene must name assemblies for ext_wall, ceiling, roof, soffit, gable, fascia";
        return false;
    }
    if (stories > 1 && (floorCeiling < 0 || subfloor < 0 || joistMaterial < 0)) {
        err = path + ": multi-story scenes need assembly_floor_ceiling, assembly_subfloor and joist_material";
        return false;
    }
    if (halfStory && (kneeWall < 0 || slopedCeiling < 0)) {
        err = path + ": half-story scenes need assembly_knee_wall and assembly_sloped_ceiling";
        return false;
    }
    if (halfStory && stories < 2) { err = path + ": a half story needs a story below it"; return false; }
    if (studMaterial < 0 || trussMaterial < 0 || gussetMaterial < 0) {
        err = path + ": scene must name stud_material, truss_material and gusset_material";
        return false;
    }
    // Exterior walls follow the footprint on every story.
    for (int s = 0; s < stories; ++s) {
        Wall w;
        w.assembly = extWall;
        w.exterior = true;
        w.story = s;
        float c[5][2] = {{0, 0}, {width, 0}, {width, depth}, {0, depth}, {0, 0}};
        for (int e = 0; e < 4; ++e) {
            w.x0 = c[e][0]; w.y0 = c[e][1]; w.x1 = c[e + 1][0]; w.y1 = c[e + 1][1];
            walls.push_back(w);
        }
    }
    generateFraming(lib);
    generateObjects(lib);
    return true;
}

void Scene::generateFraming(const MaterialLibrary& lib) {
    members.clear();
    if (const Component* c = lib.component("lumber_2x4")) { T2X = c->thickness; D2X4 = c->width; }
    if (const Component* c = lib.component("lumber_2x8")) D2X8 = c->width;
    if (const Component* c = lib.component("truss_plate")) GUSSET_T = c->thickness;
    if (const Component* c = lib.component("stair_tread")) { TREAD_T = c->thickness; TREAD_RUN = c->width; }
    auto add = [&](MemberKind kind, int m, float ax, float ay, float az, float bx, float by, float bz,
                   float sx, float sy, float sz, float w, float d) {
        Member mb;
        mb.kind = kind;
        mb.material = m;
        mb.p0[0] = ax; mb.p0[1] = ay; mb.p0[2] = az;
        mb.p1[0] = bx; mb.p1[1] = by; mb.p1[2] = bz;
        mb.side[0] = sx; mb.side[1] = sy; mb.side[2] = sz;
        mb.width = w;
        mb.depth = d;
        members.push_back(mb);
    };
    const float H = ceilingHeight;
    // ---- walls: single bottom plate, double top plate, studs at spacing,
    // king studs and doubled 2x8 headers at openings.
    for (const Wall& w : walls) {
        const float zb0 = storyBase(w.story);
        bool alongX = w.y0 == w.y1;
        float a = alongX ? std::min(w.x0, w.x1) : std::min(w.y0, w.y1);
        float b = alongX ? std::max(w.x0, w.x1) : std::max(w.y0, w.y1);
        float c = alongX ? w.y0 : w.x0;
        if (w.exterior) {  // pull exterior framing inside the footprint
            float half = D2X4 * 0.5f + 0.5f * IN;
            if (c <= 0.0f) c += half; else c -= half;
        }
        auto P = [&](float s, float z, float out[3]) {
            if (alongX) { out[0] = s; out[1] = c; } else { out[0] = c; out[1] = s; }
            out[2] = zb0 + z;
        };
        float sx = alongX ? 1.f : 0.f, sy = alongX ? 0.f : 1.f;
        float p0[3], p1[3];
        // wall height: a half story's eave walls stop at the rafter plate; its
        // other walls rise to the collar-tie ceiling and are cut off by the roof
        bool half = halfStory && w.story == stories - 1;
        float Hw = !half ? H : (w.exterior && alongX ? plateHeight : collarHeight);
        auto roomTop = [&](float s) {   // usable height at position s along the wall
            if (!half) return H;
            float y = alongX ? c : s;
            return std::min(Hw, roofUnderside(y) - zb0);
        };
        P(a, T2X * 0.5f, p0); P(b, T2X * 0.5f, p1);
        add(MemberKind::Plate, studMaterial, p0[0], p0[1], p0[2], p1[0], p1[1], p1[2], 0, 0, 1, T2X, D2X4);
        {   // double top plate wherever the wall reaches its full height
            float ta = a, tb = b;
            if (half && !alongX && !w.exterior) { ta = std::max(a, slopedRun()); tb = std::min(b, depth - slopedRun()); }
            if (half && !alongX && w.exterior) { ta = tb; }   // gable end walls: the rake follows the rafters
            if (tb > ta)
                for (float z : {Hw - T2X * 1.5f, Hw - T2X * 0.5f}) {
                    P(ta, z, p0); P(tb, z, p1);
                    add(MemberKind::Plate, studMaterial, p0[0], p0[1], p0[2], p1[0], p1[1], p1[2], 0, 0, 1, T2X, D2X4);
                }
        }
        std::vector<const Opening*> ops;
        for (const Opening& o : openings) {
            if (o.story != w.story) continue;
            bool oX = o.y0 == o.y1;
            if (oX != alongX) continue;
            float oc = oX ? o.y0 : o.x0;
            if (std::fabs(oc - (alongX ? w.y0 : w.x0)) > 0.01f) continue;
            float oa = oX ? std::min(o.x0, o.x1) : std::min(o.y0, o.y1);
            float ob = oX ? std::max(o.x0, o.x1) : std::max(o.y0, o.y1);
            if (ob <= a || oa >= b) continue;
            ops.push_back(&o);
        }
        auto inOpening = [&](float s) {
            for (const Opening* o : ops) {
                float oa = alongX ? std::min(o->x0, o->x1) : std::min(o->y0, o->y1);
                float ob = alongX ? std::max(o->x0, o->x1) : std::max(o->y0, o->y1);
                if (s > oa - T2X && s < ob + T2X) return true;
            }
            return false;
        };
        float zb = T2X;
        std::vector<float> studs;
        for (float s = a + T2X * 0.5f; s < b - T2X * 0.5f; s += studSpacing) studs.push_back(s);
        studs.push_back(b - T2X * 0.5f);
        for (float s : studs) {
            if (inOpening(s)) continue;
            float zt = roomTop(s) - 2 * T2X;
            if (zt < zb + 0.1f) continue;
            P(s, zb, p0); P(s, zt, p1);
            add(MemberKind::Stud, studMaterial, p0[0], p0[1], p0[2], p1[0], p1[1], p1[2], sx, sy, 0, T2X, D2X4);
        }
        for (const Opening* o : ops) {
            float oa = alongX ? std::min(o->x0, o->x1) : std::min(o->y0, o->y1);
            float ob = alongX ? std::max(o->x0, o->x1) : std::max(o->y0, o->y1);
            for (float s : {oa - T2X * 0.5f, ob + T2X * 0.5f}) {   // king studs
                float zt = roomTop(s) - 2 * T2X;
                P(s, zb, p0); P(s, zt, p1);
                add(MemberKind::Stud, studMaterial, p0[0], p0[1], p0[2], p1[0], p1[1], p1[2], sx, sy, 0, T2X, D2X4);
            }
            if (o->kind != OpeningKind::Cased || o->head < H - 0.1f) {
                P(oa - T2X, o->head + D2X8 * 0.5f, p0); P(ob + T2X, o->head + D2X8 * 0.5f, p1);
                add(MemberKind::Header, studMaterial, p0[0], p0[1], p0[2], p1[0], p1[1], p1[2], 0, 0, 1, D2X8, D2X4);
            }
            if (o->kind == OpeningKind::Window) {
                P(oa, o->sill - T2X * 0.5f, p0); P(ob, o->sill - T2X * 0.5f, p1);
                add(MemberKind::Plate, studMaterial, p0[0], p0[1], p0[2], p1[0], p1[1], p1[2], 0, 0, 1, T2X, D2X4);
            }
        }
    }
    // ---- floor joists between stories: span y, bearing at mid-depth (hall
    // bearing line), skipped through stairwells.
    for (int s = 0; s + 1 < stories; ++s) {
        float zc = storyBase(s) + H + joistDepth * 0.5f;
        for (float x = T2X * 0.5f; x < width; x += joistSpacing) {
            for (int half = 0; half < 2; ++half) {
                float y0 = half == 0 ? 0.0f : depth * 0.5f, y1 = half == 0 ? depth * 0.5f : depth;
                // cut around stairwells on this floor
                float ya = y0, yb = y1;
                bool skip = false;
                for (const Stair& st : stairs) {
                    if (st.story != s || x < st.x0 || x > st.x1) continue;
                    if (st.y0 <= ya && st.y1 >= yb) skip = true;
                    else if (st.y0 <= ya && st.y1 > ya) ya = st.y1;
                    else if (st.y1 >= yb && st.y0 < yb) yb = st.y0;
                }
                if (skip || yb - ya < 0.1f) continue;
                add(MemberKind::Joist, joistMaterial, x, ya, zc, x, yb, zc, 1, 0, 0, T2X, joistDepth);
            }
        }
    }
    // ---- stair treads
    for (const Stair& st : stairs) {
        float rise = H + floorDepth;
        bool alongX = st.dir < 2;
        float run = alongX ? st.x1 - st.x0 : st.y1 - st.y0;
        int n = std::max(2, (int)std::lround(rise / (7.5f * IN)));   // risers
        float step = std::min(TREAD_RUN, run / (n - 1));
        float zb0 = storyBase(st.story);
        for (int i = 1; i < n; ++i) {
            float z = zb0 + rise * i / n - TREAD_T * 0.5f;
            float s0 = step * (i - 1), s1 = step * i;
            float a = alongX ? (st.dir == 0 ? st.x0 + s0 : st.x1 - s0) : (st.dir == 2 ? st.y0 + s0 : st.y1 - s0);
            float b = alongX ? (st.dir == 0 ? st.x0 + s1 : st.x1 - s1) : (st.dir == 2 ? st.y0 + s1 : st.y1 - s1);
            if (alongX) {
                float yc = 0.5f * (st.y0 + st.y1);
                add(MemberKind::Tread, studMaterial, a, yc, z, b, yc, z, 0, 1, 0, st.y1 - st.y0, TREAD_T);
            } else {
                float xc = 0.5f * (st.x0 + st.x1);
                add(MemberKind::Tread, studMaterial, xc, a, z, xc, b, z, 1, 0, 0, st.x1 - st.x0, TREAD_T);
            }
        }
    }
    // ---- half story: stick-framed roof (rafters, ridge board, collar ties)
    // and knee walls instead of prefab trusses.
    if (halfStory) {
        const float D = depth, oh = eaveOverhang, RB = roofBase();
        const float cosT = 1.0f / std::sqrt(1 + pitch * pitch);
        float d8 = D2X8, d10 = 9.25f * IN, d6 = 5.5f * IN;
        if (const Component* c = lib.component("lumber_2x10")) d10 = c->width;
        if (const Component* c = lib.component("lumber_2x6")) d6 = c->width;
        const float zTop = storyBase(stories - 1);
        auto rafterZ = [&](float y) { float run = y <= 0.5f * D ? y : D - y; return RB + pitch * run + 0.5f * d8 / cosT; };
        std::vector<float> xs;
        for (float x = T2X * 0.5f; x < width - T2X * 0.5f; x += rafterSpacing) xs.push_back(x);
        xs.push_back(width - T2X * 0.5f);
        float yr = 0.5f * D - 0.5f * T2X;   // rafters butt the ridge board
        for (float x : xs) {
            add(MemberKind::Rafter, trussMaterial, x, -oh, rafterZ(0) - pitch * oh, x, yr, rafterZ(yr), 1, 0, 0, T2X, d8);
            add(MemberKind::Rafter, trussMaterial, x, D + oh, rafterZ(0) - pitch * oh, x, D - yr, rafterZ(yr), 1, 0, 0, T2X, d8);
            float zc = zTop + collarHeight + 0.5f * d6;
            add(MemberKind::CollarTie, trussMaterial, x + T2X, slopedRun(), zc, x + T2X, D - slopedRun(), zc, 1, 0, 0, T2X, d6);
        }
        float zr = rafterZ(0.5f * D) + 0.25f * d10;
        add(MemberKind::RidgeBoard, trussMaterial, 0, 0.5f * D, zr, width, 0.5f * D, zr, 0, 0, 1, d10, T2X);
        // knee walls: single plates, studs from the floor plate to the rafters
        for (float yk : {kneeInset, D - kneeInset}) {
            float kh = kneeWallHeight();
            add(MemberKind::Plate, studMaterial, 0, yk, zTop + 0.5f * T2X, width, yk, zTop + 0.5f * T2X, 0, 0, 1, T2X, D2X4);
            add(MemberKind::Plate, studMaterial, 0, yk, zTop + kh - 0.5f * T2X, width, yk, zTop + kh - 0.5f * T2X, 0, 0, 1, T2X, D2X4);
            for (float x = T2X * 0.5f; x < width; x += studSpacing)
                add(MemberKind::Stud, studMaterial, x, yk, zTop + T2X, x, yk, zTop + kh - T2X, 1, 0, 0, T2X, D2X4);
        }
        return;
    }
    // ---- prefab Fink trusses spanning y, spaced along x, on the top story.
    const float D = depth, oh = eaveOverhang, hc = D2X4 * 0.5f;
    const float RB = roofBase();
    const float cosT = 1.0f / std::sqrt(1 + pitch * pitch);
    auto topZ = [&](float y) {
        float run = y <= D * 0.5f ? y : D - y;
        return RB + pitch * run + hc / cosT;
    };
    std::vector<float> xs;
    for (float x = T2X * 0.5f; x < width - T2X * 0.5f; x += trussSpacing) xs.push_back(x);
    xs.push_back(width - T2X * 0.5f);
    for (float x : xs) {
        float zb = RB + hc;
        add(MemberKind::TrussChord, trussMaterial, x, 0, zb, x, D, zb, 1, 0, 0, T2X, D2X4);
        add(MemberKind::TrussChord, trussMaterial, x, -oh, topZ(0) - pitch * oh, x, D * 0.5f, topZ(D * 0.5f), 1, 0, 0, T2X, D2X4);
        add(MemberKind::TrussChord, trussMaterial, x, D + oh, topZ(D) - pitch * oh, x, D * 0.5f, topZ(D * 0.5f), 1, 0, 0, T2X, D2X4);
        float yb[2] = {D / 3, 2 * D / 3}, yt[2] = {D / 4, 3 * D / 4};
        add(MemberKind::TrussWeb, trussMaterial, x, yb[0], zb, x, yt[0], topZ(yt[0]) - hc, 1, 0, 0, T2X, D2X4);
        add(MemberKind::TrussWeb, trussMaterial, x, yb[0], zb, x, D * 0.5f, topZ(D * 0.5f) - hc, 1, 0, 0, T2X, D2X4);
        add(MemberKind::TrussWeb, trussMaterial, x, yb[1], zb, x, D * 0.5f, topZ(D * 0.5f) - hc, 1, 0, 0, T2X, D2X4);
        add(MemberKind::TrussWeb, trussMaterial, x, yb[1], zb, x, yt[1], topZ(yt[1]) - hc, 1, 0, 0, T2X, D2X4);
        float joints[7][2] = {{0, RB + hc}, {D, RB + hc}, {D * 0.5f, topZ(D * 0.5f)}, {yb[0], zb}, {yb[1], zb},
                              {yt[0], topZ(yt[0])}, {yt[1], topZ(yt[1])}};
        for (auto& j : joints)
            for (float sgn : {-1.f, 1.f}) {
                float gx = x + sgn * (T2X * 0.5f + GUSSET_T * 0.5f);
                add(MemberKind::Gusset, gussetMaterial, gx, j[0], j[1] - 0.075f, gx, j[0], j[1] + 0.075f, 1, 0, 0,
                    GUSSET_T, 0.10f);
            }
    }
}

} // namespace photon
