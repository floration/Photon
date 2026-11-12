// Photon - OpenGL 3.3 renderer: opaque scene mesh, translucent glass, a
// ray-marched smoke/flame volume read from the solver fields, a temperature
// slice, and 2D text overlay.
#pragma once
#include "core/vmath.h"
#include "platform/platform.h"
#include <string>
#include <vector>

namespace photon {

struct Vertex {
    float pos[3];
    float nrm[3];
    float col[3];
    float emi[3];
    float pat = 0;   // procedural surface texture (see Pattern)
};

// Procedural surface textures, generated in the shader from world position:
// no image files or image libraries.
enum Pattern : int { PAT_NONE = 0, PAT_WOOD, PAT_OSB, PAT_CARPET, PAT_DRYWALL, PAT_FABRIC, PAT_VINYL_TILE, PAT_SHINGLE,
                     PAT_SIDING, PAT_CONCRETE, PAT_GRASS, PAT_METAL, PAT_PLASTIC };

struct RenderOptions {
    bool smoke = true;
    bool fire = true;
    float smokeScale = 0.5f;        // display density multiplier on the physical extinction
    bool slice = false;
    float sliceZ = 1.5f;
    float clipZ = 1e9f;      // volume is hidden above this height (cutaway)
    float sliceTMin = 293.15f, sliceTMax = 1100.0f;
    float time = 0;                 // wall-clock seconds, drives display-only flame flicker
    std::vector<Vec4> fireLights;   // xyz position, w intensity
};

class Renderer {
public:
    bool init(Platform& platform, std::string& err);
    void setOpaque(const std::vector<Vertex>& v);
    void setGlass(const std::vector<Vertex>& v);
    // rgba per cell: soot density (kg/m3), aerosol density (kg/m3), gas T (K, 0 = solid), HRR (W/m3)
    void setVolume(int nx, int ny, int nz, const float* rgba, Vec3 boxMin, Vec3 boxMax);
    void text(float x, float y, const std::string& s, float r = 1, float g = 1, float b = 1, float a = 1);
    void rect(float x0, float y0, float x1, float y1, float r, float g, float b, float a);
    void render(int width, int height, const Mat4& viewProj, const Vec3& eye, const RenderOptions& opt);
    bool saveScreenshot(const std::string& path, int width, int height);  // window contents as BMP
    int glyphW() const { return font_.cellW; }
    int glyphH() const { return font_.cellH; }

private:
    unsigned meshProg_ = 0, volProg_ = 0, textProg_ = 0;
    unsigned meshVao_ = 0, meshVbo_ = 0, glassVao_ = 0, glassVbo_ = 0, emptyVao_ = 0, textVao_ = 0, textVbo_ = 0;
    int meshCount_ = 0, glassCount_ = 0;
    unsigned fbo_ = 0, colorTex_ = 0, depthTex_ = 0, volTex_ = 0, fontTex_ = 0;
    int fbW_ = 0, fbH_ = 0, volDims_[3] = {0, 0, 0};
    Vec3 boxMin_, boxMax_;
    FontAtlas font_;
    std::vector<float> textVerts_;
    void ensureFbo(int w, int h);
};

} // namespace photon
