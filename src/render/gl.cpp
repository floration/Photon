#include "render/gl.h"
#include "platform/platform.h"

#define PHOTON_GL_DEFINE(ret, name, args) PFN_##name p_##name = nullptr;
PHOTON_GL_FUNCS(PHOTON_GL_DEFINE)
#undef PHOTON_GL_DEFINE

namespace photon {

const char* loadGL() {
#define PHOTON_GL_LOAD(ret, name, args) \
    p_##name = (PFN_##name)glProc(#name); \
    if (!p_##name) return #name;
    PHOTON_GL_FUNCS(PHOTON_GL_LOAD)
#undef PHOTON_GL_LOAD
    return nullptr;
}

} // namespace photon
