#include "sr_image_convert.h"
#include <core/arena.h>
#include <core/simd.h>

// r/g/b read bytes 0/1/2 of each pixel; alpha (byte 3) is written only when keep_alpha is set (which
// requires src_channels == 4).

typedef struct RgbBlock {
    u8x16 r;
    u8x16 g;
    u8x16 b;
} RgbBlock;

static void store_rgb_block(RgbBlock px, const PlanarPlanes* out, int i) {
    u8x16_store_unaligned(px.r, out->r, i);
    u8x16_store_unaligned(px.g, out->g, i);
    u8x16_store_unaligned(px.b, out->b, i);
}

typedef struct PixelBlock {
    RgbBlock rgb;
    u8x16 a;
} PixelBlock;

static PixelBlock load_pixel_block(const u8* src, int src_channels, int i) {
    if (src_channels == 4) {
        u8x16_rgba_result px = u8x16_load_deinterleaved_4(src, i * 4);
        return (PixelBlock) { .rgb = { px.r, px.g, px.b }, .a = px.a };
    }

    u8x16_rgb_result px = u8x16_load_deinterleaved_3(src, i * 3);
    return (PixelBlock) { .rgb = { px.r, px.g, px.b } };
}

static int deinterleave_blocks(const u8* src, int simd_count, int src_channels, const PlanarPlanes* out) {
    int i = 0;
    for (; i < simd_count; i += 16) {
        const PixelBlock px = load_pixel_block(src, src_channels, i);
        store_rgb_block(px.rgb, out, i);
        if (out->keep_alpha) {
            u8x16_store_unaligned(px.a, out->a, i);
        }
    }
    return i;
}

void sr_deinterleave_pixels(const u8* src, int count, int src_channels, const PlanarPlanes* out) {
    const int simd_count = (count / 16) * 16;
    int i = deinterleave_blocks(src, simd_count, src_channels, out);

    for (; i < count; i++) {
        out->r[i] = src[i * src_channels + 0];
        out->g[i] = src[i * src_channels + 1];
        out->b[i] = src[i * src_channels + 2];
        if (out->keep_alpha) {
            out->a[i] = src[i * src_channels + 3];
        }
    }
}

static void pad_plane_last_row(u8* plane, int real_rows, int stride, int width) {
    const u8* last = plane + (real_rows - 1) * stride;
    u8* pad = plane + real_rows * stride;

    int x = 0;
    for (; x + 16 <= width; x += 16) {
        u8x16_store_unaligned(u8x16_load_unaligned(last, x), pad, x);
    }
    for (; x < width; x++) {
        pad[x] = last[x];
    }
}

static void pad_planes_last_row(const PlanarPlanes* planes, int real_rows, int stride, int width) {
    pad_plane_last_row(planes->r, real_rows, stride, width);
    pad_plane_last_row(planes->g, real_rows, stride, width);
    pad_plane_last_row(planes->b, real_rows, stride, width);
    if (planes->keep_alpha) {
        pad_plane_last_row(planes->a, real_rows, stride, width);
    }
}

static PlanarPlanes planar_planes_offset(const PlanarPlanes* planes, int offset) {
    return (PlanarPlanes) {
        .r = planes->r + offset,
        .g = planes->g + offset,
        .b = planes->b + offset,
        .a = planes->keep_alpha ? planes->a + offset : nullptr,
        .keep_alpha = planes->keep_alpha,
    };
}

// Duplicate the last pixel of the row into the padding column.
static void pad_row_edge(const PlanarPlanes* row, int width) {
    row->r[width] = row->r[width - 1];
    row->g[width] = row->g[width - 1];
    row->b[width] = row->b[width - 1];
    if (row->keep_alpha) {
        row->a[width] = row->a[width - 1];
    }
}

static bool source_is_convertible(const ImageData* src) {
    if (src == nullptr) {
        return false;
    }

    const bool is_4ch_rgba_like
        = (src->format == FlImageFormat_RGBA || src->format == FlImageFormat_XRGB) && src->channels == 4;

    // A zero (or negative) extent has no last pixel/row to duplicate: the edge padding would read one byte before
    // the plane, and pad_plane_last_row a whole row before the allocation.
    return is_4ch_rgba_like && src->width > 0 && src->height > 0;
}

static ImageData* planar_image_alloc(struct FlArena* arena, const ImageData* src, bool keep_alpha) {
    const int dst_channels = keep_alpha ? 4 : 3;
    ImageData* dst = arena_alloc_zero(arena, ImageData);

    dst->width = src->width;
    dst->height = src->height;
    // Padding: +1 for edge pixel duplication
    dst->padded_width = src->width + 1;
    dst->padded_height = src->height + 1;
    dst->stride = dst->padded_width;
    dst->channels = dst_channels;
    dst->format = FlImageFormat_Deinterleaved;

    const int plane_size = dst->padded_width * dst->padded_height;
    const int total_size = plane_size * dst_channels;

    // Guard region for the scaled sampler's fixed 32-byte window (see SR_SCALE_PLANE_LOAD_GUARD in the
    // header); zeroed so the over-read is deterministic under sanitizers.
    const int simd_load_guard = SR_SCALE_PLANE_LOAD_GUARD;

    u8* planar_data = arena_alloc_array(arena, u8, total_size + simd_load_guard);
    dst->data = planar_data;
    for (int i = 0; i < simd_load_guard; i++) {
        planar_data[total_size + i] = 0;
    }

    return dst;
}

ImageData* sr_convert_to_planar(const ImageData* src, bool keep_alpha, struct FlArena* arena) {
    if (arena == nullptr || !source_is_convertible(src)) {
        return nullptr;
    }

    ImageData* dst = planar_image_alloc(arena, src, keep_alpha);

    const int plane_size = dst->padded_width * dst->padded_height;
    const int dst_stride = dst->padded_width;
    u8* planar_data = dst->data;

    const PlanarPlanes planes = {
        .r = planar_data,
        .g = planar_data + plane_size,
        .b = planar_data + plane_size * 2,
        .a = keep_alpha ? planar_data + plane_size * 3 : nullptr,
        .keep_alpha = keep_alpha,
    };

    for_count(y, src->height) {
        const PlanarPlanes row = planar_planes_offset(&planes, y * dst_stride);

        sr_deinterleave_pixels(src->data + y * src->stride, src->width, 4, &row);
        pad_row_edge(&row, src->width);
    }

    pad_planes_last_row(&planes, src->height, dst_stride, dst->padded_width);

    return dst;
}
