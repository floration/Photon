#include "render/renderer.h"
#include "render/gl.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace photon {

namespace {

const char* MESH_VS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aN;
layout(location=2) in vec3 aC;
layout(location=3) in vec3 aE;
uniform mat4 uVP;
out vec3 vN; out vec3 vC; out vec3 vE;
out vec3 vW;
void main() { vN = aN; vC = aC; vE = aE; vW = aPos; gl_Position = uVP * vec4(aPos, 1.0); }
)";

const char* MESH_FS = R"(#version 330 core
in vec3 vN; in vec3 vC; in vec3 vE; in vec3 vW;
uniform float uAlpha;
uniform int uNumLights;
uniform vec4 uLights[8];     // xyz position, w intensity (fire light, viewer-only)
out vec4 o;
void main() {
    vec3 n = normalize(vN);
    vec3 L = normalize(vec3(0.35, 0.25, 0.9));
    float d = max(dot(n, L), 0.0);
    float hemi = 0.5 + 0.5 * n.z;
    vec3 c = vC * (0.22 + 0.28 * hemi + 0.5 * d) + vE;
    vec3 fl = vec3(0.0);
    for (int i = 0; i < uNumLights; ++i) {
        vec3 dl = uLights[i].xyz - vW;
        float r2 = dot(dl, dl);
        float lam = max(dot(n, dl * inversesqrt(r2 + 1e-6)), 0.0) * 0.85 + 0.15;
        fl += vec3(1.0, 0.55, 0.22) * uLights[i].w * lam / (1.0 + r2);
    }
    c += vC * fl;
    o = vec4(c, uAlpha);
}
)";

const char* FULL_VS = R"(#version 330 core
out vec2 vUV;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Ray-marched participating medium. Extinction uses the mass specific
// extinction coefficient of smoke (8700 m2/kg for soot, Mulholland & Croarkin);
// the aerosol value is a PROTOTYPE estimate. Flame emission is drawn from the
// computed heat release rate per unit volume; hot soot glows by temperature.
const char* VOL_FS = R"(#version 330 core
in vec2 vUV;
uniform sampler2D uColor;
uniform sampler2D uDepth;
uniform sampler3D uVol;
uniform mat4 uInvVP;
uniform vec3 uEye, uBoxMin, uBoxMax;
uniform float uStep, uClipZ, uSliceZ, uTMin, uTMax;
uniform int uSmoke, uSlice, uFire;
uniform float uTime;
out vec4 o;

vec3 worldAt(vec2 uv, float d) {
    vec4 p = uInvVP * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}
vec3 heatmap(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c0 = vec3(0.10, 0.15, 0.55), c1 = vec3(0.10, 0.65, 0.85), c2 = vec3(0.95, 0.90, 0.20),
         c3 = vec3(0.95, 0.35, 0.05), c4 = vec3(0.85, 0.05, 0.05);
    if (t < 0.25) return mix(c0, c1, t / 0.25);
    if (t < 0.5) return mix(c1, c2, (t - 0.25) / 0.25);
    if (t < 0.75) return mix(c2, c3, (t - 0.5) / 0.25);
    return mix(c3, c4, (t - 0.75) / 0.25);
}
vec3 glowColor(float T) {   // rough incandescence ramp, dull red ~800 K to yellow-white
    float t = clamp((T - 800.0) / 900.0, 0.0, 1.0);
    return vec3(1.0, 0.25 + 0.6 * t, 0.05 + 0.45 * t * t) * t * t;
}
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
float hash3(vec3 p) { p = fract(p * 0.3183099 + 0.1); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }
float vnoise(vec3 x) {
    vec3 i = floor(x), f = fract(x);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(mix(hash3(i), hash3(i + vec3(1, 0, 0)), f.x), mix(hash3(i + vec3(0, 1, 0)), hash3(i + vec3(1, 1, 0)), f.x), f.y),
               mix(mix(hash3(i + vec3(0, 0, 1)), hash3(i + vec3(1, 0, 1)), f.x), mix(hash3(i + vec3(0, 1, 1)), hash3(i + vec3(1, 1, 1)), f.x), f.y), f.z);
}
// Sub-grid flame detail for display only: the solver resolves 20 cm; the
// flickering tongues inside a burning cell are a rendering effect.
float flameNoise(vec3 p) {
    vec3 q = p * vec3(5.0, 5.0, 2.6) - vec3(0.0, 0.0, uTime * 3.2);
    float n = 0.55 * vnoise(q) + 0.30 * vnoise(q * 2.1 + vec3(0.0, 0.0, -uTime * 2.0)) + 0.15 * vnoise(q * 4.3);
    return n;
}
vec3 flameColor(float k) {   // k 0..1: deep red-orange -> orange -> yellow -> pale yellow
    return mix(mix(vec3(0.9, 0.18, 0.02), vec3(1.0, 0.5, 0.08), smoothstep(0.0, 0.4, k)),
               vec3(1.0, 0.9, 0.55), smoothstep(0.45, 1.0, k));
}

void main() {
    vec3 base = texture(uColor, vUV).rgb;
    float d = texture(uDepth, vUV).r;
    vec3 ro = uEye;
    vec3 rd = normalize(worldAt(vUV, 1.0) - ro);
    float tScene = d < 1.0 ? length(worldAt(vUV, d) - ro) : 1e9;
    vec3 bmax = vec3(uBoxMax.xy, min(uBoxMax.z, uClipZ));
    vec3 inv = 1.0 / rd;
    vec3 ta = (uBoxMin - ro) * inv, tb = (bmax - ro) * inv;
    vec3 tmn = min(ta, tb), tmx = max(ta, tb);
    float t0 = max(max(max(tmn.x, tmn.y), tmn.z), 0.0);
    float t1 = min(min(tmx.x, tmx.y), tmx.z);
    float tEnd = min(t1, tScene);
    vec3 col = vec3(0.0);
    float tr = 1.0;
    float tS = (uSliceZ - ro.z) / rd.z;
    bool sliceHit = uSlice == 1 && tS > t0 && tS < tEnd && uSliceZ < bmax.z;
    if (t0 < tEnd) {
        float ds = uStep;
        float jit = hash(gl_FragCoord.xy);
        bool sliceDone = !sliceHit;
        int n = int(min(600.0, (tEnd - t0) / ds + 1.0));
        for (int i = 0; i < n; ++i) {
            float t = t0 + (float(i) + jit) * ds;
            if (t > tEnd) break;
            if (!sliceDone && t >= tS) {
                vec4 sv = texture(uVol, (ro + rd * tS - uBoxMin) / (uBoxMax - uBoxMin));
                if (sv.b > 150.0) {
                    float tn = (sv.b - uTMin) / (uTMax - uTMin);
                    float sa = mix(0.12, 0.88, smoothstep(0.0, 0.2, tn));
                    col += tr * sa * heatmap(tn);
                    tr *= 1.0 - sa;
                }
                sliceDone = true;
            }
            if (uSmoke == 0 && uFire == 0) continue;
            vec3 p = ro + rd * t;
            vec4 v = texture(uVol, (p - uBoxMin) / (uBoxMax - uBoxMin));
            float soot = max(v.r, 0.0), aero = max(v.g, 0.0);
            float sigma = 8700.0 * soot + 3000.0 * aero;
            float a = uSmoke == 1 ? 1.0 - exp(-sigma * ds) : 0.0;
            float lightFrac = aero / (soot * 3.0 + aero + 1e-12);
            vec3 scat = mix(vec3(0.05, 0.05, 0.05), vec3(0.80, 0.78, 0.70), lightFrac);
            vec3 emit = vec3(0.0);
            if (uFire == 1) {
                float q = v.a / 1.0e6;                         // MW/m3 of heat release
                float hot = smoothstep(1000.0, 1600.0, v.b);   // flame-temperature gas
                float drive = clamp(q * 0.9, 0.0, 3.0) + hot * 0.9;
                if (drive > 0.01) {
                    float n = flameNoise(p);
                    float shape = smoothstep(0.62 - 0.25 * clamp(drive, 0.0, 1.5), 0.85, n + 0.15 * min(drive, 1.5));
                    float k = clamp(0.25 * drive + 0.6 * n * shape, 0.0, 1.0);
                    emit = flameColor(k) * shape * drive * 6.0;
                }
                emit += glowColor(v.b) * clamp(sigma * 0.5, 0.0, 1.0) * 2.0;
            }
            col += tr * (a * scat + emit * ds);
            tr *= 1.0 - a;
            if (tr < 0.004) break;
        }
        if (!sliceDone) {
            vec4 sv = texture(uVol, (ro + rd * tS - uBoxMin) / (uBoxMax - uBoxMin));
            if (sv.b > 150.0) {
                float tn = (sv.b - uTMin) / (uTMax - uTMin);
                float sa = mix(0.12, 0.88, smoothstep(0.0, 0.2, tn));
                col += tr * sa * heatmap(tn);
                tr *= 1.0 - sa;
            }
        }
    }
    vec3 c = base * tr + (vec3(1.0) - exp(-col * 1.1));   // soft-saturate light, keep flame hue
    o = vec4(c, 1.0);
}
)";

const char* TEXT_VS = R"(#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aC;
uniform vec2 uScreen;
out vec2 vUV; out vec4 vC;
void main() {
    vUV = aUV; vC = aC;
    gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
}
)";

const char* TEXT_FS = R"(#version 330 core
in vec2 vUV; in vec4 vC;
uniform sampler2D uFont;
out vec4 o;
void main() {
    float a = vUV.x < 0.0 ? 1.0 : texture(uFont, vUV).r;
    o = vec4(vC.rgb, vC.a * a);
}
)";

unsigned compile(unsigned type, const char* src, std::string& err) {
    unsigned s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        err = std::string("shader compile: ") + log;
        return 0;
    }
    return s;
}

unsigned program(const char* vs, const char* fs, std::string& err) {
    unsigned v = compile(GL_VERTEX_SHADER, vs, err);
    if (!v) return 0;
    unsigned f = compile(GL_FRAGMENT_SHADER, fs, err);
    if (!f) return 0;
    unsigned p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    int ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    glDeleteShader(v);
    glDeleteShader(f);
    if (!ok) {
        char log[4096];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        err = std::string("program link: ") + log;
        return 0;
    }
    return p;
}

void setupMeshVao(unsigned& vao, unsigned& vbo) {
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    for (int a = 0; a < 4; ++a) {
        glEnableVertexAttribArray(a);
        glVertexAttribPointer(a, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (const void*)(sizeof(float) * 3 * a));
    }
    glBindVertexArray(0);
}

} // namespace

bool Renderer::init(Platform& platform, std::string& err) {
    if (const char* missing = loadGL()) { err = std::string("OpenGL entry point missing: ") + missing; return false; }
    meshProg_ = program(MESH_VS, MESH_FS, err);
    if (!meshProg_) return false;
    volProg_ = program(FULL_VS, VOL_FS, err);
    if (!volProg_) return false;
    textProg_ = program(TEXT_VS, TEXT_FS, err);
    if (!textProg_) return false;
    setupMeshVao(meshVao_, meshVbo_);
    setupMeshVao(glassVao_, glassVbo_);
    glGenVertexArrays(1, &emptyVao_);
    glGenVertexArrays(1, &textVao_);
    glGenBuffers(1, &textVbo_);
    glBindVertexArray(textVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (const void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (const void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (const void*)(4 * sizeof(float)));
    glBindVertexArray(0);
    if (!platform.makeFont(16, font_)) { err = "font rasterization failed"; return false; }
    glGenTextures(1, &fontTex_);
    glBindTexture(GL_TEXTURE_2D, fontTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, font_.width, font_.height, 0, GL_RED, GL_UNSIGNED_BYTE, font_.alpha.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenTextures(1, &volTex_);
    return true;
}

void Renderer::setOpaque(const std::vector<Vertex>& v) {
    glBindBuffer(GL_ARRAY_BUFFER, meshVbo_);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(Vertex)), v.data(), GL_DYNAMIC_DRAW);
    meshCount_ = (int)v.size();
}

void Renderer::setGlass(const std::vector<Vertex>& v) {
    glBindBuffer(GL_ARRAY_BUFFER, glassVbo_);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(Vertex)), v.data(), GL_DYNAMIC_DRAW);
    glassCount_ = (int)v.size();
}

void Renderer::setVolume(int nx, int ny, int nz, const float* rgba, Vec3 bmin, Vec3 bmax) {
    glBindTexture(GL_TEXTURE_3D, volTex_);
    if (nx != volDims_[0] || ny != volDims_[1] || nz != volDims_[2]) {
        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA32F, nx, ny, nz, 0, GL_RGBA, GL_FLOAT, rgba);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        volDims_[0] = nx; volDims_[1] = ny; volDims_[2] = nz;
    } else {
        glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, nx, ny, nz, GL_RGBA, GL_FLOAT, rgba);
    }
    boxMin_ = bmin;
    boxMax_ = bmax;
}

void Renderer::text(float x, float y, const std::string& s, float r, float g, float b, float a) {
    float cw = (float)font_.cellW, ch = (float)font_.cellH;
    for (char c : s) {
        int code = (unsigned char)c;
        if (code < 32 || code > 127) code = '?';
        int gi = code - 32;
        float u0 = (float)((gi % font_.cols) * font_.cellW) / font_.width;
        float v0 = (float)((gi / font_.cols) * font_.cellH) / font_.height;
        float u1 = u0 + cw / font_.width, v1 = v0 + ch / font_.height;
        float q[6][4] = {{x, y, u0, v0}, {x + cw, y, u1, v0}, {x + cw, y + ch, u1, v1},
                         {x, y, u0, v0}, {x + cw, y + ch, u1, v1}, {x, y + ch, u0, v1}};
        for (auto& vtx : q) textVerts_.insert(textVerts_.end(), {vtx[0], vtx[1], vtx[2], vtx[3], r, g, b, a});
        x += cw;
    }
}

void Renderer::rect(float x0, float y0, float x1, float y1, float r, float g, float b, float a) {
    float q[6][2] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y0}, {x1, y1}, {x0, y1}};
    for (auto& vtx : q) textVerts_.insert(textVerts_.end(), {vtx[0], vtx[1], -1.f, -1.f, r, g, b, a});
}

void Renderer::ensureFbo(int w, int h) {
    if (w == fbW_ && h == fbH_ && fbo_) return;
    if (fbo_) {
        glDeleteFramebuffers(1, &fbo_);
        glDeleteTextures(1, &colorTex_);
        glDeleteTextures(1, &depthTex_);
    }
    glGenFramebuffers(1, &fbo_);
    glGenTextures(1, &colorTex_);
    glGenTextures(1, &depthTex_);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, depthTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTex_, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex_, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    fbW_ = w;
    fbH_ = h;
}

void Renderer::render(int w, int h, const Mat4& vp, const Vec3& eye, const RenderOptions& opt) {
    if (w <= 0 || h <= 0) return;
    ensureFbo(w, h);
    // ---- scene into the offscreen target
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w, h);
    glClearColor(0.56f, 0.66f, 0.78f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glUseProgram(meshProg_);
    glUniformMatrix4fv(glGetUniformLocation(meshProg_, "uVP"), 1, GL_FALSE, vp.m);
    glUniform1f(glGetUniformLocation(meshProg_, "uAlpha"), 1.0f);
    {
        int nl = opt.fire ? std::min(8, (int)opt.fireLights.size()) : 0;
        glUniform1i(glGetUniformLocation(meshProg_, "uNumLights"), nl);
        if (nl > 0) glUniform4fv(glGetUniformLocation(meshProg_, "uLights"), nl, &opt.fireLights[0].x);
    }
    glBindVertexArray(meshVao_);
    glDrawArrays(GL_TRIANGLES, 0, meshCount_);
    if (glassCount_ > 0) {
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glUniform1f(glGetUniformLocation(meshProg_, "uAlpha"), 0.28f);
        glBindVertexArray(glassVao_);
        glDrawArrays(GL_TRIANGLES, 0, glassCount_);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    // ---- volume composite to the window
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glUseProgram(volProg_);
    Mat4 inv;
    invert(vp, inv);
    glUniformMatrix4fv(glGetUniformLocation(volProg_, "uInvVP"), 1, GL_FALSE, inv.m);
    glUniform3f(glGetUniformLocation(volProg_, "uEye"), eye.x, eye.y, eye.z);
    glUniform3f(glGetUniformLocation(volProg_, "uBoxMin"), boxMin_.x, boxMin_.y, boxMin_.z);
    glUniform3f(glGetUniformLocation(volProg_, "uBoxMax"), boxMax_.x, boxMax_.y, boxMax_.z);
    float cell = volDims_[0] > 0 ? (boxMax_.x - boxMin_.x) / volDims_[0] : 0.2f;
    glUniform1f(glGetUniformLocation(volProg_, "uStep"), cell * 0.3f);
    glUniform1f(glGetUniformLocation(volProg_, "uClipZ"), opt.clipZ);
    glUniform1f(glGetUniformLocation(volProg_, "uSliceZ"), opt.sliceZ);
    glUniform1f(glGetUniformLocation(volProg_, "uTMin"), opt.sliceTMin);
    glUniform1f(glGetUniformLocation(volProg_, "uTMax"), opt.sliceTMax);
    glUniform1i(glGetUniformLocation(volProg_, "uSmoke"), opt.smoke ? 1 : 0);
    glUniform1i(glGetUniformLocation(volProg_, "uSlice"), opt.slice ? 1 : 0);
    glUniform1i(glGetUniformLocation(volProg_, "uFire"), opt.fire ? 1 : 0);
    glUniform1f(glGetUniformLocation(volProg_, "uTime"), opt.time);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, colorTex_);
    glUniform1i(glGetUniformLocation(volProg_, "uColor"), 0);
    glActiveTexture(GL_TEXTURE0 + 1);
    glBindTexture(GL_TEXTURE_2D, depthTex_);
    glUniform1i(glGetUniformLocation(volProg_, "uDepth"), 1);
    glActiveTexture(GL_TEXTURE0 + 2);
    glBindTexture(GL_TEXTURE_3D, volTex_);
    glUniform1i(glGetUniformLocation(volProg_, "uVol"), 2);
    glBindVertexArray(emptyVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glActiveTexture(GL_TEXTURE0);
    // ---- text overlay
    if (!textVerts_.empty()) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(textProg_);
        glUniform2f(glGetUniformLocation(textProg_, "uScreen"), (float)w, (float)h);
        glBindTexture(GL_TEXTURE_2D, fontTex_);
        glUniform1i(glGetUniformLocation(textProg_, "uFont"), 0);
        glBindVertexArray(textVao_);
        glBindBuffer(GL_ARRAY_BUFFER, textVbo_);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(textVerts_.size() * sizeof(float)), textVerts_.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(textVerts_.size() / 8));
        glDisable(GL_BLEND);
        textVerts_.clear();
    }
    glBindVertexArray(0);
}

bool Renderer::saveScreenshot(const std::string& path, int w, int h) {
    std::vector<unsigned char> px((size_t)w * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    int rowBytes = w * 3, pad = (4 - rowBytes % 4) % 4;
    unsigned size = 54u + (unsigned)((rowBytes + pad) * h);
    unsigned char hdr[54] = {'B', 'M'};
    auto put32 = [&](int off, unsigned v) { std::memcpy(hdr + off, &v, 4); };
    put32(2, size); put32(10, 54); put32(14, 40); put32(18, (unsigned)w); put32(22, (unsigned)h);
    hdr[26] = 1; hdr[28] = 24;
    std::fwrite(hdr, 1, 54, f);
    std::vector<unsigned char> row(rowBytes + pad, 0);
    for (int y = 0; y < h; ++y) {   // GL rows are bottom-up, as BMP expects
        for (int x = 0; x < w; ++x) {
            const unsigned char* p = &px[((size_t)y * w + x) * 4];
            row[x * 3] = p[2]; row[x * 3 + 1] = p[1]; row[x * 3 + 2] = p[0];
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    return true;
}

} // namespace photon