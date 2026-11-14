// Host-side GL compositor: entry-point loading and the fullscreen-quad program.

#include "../window_backend.h"

#if BACKEND_USE_SDL && FL_WINDOW_HAVE_GPU

#include "sdl_window_gl_compositor.h"
#include <core/log.h>
#include <SDL.h>

bool sdl_compositor_gl_load(SdlCompositorGl* gl) {
#define COMPOSITOR_GL_LOAD(type, name)                               \
    gl->name = (type)SDL_GL_GetProcAddress("gl" #name);              \
    if (!gl->name) {                                                 \
        log_error("fl_app window: missing GL entry point gl" #name); \
        return false;                                                \
    }
    COMPOSITOR_GL_FNS(COMPOSITOR_GL_LOAD)
#undef COMPOSITOR_GL_LOAD
    return true;
}

// Fullscreen-quad compositor: the vertex shader emits a screen-covering triangle from gl_VertexID; the fragment
// shader samples the UI texture flipped in V.
static const char* k_vertex_shader = "#version 430 core\n"
                                     "out vec2 v_uv;\n"
                                     "void main() {\n"
                                     "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
                                     "    v_uv = p;\n"
                                     "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                                     "}\n";

static const char* k_fragment_shader
    = "#version 430 core\n"
      "in vec2 v_uv;\n"
      "out vec4 frag;\n"
      "uniform sampler2D ui_tex;\n"
      "void main() {\n"
      "    frag = texture(ui_tex, vec2(v_uv.x, 1.0 - v_uv.y));\n" // flip V: texel row 0 is the top UI row
      "}\n";

static GLuint compile_shader(const SdlCompositorGl* gl, GLenum stage, const char* src) {
    GLuint shader = gl->CreateShader(stage);
    gl->ShaderSource(shader, 1, &src, nullptr);
    gl->CompileShader(shader);
    GLint ok = 0;
    gl->GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char info[1024];
        gl->GetShaderInfoLog(shader, sizeof(info), nullptr, info);
        log_error("fl_app window: compositor shader compile failed: %s", info);
        gl->DeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint sdl_build_compositor_program(const SdlCompositorGl* gl) {
    GLuint vs = compile_shader(gl, GL_VERTEX_SHADER, k_vertex_shader);
    GLuint fs = compile_shader(gl, GL_FRAGMENT_SHADER, k_fragment_shader);
    if (vs == 0 || fs == 0) {
        // Delete the shader that compiled.
        if (vs != 0) {
            gl->DeleteShader(vs);
        }
        if (fs != 0) {
            gl->DeleteShader(fs);
        }
        return 0;
    }
    GLuint prog = gl->CreateProgram();
    gl->AttachShader(prog, vs);
    gl->AttachShader(prog, fs);
    gl->LinkProgram(prog);
    gl->DeleteShader(vs);
    gl->DeleteShader(fs);
    GLint ok = 0;
    gl->GetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char info[1024];
        gl->GetProgramInfoLog(prog, sizeof(info), nullptr, info);
        log_error("fl_app window: compositor program link failed: %s", info);
        gl->DeleteProgram(prog);
        return 0;
    }
    return prog;
}

#endif // BACKEND_USE_SDL && FL_WINDOW_HAVE_GPU
