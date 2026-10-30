#pragma once

#include <core/array.h>
#include <core/string.h>
#include <core/simd.h>
#include <core/types.h>
#include <ui/ui_types.h>
#include "sr_command.h"
#include "sw_glyph_cache.h"
#include <ui/image.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FlFontCache;
struct FlArena;
struct Clay_RenderCommand;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Software-renderer-internal command list: the IR->SW translator's output, consumed by bin_tiles.

typedef struct UiRenderCommandsArray {
    RenderCommand* commands;
    int count;
} UiRenderCommandsArray;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define TILE_WIDTH 64
#define TILE_HEIGHT 64
#define TILE_STRIDE (TILE_WIDTH + 15)

#define PLANE_OFFSET_INCREMENT (TILE_STRIDE * TILE_WIDTH)
#define R_PLANE_START_OFFSET 0
#define G_PLANE_START_OFFSET (R_PLANE_START_OFFSET + PLANE_OFFSET_INCREMENT)
#define B_PLANE_START_OFFSET (G_PLANE_START_OFFSET + PLANE_OFFSET_INCREMENT)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct SoaRgba {
    i16x8 r;
    i16x8 g;
    i16x8 b;
    i16x8 a;
} SoaRgba;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct SoaRgb {
    i16x8 r;
    i16x8 g;
    i16x8 b;
} SoaRgb;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct TileInfo {
    f32x4 offsets;
} TileInfo;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct TileRange {
    int tx0, ty0, tx1, ty1;
} TileRange;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compute which tiles an AABB overlaps: truncate to int, divide by the 64-pixel tile size via shift, clamp
// to grid bounds.

static inline TileRange tile_range_from_aabb(f32x4 aabb, i32x4 max_tiles) {
    i32x4 zero = i32x4_new_splat(0);
    i32x4 pixels = f32x4_as_i32x4(aabb);
    i32x4 tiles = i32x4_shift_right(pixels, 6);
    tiles = i32x4_max(tiles, zero);
    tiles = i32x4_min(tiles, max_tiles);
    return (TileRange) {
        .tx0 = i32x4_extract(tiles, 0),
        .ty0 = i32x4_extract(tiles, 1),
        .tx1 = i32x4_extract(tiles, 2),
        .ty1 = i32x4_extract(tiles, 3),
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct Tile {
    u64 hash;
    u64 prev_hash;
    array(render_ids, u32);
} Tile;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct BackgroundState {
    ImageHandle image_handle;      // Handle for current/incoming image
    ImageHandle prev_image_handle; // Handle for previous/outgoing image (crossfade)
    u8 alpha;                      // Current image alpha: 0=transparent, 255=opaque (default: 255)
    u8 crossfade_t;                // Crossfade progress: 0=full prev, 255=full current
    bool crossfade_active;
    u8 clear_r; // Clear color, used when no background image
    u8 clear_g;
    u8 clear_b;
} BackgroundState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A SW surface is its output pixel buffer plus the size-derived tile grid. One backend owns many of these;
// the shared resources (tile scratch, glyph/image caches, arenas) live on RenderData.
typedef struct SwSurface {
    struct FlArena* arena; // per-surface; backs the SwSurface struct itself + output buffer + tile grid,
                           // rewound (above the pinned struct) on change_resolution, destroyed with the surface

    u8* rgb24_output_buffer;
    f32x4* tiles_aabb;
    array(tiles_info, Tile);

    int screen_width;
    int screen_height;
} SwSurface;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct RenderData {
    struct FlArena* perm_arena; // host arena (passed to create); backs RenderData + the glyph cache (not owned)
    struct FlArena* frame_arena;
    struct FlArena* frame_arena_2;
    struct FlArena*
        tile_arena; // Fixed-size per-tile scratch; allocated once, freed in destroy (resolution-independent)

    i16* tile_buffer;

    RenderCommand* render_commands; // per-frame scratch (IR translation output), valid only during render()

    struct FlFontCache* font_cache;   // Shared font layer (metrics + raster sources); injected via ui_set_font_cache
    struct SwGlyphCache* glyph_cache; // SW glyph realization, built lazily from font_cache + render_mode
    FontRenderMode render_mode;       // Rasterization target, fixed at create time

    struct ImageCache* image_cache; // SW realized-pixel pool: created with this backend, freed in destroy
    u64 image_cache_frame;          // Per-frame counter driving the pool's eviction LRU + cleanup cadence

    BackgroundState background; // Per-frame snapshot, populated from FlRenderFrame.background each render()

#ifdef FLOWI_UI_INSPECTOR
    // Perf work counters: reset and refilled every bin_tiles(), read back via sw_renderer_read_frame_stats.
    u32 stat_bin_alloc_bytes;     // tile_count * cmd_count * sizeof(u32): the bin_tiles render-id allocation
    u32 stat_clip_broadcasts;     // total clip-command appends across all tiles (the O(clips * tiles) fan-out)
    u32 stat_scaled_image_setups; // scaled-texture commands this frame = per-frame shuffle-table rebuilds
#endif
} RenderData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Render functions

void sr_render_fill(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect, const TileInfo* info);
void sr_render_border(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect, const TileInfo* info);
void sr_render_image_nearest(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect, const TileInfo* info);
void sr_render_image_scale(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect, const TileInfo* info);
void sr_render_text(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect, const TileInfo* info);
