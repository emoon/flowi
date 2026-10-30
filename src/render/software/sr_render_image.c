// GCC -fanalyzer false positives on SIMD intrinsic wrappers
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 12
#pragma GCC diagnostic ignored "-Wanalyzer-use-of-uninitialized-value"
#endif

#include <core/log.h>
#include <core/simd.h>
#include "software_renderer_internal.h"
#include "sr_image_blend.h"
#include <ui/image.h>
#include <math.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Nearest-neighbor (point-sampled) image rendering.

static ALWAYS_INLINE SoaRgb process_8_pixels_nearest(const bool blend_with_background, const bool rounding,
                                                     const int channel_count, CornerRoundingState* rounding_state,
                                                     const u8* image_line_ptr, int image_r_plane, int image_g_plane,
                                                     int image_b_plane, int image_a_plane, i16* line_output,
                                                     int write_offset, SoaRgba in_color, i16x8 global_alpha) {
    i16x8 r_res, g_res, b_res, a_res;

    const int r_offset = write_offset;
    const int g_offset = write_offset + PLANE_OFFSET_INCREMENT;
    const int b_offset = write_offset + PLANE_OFFSET_INCREMENT * 2;

    if (channel_count == 1) {
        // Single alpha channel - load single texel row (no vertical neighbor)
        a_res = u8x8_load_to_i16x8_wide_shl(image_line_ptr, image_r_plane, 7);
        a_res = i16x8_lerp(i16x8_new_splat(0), a_res, global_alpha);

        i16x8 bg_r = i16x8_load_unaligned(line_output, r_offset);
        i16x8 bg_g = i16x8_load_unaligned(line_output, g_offset);
        i16x8 bg_b = i16x8_load_unaligned(line_output, b_offset);

        return (SoaRgb) {
            .r = i16x8_lerp(bg_r, in_color.r, a_res),
            .g = i16x8_lerp(bg_g, in_color.g, a_res),
            .b = i16x8_lerp(bg_b, in_color.b, a_res),
        };
    } else {
        // RGB or RGBA - single texel load per pixel (no interpolation)
        r_res = u8x8_load_to_i16x8_wide_shl(image_line_ptr, image_r_plane, 7);
        g_res = u8x8_load_to_i16x8_wide_shl(image_line_ptr, image_g_plane, 7);
        b_res = u8x8_load_to_i16x8_wide_shl(image_line_ptr, image_b_plane, 7);

        if (channel_count == 4) {
            a_res = u8x8_load_to_i16x8_wide_shl(image_line_ptr, image_a_plane, 7);
            // Modulate image alpha by global alpha for fade transitions
            a_res = i16x8_lerp(i16x8_new_splat(0), a_res, global_alpha);
        } else {
            a_res = global_alpha;
        }
    }

    SoaRgb color = (SoaRgb) { .r = r_res, .g = g_res, .b = b_res };

    if (channel_count == 4) {
        i16x8 bg_r = i16x8_load_unaligned(line_output, r_offset);
        i16x8 bg_g = i16x8_load_unaligned(line_output, g_offset);
        i16x8 bg_b = i16x8_load_unaligned(line_output, b_offset);
        return premul_over_8(rounding, rounding_state, r_res, g_res, b_res, a_res, global_alpha, bg_r, bg_g, bg_b);
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

        color.r = i16x8_lerp(bg_r, color.r, a_res);
        color.g = i16x8_lerp(bg_g, color.g, a_res);
        color.b = i16x8_lerp(bg_b, color.b, a_res);
    }

    return color;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Nearest-neighbor inner loop

static ALWAYS_INLINE void image_internal_nearest(const bool blend_with_background, const bool rounding,
                                                 const int channel_count, i16* output, f32x4 scissor_rect,
                                                 const TileInfo* tile_info, const RenderCommand* command) {
    const ImageData* image = command->image;
    CornerRoundingState rounding_state;

    SoaRgba color = splat_color(command->corner_colors);

    i16x8 global_alpha = i16x8_new_splat((i16)((command->global_alpha * 0x7FFF) / 255));

    int texture_stride = image->padded_width;

    f32x4 x0y0x1y1_adjust = f32x4_sub(command->aabb, tile_info->offsets);
    x0y0x1y1_adjust = f32x4_add(x0y0x1y1_adjust, f32x4_new_splat(0.5f));
    f32x4 x0y0x1y1 = f32x4_floor(x0y0x1y1_adjust);
    i32x4 x0y0x1y1_int = f32x4_as_i32x4(x0y0x1y1);

    i32x4 scissor_int = f32x4_as_i32x4(scissor_rect);
    i32x4 min_box = i32x4_min(x0y0x1y1_int, scissor_int);
    i32x4 max_box = i32x4_max(x0y0x1y1_int, scissor_int);

    int32_t x0 = i32x4_extract(max_box, 0);
    int32_t y0 = i32x4_extract(max_box, 1);
    int32_t x1 = i32x4_extract(min_box, 2);
    int32_t y1 = i32x4_extract(min_box, 3);

    int ylen = (int)(y1 - y0);
    int xlen = (int)(x1 - x0);

    if (ylen <= 0 || xlen <= 0) {
        return;
    }

    i32x4 clip_diff = i32x4_sub(x0y0x1y1_int, scissor_int);
    clip_diff = i32x4_min(clip_diff, i32x4_new_splat(0));
    clip_diff = i32x4_abs(clip_diff);

    int clip_x = (int)i32x4_extract(clip_diff, 0);
    int clip_y = (int)i32x4_extract(clip_diff, 1);

    int u = command->image_src_x + clip_x + i16x8_extract(command->clip_adjust, 0);
    int v = command->image_src_y + clip_y + i16x8_extract(command->clip_adjust, 1);

    int plane_size = texture_stride * (int)image->padded_height;

    int offset = v * texture_stride + u;
    const u8* image_ptr_offset = image->data + offset;
    int image_r_plane = 0;
    int image_g_plane = image_r_plane + plane_size;
    int image_b_plane = image_g_plane + plane_size;
    int image_a_plane = image_b_plane + plane_size;

    if (rounding) {
        const float corner_radius = command->radius;
        const int corner_direction = render_mask_corner_of(command->mask) - 1;
        const int y_diff = i32x4_extract(clip_diff, 1);

        corner_rounding_init(&rounding_state, y_diff, corner_radius, corner_direction);
    }

    int write_offset = (int)y0 * TILE_STRIDE + (int)x0;

    i16* output_ptr = output;
    i16x8 write_mask = i16x8_write_mask(xlen);

    for_count(y, ylen) {
        const u8* image_line_ptr = image_ptr_offset + y * texture_stride;
        i16* output_line_ptr = output_ptr + y * TILE_STRIDE;

        if (rounding) {
            corner_rounding_step_y(&rounding_state, clip_diff);
        }

        for_count(x, (xlen >> 3)) {
            SoaRgb res = process_8_pixels_nearest(blend_with_background, rounding, channel_count, &rounding_state,
                                                  image_line_ptr, image_r_plane, image_g_plane, image_b_plane,
                                                  image_a_plane, output_line_ptr, write_offset, color, global_alpha);

            i16x8_store_unaligned(res.r, output_line_ptr, write_offset);
            i16x8_store_unaligned(res.g, output_line_ptr, write_offset + PLANE_OFFSET_INCREMENT);
            i16x8_store_unaligned(res.b, output_line_ptr, write_offset + PLANE_OFFSET_INCREMENT * 2);

            image_line_ptr += 8;
            output_line_ptr += 8;
        }

        if ((xlen & 0x7) > 0) {
            SoaRgb res = process_8_pixels_nearest(blend_with_background, rounding, channel_count, &rounding_state,
                                                  image_line_ptr, image_r_plane, image_g_plane, image_b_plane,
                                                  image_a_plane, output_line_ptr, write_offset, color, global_alpha);

            i16x8 out_r = i16x8_load_unaligned(output_line_ptr, write_offset);
            i16x8 out_g = i16x8_load_unaligned(output_line_ptr, write_offset + PLANE_OFFSET_INCREMENT);
            i16x8 out_b = i16x8_load_unaligned(output_line_ptr, write_offset + PLANE_OFFSET_INCREMENT * 2);

            out_r = i16x8_select(write_mask, res.r, out_r);
            out_g = i16x8_select(write_mask, res.g, out_g);
            out_b = i16x8_select(write_mask, res.b, out_b);

            i16x8_store_unaligned(out_r, output_line_ptr, write_offset);
            i16x8_store_unaligned(out_g, output_line_ptr, write_offset + PLANE_OFFSET_INCREMENT);
            i16x8_store_unaligned(out_b, output_line_ptr, write_offset + PLANE_OFFSET_INCREMENT * 2);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Nearest-neighbor wrapper functions

// 1-channel (alpha-only) permutations
static ALWAYS_INLINE void render_image_nearest_solid_1ch(i16* tile_output, const RenderCommand* command,
                                                         f32x4 scissor_rect, const TileInfo* tile) {
    image_internal_nearest(false, false, 1, tile_output, scissor_rect, tile, command);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void render_image_nearest_blend_1ch(i16* tile_output, const RenderCommand* command,
                                                         f32x4 scissor_rect, const TileInfo* tile) {
    image_internal_nearest(true, false, 1, tile_output, scissor_rect, tile, command);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void render_image_nearest_rounded_1ch(i16* tile_output, const RenderCommand* command,
                                                           f32x4 scissor_rect, const TileInfo* tile) {
    image_internal_nearest(false, true, 1, tile_output, scissor_rect, tile, command);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void render_image_nearest_blend_rounded_1ch(i16* tile_output, const RenderCommand* command,
                                                                 f32x4 scissor_rect, const TileInfo* tile) {
    image_internal_nearest(true, true, 1, tile_output, scissor_rect, tile, command);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// 3-channel (RGB) permutations
static ALWAYS_INLINE void render_image_nearest_solid_3ch(i16* tile_output, const RenderCommand* command,
                                                         f32x4 scissor_rect, const TileInfo* tile) {
    image_internal_nearest(false, false, 3, tile_output, scissor_rect, tile, command);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void render_image_nearest_blend_3ch(i16* tile_output, const RenderCommand* command,
                                                         f32x4 scissor_rect, const TileInfo* tile) {
    image_internal_nearest(true, false, 3, tile_output, scissor_rect, tile, command);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void render_image_nearest_rounded_3ch(i16* tile_output, const RenderCommand* command,
                                                           f32x4 scissor_rect, const TileInfo* tile) {
    image_internal_nearest(false, true, 3, tile_output, scissor_rect, tile, command);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void render_image_nearest_blend_rounded_3ch(i16* tile_output, const RenderCommand* command,
                                                                 f32x4 scissor_rect, const TileInfo* tile) {
    image_internal_nearest(true, true, 3, tile_output, scissor_rect, tile, command);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// 4-channel (RGBA) permutations - always blend with background due to alpha channel
static ALWAYS_INLINE void render_image_nearest_blend_4ch(i16* tile_output, const RenderCommand* command,
                                                         f32x4 scissor_rect, const TileInfo* tile) {
    image_internal_nearest(true, false, 4, tile_output, scissor_rect, tile, command);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void render_image_nearest_blend_rounded_4ch(i16* tile_output, const RenderCommand* command,
                                                                 f32x4 scissor_rect, const TileInfo* tile) {
    image_internal_nearest(true, true, 4, tile_output, scissor_rect, tile, command);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef void (*fill_func_t)(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect, const TileInfo* tile);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const fill_func_t image_nearest_dispatch_table[2][2][3] = {
    {
        // No rounding
        { render_image_nearest_solid_1ch, render_image_nearest_solid_3ch,
          render_image_nearest_blend_4ch }, // No blend (4ch always blends)
        { render_image_nearest_blend_1ch, render_image_nearest_blend_3ch,
          render_image_nearest_blend_4ch }, // With blend
    },
    {
        // With rounding
        { render_image_nearest_rounded_1ch, render_image_nearest_rounded_3ch,
          render_image_nearest_blend_rounded_4ch }, // No blend (4ch always blends)
        { render_image_nearest_blend_rounded_1ch, render_image_nearest_blend_rounded_3ch,
          render_image_nearest_blend_rounded_4ch }, // With blend
    },
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void sr_render_image_nearest(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect, const TileInfo* tile) {
    const ImageData* image = command->image;
    u32 render_mask = command->mask;
    int rounding = !!(render_mask & RenderMask_Corners);
    int blend_with_background = !!(render_mask & RenderMask_BlendWithBackground);

    // Map channel count to dispatch table index: 1->0, 3->1, 4->2
    int channel_index = (image->channels == 1) ? 0 : (image->channels == 3) ? 1 : 2;

    if (channel_index == 2)
        blend_with_background = 1;

    if (command->global_alpha < 255)
        blend_with_background = 1;

    image_nearest_dispatch_table[rounding][blend_with_background][channel_index](tile_output, command, scissor_rect,
                                                                                 tile);
}
