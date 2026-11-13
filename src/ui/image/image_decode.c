#include "image_decode.h"
#include <core/arena.h>
#include <core/core.h>
#include <core/log.h>
#include <core/simd.h>
#include <core/memory.h>
#include "nanosvg.h"
#include "nanosvgrast.h"
#include "stb_image.h"
#include <limits.h>
#include <math.h>
#include <stdint.h>

bool image_checked_pixel_size(int width, int height, size_t bytes_per_pixel, size_t* out_size) {
    if (width <= 0 || height <= 0) {
        return false;
    }
    if (bytes_per_pixel == 0) {
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

bool image_checked_stride(int width, size_t bytes_per_pixel, int* out_stride) {
    size_t row_bytes = 0;
    if (!image_checked_pixel_size(width, 1, bytes_per_pixel, &row_bytes) || row_bytes > INT_MAX) {
        return false;
    }
    *out_stride = (int)row_bytes;
    return true;
}

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

static bool is_grayscale_rgba_simd(const u8* rgba_data, int width, int height, int stride) {
    for_count(y, height) {
        if (!is_grayscale_rgba_row(rgba_data + (size_t)y * (size_t)stride, width)) {
            return false;
        }
    }

    return true;
}

static bool is_svg_data(const u8* data, size_t size) {
    if (size < 4) {
        return false;
    }

    if (memory_compare(data, "<svg", 4) == 0) {
        return true;
    }

    // An XML prolog may come first, so look for the <svg tag in the opening bytes.
    if (size >= 5 && memory_compare(data, "<?xml", 5) == 0) {
        size_t search_limit = (size < 512) ? size : 512;
        for_count(i, search_limit - 4) {
            if (memory_compare(data + i, "<svg", 4) == 0) {
                return true;
            }
        }
    }

    return false;
}

bool image_has_target_size(const ImageDecodeOptions* opts) {
    return opts->target_width > 0 && opts->target_height > 0;
}

static float effective_ui_scale(const ImageDecodeOptions* opts) {
    return (opts->ui_scale > 0.0f) ? opts->ui_scale : 1.0f;
}

ImageSize image_scaled_target_size(const ImageDecodeOptions* opts) {
    const float ui_scale = effective_ui_scale(opts);
    return (ImageSize) { (int)(opts->target_width * ui_scale + 0.5f), (int)(opts->target_height * ui_scale + 0.5f) };
}

void image_convert_grayscale_to_alpha(FlImageDecodeResult* image, FlArena* arena) {
    if (image->channels != 4) {
        return;
    }

    if (!is_grayscale_rgba_simd(image->pixels, image->width, image->height, image->stride)) {
        return;
    }

    size_t pixel_count = 0;
    if (!image_checked_pixel_size(image->width, image->height, 1, &pixel_count)) {
        log_debug("Grayscale image %dx%d is too large to convert", image->width, image->height);
        return;
    }
    u8* alpha_data = arena_alloc_array(arena, u8, pixel_count);

    for_count(y, image->height) {
        const u8* src_row = image->pixels + (size_t)y * (size_t)image->stride;
        u8* dst_row = alpha_data + (size_t)y * (size_t)image->width;
        for_count(x, image->width) {
            u8 r = src_row[x * 4 + 0];
            u8 a = src_row[x * 4 + 3];
            dst_row[x] = (a > 0) ? r : 0;
        }
    }

    image->pixels = alpha_data;
    image->stride = image->width;
    image->channels = 1;
}

// Largest rasterized SVG edge. nanosvg indexes its bitmap with int row * stride arithmetic, so the raster must keep
// stride * height within INT_MAX: 8192 * 4 * 8192 is 268M, well inside it.
#define IMAGE_SVG_MAX_DIMENSION 8192

// The SVG sized to the (scaled) target, shrunk to its own aspect ratio when asked.
static ImageSize svg_target_size(const NSVGimage* image, const ImageDecodeOptions* opts) {
    const ImageSize target = image_scaled_target_size(opts);
    if (!opts->preserve_aspect_ratio) {
        return target;
    }

    const float scale_w = (float)target.width / image->width;
    const float scale_h = (float)target.height / image->height;
    const float scale = (scale_w < scale_h) ? scale_w : scale_h;
    return (ImageSize) { (int)(image->width * scale + 0.5f), (int)(image->height * scale + 0.5f) };
}

// The SVG's declared size scaled by svg_scale_factor and ui_scale. Returns false for an absurd size, checked in
// float before the int conversion.
static bool svg_declared_size(const NSVGimage* image, const ImageDecodeOptions* opts, ImageSize* out) {
    const float base_scale = (opts->svg_scale_factor > 0.0f) ? opts->svg_scale_factor : 1.0f;
    const float scale_factor = base_scale * effective_ui_scale(opts);
    if (image->width * scale_factor > IMAGE_SVG_MAX_DIMENSION || image->height * scale_factor > IMAGE_SVG_MAX_DIMENSION) {
        log_debug("SVG declared size %.0fx%.0f is too large to rasterize", image->width, image->height);
        return false;
    }

    *out = (ImageSize) { (int)(image->width * scale_factor + 0.5f), (int)(image->height * scale_factor + 0.5f) };
    return true;
}

// Picks the raster size: the target size when one was asked for, else the declared size. Returns false when the
// size is unusable.
static bool svg_raster_size(const NSVGimage* image, const ImageDecodeOptions* opts, ImageSize* out) {
    ImageSize size;
    if (image_has_target_size(opts)) {
        size = svg_target_size(image, opts);
    } else if (!svg_declared_size(image, opts, &size)) {
        return false;
    }

    if (size.width <= 0 || size.height <= 0) {
        log_debug("Invalid SVG dimensions: %dx%d", size.width, size.height);
        return false;
    }

    if (size.width > IMAGE_SVG_MAX_DIMENSION || size.height > IMAGE_SVG_MAX_DIMENSION) {
        log_debug("SVG raster size %dx%d exceeds the %d limit", size.width, size.height, IMAGE_SVG_MAX_DIMENSION);
        return false;
    }

    *out = size;
    return true;
}

// Rasterizes the parsed SVG into a new RGBA result. Returns nullptr on an unusable size or allocation failure.
static FlImageDecodeResult* rasterize_svg(NSVGimage* image, const ImageDecodeOptions* opts, FlArena* arena) {
    ImageSize size;
    if (!svg_raster_size(image, opts, &size)) {
        return nullptr;
    }

    size_t pixel_size = 0;
    int stride = 0;
    if (!image_checked_pixel_size(size.width, size.height, 4, &pixel_size)
        || !image_checked_stride(size.width, 4, &stride)) {
        log_debug("SVG dimensions %dx%d are too large to rasterize", size.width, size.height);
        return nullptr;
    }

    NSVGrasterizer* rast = nsvgCreateRasterizer();
    if (!rast) {
        log_debug("Failed to create SVG rasterizer");
        return nullptr;
    }

    u8* pixel_data = arena_alloc_array(arena, u8, pixel_size);
    nsvgRasterize(rast, image, 0, 0, (float)size.width / image->width, pixel_data, size.width,
                  size.height, stride);
    nsvgDeleteRasterizer(rast);

    FlImageDecodeResult* result = arena_alloc(arena, FlImageDecodeResult);
    *result = (FlImageDecodeResult) {
        .pixels = pixel_data,
        .width = size.width,
        .height = size.height,
        .stride = stride,
        .channels = 4,
        .premultiplied = true, // nanosvg produces premultiplied output
    };
    return result;
}

static FlImageDecodeResult* decode_svg(const u8* data, size_t size, const ImageDecodeOptions* opts, FlArena* arena) {
    arena_scratch_auto_conflict(temp, arena);

    char* svg_string = arena_alloc_array(temp.arena, char, size + 1);
    memory_copy(svg_string, size, data, size);
    svg_string[size] = '\0';

    NSVGimage* image = nsvgParse(svg_string, "px", 96.0f);
    if (!image) {
        log_debug("Failed to parse SVG data (%zu bytes)", size);
        return nullptr;
    }

    FlImageDecodeResult* result = rasterize_svg(image, opts, arena);
    nsvgDelete(image);

    if (result && opts->detect_grayscale) {
        image_convert_grayscale_to_alpha(result, arena);
    }
    return result;
}

// Decodes a raster image with stb into arena memory, leaving it raw for image_thumbnail_compose() to finish. Returns
// nullptr when it cannot be decoded or is too large.
static FlImageDecodeResult* decode_raster(FlArena* arena, const u8* data, size_t size, bool force_rgb) {
    int width, height, channels;
    u8* stb_data = stbi_load_from_memory(data, (int)size, &width, &height, &channels, force_rgb ? 3 : 0);
    if (!stb_data) {
        log_debug("Failed to decode image data (%zu bytes): %s", size, stbi_failure_reason());
        return nullptr;
    }
    if (force_rgb) {
        channels = 3;
    }

    size_t image_size = 0;
    int stride = 0;
    if (!image_checked_pixel_size(width, height, (size_t)channels, &image_size)
        || !image_checked_stride(width, (size_t)channels, &stride)) {
        log_debug("Decoded image %dx%d (%d channels) is too large", width, height, channels);
        stbi_image_free(stb_data);
        return nullptr;
    }
    u8* arena_data = arena_alloc_array(arena, u8, image_size);
    memory_copy(arena_data, image_size, stb_data, image_size);
    stbi_image_free(stb_data);

    FlImageDecodeResult* result = arena_alloc(arena, FlImageDecodeResult);
    *result = (FlImageDecodeResult) {
        .pixels = arena_data,
        .width = width,
        .height = height,
        .stride = stride,
        .channels = (u8)channels,
        .needs_thumbnail_compose = true,
    };
    return result;
}

FlImageDecodeResult* image_decode(FlArena* arena, const void* file_data, size_t file_size,
                                  const ImageDecodeOptions* options) {
    if (!file_data || file_size == 0) {
        return nullptr;
    }

    const u8* data = (const u8*)file_data;
    ImageDecodeOptions opts = options ? *options : (ImageDecodeOptions) { 0 };

    if (is_svg_data(data, file_size)) {
        return decode_svg(data, file_size, &opts, arena);
    }

    return decode_raster(arena, data, file_size, opts.force_rgb);
}

