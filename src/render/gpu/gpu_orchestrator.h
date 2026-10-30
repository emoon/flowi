#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared GPU frame orchestrator: the one canonical per-frame sequence both the GL and the Metal backend drive.
// The GL/EGL path (gpu_renderer.c) and the Metal path (gpu_metal.m) compile on mutually exclusive platforms,
// so the sequence lives in one backend-agnostic TU (gpu_orchestrator.c) compiled into both, calling
// per-backend primitives through the GpuRenderOps table below.
//
// The seam is backend-internal - no GL or Metal type crosses it (GpuRenderOps is plain function pointers over
// void* backend/surface handles). This header must stay GL-type-free: gpu_renderer_internal.h, which pulls in GL
// types, cannot be included by the Metal build.

#include "gpu_glyph_atlas.h"
#include "gpu_ir_to_draw_cmd.h"
#include <core/types.h>
#include <flowi/ui/font_id.h>

struct FlArena;
struct FlGlyphOutline;
struct FlRenderFrame;
struct FlFontCache;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-backend primitive table. Every entry takes the opaque backend handle (GpuShared* / MetalShared*) it casts
// back internally; the encoder-lifecycle entries also take the opaque surface handle (GpuSurface* / MetalSurface*).
// gpu_orchestrate_frame calls these in a fixed order (see gpu_orchestrator.c). GL leaves begin_encoder a no-op
// (it binds directly to the current context) and drives ordering with a GL memory barrier; Metal owns the
// MTLComputeCommandEncoder lifecycle in begin_encoder / end_encoder.

typedef struct GpuRenderOps {
    // Frame preamble, before any glyph realization: advances the image-slot table (evicting stale dynamic
    // entries) and releases the textures freed since the last render. GL also resets its per-frame counters.
    void (*frame_begin)(void* backend);

    // Glyph realization (driven by the shared prepare_text_glyphs walk in gpu_orchestrator.c): the bound font
    // cache, and the seam that rasterizes one outline into the coverage atlas. get_font_cache returns NULL when
    // no cache is bound (a host driving prepare_glyph itself), which short-circuits the walk.
    struct FlFontCache* (*get_font_cache)(void* backend);
    void (*prepare_glyph)(void* backend, FlFontId font, u16 size, u32 glyph_id, const struct FlGlyphOutline* outline);

    // Translate the neutral IR into the backend's draw-command / AABB / clip arrays (gpu_ir_to_draw_cmds). Each
    // backend passes its own atlas/slot/SVG bookkeeping and logs its own overflow.
    GpuDrawList (*translate)(void* backend, const struct FlRenderFrame* frame, u32 screen_w, u32 screen_h);

    // Encoder lifecycle + the two-dispatch compute pipeline. begin_encoder opens the frame's command encoding
    // (Metal: command buffer + compute encoder; GL: no-op). upload_inputs streams the draw arrays into the
    // classify/raster input buffers. classify runs pass 1 (one thread per tile); barrier orders its tile-list
    // writes before pass 2; raster runs pass 2 (one workgroup per tile) with the frame's textures bound.
    // end_encoder closes the frame (GL: fence the target's read access classes; Metal: end encoding, commit,
    // wait, release the per-frame upload buffers).
    void (*begin_encoder)(void* backend, void* surface);
    void (*upload_inputs)(void* backend, const GpuDrawList* list);
    void (*classify)(void* backend, void* surface, u32 cmd_count);
    void (*barrier)(void* backend);
    void (*raster)(void* backend, void* surface);
    void (*end_encoder)(void* backend);
} GpuRenderOps;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Run one frame through the canonical sequence: frame_begin -> realize the frame's text glyphs -> translate the
// IR -> the two-dispatch compute pipeline -> the deferred glyph-atlas overflow wipe -> rewind the frame arena.
// backend and surface are the opaque handles handed to every op; glyph_atlas / frame_arena / perm_arena are
// the backend-agnostic shared state the end-of-frame steps touch directly.

void gpu_orchestrate_frame(const GpuRenderOps* ops, void* backend, void* surface, const struct FlRenderFrame* frame,
                           u32 screen_w, u32 screen_h, GpuGlyphAtlas* glyph_atlas, struct FlArena* frame_arena,
                           struct FlArena* perm_arena);
