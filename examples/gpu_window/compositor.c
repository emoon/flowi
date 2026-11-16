#include "compositor.h"
#include <SDL2/SDL.h>
#include <core/core.h>
#include <core/log.h>

// GL entry points the host-side compositor needs beyond GL 1.x, resolved once through SDL_GL_GetProcAddress.

static bool compositor_gl_load(CompositorGl* gl) {
#define COMPOSITOR_GL_LOAD(type, name)                  \
    gl->name = (type)SDL_GL_GetProcAddress("gl" #name); \
    if (!gl->name) {                                    \
        log_error("missing GL entry point gl" #name);   \
        return false;                                   \
    }
    COMPOSITOR_GL_FNS(COMPOSITOR_GL_LOAD)
#undef COMPOSITOR_GL_LOAD
    return true;
}

// Fullscreen-quad compositor. The vertex shader emits a screen-covering triangle from gl_VertexID (no VBO);
// the fragment shader samples the UI texture flipped in V (the backend stores UI row 0 at texel row 0, the
// opposite of GL's bottom-up sample origin) and blends it over the cleared background, premultiplied.

static const char* k_vertex_shader = "#version 430 core\n"
                                     "out vec2 v_uv;\n"
                                     "void main() {\n"
                                     "    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
                                     "    v_uv = p;\n"
                                     "    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                                     "}\n";

static const char* k_fragment_shader = "#version 430 core\n"
                                       "in vec2 v_uv;\n"
                                       "out vec4 frag;\n"
                                       "uniform sampler2D ui_tex;\n"
                                       "void main() {\n"
                                       "    frag = texture(ui_tex, vec2(v_uv.x, 1.0 - v_uv.y));\n"
                                       "}\n";

static GLuint compile(const CompositorGl* gl, GLenum stage, const char* src) {
    GLuint shader = gl->CreateShader(stage);
    gl->ShaderSource(shader, 1, &src, nullptr);
    gl->CompileShader(shader);
    GLint ok = 0;
    gl->GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char info[1024];
        gl->GetShaderInfoLog(shader, sizeof(info), nullptr, info);
        log_error("compositor shader compile failed: %s", info);
        gl->DeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint build_compositor_program(const CompositorGl* gl) {
    GLuint vs = compile(gl, GL_VERTEX_SHADER, k_vertex_shader);
    GLuint fs = compile(gl, GL_FRAGMENT_SHADER, k_fragment_shader);
    if (vs == 0 || fs == 0) {
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
        log_error("compositor program link failed: %s", info);
        return 0;
    }
    return prog;
}

bool compositor_init(Compositor* c) {
    if (!compositor_gl_load(&c->gl)) {
        return false;
    }
    c->program = build_compositor_program(&c->gl);
    if (c->program == 0) {
        return false;
    }
    c->vao = 0;
    c->gl.GenVertexArrays(1, &c->vao); // core profile needs a bound VAO to draw, even with no attributes
    return true;
}

void compositor_draw(const Compositor* c, GLuint texture, int draw_w, int draw_h) {
    const CompositorGl* gl = &c->gl;
    glViewport(0, 0, draw_w, draw_h);
    glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); // premultiplied src-over (seam texture is premultiplied)

    gl->ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    // The backend's target is immutable single-level storage with no sampler filter set. The blit is 1:1
    // (the zoom is in the scene geometry), so NEAREST is exact and avoids a mipmap-incomplete sample.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    gl->UseProgram(c->program);
    gl->Uniform1i(gl->GetUniformLocation(c->program, "ui_tex"), 0);
    gl->BindVertexArray(c->vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glDisable(GL_BLEND);
}
