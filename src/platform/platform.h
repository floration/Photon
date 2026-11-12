// Photon - thin platform layer: one window with an OpenGL 3.3 core context,
// mouse/keyboard input, a high-resolution clock and a font rasterizer.
// Win32 implementation now; X11 follows in Phase 2 behind the same interface.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace photon {

enum Key : int {
    KEY_ESCAPE = 256, KEY_SPACE, KEY_PAGEUP, KEY_PAGEDOWN, KEY_BACKSPACE, KEY_F1, KEY_PLUS, KEY_MINUS,
    KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_COUNT
};

struct Input {
    int mouseX = 0, mouseY = 0;      // pixels, origin top-left
    int dMouseX = 0, dMouseY = 0;
    float wheel = 0;                 // notches this frame
    bool down[3] = {};               // left, right, middle
    bool pressed[3] = {}, released[3] = {};
    bool shift = false, ctrl = false;
    bool keyPressed[KEY_COUNT] = {}; // ASCII upper-case letters and digits, plus Key values
};

struct FontAtlas {
    int cellW = 0, cellH = 0, cols = 16, rows = 6;   // glyphs 32..127
    int width = 0, height = 0;
    std::vector<uint8_t> alpha;                      // width * height
};

class Platform {
public:
    bool createWindow(const char* title, int width, int height, std::string& err);
    bool pump(Input& in);            // false when the window was closed
    void swap();
    void setTitle(const std::string& t);
    int width() const { return w_; }
    int height() const { return h_; }
    double now() const;              // seconds
    bool makeFont(int pixelHeight, FontAtlas& out);
    void destroy();

private:
    void* hwnd_ = nullptr;
    void* hdc_ = nullptr;
    void* hglrc_ = nullptr;
    int w_ = 0, h_ = 0;
};

// Shows a blocking message box (errors before / outside the GL window).
void showMessage(const char* title, const std::string& text);
// Directory containing the running executable, with trailing separator.
std::string exeDir();

// Loads an OpenGL entry point (wglGetProcAddress / glXGetProcAddress).
void* glProc(const char* name);

} // namespace photon
