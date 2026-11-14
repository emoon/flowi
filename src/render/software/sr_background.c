#include "sr_background.h"
#include "software_renderer_internal.h"
#include "sr_image_backend.h"

#include <core/perf_scope.h>
#include <string.h>
#include <ui/image/image_cache.h>
#include <ui/image/image_decode.h>

// 16 pixels of planar Q7 color, as two i16x8 halves per channel.
typedef struct Chunk16 {
    i16x8x2 r, g, b;
} Chunk16;

typedef enum CopyMode {
    CopyMode_Rgb,
    CopyMode_RgbBlend,
    CopyMode_Rgba,
    CopyMode_RgbaBlend,
} CopyMode;

static ALWAYS_INLINE Chunk16 widen_rgb(u8x16_rgb_result px) {
    return (Chunk16) {
        .r = u8x16_unpack_to_i16x8_wide_shl(px.r, 7),
        .g = u8x16_unpack_to_i16x8_wide_shl(px.g, 7),
        .b = u8x16_unpack_to_i16x8_wide_shl(px.b, 7),
    };
}

static ALWAYS_INLINE Chunk16 widen_rgba(u8x16_rgba_result px) {
    return (Chunk16) {
        .r = u8x16_unpack_to_i16x8_wide_shl(px.r, 7),
        .g = u8x16_unpack_to_i16x8_wide_shl(px.g, 7),
        .b = u8x16_unpack_to_i16x8_wide_shl(px.b, 7),
    };
}

static ALWAYS_INLINE Chunk16 load_rgb_chunk(const u8* row, int k) {
    return widen_rgb(u8x16_load_deinterleaved_3(row, k * 48));
}

static ALWAYS_INLINE Chunk16 load_rgba_chunk(const u8* row, int k) {
    return widen_rgba(u8x16_load_deinterleaved_4(row, k * 64));
}

static ALWAYS_INLINE Chunk16 splat_chunk(u8 r, u8 g, u8 b) {
    const i16x8 rv = i16x8_new_splat((i16)r << 7);
    const i16x8 gv = i16x8_new_splat((i16)g << 7);
    const i16x8 bv = i16x8_new_splat((i16)b << 7);
    return (Chunk16) { .r = { rv, rv }, .g = { gv, gv }, .b = { bv, bv } };
}

static ALWAYS_INLINE Chunk16 lerp_chunks(Chunk16 from, Chunk16 to, i16x8 t) {
    return (Chunk16) {
        .r = { i16x8_lerp(from.r.v0, to.r.v0, t), i16x8_lerp(from.r.v1, to.r.v1, t) },
        .g = { i16x8_lerp(from.g.v0, to.g.v0, t), i16x8_lerp(from.g.v1, to.g.v1, t) },
        .b = { i16x8_lerp(from.b.v0, to.b.v0, t), i16x8_lerp(from.b.v1, to.b.v1, t) },
    };
}

static ALWAYS_INLINE void store_chunk(i16* tile_row, int k, Chunk16 c) {
    i16* dst = tile_row + k * 16;
    i16x8_store_unaligned(c.r.v0, dst, R_PLANE_START_OFFSET);
    i16x8_store_unaligned(c.g.v0, dst, G_PLANE_START_OFFSET);
    i16x8_store_unaligned(c.b.v0, dst, B_PLANE_START_OFFSET);
    i16x8_store_unaligned(c.r.v1, dst + 8, R_PLANE_START_OFFSET);
    i16x8_store_unaligned(c.g.v1, dst + 8, G_PLANE_START_OFFSET);
    i16x8_store_unaligned(c.b.v1, dst + 8, B_PLANE_START_OFFSET);
}

static ALWAYS_INLINE u8x16 lerp_pack_channel(u8x16 from, u8x16 to, i16x8 t) {
    const i16x8x2 a = u8x16_unpack_to_i16x8_wide_shl(from, 7);
    const i16x8x2 b = u8x16_unpack_to_i16x8_wide_shl(to, 7);
    const i16x8x2 blended = { i16x8_lerp(a.v0, b.v0, t), i16x8_lerp(a.v1, b.v1, t) };
    return i16x8x2_pack_to_u8x16_narrow_shr(blended, 7);
}

// Convert a 0-255 factor to the Q15 lerp factor.
static i16x8 lerp_factor(u8 value) {
    return i16x8_new_splat((i16)(((i32)value << 7) + (value >> 1)));
}

static int bytes_per_pixel(CopyMode mode) {
    return (mode == CopyMode_Rgba || mode == CopyMode_RgbaBlend) ? 4 : 3;
}

static ALWAYS_INLINE Chunk16 load_chunk(const u8* row, int k, CopyMode mode) {
    if (mode == CopyMode_Rgba || mode == CopyMode_RgbaBlend) {
        return load_rgba_chunk(row, k);
    }
    return load_rgb_chunk(row, k);
}

// The image is pre-scaled to screen size, so no bounds checking is needed. Reads interleaved RGB or RGBA and
// converts to planar Q7. mode is a constant at every call site so each variant compiles to its own loop.
static ALWAYS_INLINE void copy_image_to_tile(i16* tile_buffer, const SrBackground* bg, TileRect rect, CopyMode mode) {
    const u8* src = bg->image->data;
    const int stride = bg->image->stride;
    const bool needs_blend = mode == CopyMode_RgbBlend || mode == CopyMode_RgbaBlend;
    const i16x8 alpha_vec = lerp_factor(bg->alpha);
    const Chunk16 clear = splat_chunk(bg->clear_r, bg->clear_g, bg->clear_b);

    for (int j = 0; j < rect.height; j++) {
        const u8* row = src + (rect.y0 + j) * stride + rect.x0 * bytes_per_pixel(mode);
        i16* tile_row = tile_buffer + j * TILE_STRIDE;

        for (int k = 0; k < (TILE_WIDTH >> 4); k++) {
            Chunk16 px = load_chunk(row, k, mode);
            if (needs_blend) {
                px = lerp_chunks(clear, px, alpha_vec);
            }
            store_chunk(tile_row, k, px);
        }
    }
}

static void copy_background_to_tile(i16* tile_buffer, const SrBackground* bg, TileRect rect) {
    const bool is_rgba = bg->image->channels == 4;
    const bool needs_blend = bg->alpha < 255;

    if (is_rgba && needs_blend) {
        copy_image_to_tile(tile_buffer, bg, rect, CopyMode_RgbaBlend);
    } else if (is_rgba) {
        copy_image_to_tile(tile_buffer, bg, rect, CopyMode_Rgba);
    } else if (needs_blend) {
        copy_image_to_tile(tile_buffer, bg, rect, CopyMode_RgbBlend);
    } else {
        copy_image_to_tile(tile_buffer, bg, rect, CopyMode_Rgb);
    }
}

// result = prev * (1-t) + curr * t, with t=0 meaning full prev and t=255 full curr. Both images are screen-sized RGB.
static void crossfade_backgrounds_to_tile(i16* tile_buffer, const SrBackground* bg, TileRect rect) {
    const u8* prev_src = bg->prev_image->data;
    const u8* curr_src = bg->image->data;
    const int prev_stride = bg->prev_image->stride;
    const int curr_stride = bg->image->stride;
    const i16x8 t_vec = lerp_factor(bg->crossfade_t);

    for (int j = 0; j < rect.height; j++) {
        const u8* prev_row = prev_src + (rect.y0 + j) * prev_stride + rect.x0 * 3;
        const u8* curr_row = curr_src + (rect.y0 + j) * curr_stride + rect.x0 * 3;
        i16* tile_row = tile_buffer + j * TILE_STRIDE;

        for (int k = 0; k < (TILE_WIDTH >> 4); k++) {
            store_chunk(tile_row, k, lerp_chunks(load_rgb_chunk(prev_row, k), load_rgb_chunk(curr_row, k), t_vec));
        }
    }
}

static void clear_tile_buffer(i16* tile_buffer, const SrBackground* bg) {
    const Chunk16 clear = splat_chunk(bg->clear_r, bg->clear_g, bg->clear_b);

    for_count(y, TILE_HEIGHT) {
        for_count(x, (TILE_WIDTH >> 3)) {
            i16x8_store_unaligned(clear.r.v0, tile_buffer, R_PLANE_START_OFFSET);
            i16x8_store_unaligned(clear.g.v0, tile_buffer, G_PLANE_START_OFFSET);
            i16x8_store_unaligned(clear.b.v0, tile_buffer, B_PLANE_START_OFFSET);
            tile_buffer += 8;
        }

        tile_buffer += (TILE_STRIDE - TILE_WIDTH);
    }
}

// Scalar tail for the rightmost width & 15 columns crossfade_direct_to_output skips; same widen<<7 / Q15
// lerp / narrow>>7 math.
static NO_INLINE void crossfade_tail_to_output(u8* output, int output_pitch, const SrBackground* bg, TileRect rect) {
    const u8* prev_src = bg->prev_image->data;
    const u8* curr_src = bg->image->data;
    const int prev_stride = bg->prev_image->stride;
    const int curr_stride = bg->image->stride;
    const int t_q15 = ((int)bg->crossfade_t << 7) + (bg->crossfade_t >> 1);
    const int first_tail = rect.width & ~15; // == (width >> 4) * 16

    for (int j = 0; j < rect.height; j++) {
        const u8* prev_row = prev_src + (rect.y0 + j) * prev_stride + rect.x0 * 3;
        const u8* curr_row = curr_src + (rect.y0 + j) * curr_stride + rect.x0 * 3;
        u8* out_row = output + (rect.y0 + j) * output_pitch + rect.x0 * 3;
        for (int k = first_tail * 3; k < rect.width * 3; k++) {
            const int start16 = (int)prev_row[k] << 7;
            const int delta = ((int)curr_row[k] << 7) - start16;
            const int lerp = start16 + ((delta * t_q15 + 0x4000) >> 15);
            out_row[k] = narrow_shr7_u8((i16)lerp);
        }
    }
}

// Keeps widening, lerping and narrowing in registers instead of going through the planar tile buffer.
static void crossfade_direct_to_output(u8* output, int output_pitch, const SrBackground* bg, TileRect rect) {
    const u8* prev_src = bg->prev_image->data;
    const u8* curr_src = bg->image->data;
    const int prev_stride = bg->prev_image->stride;
    const int curr_stride = bg->image->stride;
    const i16x8 t_vec = lerp_factor(bg->crossfade_t);

    for (int j = 0; j < rect.height; j++) {
        const u8* prev_row = prev_src + (rect.y0 + j) * prev_stride + rect.x0 * 3;
        const u8* curr_row = curr_src + (rect.y0 + j) * curr_stride + rect.x0 * 3;
        u8* out_row = output + (rect.y0 + j) * output_pitch + rect.x0 * 3;

        for (int k = 0; k < (rect.width >> 4); k++) {
            const u8x16_rgb_result p = u8x16_load_deinterleaved_3(prev_row, k * 48);
            const u8x16_rgb_result c = u8x16_load_deinterleaved_3(curr_row, k * 48);
            u8x16_store_interleaved_3(out_row, lerp_pack_channel(p.r, c.r, t_vec), lerp_pack_channel(p.g, c.g, t_vec),
                                      lerp_pack_channel(p.b, c.b, t_vec), k * 48);
        }
    }

    // Non-16-aligned tile widths leave a rightmost tail the SIMD loop skips.
    if ((rect.width & 15) != 0) {
        crossfade_tail_to_output(output, output_pitch, bg, rect);
    }
}

static void copy_background_direct_to_output(u8* output, int output_pitch, const ImageData* image, TileRect rect) {
    const u8* src = image->data;
    const int stride = image->stride;
    const int row_bytes = rect.width * 3;

    for (int j = 0; j < rect.height; j++) {
        const u8* src_row = src + (rect.y0 + j) * stride + rect.x0 * 3;
        u8* out_row = output + (rect.y0 + j) * output_pitch + rect.x0 * 3;
        memcpy(out_row, src_row, row_bytes);
    }
}

static bool is_ready(ImageHandle handle) {
    return handle != IMAGE_HANDLE_INVALID && image_get_status(image_get_data(handle)) == ImageStatus_Ready;
}

static bool covers_surface(const ImageData* image, int surface_width, int surface_height) {
    return image->width >= surface_width && image->height >= surface_height;
}

// Readiness comes from the core image query, pixels from the SW pixel pool. Background images are pre-scaled to
// the surface size and the tile-copy paths index them by tile AABB, so an image smaller than the surface is
// dropped and the clear color is used instead.
static const ImageData* usable_image(ImageHandle handle, int surface_width, int surface_height) {
    if (!is_ready(handle)) {
        return nullptr;
    }
    const ImageData* image = image_cache_get_data(handle);
    if (image == nullptr || !covers_surface(image, surface_width, surface_height)) {
        return nullptr;
    }
    return image;
}

static bool is_rgb(const ImageData* image) {
    return image != nullptr && image->channels == 3;
}

SrBackground sr_background_resolve(const BackgroundState* state, int surface_width, int surface_height) {
    perf_scope_function();

    const ImageData* image = usable_image(state->image_handle, surface_width, surface_height);
    const ImageData* prev_image = nullptr;
    if (state->crossfade_active) {
        prev_image = usable_image(state->prev_image_handle, surface_width, surface_height);
    }

    const bool can_crossfade = is_rgb(image) && is_rgb(prev_image);

    return (SrBackground) {
        .image = image,
        .prev_image = prev_image,
        .alpha = state->alpha,
        .crossfade_t = state->crossfade_t,
        .clear_r = state->clear_r,
        .clear_g = state->clear_g,
        .clear_b = state->clear_b,
        .can_crossfade = can_crossfade,
        .can_direct_copy = is_rgb(image) && state->alpha == 255 && !can_crossfade,
    };
}

void sr_background_fill_tile(const SrBackground* background, i16* tile_buffer, f32x4 tile_aabb) {
    const TileRect rect = tile_rect_from_aabb(tile_aabb);

    if (background->can_crossfade) {
        crossfade_backgrounds_to_tile(tile_buffer, background, rect);
    } else if (background->image != nullptr) {
        copy_background_to_tile(tile_buffer, background, rect);
    } else {
        clear_tile_buffer(tile_buffer, background);
    }
}

bool sr_background_write_direct(const SrBackground* background, u8* output, int output_pitch, f32x4 tile_aabb) {
    const TileRect rect = tile_rect_from_aabb(tile_aabb);

    if (background->can_crossfade) {
        crossfade_direct_to_output(output, output_pitch, background, rect);
        return true;
    }
    if (background->can_direct_copy) {
        copy_background_direct_to_output(output, output_pitch, background->image, rect);
        return true;
    }
    return false;
}
