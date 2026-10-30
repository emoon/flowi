#include "gpu_gl.h"
#include <core/log.h>
#include <EGL/egl.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool gpu_gl_load(GpuGl* gl) {
    bool ok = true;
#define GPU_GL_LOAD(type, name, sym)                         \
    gl->name = (type)eglGetProcAddress(sym);                 \
    if (gl->name == NULL) {                                  \
        log_error("gpu backend: failed to resolve %s", sym); \
        ok = false;                                          \
    }
    GPU_GL_FNS(GPU_GL_LOAD)
#undef GPU_GL_LOAD
    return ok;
}
