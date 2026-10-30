#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GPU backend: debug readback + debug render modes.
//
// Backend-private - not part of the FlRenderBackend seam. Callers pass the FlRenderBackend's user_data; no GL type
// crosses this header. Two facilities: readback of the classification pass's per-tile lists, and a raster-shader
// switch that visualizes the same per-tile data as a heat-map.
//
// On Metal, gpu_debug_read_tile_classification returns false, gpu_debug_set_mode is a no-op,
// gpu_debug_viewport3d_target reports found == false, and gpu_debug_read_frame_stats zeroes *out.

#include <core/types.h>
#include "gpu_glyph_atlas.h"
#include "gpu_svg_atlas.h"
#include <stdbool.h>
#include <stddef.h>

// Opaque backend-owned surface handle (render_backend.h); the tile-classification readback is per-surface.
typedef struct FlSurface FlSurface;

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tile-list layout, shared with both compute shaders: 8x8-pixel tiles, each owning a fixed-capacity index
// list laid out as [count, overflow, index * GPU_TILE_CAPACITY]. count is clamped to capacity; overflow
// counts the commands that did not fit and were dropped.

#define GPU_TILE_SIZE 8
#define GPU_TILE_CAPACITY 64
#define GPU_TILE_STRIDE_U32 (2 + GPU_TILE_CAPACITY)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The classification pass's output for one frame, read back verbatim. tile_lists is the raw SSBO contents -
// tile_cols * tile_rows records of GPU_TILE_STRIDE_U32 u32 each, in row-major tile order. List slots at and
// past a tile's count are unspecified (the shader never writes them).

typedef struct GpuTileClassification {
    const u32* tile_lists;
    int tile_cols;
    int tile_rows;
} GpuTileClassification;

static inline const u32* gpu_tile_list(const GpuTileClassification* c, int tile_x, int tile_y) {
    return c->tile_lists + ((size_t)tile_y * (size_t)c->tile_cols + (size_t)tile_x) * GPU_TILE_STRIDE_U32;
}

static inline u32 gpu_tile_count(const GpuTileClassification* c, int tile_x, int tile_y) {
    return gpu_tile_list(c, tile_x, tile_y)[0];
}

static inline u32 gpu_tile_overflow(const GpuTileClassification* c, int tile_x, int tile_y) {
    return gpu_tile_list(c, tile_x, tile_y)[1];
}

// The draw-command index in slot (0 <= slot < the tile's count).
static inline u32 gpu_tile_cmd_index(const GpuTileClassification* c, int tile_x, int tile_y, u32 slot) {
    return gpu_tile_list(c, tile_x, tile_y)[2 + slot];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Read the given surface's most recently rendered frame's classification results back to CPU memory, allocated
// from arena. The tile grid and its SSBO are per-surface. Requires the backend's GL context current and at
// least one render() call on that surface. Off the per-frame path - this stalls the pipeline like pixel readback.

bool gpu_debug_read_tile_classification(void* renderer_user_data, FlSurface* surface, struct FlArena* arena,
                                        GpuTileClassification* out);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug render modes - a raster-shader switch consuming the same per-tile data as the blend loop.

typedef enum GpuDebugMode {
    GpuDebug_Off = 0,     // normal rendering
    GpuDebug_TileHeatMap, // per-tile command count as a color ramp: transparent (empty) through green (cheap)
                          // to red (at capacity); overflowing tiles flagged magenta
} GpuDebugMode;

// Takes effect on the next render() call and stays until changed.
void gpu_debug_set_mode(void* renderer_user_data, GpuDebugMode mode);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Glyph-atlas readback: the R8 coverage texels verbatim as one width * height byte buffer (row 0 = texel row 0),
// plus the placement lookup. The atlas texture is never cleared, so texels outside prepared cells are undefined;
// a prepared cell is fully written by its dispatch, padding ring included.

typedef struct GpuGlyphAtlasReadback {
    const u8* texels;
    u32 width;
    u32 height;
} GpuGlyphAtlasReadback;

bool gpu_debug_read_glyph_atlas(void* renderer_user_data, struct FlArena* arena, GpuGlyphAtlasReadback* out);

// The CPU-side placement for a prepared glyph (NULL when nothing was prepared for the key). Valid until the
// next prepare_glyph call.
const GpuGlyphEntry* gpu_debug_find_glyph(void* renderer_user_data, FlFontId font, u16 size, u32 glyph_id);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SVG vector-icon atlas readback: the R8 coverage texels as one width * height byte buffer (row 0 = texel row 0),
// plus the placement lookup. Texels outside prepared cells are undefined, as in the glyph atlas above.

typedef struct GpuSvgAtlasReadback {
    const u8* texels;
    u32 width;
    u32 height;
} GpuSvgAtlasReadback;

bool gpu_debug_read_svg_atlas(void* renderer_user_data, struct FlArena* arena, GpuSvgAtlasReadback* out);

// The CPU-side placement for a prepared vector icon (NULL when nothing was prepared for the handle). Valid
// until the next prepare_vector call.
const GpuSvgEntry* gpu_debug_find_svg(void* renderer_user_data, FlImageHandle handle);

// Monotonic count of SVG cell-raster dispatches since backend create: ticks each time prepare_vector rasterizes a
// cell, flat when it resolves to the live cell unchanged.
u32 gpu_debug_svg_raster_count(void* renderer_user_data);

// Monotonic count of SVG-atlas compactions since backend create: ticks each time a full atlas is
// wholesale-compacted on a placement failure.
u32 gpu_debug_svg_compact_count(void* renderer_user_data);

// Monotonic count of dynamic-image texture uploads since backend create: ticks once per frame that references a
// dynamic slot entry. Statics never tick it - they upload at prepare_image, not per frame.
u32 gpu_debug_dynamic_upload_count(void* renderer_user_data);

// True when handle resolves to a cell of the shared image atlas rather than a texture of its own. An image
// created dedicated reports false at any size.
bool gpu_debug_image_in_atlas(void* renderer_user_data, FlImageHandle handle);

// The current color+depth target of the 3D viewport keyed by viewport_id, plus the number of live viewports.
// found is false when that id has no target yet, and every other field is then zero.
//
// The framebuffer and depth renderbuffer are deleted inline by a resize, but the color texture belongs to the
// image-slot table and is deleted on the next render, when the backend drains its dead list with the context
// current.

typedef struct GpuViewport3dTarget {
    bool found;
    u32 width;
    u32 height;
    u32 viewport_count; // live viewports on the backend, whether or not viewport_id was one of them
    u32 fbo;
    u32 depth_rb;
    u32 color_tex; // 0 when the color handle no longer resolves to a slot texture
} GpuViewport3dTarget;

GpuViewport3dTarget gpu_debug_viewport3d_target(void* renderer_user_data, u32 viewport_id);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Multi-color SVG atlas readback: the RGBA8 color texels as one width * height * 4 byte buffer of interleaved
// premultiplied RGBA (row 0 = texel row 0), plus the placement lookup. Texels outside prepared cells are undefined,
// as in the glyph atlas above; a prepared cell's padding ring composites to transparent.

typedef struct GpuColorSvgAtlasReadback {
    const u8* texels; // interleaved RGBA8, width * height * 4 bytes
    u32 width;
    u32 height;
} GpuColorSvgAtlasReadback;

bool gpu_debug_read_color_svg_atlas(void* renderer_user_data, struct FlArena* arena, GpuColorSvgAtlasReadback* out);

// The CPU-side placement for a prepared multi-color icon (NULL when nothing was prepared for the handle).
// Valid until the next prepare_vector call.
const GpuSvgEntry* gpu_debug_find_color_svg(void* renderer_user_data, FlImageHandle handle);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-frame GPU work counters, read back after a rendered frame.

typedef struct GpuFrameStats {
    u32 glyph_dispatches; // glyph cell-raster dispatches this frame
    u32 svg_dispatches;   // SVG cell-raster dispatches, monotonic since backend create - assert on a delta
    u32 tile_overflow;    // commands dropped past GPU_TILE_CAPACITY, summed over the surface's tiles this frame
} GpuFrameStats;

// Fill the GPU work counters for surface's most recently rendered frame; tile_overflow is summed from
// the per-surface classification SSBO read back into arena. Requires the backend's GL context current and at
// least one render() on the surface.
void gpu_debug_read_frame_stats(void* renderer_user_data, FlSurface* surface, struct FlArena* arena,
                                GpuFrameStats* out);
