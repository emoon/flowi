#pragma once

// Software backend: neutral render IR (ui/render_ir.h) -> internal RenderCommand list.
//
// Font ids and image handles are resolved here, backend-side - the IR never carries baked glyph bitmaps or
// ImageData pointers.

#include "software_renderer_internal.h"
#include <stddef.h>

struct FlArena;
struct SwGlyphCache;

// One translation request. Every field is required except glyph_cache.
// commands_arena: receives the contiguous RenderCommand array (the returned .commands).
// state_arena:    per-frame scratch for image conversion + gradient/scaled-texture setup.
// glyph_cache:    may be null before a font cache is injected, in which case text runs are skipped.
// ir_commands:    FlCmdHeader-prefixed records; ir_bytes is their total size.
// global_alpha:   frame-wide opacity, folded in here (never pre-folded in the IR).
typedef struct SrIrRequest {
    struct FlArena* commands_arena;
    struct FlArena* state_arena;
    struct SwGlyphCache* glyph_cache;
    const void* ir_commands;
    size_t ir_bytes;
    u8 global_alpha;
} SrIrRequest;

// Translates the request's IR records into RenderCommands and runs sr_command_setup on each. Returns the new
// commands: they live in req->commands_arena, which owns them, and the array is empty when the IR holds no
// core-tier command. req is only read during the call and may be a temporary.
UiRenderCommandsArray sr_ir_to_render_commands(const SrIrRequest* req);
