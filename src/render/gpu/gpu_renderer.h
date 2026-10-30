#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GPU render backend entry (OpenGL 4.3 core on Linux/Windows, Metal on Apple), all-compute.
//
// A frame is two compute dispatches into an offscreen RGBA8 target: tile classification (one thread per 8x8 tile,
// brute-force AABB tests into fixed-capacity per-tile index lists) and raster (one 8x8 workgroup per tile, register
// blending, a single imageStore per pixel). The target carries premultiplied coverage alpha for the host to
// composite over the display plane; presentation stays out of this object.
//
// GL: the host must have a GL >= 4.3 core context current on this thread for every entry point here - create,
// render, change_resolution, read_pixels, destroy. Metal: no GL context is involved.
//
// No GL type crosses this header.

#include <core/types.h>
#include <ui/render_backend.h>

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Returns a zeroed FlRenderBackend (render == NULL) when the current context is below the 4.3 floor or shader
// compilation fails; the host must check before injecting.

FlRenderBackend create_gpu_renderer(struct FlArena* arena);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Surface-texture seam: the host's handle on the offscreen RGBA8 target the backend renders into, carrying
// premultiplied coverage alpha, for the host to sample and composite. Takes the surface handle it rendered into,
// one per window, and requires the backend's GL context current on this thread.
//
// Valid after a surface is created; the dimensions track that surface's current resolution. The handle is
// backend-owned, and rebuilding the offscreen target invalidates it - re-fetch after a change_resolution on this
// surface, and on Metal after this surface's first gpu_renderer_surface_iosurface call (below). Later IOSurface
// calls invalidate nothing.
//
// The handle field is platform-conditional: a u32 GL texture name on the GL backend, an opaque void* carrying the
// retained id<MTLTexture> on Metal. sizeof(GpuSurfaceTexture) varies with it, so the struct is returned and
// consumed locally, never memcpy'd across an SDK or compile-unit boundary.

typedef struct GpuSurfaceTexture {
#if __APPLE__
    void* handle; // opaque id<MTLTexture> of the offscreen RGBA8 target, retained by the backend
#else
    u32 handle; // GL texture name of the offscreen RGBA8 target (premultiplied coverage alpha)
#endif
    i32 width;
    i32 height;
} GpuSurfaceTexture;

GpuSurfaceTexture gpu_renderer_surface_texture(FlSurface* surface);

#if __APPLE__
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// IOSurface variant of the Surface-texture seam, Metal only. Returns the offscreen RGBA8 target's backing IOSurface
// instead of the id<MTLTexture> handle, so a host in a different process can open the same pixels into its own
// MTLTexture. The pixels are linear RGBA8 with premultiplied coverage alpha, identical to the primary accessor's.
//
// The first call on a surface upgrades its offscreen target to an IOSurface-backed MTLTexture at the current
// resolution, after which both accessors view the same resource; subsequent calls return the same handle. That first
// call rebuilds the target and so invalidates any handle previously fetched from gpu_renderer_surface_texture - latch
// the IOSurface before caching one. Backend-owned and valid until the next change_resolution for this surface; the
// host never retains or releases it.
//
// The handle crosses as an opaque void* carrying the retained IOSurfaceRef, for the consumer to cast back. It is NULL
// when the IOSurface-backed allocation fails: the surface keeps a working private-storage target and renders
// normally, but cannot be shared cross-process, and the host falls back to the primary accessor.

typedef struct GpuSurfaceIOSurface {
    void* handle; // opaque IOSurfaceRef backing the offscreen RGBA8 target (linear, premultiplied), backend-owned
    i32 width;
    i32 height;
} GpuSurfaceIOSurface;

GpuSurfaceIOSurface gpu_renderer_surface_iosurface(FlSurface* surface);
#endif
