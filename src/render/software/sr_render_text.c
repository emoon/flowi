#include <core/math.h>
#include <core/simd.h>
#include "software_renderer_internal.h"
#include <math.h>
#include <stddef.h>

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

ALWAYS_INLINE static void draw_text_line(const bool lcd_mode, const bool use_write_mask, i16* line_output,
                                         const u8* glyph_line_r, const u8* glyph_line_g, const u8* glyph_line_b,
                                         int r_offset, int g_offset, int b_offset, SoaRgba text_color, i16x8 write_mask,
                                         i16x8 coverage_scale) {
    i16x8 alpha_r_wide, alpha_g_wide, alpha_b_wide;
    i16x8 zero = i16x8_new_splat(0);

    if (lcd_mode) {
        alpha_r_wide = u8x8_load_to_i16x8_wide_shl(glyph_line_r, 0, 7);
        alpha_g_wide = u8x8_load_to_i16x8_wide_shl(glyph_line_g, 0, 7);
        alpha_b_wide = u8x8_load_to_i16x8_wide_shl(glyph_line_b, 0, 7);
        alpha_r_wide = i16x8_lerp(zero, alpha_r_wide, coverage_scale);
        alpha_g_wide = i16x8_lerp(zero, alpha_g_wide, coverage_scale);
        alpha_b_wide = i16x8_lerp(zero, alpha_b_wide, coverage_scale);
    } else {
        alpha_r_wide = u8x8_load_to_i16x8_wide_shl(glyph_line_r, 0, 7);
        alpha_r_wide = i16x8_lerp(zero, alpha_r_wide, coverage_scale);
        alpha_g_wide = alpha_b_wide = alpha_r_wide;
    }

    i16x8 bg_r = i16x8_load_unaligned(line_output, r_offset);
    i16x8 bg_g = i16x8_load_unaligned(line_output, g_offset);
    i16x8 bg_b = i16x8_load_unaligned(line_output, b_offset);

    i16x8 result_r = i16x8_lerp(bg_r, text_color.r, alpha_r_wide);
    i16x8 result_g = i16x8_lerp(bg_g, text_color.g, alpha_g_wide);
    i16x8 result_b = i16x8_lerp(bg_b, text_color.b, alpha_b_wide);

    if (use_write_mask) {
        result_r = i16x8_select(write_mask, result_r, bg_r);
        result_g = i16x8_select(write_mask, result_g, bg_g);
        result_b = i16x8_select(write_mask, result_b, bg_b);
    }

    i16x8_store_unaligned(result_r, line_output, r_offset);
    i16x8_store_unaligned(result_g, line_output, g_offset);
    i16x8_store_unaligned(result_b, line_output, b_offset);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void draw_text(const bool lcd_mode, i16* tile_output, const RenderCommand* command,
                                    f32x4 scissor_rect, const TileInfo* info) {

    const RenderText* glyph_data = &command->render_text;
    const f32x4 glyph_coords = glyph_data->aabb;

    // Transform to tile space
    f32x4 x0y0x1y1_adjust = f32x4_sub(glyph_coords, info->offsets);
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

    int ylen = y1 - y0;
    int xlen = x1 - x0;

    if (ylen <= 0 || xlen <= 0) {
        return;
    }

    i32x4 clip_diff = i32x4_sub(x0y0x1y1_int, scissor_int);
    clip_diff = i32x4_min(clip_diff, i32x4_new_splat(0));
    clip_diff = i32x4_abs(clip_diff);

    int clip_x = i32x4_extract(clip_diff, 0);
    int clip_y = i32x4_extract(clip_diff, 1);

    // Start reading from the clipped position in the glyph
    int glyph_x = clip_x;
    int glyph_y = clip_y;

    const int write_offset = (y0 * TILE_STRIDE) + x0;
    const int r_offset = write_offset;
    const int g_offset = write_offset + PLANE_OFFSET_INCREMENT;
    const int b_offset = write_offset + PLANE_OFFSET_INCREMENT * 2;

    i16x8 write_mask = i16x8_write_mask(xlen);

    SoaRgba text_color = splat_color(glyph_data->color);
    // The frame's global_alpha is already folded into the text color's alpha when the command is emitted, so
    // the color's alpha (Q7) is the coverage scale.
    i16 color_alpha_q7 = i16x8_extract(text_color.a, 0);
    i16x8 coverage_scale = i16x8_new_splat(color_alpha_q7);

    if (lcd_mode) {
        const u8* glyph_ptr_r = glyph_data->data_r + glyph_y * glyph_data->stride + glyph_x;
        const u8* glyph_ptr_g = glyph_data->data_g + glyph_y * glyph_data->stride + glyph_x;
        const u8* glyph_ptr_b = glyph_data->data_b + glyph_y * glyph_data->stride + glyph_x;

        for_count(y, ylen) {
            i16* line_output = tile_output + y * TILE_STRIDE;
            const u8* glyph_line_r = glyph_ptr_r + y * glyph_data->stride;
            const u8* glyph_line_g = glyph_ptr_g + y * glyph_data->stride;
            const u8* glyph_line_b = glyph_ptr_b + y * glyph_data->stride;

            for_count(x, (xlen >> 3)) {
                draw_text_line(true, false, line_output, glyph_line_r, glyph_line_g, glyph_line_b, r_offset, g_offset,
                               b_offset, text_color, write_mask, coverage_scale);

                glyph_line_r += 8;
                glyph_line_g += 8;
                glyph_line_b += 8;
                line_output += 8;
            }

            if ((xlen & 0x7) > 0) {
                draw_text_line(true, true, line_output, glyph_line_r, glyph_line_g, glyph_line_b, r_offset, g_offset,
                               b_offset, text_color, write_mask, coverage_scale);
            }
        }
    } else {
        const u8* glyph_ptr = glyph_data->data_r + glyph_y * glyph_data->stride + glyph_x;

        for_count(y, ylen) {
            i16* line_output = tile_output + y * TILE_STRIDE;
            const u8* glyph_line = glyph_ptr + y * glyph_data->stride;

            for_count(x, (xlen >> 3)) {
                draw_text_line(false, false, line_output, glyph_line, nullptr, nullptr, r_offset, g_offset, b_offset,
                               text_color, write_mask, coverage_scale);

                glyph_line += 8;
                line_output += 8;
            }

            if ((xlen & 0x7) > 0) {
                draw_text_line(false, true, line_output, glyph_line, nullptr, nullptr, r_offset, g_offset, b_offset,
                               text_color, write_mask, coverage_scale);
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void sr_render_text_lcd(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect,
                               const TileInfo* info) {
    draw_text(true, tile_output, command, scissor_rect, info);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void sr_render_text_grayscale(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect,
                                     const TileInfo* info) {
    draw_text(false, tile_output, command, scissor_rect, info);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void sr_render_text(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect, const TileInfo* info) {
    if (command->render_text.data_g == nullptr) {
        sr_render_text_grayscale(tile_output, command, scissor_rect, info);
    } else {
        sr_render_text_lcd(tile_output, command, scissor_rect, info);
    }
}
