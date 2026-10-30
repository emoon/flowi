#pragma once

#include "software_renderer_internal.h"
#include <ui/image.h>
#include <ui/render_backend.h>
#include <stdint.h>

struct FlArena;
struct FlFontCache;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// font_cache may be null at create time (injected later via ui_set_font_cache); render_mode fixes the
// rasterization target for the SW glyph cache, which is built lazily on first text resolve.
FlRenderBackend create_software_renderer(struct FlArena* arena, struct FlFontCache* font_cache,
                                         FontRenderMode render_mode);

#ifdef FLOWI_UI_INSPECTOR
// Deterministic per-frame work counters. render_commands is the core-side Clay command count, merged in by the
// caller from ui_get_render_command_count; the rest are the SW backend's, filled by sw_renderer_read_frame_stats
// after a rendered frame.
typedef struct FlFrameStats {
    uint32_t render_commands;
    uint32_t bin_alloc_bytes;
    uint32_t clip_broadcasts;
    uint32_t scaled_image_setups; // scaled-texture commands this frame = per-frame shuffle-table rebuilds
} FlFrameStats;

// Reads the last frame's SW work counters out of the backend's RenderData. Leaves render_commands untouched.
void sw_renderer_read_frame_stats(void* user_data, FlFrameStats* out);
#endif
