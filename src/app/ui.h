// Photon - minimal immediate-mode UI drawn with the renderer's rects and text.
// Panels lay widgets out top to bottom; a widget returns true when clicked.
#pragma once
#include "platform/platform.h"
#include "render/renderer.h"
#include <algorithm>
#include <string>

namespace photon {

class Ui {
public:
    void begin(Renderer* r, const Input* in) {
        r_ = r;
        in_ = in;
        lh_ = (float)r->glyphH() + 4;
        gw_ = (float)r->glyphW();
        overUi_ = false;
        clickUsed_ = false;
    }
    // true while the mouse is over any panel drawn this frame (camera ignores it)
    bool mouseOverUi() const { return overUi_; }
    bool clickConsumed() const { return clickUsed_; }

    void panel(float x, float y, float w, float h, const std::string& title, float* scroll = nullptr) {
        px_ = x; py_ = y; pw_ = w; ph_ = h;
        if (hover(x, y, w, h)) {
            overUi_ = true;
            if (scroll && in_->wheel != 0) *scroll = std::max(0.0f, *scroll - in_->wheel * 3 * lh_);
        }
        r_->rect(x, y, x + w, y + h, 0.06f, 0.07f, 0.09f, 0.82f);
        r_->rect(x, y, x + w, y + lh_ + 4, 0.16f, 0.20f, 0.28f, 0.95f);
        r_->text(x + 8, y + 3, title, 1.0f, 0.85f, 0.45f);
        cy_ = y + lh_ + 8 - (scroll ? *scroll : 0);
        clipTop_ = y + lh_ + 6;
        clipBot_ = y + h - 4;
    }
    void label(const std::string& s, float r = 0.88f, float g = 0.88f, float b = 0.88f) {
        if (visible()) r_->text(px_ + 10, cy_, fit(s, 10), r, g, b);
        cy_ += lh_;
    }
    void heading(const std::string& s) {
        cy_ += 4;
        if (visible()) {
            r_->text(px_ + 8, cy_, fit(s, 8), 0.55f, 0.75f, 1.0f);
            r_->rect(px_ + 8, cy_ + lh_ - 3, px_ + pw_ - 8, cy_ + lh_ - 2, 0.3f, 0.4f, 0.55f, 0.8f);
        }
        cy_ += lh_ + 2;
    }
    bool button(const std::string& s, bool active = false, float indent = 0) {
        bool clicked = false;
        if (visible()) {
            float x0 = px_ + 8 + indent, x1 = px_ + pw_ - 8, y0 = cy_ - 1, y1 = cy_ + lh_ - 2;
            bool h = hover(x0, y0, x1 - x0, y1 - y0);
            float k = active ? 0.34f : (h ? 0.24f : 0.13f);
            r_->rect(x0, y0, x1, y1, k * 0.8f, k, k * 1.3f, 0.95f);
            r_->text(x0 + 6, cy_, fit(s, 14 + indent), active ? 1.0f : 0.92f, active ? 0.9f : 0.92f, active ? 0.5f : 0.92f);
            if (h && in_->released[0] && !clickUsed_) { clicked = true; clickUsed_ = true; }
            if (h && (in_->pressed[0] || in_->down[0])) clickUsed_ = true;
        }
        cy_ += lh_ + 1;
        return clicked;
    }
    bool checkbox(const std::string& s, bool& v) {
        bool c = button(std::string(v ? "[x] " : "[ ] ") + s, v);
        if (c) v = !v;
        return c;
    }
    void space(float px = 6) { cy_ += px; }
    float lineHeight() const { return lh_; }

private:
    Renderer* r_ = nullptr;
    const Input* in_ = nullptr;
    float lh_ = 18, gw_ = 9;
    float px_ = 0, py_ = 0, pw_ = 0, ph_ = 0, cy_ = 0, clipTop_ = 0, clipBot_ = 0;
    bool overUi_ = false, clickUsed_ = false;
    bool hover(float x, float y, float w, float h) const {
        return in_->mouseX >= x && in_->mouseX < x + w && in_->mouseY >= y && in_->mouseY < y + h;
    }
    bool visible() const { return cy_ >= clipTop_ && cy_ + lh_ <= clipBot_; }
    std::string fit(const std::string& s, float pad) const {
        size_t maxc = (size_t)std::max(1.0f, (pw_ - pad - 8) / gw_);
        return s.size() <= maxc ? s : s.substr(0, maxc - 1) + "~";
    }
};

} // namespace photon
