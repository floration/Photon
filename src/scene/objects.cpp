// Photon - expands the scene's construction description into physical objects:
// every framing member, gypsum and OSB sheet at standard 4 x 8 ft size,
// subfloor and roof-deck panels, door slabs, window units, floor coverings and
// furniture parts, each at its true size and position.
#include "scene/scene.h"
#include <algorithm>
#include <cmath>

namespace photon {

namespace {
constexpr float FT = 0.3048f;
constexpr float SHEET_W = 4 * FT, SHEET_L = 8 * FT;   // standard 4 x 8 ft panel (ASTM C1396 / PS 2)

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
    }
    return "member";
}
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
    auto add = [&](const std::string& name, const std::string& kind, int mat, int story, const float c[3],
                   const float u[3], const float v[3], float hu, float hv, float hn) -> SceneObject& {
        SceneObject o;
        o.id = (int)objects.size() + 1;
        o.name = name;
        o.kind = kind;
        o.material = mat;
        o.story = story;
        for (int i = 0; i < 3; ++i) { o.c[i] = c[i]; o.u[i] = u[i]; o.v[i] = v[i]; }
        cross3(o.u, o.v, o.n);
        norm3(o.n);
        o.hu = hu; o.hv = hv; o.hn = hn;
        objects.push_back(o);
        return objects.back();
    };
    // ---- framing members ----------------------------------------------------
    for (const Member& m : members) {
        float d[3] = {m.p1[0] - m.p0[0], m.p1[1] - m.p0[1], m.p1[2] - m.p0[2]};
        float L = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        if (L < 1e-5f) continue;
        float u[3] = {d[0] / L, d[1] / L, d[2] / L}, v[3] = {m.side[0], m.side[1], m.side[2]};
        norm3(v);
        float c[3] = {0.5f * (m.p0[0] + m.p1[0]), 0.5f * (m.p0[1] + m.p1[1]), 0.5f * (m.p0[2] + m.p1[2])};
        std::string nm = memberName(m.kind);
        add(nm, nm, m.material, storyAt(c[2]), c, u, v, 0.5f * L, 0.5f * m.width, 0.5f * m.depth);
    }
    // ---- wall sheets (gypsum, sheathing, siding) at their layer positions -----
    for (const Wall& w : walls) {
        const Assembly& as = lib.assemblies[w.assembly];
        bool alongX = w.y0 == w.y1;
        float a = alongX ? std::min(w.x0, w.x1) : std::min(w.y0, w.y1);
        float b = alongX ? std::max(w.x0, w.x1) : std::max(w.y0, w.y1);
        float line = alongX ? w.y0 : w.x0;
        // layers run toward +axis unless this is an exterior wall on the +axis side of the house
        float outward = 1.0f;
        if (w.exterior) {
            bool onLow = line <= 0.0f;
            outward = onLow ? -1.0f : 1.0f;   // inside (layer 0) faces the interior
        }
        float zb = storyBase(w.story);
        float H = ceilingHeight;
        float u[3] = {alongX ? 1.f : 0.f, alongX ? 0.f : 1.f, 0.f}, v[3] = {0, 0, 1};
        for (size_t l = 0; l < as.layers.size(); ++l) {
            const Layer& ly = as.layers[l];
            if (ly.fill < 0.999f || ly.thickness < 0.001f) continue;   // framing (members) and coatings
            const Material& m = lib.materials[ly.material];
            float off = layerOffset(lib, as, (int)l, false) + 0.5f * ly.thickness;
            float pos = line + outward * off;
            int k = 0;
            for (float s = a; s < b - 0.01f; s += SHEET_W, ++k) {
                float e = std::min(b, s + SHEET_W);
                float c[3];
                if (alongX) { c[0] = 0.5f * (s + e); c[1] = pos; } else { c[0] = pos; c[1] = 0.5f * (s + e); }
                c[2] = zb + 0.5f * H;
                std::string nm = m.name + " sheet " + std::to_string(k + 1) + (w.exterior ? " (exterior wall)" : "");
                add(nm, std::string(m.id) + " sheet", ly.material, w.story, c, u, v, 0.5f * (e - s), 0.5f * H, 0.5f * ly.thickness);
            }
        }
    }
    // ---- ceilings, attic insulation, subfloors -------------------------------
    for (int s = 0; s < stories; ++s) {
        bool top = s == stories - 1;
        int ai = top ? ceiling : floorCeiling;
        if (ai >= 0) {
            const Assembly& as = lib.assemblies[ai];
            float plane = storyBase(s) + ceilingHeight;
            for (size_t l = 0; l < as.layers.size(); ++l) {
                const Layer& ly = as.layers[l];
                if (ly.thickness < 0.001f) continue;
                const Material& m = lib.materials[ly.material];
                float zc = plane + layerOffset(lib, as, (int)l, false) + 0.5f * ly.thickness;
                float u[3] = {1, 0, 0}, v[3] = {0, 1, 0};
                if (m.density < 50) {   // loose-fill insulation: one blanket over the ceiling
                    float c[3] = {0.5f * width, 0.5f * depth, zc};
                    add(m.name + " (attic)", "insulation", ly.material, s, c, u, v, 0.5f * width, 0.5f * depth, 0.5f * ly.thickness);
                    continue;
                }
                int k = 0;
                for (float y = 0; y < depth - 0.01f; y += SHEET_W)
                    for (float x = 0; x < width - 0.01f; x += SHEET_L, ++k) {
                        float xe = std::min(width, x + SHEET_L), ye = std::min(depth, y + SHEET_W);
                        float c[3] = {0.5f * (x + xe), 0.5f * (y + ye), zc};
                        add(m.name + " ceiling sheet " + std::to_string(k + 1), std::string(m.id) + " sheet", ly.material, s, c, u, v,
                            0.5f * (xe - x), 0.5f * (ye - y), 0.5f * ly.thickness);
                    }
            }
        }
        if (!top && subfloor >= 0) {
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
                        float c[3] = {0.5f * (x + xe), 0.5f * (y + ye), plane - 0.5f * ly.thickness};
                        float u[3] = {1, 0, 0}, v[3] = {0, 1, 0};
                        add(m.name + " subfloor panel " + std::to_string(k + 1), "subfloor panel", ly.material, s + 1, c, u, v,
                            0.5f * (xe - x), 0.5f * (ye - y), 0.5f * ly.thickness);
                    }
            }
        }
    }
    // ---- roof deck panels and shingle field per slope ------------------------
    {
        const Assembly& as = lib.assemblies[roof];
        const float RB = roofBase(), D = depth, oh = eaveOverhang;
        const float cosT = 1.0f / std::sqrt(1 + pitch * pitch), sinT = pitch * cosT;
        const float chord = 3.5f * 0.0254f / cosT;   // deck sits on the 2x4 top chords
        const float slopeLen = (0.5f * D + oh) / cosT;
        for (int side = 0; side < 2; ++side) {
            float sgn = side == 0 ? 1.f : -1.f;   // south slope rises toward +y
            float v[3] = {0, sgn * cosT, sinT}, u[3] = {1, 0, 0};
            float ystart = side == 0 ? -oh : D + oh;
            float z0 = RB + chord - pitch * oh;
            float off = 0;
            for (size_t l = 0; l < as.layers.size(); ++l) {
                const Layer& ly = as.layers[l];
                const Material& m = lib.materials[ly.material];
                float t = ly.thickness;
                float nrm[3];
                cross3(u, v, nrm);
                norm3(nrm);
                if (nrm[2] < 0) for (float& q : nrm) q = -q;
                float lift = off + 0.5f * t;
                if (m.density > 1000) {   // shingle field: one object per slope
                    float sm = 0.5f * slopeLen;
                    float c[3] = {0.5f * width, ystart + sgn * sm * cosT + nrm[1] * lift, z0 + sm * sinT + nrm[2] * lift};
                    add(m.name + (side == 0 ? " (south slope)" : " (north slope)"), "roofing", ly.material, stories - 1, c, u, v,
                        0.5f * width, 0.5f * slopeLen, 0.5f * t);
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
    // ---- doors and windows ----------------------------------------------------
    for (const Opening& o : openings) {
        if (o.kind == OpeningKind::Cased || o.assembly < 0) continue;
        const Assembly& as = lib.assemblies[o.assembly];
        float total = 0;
        for (const Layer& l : as.layers) total += l.thickness;
        bool alongX = o.y0 == o.y1;
        float a = alongX ? std::min(o.x0, o.x1) : std::min(o.y0, o.y1);
        float b = alongX ? std::max(o.x0, o.x1) : std::max(o.y0, o.y1);
        float line = alongX ? o.y0 : o.x0;
        float zb = storyBase(o.story);
        float c[3];
        if (alongX) { c[0] = 0.5f * (a + b); c[1] = line; } else { c[0] = line; c[1] = 0.5f * (a + b); }
        c[2] = zb + 0.5f * (o.sill + o.head);
        float u[3] = {alongX ? 1.f : 0.f, alongX ? 0.f : 1.f, 0.f}, v[3] = {0, 0, 1};
        bool door = o.kind == OpeningKind::Door;
        add(o.name, door ? "door slab" : "window unit", as.layers[0].material, o.story, c, u, v, 0.5f * (b - a),
            0.5f * (o.head - o.sill), 0.5f * std::max(total, 0.006f));
    }
    // ---- floor coverings --------------------------------------------------------
    for (const Room& r : rooms) {
        if (r.floorMaterial < 0) continue;
        const Material& m = lib.materials[r.floorMaterial];
        float t = std::max(0.002f, r.floorArealMass / m.density);
        float c[3] = {0.5f * (r.x0 + r.x1), 0.5f * (r.y0 + r.y1), storyBase(r.story) + 0.5f * t};
        float u[3] = {1, 0, 0}, v[3] = {0, 1, 0};
        add(r.name + " floor: " + m.name, "floor covering", r.floorMaterial, r.story, c, u, v, 0.5f * (r.x1 - r.x0),
            0.5f * (r.y1 - r.y0), 0.5f * t);
    }
    // ---- furniture parts ----------------------------------------------------
    for (size_t it = 0; it < items.size(); ++it) {
        for (const Part& p : items[it].parts) {
            float c[3] = {0.5f * (p.box.x0 + p.box.x1), 0.5f * (p.box.y0 + p.box.y1), 0.5f * (p.box.z0 + p.box.z1)};
            float u[3] = {1, 0, 0}, v[3] = {0, 1, 0};
            SceneObject& o = add(items[it].name + " - " + lib.materials[p.material].name, "furniture part", p.material,
                                 items[it].story, c, u, v, 0.5f * (p.box.x1 - p.box.x0), 0.5f * (p.box.y1 - p.box.y0),
                                 0.5f * (p.box.z1 - p.box.z0));
            o.item = (int)it;
        }
    }
}

} // namespace photon
