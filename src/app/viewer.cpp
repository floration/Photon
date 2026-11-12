// Photon - Phase 1 viewer: orbit camera, cutaway toggles, click to ignite,
// doors and windows, temperature slice, live solver.
#include "core/vmath.h"
#include "platform/platform.h"
#include "render/renderer.h"
#include "scene/materials.h"
#include "scene/scene.h"
#include "sim/solver.h"
#include "app/ui.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <thread>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace photon;

namespace {

struct Source { const char* name; float kw; float seconds; };
// PROTOTYPE source strengths; Phase 6 replaces them with cited values
// (SFPE Handbook, NFPA 921) and real source models.
const Source SOURCES[3] = {
    {"Match / lighter flame 0.05 kW, 30 s", 0.05f, 30.0f},
    {"Small open flame 10 kW, 2 min (wastebasket-size)", 10.0f, 120.0f},
    {"Large flame 100 kW, 2 min (trash bag-size)", 100.0f, 120.0f},
};
const float SPEEDS[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 1e9f};
constexpr int NSPEED = 7;

Vec3 glow(float T) {
    float t = std::clamp((T - 760.0f) / 900.0f, 0.0f, 1.0f);
    return Vec3(1.0f, 0.22f + 0.6f * t, 0.04f + 0.45f * t * t) * (t * t * 1.6f);
}

std::string mmss(float t) {
    char b[32];
    int s = (int)t;
    std::snprintf(b, sizeof(b), "%02d:%02d", s / 60, s % 60);
    return b;
}
std::string fmt(const char* f, double a) { char b[64]; std::snprintf(b, sizeof(b), f, a); return b; }

class Viewer {
public:
    bool init(const std::string& scenePath, const std::string& demo, std::string& err);
    void run();

private:
    Platform plat_;
    Renderer ren_;
    MaterialLibrary lib_;
    Scene scene_;
    std::unique_ptr<Solver> sim_;
    Input in_;
    // camera
    float yaw_ = 0, pitch_ = 0, flySpeed_ = 5.0f;   // m/s
    void lookFrom(Vec3 eye, Vec3 target);
    Mat4 vp_;
    Vec3 eye_;
    // toggles
    std::atomic<bool> running_{true};
    bool hideRoof_ = true, hideCeiling_ = true, framing_ = false, help_ = false, upperCut_ = false;
    float cutZ() const { return upperCut_ && sim_->kCeilS.size() > 1 ? sim_->oz + sim_->kCeilS[0] * sim_->dx : 1e9f; }
    RenderOptions ropt_;
    int source_ = 1;
    std::atomic<int> speedIdx_{2};
    // The solver runs on its own thread; the UI thread takes simMtx_ while it
    // reads or changes solver state.
    std::mutex simMtx_;
    std::thread simThread_;
    std::atomic<bool> quit_{false};
    std::atomic<bool> uiWants_{false};   // UI thread is waiting for the lock: solver steps aside
    std::atomic<float> simRate_{1.0f};   // achieved / requested speed
    void simLoop();
    int paintMat_ = -1;
    double lastMesh_ = -1;
    bool meshDirty_ = true;
    int dragButton_ = -1, dragPixels_ = 0;
    std::vector<float> vol_;
    std::string hover_;
    std::string demo_;
    std::string still_;          // --shot: render a few fixed views, save them, exit (no solving)
    int stillFrame_ = 0;
    std::vector<float> shots_ = {20, 75, 150, 240, 330, 420};
    size_t shotIdx_ = 0;
    int shotCount_ = 0;
    void igniteItem(const std::string& name, float kw, float seconds);

    void resetSim();
    void updateCamera();
    void handleInput(float frameDt);
    void drawPanels();
    Ui ui_;
    bool showScene_ = true;
    float sceneScroll_ = 0;
    int clickMode_ = 0;            // 0 ignite, 1 inspect
    bool haveSel_ = false;
    struct SelHit { int kind = 0, cell = -1, from = -1, barrier = -1; Vec3 pos; } sel_;
    int selObj_ = -1;
    void buildMeshes();
    void uploadVolume();
    void drawOverlay();
    bool barrierVisible(const Barrier& b) const;
    struct Hit { int kind = 0; int cell = -1; int from = -1; int barrier = -1; Vec3 pos; };  // 1 floor, 2 solid, 3 barrier
    Hit pick(int mx, int my);
    std::string describe(const Hit& h);
    const SceneObject* objectFor(const Hit& h);
};

bool Viewer::init(const std::string& scenePath, const std::string& demo, std::string& err) {
    if (demo.rfind("shot:", 0) == 0) { still_ = demo.substr(5); running_ = false; help_ = false; }
    else demo_ = demo;
    std::string base = exeDir();
    if (!lib_.loadMaterials(base + "data/materials.txt", err)) return false;
    if (!lib_.loadAssemblies(base + "data/assemblies.txt", err)) return false;
    std::string sp = scenePath;
    if (sp.find(':') == std::string::npos && sp.rfind('/', 0) != 0) sp = base + sp;
    if (!scene_.load(sp, lib_, err)) return false;
    paintMat_ = lib_.findMaterial("latex_paint");
    if (!plat_.createWindow("Photon - Phase 1 prototype - PROTOTYPE PHYSICS, NOT VALIDATED", 1600, 950, err)) return false;
    if (!ren_.init(plat_, err)) return false;
    resetSim();
    if (!sim_) { err = "solver build failed"; return false; }
    lookFrom(Vec3(scene_.width * 0.5f + 9.0f, -10.0f, 13.0f), Vec3(scene_.width * 0.5f, scene_.depth * 0.5f, 0.5f));
    if (!demo_.empty()) {
        help_ = false;
        speedIdx_ = NSPEED - 1;
        lookFrom(Vec3(10.5f, -4.5f, 8.5f), Vec3(3.5f, 3.0f, 0.5f));
        igniteItem("Sofa", 10.0f, 120.0f);
        for (size_t o = 0; o < scene_.openings.size(); ++o)
            if (scene_.openings[o].name == "Front door") sim_->toggleOpening((int)o);
        lookFrom(Vec3(1.2f, 7.5f, 2.0f), Vec3(3.0f, 2.0f, 0.8f));
    }
    return true;
}

void Viewer::lookFrom(Vec3 eye, Vec3 target) {
    eye_ = eye;
    Vec3 d = normalize(target - eye);
    yaw_ = std::atan2(d.y, d.x);
    pitch_ = std::asin(std::clamp(d.z, -1.0f, 1.0f));
}

void Viewer::igniteItem(const std::string& name, float kw, float seconds) {
    Solver& s = *sim_;
    for (int c = 0; c < s.N; ++c) {
        if (s.itemOf[c] < 0 || scene_.items[s.itemOf[c]].name != name) continue;
        int up = c + s.nx * s.ny;
        if (up < s.N && !s.blocked[up]) { s.addIgnition(up, kw * 1000.0f, seconds, SOURCES[1].name); return; }
    }
}

void Viewer::resetSim() {
    sim_ = std::make_unique<Solver>();
    std::string err;
    if (!sim_->build(scene_, lib_, err)) { showMessage("Photon", "Solver build failed: " + err); sim_.reset(); return; }
    meshDirty_ = true;
}

void Viewer::updateCamera() {
    float cp = std::cos(pitch_), sp = std::sin(pitch_);
    Vec3 fwd(std::cos(yaw_) * cp, std::sin(yaw_) * cp, sp);
    float aspect = plat_.height() > 0 ? (float)plat_.width() / plat_.height() : 1.0f;
    // 120 degree horizontal field of view; vertical follows the window aspect
    float vfov = 2.0f * std::atan(std::tan(0.5f * 120.0f * 3.14159265f / 180.0f) / aspect);
    vp_ = perspective(vfov, aspect, 0.05f, 300.0f) * lookAt(eye_, eye_ + fwd, Vec3(0, 0, 1));
}

bool Viewer::barrierVisible(const Barrier& b) const {
    if (b.failed || b.userOpen) return false;
    switch (b.kind) {
    case BarrierKind::Roof: case BarrierKind::Fascia: case BarrierKind::Gable: case BarrierKind::Soffit: return !hideRoof_;
    case BarrierKind::Ceiling: return !hideCeiling_ && !framing_;
    case BarrierKind::Wall: return !framing_;
    default: return true;
    }
}

// ---------------------------------------------------------------- picking --
Viewer::Hit Viewer::pick(int mx, int my) {
    Hit h;
    const Solver& s = *sim_;
    Mat4 inv;
    invert(vp_, inv);
    float nxp = 2.0f * mx / std::max(1, plat_.width()) - 1, nyp = 1 - 2.0f * my / std::max(1, plat_.height());
    float a[4], b[4];
    transformPoint(inv, nxp, nyp, -1, 1, a);
    transformPoint(inv, nxp, nyp, 1, 1, b);
    Vec3 ro(a[0] / a[3], a[1] / a[3], a[2] / a[3]), rf(b[0] / b[3], b[1] / b[3], b[2] / b[3]);
    Vec3 rd = normalize(rf - ro);
    // enter the grid box
    float bmin[3] = {s.ox, s.oy, s.oz}, bmax[3] = {s.ox + s.nx * s.dx, s.oy + s.ny * s.dx, s.oz + s.nz * s.dx};
    float o[3] = {ro.x, ro.y, ro.z}, d[3] = {rd.x, rd.y, rd.z};
    float t0 = 0, t1 = 1e9f;
    for (int q = 0; q < 3; ++q) {
        if (std::fabs(d[q]) < 1e-9f) { if (o[q] < bmin[q] || o[q] > bmax[q]) return h; continue; }
        float ta = (bmin[q] - o[q]) / d[q], tb = (bmax[q] - o[q]) / d[q];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta);
        t1 = std::min(t1, tb);
    }
    if (t0 > t1) return h;
    float t = t0 + 1e-4f;
    float p[3] = {o[0] + d[0] * t, o[1] + d[1] * t, o[2] + d[2] * t};
    int ci[3] = {std::clamp((int)std::floor((p[0] - s.ox) / s.dx), 0, s.nx - 1),
                 std::clamp((int)std::floor((p[1] - s.oy) / s.dx), 0, s.ny - 1),
                 std::clamp((int)std::floor((p[2] - s.oz) / s.dx), 0, s.nz - 1)};
    int dims[3] = {s.nx, s.ny, s.nz};
    int stepd[3];
    float tMax[3], tDelta[3];
    float org[3] = {s.ox, s.oy, s.oz};
    for (int q = 0; q < 3; ++q) {
        stepd[q] = d[q] > 0 ? 1 : -1;
        float nextB = org[q] + (ci[q] + (d[q] > 0 ? 1 : 0)) * s.dx;
        tMax[q] = std::fabs(d[q]) < 1e-9f ? 1e30f : (nextB - o[q]) / d[q];
        tDelta[q] = std::fabs(d[q]) < 1e-9f ? 1e30f : s.dx / std::fabs(d[q]);
    }
    int cur = s.cell(ci[0], ci[1], ci[2]);
    if (s.blocked[cur]) { h.kind = 2; h.cell = cur; h.from = cur; h.pos = Vec3(p[0], p[1], p[2]); return h; }
    for (int iter = 0; iter < 1000; ++iter) {
        int ax = tMax[0] < tMax[1] ? (tMax[0] < tMax[2] ? 0 : 2) : (tMax[1] < tMax[2] ? 1 : 2);
        float tf = tMax[ax];
        int nci[3] = {ci[0], ci[1], ci[2]};
        nci[ax] += stepd[ax];
        Vec3 hp = ro + rd * tf;
        // face between cur and next
        int dir = ax * 2 + (stepd[ax] > 0 ? 1 : 0);
        int bidx = s.barrierOn(cur, dir);
        if (bidx >= 0 && barrierVisible(s.barriers[bidx])) {
            h.kind = 3; h.barrier = bidx; h.from = cur; h.pos = hp; return h;
        }
        if (nci[ax] < 0 || nci[ax] >= dims[ax]) {
            if (ax == 2 && nci[2] < 0) { h.kind = 1; h.cell = cur; h.from = cur; h.pos = hp; }
            return h;
        }
        int nc = s.cell(nci[0], nci[1], nci[2]);
        if (s.blocked[nc]) { h.kind = 2; h.cell = nc; h.from = cur; h.pos = hp; return h; }
        cur = nc;
        ci[0] = nci[0]; ci[1] = nci[1]; ci[2] = nci[2];
        tMax[ax] += tDelta[ax];
    }
    return h;
}

const SceneObject* Viewer::objectFor(const Hit& h) {
    const Solver& s = *sim_;
    float p[3];
    float tol = 0.006f;
    int item = -1;
    if (h.kind == 3) {
        // move from the face plane to the surface actually drawn on the camera's side
        const Barrier& b = s.barriers[h.barrier];
        const Assembly& as = lib_.assemblies[b.assembly];
        bool centered = b.kind == BarrierKind::Door || b.kind == BarrierKind::Window;
        bool fromA = h.from == b.cellA;
        int asmSide = fromA == b.aIsInside ? 0 : 1;
        int l = asmSide == 0 ? b.front : b.back;
        float off = Scene::layerOffset(lib_, as, l, centered) + 0.5f * as.layers[l].thickness;   // middle of the exposed layer
        float sgn = b.aIsInside ? 1.0f : -1.0f;
        float d[3] = {0, 0, 0};
        d[b.axis] = sgn * off;
        p[0] = h.pos.x + d[0]; p[1] = h.pos.y + d[1]; p[2] = h.pos.z + d[2];
        if (b.kind == BarrierKind::Roof || b.kind == BarrierKind::Window || b.kind == BarrierKind::Door) tol = 0.2f;
    } else if (h.kind == 2) {
        int i, j, k;
        s.cellIJK(h.cell, i, j, k);
        p[0] = s.ox + (i + 0.5f) * s.dx; p[1] = s.oy + (j + 0.5f) * s.dx; p[2] = s.oz + (k + 0.5f) * s.dx;
        item = s.itemOf[h.cell];
        tol = 0.15f;
    } else if (h.kind == 1) {
        p[0] = h.pos.x; p[1] = h.pos.y; p[2] = h.pos.z + 0.001f;
        tol = 0.02f;
    } else return nullptr;
    const SceneObject* best = nullptr;
    float bestVol = 1e30f;
    for (const SceneObject& o : scene_.objects) {
        if (item >= 0 && o.item != item) continue;
        if (!o.contains(p, tol)) continue;
        float vol = o.hu * o.hv * std::max(o.hn, 0.001f);
        if (vol < bestVol) { bestVol = vol; best = &o; }
    }
    return best;
}

std::string Viewer::describe(const Hit& h) {
    const Solver& s = *sim_;
    if (h.kind == 0) return "";
    std::string out;
    const Room* rm = scene_.roomAt(h.pos.x, h.pos.y, h.pos.z + 0.01f);
    out += rm ? rm->name : "Outside";
    if (h.from >= 0 && !s.blocked[h.from]) {
        int c = h.from;
        float rho = s.gasDensity(c);
        out += "  gas " + fmt("%.0f C", s.T[c] - 273.15) + "  O2 " + fmt("%.1f%%", s.Yo[c] * 100) +
               "  soot " + fmt("%.2f g/m3", s.Ys[c] * rho * 1000) + "  fuel gas " + fmt("%.2f%%", s.Yf[c] * 100);
    }
    if (h.kind == 2) {
        int c = h.cell;
        if (s.fuelMat[c] >= 0) {
            const Material& m = lib_.materials[s.fuelMat[c]];
            int it = s.itemOf[c];
            out += "  |  " + (it >= 0 ? scene_.items[it].name : std::string("solid")) + ": " + m.name;
            out += "  " + fmt("%.0f C", s.fuelT[c] - 273.15);
            if (m.combustible && s.fuelMass0[c] > 0)
                out += "  " + fmt("%.0f%% left", 100.0 * s.fuelMass[c] / (s.fuelMass0[c] * (1 - m.charYield)));
        }
    } else if (h.kind == 3) {
        const Barrier& b = s.barriers[h.barrier];
        const Assembly& as = lib_.assemblies[b.assembly];
        out += "  |  ";
        if (b.opening >= 0) out += scene_.openings[b.opening].name + " - ";
        out += as.name;
        out += "  faces " + fmt("%.0f C", b.T[0] - 273.15) + " / " + fmt("%.0f C", b.T[1] - 273.15);
        out += "  layers " + std::to_string(b.back - b.front + 1) + "/" + std::to_string(as.layers.size());
        if (b.opening >= 0) out += b.kind == BarrierKind::Door ? "  [right-click: open]" : "  [right-click: break]";
    } else if (h.kind == 1) {
        int c = h.cell;
        if (s.fuelMat[c] >= 0) out += "  |  floor: " + lib_.materials[s.fuelMat[c]].name + "  " + fmt("%.0f C", s.fuelT[c] - 273.15);
    }
    // the physical object under the cursor
    if (const SceneObject* o = objectFor(h)) {
        auto in = [](float m) { return fmt("%.1f", m / 0.0254f); };
        out += "\n" + std::string("Object #") + std::to_string(o->id) + ": " + o->name + "   " + in(2 * o->hu) + " x " + in(2 * o->hv) +
               " x " + in(2 * o->hn) + " in   [" + o->kind + "]";
    }
    return out;
}

// ------------------------------------------------------------------ input --
void Viewer::handleInput(float frameDt) {
    const Input& in = in_;
    if (in.keyPressed['P'] || in.keyPressed[KEY_PAUSE]) running_ = !running_.load();
    if (in.keyPressed['1']) source_ = 0;
    if (in.keyPressed['2']) source_ = 1;
    if (in.keyPressed['3']) source_ = 2;
    if (in.keyPressed['R'] || in.keyPressed[KEY_F2]) { hideRoof_ = !hideRoof_; meshDirty_ = true; }
    if (in.keyPressed['Z'] || in.keyPressed[KEY_F3]) { hideCeiling_ = !hideCeiling_; meshDirty_ = true; }
    if (in.keyPressed['F'] || in.keyPressed[KEY_F4]) { framing_ = !framing_; meshDirty_ = true; }
    if (in.keyPressed['X'] || in.keyPressed[KEY_F5]) ropt_.smoke = !ropt_.smoke;
    if (in.keyPressed['V']) ropt_.fire = !ropt_.fire;
    if (in.keyPressed['T'] || in.keyPressed[KEY_F6]) ropt_.slice = !ropt_.slice;
    if (in.keyPressed['U']) { upperCut_ = !upperCut_; meshDirty_ = true; }
    if (in.keyPressed['H'] || in.keyPressed[KEY_F1]) help_ = !help_;
    if (in.keyPressed[KEY_PAGEUP]) ropt_.sliceZ = std::min(ropt_.sliceZ + 0.2f, scene_.ridgeHeight());
    if (in.keyPressed[KEY_PAGEDOWN]) ropt_.sliceZ = std::max(ropt_.sliceZ - 0.2f, 0.1f);
    if (in.keyPressed[KEY_PLUS]) speedIdx_ = std::min(speedIdx_.load() + 1, NSPEED - 1);
    if (in.keyPressed[KEY_MINUS]) speedIdx_ = std::max(speedIdx_.load() - 1, 0);
    if (in.keyPressed[KEY_BACKSPACE]) resetSim();
    // ---- fly camera: WASD move, Q/E down/up, Shift fast, Ctrl slow,
    // hold either mouse button and drag to look, wheel moves forward/back
    float dt = std::min(frameDt, 0.1f);
    float turn = 1.8f * dt;
    if (in.keyDown[KEY_LEFT]) yaw_ += turn;
    if (in.keyDown[KEY_RIGHT]) yaw_ -= turn;
    if (in.keyDown[KEY_UP]) pitch_ += turn;
    if (in.keyDown[KEY_DOWN]) pitch_ -= turn;
    Vec3 fwd = Vec3(std::cos(pitch_) * std::cos(yaw_), std::cos(pitch_) * std::sin(yaw_), std::sin(pitch_));
    Vec3 right = normalize(cross(fwd, Vec3(0, 0, 1)));
    float speed = flySpeed_ * (in.keyDown[KEY_SHIFT] ? 4.0f : 1.0f) * (in.keyDown[KEY_CTRL] ? 0.25f : 1.0f);
    Vec3 move;
    if (in.keyDown['W']) move += fwd;
    if (in.keyDown['S']) move += fwd * -1;
    if (in.keyDown['D']) move += right;
    if (in.keyDown['A']) move += right * -1;
    if (in.keyDown['E'] || in.keyDown[KEY_SPACE]) move += Vec3(0, 0, 1);
    if (in.keyDown['Q'] || in.keyDown['C']) move += Vec3(0, 0, -1);
    if (length(move) > 0) eye_ += normalize(move) * (speed * dt);
    if (in.wheel != 0) eye_ += fwd * (in.wheel * 0.6f * (in.keyDown[KEY_SHIFT] ? 4.0f : 1.0f));
    eye_.z = std::max(eye_.z, 0.15f);

    for (int b = 0; b < 3; ++b)
        if (in.pressed[b] && dragButton_ < 0 && !ui_.mouseOverUi()) { dragButton_ = b; dragPixels_ = 0; }
    if (dragButton_ >= 0) {
        dragPixels_ += std::abs(in.dMouseX) + std::abs(in.dMouseY);
        if (dragPixels_ > 4) {
            yaw_ -= in.dMouseX * 0.0035f;
            pitch_ -= in.dMouseY * 0.0035f;
        }
        if (in.released[dragButton_]) {
            if (dragPixels_ <= 4 && !ui_.mouseOverUi()) {
                Hit h = pick(in.mouseX, in.mouseY);
                if (dragButton_ == 0 && h.kind != 0 && (clickMode_ == 1 || in.ctrl)) {
                    haveSel_ = true;
                    sel_.kind = h.kind; sel_.cell = h.cell; sel_.from = h.from; sel_.barrier = h.barrier; sel_.pos = h.pos;
                    const SceneObject* o = objectFor(h);
                    selObj_ = o ? o->id - 1 : -1;
                } else if (dragButton_ == 0 && h.kind != 0) {
                    int gasCell = h.kind == 1 ? h.cell : h.from;
                    if (gasCell >= 0 && !sim_->blocked[gasCell]) {
                        const Source& src = SOURCES[source_];
                        sim_->addIgnition(gasCell, src.kw * 1000.0f, src.seconds, src.name);
                        running_ = true;
                    }
                } else if (dragButton_ == 1 && h.kind == 3) {
                    int op = sim_->barriers[h.barrier].opening;
                    if (op >= 0) { sim_->toggleOpening(op); meshDirty_ = true; }
                }
            }
            dragButton_ = -1;
        }
    }
    pitch_ = std::clamp(pitch_, -1.55f, 1.55f);
}

// Solver thread: steps continuously, paced to the requested speed. "max"
// runs flat out. The UI thread never waits on a whole frame of solving.
void Viewer::simLoop() {
    using clock = std::chrono::steady_clock;
    auto last = clock::now();
    double debt = 0;                 // simulated seconds owed to the wall clock
    double winWall = 0, winSim = 0;  // 1 s window for the achieved-rate readout
    while (!quit_) {
        auto now = clock::now();
        double wall = std::chrono::duration<double>(now - last).count();
        last = now;
        float speed = SPEEDS[speedIdx_.load()];
        bool maxSpeed = speed > 1e8f;
        if (!running_) { debt = 0; simRate_ = 1.0f; std::this_thread::sleep_for(std::chrono::milliseconds(5)); continue; }
        if (!maxSpeed) debt = std::min(debt + wall * speed, 0.5 * speed + 0.1);
        winWall += wall;
        if (!maxSpeed && debt <= 0) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
        // let the UI thread in first; std::mutex is not fair
        while (uiWants_.load() && !quit_) std::this_thread::yield();
        float dt;
        {
            std::lock_guard<std::mutex> lk(simMtx_);
            if (!sim_) continue;
            dt = sim_->step(0.05f);
        }
        debt -= dt;
        winSim += dt;
        if (winWall > 1.0) {
            simRate_ = maxSpeed ? (float)(winSim / winWall) : (float)std::min(1.0, winSim / (winWall * speed));
            winWall = winSim = 0;
        }
    }
}
// ----------------------------------------------------------------- meshes --
void addQuad(std::vector<Vertex>& v, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 n, Vec3 col, Vec3 emi) {
    Vec3 q[6] = {a, b, c, a, c, d};
    for (const Vec3& p : q) v.push_back({{p.x, p.y, p.z}, {n.x, n.y, n.z}, {col.x, col.y, col.z}, {emi.x, emi.y, emi.z}});
}
void addBox(std::vector<Vertex>& v, Vec3 c, Vec3 ax, Vec3 ay, Vec3 az, Vec3 col, Vec3 emi) {
    // c = center, ax/ay/az = half-extent vectors (right-handed)
    Vec3 n;
    auto face = [&](Vec3 o, Vec3 u, Vec3 w, Vec3 nn) { addQuad(v, o - u - w, o + u - w, o + u + w, o - u + w, nn, col, emi); };
    face(c + ax, ay, az, normalize(ax));
    face(c - ax, az, ay, normalize(ax * -1));
    face(c + ay, az, ax, normalize(ay));
    face(c - ay, ax, az, normalize(ay * -1));
    face(c + az, ax, ay, normalize(az));
    face(c - az, ay, ax, normalize(az * -1));
    (void)n;
}

void Viewer::buildMeshes() {
    const Solver& s = *sim_;
    std::vector<Vertex> op, glass;
    op.reserve(600000);
    const float dx = s.dx;
    // ground and slab
    {
        float e = 30;
        addQuad(op, Vec3(-e, -e, -0.002f), Vec3(scene_.width + e, -e, -0.002f), Vec3(scene_.width + e, scene_.depth + e, -0.002f),
                Vec3(-e, scene_.depth + e, -0.002f), Vec3(0, 0, 1), Vec3(0.36f, 0.45f, 0.30f), Vec3());
    }
    const float zc = cutZ();
    for (size_t st = 0; st < s.kFloorS.size(); ++st)
    for (int j = 0; j < s.ny; ++j)
        for (int i = 0; i < s.nx; ++i) {
            int kf = s.kFloorS[st];
            float zb = s.oz + kf * dx;
            if (zb >= zc) continue;
            int c = s.cell(i, j, kf);
            float x0 = s.ox + i * dx, y0 = s.oy + j * dx;
            if (x0 < -0.01f || y0 < -0.01f || x0 + dx > scene_.width + 0.11f || y0 + dx > scene_.depth + 0.11f) continue;
            Vec3 col(0.55f, 0.55f, 0.53f), emi;
            float z = zb + (st > 0 ? 0.004f : 0.0f);
            bool covered = s.porous[c] && s.fuelMat[c] >= 0 && !s.blocked[c] && s.fuelMat[c] != scene_.joistMaterial;
            if (st > 0 && !covered) continue;   // upper floors: the subfloor barrier is drawn instead
            if (covered) {
                const Material& m = lib_.materials[s.fuelMat[c]];
                float left = s.fuelMass0[c] > 0 ? s.fuelMass[c] / (s.fuelMass0[c] * std::max(0.01f, 1 - m.charYield)) : 0;
                float k = 0.15f + 0.85f * std::clamp(left, 0.0f, 1.0f);
                col = Vec3(m.color[0], m.color[1], m.color[2]) * k;
                emi = glow(s.fuelT[c]);
                z += 0.012f * std::clamp(left, 0.2f, 1.0f);
            }
            addQuad(op, Vec3(x0, y0, z), Vec3(x0 + dx, y0, z), Vec3(x0 + dx, y0 + dx, z), Vec3(x0, y0 + dx, z), Vec3(0, 0, 1), col, emi);
        }
    // barriers, drawn as the real pieces they stand for: each exposed layer at
    // its true position across the assembly (walls have real thickness), the
    // roof as a true slope, and edges where a neighbouring piece is gone.
    const float RB = scene_.roofBase(), Dd = scene_.depth;
    const float cosT = 1.0f / std::sqrt(1 + scene_.pitch * scene_.pitch);
    const float chordZ = 3.5f * 0.0254f / cosT;
    auto slopeZ = [&](float y) {   // underside of the roof deck at plan position y
        float run = y <= 0.5f * Dd ? y : Dd - y;
        return RB + chordZ + scene_.pitch * run;
    };
    auto faceIJK = [&](const Barrier& b, int& i, int& j, int& k) {
        int f = b.face;
        if (b.axis == 0) { i = f % (s.nx + 1); j = (f / (s.nx + 1)) % s.ny; k = f / ((s.nx + 1) * s.ny); }
        else if (b.axis == 1) { i = f % s.nx; j = (f / s.nx) % (s.ny + 1); k = f / (s.nx * (s.ny + 1)); }
        else { i = f % s.nx; j = (f / s.nx) % s.ny; k = f / (s.nx * s.ny); }
    };
    auto barrierAt = [&](int ax, int i, int j, int k) -> int {
        if (i < 0 || j < 0 || k < 0) return -1;
        if (ax == 0) { if (i > s.nx || j >= s.ny || k >= s.nz) return -1; return s.bx[s.xface(i, j, k)]; }
        if (ax == 1) { if (i >= s.nx || j > s.ny || k >= s.nz) return -1; return s.by[s.yface(i, j, k)]; }
        if (i >= s.nx || j >= s.ny || k > s.nz) return -1;
        return s.bz[s.zface(i, j, k)];
    };
    for (const Barrier& b : s.barriers) {
        if (!barrierVisible(b)) continue;
        int i, j, k;
        faceIJK(b, i, j, k);
        const int ax = b.axis;
        float x = s.ox + i * dx, y = s.oy + j * dx, z = s.oz + k * dx;
        if (z >= zc - 1e-3f) continue;
        const Assembly& as = lib_.assemblies[b.assembly];
        bool centered = b.kind == BarrierKind::Door || b.kind == BarrierKind::Window;
        // layer surfaces measured along +axis from the face plane
        float sgn = b.aIsInside ? 1.0f : -1.0f;   // layer order runs toward +axis when layer 0 faces cellA
        auto surf = [&](int asmSide) {
            int l = asmSide == 0 ? b.front : b.back;
            float off = Scene::layerOffset(lib_, as, l, centered);
            if (asmSide == 1) off += as.layers[l].thickness;
            return sgn * off;
        };
        float sA = surf(b.aIsInside ? 0 : 1), sB = surf(b.aIsInside ? 1 : 0);   // surface facing cellA / cellB
        // styling per side
        Vec3 colSide[2], emiSide[2];
        for (int side = 0; side < 2; ++side) {
            int asmSide = (side == 0) == b.aIsInside ? 0 : 1;
            int layer = asmSide == 0 ? b.front : b.back;
            const Material& m = lib_.materials[as.layers[layer].material];
            float am = lib_.arealMass(as.layers[layer]) * (m.combustible ? 1 - m.charYield : 1);
            float left = am > 0 ? std::clamp(b.mass[layer] / am, 0.0f, 1.0f) : 1;
            float dmg = m.combustible ? 0.0f : std::clamp(b.damage[layer], 0.0f, 1.0f);
            float kk = (0.2f + 0.8f * left) * (1 - 0.5f * dmg);
            Vec3 col = Vec3(m.color[0], m.color[1], m.color[2]) * kk;
            if (as.layers[layer].material == paintMat_) {   // paint takes the color of the room it faces
                int sc = side == 0 ? b.cellA : b.cellB;
                if (sc >= 0) {
                    int ci, cj, ck;
                    s.cellIJK(sc, ci, cj, ck);
                    const Room* rm = scene_.roomAt(s.ox + (ci + 0.5f) * dx, s.oy + (cj + 0.5f) * dx, s.oz + (ck + 0.5f) * dx);
                    if (rm && b.kind != BarrierKind::Ceiling) col = Vec3(rm->paint[0], rm->paint[1], rm->paint[2]) * kk;
                    else col = Vec3(0.93f, 0.93f, 0.91f) * kk;
                }
            }
            colSide[side] = col;
            emiSide[side] = glow(b.T[asmSide]);
        }
        // ---- roof top faces: sloped tiles following the real roof plane
        if (b.kind == BarrierKind::Roof) {
            if (ax != 2) continue;   // risers are hidden under the slope
            float y0 = y, y1 = y + dx;
            float za = slopeZ(y0), zb = slopeZ(y1);
            if ((y0 < 0.5f * Dd) != (y1 < 0.5f * Dd)) za = zb = slopeZ(0.5f * Dd);   // ridge cell
            float tk = 0;
            for (const Layer& l : as.layers) tk += l.thickness;
            Vec3 p00(x, y0, za), p10(x + dx, y0, za), p11(x + dx, y1, zb), p01(x, y1, zb);
            Vec3 nUp = normalize(cross(p10 - p00, p01 - p00));
            Vec3 lift = nUp * tk;
            // outside (shingles) above, deck underside below
            addQuad(op, p00 + lift, p10 + lift, p11 + lift, p01 + lift, nUp, colSide[1], emiSide[1]);
            addQuad(op, p00, p01, p11, p10, nUp * -1, colSide[0], emiSide[0]);
            continue;
        }
        Vec3 n, u, w;
        float hU = dx, hW = dx;
        if (ax == 0) { n = Vec3(1, 0, 0); u = Vec3(0, 1, 0); w = Vec3(0, 0, 1); }
        else if (ax == 1) { n = Vec3(0, 1, 0); u = Vec3(0, 0, 1); w = Vec3(1, 0, 0); }
        else { n = Vec3(0, 0, 1); u = Vec3(1, 0, 0); w = Vec3(0, 1, 0); }
        Vec3 p0(x, y, z);
        if (b.kind == BarrierKind::Gable) {   // trim the gable to the roof line
            float top = slopeZ(y + 0.5f * dx) - z;
            if (top <= 0) continue;
            hW = std::min(dx, top);   // w is +z for x-faces
        }
        Vec3 U = u * hU, W = w * hW;
        if (b.kind == BarrierKind::Window) {
            float mid = 0.5f * (sA + sB);
            Vec3 q = p0 + n * mid;
            addQuad(glass, q, q + U, q + U + W, q + W, n, Vec3(0.55f, 0.72f, 0.85f), emiSide[0]);
            continue;
        }
        Vec3 qa = p0 + n * sA, qb = p0 + n * sB;
        addQuad(op, qb, qb + U, qb + U + W, qb + W, n, colSide[1], emiSide[1]);          // faces cellB
        addQuad(op, qa, qa + W, qa + U + W, qa + U, n * -1, colSide[0], emiSide[0]);     // faces cellA
        // edges where the neighbouring piece in this plane is missing (holes, openings, burn-through)
        if (std::fabs(sB - sA) > 0.004f) {
            Vec3 ecol = colSide[0] * 0.8f;
            int di[4][3];
            // neighbours along u and w in grid index space
            int ui = ax == 0 ? 1 : (ax == 1 ? 2 : 0), wi = ax == 0 ? 2 : (ax == 1 ? 0 : 1);
            int idx[3] = {i, j, k};
            for (int e = 0; e < 4; ++e) {
                for (int q = 0; q < 3; ++q) di[e][q] = idx[q];
                if (e == 0) di[e][ui] -= 1;
                if (e == 1) di[e][ui] += 1;
                if (e == 2) di[e][wi] -= 1;
                if (e == 3) di[e][wi] += 1;
                int nb = barrierAt(ax, di[e][0], di[e][1], di[e][2]);
                if (nb >= 0 && barrierVisible(s.barriers[nb])) continue;
                if (b.kind == BarrierKind::Gable && e == 3) continue;
                Vec3 o0 = p0 + (e == 1 ? U : Vec3()) + (e == 3 ? W : Vec3());
                Vec3 along = (e < 2) ? W : U;
                Vec3 en = (e == 0) ? u * -1 : (e == 1) ? u : (e == 2) ? w * -1 : w;
                Vec3 a0 = o0 + n * sA, a1 = o0 + n * sB;
                addQuad(op, a0, a1, a1 + along, a0 + along, en, ecol, Vec3());
                addQuad(op, a0, a0 + along, a1 + along, a1, en * -1, ecol, Vec3());
            }
        }
    }
    // solid cells (contents)
    const int off[6] = {-1, 1, -s.nx, s.nx, -s.nx * s.ny, s.nx * s.ny};
    for (int c : s.solidCells) {
        if (!s.blocked[c]) continue;
        int i, j, k;
        s.cellIJK(c, i, j, k);
        const Material& m = lib_.materials[s.fuelMat[c]];
        float left = 1;
        if (m.combustible && s.fuelMass0[c] > 0) left = std::clamp(s.fuelMass[c] / (s.fuelMass0[c] * (1 - m.charYield)), 0.0f, 1.0f);
        Vec3 col = Vec3(m.color[0], m.color[1], m.color[2]) * (0.12f + 0.88f * left);
        Vec3 e = glow(s.fuelT[c]);
        float x0 = s.ox + i * dx, y0 = s.oy + j * dx, z0 = s.oz + k * dx;
        if (z0 >= zc) continue;
        Vec3 P[8];
        for (int q = 0; q < 8; ++q) P[q] = Vec3(x0 + ((q & 1) ? dx : 0), y0 + ((q & 2) ? dx : 0), z0 + ((q & 4) ? dx : 0));
        auto exposed = [&](int d) {
            if ((d == 0 && i == 0) || (d == 1 && i == s.nx - 1) || (d == 2 && j == 0) || (d == 3 && j == s.ny - 1) ||
                (d == 4 && k == 0) || (d == 5 && k == s.nz - 1)) return d != 4;
            return !s.blocked[c + off[d]];
        };
        if (exposed(0)) addQuad(op, P[0], P[4], P[6], P[2], Vec3(-1, 0, 0), col, e);
        if (exposed(1)) addQuad(op, P[1], P[3], P[7], P[5], Vec3(1, 0, 0), col, e);
        if (exposed(2)) addQuad(op, P[0], P[1], P[5], P[4], Vec3(0, -1, 0), col, e);
        if (exposed(3)) addQuad(op, P[2], P[6], P[7], P[3], Vec3(0, 1, 0), col, e);
        if (exposed(4)) addQuad(op, P[0], P[2], P[3], P[1], Vec3(0, 0, -1), col, e);
        if (exposed(5)) addQuad(op, P[4], P[5], P[7], P[6], Vec3(0, 0, 1), col, e);
    }
    // framing members at their exact size; trusses take their state from the
    // attic cell they pass through
    bool showTruss = hideRoof_ || framing_;
    for (const Member& mb : scene_.members) {
        bool truss = mb.kind == MemberKind::TrussChord || mb.kind == MemberKind::TrussWeb || mb.kind == MemberKind::Gusset;
        if (truss && !showTruss) continue;
        if (!truss && mb.kind != MemberKind::Tread && !framing_) continue;
        if (std::min(mb.p0[2], mb.p1[2]) >= zc) continue;
        Vec3 a(mb.p0[0], mb.p0[1], mb.p0[2]), b(mb.p1[0], mb.p1[1], mb.p1[2]);
        Vec3 dir = b - a;
        float L = length(dir);
        if (L < 1e-5f) continue;
        Vec3 dn = dir * (1.0f / L);
        Vec3 side = normalize(Vec3(mb.side[0], mb.side[1], mb.side[2]));
        Vec3 t = normalize(cross(dn, side));
        const Material& m = lib_.materials[mb.material];
        int segs = (mb.kind == MemberKind::TrussChord || mb.kind == MemberKind::TrussWeb) ? std::max(1, (int)(L / 0.2f)) : 1;
        for (int q = 0; q < segs; ++q) {
            Vec3 c = a + dir * ((q + 0.5f) / segs);
            Vec3 col(m.color[0], m.color[1], m.color[2]), e;
            if (mb.kind == MemberKind::TrussChord || mb.kind == MemberKind::TrussWeb) {
                int cc = s.cellAt(c.x, c.y, c.z);
                int i, j, k;
                if (cc >= 0) {
                    s.cellIJK(cc, i, j, k);
                    k = std::max(k, s.kCeil);
                    cc = s.cell(i, j, std::min(k, s.nz - 1));
                    // find the attic cell that holds this lumber (discretizer clamps downward)
                    while (cc >= 0 && s.fuelMat[cc] < 0 && k > s.kCeil) { --k; cc = s.cell(i, j, k); }
                }
                if (cc >= 0 && s.fuelMat[cc] >= 0 && s.fuelMass0[cc] > 0) {
                    float left = s.fuelMass[cc] / (s.fuelMass0[cc] * (1 - m.charYield));
                    if (left < 0.05f) continue;
                    col = col * (0.12f + 0.88f * std::clamp(left, 0.0f, 1.0f));
                    e = glow(s.fuelT[cc]);
                }
            }
            addBox(op, c, dn * (0.5f * L / segs), side * (0.5f * mb.width), t * (0.5f * mb.depth), col, e);
        }
    }
    ren_.setOpaque(op);
    ren_.setGlass(glass);
    lastMesh_ = plat_.now();
    meshDirty_ = false;
}

void Viewer::uploadVolume() {
    const Solver& s = *sim_;
    vol_.resize((size_t)s.N * 4);
    for (int c = 0; c < s.N; ++c) {
        float* v = &vol_[(size_t)c * 4];
        if (s.blocked[c]) { v[0] = v[1] = v[2] = v[3] = 0; continue; }
        float rho = s.gasDensity(c);
        v[0] = s.Ys[c] * rho;
        v[1] = s.Ya[c] * rho;
        v[2] = s.T[c];
        v[3] = s.hrr[c];
    }
    // Fire lights for the surface shader (display only): strongest burning
    // cells, at least 1.6 m apart, each carrying the HRR of the cells nearest it.
    std::vector<std::pair<float, int>> burning;
    for (int c = 0; c < s.N; ++c)
        if (s.hrr[c] > 1e5f) burning.push_back({s.hrr[c], c});
    std::sort(burning.begin(), burning.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<Vec3> lp;
    auto center = [&](int c) { int i, j, k; s.cellIJK(c, i, j, k); return Vec3(s.ox + (i + 0.5f) * s.dx, s.oy + (j + 0.5f) * s.dx, s.oz + (k + 0.5f) * s.dx); };
    for (auto& b : burning) {
        if (lp.size() >= 8) break;
        Vec3 p = center(b.second);
        bool far = true;
        for (const Vec3& q : lp) if (length(p - q) < 1.6f) { far = false; break; }
        if (far) lp.push_back(p);
    }
    std::vector<float> power(lp.size(), 0.0f);
    const float V = s.dx * s.dx * s.dx;
    for (auto& b : burning) {
        Vec3 p = center(b.second);
        int best = 0;
        float bd = 1e9f;
        for (size_t q = 0; q < lp.size(); ++q) { float d = length(p - lp[q]); if (d < bd) { bd = d; best = (int)q; } }
        if (!lp.empty()) power[best] += b.first * V;
    }
    ropt_.fireLights.clear();
    float tnow = (float)plat_.now();
    for (size_t q = 0; q < lp.size(); ++q) {
        float kw = power[q] / 1000.0f;
        float flicker = 0.85f + 0.1f * std::sin(tnow * 13.0f + q * 2.3f) + 0.05f * std::sin(tnow * 29.0f + q);
        ropt_.fireLights.push_back({lp[q].x, lp[q].y, lp[q].z, std::clamp(std::sqrt(kw) * 0.16f, 0.0f, 5.0f) * flicker});
    }
    ren_.setVolume(s.nx, s.ny, s.nz, vol_.data(), Vec3(s.ox, s.oy, s.oz),
                   Vec3(s.ox + s.nx * s.dx, s.oy + s.ny * s.dx, s.oz + s.nz * s.dx));
}

// ---------------------------------------------------------------- overlay --
void Viewer::drawOverlay() {
    const Solver& s = *sim_;
    float lh = (float)ren_.glyphH() + 2, x = 12, y = 10;
    int W = plat_.width();
    ren_.rect(0, 0, (float)W, lh * 3 + 14, 0, 0, 0, 0.55f);
    ren_.text(x, y, "PHOTON  Phase 1 prototype", 1, 0.85f, 0.4f);
    ren_.text(x + 27 * ren_.glyphW(), y, "PROTOTYPE PHYSICS - NOT VALIDATED - shape roughly right, numbers wrong. Training/education only.", 1, 0.45f, 0.35f);
    y += lh;
    std::string spd = SPEEDS[speedIdx_.load()] > 1e8f ? "max" : fmt("x%g", SPEEDS[speedIdx_.load()]);
    std::string line = "t " + mmss(s.time) + "   " + (running_ ? "running [P]" : "PAUSED [P]") + "  speed " + spd;
    if (running_ && SPEEDS[speedIdx_.load()] > 1e8f) line += fmt(" (%.2fx realtime)", simRate_.load());
    else if (running_ && simRate_ < 0.95f) line += fmt(" (solver keeping %.2f of requested speed)", simRate_.load());
    line += "   HRR " + fmt("%.0f kW", s.stats.hrr / 1000) + "   max gas " + fmt("%.0f C", s.stats.maxGasT - 273.15) +
            "   burning cells " + std::to_string(s.stats.burningCells) + "   step " + fmt("%.1f ms", s.stats.stepMs) +
            "   grid " + std::to_string(s.nx) + "x" + std::to_string(s.ny) + "x" + std::to_string(s.nz) + " @ 20 cm";
    ren_.text(x, y, line);
    y += lh;
    std::string tog = std::string("Source [1-3]: ") + SOURCES[source_].name + "   roof " + (hideRoof_ ? "hidden" : "shown") +
                      " [R]  ceiling " + (hideCeiling_ ? "hidden" : "shown") + " [Z]  framing " + (framing_ ? "on" : "off") +
                      " [F]  smoke " + (ropt_.smoke ? "on" : "off") + " [X]  fire " + (ropt_.fire ? "on" : "off") + " [V]  temp slice " +
                      (ropt_.slice ? fmt("%.1f m", ropt_.sliceZ) : std::string("off")) + " [T, PgUp/PgDn]";
    ren_.text(x, y, tog, 0.8f, 0.9f, 1.0f);
    // hover line
    if (!hover_.empty()) {
        size_t nl = hover_.find('\n');
        int lines = nl == std::string::npos ? 1 : 2;
        float hy = (float)plat_.height() - lh * lines - 8;
        ren_.rect(0, hy - 4, (float)W, (float)plat_.height(), 0, 0, 0, 0.55f);
        ren_.text(x, hy, hover_.substr(0, nl), 0.95f, 0.95f, 0.8f);
        if (lines == 2) ren_.text(x, hy + lh, hover_.substr(nl + 1), 0.75f, 0.9f, 1.0f);
    }
    // event log
    int nev = (int)s.events.size();
    int first = std::max(0, nev - 10);
    float ey = lh * 3 + 24;
    if (nev > 0) {
        float w = 64.0f * ren_.glyphW();
        ren_.rect((float)W - w - 16, ey - 6, (float)W - 4, ey + (nev - first) * lh + 4, 0, 0, 0, 0.5f);
        for (int e = first; e < nev; ++e, ey += lh)
            ren_.text((float)W - w - 8, ey, mmss(s.events[e].time) + "  " + s.events[e].text, 1, 0.9f, 0.75f);
    }
    if (help_ && !showScene_) {
        const char* lines[] = {
            "Left-click a surface        start a fire with the selected source (solver decides if it ignites)",
            "Right-click a door/window   open or close a door, break a window",
            "WASD move  Space/C up/down (or E/Q)  Shift fast  Ctrl slow   hold mouse + drag  look   wheel  dolly   arrows  turn",
            "P  pause   +/-  speed   U upper story   R roof  Z ceiling  F framing  X smoke  V fire  T slice   Backspace reset   H help",
            "Doors and windows start as in the scene file. Closed house fires can starve for oxygen.",
        };
        float hy = lh * 3 + 24;
        ren_.rect(8, hy - 6, 8 + 98.0f * ren_.glyphW(), hy + 5 * lh + 4, 0, 0, 0, 0.5f);
        for (const char* l : lines) { ren_.text(16, hy, l, 0.9f, 0.9f, 0.9f); hy += lh; }
    }
    if (ropt_.slice) {
        float lx = 16, ly = (float)plat_.height() - lh * 3 - 16;
        ren_.text(lx, ly - lh, "gas temperature slice", 0.9f, 0.9f, 0.9f);
        const float stops[5][3] = {{0.10f, 0.15f, 0.55f}, {0.10f, 0.65f, 0.85f}, {0.95f, 0.90f, 0.20f}, {0.95f, 0.35f, 0.05f}, {0.85f, 0.05f, 0.05f}};
        for (int q = 0; q < 4; ++q)
            ren_.rect(lx + q * 60, ly, lx + q * 60 + 60, ly + 12, stops[q][0], stops[q][1], stops[q][2], 1);
        ren_.rect(lx + 240, ly, lx + 300, ly + 12, stops[4][0], stops[4][1], stops[4][2], 1);
        ren_.text(lx, ly + 14, "20 C", 0.9f, 0.9f, 0.9f);
        ren_.text(lx + 260, ly + 14, "827 C", 0.9f, 0.9f, 0.9f);
    }
}

void Viewer::drawPanels() {
    Solver& s = *sim_;
    float top = ui_.lineHeight() * 3 + 22;
    float H = (float)plat_.height();
    if (in_.keyPressed['G']) showScene_ = !showScene_;
    // ---- left: scene panel
    if (showScene_) {
        ui_.panel(8, top, 330, H - top - 70, "Scene  [G hides]", &sceneScroll_);
        ui_.heading("Click action");
        if (ui_.button("Start fire with selected source", clickMode_ == 0)) clickMode_ = 0;
        if (ui_.button("Inspect object (or Ctrl+click)", clickMode_ == 1)) clickMode_ = 1;
        ui_.heading("Ignition source");
        for (int i = 0; i < 3; ++i)
            if (ui_.button(SOURCES[i].name, source_ == i)) source_ = i;
        ui_.heading("Simulation");
        if (ui_.button(running_ ? "Pause  [P]" : "Run  [P]", !running_)) running_ = !running_.load();
        std::string spd = SPEEDS[speedIdx_.load()] > 1e8f ? "max" : fmt("x%g", SPEEDS[speedIdx_.load()]);
        if (ui_.button("Faster  (now " + spd + ")  [+]")) speedIdx_ = std::min(speedIdx_.load() + 1, NSPEED - 1);
        if (ui_.button("Slower  [-]")) speedIdx_ = std::max(speedIdx_.load() - 1, 0);
        if (ui_.button("Reset house  [Backspace]")) { resetSim(); haveSel_ = false; return; }
        ui_.heading("View");
        if (ui_.checkbox("Hide roof  [R]", hideRoof_)) meshDirty_ = true;
        if (ui_.checkbox("Hide ceilings  [Z]", hideCeiling_)) meshDirty_ = true;
        if (ui_.checkbox("Hide upper story  [U]", upperCut_)) meshDirty_ = true;
        if (ui_.checkbox("Framing view  [F]", framing_)) meshDirty_ = true;
        ui_.checkbox("Smoke  [X]", ropt_.smoke);
        ui_.checkbox("Fire  [V]", ropt_.fire);
        ui_.checkbox("Temperature slice  [T]", ropt_.slice);
        ui_.heading("Doors and windows");
        for (size_t o = 0; o < scene_.openings.size(); ++o) {
            const Opening& op = scene_.openings[o];
            if (op.kind == OpeningKind::Cased) continue;
            bool open = false, any = false;
            for (const Barrier& b : s.barriers)
                if (b.opening == (int)o) { any = true; open = open || b.userOpen || b.failed; }
            if (!any) continue;
            std::string st = op.kind == OpeningKind::Door ? (open ? "open" : "closed") : (open ? "broken/out" : "intact");
            if (ui_.button(op.name + "  (" + st + ")", open, 6)) { s.toggleOpening((int)o); meshDirty_ = true; }
        }
        ui_.heading("Rooms (click to fly there)");
        for (int st = 0; st < scene_.stories; ++st) {
            ui_.label(st == 0 ? "First floor" : st == 1 ? "Second floor" : "Story " + std::to_string(st + 1), 0.7f, 0.7f, 0.7f);
            for (const Room& r : scene_.rooms) {
                if (r.story != st) continue;
                if (ui_.button(r.name, false, 6)) {
                    float zb = scene_.storyBase(st);
                    Vec3 c(0.5f * (r.x0 + r.x1), 0.5f * (r.y0 + r.y1), zb + 1.0f);
                    lookFrom(Vec3(r.x0 + 0.4f, r.y0 + 0.4f, zb + 1.7f), c);
                    if (st == 0 && scene_.stories > 1) { upperCut_ = false; }
                    meshDirty_ = true;
                }
            }
        }
        ui_.space(20);
    }
    // ---- right: inspector
    if (haveSel_) {
        float w = 430, x = (float)plat_.width() - w - 8, y0 = H - 360 - 70;
        ui_.panel(x, y0, w, 360, "Inspector  (Ctrl+click to select)");
        if (selObj_ >= 0 && selObj_ < (int)scene_.objects.size()) {
            const SceneObject& o = scene_.objects[selObj_];
            const Material& m = lib_.materials[o.material];
            auto in = [](float mm) { return fmt("%.2f", mm / 0.0254f); };
            ui_.label("#" + std::to_string(o.id) + "  " + o.name, 1.0f, 0.95f, 0.7f);
            ui_.label("Kind: " + o.kind);
            ui_.label("Size: " + in(2 * o.hu) + " x " + in(2 * o.hv) + " x " + in(2 * o.hn) + " in");
            ui_.label("Center: " + fmt("%.2f", o.c[0]) + ", " + fmt("%.2f", o.c[1]) + ", " + fmt("%.2f m", o.c[2]));
            ui_.heading("Material: " + m.id);
            ui_.label(m.name);
            ui_.label("density " + fmt("%.0f kg/m3", m.density) + "   k " + fmt("%.3f W/m K", m.conductivity));
            ui_.label("c_p " + fmt("%.0f J/kg K", m.specificHeat) + "   emissivity " + fmt("%.2f", m.emissivity));
            if (m.combustible) {
                ui_.label("off-gas from " + fmt("%.0f C", m.pyrolysisTemp - 273.15) + "   dHc " + fmt("%.1f MJ/kg", m.heatOfCombustion / 1e6));
                ui_.label("soot yield " + fmt("%.3f", m.sootYield) + "   char " + fmt("%.2f", m.charYield));
            } else {
                ui_.label(m.failTemp > 0 ? "non-combustible, fails near " + fmt("%.0f C", m.failTemp - 273.15) : "non-combustible");
            }
            std::string src = m.source;
            while (!src.empty()) {   // wrap the citation
                size_t n = std::min<size_t>(src.size(), 44);
                ui_.label(src.substr(0, n), 0.6f, 0.7f, 0.8f);
                src.erase(0, n);
            }
        }
        ui_.heading("Live state");
        Hit h;
        h.kind = sel_.kind; h.cell = sel_.cell; h.from = sel_.from; h.barrier = sel_.barrier; h.pos = sel_.pos;
        std::string d = describe(h);
        d = d.substr(0, d.find('\n'));
        while (!d.empty()) {
            size_t n = std::min<size_t>(d.size(), 44);
            ui_.label(d.substr(0, n));
            d.erase(0, n);
        }
        if (ui_.button("Close")) haveSel_ = false;
    }
}

void Viewer::run() {
    double last = plat_.now();
    double lastHover = 0;
    float lastSimTime = -1;
    simThread_ = std::thread([this] { simLoop(); });
    while (plat_.pump(in_)) {
        if (in_.keyPressed[KEY_ESCAPE]) break;
        double now = plat_.now();
        float frameDt = (float)(now - last);
        last = now;
        bool done = false;
        uiWants_ = true;
        {
            std::lock_guard<std::mutex> lk(simMtx_);
            uiWants_ = false;
            if (!sim_) break;
            updateCamera();
            ui_.begin(&ren_, &in_);
            drawPanels();
            handleInput(frameDt);
            if (!sim_) break;
            updateCamera();
            if (sim_->time != lastSimTime && now - lastMesh_ > 0.5) meshDirty_ = true;
            lastSimTime = sim_->time;
            if (meshDirty_) buildMeshes();
            uploadVolume();
            ropt_.time = (float)now;
            ropt_.clipZ = std::min(hideCeiling_ ? sim_->oz + sim_->kCeil * sim_->dx : 1e9f, cutZ());
            if (now - lastHover > 0.1) {
                hover_ = ui_.mouseOverUi() ? std::string() : describe(pick(in_.mouseX, in_.mouseY));
                lastHover = now;
            }
            drawOverlay();
            if (!demo_.empty() && shotIdx_ < shots_.size() && sim_->time >= shots_[shotIdx_]) done = true;
        }
        ren_.render(plat_.width(), plat_.height(), vp_, eye_, ropt_);
        if (in_.keyPressed[KEY_F12])
            ren_.saveScreenshot(exeDir() + "photon_shot_" + std::to_string(shotCount_++) + ".bmp", plat_.width(), plat_.height());
        if (!still_.empty()) {
            ++stillFrame_;
            struct View { Vec3 eye, at; bool roofHidden; } views[3] = {
                {Vec3(-9, -12, 11), Vec3(7, 5, 3), false},
                {Vec3(7.6f, -6, 16), Vec3(7.6f, 4.6f, 2), true},
                {Vec3(1.5f, 1.5f, 1.6f), Vec3(5, 4.5f, 1.2f), true}};
            int vi = stillFrame_ / 20;
            if (vi >= 3) break;
            if (stillFrame_ % 20 == 1) { lookFrom(views[vi].eye, views[vi].at); hideRoof_ = views[vi].roofHidden; upperCut_ = vi == 1; meshDirty_ = true; }
            if (stillFrame_ % 20 == 19) ren_.saveScreenshot(still_ + "_" + std::to_string(vi) + ".bmp", plat_.width(), plat_.height());
        }
        if (done) {
            ren_.saveScreenshot(demo_ + "_" + std::to_string((int)shots_[shotIdx_]) + "s.bmp", plat_.width(), plat_.height());
            if (++shotIdx_ >= shots_.size()) break;
        }
        plat_.swap();
    }
    quit_ = true;
    if (simThread_.joinable()) simThread_.join();
}
} // namespace

int runViewer(const std::string& scenePath, const std::string& demo) {
    Viewer v;
    std::string err;
    if (!v.init(scenePath, demo, err)) {
        showMessage("Photon", err);
        return 1;
    }
    v.run();
    return 0;
}
