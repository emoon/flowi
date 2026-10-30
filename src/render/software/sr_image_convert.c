// -----------------------------------------------------------------------------
// Image Format Conversion for Software Renderer
// -----------------------------------------------------------------------------

#include "sr_image_convert.h"
#include <core/arena.h>
#include <core/simd.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// r/g/b read bytes 0/1/2 of each pixel; alpha (byte 3) is written only when keep_alpha is set (which
// requires src_channels == 4).

void sr_deinterleave_pixels(const u8* src, u8* r, u8* g, u8* b, u8* a, int count, int src_channels, bool keep_alpha) {
    int simd_count = (count / 16) * 16;
    int i = 0;

    if (src_channels == 4) {
        for (; i < simd_count; i += 16) {
            u8x16_rgba_result px = u8x16_load_deinterleaved_4(src, i * 4);
            u8x16_store_unaligned(px.r, r, i);
            u8x16_store_unaligned(px.g, g, i);
            u8x16_store_unaligned(px.b, b, i);
            if (keep_alpha) {
                u8x16_store_unaligned(px.a, a, i);
            }
        }
    } else {
        for (; i < simd_count; i += 16) {
            u8x16_rgb_result px = u8x16_load_deinterleaved_3(src, i * 3);
            u8x16_store_unaligned(px.r, r, i);
            u8x16_store_unaligned(px.g, g, i);
            u8x16_store_unaligned(px.b, b, i);
        }
    }

    for (; i < count; i++) {
        r[i] = src[i * src_channels + 0];
        g[i] = src[i * src_channels + 1];
        b[i] = src[i * src_channels + 2];
        if (keep_alpha) {
            a[i] = src[i * src_channels + 3];
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ImageData* sr_convert_to_planar(const ImageData* src, bool keep_alpha, struct FlArena* arena) {
    if (src == nullptr || arena == nullptr) {
        return nullptr;
    }

    if ((src->format != FlImageFormat_RGBA && src->format != FlImageFormat_XRGB) || src->channels != 4) {
        return nullptr;
    }

    // A zero (or negative) extent has no last pixel/row to duplicate: the edge padding below would read
    // one byte before the plane, and pad_plane_last_row a whole row before the allocation.
    if (src->width <= 0 || src->height <= 0) {
        return nullptr;
    }

    int dst_channels = keep_alpha ? 4 : 3;

    ImageData* dst = arena_alloc_zero(arena, ImageData);

    dst->width = src->width;
    dst->height = src->height;
    // Padding: +1 for edge pixel duplication
    dst->padded_width = src->width + 1;
    dst->padded_height = src->height + 1;
    dst->stride = dst->padded_width;
    dst->channels = dst_channels;
    dst->format = FlImageFormat_Deinterleaved;

    int plane_size = dst->padded_width * dst->padded_height;
    int total_size = plane_size * dst_channels;

    // Guard region for the scaled sampler's fixed 32-byte window (see SR_SCALE_PLANE_LOAD_GUARD in the
    // header); zeroed so the over-read is deterministic under sanitizers.
    const int simd_load_guard = SR_SCALE_PLANE_LOAD_GUARD;

    u8* planar_data = arena_alloc_array(arena, u8, total_size + simd_load_guard);
    dst->data = planar_data;
    for (int i = 0; i < simd_load_guard; i++) {
        planar_data[total_size + i] = 0;
    }

    u8* r_plane = planar_data;
    u8* g_plane = planar_data + plane_size;
    u8* b_plane = planar_data + plane_size * 2;
    u8* a_plane = keep_alpha ? planar_data + plane_size * 3 : nullptr;

    int src_stride = src->stride;
    int dst_stride = dst->padded_width;

    for_count(y, src->height) {
        const u8* src_row = src->data + y * src_stride;
        u8* r_row = r_plane + y * dst_stride;
        u8* g_row = g_plane + y * dst_stride;
        u8* b_row = b_plane + y * dst_stride;
        u8* a_row = keep_alpha ? a_plane + y * dst_stride : nullptr;

        sr_deinterleave_pixels(src_row, r_row, g_row, b_row, a_row, src->width, 4, keep_alpha);

        r_row[src->width] = r_row[src->width - 1];
        g_row[src->width] = g_row[src->width - 1];
        b_row[src->width] = b_row[src->width - 1];
        if (keep_alpha) {
            a_row[src->width] = a_row[src->width - 1];
        }
    }

    pad_plane_last_row(r_plane, src->height, dst_stride, dst->padded_width);
    pad_plane_last_row(g_plane, src->height, dst_stride, dst->padded_width);
    pad_plane_last_row(b_plane, src->height, dst_stride, dst->padded_width);
    if (keep_alpha) {
        pad_plane_last_row(a_plane, src->height, dst_stride, dst->padded_width);
    }

    return dst;
}
