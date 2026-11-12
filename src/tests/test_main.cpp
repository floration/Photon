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
        ok = lib.loadMaterials("data/materials.txt", err) && lib.loadAssemblies("data/assemblies.txt", err) &&
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
    CHECK(sheets > 100 && studs > 300 && doors >= 10 && parts > 50 && deck > 20);
    // gypsum sheets are standard 4 x 8 ft or cut smaller, never larger
    for (const SceneObject& o : w.scene.objects)
        if (o.kind == "gypsum_board sheet")
            CHECK_MSG(2 * std::min(o.hu, o.hv) <= 4 * 0.3048f + 1e-4f && 2 * std::max(o.hu, o.hv) <= 8 * 0.3048f + 1e-4f, o.name);
    // a 2x4 interior wall's gypsum faces sit 1-3/4 in + 1/4 in off the centerline (stud half depth + half board)
    float off = Scene::layerOffset(w.lib, w.lib.assemblies[w.lib.findAssembly("int_wall_2x4")], 1, false) + 0.5f * 0.0127f;
    CHECK(std::fabs(off - (-(1.75f * 0.0254f) - 0.5f * 0.0127f)) < 1e-4f);
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
