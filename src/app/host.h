#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fl_host - the embedded flowi tier for a host that owns its own OS integration.
//
// fl_host brings up everything a flowi frame needs except the window, the input pump and presentation: the
// render backend, the app-level shared resource tier, the process-global VFS and image-system singletons, and
// the shared font cache. Per window it creates a render surface and a UI context. fl_app is built on this and
// adds the window backend, the tick loop and the present path.
//
// Ownership: the host allocates from the caller's arena and never frees it. fl_vfs_init and image_create_system
// are process-global and first-caller-wins, so fl_host_destroy tears down only what this host brought up -
// create/destroy/create cycles work, and a pre-existing owner's globals are never yanked out from under it.

#include <stdbool.h>

#include <core/types.h>
#include <flowi/app/host.h> // @generated FlHostSettings (api_gen)
#include <ui/render_backend.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FlHost FlHost;
typedef struct FlArena FlArena;
typedef struct FlUiCtx FlUiCtx;

// These are the INTERNAL entry points, and the ones flowi's own code must call. They are also wider than the
// exported `fl_host_*` set (flowi/app/host.h, generated), which is just create / destroy / frame_update /
// read_pixels: the per-window pieces below exist for fl_app.
//
// `fl_host_*` is on the shared library's exported symbol list, so in a process that both links flowi statically
// and dlopens libflowi.so - which the shared-ABI tests do - a call to the exported name from inside the .so is
// interposed by the executable's definition, bringing up the executable's VFS and image-system globals and
// leaving its own null. Calling the internal name keeps every intra-library call bound locally.

// adopt_backend is a finished render backend the caller built against something it owns (fl_app's windowed-GPU
// path builds against its SDL-GL surface); the host copies it and the caller keeps ownership. Null makes the host
// create the software renderer and own it.
FlHost* host_create(FlArena* arena, const FlHostSettings* settings, const FlRenderBackend* adopt_backend);
void host_destroy(FlHost* host);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-window: one surface + one UI context. A sized host_create does both for its own window; fl_app calls
// them per OS window instead.

FlSurface* host_surface_create(FlHost* host, i32 width, i32 height);
void host_surface_destroy(FlHost* host, FlSurface* surface);

// Attach a UI context to surface: its own layout tree, input and focus, bound to the shared tier, with the
// shared font cache and default font active, at the given render resolution. The context becomes current.
FlUiCtx* host_window_attach(FlHost* host, FlSurface* surface, i32 width, i32 height);
void host_window_detach(FlHost* host, FlUiCtx* ctx);

// Read back the last-rendered pixels of surface (native interleaved - RGB24 software, RGBA8 GPU) so the
// host can copy them into its own framebuffer. False when the backend has no readback capability.
bool host_read_pixels(FlHost* host, FlSurface* surface, FlPixelReadback* out);
