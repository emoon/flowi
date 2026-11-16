#pragma once

#include <GL/gl.h>
#include <GL/glext.h>
#include <stdbool.h>

#define COMPOSITOR_GL_FNS(X)                           \
    X(PFNGLCREATESHADERPROC, CreateShader)             \
    X(PFNGLSHADERSOURCEPROC, ShaderSource)             \
    X(PFNGLCOMPILESHADERPROC, CompileShader)           \
    X(PFNGLGETSHADERIVPROC, GetShaderiv)               \
    X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog)     \
    X(PFNGLDELETESHADERPROC, DeleteShader)             \
    X(PFNGLCREATEPROGRAMPROC, CreateProgram)           \
    X(PFNGLATTACHSHADERPROC, AttachShader)             \
    X(PFNGLLINKPROGRAMPROC, LinkProgram)               \
    X(PFNGLGETPROGRAMIVPROC, GetProgramiv)             \
    X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog)   \
    X(PFNGLUSEPROGRAMPROC, UseProgram)                 \
    X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation) \
    X(PFNGLUNIFORM1IPROC, Uniform1i)                   \
    X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays)       \
    X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray)       \
    X(PFNGLACTIVETEXTUREPROC, ActiveTexture)

typedef struct CompositorGl {
#define COMPOSITOR_GL_MEMBER(type, name) type name;
    COMPOSITOR_GL_FNS(COMPOSITOR_GL_MEMBER)
#undef COMPOSITOR_GL_MEMBER
} CompositorGl;

typedef struct Compositor {
    CompositorGl gl;
    GLuint program;
    GLuint vao;
} Compositor;

bool compositor_init(Compositor* c);

void compositor_draw(const Compositor* c, GLuint texture, int draw_w, int draw_h);
