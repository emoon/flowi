#include "image_decode.h"
#include <core/arena.h>
#include <core/core.h>
#include <core/log.h>
#include <core/simd.h>
#include <core/memory.h>
#include "nanosvg.h"
#include "nanosvgrast.h"
#include "stb_image.h"
#include "stb_image_resize2.h"
#include <limits.h>
#include <math.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Checked pixel sizes

bool image_checked_pixel_size(int width, int height, size_t bytes_per_pixel, size_t* out_size) {
    if (width <= 0 || height <= 0 || bytes_per_pixel == 0) {
        return false;
    }

    size_t w = (size_t)width;
    size_t h = (size_t)height;
    if (w > SIZE_MAX / h) {
        return false;
    }
    size_t pixels = w * h;
    if (pixels > SIZE_MAX / bytes_per_pixel) {
        return false;
    }
    *out_size = pixels * bytes_per_pixel;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool image_checked_stride(int width, size_t bytes_per_pixel, int* out_stride) {
    size_t row_bytes = 0;
    if (!image_checked_pixel_size(width, 1, bytes_per_pixel, &row_bytes) || row_bytes > INT_MAX) {
        return false;
    }
    *out_stride = (int)row_bytes;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SIMD grayscale detection

static bool is_grayscale_rgba_row(const u8* row, int width) {
    // Process 16 pixels (64 bytes) at a time using SIMD deinterleaving
    int simd_count = width / 16;

    for_count(i, simd_count) {
        u8x16_rgba_result rgba = u8x16_load_deinterleaved_4(row, i * 64);

        u8x16 rg_equal = u8x16_cmp_eq(rgba.r, rgba.g);
        u8x16 gb_equal = u8x16_cmp_eq(rgba.g, rgba.b);
        u8x16 grayscale_mask = u8x16_and(rg_equal, gb_equal);

        if (!u8x16_all_true(grayscale_mask)) {
            return false;
        }
    }

    for (int x = simd_count * 16; x < width; x++) {
        const u8* pixel = row + x * 4;
        if (pixel[0] != pixel[1] || pixel[1] != pixel[2]) {
            return false;
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool is_grayscale_rgba_simd(const u8* rgba_data, int width, int height, int stride) {
    for_count(y, height) {
        if (!is_grayscale_rgba_row(rgba_data + (size_t)y * (size_t)stride, width)) {
            return false;
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Black border detection

static bool is_row_all_black(const u8* row_data, int width, int channels) {
    u8x16 zero = u8x16_new_splat(0);

    if (channels == 4) {
        int simd_pixels = (width / 16) * 16;

        for (int i = 0; i < simd_pixels; i += 16) {
            u8x16_rgba_result rgba = u8x16_load_deinterleaved_4(row_data, i * 4);
            u8x16 rgb_or = u8x16_or(u8x16_or(rgba.r, rgba.g), rgba.b);
            u8x16 all_black = u8x16_cmp_eq(rgb_or, zero);

            if (!u8x16_all_true(all_black)) {
                return false;
            }
        }

        for (int i = simd_pixels; i < width; i++) {
            const u8* pixel = row_data + i * 4;
            if (pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0) {
                return false;
            }
        }
    } else if (channels == 3) {
        int simd_pixels = (width / 16) * 16;

        for (int i = 0; i < simd_pixels; i += 16) {
            u8x16_rgb_result rgb = u8x16_load_deinterleaved_3(row_data, i * 3);
            u8x16 rgb_or = u8x16_or(u8x16_or(rgb.r, rgb.g), rgb.b);
            u8x16 all_black = u8x16_cmp_eq(rgb_or, zero);

            if (!u8x16_all_true(all_black)) {
                return false;
            }
        }

        for (int i = simd_pixels; i < width; i++) {
            const u8* pixel = row_data + i * 3;
            if (pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0) {
                return false;
            }
        }
    } else if (channels == 1) {
        int simd_pixels = (width / 16) * 16;

        for (int i = 0; i < simd_pixels; i += 16) {
            u8x16 gray = u8x16_load_unaligned(row_data, i);
            u8x16 all_black = u8x16_cmp_eq(gray, zero);

            if (!u8x16_all_true(all_black)) {
                return false;
            }
        }

        for (int i = simd_pixels; i < width; i++) {
            if (row_data[i] != 0) {
                return false;
            }
        }
    } else {
        // Grey+alpha (2 channels, what stb hands back for a GA PNG without force_rgb) and any other layout:
        // leading bytes are colour, trailing bytes are alpha.
        if (channels <= 0) {
            return false;
        }

        const int color_bytes = channels >= 3 ? 3 : 1;
        for (int i = 0; i < width; i++) {
            const u8* pixel = row_data + i * channels;
            for (int c = 0; c < color_bytes; c++) {
                if (pixel[c] != 0) {
                    return false;
                }
            }
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool is_column_all_black(const u8* data, int column, int start_row, int end_row, int stride, int channels) {
    if (channels <= 0) {
        return false;
    }

    // Leading bytes are colour, trailing bytes are alpha - so 2-channel grey+alpha tests its grey byte rather
    // than falling through and reporting all-black.
    const int color_bytes = channels >= 3 ? 3 : 1;

    for (int row = start_row; row < end_row; row++) {
        const u8* pixel = data + row * stride + column * channels;

        for (int c = 0; c < color_bytes; c++) {
            if (pixel[c] != 0) {
                return false;
            }
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool is_region_all_black(const u8* data, int row_start, int row_end, int col_start, int col_end, int stride,
                                int channels) {
    int region_width = col_end - col_start;
    if (region_width <= 0) {
        return true;
    }

    for (int row = row_start; row < row_end; row++) {
        const u8* row_data = data + row * stride + col_start * channels;
        if (!is_row_all_black(row_data, region_width, channels)) {
            return false;
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void detect_and_remove_black_border(InternalDecodeData* image) {
    if (!image->data || image->width <= 0 || image->height <= 0) {
        return;
    }

    int width = image->width;
    int height = image->height;
    int channels = image->channels;
    int stride = image->stride;
    const u8* data = image->data;

    int top_border = 0;
    for (int row = 0; row < height; row++) {
        if (!is_row_all_black(data + row * stride, width, channels)) {
            break;
        }
        top_border++;
    }

    if (top_border >= height) {
        return;
    }

    int bottom_border = 0;
    for (int row = height - 1; row >= top_border; row--) {
        if (!is_row_all_black(data + row * stride, width, channels)) {
            break;
        }
        bottom_border++;
    }

    int content_start_row = top_border;
    int content_end_row = height - bottom_border;

    int left_border = 0;
    for (int col = 0; col < width; col++) {
        if (!is_column_all_black(data, col, content_start_row, content_end_row, stride, channels)) {
            break;
        }
        left_border++;
    }

    if (left_border >= width) {
        return;
    }

    int right_border = 0;
    for (int col = width - 1; col >= left_border; col--) {
        if (!is_column_all_black(data, col, content_start_row, content_end_row, stride, channels)) {
            break;
        }
        right_border++;
    }

    if (top_border == 0 && bottom_border == 0 && left_border == 0 && right_border == 0) {
        return;
    }

    // Verify corners
    if (!is_region_all_black(data, 0, top_border, 0, left_border, stride, channels)) {
        return;
    }
    if (!is_region_all_black(data, 0, top_border, width - right_border, width, stride, channels)) {
        return;
    }
    if (!is_region_all_black(data, height - bottom_border, height, 0, left_border, stride, channels)) {
        return;
    }
    if (!is_region_all_black(data, height - bottom_border, height, width - right_border, width, stride, channels)) {
        return;
    }

    // Zero-copy adjustment
    int new_width = width - left_border - right_border;
    int new_height = height - top_border - bottom_border;

    if (new_width <= 0 || new_height <= 0) {
        return;
    }

    image->data = (u8*)data + (top_border * stride) + (left_border * channels);
    image->width = new_width;
    image->height = new_height;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SVG detection

static bool is_svg_data(const u8* data, size_t size) {
    if (size < 4)
        return false;

    if (size >= 5 && memory_compare(data, "<?xml", 5) == 0) {
        size_t search_limit = (size < 512) ? size : 512;
        for_count(i, search_limit - 4) {
            if (memory_compare(data + i, "<svg", 4) == 0) {
                return true;
            }
        }
    }

    if (size >= 4 && memory_compare(data, "<svg", 4) == 0) {
        return true;
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Resize

// Returns true when the image is left at the requested size (either a successful resize or a no-op because it
// already matched / the target was invalid). Returns false when stbir_resize failed: the image is unchanged and
// keeps its pre-resize size.
static bool resize_image(InternalDecodeData* image, int target_width, int target_height, ImageDecodeResizeFilter filter,
                         FlArena* arena) {
    if (target_width <= 0 || target_height <= 0) {
        return true;
    }

    if (image->width == target_width && image->height == target_height) {
        return true;
    }

    int channels = image->channels;
    int input_stride = image->stride;
    int output_stride = 0;
    size_t resized_size = 0;
    if (!image_checked_stride(target_width, (size_t)channels, &output_stride)
        || !image_checked_pixel_size(target_width, target_height, (size_t)channels, &resized_size)) {
        log_debug("Resize target %dx%d (%d channels) is too large", target_width, target_height, channels);
        return false;
    }
    u8* resized_data = arena_alloc_array(arena, u8, resized_size);

    stbir_filter stb_filter
        = (filter == ImageDecodeResizeFilter_Nearest) ? STBIR_FILTER_POINT_SAMPLE : STBIR_FILTER_DEFAULT;

    if (!stbir_resize(image->data, image->width, image->height, input_stride, resized_data, target_width, target_height,
                      0, (stbir_pixel_layout)channels, STBIR_TYPE_UINT8, STBIR_EDGE_CLAMP, stb_filter)) {
        return false;
    }

    image->data = resized_data;
    image->width = target_width;
    image->height = target_height;
    image->stride = output_stride;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Premultiplication

static inline u8x16 premul_channel_x16(u8x16 c, i16x8x2 alpha) {
    const i16x8 bias = i16x8_new_splat(128);
    i16x8x2 t = i16x8x2_mul(u8x16_unpack_to_i16x8_low(c), alpha);
    t.v0 = i16x8_add(t.v0, bias);
    t.v1 = i16x8_add(t.v1, bias);

    const i16x8x2 high = u8x16_unpack_to_i16x8_low(i16x8x2_pack_to_u8x16_narrow_shr(t, 8));
    t.v0 = i16x8_add(t.v0, high.v0);
    t.v1 = i16x8_add(t.v1, high.v1);
    return i16x8x2_pack_to_u8x16_narrow_shr(t, 8);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void premultiply_alpha(InternalDecodeData* image) {
    if (image->channels != 4) {
        return;
    }

    int content_width = image->width;
    int content_height = image->height;
    int content_stride = image->stride;
    u8* data = image->data;

    for (int row = 0; row < content_height; row++) {
        u8* row_data = data + row * content_stride;

        int simd_pixels = (content_width / 16) * 16;

        for (int i = 0; i < simd_pixels; i += 16) {
            u8x16_rgba_result rgba = u8x16_load_deinterleaved_4(row_data, i * 4);

            i16x8x2 alpha = u8x16_unpack_to_i16x8_low(rgba.a);

            u8x16 final_r = premul_channel_x16(rgba.r, alpha);
            u8x16 final_g = premul_channel_x16(rgba.g, alpha);
            u8x16 final_b = premul_channel_x16(rgba.b, alpha);

            u8x16_store_interleaved_4(row_data, final_r, final_g, final_b, rgba.a, i * 4);
        }

        for (int i = simd_pixels; i < content_width; i++) {
            const int a = row_data[i * 4 + 3];

            row_data[i * 4 + 0] = image_premul_channel(row_data[i * 4 + 0], a);
            row_data[i * 4 + 1] = image_premul_channel(row_data[i * 4 + 1], a);
            row_data[i * 4 + 2] = image_premul_channel(row_data[i * 4 + 2], a);
        }
    }

    image->premultiplied = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Grayscale conversion

static void convert_grayscale_to_alpha(InternalDecodeData* image, FlArena* arena) {
    if (image->channels != 4) {
        return;
    }

    if (!is_grayscale_rgba_simd(image->data, image->width, image->height, image->stride)) {
        return;
    }

    size_t pixel_count = 0;
    if (!image_checked_pixel_size(image->width, image->height, 1, &pixel_count)) {
        log_debug("Grayscale image %dx%d is too large to convert", image->width, image->height);
        return;
    }
    u8* alpha_data = arena_alloc_array(arena, u8, pixel_count);

    for_count(y, image->height) {
        const u8* src_row = image->data + (size_t)y * (size_t)image->stride;
        u8* dst_row = alpha_data + (size_t)y * (size_t)image->width;
        for_count(x, image->width) {
            u8 r = src_row[x * 4 + 0];
            u8 a = src_row[x * 4 + 3];
            dst_row[x] = (a > 0) ? r : 0;
        }
    }

    image->data = alpha_data;
    image->stride = image->width;
    image->channels = 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tint gradient

static void apply_tint_gradient_rgb(u8* rgb_data, int width, int height, int stride, int image_start_x, u8 tint_r,
                                    u8 tint_g, u8 tint_b, FlArena* arena) {
    if (width <= 1)
        return;

    int image_width = width - image_start_x;
    if (image_width <= 1)
        return;

    int gradient_width = image_width / 4;
    if (gradient_width < 2)
        gradient_width = 2;

    i16* blend_table = arena_alloc_array(arena, i16, width);
    for (int x = 0; x < image_start_x; x++) {
        blend_table[x] = 0;
    }
    for (int x = image_start_x; x < image_start_x + gradient_width; x++) {
        int t = ((x - image_start_x) * 32767) / (gradient_width - 1);
        blend_table[x] = (i16)t;
    }
    for (int x = image_start_x + gradient_width; x < width; x++) {
        blend_table[x] = 32767;
    }

    i16x8 tint_r_vec = i16x8_new_splat((int16_t)tint_r);
    i16x8 tint_g_vec = i16x8_new_splat((int16_t)tint_g);
    i16x8 tint_b_vec = i16x8_new_splat((int16_t)tint_b);

    int gradient_end_x = image_start_x + gradient_width;

    for (int y = 0; y < height; y++) {
        u8* row = rgb_data + y * stride;

        int x = image_start_x;
        for (; x + 16 <= gradient_end_x; x += 16) {
            u8x16_rgb_result pixels = u8x16_load_deinterleaved_3(row, x * 3);

            i16x8 t_lo = i16x8_load_unaligned(blend_table, x);
            i16x8 t_hi = i16x8_load_unaligned(blend_table, x + 8);

            i16x8x2 r_i16 = u8x16_unpack_to_i16x8_low(pixels.r);
            i16x8x2 g_i16 = u8x16_unpack_to_i16x8_low(pixels.g);
            i16x8x2 b_i16 = u8x16_unpack_to_i16x8_low(pixels.b);

            i16x8 r_lo = i16x8_lerp(tint_r_vec, r_i16.v0, t_lo);
            i16x8 r_hi = i16x8_lerp(tint_r_vec, r_i16.v1, t_hi);
            i16x8 g_lo = i16x8_lerp(tint_g_vec, g_i16.v0, t_lo);
            i16x8 g_hi = i16x8_lerp(tint_g_vec, g_i16.v1, t_hi);
            i16x8 b_lo = i16x8_lerp(tint_b_vec, b_i16.v0, t_lo);
            i16x8 b_hi = i16x8_lerp(tint_b_vec, b_i16.v1, t_hi);

            u8x16 r_out = u16x8_narrow_to_u8x16((u16x8) { .v = r_lo.v }, (u16x8) { .v = r_hi.v });
            u8x16 g_out = u16x8_narrow_to_u8x16((u16x8) { .v = g_lo.v }, (u16x8) { .v = g_hi.v });
            u8x16 b_out = u16x8_narrow_to_u8x16((u16x8) { .v = b_lo.v }, (u16x8) { .v = b_hi.v });

            u8x16_store_interleaved_3(row, r_out, g_out, b_out, x * 3);
        }

        for (; x < gradient_end_x; x++) {
            u8* pixel = row + x * 3;
            int t = blend_table[x];

            int r = pixel[0];
            int g = pixel[1];
            int b = pixel[2];

            pixel[0] = (u8)(tint_r + ((r - tint_r) * t) / 32767);
            pixel[1] = (u8)(tint_g + ((g - tint_g) * t) / 32767);
            pixel[2] = (u8)(tint_b + ((b - tint_b) * t) / 32767);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Vertical fade gradient (top to bottom) - RGB version
// Fades from original image color at top to target color at bottom

static void apply_vertical_fade_rgb(u8* rgb_data, int width, int height, int stride, f32 fade_start, u8 target_r,
                                    u8 target_g, u8 target_b, FlArena* arena) {
    if (height <= 1 || width <= 0) {
        return;
    }

    int fade_start_row = (int)(height * fade_start);
    if (fade_start_row >= height - 1) {
        return;
    }

    int fade_height = height - fade_start_row;
    if (fade_height < 2) {
        return;
    }

    // Blend factor per row: t=32767 means fully original, t=0 means fully target color.
    i16* blend_table = arena_alloc_array(arena, i16, height);
    for (int y = 0; y < fade_start_row; y++) {
        blend_table[y] = 32767;
    }
    for (int y = fade_start_row; y < height; y++) {
        int t = 32767 - ((y - fade_start_row) * 32767) / (fade_height - 1);
        blend_table[y] = (i16)t;
    }

    i16x8 target_r_vec = i16x8_new_splat((int16_t)target_r);
    i16x8 target_g_vec = i16x8_new_splat((int16_t)target_g);
    i16x8 target_b_vec = i16x8_new_splat((int16_t)target_b);

    for (int y = fade_start_row; y < height; y++) {
        u8* row = rgb_data + y * stride;
        i16 t = blend_table[y];
        i16x8 t_vec = i16x8_new_splat(t);

        int simd_pixels = (width / 16) * 16;

        for (int x = 0; x < simd_pixels; x += 16) {
            u8x16_rgb_result rgb = u8x16_load_deinterleaved_3(row, x * 3);

            i16x8x2 r_i16 = u8x16_unpack_to_i16x8_low(rgb.r);
            i16x8x2 g_i16 = u8x16_unpack_to_i16x8_low(rgb.g);
            i16x8x2 b_i16 = u8x16_unpack_to_i16x8_low(rgb.b);

            // Lerp: result = target + (original - target) * t / 32767
            i16x8 r_lo = i16x8_lerp(target_r_vec, r_i16.v0, t_vec);
            i16x8 r_hi = i16x8_lerp(target_r_vec, r_i16.v1, t_vec);
            i16x8 g_lo = i16x8_lerp(target_g_vec, g_i16.v0, t_vec);
            i16x8 g_hi = i16x8_lerp(target_g_vec, g_i16.v1, t_vec);
            i16x8 b_lo = i16x8_lerp(target_b_vec, b_i16.v0, t_vec);
            i16x8 b_hi = i16x8_lerp(target_b_vec, b_i16.v1, t_vec);

            u8x16 r_out = u16x8_narrow_to_u8x16((u16x8) { .v = r_lo.v }, (u16x8) { .v = r_hi.v });
            u8x16 g_out = u16x8_narrow_to_u8x16((u16x8) { .v = g_lo.v }, (u16x8) { .v = g_hi.v });
            u8x16 b_out = u16x8_narrow_to_u8x16((u16x8) { .v = b_lo.v }, (u16x8) { .v = b_hi.v });

            u8x16_store_interleaved_3(row, r_out, g_out, b_out, x * 3);
        }

        for (int x = simd_pixels; x < width; x++) {
            u8* pixel = row + x * 3;
            int r = pixel[0];
            int g = pixel[1];
            int b = pixel[2];

            pixel[0] = (u8)(target_r + ((r - target_r) * t) / 32767);
            pixel[1] = (u8)(target_g + ((g - target_g) * t) / 32767);
            pixel[2] = (u8)(target_b + ((b - target_b) * t) / 32767);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Composite the decoded RGB(A) image onto a fresh fill-colored RGB canvas at (offset_x, offset_y). Source alpha
// is dropped (the canvas is always 3-channel). Callers pick the offset: right-align for the tint preview,
// centered for letterbox/pillarbox.
//
// Reads are channel-agnostic: 1-channel (grayscale) and 2-channel (grayscale+alpha) sources replicate the gray
// value into R/G/B. The blit is bounds-checked against the canvas, so an overhanging source is cropped.
void composite_source_into_canvas_rgb(const InternalDecodeData* image, u8* canvas, int canvas_w, int canvas_h,
                                      u8 fill_r, u8 fill_g, u8 fill_b, int offset_x, int offset_y) {
    for (int i = 0; i < canvas_w * canvas_h; i++) {
        canvas[i * 3 + 0] = fill_r;
        canvas[i * 3 + 1] = fill_g;
        canvas[i * 3 + 2] = fill_b;
    }

    int src_channels = image->channels;
    for (int y = 0; y < image->height; y++) {
        int dst_y = offset_y + y;
        if (dst_y < 0 || dst_y >= canvas_h) {
            continue;
        }

        const u8* src_row = image->data + y * image->stride;
        u8* dst_row = canvas + dst_y * canvas_w * 3;

        for (int x = 0; x < image->width; x++) {
            int dst_x = offset_x + x;
            if (dst_x < 0 || dst_x >= canvas_w) {
                continue;
            }

            const u8* src = src_row + x * src_channels;
            u8 r = src[0];
            u8 g = (src_channels >= 3) ? src[1] : r;
            u8 b = (src_channels >= 3) ? src[2] : r;

            dst_row[dst_x * 3 + 0] = r;
            dst_row[dst_x * 3 + 1] = g;
            dst_row[dst_x * 3 + 2] = b;
        }
    }
}

static void composite_on_canvas_rgb(InternalDecodeData* image, int canvas_w, int canvas_h, u8 fill_r, u8 fill_g,
                                    u8 fill_b, int offset_x, int offset_y, FlArena* arena) {
    size_t canvas_size = 0;
    int canvas_stride = 0;
    if (!image_checked_pixel_size(canvas_w, canvas_h, 3, &canvas_size)
        || !image_checked_stride(canvas_w, 3, &canvas_stride)) {
        log_debug("Canvas %dx%d is too large to composite", canvas_w, canvas_h);
        return;
    }
    u8* canvas = arena_alloc_array(arena, u8, canvas_size);

    composite_source_into_canvas_rgb(image, canvas, canvas_w, canvas_h, fill_r, fill_g, fill_b, offset_x, offset_y);

    image->data = canvas;
    image->width = canvas_w;
    image->height = canvas_h;
    image->stride = canvas_stride;
    image->channels = 3;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Allocate and populate the public decode result from the internal working data.

static FlImageDecodeResult* internal_to_result(const InternalDecodeData* internal, FlArena* arena) {
    FlImageDecodeResult* result = arena_alloc(arena, FlImageDecodeResult);
    result->pixels = internal->data;
    result->width = internal->width;
    result->height = internal->height;
    result->stride = internal->stride;
    result->channels = internal->channels;
    result->premultiplied = internal->premultiplied;
    result->needs_thumbnail_compose = false;
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SVG loading

// Largest rasterized SVG edge. nanosvg indexes its bitmap with int row * stride arithmetic, so the raster must keep
// stride * height within INT_MAX: 8192 * 4 * 8192 is 268M, well inside it.
#define IMAGE_SVG_MAX_DIMENSION 8192

static FlImageDecodeResult* decode_svg(const u8* data, size_t size, const ImageDecodeOptions* opts, FlArena* arena) {
    arena_scratch_auto_conflict(temp, arena);

    float ui_scale = (opts->ui_scale > 0.0f) ? opts->ui_scale : 1.0f;
    float base_scale = (opts->svg_scale_factor > 0.0f) ? opts->svg_scale_factor : 1.0f;
    float scale_factor = base_scale * ui_scale;

    char* svg_string = arena_alloc_array(temp.arena, char, size + 1);
    memory_copy(svg_string, size, data, size);
    svg_string[size] = '\0';

    NSVGimage* image = nsvgParse(svg_string, "px", 96.0f);
    if (!image) {
        log_debug("Failed to parse SVG data (%zu bytes)", size);
        return nullptr;
    }

    // A target size replaces the declared size below, so only an unscaled declared size can be absurd here. Check in
    // float, before the int conversion.
    bool has_target = opts->target_width > 0 && opts->target_height > 0;
    if (!has_target
        && (image->width * scale_factor > IMAGE_SVG_MAX_DIMENSION
            || image->height * scale_factor > IMAGE_SVG_MAX_DIMENSION)) {
        log_debug("SVG declared size %.0fx%.0f is too large to rasterize", image->width, image->height);
        nsvgDelete(image);
        return nullptr;
    }

    int width = has_target ? 0 : (int)(image->width * scale_factor + 0.5f);
    int height = has_target ? 0 : (int)(image->height * scale_factor + 0.5f);

    if (has_target) {
        int scaled_target_w = (int)(opts->target_width * ui_scale + 0.5f);
        int scaled_target_h = (int)(opts->target_height * ui_scale + 0.5f);

        if (opts->preserve_aspect_ratio) {
            float scale_w = (float)scaled_target_w / image->width;
            float scale_h = (float)scaled_target_h / image->height;
            float scale = (scale_w < scale_h) ? scale_w : scale_h;

            width = (int)(image->width * scale + 0.5f);
            height = (int)(image->height * scale + 0.5f);
        } else {
            width = scaled_target_w;
            height = scaled_target_h;
        }
    }

    if (width <= 0 || height <= 0) {
        log_debug("Invalid SVG dimensions: %dx%d", width, height);
        nsvgDelete(image);
        return nullptr;
    }

    if (width > IMAGE_SVG_MAX_DIMENSION || height > IMAGE_SVG_MAX_DIMENSION) {
        log_debug("SVG raster size %dx%d exceeds the %d limit", width, height, IMAGE_SVG_MAX_DIMENSION);
        nsvgDelete(image);
        return nullptr;
    }

    size_t pixel_size = 0;
    int stride = 0;
    if (!image_checked_pixel_size(width, height, 4, &pixel_size) || !image_checked_stride(width, 4, &stride)) {
        log_debug("SVG dimensions %dx%d are too large to rasterize", width, height);
        nsvgDelete(image);
        return nullptr;
    }

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        log_debug("Failed to create SVG rasterizer");
        nsvgDelete(image);
        return nullptr;
    }

    u8* pixel_data = arena_alloc_array(arena, u8, pixel_size);
    float raster_scale = (float)width / image->width;

    nsvgRasterize(rast, image, 0, 0, raster_scale, pixel_data, width, height, stride);

    nsvgDeleteRasterizer(rast);
    nsvgDelete(image);

    InternalDecodeData internal = {
        .data = pixel_data,
        .width = width,
        .height = height,
        .stride = stride,
        .channels = 4,
        .premultiplied = true, // nanosvg produces premultiplied output
    };

    if (opts->detect_grayscale) {
        convert_grayscale_to_alpha(&internal, arena);
    }

    return internal_to_result(&internal, arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main decode function

FlImageDecodeResult* image_decode(FlArena* arena, const void* file_data, size_t file_size,
                                  const ImageDecodeOptions* options) {
    if (!file_data || file_size == 0) {
        return nullptr;
    }

    const u8* data = (const u8*)file_data;

    ImageDecodeOptions opts = { 0 };
    if (options) {
        opts = *options;
    }
    if (opts.ui_scale <= 0.0f) {
        opts.ui_scale = 1.0f;
    }

    if (is_svg_data(data, file_size)) {
        return decode_svg(data, file_size, &opts, arena);
    }

    // Force RGB (3 channels) when the caller asks for it.
    int desired_channels = opts.force_rgb ? 3 : 0;
    int width, height, channels;
    u8* stb_data = stbi_load_from_memory(data, (int)file_size, &width, &height, &channels, desired_channels);
    if (!stb_data) {
        log_debug("Failed to decode image data (%zu bytes): %s", file_size, stbi_failure_reason());
        return nullptr;
    }
    if (desired_channels > 0) {
        channels = desired_channels;
    }

    size_t image_size = (size_t)width * height * channels;
    u8* arena_data = arena_alloc_array(arena, u8, image_size);
    memory_copy(arena_data, image_size, stb_data, image_size);
    stbi_image_free(stb_data);

    InternalDecodeData internal = {
        .data = arena_data,
        .width = width,
        .height = height,
        .stride = width * channels,
        .channels = (u8)channels,
        .premultiplied = false,
    };

    // Raster decode returns raw pixels; the finishing pass (premultiply, resize, compositing, grayscale) runs
    // in image_thumbnail_compose().
    FlImageDecodeResult* result = internal_to_result(&internal, arena);
    result->needs_thumbnail_compose = true;
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Thumbnail compose - the content-browser preview finishing pass.

void image_thumbnail_compose(FlArena* arena, FlImageDecodeResult* result, const ImageDecodeOptions* decode_opts,
                             const ImageThumbnailOptions* thumb) {
    if (!result || !result->needs_thumbnail_compose) {
        return; // SVG decodes are already final
    }

    ImageDecodeOptions opts = decode_opts ? *decode_opts : (ImageDecodeOptions) { 0 };
    ImageThumbnailOptions tb = thumb ? *thumb : (ImageThumbnailOptions) { 0 };

    InternalDecodeData internal = {
        .data = result->pixels,
        .width = result->width,
        .height = result->height,
        .stride = result->stride,
        .channels = result->channels,
        .premultiplied = result->premultiplied,
    };

    // Black border detection (before resize, on the raw decoded pixels)
    if (tb.detect_black_border) {
        detect_and_remove_black_border(&internal);
    }

    if (internal.channels == 4) {
        premultiply_alpha(&internal);
    }

    int target_canvas_w = 0;
    int target_canvas_h = 0;

    if (opts.target_width > 0 && opts.target_height > 0) {
        float ui_scale = (opts.ui_scale > 0.0f) ? opts.ui_scale : 1.0f;
        int scaled_w = (int)(opts.target_width * ui_scale + 0.5f);
        int scaled_h = (int)(opts.target_height * ui_scale + 0.5f);

        target_canvas_w = scaled_w;
        target_canvas_h = scaled_h;

        if (opts.preserve_aspect_ratio && internal.width > 0 && internal.height > 0) {
            float src_aspect = (float)internal.width / (float)internal.height;
            float dst_aspect = (float)scaled_w / (float)scaled_h;

            if (src_aspect > dst_aspect) {
                scaled_h = (int)(scaled_w / src_aspect + 0.5f);
            } else {
                scaled_w = (int)(scaled_h * src_aspect + 0.5f);
            }
        }

        if (!resize_image(&internal, scaled_w, scaled_h, opts.resize_filter, arena)) {
            // The source keeps its pre-resize dimensions; the compose paths below clamp offsets and blit extents.
            log_debug("Image resize to %dx%d failed; compositing un-resized source", scaled_w, scaled_h);
        }
    }

    if (tb.apply_vertical_fade && internal.channels == 3) {
        apply_vertical_fade_rgb(internal.data, internal.width, internal.height, internal.stride, tb.fade_start,
                                tb.fade_r, tb.fade_g, tb.fade_b, arena);
    }

    // Tint mode - right-align with gradient
    if (tb.background_mode == ImageDecodeBackgroundMode_Tint && target_canvas_w > 0 && target_canvas_h > 0) {
        // Clamp the right-align offset to 0 for a source wider than the canvas.
        int image_start_x = target_canvas_w - internal.width;
        if (image_start_x < 0) {
            image_start_x = 0;
        }
        composite_on_canvas_rgb(&internal, target_canvas_w, target_canvas_h, tb.tint_r, tb.tint_g, tb.tint_b,
                                image_start_x, 0, arena);
        apply_tint_gradient_rgb(internal.data, internal.width, internal.height, internal.stride, image_start_x,
                                tb.tint_r, tb.tint_g, tb.tint_b, arena);
    }

    // Center mode - center on canvas with fill color, no gradient
    if (tb.background_mode == ImageDecodeBackgroundMode_Center && target_canvas_w > 0 && target_canvas_h > 0) {
        // Clamp the letterbox offsets to 0 for a source larger than the canvas.
        int offset_x = (target_canvas_w - internal.width) / 2;
        int offset_y = (target_canvas_h - internal.height) / 2;
        if (offset_x < 0) {
            offset_x = 0;
        }
        if (offset_y < 0) {
            offset_y = 0;
        }
        composite_on_canvas_rgb(&internal, target_canvas_w, target_canvas_h, tb.tint_r, tb.tint_g, tb.tint_b, offset_x,
                                offset_y, arena);
    }

    if (opts.detect_grayscale && internal.channels == 4) {
        convert_grayscale_to_alpha(&internal, arena);
    }

    // Write the finished image back into the result.
    result->pixels = internal.data;
    result->width = internal.width;
    result->height = internal.height;
    result->stride = internal.stride;
    result->channels = internal.channels;
    result->premultiplied = internal.premultiplied;
    result->needs_thumbnail_compose = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool image_decode_test_is_row_all_black(const u8* row_data, int width, int channels) {
    return is_row_all_black(row_data, width, channels);
}

bool image_decode_test_is_column_all_black(const u8* data, int column, int start_row, int end_row, int stride,
                                           int channels) {
    return is_column_all_black(data, column, start_row, end_row, stride, channels);
}
