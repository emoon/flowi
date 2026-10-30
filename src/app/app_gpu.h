#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fl_app GPU host - Surface Binding for the GPU render backend.
//
// The GPU backend (src/render/gpu) renders into an offscreen RGBA8 target but does NOT own a GL context or a
// window: the host creates and makes-current a GL >= 4.3 core context, then constructs the backend against it.
// Headless, this module owns that context (the FlAppGpu handle); windowed, the SDL WindowBackend owns the window,
// the shared GL context and the compositor/present, and this module only builds the backend against it and
// resolves the surface texture.
//
// All EGL / GL types stay inside app_gpu.c. Compiled only when FL_APP_HAVE_GPU (OpenGL_EGL_FOUND).
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <ui/render_backend.h>
#include <core/types.h>
#include <stdbool.h>

struct FlArena;

typedef struct FlAppGpu FlAppGpu;

// Bring up a headless EGL surfaceless GL 4.3 core context and construct the GPU render backend against it into
// *out_backend. Returns NULL (and leaves *out_backend zeroed) on any failure: EGL unavailable, no GL 4.3, or a
// shader compile error. The handle owns the context.
FlAppGpu* fl_app_gpu_create_headless(struct FlArena* arena, FlRenderBackend* out_backend);

// Construct the GPU render backend against the GL context the caller has already made current (the windowed path,
// where the SDL WindowBackend owns the context). Returns false (and leaves *out_backend zeroed) when GL is below
// the 4.3 floor or a shader fails to compile.
bool fl_app_gpu_create_backend(struct FlArena* arena, FlRenderBackend* out_backend);

// Resolve the GPU backend's offscreen surface texture for the host to hand to the window backend's present. The
// handle is a plain u32 GL texture name on GL (requires the backend's GL context current), an opaque void*
// id<MTLTexture> on Metal. Dimensions track the surface's resolution. Returns true when resolved.
#if __APPLE__
bool fl_app_gpu_surface_texture(FlSurface* surface, void** handle, u32* width, u32* height);
#else
bool fl_app_gpu_surface_texture(FlSurface* surface, u32* texture, u32* width, u32* height);
#endif

// Tear down the headless EGL context. Call AFTER the backend's own destroy (which frees GL resources and needs
// the context still current).
void fl_app_gpu_destroy(FlAppGpu* gpu);
