#pragma once

#include <core/simd.h>
#include "software_renderer_internal.h"

static ALWAYS_INLINE SoaRgba splat_color(u8x16 color) {
    i16x8x2 corner_colors = u8x16_unpack_to_i16x8_wide_shl(color, 7);
    return (SoaRgba) {
        .r = i16x8_splat_lane(corner_colors.v0, 0),
        .g = i16x8_splat_lane(corner_colors.v0, 1),
        .b = i16x8_splat_lane(corner_colors.v0, 2),
        .a = i16x8_splat_lane(corner_colors.v0, 3),
    };
}

// One row of the tile's planar RGB output, positioned at the pixel the next 8-pixel group is read from and
// written to.
typedef struct PlaneRow {
    i16* line_output;
    int r_offset;
    int g_offset;
    int b_offset;
} PlaneRow;

static ALWAYS_INLINE PlaneRow plane_row_new(i16* line_output, int write_offset) {
    return (PlaneRow) {
        .line_output = line_output,
        .r_offset = write_offset,
        .g_offset = write_offset + PLANE_OFFSET_INCREMENT,
        .b_offset = write_offset + PLANE_OFFSET_INCREMENT * 2,
    };
}

static ALWAYS_INLINE i16x8 plane_load_r(const PlaneRow* row) {
    return i16x8_load_unaligned(row->line_output, row->r_offset);
}

static ALWAYS_INLINE i16x8 plane_load_g(const PlaneRow* row) {
    return i16x8_load_unaligned(row->line_output, row->g_offset);
}

static ALWAYS_INLINE i16x8 plane_load_b(const PlaneRow* row) {
    return i16x8_load_unaligned(row->line_output, row->b_offset);
}

static ALWAYS_INLINE SoaRgb plane_load_rgb(const PlaneRow* row) {
    return (SoaRgb) { .r = plane_load_r(row), .g = plane_load_g(row), .b = plane_load_b(row) };
}

static ALWAYS_INLINE SoaRgb lerp_over_background(SoaRgb color, const PlaneRow* row, i16x8 alpha) {
    color.r = i16x8_lerp(plane_load_r(row), color.r, alpha);
    color.g = i16x8_lerp(plane_load_g(row), color.g, alpha);
    color.b = i16x8_lerp(plane_load_b(row), color.b, alpha);
    return color;
}

static ALWAYS_INLINE void store_rgb(SoaRgb color, const PlaneRow* row) {
    i16x8_store_unaligned(color.r, row->line_output, row->r_offset);
    i16x8_store_unaligned(color.g, row->line_output, row->g_offset);
    i16x8_store_unaligned(color.b, row->line_output, row->b_offset);
}

// Store color into the xlen & 7 remainder pixels of the row only; the other lanes keep their current value.
static ALWAYS_INLINE void store_rgb_masked(SoaRgb color, i16x8 write_mask, const PlaneRow* row) {
    SoaRgb out;
    out.r = i16x8_select(write_mask, color.r, plane_load_r(row));
    out.g = i16x8_select(write_mask, color.g, plane_load_g(row));
    out.b = i16x8_select(write_mask, color.b, plane_load_b(row));
    store_rgb(out, row);
}

// Where a render function writes: the tile's planar output, the scissor rect in tile space and the tile's origin.
typedef struct TileTarget {
    i16* output;
    f32x4 scissor_rect;
    const TileInfo* tile;
} TileTarget;

// Tile-local pixel box of a command clipped to the scissor rect. clip_diff is how far the scissor cuts into the
// command rect on each axis, as a positive value.
typedef struct TileClip {
    int x0;
    int y0;
    int write_offset;
    int xlen;
    int ylen;
    i32x4 clip_diff;
} TileClip;

static ALWAYS_INLINE bool tile_clip_setup(f32x4 aabb, const TileTarget* target, TileClip* out) {
    f32x4 x0y0x1y1_adjust = f32x4_sub(aabb, target->tile->offsets);
    x0y0x1y1_adjust = f32x4_add(x0y0x1y1_adjust, f32x4_new_splat(0.5f));
    i32x4 x0y0x1y1_int = f32x4_as_i32x4(f32x4_floor(x0y0x1y1_adjust));

    i32x4 scissor_int = f32x4_as_i32x4(target->scissor_rect);
    i32x4 min_box = i32x4_min(x0y0x1y1_int, scissor_int);
    i32x4 max_box = i32x4_max(x0y0x1y1_int, scissor_int);

    out->x0 = i32x4_extract(max_box, 0);
    out->y0 = i32x4_extract(max_box, 1);
    out->xlen = i32x4_extract(min_box, 2) - out->x0;
    out->ylen = i32x4_extract(min_box, 3) - out->y0;
    out->write_offset = (out->y0 * TILE_STRIDE) + out->x0;
    out->clip_diff = i32x4_abs(i32x4_min(i32x4_sub(x0y0x1y1_int, scissor_int), i32x4_new_splat(0)));

    return out->xlen > 0 && out->ylen > 0;
}
