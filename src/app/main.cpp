// Photon - viewer entry point.
//   photon [scene]                 interactive viewer
//   photon [scene] --demo <prefix> scripted run: sofa fire, screenshots, exit
//   photon [scene] --test-fire <prefix> [--ignite "<item>" <kW> <s>] [--no-events]
//          [--times t1,t2,..] [--cam ex,ey,ez,ax,ay,az]
//          scripted fire with captures at the given times (5 views each)
#include <string>

int runViewer(const std::string& scenePath, const std::string& demo);

int main(int argc, char** argv) {
    std::string scene = "scenes/model_home.txt", demo;
    std::string item, kw, dur, events, times, cam;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--ignite" && i + 3 < argc) { item = argv[++i]; kw = argv[++i]; dur = argv[++i]; continue; }
        if (a == "--no-events") { events = "noevents"; continue; }
        if (a == "--times" && i + 1 < argc) { times = argv[++i]; continue; }
        if (a == "--cam" && i + 1 < argc) { cam = argv[++i]; continue; }
        if (a == "--demo" && i + 1 < argc) demo = argv[++i];
        else if (a == "--shot" && i + 1 < argc) demo = std::string("shot:") + argv[++i];
        else if (a == "--test-fire" && i + 1 < argc) demo = std::string("test:") + argv[++i];
        else scene = a;
    }
    if (demo.rfind("test:", 0) == 0) demo += "|" + item + "|" + kw + "|" + dur + "|" + events + "|" + times + "|" + cam;
    return runViewer(scene, demo);
}