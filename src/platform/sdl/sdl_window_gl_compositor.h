#pragma once

// Host-side GL compositor: the GL entry points beyond GL 1.x and the fullscreen-quad program.

#include <GL/gl.h>
#include <GL/glext.h>
#include <core/core.h>

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
    X(PFNGLDELETEPROGRAMPROC, DeleteProgram)           \
    X(PFNGLUSEPROGRAMPROC, UseProgram)                 \
    X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation) \
    X(PFNGLUNIFORM1IPROC, Uniform1i)                   \
    X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays)       \
    X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays) \
    X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray)       \
    X(PFNGLACTIVETEXTUREPROC, ActiveTexture)

typedef struct SdlCompositorGl {
#define COMPOSITOR_GL_MEMBER(type, name) type name;
    COMPOSITOR_GL_FNS(COMPOSITOR_GL_MEMBER)
#undef COMPOSITOR_GL_MEMBER
} SdlCompositorGl;

// Resolves every entry point through SDL_GL_GetProcAddress with a GL context current. False if one is missing.
bool sdl_compositor_gl_load(SdlCompositorGl* gl);

// Compiles and links the fullscreen-quad program. Returns 0 on failure (logged).
GLuint sdl_build_compositor_program(const SdlCompositorGl* gl);
