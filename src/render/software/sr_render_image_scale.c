#include "sr_render_image_scale.h"
#include <core/log.h>
#include <core/simd.h>
#include "software_renderer_internal.h"
#include "sr_image_blend.h"
#include <ui/image.h>
#include <math.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Sample 8 destination pixels from one source plane as Q7 values. Takes the scalar gather when this command's
// source spread does not fit the 32-byte shuffle window.
static ALWAYS_INLINE i16x8 sample_plane_8(const ScaledTexturePrecomputed* scaled, const u8* line, int dest_x_pos) {
    u8x16 bytes;
    if (scaled->wide_spread) {
        // Indices are clamped to the valid source column range at precompute time.
        u8 gathered[16] = { 0 };
        const u16* src_indices = scaled->src_indices + dest_x_pos;
        for_count(i, 8) {
            gathered[i] = line[src_indices[i]];
        }
        bytes = u8x16_load_unaligned(gathered, 0);
    } else {
        const size_t load_offset = scaled->load_offsets[dest_x_pos];
        bytes = u8x16_shuffle2(u8x16_load_unaligned(line, load_offset), u8x16_load_unaligned(line, load_offset + 16),
                               scaled->shuffle_masks[dest_x_pos]);
    }
    return u8x16_unpack_to_i16x8_wide_shl(bytes, 7).v0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Composite 8 sampled destination pixels over the tile background. 1-channel sources are alpha coverage for the
// command tint, 3-channel sources are opaque unless faded by the frame's global alpha, 4-channel sources are
// premultiplied RGBA.
static ALWAYS_INLINE SoaRgb scale_8_pixels(const int channel_count, const bool fade,
                                           const ScaledTexturePrecomputed* scaled, const u8* const plane_lines[4],
                                           int dest_x_pos, const i16* output_line, int write_offset, SoaRgba tint,
                                           i16x8 global_alpha) {
    const int r_offset = write_offset;
    const int g_offset = write_offset + PLANE_OFFSET_INCREMENT;
    const int b_offset = write_offset + PLANE_OFFSET_INCREMENT * 2;

    if (channel_count == 1) {
        i16x8 coverage = sample_plane_8(scaled, plane_lines[0], dest_x_pos);
        coverage = i16x8_lerp(i16x8_new_splat(0), coverage, global_alpha);
        return (SoaRgb) {
            .r = i16x8_lerp(i16x8_load_unaligned(output_line, r_offset), tint.r, coverage),
            .g = i16x8_lerp(i16x8_load_unaligned(output_line, g_offset), tint.g, coverage),
            .b = i16x8_lerp(i16x8_load_unaligned(output_line, b_offset), tint.b, coverage),
        };
    }

    SoaRgb color = {
        .r = sample_plane_8(scaled, plane_lines[0], dest_x_pos),
        .g = sample_plane_8(scaled, plane_lines[1], dest_x_pos),
        .b = sample_plane_8(scaled, plane_lines[2], dest_x_pos),
    };

    if (channel_count == 4) {
        i16x8 alpha = sample_plane_8(scaled, plane_lines[3], dest_x_pos);
        alpha = i16x8_lerp(i16x8_new_splat(0), alpha, global_alpha);
        return premul_over_8(false, nullptr, color.r, color.g, color.b, alpha, global_alpha,
                             i16x8_load_unaligned(output_line, r_offset), i16x8_load_unaligned(output_line, g_offset),
                             i16x8_load_unaligned(output_line, b_offset));
    }

    if (fade) {
        color.r = i16x8_lerp(i16x8_load_unaligned(output_line, r_offset), color.r, global_alpha);
        color.g = i16x8_lerp(i16x8_load_unaligned(output_line, g_offset), color.g, global_alpha);
        color.b = i16x8_lerp(i16x8_load_unaligned(output_line, b_offset), color.b, global_alpha);
    }
    return color;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void sr_render_image_scale(i16* output, const RenderCommand* command, f32x4 scissor_rect, const TileInfo* tile_info) {
    const ImageData* image = command->image;
    const ScaledTexturePrecomputed* scaled = command->scaled_texture;
    const int channel_count = image->channels;
    const bool fade = command->global_alpha < 255;
    const SoaRgba tint = splat_color(command->corner_colors);
    const i16x8 global_alpha = i16x8_new_splat((i16)((command->global_alpha * 0x7FFF) / 255));

    if (channel_count != 1 && channel_count != 3 && channel_count != 4) {
        log_error("sr_render_image_scale: unsupported channel count %d", channel_count);
        return;
    }

    if (!scaled || !scaled->load_offsets || !scaled->shuffle_masks) {
        return;
    }

    int texture_stride = image->padded_width;

    // Convert AABB from screen coordinates to tile-local coordinates
    float tile_screen_x = f32x4_extract(tile_info->offsets, 0);
    float tile_screen_y = f32x4_extract(tile_info->offsets, 1);
    f32x4 tile_offset = f32x4_new(tile_screen_x, tile_screen_y, tile_screen_x, tile_screen_y);
    f32x4 x0y0x1y1_adjust = f32x4_sub(command->aabb, tile_offset);
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

    // Actual height, not padded_height, for the correct scaling ratio
    float dest_height = f32x4_extract(command->aabb, 3) - f32x4_extract(command->aabb, 1);
    float src_height = (float)image->height;
    float y_scale = src_height / dest_height;

    int plane_size = texture_stride * (int)image->padded_height;

    int write_offset = (int)y0 * TILE_STRIDE + (int)x0;
    i16* output_ptr = output;
    i16x8 write_mask = i16x8_write_mask(xlen & 0x7);

    // dest_x_start is the destination pixel we start reading from, relative to the image's left edge in
    // screen coordinates. Computed in floating point to preserve precision with fractional positions.
    float image_x0 = f32x4_extract(command->aabb, 0);
    float screen_x_f = tile_screen_x + (float)x0;
    int dest_x_start = clamp((i32)floorf(screen_x_f - image_x0), 0, scaled->dst_width - 1);

    float image_screen_y = f32x4_extract(command->aabb, 1);

    for_count(y, ylen) {
        int tile_local_y = (int)y0 + y;
        float screen_y = tile_screen_y + (float)tile_local_y;
        float dest_image_y = screen_y - image_screen_y;
        float src_y_f = dest_image_y * y_scale;
        int src_y = (int)src_y_f;

        // Planes past channel_count are never dereferenced.
        const u8* plane_lines[4];
        for_count(c, channel_count) {
            plane_lines[c] = image->data + c * plane_size + src_y * texture_stride;
        }
        i16* output_line_ptr = output_ptr + y * TILE_STRIDE;

        int dest_x_pos = dest_x_start;
        for_count(x, (xlen >> 3)) {
            SoaRgb color = scale_8_pixels(channel_count, fade, scaled, plane_lines, dest_x_pos, output_line_ptr,
                                          write_offset, tint, global_alpha);

            i16x8_store_unaligned(color.r, output_line_ptr, write_offset);
            i16x8_store_unaligned(color.g, output_line_ptr, write_offset + PLANE_OFFSET_INCREMENT);
            i16x8_store_unaligned(color.b, output_line_ptr, write_offset + PLANE_OFFSET_INCREMENT * 2);

            dest_x_pos += 8;
            output_line_ptr += 8;
        }

        if ((xlen & 0x7) > 0) {
            // Sample the remaining (< 8) destination pixels; the write mask below keeps only the valid lanes.
            SoaRgb color = scale_8_pixels(channel_count, fade, scaled, plane_lines, dest_x_pos, output_line_ptr,
                                          write_offset, tint, global_alpha);

            i16x8 out_r = i16x8_load_unaligned(output_line_ptr, write_offset);
            i16x8 out_g = i16x8_load_unaligned(output_line_ptr, write_offset + PLANE_OFFSET_INCREMENT);
            i16x8 out_b = i16x8_load_unaligned(output_line_ptr, write_offset + PLANE_OFFSET_INCREMENT * 2);

            out_r = i16x8_select(write_mask, color.r, out_r);
            out_g = i16x8_select(write_mask, color.g, out_g);
            out_b = i16x8_select(write_mask, color.b, out_b);

            i16x8_store_unaligned(out_r, output_line_ptr, write_offset);
            i16x8_store_unaligned(out_g, output_line_ptr, write_offset + PLANE_OFFSET_INCREMENT);
            i16x8_store_unaligned(out_b, output_line_ptr, write_offset + PLANE_OFFSET_INCREMENT * 2);
        }
    }
}
