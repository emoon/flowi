#include "image_raw_pixels.h"
#include <core/memory.h>

// Widen pixel_count pixels of src_channels bytes each to 4-byte RGBA, applying the source alpha to RGB and keeping it.
static void expand_premultiplied_rgba(u8* dst, const u8* src, size_t pixel_count, size_t src_channels) {
    for (size_t i = 0; i < pixel_count; i++) {
        const u8* s = src + i * src_channels;
        const u8 a = s[3];
        dst[i * 4u + 0] = image_premul_channel(s[0], a);
        dst[i * 4u + 1] = image_premul_channel(s[1], a);
        dst[i * 4u + 2] = image_premul_channel(s[2], a);
        dst[i * 4u + 3] = a;
    }
}

// Widen to 4-byte RGBA with opaque alpha; any fourth source byte is dropped.
static void expand_opaque_rgba(u8* dst, const u8* src, size_t pixel_count, size_t src_channels) {
    for (size_t i = 0; i < pixel_count; i++) {
        const u8* s = src + i * src_channels;
        dst[i * 4u + 0] = s[0];
        dst[i * 4u + 1] = s[1];
        dst[i * 4u + 2] = s[2];
        dst[i * 4u + 3] = 255;
    }
}

static bool convert_alpha(FlArena* arena, const u8* src, RawPixelShape shape, FlImageDecodeResult* out) {
    size_t size = 0;
    if (!image_checked_pixel_size(shape.width, shape.height, 1, &size)) {
        return false;
    }

    u8* dst = arena_alloc_array(arena, u8, size);
    memory_copy(dst, size, src, size);
    out->pixels = dst;
    out->stride = shape.width;
    out->channels = 1;
    return true;
}

static bool convert_to_rgba(FlArena* arena, const u8* src, RawPixelShape shape, FlImageDecodeResult* out) {
    const size_t src_channels = (shape.format == FlImageFormat_RGB) ? 3u : 4u;
    size_t src_size = 0;
    size_t dst_size = 0;
    if (!image_checked_pixel_size(shape.width, shape.height, src_channels, &src_size)
        || !image_checked_pixel_size(shape.width, shape.height, 4, &dst_size)) {
        return false;
    }

    u8* dst = arena_alloc_array(arena, u8, dst_size);
    const size_t pixel_count = (size_t)shape.width * (size_t)shape.height;
    if (shape.format == FlImageFormat_RGBA) {
        expand_premultiplied_rgba(dst, src, pixel_count, src_channels);
    } else {
        expand_opaque_rgba(dst, src, pixel_count, src_channels);
    }

    out->pixels = dst;
    out->stride = shape.width * 4;
    out->channels = 4;
    return true;
}

bool image_raw_pixels_to_decoded(FlArena* arena, const void* pixels, RawPixelShape shape, FlImageDecodeResult* out) {
    const u8* src = (const u8*)pixels;
    bool converted = false;

    switch (shape.format) {
        case FlImageFormat_Alpha:
            converted = convert_alpha(arena, src, shape, out);
            break;
        case FlImageFormat_RGB:
        case FlImageFormat_XRGB:
        case FlImageFormat_RGBA:
            converted = convert_to_rgba(arena, src, shape, out);
            break;
        case FlImageFormat_Default:
        case FlImageFormat_Deinterleaved:
            break;
    }
    if (!converted) {
        return false;
    }

    out->width = shape.width;
    out->height = shape.height;
    out->premultiplied = out->channels == 4;
    out->needs_thumbnail_compose = false;
    return true;
}
