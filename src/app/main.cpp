// Photon - viewer entry point.
//   photon [scene]                 interactive viewer
//   photon [scene] --demo <prefix> scripted run: sofa fire, screenshots, exit
#include <string>

int runViewer(const std::string& scenePath, const std::string& demo);

int main(int argc, char** argv) {
    std::string scene = "scenes/model_home.txt", demo;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--demo" && i + 1 < argc) demo = argv[++i];
        else if (a == "--shot" && i + 1 < argc) demo = std::string("shot:") + argv[++i];
        else if (a == "--test-fire" && i + 1 < argc) demo = std::string("test:") + argv[++i];
        else scene = a;
    }
    return runViewer(scene, demo);
}