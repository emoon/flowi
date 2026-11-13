#include "image_decode.h"
#include <core/arena.h>
#include <core/core.h>
#include <core/log.h>
#include <core/simd.h>
#include "stb_image_resize2.h"

// Thumbnail compose: the finishing pass for raw raster decodes (border crop, premultiply, resize, fade, background,
// grayscale).

// Leading bytes are colour, trailing bytes are alpha, so a grey+alpha pixel tests its grey byte only.
static int color_byte_count(int channels) {
    return channels >= 3 ? 3 : 1;
}

static bool pixel_is_black(const u8* pixel, int color_bytes) {
    for (int c = 0; c < color_bytes; c++) {
        if (pixel[c] != 0) {
            return false;
        }
    }
    return true;
}

bool image_region_all_black(const FlImageDecodeResult* image, PixelRect region) {
    const int color_bytes = color_byte_count(image->channels);

    for (int row = region.row_start; row < region.row_end; row++) {
        const u8* pixel = image->pixels + row * image->stride + region.col_start * image->channels;

        for (int col = region.col_start; col < region.col_end; col++, pixel += image->channels) {
            if (!pixel_is_black(pixel, color_bytes)) {
                return false;
            }
        }
    }
    return true;
}

static bool has_pixels(const FlImageDecodeResult* image) {
    return image->pixels && image->width > 0 && image->height > 0 && image->channels > 0;
}

typedef struct BorderInsets {
    int top, bottom, left, right;
} BorderInsets;

// Measures the black margin on each side. Returns false when the image has no content at all.
static bool measure_black_border(const FlImageDecodeResult* image, BorderInsets* out) {
    const int width = image->width;
    const int height = image->height;
    BorderInsets border = { 0 };

    while (border.top < height && image_region_all_black(image, (PixelRect) { border.top, border.top + 1, 0, width })) {
        border.top++;
    }
    if (border.top >= height) {
        return false;
    }

    while (height - 1 - border.bottom >= border.top) {
        const int row = height - 1 - border.bottom;
        if (!image_region_all_black(image, (PixelRect) { row, row + 1, 0, width })) {
            break;
        }
        border.bottom++;
    }

    // The columns only need checking between the content rows; the black rows above and below already cover the rest.
    const int content_start = border.top;
    const int content_end = height - border.bottom;

    // Some content row has a non-black pixel, so both column scans stop inside the image.
    while (image_region_all_black(image, (PixelRect) { content_start, content_end, border.left, border.left + 1 })) {
        border.left++;
    }
    while (image_region_all_black(image, (PixelRect) { content_start, content_end, width - 1 - border.right,
                                                       width - border.right })) {
        border.right++;
    }

    *out = border;
    return true;
}

// Crops the black margin by moving the data pointer, so no pixels are copied.
static void detect_and_remove_black_border(FlImageDecodeResult* image) {
    if (!has_pixels(image)) {
        return;
    }

    BorderInsets border;
    if (!measure_black_border(image, &border)) {
        return;
    }

    image->pixels += border.top * image->stride + border.left * image->channels;
    image->width -= border.left + border.right;
    image->height -= border.top + border.bottom;
}

// False for an invalid target or one the image already matches.
static bool size_needs_resize(const FlImageDecodeResult* image, ImageSize target) {
    if (target.width <= 0 || target.height <= 0) {
        return false;
    }
    return image->width != target.width || image->height != target.height;
}

// Returns true when the image is left at the requested size (either a successful resize or a no-op because it
// already matched / the target was invalid). Returns false when the resize failed: the image is unchanged and
// keeps its pre-resize size.
static bool resize_image(FlImageDecodeResult* image, ImageSize target, ImageDecodeResizeFilter filter, FlArena* arena) {
    if (!size_needs_resize(image, target)) {
        return true;
    }

    const int channels = image->channels;
    int output_stride = 0;
    size_t resized_size = 0;
    if (!image_checked_stride(target.width, (size_t)channels, &output_stride)
        || !image_checked_pixel_size(target.width, target.height, (size_t)channels, &resized_size)) {
        log_debug("Resize target %dx%d (%d channels) is too large", target.width, target.height, channels);
        return false;
    }
    u8* resized_data = arena_alloc_array(arena, u8, resized_size);

    stbir_filter stb_filter
        = (filter == ImageDecodeResizeFilter_Nearest) ? STBIR_FILTER_POINT_SAMPLE : STBIR_FILTER_DEFAULT;

    if (!stbir_resize(image->pixels, image->width, image->height, image->stride, resized_data, target.width,
                      target.height, 0, (stbir_pixel_layout)channels, STBIR_TYPE_UINT8, STBIR_EDGE_CLAMP,
                      stb_filter)) {
        return false;
    }

    image->pixels = resized_data;
    image->width = target.width;
    image->height = target.height;
    image->stride = output_stride;
    return true;
}

// The largest size within bounds that keeps the image's aspect ratio.
static ImageSize fit_aspect(const FlImageDecodeResult* image, ImageSize bounds) {
    if (!has_pixels(image) || bounds.height <= 0) {
        return bounds;
    }

    const float src_aspect = (float)image->width / (float)image->height;
    const float dst_aspect = (float)bounds.width / (float)bounds.height;

    if (src_aspect > dst_aspect) {
        return (ImageSize) { bounds.width, (int)(bounds.width / src_aspect + 0.5f) };
    }
    return (ImageSize) { (int)(bounds.height * src_aspect + 0.5f), bounds.height };
}

// Resizes to the requested target (shrinking to fit when the aspect ratio is preserved) and returns the canvas size
// the compose stage should fill, or {0, 0} when no target was asked for.
static ImageSize resize_to_target(FlImageDecodeResult* image, const ImageDecodeOptions* opts, FlArena* arena) {
    if (!image_has_target_size(opts)) {
        return (ImageSize) { 0, 0 };
    }

    const ImageSize canvas = image_scaled_target_size(opts);
    const ImageSize fitted = opts->preserve_aspect_ratio ? fit_aspect(image, canvas) : canvas;

    if (!resize_image(image, fitted, opts->resize_filter, arena)) {
        // The source keeps its pre-resize dimensions; the compose paths clamp offsets and blit extents.
        log_debug("Image resize to %dx%d failed; compositing un-resized source", fitted.width, fitted.height);
    }
    return canvas;
}

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

static void premultiply_alpha(FlImageDecodeResult* image) {
    if (image->channels != 4) {
        return;
    }

    const int simd_pixels = (image->width / 16) * 16;

    for (int row = 0; row < image->height; row++) {
        u8* row_data = image->pixels + row * image->stride;

        for (int i = 0; i < simd_pixels; i += 16) {
            u8x16_rgba_result rgba = u8x16_load_deinterleaved_4(row_data, i * 4);

            i16x8x2 alpha = u8x16_unpack_to_i16x8_low(rgba.a);

            u8x16 final_r = premul_channel_x16(rgba.r, alpha);
            u8x16 final_g = premul_channel_x16(rgba.g, alpha);
            u8x16 final_b = premul_channel_x16(rgba.b, alpha);

            u8x16_store_interleaved_4(row_data, final_r, final_g, final_b, rgba.a, i * 4);
        }

        for (int i = simd_pixels; i < image->width; i++) {
            const int a = row_data[i * 4 + 3];

            row_data[i * 4 + 0] = image_premul_channel(row_data[i * 4 + 0], a);
            row_data[i * 4 + 1] = image_premul_channel(row_data[i * 4 + 1], a);
            row_data[i * 4 + 2] = image_premul_channel(row_data[i * 4 + 2], a);
        }
    }

    image->premultiplied = true;
}

// Blend weights are fixed point: BLEND_ONE keeps the pixel, 0 gives the target.
#define BLEND_ONE 32767

// Blends 16 RGB pixels at x toward target; t.v0 / t.v1 are the weights for the low and high 8 pixels.
static void blend_toward_rgb_x16(u8* row, int x, Rgb8 target, i16x8x2 t) {
    const i16x8 target_r = i16x8_new_splat((int16_t)target.r);
    const i16x8 target_g = i16x8_new_splat((int16_t)target.g);
    const i16x8 target_b = i16x8_new_splat((int16_t)target.b);

    u8x16_rgb_result pixels = u8x16_load_deinterleaved_3(row, x * 3);

    i16x8x2 r = u8x16_unpack_to_i16x8_low(pixels.r);
    i16x8x2 g = u8x16_unpack_to_i16x8_low(pixels.g);
    i16x8x2 b = u8x16_unpack_to_i16x8_low(pixels.b);

    // Lerp: result = target + (original - target) * t / BLEND_ONE
    i16x8 r_lo = i16x8_lerp(target_r, r.v0, t.v0);
    i16x8 r_hi = i16x8_lerp(target_r, r.v1, t.v1);
    i16x8 g_lo = i16x8_lerp(target_g, g.v0, t.v0);
    i16x8 g_hi = i16x8_lerp(target_g, g.v1, t.v1);
    i16x8 b_lo = i16x8_lerp(target_b, b.v0, t.v0);
    i16x8 b_hi = i16x8_lerp(target_b, b.v1, t.v1);

    u8x16 r_out = u16x8_narrow_to_u8x16((u16x8) { .v = r_lo.v }, (u16x8) { .v = r_hi.v });
    u8x16 g_out = u16x8_narrow_to_u8x16((u16x8) { .v = g_lo.v }, (u16x8) { .v = g_hi.v });
    u8x16 b_out = u16x8_narrow_to_u8x16((u16x8) { .v = b_lo.v }, (u16x8) { .v = b_hi.v });

    u8x16_store_interleaved_3(row, r_out, g_out, b_out, x * 3);
}

static void blend_toward_rgb(u8* pixel, Rgb8 target, int t) {
    pixel[0] = (u8)(target.r + ((pixel[0] - target.r) * t) / BLEND_ONE);
    pixel[1] = (u8)(target.g + ((pixel[1] - target.g) * t) / BLEND_ONE);
    pixel[2] = (u8)(target.b + ((pixel[2] - target.b) * t) / BLEND_ONE);
}

// Tint gradient: from image_start_x the tint color fades into the image over a quarter of the image width.
static void apply_tint_gradient_rgb(FlImageDecodeResult* image, int image_start_x, Rgb8 tint, FlArena* arena) {
    const int image_width = image->width - image_start_x;
    if (image_width <= 1) {
        return;
    }

    const int gradient_width = (image_width / 4 < 2) ? 2 : image_width / 4;
    const int gradient_end_x = image_start_x + gradient_width;

    // Weight per gradient column. Past the gradient the weight is BLEND_ONE, which leaves the pixel as it is.
    i16* blend_table = arena_alloc_array(arena, i16, gradient_width);
    for (int i = 0; i < gradient_width; i++) {
        blend_table[i] = (i16)((i * BLEND_ONE) / (gradient_width - 1));
    }

    for (int y = 0; y < image->height; y++) {
        u8* row = image->pixels + y * image->stride;

        int x = image_start_x;
        for (; x + 16 <= gradient_end_x; x += 16) {
            const i16x8x2 t = { i16x8_load_unaligned(blend_table, x - image_start_x),
                                i16x8_load_unaligned(blend_table, x - image_start_x + 8) };
            blend_toward_rgb_x16(row, x, tint, t);
        }

        for (; x < gradient_end_x; x++) {
            blend_toward_rgb(row + x * 3, tint, blend_table[x - image_start_x]);
        }
    }
}

// Vertical fade: from fade_start (0.0 = top, 1.0 = bottom) each row blends from the original toward target.
static void apply_vertical_fade_rgb(FlImageDecodeResult* image, f32 fade_start, Rgb8 target) {
    const int height = image->height;
    if (image->channels != 3 || height <= 1) {
        return;
    }

    int fade_start_row = (int)(height * fade_start);
    if (fade_start_row < 0) {
        fade_start_row = 0;
    }
    if (fade_start_row >= height - 1) {
        return;
    }

    const int fade_height = height - fade_start_row;
    const int simd_pixels = (image->width / 16) * 16;

    for (int y = fade_start_row; y < height; y++) {
        u8* row = image->pixels + y * image->stride;
        const i16 t = (i16)(BLEND_ONE - ((y - fade_start_row) * BLEND_ONE) / (fade_height - 1));
        const i16x8 t_vec = i16x8_new_splat(t);
        const i16x8x2 t_pair = { t_vec, t_vec };

        for (int x = 0; x < simd_pixels; x += 16) {
            blend_toward_rgb_x16(row, x, target, t_pair);
        }

        for (int x = simd_pixels; x < image->width; x++) {
            blend_toward_rgb(row + x * 3, target, t);
        }
    }
}

// Copies source row y into dst_row (one canvas row), dropping pixels that land outside the canvas.
static void blit_row_rgb(const FlImageDecodeResult* image, int y, const ImageCanvas* canvas, u8* dst_row) {
    const int src_channels = image->channels;
    const u8* src_row = image->pixels + y * image->stride;

    for (int x = 0; x < image->width; x++) {
        const int dst_x = canvas->offset_x + x;
        if (dst_x < 0 || dst_x >= canvas->width) {
            continue;
        }

        const u8* src = src_row + x * src_channels;
        const u8 r = src[0];

        dst_row[dst_x * 3 + 0] = r;
        dst_row[dst_x * 3 + 1] = (src_channels >= 3) ? src[1] : r;
        dst_row[dst_x * 3 + 2] = (src_channels >= 3) ? src[2] : r;
    }
}

// Fills the canvas with its fill color, then blits the decoded RGB(A) image at (offset_x, offset_y). Source alpha is
// dropped (the canvas is always 3-channel). Callers pick the offset: right-align for the tint preview, centered for
// letterbox/pillarbox.
//
// Reads are channel-agnostic: 1-channel (grayscale) and 2-channel (grayscale+alpha) sources replicate the gray
// value into R/G/B. The blit is bounds-checked against the canvas, so an overhanging source is cropped.
void composite_source_into_canvas_rgb(const FlImageDecodeResult* image, const ImageCanvas* canvas) {
    for (int i = 0; i < canvas->width * canvas->height; i++) {
        canvas->pixels[i * 3 + 0] = canvas->fill.r;
        canvas->pixels[i * 3 + 1] = canvas->fill.g;
        canvas->pixels[i * 3 + 2] = canvas->fill.b;
    }

    for (int y = 0; y < image->height; y++) {
        const int dst_y = canvas->offset_y + y;
        if (dst_y >= 0 && dst_y < canvas->height) {
            blit_row_rgb(image, y, canvas, canvas->pixels + dst_y * canvas->width * 3);
        }
    }
}

// Replaces the image with a freshly allocated canvas holding it. Returns false, leaving the image untouched, when
// the canvas is too large to allocate. canvas.pixels is ignored.
static bool composite_on_canvas_rgb(FlImageDecodeResult* image, ImageCanvas canvas, FlArena* arena) {
    size_t canvas_size = 0;
    int canvas_stride = 0;
    if (!image_checked_pixel_size(canvas.width, canvas.height, 3, &canvas_size)
        || !image_checked_stride(canvas.width, 3, &canvas_stride)) {
        log_debug("Canvas %dx%d is too large to composite", canvas.width, canvas.height);
        return false;
    }
    canvas.pixels = arena_alloc_array(arena, u8, canvas_size);

    composite_source_into_canvas_rgb(image, &canvas);

    image->pixels = canvas.pixels;
    image->width = canvas.width;
    image->height = canvas.height;
    image->stride = canvas_stride;
    image->channels = 3;
    return true;
}

static int max_zero(int value) {
    return (value < 0) ? 0 : value;
}

// Places the image on a canvas of the target size per the background mode. Tint right-aligns it and fades the tint
// into its left edge; Center letterboxes it. An overhanging source gets offset 0 and is cropped by the blit.
static void apply_background(FlImageDecodeResult* image, ImageSize canvas_size, const ImageThumbnailOptions* tb,
                             FlArena* arena) {
    if (canvas_size.width <= 0 || canvas_size.height <= 0) {
        return;
    }

    const Rgb8 tint = { tb->tint_r, tb->tint_g, tb->tint_b };
    ImageCanvas canvas = { .width = canvas_size.width, .height = canvas_size.height, .fill = tint };

    switch (tb->background_mode) {
        case ImageDecodeBackgroundMode_Tint:
            canvas.offset_x = max_zero(canvas.width - image->width);
            if (composite_on_canvas_rgb(image, canvas, arena)) {
                apply_tint_gradient_rgb(image, canvas.offset_x, tint, arena);
            }
            break;
        case ImageDecodeBackgroundMode_Center:
            canvas.offset_x = max_zero((canvas.width - image->width) / 2);
            canvas.offset_y = max_zero((canvas.height - image->height) / 2);
            composite_on_canvas_rgb(image, canvas, arena);
            break;
        case ImageDecodeBackgroundMode_Regular:
            break;
    }
}

void image_thumbnail_compose(FlArena* arena, FlImageDecodeResult* result, const ImageDecodeOptions* decode_opts,
                             const ImageThumbnailOptions* thumb) {
    if (!result || !result->needs_thumbnail_compose) {
        return; // SVG decodes are already final
    }

    const ImageDecodeOptions opts = decode_opts ? *decode_opts : (ImageDecodeOptions) { 0 };
    const ImageThumbnailOptions tb = thumb ? *thumb : (ImageThumbnailOptions) { 0 };

    // Black border detection (before resize, on the raw decoded pixels)
    if (tb.detect_black_border) {
        detect_and_remove_black_border(result);
    }

    premultiply_alpha(result);

    const ImageSize canvas_size = resize_to_target(result, &opts, arena);

    if (tb.apply_vertical_fade) {
        apply_vertical_fade_rgb(result, tb.fade_start, (Rgb8) { tb.fade_r, tb.fade_g, tb.fade_b });
    }

    apply_background(result, canvas_size, &tb, arena);

    if (opts.detect_grayscale) {
        image_convert_grayscale_to_alpha(result, arena);
    }

    result->needs_thumbnail_compose = false;
}
