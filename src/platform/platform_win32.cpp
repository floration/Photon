// Photon - Win32 platform layer (window, WGL 3.3 core context, input, fonts).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include "platform/platform.h"
#include <cstring>

namespace photon {

namespace {
Input* g_in = nullptr;
bool g_closed = false;
int g_w = 0, g_h = 0;

typedef HGLRC(WINAPI* PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int*);
typedef BOOL(WINAPI* PFN_wglSwapIntervalEXT)(int);
constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB = 0x2091;
constexpr int WGL_CONTEXT_MINOR_VERSION_ARB = 0x2092;
constexpr int WGL_CONTEXT_PROFILE_MASK_ARB = 0x9126;
constexpr int WGL_CONTEXT_CORE_PROFILE_BIT_ARB = 0x00000001;

bool g_down[KEY_COUNT] = {};

int keyIndex(WPARAM vk) {
    int k = -1;
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) k = (int)vk;
    else switch (vk) {
        case VK_ESCAPE: k = KEY_ESCAPE; break;
        case VK_SPACE: k = KEY_SPACE; break;
        case VK_PRIOR: k = KEY_PAGEUP; break;
        case VK_NEXT: k = KEY_PAGEDOWN; break;
        case VK_BACK: k = KEY_BACKSPACE; break;
        case VK_F1: k = KEY_F1; break;
        case VK_OEM_PLUS: case VK_ADD: k = KEY_PLUS; break;
        case VK_OEM_MINUS: case VK_SUBTRACT: k = KEY_MINUS; break;
        case VK_LEFT: k = KEY_LEFT; break;
        case VK_RIGHT: k = KEY_RIGHT; break;
        case VK_UP: k = KEY_UP; break;
        case VK_DOWN: k = KEY_DOWN; break;
        case VK_F2: k = KEY_F2; break;
        case VK_F3: k = KEY_F3; break;
        case VK_F4: k = KEY_F4; break;
        case VK_F5: k = KEY_F5; break;
        case VK_F6: k = KEY_F6; break;
        case VK_F12: k = KEY_F12; break;
        case VK_PAUSE: k = KEY_PAUSE; break;
        case VK_SHIFT: k = KEY_SHIFT; break;
        case VK_CONTROL: k = KEY_CTRL; break;
        default: break;
    }
    return k;
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE: g_closed = true; return 0;
    case WM_SIZE: g_w = LOWORD(lp); g_h = HIWORD(lp); return 0;
    case WM_KEYDOWN: case WM_SYSKEYDOWN: {
        int k = keyIndex(wp);
        if (k >= 0) {
            if (!(lp & (1 << 30)) && g_in) g_in->keyPressed[k] = true;
            g_down[k] = true;
        }
        return 0;
    }
    case WM_KEYUP: case WM_SYSKEYUP: {
        int k = keyIndex(wp);
        if (k >= 0) g_down[k] = false;
        return 0;
    }
    case WM_KILLFOCUS:
        for (bool& d : g_down) d = false;
        return 0;
    case WM_MOUSEWHEEL:
        if (g_in) g_in->wheel += GET_WHEEL_DELTA_WPARAM(wp) / 120.0f;
        return 0;
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: {
        int b = msg == WM_LBUTTONDOWN ? 0 : msg == WM_RBUTTONDOWN ? 1 : 2;
        if (g_in) { g_in->down[b] = true; g_in->pressed[b] = true; }
        SetCapture(h);
        return 0;
    }
    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: {
        int b = msg == WM_LBUTTONUP ? 0 : msg == WM_RBUTTONUP ? 1 : 2;
        if (g_in) { g_in->down[b] = false; g_in->released[b] = true; }
        if (!(GetKeyState(VK_LBUTTON) & 0x8000) && !(GetKeyState(VK_RBUTTON) & 0x8000) &&
            !(GetKeyState(VK_MBUTTON) & 0x8000))
            ReleaseCapture();
        return 0;
    }
    default: break;
    }
    return DefWindowProcA(h, msg, wp, lp);
}
} // namespace

void showMessage(const char* title, const std::string& text) { MessageBoxA(nullptr, text.c_str(), title, MB_OK | MB_ICONERROR); }

std::string exeDir() {
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string s(buf, n);
    size_t p = s.find_last_of("\\/");
    return p == std::string::npos ? std::string() : s.substr(0, p + 1);
}

void* glProc(const char* name) {
    void* p = (void*)wglGetProcAddress(name);
    if (!p || p == (void*)1 || p == (void*)2 || p == (void*)3 || p == (void*)-1) {
        static HMODULE gl = LoadLibraryA("opengl32.dll");
        p = (void*)GetProcAddress(gl, name);
    }
    return p;
}

bool Platform::createWindow(const char* title, int width, int height, std::string& err) {
    SetProcessDPIAware();
    HINSTANCE inst = GetModuleHandleA(nullptr);
    WNDCLASSA wc = {};
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "PhotonWindow";
    RegisterClassA(&wc);
    RECT r = {0, 0, width, height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND h = CreateWindowExA(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT,
                             r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    if (!h) { err = "CreateWindow failed"; return false; }
    HDC dc = GetDC(h);
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    int pf = ChoosePixelFormat(dc, &pfd);
    if (!pf || !SetPixelFormat(dc, pf, &pfd)) { err = "SetPixelFormat failed"; return false; }
    HGLRC legacy = wglCreateContext(dc);
    if (!legacy || !wglMakeCurrent(dc, legacy)) { err = "wglCreateContext failed"; return false; }
    auto createAttribs = (PFN_wglCreateContextAttribsARB)wglGetProcAddress("wglCreateContextAttribsARB");
    if (!createAttribs) { err = "wglCreateContextAttribsARB not available (OpenGL 3.3 required)"; return false; }
    const int attribs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 3,
                           WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0};
    HGLRC core = createAttribs(dc, nullptr, attribs);
    if (!core) { err = "could not create an OpenGL 3.3 core context"; return false; }
    wglMakeCurrent(dc, core);
    wglDeleteContext(legacy);
    if (auto swapInterval = (PFN_wglSwapIntervalEXT)wglGetProcAddress("wglSwapIntervalEXT")) swapInterval(1);
    hwnd_ = h;
    hdc_ = dc;
    hglrc_ = core;
    RECT cr;
    GetClientRect(h, &cr);
    g_w = w_ = cr.right;
    g_h = h_ = cr.bottom;
    return true;
}

bool Platform::pump(Input& in) {
    in.dMouseX = in.dMouseY = 0;
    in.wheel = 0;
    for (int i = 0; i < 3; ++i) in.pressed[i] = in.released[i] = false;
    std::memset(in.keyPressed, 0, sizeof(in.keyPressed));
    g_in = &in;
    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    g_in = nullptr;
    POINT pt;
    GetCursorPos(&pt);
    ScreenToClient((HWND)hwnd_, &pt);
    in.dMouseX = pt.x - in.mouseX;
    in.dMouseY = pt.y - in.mouseY;
    in.mouseX = pt.x;
    in.mouseY = pt.y;
    in.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    in.ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    std::memcpy(in.keyDown, g_down, sizeof(g_down));
    w_ = g_w;
    h_ = g_h;
    return !g_closed;
}

void Platform::swap() { SwapBuffers((HDC)hdc_); }

void Platform::setTitle(const std::string& t) { SetWindowTextA((HWND)hwnd_, t.c_str()); }

double Platform::now() const {
    static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

// Rasterizes ASCII 32..127 of a monospace system font into an alpha atlas
// using GDI (an operating system interface, not a third-party library).
bool Platform::makeFont(int px, FontAtlas& out) {
    HDC dc = CreateCompatibleDC(nullptr);
    HFONT font = CreateFontA(-px, 0, 0, 0, FW_NORMAL, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             ANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
    SelectObject(dc, font);
    TEXTMETRICA tm;
    GetTextMetricsA(dc, &tm);
    out.cellW = tm.tmAveCharWidth;
    out.cellH = tm.tmHeight;
    out.width = out.cellW * out.cols;
    out.height = out.cellH * out.rows;
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = out.width;
    bi.bmiHeader.biHeight = -out.height;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp) { DeleteObject(font); DeleteDC(dc); return false; }
    SelectObject(dc, bmp);
    std::memset(bits, 0, (size_t)out.width * out.height * 4);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    for (int ch = 32; ch < 128; ++ch) {
        int g = ch - 32;
        char s = (char)ch;
        TextOutA(dc, (g % out.cols) * out.cellW, (g / out.cols) * out.cellH, &s, 1);
    }
    GdiFlush();
    out.alpha.resize((size_t)out.width * out.height);
    const uint8_t* p = (const uint8_t*)bits;
    for (size_t i = 0; i < out.alpha.size(); ++i) out.alpha[i] = p[i * 4 + 1];
    DeleteObject(bmp);
    DeleteObject(font);
    DeleteDC(dc);
    return true;
}

void Platform::destroy() {
    if (hglrc_) { wglMakeCurrent(nullptr, nullptr); wglDeleteContext((HGLRC)hglrc_); }
    if (hwnd_) DestroyWindow((HWND)hwnd_);
    hwnd_ = hdc_ = hglrc_ = nullptr;
}

} // namespace photon
