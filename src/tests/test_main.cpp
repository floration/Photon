// Photon - project test runner. No framework: TEST() registers a function,
// CHECK() records a failure and keeps going. Every test must finish in a few
// seconds; long validation runs belong in the (opt-in) validation suite.
//
//   photon_tests            run everything
//   photon_tests <filter>   run tests whose name contains <filter>
#include "scene/materials.h"
#include "scene/scene.h"
#include "scene/textfile.h"
#include "sim/solver.h"
#include "sim/structure.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

using namespace photon;

namespace {

struct TestCase { const char* name; std::function<void()> fn; };
std::vector<TestCase>& registry() { static std::vector<TestCase> r; return r; }
int g_failures = 0;
const char* g_current = "";

struct Registrar {
    Registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};
#define TEST(name) \
    static void test_##name(); \
    static Registrar reg_##name(#name, test_##name); \
    static void test_##name()
#define CHECK(cond) \
    do { if (!(cond)) { ++g_failures; std::printf("    FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_MSG(cond, msg) \
    do { if (!(cond)) { ++g_failures; std::printf("    FAIL %s:%d  %s  (%s)\n", __FILE__, __LINE__, #cond, std::string(msg).c_str()); } } while (0)

// Shared fixture: data and the model home, loaded once.
struct World {
    MaterialLibrary lib;
    Scene scene;
    std::string err;
    bool ok = false;
    World() {
        ok = lib.loadGases("data/gases.txt", err) && lib.loadMaterials("data/materials.txt", err) && lib.loadAssemblies("data/assemblies.txt", err) &&
             lib.loadComponents("data/components.txt", err) && lib.loadAssets("data/assets.txt", err) &&
             scene.load("scenes/model_home.txt", lib, err);
    }
};
World& world() { static World w; return w; }

int firstGasAbove(const Solver& s, const Scene& sc, const std::string& item) {
    for (int c = 0; c < s.N; ++c) {
        if (s.itemOf[c] < 0 || sc.items[s.itemOf[c]].name != item) continue;
        int up = c + s.nx * s.ny;
        if (up < s.N && !s.blocked[up]) return up;
    }
    return -1;
}

} // namespace

// ---------------------------------------------------------------- formats --
TEST(textfile_tokens) {
    const char* path = "photon_test_tmp.txt";
    {
        std::ofstream f(path, std::ios::binary);
        f << "\xEF\xBB\xBF" << "key  \"two words\"  3.5 # comment\n\n  # only comment\nnext\t1\r\n";
    }
    TextFile tf;
    std::string err;
    CHECK(tf.load(path, err));
    CHECK(tf.lines.size() == 2);
    if (tf.lines.size() == 2) {
        CHECK(tf.lines[0].tok.size() == 3);
        CHECK(tf.lines[0].tok[0] == "key");          // byte order mark stripped
        CHECK(tf.lines[0].tok[1] == "two words");
        CHECK(tf.lines[1].tok.size() == 2 && tf.lines[1].tok[1] == "1");
        CHECK(tf.lines[1].number == 4);
    }
    std::remove(path);
}

TEST(materials_have_sources) {
    World& w = world();
    CHECK_MSG(w.ok, w.err);
    CHECK(w.lib.materials.size() >= 13);
    for (const Material& m : w.lib.materials) {
        CHECK_MSG(!m.source.empty(), m.id);
        CHECK_MSG(m.density > 0 && m.conductivity > 0 && m.specificHeat > 0, m.id);
        if (m.combustible) CHECK_MSG(m.heatOfGasification > 0 && m.heatOfCombustion > 0, m.id);
    }
}

TEST(format_versions) {
    const char* path = "photon_test_ver.txt";
    { std::ofstream f(path); f << "photon_materials 99\n"; }
    MaterialLibrary lib;
    std::string err;
    CHECK(!lib.loadMaterials(path, err));
    CHECK(err.find("newer") != std::string::npos);
    { std::ofstream f(path); f << "material x\nend\n"; }
    CHECK(!lib.loadMaterials(path, err));      // header required
    std::remove(path);
    World& w = world();
    CHECK(w.lib.component("lumber_2x4") && std::fabs(w.lib.component("lumber_2x4")->width - 3.5f * 0.0254f) < 1e-6f);
    for (const Component& c : w.lib.components) CHECK_MSG(!c.source.empty(), c.id);
}

TEST(gases_and_offgas) {
    World& w = world();
    CHECK(w.lib.gases.size() >= 15);
    for (const Gas& g : w.lib.gases) {
        CHECK_MSG(!g.source.empty() && g.molarMass > 0, g.id);
        if (g.heatOfCombustion > 0) CHECK_MSG(g.lfl > 0 && g.ufl > g.lfl, g.id);
    }
    for (const Material& m : w.lib.materials) {
        if (m.combustible) CHECK_MSG(!m.offgas.empty(), m.id);
        float sum = 0;
        for (const Offgas& o : m.offgas) { CHECK(o.gas >= 0); sum += o.massFraction; }
        if (!m.offgas.empty()) CHECK_MSG(std::fabs(sum - 1) < 0.02f, m.id);
    }
    // PVC gives off hydrogen chloride; gypsum gives off steam
    auto has = [&](const char* mat, const char* gas) {
        const Material& m = w.lib.materials[w.lib.findMaterial(mat)];
        for (const Offgas& o : m.offgas) if (w.lib.gases[o.gas].id == gas) return true;
        return false;
    };
    CHECK(has("pvc_vinyl", "HCl"));
    CHECK(has("gypsum_board", "H2O"));
    CHECK(has("pu_foam", "HCN"));
}

TEST(assemblies_resolve) {
    World& w = world();
    CHECK(w.lib.assemblies.size() >= 10);
    for (const Assembly& a : w.lib.assemblies) {
        CHECK_MSG(!a.source.empty(), a.id);
        CHECK_MSG(!a.layers.empty() && a.layers.size() <= (size_t)MAX_LAYERS, a.id);
        for (const Layer& l : a.layers) CHECK_MSG(l.material >= 0 && l.thickness > 0, a.id);
    }
}

TEST(scene_loads) {
    World& w = world();
    CHECK_MSG(w.ok, w.err);
    CHECK(w.scene.stories == 2);
    CHECK(w.scene.rooms.size() >= 10);
    CHECK(!w.scene.stairs.empty());
    CHECK(w.scene.members.size() > 500);
    // actual 2x4 size (PS 20): 1.5 x 3.5 in
    bool found = false;
    for (const Member& m : w.scene.members)
        if (m.kind == MemberKind::Stud) {
            CHECK(std::fabs(m.width - 1.5f * 0.0254f) < 1e-5f && std::fabs(m.depth - 3.5f * 0.0254f) < 1e-5f);
            found = true;
            break;
        }
    CHECK(found);
}

TEST(scene_rejects_bad_input) {
    World& w = world();
    const char* path = "photon_test_bad.txt";
    {
        std::ofstream f(path);
        f << "photon_scene 1\nunits ft\nwall int_wall_2x4 0 0 5 5\n";   // diagonal wall
    }
    Scene sc;
    std::string err;
    CHECK(!sc.load(path, w.lib, err));
    CHECK(err.find("axis-aligned") != std::string::npos);
    std::remove(path);
}

TEST(scene_objects) {
    World& w = world();
    int sheets = 0, studs = 0, doors = 0, parts = 0, deck = 0;
    for (const SceneObject& o : w.scene.objects) {
        if (o.kind == "gypsum_board sheet") ++sheets;
        if (o.kind == "stud") ++studs;
        if (o.kind == "door slab") ++doors;
        if (o.kind == "furniture part") ++parts;
        if (o.kind == "roof deck panel") ++deck;
        CHECK_MSG(o.material >= 0 && o.hu > 0 && o.hv > 0 && o.hn > 0, o.name);
    }
    CHECK(sheets > 100 && studs > 300 && doors >= 8 && parts > 50 && deck > 20);
    // gypsum sheets are standard 4 x 8 ft or cut smaller, never larger
    for (const SceneObject& o : w.scene.objects)
        if (o.kind == "gypsum_board sheet")
            CHECK_MSG(2 * std::min(o.hu, o.hv) <= 4 * 0.3048f + 1e-4f && 2 * std::max(o.hu, o.hv) <= 8 * 0.3048f + 1e-4f, o.name);
    // a 2x4 interior wall's gypsum faces sit 1-3/4 in + 1/4 in off the centerline (stud half depth + half board)
    float off = Scene::layerOffset(w.lib, w.lib.assemblies[w.lib.findAssembly("int_wall_2x4")], 1, false) + 0.5f * 0.0127f;
    CHECK(std::fabs(off - (-(1.75f * 0.0254f) - 0.5f * 0.0127f)) < 1e-4f);
}

TEST(build_stages_complete) {
    World& w = world();
    int perStage[STAGE_COUNT + 1] = {};
    int cables = 0, water = 0, drains = 0, receptacles = 0, batts = 0, siding = 0, gutters = 0, trash = 0;
    for (const SceneObject& o : w.scene.objects) {
        CHECK_MSG(o.stage >= STAGE_FLOORPLAN && o.stage <= STAGE_COUNT, o.name);
        if (o.stage >= 1 && o.stage <= STAGE_COUNT) perStage[o.stage]++;
        if (o.kind == "cable") ++cables;
        if (o.kind == "water line") ++water;
        if (o.kind == "drain") ++drains;
        if (o.kind == "device box (receptacle)") ++receptacles;
        if (o.kind == "insulation batt") ++batts;
        if (o.kind == "siding course") ++siding;
        if (o.kind == "gutter") ++gutters;
        if (o.name.find("trash") != std::string::npos || o.name.find("wastebasket") != std::string::npos) ++trash;
    }
    for (int s = STAGE_FLOORPLAN; s <= STAGE_COUNT; ++s)
        if (s != STAGE_MEP_LAYOUT) CHECK_MSG(perStage[s] > 0, std::string("no objects in stage ") + stageName(s));
    CHECK(cables > 50 && water > 10 && drains > 5 && receptacles > 30);
    CHECK(batts > 50 && siding > 50 && gutters == 2 && trash >= 4);
    // wiring and plumbing are installed before the walls close
    for (const SceneObject& o : w.scene.objects)
        if (o.kind == "cable" || o.kind == "water line" || o.kind == "drain") CHECK(o.stage == STAGE_ROUGH_IN);
}

TEST(half_story_cape) {
    World& w = world();
    const Scene& sc = w.scene;
    CHECK(sc.halfStory && sc.stories == 2);
    int rafters = 0, collars = 0, ridge = 0, trusses = 0;
    for (const Member& m : sc.members) {
        rafters += m.kind == MemberKind::Rafter;
        collars += m.kind == MemberKind::CollarTie;
        ridge += m.kind == MemberKind::RidgeBoard;
        trusses += m.kind == MemberKind::TrussChord;
    }
    CHECK(rafters > 50 && collars > 25 && ridge == 1 && trusses == 0);
    CHECK(std::fabs(sc.kneeWallHeight() - 5 * 0.3048f) < 0.01f);   // 2 ft plate + 3 ft at 12/12
    Solver s;
    std::string err;
    CHECK_MSG(s.build(sc, w.lib, err), err);
    int knee = 0, sloped = 0;
    int kneeAsm = w.lib.findAssembly("knee_wall_2x4"), slopeAsm = w.lib.findAssembly("roof_sloped_ceiling");
    for (const Barrier& b : s.barriers) {
        knee += b.assembly == kneeAsm;
        sloped += b.assembly == slopeAsm;
    }
    CHECK(knee > 100 && sloped > 100);
    // the knee-wall attic is a separate space: a cell behind the knee wall and one in the room are walled off
    float zMid = sc.storyBase(1) + 0.5f;
    int attic = s.cellAt(13 * 0.3048f, 1.5f * 0.3048f, zMid), room = s.cellAt(13 * 0.3048f, 3.6f * 0.3048f, zMid);
    CHECK(attic >= 0 && room >= 0 && !s.blocked[room]);
    if (attic >= 0 && room >= 0) {
        int ai, aj, ak, ri, rj, rk;
        s.cellIJK(attic, ai, aj, ak);
        s.cellIJK(room, ri, rj, rk);
        bool wall = false;
        for (int j = aj; j < rj; ++j) wall = wall || s.barrierIntact(s.barrierOn(s.cell(ai, j, ak), 3));
        (void)ri; (void)rk;
        CHECK(wall);
    }
}

// ------------------------------------------------------------ discretizer --
TEST(discretizer_invariants) {
    World& w = world();
    Solver s;
    std::string err;
    CHECK_MSG(s.build(w.scene, w.lib, err), err);
    CHECK(s.kFloorS.size() == 2 && s.kCeilS.size() == 2);
    CHECK(s.kFloorS[1] > s.kCeilS[0]);   // a joist-space layer exists
    // every barrier is indexed from its face, and vice versa
    for (size_t b = 0; b < s.barriers.size(); ++b) {
        const Barrier& br = s.barriers[b];
        const std::vector<int>& fv = br.axis == 0 ? s.bx : br.axis == 1 ? s.by : s.bz;
        CHECK(fv[br.face] == (int)b);
    }
    // intact walls are closed apart from their leakage fraction
    int bad = 0;
    for (const Barrier& br : s.barriers) {
        const std::vector<float>& f = br.axis == 0 ? s.fx : br.axis == 1 ? s.fy : s.fz;
        float expect = br.userOpen ? 1.0f : br.leak;
        if (f[br.face] > expect + 1e-6f) ++bad;
    }
    CHECK(bad == 0);
    // the stairwell has no ceiling between the stories
    const Stair& st = w.scene.stairs[0];
    int c = s.cellAt(0.5f * (st.x0 + st.x1), 0.5f * (st.y0 + st.y1), s.oz + (s.kCeilS[0] + 0.5f) * s.dx);
    CHECK(c >= 0 && s.barrierOn(c, 4) < 0 && s.barrierOn(c, 5) < 0);
    // the ground is closed, the sky is open
    CHECK(s.fz[s.zface(0, 0, 0)] == 0.0f);
    CHECK(s.fz[s.zface(0, 0, s.nz)] == 1.0f);
}

// ----------------------------------------------------------------- solver --
TEST(smoke_stays_in_closed_room) {
    World& w = world();
    Solver s;
    std::string err;
    CHECK_MSG(s.build(w.scene, w.lib, err), err);
    // Bedroom 3 (door closed): fill its air with soot, step a little, check where it went
    const Room* r = nullptr;
    for (const Room& rm : w.scene.rooms) if (rm.name == "Bedroom 3") r = &rm;
    CHECK(r != nullptr);
    if (!r) return;
    std::vector<uint8_t> inRoom(s.N, 0);
    double total0 = 0;
    for (int c = 0; c < s.N; ++c) {
        int i, j, k;
        s.cellIJK(c, i, j, k);
        float x = s.ox + (i + 0.5f) * s.dx, y = s.oy + (j + 0.5f) * s.dx, z = s.oz + (k + 0.5f) * s.dx;
        const Room* rr = w.scene.roomAt(x, y, z);
        if (rr == r && k >= s.kFloorS[1] && k < s.kCeilS[1] && !s.blocked[c]) {
            inRoom[c] = 1;
            s.Ys[c] = 0.01f;
            total0 += s.Ys[c];
        }
    }
    for (int i = 0; i < 20; ++i) s.step(0.05f);
    double inside = 0, outside = 0;
    for (int c = 0; c < s.N; ++c) (inRoom[c] ? inside : outside) += s.Ys[c];
    CHECK(total0 > 0);
    CHECK_MSG(outside < 0.01 * total0, "soot outside " + std::to_string(outside / total0 * 100) + "%");
}

TEST(solver_deterministic) {
    World& w = world();
    auto run = [&](std::vector<float>& T) {
        Solver s;
        std::string err;
        s.build(w.scene, w.lib, err);
        s.addIgnition(firstGasAbove(s, w.scene, "Sofa"), 100e3f, 60, "test");
        for (int i = 0; i < 40; ++i) s.step(0.05f);
        T = s.T;
    };
    std::vector<float> a, b;
    run(a);
    run(b);
    CHECK(a.size() == b.size());
    size_t diff = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) diff += a[i] != b[i];
    CHECK_MSG(diff == 0, std::to_string(diff) + " cells differ");
}

TEST(burner_heats_and_burns) {
    World& w = world();
    Solver s;
    std::string err;
    s.build(w.scene, w.lib, err);
    s.addIgnition(firstGasAbove(s, w.scene, "Sofa"), 100e3f, 60, "test");
    for (int i = 0; i < 60; ++i) s.step(0.05f);
    CHECK(s.stats.hrr > 20e3f);              // the burner's fuel is burning
    CHECK(s.stats.maxGasT > 600.0f);
    // oxygen never goes negative, temperatures stay in range
    bool okRange = true;
    for (int c = 0; c < s.N; ++c)
        if (s.Yo[c] < 0 || s.Yo[c] > 0.2321f || s.T[c] < 199 || s.T[c] > 2301 || std::isnan(s.T[c])) okRange = false;
    CHECK(okRange);
}

TEST(structure_stable_then_collapses) {
    World& w = world();
    Solver s;
    std::string err;
    CHECK_MSG(s.build(w.scene, w.lib, err), err);
    Structure st;
    CHECK(st.build(w.scene, w.lib, s));
    CHECK(st.pieces().size() > 1500);
    float total = 0;
    for (const Piece& p : st.pieces()) total += p.mass;
    CHECK_MSG(total > 20000 && total < 400000, "house mass " + std::to_string(total) + " kg");   // tens of tonnes incl. slab
    st.update(s, 0.25f);
    CHECK_MSG(st.fallenCount() == 0, "pieces fell with no fire");
    // engineering capacities in the right range (intact, ultimate)
    for (const Piece& p : st.pieces()) {
        const std::string& k = w.scene.objects[p.object].kind;
        if (k == "floor joist" && p.len > 3.5f) CHECK_MSG(p.capacity > 5e3f && p.capacity < 3e4f, "joist " + std::to_string(p.capacity) + " N");
        if (k == "stud" && p.len > 2.0f) CHECK_MSG(p.capacity > 5e3f && p.capacity < 8e4f, "stud " + std::to_string(p.capacity) + " N");
    }
    int falling = 0;
    for (const Piece& p : st.pieces()) falling += p.state != PieceState::Standing;
    CHECK_MSG(falling == 0, std::to_string(falling) + " pieces unstable at t=0");
    // burn away all the roof framing: the roof deck must come down
    for (int c : s.solidCells)
        if (s.porous[c] && s.fuelMat[c] == w.lib.findMaterial("lumber_spf") && c / (s.nx * s.ny) >= s.kFloorS[1]) s.fuelMass[c] = 0;
    for (size_t b = 0; b < s.barriers.size(); ++b) {   // and the rafters inside the sloped ceilings
        if (s.barriers[b].kind != BarrierKind::Roof) continue;
        const Assembly& as = w.lib.assemblies[s.barriers[b].assembly];
        for (int l = 0; l < (int)as.layers.size(); ++l)
            if (as.layers[l].fill < 0.999f) s.removeLayer((int)b, l);
    }
    for (int i = 0; i < 20; ++i) st.update(s, 0.25f);
    int deckDown = 0, deck = 0;
    for (const Piece& p : st.pieces())
        if (w.scene.objects[p.object].kind == "roof deck panel") { ++deck; deckDown += p.state != PieceState::Standing; }
    CHECK_MSG(deck > 0 && deckDown > deck / 2, std::to_string(deckDown) + " of " + std::to_string(deck) + " deck panels fell");
}

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (const TestCase& tc : registry()) {
        if (filter && !std::strstr(tc.name, filter)) continue;
        g_current = tc.name;
        int before = g_failures;
        auto ts = std::chrono::steady_clock::now();
        tc.fn();
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ts).count();
        std::printf("%-4s %-32s %7.0f ms\n", g_failures == before ? "ok" : "FAIL", tc.name, ms);
        ++run;
    }
    double total = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("%d tests, %d failed checks, %.1f s\n", run, g_failures, total);
    return g_failures == 0 ? 0 : 1;
}
