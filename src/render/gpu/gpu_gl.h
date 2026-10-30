#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GPU backend: minimal OpenGL 4.3 function loader.
//
// The backend assumes a current GL >= 4.3 core context, created by the host before the backend is constructed.
// GL 1.x entry points (glGenTextures, glReadPixels, ...) are exported by libGL and called directly; everything
// newer is resolved through eglGetProcAddress into this table at backend create.

#include <GL/gl.h>
#include <GL/glext.h>
#include <stdbool.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// X-macro: typedef, struct member, GL symbol name.

#define GPU_GL_FNS(X)                                                                         \
    X(PFNGLCREATESHADERPROC, CreateShader, "glCreateShader")                                  \
    X(PFNGLSHADERSOURCEPROC, ShaderSource, "glShaderSource")                                  \
    X(PFNGLCOMPILESHADERPROC, CompileShader, "glCompileShader")                               \
    X(PFNGLGETSHADERIVPROC, GetShaderiv, "glGetShaderiv")                                     \
    X(PFNGLGETSHADERINFOLOGPROC, GetShaderInfoLog, "glGetShaderInfoLog")                      \
    X(PFNGLDELETESHADERPROC, DeleteShader, "glDeleteShader")                                  \
    X(PFNGLCREATEPROGRAMPROC, CreateProgram, "glCreateProgram")                               \
    X(PFNGLATTACHSHADERPROC, AttachShader, "glAttachShader")                                  \
    X(PFNGLLINKPROGRAMPROC, LinkProgram, "glLinkProgram")                                     \
    X(PFNGLGETPROGRAMIVPROC, GetProgramiv, "glGetProgramiv")                                  \
    X(PFNGLGETPROGRAMINFOLOGPROC, GetProgramInfoLog, "glGetProgramInfoLog")                   \
    X(PFNGLDELETEPROGRAMPROC, DeleteProgram, "glDeleteProgram")                               \
    X(PFNGLUSEPROGRAMPROC, UseProgram, "glUseProgram")                                        \
    X(PFNGLGETUNIFORMLOCATIONPROC, GetUniformLocation, "glGetUniformLocation")                \
    X(PFNGLUNIFORM1UIPROC, Uniform1ui, "glUniform1ui")                                        \
    X(PFNGLUNIFORM2UIPROC, Uniform2ui, "glUniform2ui")                                        \
    X(PFNGLUNIFORM1IPROC, Uniform1i, "glUniform1i")                                           \
    X(PFNGLUNIFORM1IVPROC, Uniform1iv, "glUniform1iv")                                        \
    X(PFNGLUNIFORM2FVPROC, Uniform2fv, "glUniform2fv")                                        \
    X(PFNGLGENSAMPLERSPROC, GenSamplers, "glGenSamplers")                                     \
    X(PFNGLDELETESAMPLERSPROC, DeleteSamplers, "glDeleteSamplers")                            \
    X(PFNGLBINDSAMPLERPROC, BindSampler, "glBindSampler")                                     \
    X(PFNGLSAMPLERPARAMETERIPROC, SamplerParameteri, "glSamplerParameteri")                   \
    X(PFNGLGENBUFFERSPROC, GenBuffers, "glGenBuffers")                                        \
    X(PFNGLDELETEBUFFERSPROC, DeleteBuffers, "glDeleteBuffers")                               \
    X(PFNGLBINDBUFFERPROC, BindBuffer, "glBindBuffer")                                        \
    X(PFNGLBUFFERDATAPROC, BufferData, "glBufferData")                                        \
    X(PFNGLBINDBUFFERBASEPROC, BindBufferBase, "glBindBufferBase")                            \
    X(PFNGLGETBUFFERSUBDATAPROC, GetBufferSubData, "glGetBufferSubData")                      \
    X(PFNGLTEXSTORAGE2DPROC, TexStorage2D, "glTexStorage2D")                                  \
    X(PFNGLBINDIMAGETEXTUREPROC, BindImageTexture, "glBindImageTexture")                      \
    X(PFNGLDISPATCHCOMPUTEPROC, DispatchCompute, "glDispatchCompute")                         \
    X(PFNGLMEMORYBARRIERPROC, MemoryBarrier, "glMemoryBarrier")                               \
    X(PFNGLGENFRAMEBUFFERSPROC, GenFramebuffers, "glGenFramebuffers")                         \
    X(PFNGLDELETEFRAMEBUFFERSPROC, DeleteFramebuffers, "glDeleteFramebuffers")                \
    X(PFNGLBINDFRAMEBUFFERPROC, BindFramebuffer, "glBindFramebuffer")                         \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, FramebufferTexture2D, "glFramebufferTexture2D")          \
    X(PFNGLCHECKFRAMEBUFFERSTATUSPROC, CheckFramebufferStatus, "glCheckFramebufferStatus")    \
    X(PFNGLGENRENDERBUFFERSPROC, GenRenderbuffers, "glGenRenderbuffers")                      \
    X(PFNGLDELETERENDERBUFFERSPROC, DeleteRenderbuffers, "glDeleteRenderbuffers")             \
    X(PFNGLBINDRENDERBUFFERPROC, BindRenderbuffer, "glBindRenderbuffer")                      \
    X(PFNGLRENDERBUFFERSTORAGEPROC, RenderbufferStorage, "glRenderbufferStorage")             \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, FramebufferRenderbuffer, "glFramebufferRenderbuffer") \
    X(PFNGLGENVERTEXARRAYSPROC, GenVertexArrays, "glGenVertexArrays")                         \
    X(PFNGLDELETEVERTEXARRAYSPROC, DeleteVertexArrays, "glDeleteVertexArrays")                \
    X(PFNGLBINDVERTEXARRAYPROC, BindVertexArray, "glBindVertexArray")                         \
    X(PFNGLVERTEXATTRIBPOINTERPROC, VertexAttribPointer, "glVertexAttribPointer")             \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, EnableVertexAttribArray, "glEnableVertexAttribArray") \
    X(PFNGLUNIFORMMATRIX4FVPROC, UniformMatrix4fv, "glUniformMatrix4fv")

typedef struct GpuGl {
#define GPU_GL_MEMBER(type, name, sym) type name;
    GPU_GL_FNS(GPU_GL_MEMBER)
#undef GPU_GL_MEMBER
} GpuGl;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Resolve every entry via eglGetProcAddress. Requires a current GL context. Returns false (and logs) if any
// function is missing - the backend must not be constructed then.

bool gpu_gl_load(GpuGl* gl);
