// Photon - staged house builder. Expands the scene's construction description
// into physical objects in the order a real house is built:
//   1 floorplan   2 MEP layout   3 framing   4 rough-in   5 insulation
//   6 walls       7 roofing      8 exterior  9 interior
// Every object records the stage that installs it. Sizes come from the
// component library (data/components.txt) where it has them.
#include "scene/scene.h"
#include <algorithm>
#include <cmath>

namespace photon {

const char* stageName(int stage) {
    switch (stage) {
    case STAGE_FLOORPLAN: return "1 Floorplan";
    case STAGE_MEP_LAYOUT: return "2 Wire and pipe layout";
    case STAGE_FRAMING: return "3 Framing";
    case STAGE_ROUGH_IN: return "4 Rough-in (wiring, plumbing)";
    case STAGE_INSULATION: return "5 Insulation";
    case STAGE_WALLS: return "6 Sheathing, drywall, paint";
    case STAGE_ROOFING: return "7 Roofing";
    case STAGE_EXTERIOR: return "8 Exterior";
    case STAGE_INTERIOR: return "9 Interior";
    }
    return "?";
}

namespace {
constexpr float IN = 0.0254f, FT = 0.3048f;

void cross3(const float a[3], const float b[3], float o[3]) {
    o[0] = a[1] * b[2] - a[2] * b[1];
    o[1] = a[2] * b[0] - a[0] * b[2];
    o[2] = a[0] * b[1] - a[1] * b[0];
}
void norm3(float a[3]) {
    float l = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (l > 0) { a[0] /= l; a[1] /= l; a[2] /= l; }
}
const char* memberName(MemberKind k) {
    switch (k) {
    case MemberKind::Stud: return "stud";
    case MemberKind::Plate: return "plate";
    case MemberKind::Header: return "header";
    case MemberKind::TrussChord: return "truss chord";
    case MemberKind::TrussWeb: return "truss web";
    case MemberKind::Gusset: return "gusset plate";
    case MemberKind::Joist: return "floor joist";
    case MemberKind::Tread: return "stair tread";
    case MemberKind::Rafter: return "rafter";
    case MemberKind::RidgeBoard: return "ridge board";
    case MemberKind::CollarTie: return "collar tie";
    }
    return "member";
}

struct P3 { float x, y, z; };
// A planned run of cable or pipe (MEP layout stage), installed at rough-in.
struct Run {
    std::string name, kind;   // kind: "cable", "water line", "drain"
    int material = -1;
    float thick = 0, width = 0;   // cross-section (m)
    std::vector<P3> pts;
};
struct Device { std::string name, kind; P3 p; float nx, ny; };   // kind: receptacle, switch, light; (nx,ny) faces into the room
} // namespace

bool SceneObject::contains(const float p[3], float tol) const {
    float d[3] = {p[0] - c[0], p[1] - c[1], p[2] - c[2]};
    auto dot = [&](const float a[3]) { return d[0] * a[0] + d[1] * a[1] + d[2] * a[2]; };
    return std::fabs(dot(u)) <= hu + tol && std::fabs(dot(v)) <= hv + tol && std::fabs(dot(n)) <= hn + tol;
}

float Scene::layerOffset(const MaterialLibrary& lib, const Assembly& a, int layer, bool centered) {
    int frame = -1;
    float total = 0;
    for (size_t l = 0; l < a.layers.size(); ++l) {
        total += a.layers[l].thickness;
        if (frame < 0 && a.layers[l].fill < 0.999f) frame = (int)l;
    }
    float start = 0;
    if (frame >= 0) {
        for (int l = 0; l < frame; ++l) start -= a.layers[l].thickness;
        start -= 0.5f * a.layers[frame].thickness;
    } else if (centered) {
        start = -0.5f * total;
    }
    for (int l = 0; l < layer; ++l) start += a.layers[l].thickness;
    (void)lib;
    return start;
}

void Scene::generateObjects(const MaterialLibrary& lib) {
    objects.clear();
    int stage = STAGE_FLOORPLAN;
    auto comp = [&](const char* id, float t, float w) {
        if (const Component* c = lib.component(id)) return std::make_pair(c->thickness, c->width);
        return std::make_pair(t * IN, w * IN);
    };
    auto mat = [&](const char* id) { return lib.findMaterial(id); };
    const auto sheet = comp("sheet_4x8", 48, 96);
    const float SHEET_W = sheet.first, SHEET_L = sheet.second;
    const float H = ceilingHeight;

    auto add = [&](const std::string& name, const std::string& kind, int m, int story, const float c[3],
                   const float u[3], const float v[3], float hu, float hv, float hn) -> SceneObject& {
        SceneObject o;
        o.id = (int)objects.size() + 1;
        o.stage = stage;
        o.name = name;
        o.kind = kind;
        o.material = m;
        o.story = story;
        for (int i = 0; i < 3; ++i) { o.c[i] = c[i]; o.u[i] = u[i]; o.v[i] = v[i]; }
        norm3(o.u);
        norm3(o.v);
        cross3(o.u, o.v, o.n);
        norm3(o.n);
        o.hu = hu; o.hv = hv; o.hn = hn;
        objects.push_back(o);
        return objects.back();
    };
    auto addBoxXYZ = [&](const std::string& name, const std::string& kind, int m, int story, float x0, float y0, float z0,
                         float x1, float y1, float z1) -> SceneObject& {
        float c[3] = {0.5f * (x0 + x1), 0.5f * (y0 + y1), 0.5f * (z0 + z1)}, u[3] = {1, 0, 0}, v[3] = {0, 1, 0};
        return add(name, kind, m, story, c, u, v, 0.5f * std::fabs(x1 - x0), 0.5f * std::fabs(y1 - y0), 0.5f * std::fabs(z1 - z0));
    };
    // a straight piece of cable or pipe between two points
    auto addSegment = [&](const std::string& name, const std::string& kind, int m, P3 a, P3 b, float thick, float wid) {
        float d[3] = {b.x - a.x, b.y - a.y, b.z - a.z};
        float L = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (L < 0.005f) return;
        float u[3] = {d[0] / L, d[1] / L, d[2] / L};
        float v[3] = {0, 0, 1};
        if (std::fabs(u[2]) > 0.9f) { v[0] = 1; v[2] = 0; }
        float c[3] = {0.5f * (a.x + b.x), 0.5f * (a.y + b.y), 0.5f * (a.z + b.z)};
        add(name, kind, m, storyAt(c[2]), c, u, v, 0.5f * L, 0.5f * wid, 0.5f * thick);
    };

    // ============================================================ 1 floorplan
    // Slab on grade (4 in), the plan itself (rooms, walls, openings, stairs)
    // and the fixture locations everything later is routed to.
    addBoxXYZ("Slab on grade, 4 in concrete", "slab", mat("concrete"), 0, 0, 0, -4 * IN, width, depth, 0);

    // ============================================================ 2 MEP layout
    stage = STAGE_MEP_LAYOUT;
    std::vector<Run> runs;
    std::vector<Device> devices;
    auto findFixture = [&](const char* type) -> const Fixture* {
        for (const Fixture& f : fixtures) if (f.type == type) return &f;
        return nullptr;
    };
    auto onOpening = [&](int story, bool alongX, float line, float s) {
        for (const Opening& o : openings) {
            if (o.story != story || (o.y0 == o.y1) != alongX) continue;
            float oc = alongX ? o.y0 : o.x0;
            if (std::fabs(oc - line) > 0.05f) continue;
            float a = alongX ? std::min(o.x0, o.x1) : std::min(o.y0, o.y1), b = alongX ? std::max(o.x0, o.x1) : std::max(o.y0, o.y1);
            if (o.kind == OpeningKind::Window) continue;   // receptacles under windows are fine
            if (s > a - 0.15f && s < b + 0.15f) return true;
        }
        return false;
    };
    const float WALL_IN = 2.25f * IN;        // stud half-depth: device boxes sit in the stud cavity
    const auto nm14 = comp("nm_14_2", 0.20f, 0.36f), nm12 = comp("nm_12_2", 0.22f, 0.40f);
    const Fixture* panel = findFixture("panel");
    P3 panelP = panel ? P3{panel->x, panel->y, storyBase(panel->story) + 1.5f} : P3{0.3f, 0.3f, 1.5f};
    int panelStory = panel ? panel->story : 0;
    // horizontal cable runs: in the joist space or attic above the story; under a half story's sloped roof
    // they run in the floor joists below instead and come up through the walls
    auto ceilingRunZ = [&](int st) { return (halfStory && st == stories - 1) ? storyBase(st) - 0.12f : storyBase(st) + H + 0.12f; };
    // Manhattan route at a given height: up/down, along x, along y, then to the end
    auto route = [&](P3 a, P3 b, float zRun) {
        std::vector<P3> p;
        p.push_back(a);
        p.push_back({a.x, a.y, zRun});
        p.push_back({b.x, a.y, zRun});
        p.push_back({b.x, b.y, zRun});
        p.push_back(b);
        return p;
    };
    auto homeRun = [&](P3 from, int story) {
        // up into this story's ceiling, across to the panel, down through floors to the panel
        std::vector<P3> p = route(from, {panelP.x, panelP.y, ceilingRunZ(story)}, ceilingRunZ(story));
        p.push_back(panelP);
        (void)panelStory;
        return p;
    };
    int circuit = 1;
    for (size_t ri = 0; ri < rooms.size(); ++ri) {
        const Room& r = rooms[ri];
        float zb = storyBase(r.story);
        bool wet = r.name.find("Kitchen") != std::string::npos || r.name.find("Bath") != std::string::npos ||
                   r.name.find("Powder") != std::string::npos || r.name.find("Laundry") != std::string::npos;
        bool hall = r.name.find("Hall") != std::string::npos || r.name.find("hall") != std::string::npos;
        auto cable = wet ? nm12 : nm14;
        std::string gauge = wet ? "12/2" : "14/2";
        // receptacles: none more than 12 ft apart along the walls (NEC 210.52(A)), 16 in to center
        std::vector<P3> recs;
        float edges[4][4] = {{r.x0, r.y0, r.x1, r.y0}, {r.x1, r.y0, r.x1, r.y1}, {r.x1, r.y1, r.x0, r.y1}, {r.x0, r.y1, r.x0, r.y0}};
        for (auto& e : edges) {
            bool alongX = e[1] == e[3];
            float a = alongX ? e[0] : e[1], b = alongX ? e[2] : e[3];
            float line = alongX ? e[1] : e[0];
            float L = std::fabs(b - a), dir = b > a ? 1.f : -1.f;
            if (hall && L < 3.0f) continue;
            // inward normal (rooms are rectangles: inside is toward the center)
            float cxr = 0.5f * (r.x0 + r.x1), cyr = 0.5f * (r.y0 + r.y1);
            float nx = alongX ? 0 : (cxr > line ? 1.f : -1.f), ny = alongX ? (cyr > line ? 1.f : -1.f) : 0;
            for (float s = 0.6f; s < L - 0.3f; s += 12 * FT) {
                float pos = a + dir * s;
                if (onOpening(r.story, alongX, line, pos)) pos += dir * 0.5f;
                if (onOpening(r.story, alongX, line, pos)) continue;
                P3 p = alongX ? P3{pos, line + ny * WALL_IN, zb + 16 * IN} : P3{line + nx * WALL_IN, pos, zb + 16 * IN};
                recs.push_back(p);
                devices.push_back({r.name + " receptacle", "receptacle", p, nx, ny});
                if (hall) break;
            }
        }
        // kitchen counters: a receptacle every 4 ft at 44 in (NEC 210.52(C))
        for (const Item& it : items) {
            if (it.story != r.story || it.name.find("base cabinets") == std::string::npos) continue;
            for (const Part& pt : it.parts) {
                bool alongX = (pt.box.x1 - pt.box.x0) > (pt.box.y1 - pt.box.y0);
                float wallLine = alongX ? (std::fabs(pt.box.y1 - r.y1) < std::fabs(pt.box.y0 - r.y0) ? r.y1 : r.y0)
                                        : (std::fabs(pt.box.x1 - r.x1) < std::fabs(pt.box.x0 - r.x0) ? r.x1 : r.x0);
                float a = alongX ? pt.box.x0 : pt.box.y0, b = alongX ? pt.box.x1 : pt.box.y1;
                float inward = (alongX ? (wallLine == r.y1 ? -1.f : 1.f) : (wallLine == r.x1 ? -1.f : 1.f));
                for (float s = a + 0.5f; s < b; s += 4 * FT) {
                    P3 p = alongX ? P3{s, wallLine + inward * WALL_IN, zb + 44 * IN} : P3{wallLine + inward * WALL_IN, s, zb + 44 * IN};
                    recs.push_back(p);
                    devices.push_back({r.name + " counter receptacle", "receptacle", p, alongX ? 0 : inward, alongX ? inward : 0});
                }
            }
        }
        if (!recs.empty()) {
            Run run;
            run.name = "Circuit " + std::to_string(circuit++) + ": " + r.name + " receptacles, NM-B " + gauge;
            run.kind = "cable";
            run.material = mat("nm_cable");
            run.thick = cable.first;
            run.width = cable.second;
            // daisy-chain through the studs at 24 in, then home run to the panel
            run.pts = homeRun(recs[0], r.story);
            std::reverse(run.pts.begin(), run.pts.end());
            for (size_t i = 1; i < recs.size(); ++i) {
                P3 a = recs[i - 1], b = recs[i];
                float zc = zb + 24 * IN;
                run.pts.push_back({a.x, a.y, zc});
                if (std::fabs(a.x - b.x) > 0.01f && std::fabs(a.y - b.y) > 0.01f) {
                    // turn the corner along the walls, never across the room
                    bool aOnXWall = std::fabs(a.y - (r.y0 + WALL_IN)) < 0.03f || std::fabs(a.y - (r.y1 - WALL_IN)) < 0.03f;
                    run.pts.push_back(aOnXWall ? P3{b.x, a.y, zc} : P3{a.x, b.y, zc});
                }
                run.pts.push_back({b.x, b.y, zc});
                run.pts.push_back(b);
            }
            runs.push_back(run);
        }
        // switch at the first doorway into the room (48 in), ceiling light at the room center
        P3 light{0.5f * (r.x0 + r.x1), 0.5f * (r.y0 + r.y1), zb + H + 1.0f * IN};
        devices.push_back({r.name + " ceiling light box", "light", light, 0, 0});
        for (const Opening& o : openings) {
            if (o.story != r.story || o.kind == OpeningKind::Window) continue;
            bool alongX = o.y0 == o.y1;
            float line = alongX ? o.y0 : o.x0;
            bool onEdge = alongX ? (std::fabs(line - r.y0) < 0.05f || std::fabs(line - r.y1) < 0.05f)
                                 : (std::fabs(line - r.x0) < 0.05f || std::fabs(line - r.x1) < 0.05f);
            if (!onEdge) continue;
            float b = alongX ? std::max(o.x0, o.x1) : std::max(o.y0, o.y1);
            float hiLim = alongX ? r.x1 : r.y1, loLim = alongX ? r.x0 : r.y0;
            float pos = b + 6 * IN;
            if (pos > hiLim - 0.1f) pos = (alongX ? std::min(o.x0, o.x1) : std::min(o.y0, o.y1)) - 6 * IN;
            if (pos < loLim + 0.1f) continue;
            float cxr = 0.5f * (r.x0 + r.x1), cyr = 0.5f * (r.y0 + r.y1);
            float nx = alongX ? 0 : (cxr > line ? 1.f : -1.f), ny = alongX ? (cyr > line ? 1.f : -1.f) : 0;
            P3 sw = alongX ? P3{pos, line + ny * WALL_IN, zb + 48 * IN} : P3{line + nx * WALL_IN, pos, zb + 48 * IN};
            devices.push_back({r.name + " light switch", "switch", sw, nx, ny});
            Run sl;
            sl.name = r.name + " switch leg, NM-B 14/2";
            sl.kind = "cable";
            sl.material = mat("nm_cable");
            sl.thick = nm14.first;
            sl.width = nm14.second;
            sl.pts = route(sw, light, ceilingRunZ(r.story));
            runs.push_back(sl);
            break;
        }
    }
    // lighting circuits: one per story, panel -> each light, nearest first
    for (int st = 0; st < stories; ++st) {
        std::vector<P3> lights;
        for (const Device& d : devices)
            if (d.kind == "light" && storyAt(d.p.z - 0.2f) == st) lights.push_back(d.p);
        if (lights.empty()) continue;
        Run run;
        run.name = "Circuit " + std::to_string(circuit++) + ": story " + std::to_string(st + 1) + " lighting, NM-B 14/2";
        run.kind = "cable";
        run.material = mat("nm_cable");
        run.thick = nm14.first;
        run.width = nm14.second;
        run.pts = homeRun(lights[0], st);
        std::reverse(run.pts.begin(), run.pts.end());
        std::vector<bool> used(lights.size(), false);
        used[0] = true;
        P3 cur = lights[0];
        for (size_t n = 1; n < lights.size(); ++n) {
            size_t best = 0;
            float bd = 1e9f;
            for (size_t i = 0; i < lights.size(); ++i) {
                if (used[i]) continue;
                float d = std::fabs(lights[i].x - cur.x) + std::fabs(lights[i].y - cur.y);
                if (d < bd) { bd = d; best = i; }
            }
            used[best] = true;
            P3 nx = lights[best];
            float zr = ceilingRunZ(st);
            run.pts.push_back({cur.x, cur.y, zr});
            run.pts.push_back({nx.x, cur.y, zr});
            run.pts.push_back({nx.x, nx.y, zr});
            run.pts.push_back(nx);
            cur = nx;
        }
        runs.push_back(run);
    }
    // plumbing: PEX supply from the water heater through the first-floor joist
    // space to every fixture; PVC drain/waste/vent to one stack through the roof.
    const Fixture* heater = findFixture("water_heater");
    const auto pex12 = comp("pex_1_2", 0.625f, 0.625f), pex34 = comp("pex_3_4", 0.875f, 0.875f);
    const auto dwv15 = comp("pvc_dwv_1_5", 1.9f, 1.9f), dwv2 = comp("pvc_dwv_2", 2.375f, 2.375f), dwv3 = comp("pvc_dwv_3", 3.5f, 3.5f);
    const float zTrunk = stories > 1 ? storyBase(0) + H + 0.08f : storyBase(0) + H + 0.15f;
    P3 heaterTop = heater ? P3{heater->x, heater->y, storyBase(heater->story) + 1.6f} : P3{0, 0, 1.6f};
    if (heater) {
        Run cold{"Cold water main, PEX 3/4 in", "water line", mat("polyethylene"), pex34.first, pex34.second, {}};
        cold.pts = {{heater->x - 0.3f, heater->y, 0.0f}, {heater->x - 0.3f, heater->y, zTrunk}};
        runs.push_back(cold);
        Run hot{"Hot water trunk, PEX 3/4 in", "water line", mat("polyethylene"), pex34.first, pex34.second, {}};
        hot.pts = {heaterTop, {heaterTop.x, heaterTop.y, zTrunk}};
        runs.push_back(hot);
    }
    const Fixture* stackFx = nullptr;
    for (const Fixture& f : fixtures)
        if (f.type == "toilet" && (!stackFx || f.story > stackFx->story)) stackFx = &f;
    P3 stack = stackFx ? P3{stackFx->x, stackFx->y, 0} : P3{width * 0.5f, depth * 0.5f, 0};
    for (const Fixture& f : fixtures) {
        bool water = f.type == "sink" || f.type == "kitchen_sink" || f.type == "toilet" || f.type == "tub" || f.type == "washer";
        if (!water) continue;
        float zb = storyBase(f.story);
        float zFix = zb + (f.type == "tub" ? 0.3f : 0.5f);
        if (heater) {
            for (int hc = 0; hc < 2; ++hc) {
                if (hc == 1 && f.type == "toilet") continue;   // toilets take cold only
                float off = hc == 0 ? -0.05f : 0.05f;
                Run w{f.name + (hc == 0 ? " cold supply" : " hot supply") + ", PEX 1/2 in", "water line", mat("polyethylene"),
                      pex12.first, pex12.second, {}};
                P3 src = hc == 0 ? P3{heater->x - 0.3f, heater->y, zTrunk} : P3{heaterTop.x, heaterTop.y, zTrunk};
                w.pts = {src, {f.x + off, src.y, zTrunk}, {f.x + off, f.y, zTrunk}, {f.x + off, f.y, zFix}};
                runs.push_back(w);
            }
        }
        // drain to the stack: under the slab on the first floor, in the joist space above it
        auto d = f.type == "toilet" ? dwv3 : (f.type == "sink" ? dwv15 : dwv2);
        std::string size = f.type == "toilet" ? "3 in" : (f.type == "sink" ? "1-1/2 in" : "2 in");
        float zRun = f.story == 0 ? -0.25f : storyBase(f.story) - 0.12f;
        float slope = 0.25f * IN / FT;   // 1/4 in per ft toward the stack
        float len = std::fabs(stack.x - f.x) + std::fabs(stack.y - f.y);
        Run dr{f.name + " drain, PVC DWV " + size, "drain", mat("pvc_vinyl"), d.first, d.second, {}};
        dr.pts = {{f.x, f.y, zb + 0.15f}, {f.x, f.y, zRun}, {stack.x, f.y, zRun - slope * std::fabs(stack.x - f.x)},
                  {stack.x, stack.y, zRun - slope * len}};
        runs.push_back(dr);
    }
    if (stackFx) {
        float D = depth;
        float run = stack.y <= 0.5f * D ? stack.y : D - stack.y;
        float roofZ = roofBase() + (halfStory ? 7.25f : 3.5f) * IN * std::sqrt(1 + pitch * pitch) + pitch * run;
        Run st{"Drain/waste/vent stack, PVC DWV 3 in", "drain", mat("pvc_vinyl"), dwv3.first, dwv3.second, {}};
        st.pts = {{stack.x, stack.y, -0.4f}, {stack.x, stack.y, roofZ + 0.3f}};
        runs.push_back(st);
    }

    // ============================================================ 3 framing
    stage = STAGE_FRAMING;
    memberObject.assign(members.size(), -1);
    for (size_t mi = 0; mi < members.size(); ++mi) {
        const Member& m = members[mi];
        float d[3] = {m.p1[0] - m.p0[0], m.p1[1] - m.p0[1], m.p1[2] - m.p0[2]};
        float L = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (L < 1e-5f) continue;
        memberObject[mi] = (int)objects.size();
        float u[3] = {d[0] / L, d[1] / L, d[2] / L}, v[3] = {m.side[0], m.side[1], m.side[2]};
        float c[3] = {0.5f * (m.p0[0] + m.p1[0]), 0.5f * (m.p0[1] + m.p1[1]), 0.5f * (m.p0[2] + m.p1[2])};
        std::string nm = memberName(m.kind);
        add(nm, nm, m.material, storyAt(c[2]), c, u, v, 0.5f * L, 0.5f * m.width, 0.5f * m.depth);
    }
    for (int s = 0; s + 1 < stories && subfloor >= 0; ++s) {   // subfloor is nailed down during framing
        const Assembly& as = lib.assemblies[subfloor];
        float plane = storyBase(s + 1);
        int k = 0;
        for (const Layer& ly : as.layers) {
            const Material& m = lib.materials[ly.material];
            for (float y = 0; y < depth - 0.01f; y += SHEET_W)
                for (float x = 0; x < width - 0.01f; x += SHEET_L, ++k) {
                    float xe = std::min(width, x + SHEET_L), ye = std::min(depth, y + SHEET_W);
                    bool inStair = false;
                    for (const Stair& st : stairs)
                        if (st.story == s && 0.5f * (x + xe) > st.x0 && 0.5f * (x + xe) < st.x1 && 0.5f * (y + ye) > st.y0 &&
                            0.5f * (y + ye) < st.y1) inStair = true;
                    if (inStair) continue;
                    addBoxXYZ(m.name + " subfloor panel " + std::to_string(k + 1), "subfloor panel", ly.material, s + 1, x, y,
                              plane - ly.thickness, xe, ye, plane);
                }
        }
    }

    // ============================================================ 4 rough-in
    stage = STAGE_ROUGH_IN;
    const auto box = comp("device_box", 2.0f, 3.0f);
    for (const Device& d : devices) {
        float c[3] = {d.p.x, d.p.y, d.p.z};
        float u[3] = {std::fabs(d.ny) > 0.5f ? 1.f : 0.f, std::fabs(d.ny) > 0.5f ? 0.f : 1.f, 0.f}, v[3] = {0, 0, 1};
        if (d.kind == "light") { u[0] = 1; u[1] = 0; v[0] = 0; v[1] = 1; v[2] = 0; }   // ceiling box lies flat
        add(d.name, d.kind == "light" ? "ceiling box" : "device box (" + d.kind + ")", mat("pvc_vinyl"), storyAt(d.p.z - 0.1f), c, u, v,
            0.5f * box.first, 0.5f * box.second, 1.25f * IN);
    }
    if (panel) {
        float zb = storyBase(panel->story);
        addBoxXYZ("Electrical service panel, 200 A", "panel", mat("steel"), panel->story, panel->x - 7 * IN, panel->y - 2 * IN,
                  zb + 1.0f, panel->x + 7 * IN, panel->y + 2 * IN, zb + 1.0f + 30 * IN);
    }
    if (heater) {
        float zb = storyBase(heater->story);
        addBoxXYZ("Water heater, 50 gal gas-fired", "water heater", mat("steel"), heater->story, heater->x - 11 * IN,
                  heater->y - 11 * IN, zb, heater->x + 11 * IN, heater->y + 11 * IN, zb + 60 * IN);
    }
    for (const Run& r : runs) {
        if (r.material < 0) continue;
        for (size_t i = 1; i < r.pts.size(); ++i) addSegment(r.name, r.kind, r.material, r.pts[i - 1], r.pts[i], r.thick, r.width);
    }

    // ============================================================ 5 insulation
    stage = STAGE_INSULATION;
    const auto batt = comp("batt_r13", 3.5f, 15.0f);
    for (const Wall& w : walls) {
        if (!w.exterior) continue;
        bool alongX = w.y0 == w.y1;
        float a = alongX ? std::min(w.x0, w.x1) : std::min(w.y0, w.y1), b = alongX ? std::max(w.x0, w.x1) : std::max(w.y0, w.y1);
        float line = alongX ? w.y0 : w.x0;
        float half = 3.5f * IN * 0.5f + 0.5f * IN;
        float cline = line <= 0.0f ? line + half : line - half;   // stud centerline (framing is pulled inside)
        bool halfW = halfStory && w.story == stories - 1;
        float wallH = !halfW ? H : (alongX ? plateHeight : collarHeight);
        float zb = storyBase(w.story) + 1.5f * IN, zt = storyBase(w.story) + wallH - 3.0f * IN;
        int k = 0;
        for (float s = a + 0.75f * IN; s + 0.1f < b; s += studSpacing) {
            if (halfW && !alongX && (s < kneeInset || s > depth - kneeInset)) continue;   // gable behind the knee walls: unfinished
            float s0 = s + 0.75f * IN, s1 = std::min(b - 0.75f * IN, s + studSpacing - 0.75f * IN);
            if (s1 - s0 < 0.05f) continue;
            bool blocked = false;
            for (const Opening& o : openings) {
                if (o.story != w.story || (o.y0 == o.y1) != alongX || std::fabs((alongX ? o.y0 : o.x0) - line) > 0.05f) continue;
                float oa = alongX ? std::min(o.x0, o.x1) : std::min(o.y0, o.y1), ob = alongX ? std::max(o.x0, o.x1) : std::max(o.y0, o.y1);
                if (s1 > oa && s0 < ob) blocked = true;
            }
            if (blocked) continue;
            float c[3] = {alongX ? 0.5f * (s0 + s1) : cline, alongX ? cline : 0.5f * (s0 + s1), 0.5f * (zb + zt)};
            float u[3] = {alongX ? 1.f : 0.f, alongX ? 0.f : 1.f, 0}, v[3] = {0, 0, 1};
            add("R-13 fiberglass batt " + std::to_string(++k), "insulation batt", mat("fiberglass"), w.story, c, u, v, 0.5f * (s1 - s0),
                0.5f * (zt - zb), 0.5f * batt.first);
        }
    }
    {
        const Assembly& as = lib.assemblies[ceiling];
        float plane = topCeiling();
        float y0 = halfStory ? slopedRun() : 0.0f, y1 = halfStory ? depth - slopedRun() : depth;   // over the flat ceiling only
        for (size_t l = 0; l < as.layers.size(); ++l) {
            const Layer& ly = as.layers[l];
            const Material& m = lib.materials[ly.material];
            if (m.density >= 50) continue;
            float zc = plane + layerOffset(lib, as, (int)l, false) + 0.5f * ly.thickness;
            float c[3] = {0.5f * width, 0.5f * (y0 + y1), zc}, u[3] = {1, 0, 0}, v[3] = {0, 1, 0};
            add(m.name + " (attic, blown)", "insulation", ly.material, stories - 1, c, u, v, 0.5f * width, 0.5f * (y1 - y0), 0.5f * ly.thickness);
        }
    }
    if (halfStory) {
        // knee walls: R-13 batts in every bay; sloped ceilings: R-30 batts in the rafter bays
        float zTop = storyBase(stories - 1), kh = kneeWallHeight();
        const float cosT = 1.0f / std::sqrt(1 + pitch * pitch), sinT = pitch * cosT;
        int k = 0;
        for (float yk : {kneeInset, depth - kneeInset})
            for (float x = 0.75f * IN; x + 0.1f < width; x += studSpacing) {
                float x1 = std::min(width - 0.75f * IN, x + studSpacing - 1.5f * IN);
                float c[3] = {0.5f * (x + 1.5f * IN + x1), yk, zTop + 0.5f * kh}, u[3] = {1, 0, 0}, v[3] = {0, 0, 1};
                add("R-13 fiberglass batt, knee wall " + std::to_string(++k), "insulation batt", mat("fiberglass"), stories - 1, c, u, v,
                    0.5f * (x1 - x - 1.5f * IN), 0.5f * (kh - 3 * IN), 0.5f * batt.first);
            }
        for (int side = 0; side < 2; ++side) {
            float sgn = side == 0 ? 1.f : -1.f;
            float ya = side == 0 ? kneeInset : depth - kneeInset;
            float len = (slopedRun() - kneeInset) / cosT;
            float zA = roofUnderside(ya) + 0.5f * 0.184f * cosT;
            float u[3] = {1, 0, 0}, v[3] = {0, sgn * cosT, sinT};
            for (float x = 0.75f * IN; x + 0.1f < width; x += rafterSpacing) {   // one batt per rafter bay
                float x0 = x + 0.75f * IN, x1 = std::min(width - 0.75f * IN, x + rafterSpacing - 0.75f * IN);
                float c[3] = {0.5f * (x0 + x1), ya + sgn * 0.5f * len * cosT, zA + 0.5f * len * sinT};
                add("R-30 fiberglass batt, sloped ceiling " + std::to_string(++k), "insulation batt", mat("fiberglass"), stories - 1, c, u, v,
                    0.5f * (x1 - x0), 0.5f * len, 0.5f * 0.184f);
            }
        }
    }

    // ============================================================ 6 walls
    stage = STAGE_WALLS;
    for (const Wall& w : walls) {
        const Assembly& as = lib.assemblies[w.assembly];
        bool alongX = w.y0 == w.y1;
        float a = alongX ? std::min(w.x0, w.x1) : std::min(w.y0, w.y1);
        float b = alongX ? std::max(w.x0, w.x1) : std::max(w.y0, w.y1);
        float line = alongX ? w.y0 : w.x0;
        float outward = w.exterior ? (line <= 0.0f ? -1.0f : 1.0f) : 1.0f;
        float zb = storyBase(w.story);
        bool halfW = halfStory && w.story == stories - 1;
        float wallH = !halfW ? H : (w.exterior && alongX ? plateHeight : (w.exterior ? ridgeHeight() - zb : collarHeight));
        float u[3] = {alongX ? 1.f : 0.f, alongX ? 0.f : 1.f, 0.f}, v[3] = {0, 0, 1};
        for (size_t l = 0; l < as.layers.size(); ++l) {
            const Layer& ly = as.layers[l];
            if (ly.fill < 0.999f || ly.thickness < 0.001f) continue;   // framing and coatings
            const Material& m = lib.materials[ly.material];
            if (std::string(m.id) == "pvc_vinyl") continue;              // siding goes on at the exterior stage
            float off = layerOffset(lib, as, (int)l, false) + 0.5f * ly.thickness;
            float pos = line + outward * off;
            // openings in this wall: sheets are cut around them
            struct R { float s0, s1, z0, z1; };
            std::vector<R> holes;
            for (const Opening& op : openings) {
                if (op.story != w.story || (op.y0 == op.y1) != alongX) continue;
                if (std::fabs((alongX ? op.y0 : op.x0) - line) > 0.05f) continue;
                holes.push_back({alongX ? std::min(op.x0, op.x1) : std::min(op.y0, op.y1), alongX ? std::max(op.x0, op.x1) : std::max(op.y0, op.y1),
                                 zb + op.sill, zb + op.head});
            }
            int k = 0;
            for (float s = a; s < b - 0.01f; s += SHEET_W, ++k) {
                float e = std::min(b, s + SHEET_W);
                std::vector<R> pieces = {{s, e, zb, zb + wallH}};
                for (const R& h : holes) {
                    std::vector<R> next;
                    for (const R& p : pieces) {
                        if (h.s1 <= p.s0 || h.s0 >= p.s1 || h.z1 <= p.z0 || h.z0 >= p.z1) { next.push_back(p); continue; }
                        if (h.s0 > p.s0) next.push_back({p.s0, h.s0, p.z0, p.z1});
                        if (h.s1 < p.s1) next.push_back({h.s1, p.s1, p.z0, p.z1});
                        float cs0 = std::max(p.s0, h.s0), cs1 = std::min(p.s1, h.s1);
                        if (h.z0 > p.z0) next.push_back({cs0, cs1, p.z0, h.z0});
                        if (h.z1 < p.z1) next.push_back({cs0, cs1, h.z1, p.z1});
                    }
                    pieces.swap(next);
                }
                bool painted = l > 0 && as.layers[l - 1].thickness < 0.001f;
                painted = painted || (l + 1 < as.layers.size() && as.layers[l + 1].thickness < 0.001f);
                for (R p : pieces) {
                    if (halfW) {   // cut the sheet off at the roof (and at the collar ceiling for interior finishes)
                        float yA = alongX ? line : p.s0, yB = alongX ? line : p.s1;
                        float roofZ = std::min(roofUnderside(yA), roofUnderside(yB));
                        bool interiorFinish = std::string(m.id) == "gypsum_board";
                        float cap = interiorFinish ? std::min(roofZ, zb + collarHeight) : roofZ;
                        if (w.exterior && !alongX && interiorFinish && (0.5f * (p.s0 + p.s1) < kneeInset || 0.5f * (p.s0 + p.s1) > depth - kneeInset)) continue;
                        p.z1 = std::min(p.z1, cap);
                    }
                    if (p.s1 - p.s0 < 0.02f || p.z1 - p.z0 < 0.02f) continue;
                    float c[3];
                    if (alongX) { c[0] = 0.5f * (p.s0 + p.s1); c[1] = pos; } else { c[0] = pos; c[1] = 0.5f * (p.s0 + p.s1); }
                    c[2] = 0.5f * (p.z0 + p.z1);
                    std::string nm = m.name + " sheet " + std::to_string(k + 1) + (pieces.size() > 1 ? " (cut)" : "") +
                                     (painted ? ", painted" : "") + (w.exterior ? " (exterior wall)" : "");
                    add(nm, std::string(m.id) + " sheet", ly.material, w.story, c, u, v, 0.5f * (p.s1 - p.s0), 0.5f * (p.z1 - p.z0),
                        0.5f * ly.thickness);
                }
            }
        }
    }
    for (int s = 0; s < stories; ++s) {   // ceilings
        bool top = s == stories - 1;
        int ai = top ? ceiling : floorCeiling;
        if (ai < 0) continue;
        const Assembly& as = lib.assemblies[ai];
        float plane = top ? topCeiling() : storyBase(s) + H;
        float yLo = top && halfStory ? slopedRun() : 0.0f, yHi = top && halfStory ? depth - slopedRun() : depth;
        for (size_t l = 0; l < as.layers.size(); ++l) {
            const Layer& ly = as.layers[l];
            const Material& m = lib.materials[ly.material];
            if (ly.thickness < 0.001f || m.density < 50) continue;
            float zc = plane + layerOffset(lib, as, (int)l, false) + 0.5f * ly.thickness;
            int k = 0;
            for (float y = yLo; y < yHi - 0.01f; y += SHEET_W)
                for (float x = 0; x < width - 0.01f; x += SHEET_L, ++k) {
                    float xe = std::min(width, x + SHEET_L), ye = std::min(yHi, y + SHEET_W);
                    float c[3] = {0.5f * (x + xe), 0.5f * (y + ye), zc}, u[3] = {1, 0, 0}, v[3] = {0, 1, 0};
                    add(m.name + " ceiling sheet " + std::to_string(k + 1) + ", painted", std::string(m.id) + " sheet", ly.material, s, c,
                        u, v, 0.5f * (xe - x), 0.5f * (ye - y), 0.5f * ly.thickness);
                }
        }
    }

    if (halfStory) {
        // knee walls and sloped ceilings: painted gypsum on the room side
        const int gyp = mat("gypsum_board");
        const float t = 0.5f * IN, zTop = storyBase(stories - 1), kh = kneeWallHeight();
        const float cosT = 1.0f / std::sqrt(1 + pitch * pitch), sinT = pitch * cosT;
        int k = 0;
        for (int side = 0; side < 2; ++side) {
            float sgn = side == 0 ? 1.f : -1.f;
            float yk = side == 0 ? kneeInset : depth - kneeInset;
            float face = yk + sgn * (1.75f * IN + 0.5f * t);   // on the room side of the knee-wall studs
            for (float x = 0; x < width - 0.01f; x += SHEET_L)
                for (float z = 0; z < kh - 0.01f; z += SHEET_W) {   // sheets hung horizontally, ripped to height
                    float xe = std::min(width, x + SHEET_L), ze = std::min(kh, z + SHEET_W);
                    float c[3] = {0.5f * (x + xe), face, zTop + 0.5f * (z + ze)}, u[3] = {1, 0, 0}, v[3] = {0, 0, 1};
                    add("Gypsum knee-wall sheet " + std::to_string(++k) + ", painted", "gypsum_board sheet", gyp, stories - 1, c, u, v,
                        0.5f * (xe - x), 0.5f * (ze - z), 0.5f * t);
                }
            float len = (slopedRun() - kneeInset) / cosT;
            float zA = roofUnderside(yk) - 0.5f * t / cosT;
            for (float x = 0; x < width - 0.01f; x += SHEET_L) {
                float xe = std::min(width, x + SHEET_L);
                float c[3] = {0.5f * (x + xe), yk + sgn * 0.5f * len * cosT, zA + 0.5f * len * sinT};
                float u[3] = {1, 0, 0}, v[3] = {0, sgn * cosT, sinT};
                add("Gypsum sloped-ceiling sheet " + std::to_string(++k) + ", painted", "gypsum_board sheet", gyp, stories - 1, c, u, v,
                    0.5f * (xe - x), 0.5f * len, 0.5f * t);
            }
        }
    }

    // ============================================================ 7 roofing
    stage = STAGE_ROOFING;
    const float RB = roofBase(), D = depth, oh = eaveOverhang;
    const float cosT = 1.0f / std::sqrt(1 + pitch * pitch), sinT = pitch * cosT;
    const float chord = (halfStory ? 7.25f : 3.5f) * IN / cosT;   // deck sits on 2x8 rafters or 2x4 truss chords
    {
        const Assembly& as = lib.assemblies[roof];
        const float slopeLen = (0.5f * D + oh) / cosT;
        for (int side = 0; side < 2; ++side) {
            float sgn = side == 0 ? 1.f : -1.f;
            float v[3] = {0, sgn * cosT, sinT}, u[3] = {1, 0, 0};
            float ystart = side == 0 ? -oh : D + oh;
            float z0 = RB + chord - pitch * oh;
            float nrm[3];
            cross3(u, v, nrm);
            norm3(nrm);
            if (nrm[2] < 0) for (float& q : nrm) q = -q;
            float off = 0;
            for (size_t l = 0; l < as.layers.size(); ++l) {
                const Layer& ly = as.layers[l];
                const Material& m = lib.materials[ly.material];
                float t = ly.thickness, lift = off + 0.5f * t;
                if (m.density > 1000) {
                    // individual laminated shingles, 13-1/4 x 39-3/8 in at 5-5/8 in exposure, each course
                    // offset; the visible exposure is modelled (each shingle laps the one below)
                    const auto sh = comp("shingle", 13.25f, 39.375f);
                    const float expo = 5.625f * IN, shW = sh.second, shH = sh.first;
                    int k = 0, course = 0;
                    for (float sl = 0; sl < slopeLen - 0.01f; sl += expo, ++course) {
                        float se = std::min(slopeLen, sl + shH);
                        float off = (course % 2) ? 0.5f * shW : 0.0f;
                        for (float x = -off; x < width - 0.01f; x += shW) {
                            float x0 = std::max(0.0f, x), x1 = std::min(width, x + shW);
                            if (x1 - x0 < 0.02f) continue;
                            float sm = 0.5f * (sl + se);
                            float c[3] = {0.5f * (x0 + x1), ystart + sgn * sm * cosT + nrm[1] * lift, z0 + sm * sinT + nrm[2] * lift};
                            add("Shingle " + std::to_string(++k) + (side == 0 ? " (south)" : " (north)"), "shingle", ly.material, stories - 1,
                                c, u, v, 0.5f * (x1 - x0), 0.5f * (se - sl), 0.5f * t);
                        }
                    }
                    if (side == 1) {   // ridge cap: 12 in pieces at 5 in exposure along the ridge
                        float zr = RB + chord + pitch * 0.5f * D + t + 0.01f;
                        int r = 0;
                        for (float x = 0; x < width - 0.01f; x += 5 * IN) {
                            float x1 = std::min(width, x + 12 * IN);
                            float c[3] = {0.5f * (x + x1), 0.5f * D, zr}, uu[3] = {1, 0, 0}, vv[3] = {0, 1, 0};
                            add("Ridge cap shingle " + std::to_string(++r), "shingle", ly.material, stories - 1, c, uu, vv,
                                0.5f * (x1 - x), 6 * IN, 0.5f * t);
                        }
                    }
                } else {
                    int k = 0;
                    for (float sl = 0; sl < slopeLen - 0.01f; sl += SHEET_W)
                        for (float x = 0; x < width - 0.01f; x += SHEET_L, ++k) {
                            float se = std::min(slopeLen, sl + SHEET_W), xe = std::min(width, x + SHEET_L);
                            float sm = 0.5f * (sl + se);
                            float c[3] = {0.5f * (x + xe), ystart + sgn * sm * cosT + nrm[1] * lift, z0 + sm * sinT + nrm[2] * lift};
                            add(m.name + " roof deck panel " + std::to_string(k + 1) + (side == 0 ? " (south)" : " (north)"),
                                "roof deck panel", ly.material, stories - 1, c, u, v, 0.5f * (xe - x), 0.5f * (se - sl), 0.5f * t);
                        }
                }
                off += t;
            }
        }
    }

    // ============================================================ 8 exterior
    stage = STAGE_EXTERIOR;
    const auto course = comp("siding_course", 0.044f, 8.0f);
    const auto gut = comp("gutter_k5", 5.0f, 5.0f), dsp = comp("downspout", 2.0f, 3.0f), fasc = comp("board_1x6", 0.75f, 5.5f);
    const int vinyl = mat("pvc_vinyl");
    auto slopeTop = [&](float y) { float run = y <= 0.5f * D ? y : D - y; return RB + pitch * run; };
    {
        const Assembly& ew = lib.assemblies[extWall];
        float outer = 0;
        for (size_t l = 0; l < ew.layers.size(); ++l)
            if (std::string(lib.materials[ew.layers[l].material].id) == "pvc_vinyl") outer = layerOffset(lib, ew, (int)l, false) + 0.5f * ew.layers[l].thickness;
        const float pieceL = 12.5f * FT;   // vinyl siding panels are sold 12 ft 6 in long
        // wall courses, story by story, from the slab to the eaves
        for (int side = 0; side < 4; ++side) {
            bool alongX = side % 2 == 0;
            float line = side == 0 ? 0 : side == 1 ? width : side == 2 ? depth : 0;
            float len = alongX ? width : depth;
            float pos = (side == 0 || side == 3) ? line - outer : line + outer;
            float u[3] = {alongX ? 1.f : 0.f, alongX ? 0.f : 1.f, 0}, v[3] = {0, 0, 1};
            int k = 0;
            for (float z = 0; z < RB - 0.01f; z += course.second) {
                float zt = std::min(RB, z + course.second);
                // windows and doors in this wall at this height: siding is cut around them
                std::vector<std::pair<float, float>> holes;
                for (const Opening& op : openings) {
                    if (op.kind == OpeningKind::Cased || (op.y0 == op.y1) != alongX) continue;
                    float ol = alongX ? op.y0 : op.x0;
                    if (std::fabs(ol - line) > 0.05f) continue;
                    float zb0 = storyBase(op.story) + op.sill, zb1 = storyBase(op.story) + op.head;
                    if (zt <= zb0 || z >= zb1) continue;
                    holes.push_back({alongX ? std::min(op.x0, op.x1) : std::min(op.y0, op.y1), alongX ? std::max(op.x0, op.x1) : std::max(op.y0, op.y1)});
                }
                for (float s = 0; s < len - 0.01f; s += pieceL) {
                    float e = std::min(len, s + pieceL);
                    std::vector<std::pair<float, float>> pieces = {{s, e}};
                    for (auto& h : holes) {
                        std::vector<std::pair<float, float>> next;
                        for (auto& pc : pieces) {
                            if (h.second <= pc.first || h.first >= pc.second) { next.push_back(pc); continue; }
                            if (h.first > pc.first) next.push_back({pc.first, h.first});
                            if (h.second < pc.second) next.push_back({h.second, pc.second});
                        }
                        pieces.swap(next);
                    }
                    for (auto& pc : pieces) {
                        if (pc.second - pc.first < 0.02f) continue;
                        float c[3] = {alongX ? 0.5f * (pc.first + pc.second) : pos, alongX ? pos : 0.5f * (pc.first + pc.second), 0.5f * (z + zt)};
                        if (side == 2) c[1] = depth + outer;
                        if (side == 1) c[0] = width + outer;
                        add("Vinyl siding course " + std::to_string(++k), "siding course", vinyl, storyAt(c[2]), c, u, v,
                            0.5f * (pc.second - pc.first), 0.5f * (zt - z), 0.5f * course.first);
                    }
                }
            }
        }
        // gable ends, trimmed to the roof line
        for (int end = 0; end < 2; ++end) {
            float x = end == 0 ? -outer : width + outer;
            float u[3] = {0, 1, 0}, v[3] = {0, 0, 1};
            int k = 0;
            for (float z = RB; z < slopeTop(0.5f * D) - 0.02f; z += course.second) {
                float zt = std::min(slopeTop(0.5f * D), z + course.second);
                float run = (0.5f * (z + zt) - RB) / pitch;
                float y0 = run, y1 = D - run;
                if (y1 - y0 < 0.05f) break;
                float c[3] = {x, 0.5f * (y0 + y1), 0.5f * (z + zt)};
                add("Vinyl siding course, gable " + std::to_string(++k), "siding course", vinyl, stories - 1, c, u, v,
                    0.5f * (y1 - y0), 0.5f * (zt - z), 0.5f * course.first);
            }
        }
    }
    for (int side = 0; side < 2; ++side) {   // soffit, fascia, gutter along each eave
        float yIn = side == 0 ? 0.0f : D, yOut = side == 0 ? -oh : D + oh;
        float zEdge = RB + chord - pitch * oh;
        float a = std::min(yIn, yOut), b = std::max(yIn, yOut);
        std::string sname = side == 0 ? " (south eave)" : " (north eave)";
        int k = 0;
        for (float x = 0; x < width - 0.01f; x += 1.0f * FT)   // vented vinyl soffit panels, 12 in wide
            addBoxXYZ("Vented vinyl soffit panel " + std::to_string(++k) + sname, "soffit", vinyl, stories - 1, x, a, RB - 0.03f,
                      std::min(width, x + 1.0f * FT), b, RB - 0.03f + 0.0011f);
        float fy = side == 0 ? yOut - 0.5f * fasc.first : yOut + 0.5f * fasc.first;
        k = 0;
        for (float x = 0; x < width - 0.01f; x += 16 * FT)     // fascia boards, 16 ft lengths
            addBoxXYZ("Fascia board " + std::to_string(++k) + sname, "fascia", mat("lumber_spf"), stories - 1, x, fy - 0.5f * fasc.first,
                      zEdge - fasc.second, std::min(width, x + 16 * FT), fy + 0.5f * fasc.first, zEdge);
        float gy = side == 0 ? yOut - fasc.first - 0.5f * gut.first : yOut + fasc.first + 0.5f * gut.first;
        k = 0;
        for (float x = 0; x < width - 0.01f; x += 10 * FT)     // gutter sections, 10 ft lengths
            addBoxXYZ("5 in K-style gutter section " + std::to_string(++k) + sname, "gutter", mat("aluminum"), stories - 1, x,
                      gy - 0.5f * gut.first, zEdge - 0.08f - gut.second, std::min(width, x + 10 * FT), gy + 0.5f * gut.first, zEdge - 0.08f);
        for (float x : {0.15f, width - 0.15f}) {
            addBoxXYZ("Downspout, 2 x 3 in", "downspout", mat("aluminum"), 0, x - 0.5f * dsp.second, gy - 0.5f * dsp.first, 0.0f,
                      x + 0.5f * dsp.second, gy + 0.5f * dsp.first, zEdge - 0.08f - gut.second);
        }
    }
    auto addOpening = [&](const Opening& o) {
        const Assembly& as = lib.assemblies[o.assembly];
        float total = 0;
        for (const Layer& l : as.layers) total += l.thickness;
        bool alongX = o.y0 == o.y1;
        float a = alongX ? std::min(o.x0, o.x1) : std::min(o.y0, o.y1), b = alongX ? std::max(o.x0, o.x1) : std::max(o.y0, o.y1);
        float line = alongX ? o.y0 : o.x0;
        float c[3];
        if (alongX) { c[0] = 0.5f * (a + b); c[1] = line; } else { c[0] = line; c[1] = 0.5f * (a + b); }
        c[2] = storyBase(o.story) + 0.5f * (o.sill + o.head);
        float u[3] = {alongX ? 1.f : 0.f, alongX ? 0.f : 1.f, 0.f}, v[3] = {0, 0, 1};
        bool door = o.kind == OpeningKind::Door;
        add(o.name, door ? "door slab" : "window unit", as.layers[0].material, o.story, c, u, v, 0.5f * (b - a), 0.5f * (o.head - o.sill),
            0.5f * std::max(total, 0.006f));
    };
    auto isExterior = [&](const Opening& o) {
        bool alongX = o.y0 == o.y1;
        float line = alongX ? o.y0 : o.x0;
        return std::fabs(line) < 0.05f || std::fabs(line - (alongX ? depth : width)) < 0.05f;
    };
    for (const Opening& o : openings)
        if (o.kind != OpeningKind::Cased && o.assembly >= 0 && isExterior(o)) addOpening(o);

    // ============================================================ 9 interior
    stage = STAGE_INTERIOR;
    for (const Opening& o : openings)
        if (o.kind != OpeningKind::Cased && o.assembly >= 0 && !isExterior(o)) addOpening(o);
    for (const Room& r : rooms) {
        if (r.floorMaterial < 0) continue;
        const Material& m = lib.materials[r.floorMaterial];
        float t = std::max(0.002f, r.floorArealMass / m.density);
        float zb = storyBase(r.story);
        // flooring is cut around a stairwell opening coming up from the story below
        struct Rc { float x0, y0, x1, y1; };
        std::vector<Rc> pieces = {{r.x0, r.y0, r.x1, r.y1}};
        for (const Stair& st : stairs) {
            if (st.story != r.story - 1) continue;
            std::vector<Rc> next;
            for (const Rc& p : pieces) {
                if (st.x1 <= p.x0 || st.x0 >= p.x1 || st.y1 <= p.y0 || st.y0 >= p.y1) { next.push_back(p); continue; }
                if (st.x0 > p.x0) next.push_back({p.x0, p.y0, st.x0, p.y1});
                if (st.x1 < p.x1) next.push_back({st.x1, p.y0, p.x1, p.y1});
                float cx0 = std::max(p.x0, st.x0), cx1 = std::min(p.x1, st.x1);
                if (st.y0 > p.y0) next.push_back({cx0, p.y0, cx1, st.y0});
                if (st.y1 < p.y1) next.push_back({cx0, st.y1, cx1, p.y1});
            }
            pieces.swap(next);
        }
        bool tile = std::string(m.id) == "pvc_vinyl";
        float stepX = tile ? 1.0f * FT : 1e9f, stepY = tile ? 1.0f * FT : 12.0f * FT;   // 12 in tiles, or 12 ft carpet widths
        int k = 0;
        for (const Rc& p : pieces) {
            if (p.x1 - p.x0 <= 0.02f || p.y1 - p.y0 <= 0.02f) continue;
            for (float y = p.y0; y < p.y1 - 0.005f; y += stepY)
                for (float x = p.x0; x < p.x1 - 0.005f; x += stepX) {
                    float x1 = std::min(p.x1, x + stepX), y1 = std::min(p.y1, y + stepY);
                    addBoxXYZ(r.name + (tile ? " vinyl floor tile " : " carpet, 12 ft width ") + std::to_string(++k), "floor covering",
                              r.floorMaterial, r.story, x, y, zb, x1, y1, zb + t);
                }
        }
    }
    for (size_t it = 0; it < items.size(); ++it)
        for (const Part& p : items[it].parts) {
            SceneObject& o = addBoxXYZ(items[it].name + " - " + lib.materials[p.material].name, "furniture part", p.material,
                                       items[it].story, p.box.x0, p.box.y0, p.box.z0, p.box.x1, p.box.y1, p.box.z1);
            o.item = (int)it;
        }
}

} // namespace photon
