// GCC -fanalyzer false positives on SIMD intrinsic wrappers
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 12
#pragma GCC diagnostic ignored "-Wanalyzer-use-of-uninitialized-value"
#endif

#include <core/core.h>
#include <core/math.h>
#include <core/log.h>
#include <core/simd.h>
#include "calculate_edge_rounding.h"
#include "sr_command.h"
#include "software_renderer_internal.h"
#include "sr_gradient_interpolation.h"
#include <math.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE SoaRgba splat_color(u8x16 color) {
    i16x8x2 corner_colors = u8x16_unpack_to_i16x8_wide_shl(color, 7);
    return (SoaRgba) {
        .r = i16x8_splat_lane(corner_colors.v0, 0),
        .g = i16x8_splat_lane(corner_colors.v0, 1),
        .b = i16x8_splat_lane(corner_colors.v0, 2),
        .a = i16x8_splat_lane(corner_colors.v0, 3),
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE SoaRgba process_8_pixels(SoaRgba fixed_color, const bool gradient, const bool rounding,
                                              const bool blend_with_background,
                                              GradientInterpolationState* gradient_state,
                                              CornerRoundingState* rounding_state, i16* line_output, int r_offset,
                                              int g_offset, int b_offset) {
    SoaRgba color = fixed_color;

    if (gradient) {
        color = gradient_interpolation_step_x(gradient_state);
    }

    if (rounding) {
        i16x8 corner_alpha = corner_rounding_step_8(rounding_state);

        i16x8 bg_r = i16x8_load_unaligned(line_output, r_offset);
        i16x8 bg_g = i16x8_load_unaligned(line_output, g_offset);
        i16x8 bg_b = i16x8_load_unaligned(line_output, b_offset);

        color.r = i16x8_lerp(bg_r, color.r, corner_alpha);
        color.g = i16x8_lerp(bg_g, color.g, corner_alpha);
        color.b = i16x8_lerp(bg_b, color.b, corner_alpha);
    }

    if (blend_with_background) {
        i16x8 bg_r = i16x8_load_unaligned(line_output, r_offset);
        i16x8 bg_g = i16x8_load_unaligned(line_output, g_offset);
        i16x8 bg_b = i16x8_load_unaligned(line_output, b_offset);

        color.r = i16x8_lerp(bg_r, color.r, color.a);
        color.g = i16x8_lerp(bg_g, color.g, color.a);
        color.b = i16x8_lerp(bg_b, color.b, color.a);
    }

    return color;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE SoaRgba process_8_pixels_border(SoaRgba fixed_color, const bool blend_with_background,
                                                     CornerRoundingState* rounding_state, i16* line_output,
                                                     int r_offset, int g_offset, int b_offset) {
    SoaRgba color = fixed_color;

    i16x8 corner_alpha = corner_rounding_step_8_dual(rounding_state);

    i16x8 bg_r = i16x8_load_unaligned(line_output, r_offset);
    i16x8 bg_g = i16x8_load_unaligned(line_output, g_offset);
    i16x8 bg_b = i16x8_load_unaligned(line_output, b_offset);

    color.r = i16x8_lerp(bg_r, color.r, corner_alpha);
    color.g = i16x8_lerp(bg_g, color.g, corner_alpha);
    color.b = i16x8_lerp(bg_b, color.b, corner_alpha);

    if (blend_with_background) {
        color.r = i16x8_lerp(bg_r, color.r, color.a);
        color.g = i16x8_lerp(bg_g, color.g, color.a);
        color.b = i16x8_lerp(bg_b, color.b, color.a);
    }

    return color;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Tile-local pixel box of a command clipped to the scissor rect. clip_diff is how far the scissor cuts into the
// command rect on each axis, as a positive value.
typedef struct FillClip {
    int write_offset;
    int xlen;
    int ylen;
    i32x4 clip_diff;
} FillClip;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE bool fill_clip_setup(const RenderCommand* command, f32x4 scissor_rect, const TileInfo* tile_info,
                                          FillClip* out) {
    f32x4 x0y0x1y1_adjust = f32x4_sub(command->aabb, tile_info->offsets);
    x0y0x1y1_adjust = f32x4_add(x0y0x1y1_adjust, f32x4_new_splat(0.5f));
    i32x4 x0y0x1y1_int = f32x4_as_i32x4(f32x4_floor(x0y0x1y1_adjust));

    i32x4 scissor_int = f32x4_as_i32x4(scissor_rect);
    i32x4 min_box = i32x4_min(x0y0x1y1_int, scissor_int);
    i32x4 max_box = i32x4_max(x0y0x1y1_int, scissor_int);

    int x0 = i32x4_extract(max_box, 0);
    int y0 = i32x4_extract(max_box, 1);
    out->xlen = i32x4_extract(min_box, 2) - x0;
    out->ylen = i32x4_extract(min_box, 3) - y0;
    out->write_offset = (y0 * TILE_STRIDE) + x0;
    out->clip_diff = i32x4_abs(i32x4_min(i32x4_sub(x0y0x1y1_int, scissor_int), i32x4_new_splat(0)));

    return out->xlen > 0 && out->ylen > 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Store color into the xlen & 7 remainder pixels of the row only; the other lanes keep their current value.
static ALWAYS_INLINE void store_rgb_masked(SoaRgba color, i16x8 write_mask, i16* line_output, int r_offset,
                                           int g_offset, int b_offset) {
    i16x8 out_r = i16x8_select(write_mask, color.r, i16x8_load_unaligned(line_output, r_offset));
    i16x8 out_g = i16x8_select(write_mask, color.g, i16x8_load_unaligned(line_output, g_offset));
    i16x8 out_b = i16x8_select(write_mask, color.b, i16x8_load_unaligned(line_output, b_offset));

    i16x8_store_unaligned(out_r, line_output, r_offset);
    i16x8_store_unaligned(out_g, line_output, g_offset);
    i16x8_store_unaligned(out_b, line_output, b_offset);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void fill_internal(const bool blend_with_background, const bool gradient, const bool rounding,
                                        i16* output, f32x4 scissor_rect, const TileInfo* tile_info,
                                        const RenderCommand* command) {
    CornerRoundingState rounding_state;
    GradientInterpolationState gradient_state;

    FillClip clip;
    if (!fill_clip_setup(command, scissor_rect, tile_info, &clip)) {
        return;
    }

    const i32x4 clip_diff = clip.clip_diff;
    const int xlen = clip.xlen;
    const int ylen = clip.ylen;
    const int write_offset = clip.write_offset;

    SoaRgba fixed_color = splat_color(command->corner_colors);
    i16x8 write_mask = i16x8_write_mask(xlen);

    if (gradient) {
        i16x8 clip_diff_i16 = i16x8_add(i32x4_pack_i16x8(clip_diff), command->clip_adjust);
        gradient_state = gradient_interpolation_tile_setup((GradientQuadState*)command->gradient_data, clip_diff_i16);
    }

    if (rounding) {
        const float corner_radius = command->radius;
        const int corner_direction = render_mask_corner_of(command->mask) - 1;
        const int y_diff = i32x4_extract(clip_diff, 1);

        corner_rounding_init(&rounding_state, y_diff, corner_radius, corner_direction);
    }

    int r_offset = write_offset;
    int g_offset = write_offset + PLANE_OFFSET_INCREMENT;
    int b_offset = write_offset + PLANE_OFFSET_INCREMENT * 2;

    for_count(y, ylen) {
        i16* line_output = output;

        if (rounding) {
            corner_rounding_step_y(&rounding_state, clip_diff);
        }

        if (gradient) {
            gradient_interpolation_step_y(&gradient_state);
        }

        for_count(x, (xlen >> 3)) {
            SoaRgba color = process_8_pixels(fixed_color, gradient, rounding, blend_with_background, &gradient_state,
                                             &rounding_state, line_output, r_offset, g_offset, b_offset);

            i16x8_store_unaligned(color.r, line_output, r_offset);
            i16x8_store_unaligned(color.g, line_output, g_offset);
            i16x8_store_unaligned(color.b, line_output, b_offset);

            line_output += 8;
        }

        if ((xlen & 0x7) > 0) {
            SoaRgba color = process_8_pixels(fixed_color, gradient, rounding, blend_with_background, &gradient_state,
                                             &rounding_state, line_output, r_offset, g_offset, b_offset);

            store_rgb_masked(color, write_mask, line_output, r_offset, g_offset, b_offset);
        }

        output += TILE_STRIDE;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void fill_border_internal(const bool blend_with_background, i16* output, f32x4 scissor_rect,
                                               const TileInfo* tile_info, const RenderCommand* command) {
    CornerRoundingState rounding_state;

    FillClip clip;
    if (!fill_clip_setup(command, scissor_rect, tile_info, &clip)) {
        return;
    }

    const i32x4 clip_diff = clip.clip_diff;
    const int xlen = clip.xlen;
    const int ylen = clip.ylen;
    const int write_offset = clip.write_offset;

    SoaRgba fixed_color = splat_color(command->corner_colors);
    i16x8 write_mask = i16x8_write_mask(xlen);

    const float outer_radius = command->radius;
    const float inner_radius = command->inner_radius;
    const int corner_direction = render_mask_corner_of(command->mask) - 1;
    const int y_diff = i32x4_extract(clip_diff, 1);

    corner_rounding_init_dual(&rounding_state, y_diff, outer_radius, inner_radius, corner_direction);

    int r_offset = write_offset;
    int g_offset = write_offset + PLANE_OFFSET_INCREMENT;
    int b_offset = write_offset + PLANE_OFFSET_INCREMENT * 2;

    for_count(y, ylen) {
        i16* line_output = output;

        corner_rounding_step_y(&rounding_state, clip_diff);

        for_count(x, (xlen >> 3)) {
            SoaRgba color = process_8_pixels_border(fixed_color, blend_with_background, &rounding_state, line_output,
                                                    r_offset, g_offset, b_offset);

            i16x8_store_unaligned(color.r, line_output, r_offset);
            i16x8_store_unaligned(color.g, line_output, g_offset);
            i16x8_store_unaligned(color.b, line_output, b_offset);

            line_output += 8;
        }

        if ((xlen & 0x7) > 0) {
            SoaRgba color = process_8_pixels_border(fixed_color, blend_with_background, &rounding_state, line_output,
                                                    r_offset, g_offset, b_offset);

            store_rgb_masked(color, write_mask, line_output, r_offset, g_offset, b_offset);
        }

        output += TILE_STRIDE;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void fill_border(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect,
                                      const TileInfo* tile) {
    fill_border_internal(false, tile_output, scissor_rect, tile, command);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void fill_border_blend(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect,
                                            const TileInfo* tile) {
    fill_border_internal(true, tile_output, scissor_rect, tile, command);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void fill_solid(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect,
                                     const TileInfo* tile) {
    fill_internal(false, false, false, tile_output, scissor_rect, tile, command);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void fill_solid_rounded(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect,
                                             const TileInfo* tile) {
    fill_internal(false, false, true, tile_output, scissor_rect, tile, command);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void fill_gradient(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect,
                                        const TileInfo* tile) {
    fill_internal(false, true, false, tile_output, scissor_rect, tile, command);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void fill_gradient_rounded(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect,
                                                const TileInfo* tile) {
    fill_internal(false, true, true, tile_output, scissor_rect, tile, command);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void fill_solid_blend(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect,
                                           const TileInfo* tile) {
    fill_internal(true, false, false, tile_output, scissor_rect, tile, command);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void fill_solid_rounded_blend(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect,
                                                   const TileInfo* tile) {
    fill_internal(true, false, true, tile_output, scissor_rect, tile, command);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void fill_gradient_blend(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect,
                                              const TileInfo* tile) {
    fill_internal(true, true, false, tile_output, scissor_rect, tile, command);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void fill_gradient_rounded_blend(i16* tile_output, const RenderCommand* command,
                                                      f32x4 scissor_rect, const TileInfo* tile) {
    fill_internal(true, true, true, tile_output, scissor_rect, tile, command);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef void (*fill_func_t)(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect, const TileInfo* tile);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Lookup table for all 8 permutations: [blend_with_background][gradient][rounding]

// clang-format off
static const fill_func_t fill_dispatch_table[2][2][2] = {
    // blend_with_background = false
    { // gradient = false
      { fill_solid, fill_solid_rounded },
      // gradient = true
      { fill_gradient, fill_gradient_rounded } 
    },
    // blend_with_background = true
    { // gradient = false
      { fill_solid_blend, fill_solid_rounded_blend },
      // gradient = true
      { fill_gradient_blend, fill_gradient_rounded_blend } 
    }
};
// clang-format on

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void sr_render_fill(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect, const TileInfo* tile) {
    u32 render_mask = command->mask;
    int rounding = !!(render_mask & RenderMask_Corners);
    int gradient = !!(render_mask & RenderMask_ColorGradient);
    int blend_with_background = !!(render_mask & RenderMask_BlendWithBackground);

    fill_dispatch_table[blend_with_background][gradient][rounding](tile_output, command, scissor_rect, tile);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Lookup table for border rendering: [blend_with_background]

// clang-format off
static const fill_func_t border_dispatch_table[2] = {
    fill_border,
    fill_border_blend
};
// clang-format on

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void sr_render_border(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect, const TileInfo* tile) {
    int blend_with_background = !!(command->mask & RenderMask_BlendWithBackground);
    border_dispatch_table[blend_with_background](tile_output, command, scissor_rect, tile);
}
