#include "software_renderer.h"
#include "sr_command.h"
#include "sr_image_backend.h"
#include "sr_ir_to_render_command.h"
#include "sr_render_image_scale.h"
#include "software_renderer_internal.h"

#include <core/arena.h>
#include <core/log.h>
#include <core/math.h>
#include <core/os/os.h>
#include <core/perf_scope.h>
#include <core/simd.h>
#include "external/clay/clay.h"
#include <ui/image_cache.h>
#include <ui/image_decode.h>
#include <ui/render_backend.h>
#include <assert.h>
#include <math.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int get_tile_size(int pos, int max_size, int tile_size) {
    if ((pos + tile_size) > max_size) {
        return max_size - pos;
    } else {
        return tile_size;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool create_tile_setup(SwSurface* surface, int width, int height) {
    FlArena* arena = surface->arena;

    size_t rgb24_size = 0;
    if (!image_checked_pixel_size(width, height, 3, &rgb24_size)) {
        log_error("Software surface %dx%d is too large", width, height);
        return false;
    }

    // Reset to the initial position; everything below is set up again
    fl_arena_rewind(arena);

    int max_count = ((width + TILE_WIDTH - 1) / TILE_WIDTH) * ((height + TILE_HEIGHT - 1) / TILE_HEIGHT);
    int tile_count = 0;
    f32x4* tiles_aabb = arena_alloc_array(arena, f32x4, max_count);

    for (int y = 0; y < height; y += TILE_HEIGHT) {
        for (int x = 0; x < width; x += TILE_WIDTH) {
            int tile_width = get_tile_size(x, width, TILE_WIDTH);
            int tile_height = get_tile_size(y, height, TILE_HEIGHT);
            tiles_aabb[tile_count++] = f32x4_new((f32)x, (f32)y, (f32)(x + tile_width), (f32)(y + tile_height));
        }
    }

    surface->tiles_aabb = tiles_aabb;
    surface->rgb24_output_buffer = arena_alloc_array(arena, u8, rgb24_size);
    // tile_buffer is resolution-independent and allocated once in create_software_renderer (tile_arena).

    surface->tiles_info = arena_alloc_array_zero(arena, Tile, tile_count);
    surface->tiles_info_count = tile_count;

    surface->screen_width = width;
    surface->screen_height = height;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Surface lifecycle: a SW surface owns its output pixel buffer + tile grid in a dedicated arena.

static FlSurface* sw_create_surface(void* user_data, int width, int height) {
    (void)user_data;
    // The SwSurface lives in its own arena together with the output buffer and tile grid.
    FlArena* arena = arena_new();
    SwSurface* surface = arena_alloc_zero(arena, SwSurface);
    surface->arena = arena;
    // create_tile_setup rewinds this arena; the pinned floor keeps the SwSurface header intact.
    arena_pin_rewind_floor(arena);
    if (!create_tile_setup(surface, width, height)) {
        fl_arena_destroy(arena);
        return nullptr;
    }
    return (FlSurface*)surface;
}

static void sw_destroy_surface(void* user_data, FlSurface* surface) {
    (void)user_data;
    SwSurface* surf = (SwSurface*)surface;
    if (surf != nullptr) {
        fl_arena_destroy(surf->arena);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void change_resolution(void* user_data, FlSurface* surface, int width, int height) {
    (void)user_data;
    (void)create_tile_setup((SwSurface*)surface, width, height); // an oversized request keeps the old setup
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// No-op: glyphs are rasterized on demand at the requested size.
static void scale_changed(void* user_data, float ui_scale) {
    (void)user_data;
    (void)ui_scale;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void clear_tile_buffer(i16* tile_buffer, u8 clear_r, u8 clear_g, u8 clear_b) {
    const i16x8 r_clear = i16x8_new_splat((i16)clear_r << 7);
    const i16x8 g_clear = i16x8_new_splat((i16)clear_g << 7);
    const i16x8 b_clear = i16x8_new_splat((i16)clear_b << 7);

    for_count(y, TILE_HEIGHT) {
        for_count(x, (TILE_WIDTH >> 3)) {
            i16x8_store_unaligned(r_clear, tile_buffer, R_PLANE_START_OFFSET);
            i16x8_store_unaligned(g_clear, tile_buffer, G_PLANE_START_OFFSET);
            i16x8_store_unaligned(b_clear, tile_buffer, B_PLANE_START_OFFSET);
            tile_buffer += 8;
        }

        tile_buffer += (TILE_STRIDE - TILE_WIDTH);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Copy background image to tile buffer. The image is pre-scaled to screen size, so no bounds checking is
// needed. Reads interleaved RGB or RGBA, converts to planar Q7 format.

static ALWAYS_INLINE void copy_background_to_tile_impl(i16* tile_buffer, const ImageData* image, u8 alpha,
                                                       f32x4 tile_aabb, u8 clear_r, u8 clear_g, u8 clear_b,
                                                       const bool needs_blend, const bool is_rgba) {
    const u8* src = image->data;
    const int img_stride = image->stride;
    const int bytes_per_pixel = is_rgba ? 4 : 3;

    const i32x4 int_tile_aabb = f32x4_as_i32x4(tile_aabb);
    const int x0 = i32x4_extract(int_tile_aabb, 0);
    const int y0 = i32x4_extract(int_tile_aabb, 1);
    const int tile_height = i32x4_extract(int_tile_aabb, 3) - y0;

    const i16 alpha_q15 = (i16)(((i32)alpha << 7) + (alpha >> 1));
    const i16x8 alpha_vec = i16x8_new_splat(alpha_q15);
    const i16x8 clear_r_vec = i16x8_new_splat((i16)clear_r << 7);
    const i16x8 clear_g_vec = i16x8_new_splat((i16)clear_g << 7);
    const i16x8 clear_b_vec = i16x8_new_splat((i16)clear_b << 7);

    for (int j = 0; j < tile_height; j++) {
        const u8* row = src + (y0 + j) * img_stride + x0 * bytes_per_pixel;
        i16* tile_row = tile_buffer + j * TILE_STRIDE;

        for (int k = 0; k < (TILE_WIDTH >> 4); k++) {
            i16x8x2 r, g, b;

            if (is_rgba) {
                u8x16_rgba_result px = u8x16_load_deinterleaved_4(row, k * 64);
                r = u8x16_unpack_to_i16x8_wide_shl(px.r, 7);
                g = u8x16_unpack_to_i16x8_wide_shl(px.g, 7);
                b = u8x16_unpack_to_i16x8_wide_shl(px.b, 7);
            } else {
                u8x16_rgb_result px = u8x16_load_deinterleaved_3(row, k * 48);
                r = u8x16_unpack_to_i16x8_wide_shl(px.r, 7);
                g = u8x16_unpack_to_i16x8_wide_shl(px.g, 7);
                b = u8x16_unpack_to_i16x8_wide_shl(px.b, 7);
            }

            if (needs_blend) {
                r.v0 = i16x8_lerp(clear_r_vec, r.v0, alpha_vec);
                g.v0 = i16x8_lerp(clear_g_vec, g.v0, alpha_vec);
                b.v0 = i16x8_lerp(clear_b_vec, b.v0, alpha_vec);
                r.v1 = i16x8_lerp(clear_r_vec, r.v1, alpha_vec);
                g.v1 = i16x8_lerp(clear_g_vec, g.v1, alpha_vec);
                b.v1 = i16x8_lerp(clear_b_vec, b.v1, alpha_vec);
            }

            i16x8_store_unaligned(r.v0, tile_row + k * 16, R_PLANE_START_OFFSET);
            i16x8_store_unaligned(g.v0, tile_row + k * 16, G_PLANE_START_OFFSET);
            i16x8_store_unaligned(b.v0, tile_row + k * 16, B_PLANE_START_OFFSET);
            i16x8_store_unaligned(r.v1, tile_row + k * 16 + 8, R_PLANE_START_OFFSET);
            i16x8_store_unaligned(g.v1, tile_row + k * 16 + 8, G_PLANE_START_OFFSET);
            i16x8_store_unaligned(b.v1, tile_row + k * 16 + 8, B_PLANE_START_OFFSET);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void copy_background_to_tile_rgba_blend(i16* tile_buffer, const ImageData* image, u8 alpha, f32x4 tile_aabb,
                                               u8 clear_r, u8 clear_g, u8 clear_b) {
    copy_background_to_tile_impl(tile_buffer, image, alpha, tile_aabb, clear_r, clear_g, clear_b, true, true);
}

static void copy_background_to_tile_rgba_direct(i16* tile_buffer, const ImageData* image, f32x4 tile_aabb) {
    copy_background_to_tile_impl(tile_buffer, image, 255, tile_aabb, 0, 0, 0, false, true);
}

static void copy_background_to_tile_rgb_blend(i16* tile_buffer, const ImageData* image, u8 alpha, f32x4 tile_aabb,
                                              u8 clear_r, u8 clear_g, u8 clear_b) {
    copy_background_to_tile_impl(tile_buffer, image, alpha, tile_aabb, clear_r, clear_g, clear_b, true, false);
}

static void copy_background_to_tile_rgb_direct(i16* tile_buffer, const ImageData* image, f32x4 tile_aabb) {
    copy_background_to_tile_impl(tile_buffer, image, 255, tile_aabb, 0, 0, 0, false, false);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void copy_background_to_tile(i16* tile_buffer, const ImageData* image, u8 alpha, f32x4 tile_aabb, u8 clear_r,
                                    u8 clear_g, u8 clear_b) {
    const bool is_rgba = image->channels == 4;
    const bool needs_blend = alpha < 255;

    if (is_rgba) {
        if (needs_blend) {
            copy_background_to_tile_rgba_blend(tile_buffer, image, alpha, tile_aabb, clear_r, clear_g, clear_b);
        } else {
            copy_background_to_tile_rgba_direct(tile_buffer, image, tile_aabb);
        }
    } else {
        if (needs_blend) {
            copy_background_to_tile_rgb_blend(tile_buffer, image, alpha, tile_aabb, clear_r, clear_g, clear_b);
        } else {
            copy_background_to_tile_rgb_direct(tile_buffer, image, tile_aabb);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Crossfade between two full-screen background images, both assumed screen-sized RGB (3 channels).
// Blends with result = prev * (1-t) + curr * t.

static void crossfade_backgrounds_to_tile(i16* tile_buffer, const ImageData* prev_image, const ImageData* curr_image,
                                          u8 crossfade_t, f32x4 tile_aabb) {
    const u8* prev_src = prev_image->data;
    const u8* curr_src = curr_image->data;
    const int prev_stride = prev_image->stride;
    const int curr_stride = curr_image->stride;

    const i32x4 int_tile_aabb = f32x4_as_i32x4(tile_aabb);
    const int x0 = i32x4_extract(int_tile_aabb, 0);
    const int y0 = i32x4_extract(int_tile_aabb, 1);
    const int tile_height = i32x4_extract(int_tile_aabb, 3) - y0;

    // Convert crossfade_t (0-255) to Q15 format for SIMD lerp
    // t=0 means full prev, t=255 means full curr
    const i16 t_q15 = (i16)(((i32)crossfade_t << 7) + (crossfade_t >> 1));
    const i16x8 t_vec = i16x8_new_splat(t_q15);

    for (int j = 0; j < tile_height; j++) {
        const u8* prev_row = prev_src + (y0 + j) * prev_stride + x0 * 3;
        const u8* curr_row = curr_src + (y0 + j) * curr_stride + x0 * 3;
        i16* tile_row = tile_buffer + j * TILE_STRIDE;

        // Process 16 pixels at a time (48 bytes RGB per iteration)
        for (int k = 0; k < (TILE_WIDTH >> 4); k++) {
            u8x16_rgb_result prev_px = u8x16_load_deinterleaved_3(prev_row, k * 48);
            i16x8x2 prev_r = u8x16_unpack_to_i16x8_wide_shl(prev_px.r, 7);
            i16x8x2 prev_g = u8x16_unpack_to_i16x8_wide_shl(prev_px.g, 7);
            i16x8x2 prev_b = u8x16_unpack_to_i16x8_wide_shl(prev_px.b, 7);

            u8x16_rgb_result curr_px = u8x16_load_deinterleaved_3(curr_row, k * 48);
            i16x8x2 curr_r = u8x16_unpack_to_i16x8_wide_shl(curr_px.r, 7);
            i16x8x2 curr_g = u8x16_unpack_to_i16x8_wide_shl(curr_px.g, 7);
            i16x8x2 curr_b = u8x16_unpack_to_i16x8_wide_shl(curr_px.b, 7);

            i16x8 r0 = i16x8_lerp(prev_r.v0, curr_r.v0, t_vec);
            i16x8 g0 = i16x8_lerp(prev_g.v0, curr_g.v0, t_vec);
            i16x8 b0 = i16x8_lerp(prev_b.v0, curr_b.v0, t_vec);
            i16x8 r1 = i16x8_lerp(prev_r.v1, curr_r.v1, t_vec);
            i16x8 g1 = i16x8_lerp(prev_g.v1, curr_g.v1, t_vec);
            i16x8 b1 = i16x8_lerp(prev_b.v1, curr_b.v1, t_vec);

            i16x8_store_unaligned(r0, tile_row + k * 16, R_PLANE_START_OFFSET);
            i16x8_store_unaligned(g0, tile_row + k * 16, G_PLANE_START_OFFSET);
            i16x8_store_unaligned(b0, tile_row + k * 16, B_PLANE_START_OFFSET);
            i16x8_store_unaligned(r1, tile_row + k * 16 + 8, R_PLANE_START_OFFSET);
            i16x8_store_unaligned(g1, tile_row + k * 16 + 8, G_PLANE_START_OFFSET);
            i16x8_store_unaligned(b1, tile_row + k * 16 + 8, B_PLANE_START_OFFSET);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Scalar equivalent of i16x8x2_pack_to_u8x16_narrow_shr(v, 7): reinterpret the i16 as u16, logical-shift right
// by 7, then saturate to u8 (out-of-range values count as large positives).
static inline u8 narrow_shr7_u8(i16 v) {
    u16 s = (u16)v >> 7;
    return s > 255 ? (u8)255 : (u8)s;
}

// Scalar tail covering the rightmost tile_width & 15 columns the 16-px SIMD loop skips, one pixel at a time.
static NO_INLINE void copy_tile_tail_to_output(u8* output, const i16* tile_data, int x0, int y0, int tile_width,
                                               int tile_height, int output_pitch) {
    const int first_tail = tile_width & ~15; // == (tile_width >> 4) * 16

    for (int j = 0; j < tile_height; j++) {
        const int r_plane_row_start = R_PLANE_START_OFFSET + j * TILE_STRIDE;
        const int g_plane_row_start = G_PLANE_START_OFFSET + j * TILE_STRIDE;
        const int b_plane_row_start = B_PLANE_START_OFFSET + j * TILE_STRIDE;
        const int output_row_start = (y0 + j) * output_pitch + x0 * 3;
        for (int k = first_tail; k < tile_width; k++) {
            u8* out_px = output + output_row_start + k * 3;
            out_px[0] = narrow_shr7_u8(tile_data[r_plane_row_start + k]);
            out_px[1] = narrow_shr7_u8(tile_data[g_plane_row_start + k]);
            out_px[2] = narrow_shr7_u8(tile_data[b_plane_row_start + k]);
        }
    }
}

// Copy tile data from planar RGB format to interleaved RGB output buffer
static void copy_tile_to_output(u8* output, const i16* tile_data, const f32x4 tile_aabb, int output_pitch) {
    const i32x4 int_tile_aabb = f32x4_as_i32x4(tile_aabb);

    const int x0 = i32x4_extract(int_tile_aabb, 0);
    const int y0 = i32x4_extract(int_tile_aabb, 1);
    const int x1 = i32x4_extract(int_tile_aabb, 2);
    const int y1 = i32x4_extract(int_tile_aabb, 3);

    const int tile_width = x1 - x0;
    const int tile_height = y1 - y0;
    const int output_row_stride = output_pitch;

    for (int j = 0; j < tile_height; j++) {
        const int r_plane_row_start = R_PLANE_START_OFFSET + j * TILE_STRIDE;
        const int g_plane_row_start = G_PLANE_START_OFFSET + j * TILE_STRIDE;
        const int b_plane_row_start = B_PLANE_START_OFFSET + j * TILE_STRIDE;

        const int output_row_start = (y0 + j) * output_row_stride + x0 * 3;

        const int vector_chunks = tile_width >> 4;
        for (int k = 0; k < vector_chunks; k++) {
            const int chunk_offset = k * 16;

            i16x8x2 r_chunk_16 = i16x8x2_load(tile_data, r_plane_row_start + chunk_offset);
            i16x8x2 g_chunk_16 = i16x8x2_load(tile_data, g_plane_row_start + chunk_offset);
            i16x8x2 b_chunk_16 = i16x8x2_load(tile_data, b_plane_row_start + chunk_offset);

            u8x16 r_chunk = i16x8x2_pack_to_u8x16_narrow_shr(r_chunk_16, 7);
            u8x16 g_chunk = i16x8x2_pack_to_u8x16_narrow_shr(g_chunk_16, 7);
            u8x16 b_chunk = i16x8x2_pack_to_u8x16_narrow_shr(b_chunk_16, 7);

            const int output_chunk_offset = output_row_start + chunk_offset * 3;
            u8x16_store_interleaved_3(output + output_chunk_offset, r_chunk, g_chunk, b_chunk, 0);
        }
    }

    // Non-16-aligned tile widths leave a rightmost tail the SIMD loop skips.
    if ((tile_width & 15) != 0) {
        copy_tile_tail_to_output(output, tile_data, x0, y0, tile_width, tile_height, output_pitch);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void dispatch_command(RenderData* self, const RenderCommand* render_command, const TileInfo* tile_info,
                             f32x4 scissor_rect) {
    u32 render_flags = render_command->mask;

    if (render_flags & RenderMask_Text) {
        sr_render_text(self->tile_buffer, render_command, scissor_rect, tile_info);
    } else if (render_flags & RenderMask_TextureSameSize) {
        sr_render_image_nearest(self->tile_buffer, render_command, scissor_rect, tile_info);
    } else if (render_flags & RenderMask_TextureScaled) {
        sr_render_image_scale(self->tile_buffer, render_command, scissor_rect, tile_info);
    } else if (render_flags & RenderMask_Border) {
        sr_render_border(self->tile_buffer, render_command, scissor_rect, tile_info);
    } else if (render_flags & RenderMask_DrawQuad) {
        sr_render_fill(self->tile_buffer, render_command, scissor_rect, tile_info);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Tiles with only clip-rect commands can use the direct-to-output fast path

static bool tile_has_visual_commands(const Tile* tile, const RenderCommand* commands) {
    for (int t = 0, count = tile->render_ids_count; t < count; t++) {
        u32 flags = commands[tile->render_ids[t]].mask;
        if (!(flags & (RenderMask_ClipRectBegin | RenderMask_ClipRectEnd))) {
            return true;
        }
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Scalar tail for the rightmost tile_width & 15 columns crossfade_direct_to_output skips; same widen<<7 / Q15
// lerp / narrow>>7 math.
static NO_INLINE void crossfade_tail_to_output(u8* output, int output_pitch, const ImageData* prev_image,
                                               const ImageData* curr_image, u8 crossfade_t, int x0, int y0,
                                               int tile_width, int tile_height) {
    const u8* prev_src = prev_image->data;
    const u8* curr_src = curr_image->data;
    const int prev_stride = prev_image->stride;
    const int curr_stride = curr_image->stride;

    const int t_q15 = ((int)crossfade_t << 7) + (crossfade_t >> 1);
    const int first_tail = tile_width & ~15; // == (tile_width >> 4) * 16

    for (int j = 0; j < tile_height; j++) {
        const u8* prev_row = prev_src + (y0 + j) * prev_stride + x0 * 3;
        const u8* curr_row = curr_src + (y0 + j) * curr_stride + x0 * 3;
        u8* out_row = output + (y0 + j) * output_pitch + x0 * 3;
        for (int k = first_tail; k < tile_width; k++) {
            const u8* pp = prev_row + k * 3;
            const u8* cp = curr_row + k * 3;
            u8* op = out_row + k * 3;
            for (int c = 0; c < 3; c++) {
                const int start16 = (int)pp[c] << 7;
                const int delta = ((int)cp[c] << 7) - start16;
                const int lerp = start16 + ((delta * t_q15 + 0x4000) >> 15);
                op[c] = narrow_shr7_u8((i16)lerp);
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Crossfade two RGB backgrounds directly into the interleaved output buffer, skipping the planar tile buffer
// so all widening/lerping/narrowing stays in registers.

static void crossfade_direct_to_output(u8* output, int output_pitch, const ImageData* prev_image,
                                       const ImageData* curr_image, u8 crossfade_t, f32x4 tile_aabb) {
    const u8* prev_src = prev_image->data;
    const u8* curr_src = curr_image->data;
    const int prev_stride = prev_image->stride;
    const int curr_stride = curr_image->stride;

    const i32x4 int_tile_aabb = f32x4_as_i32x4(tile_aabb);
    const int x0 = i32x4_extract(int_tile_aabb, 0);
    const int y0 = i32x4_extract(int_tile_aabb, 1);
    const int tile_width = i32x4_extract(int_tile_aabb, 2) - x0;
    const int tile_height = i32x4_extract(int_tile_aabb, 3) - y0;

    const i16 t_q15 = (i16)(((i32)crossfade_t << 7) + (crossfade_t >> 1));
    const i16x8 t_vec = i16x8_new_splat(t_q15);

    for (int j = 0; j < tile_height; j++) {
        const u8* prev_row = prev_src + (y0 + j) * prev_stride + x0 * 3;
        const u8* curr_row = curr_src + (y0 + j) * curr_stride + x0 * 3;
        u8* out_row = output + (y0 + j) * output_pitch + x0 * 3;

        for (int k = 0; k < (tile_width >> 4); k++) {
            u8x16_rgb_result prev_px = u8x16_load_deinterleaved_3(prev_row, k * 48);
            u8x16_rgb_result curr_px = u8x16_load_deinterleaved_3(curr_row, k * 48);

            i16x8x2 prev_r = u8x16_unpack_to_i16x8_wide_shl(prev_px.r, 7);
            i16x8x2 curr_r = u8x16_unpack_to_i16x8_wide_shl(curr_px.r, 7);
            i16x8x2 blended_r = { i16x8_lerp(prev_r.v0, curr_r.v0, t_vec), i16x8_lerp(prev_r.v1, curr_r.v1, t_vec) };
            u8x16 r = i16x8x2_pack_to_u8x16_narrow_shr(blended_r, 7);

            i16x8x2 prev_g = u8x16_unpack_to_i16x8_wide_shl(prev_px.g, 7);
            i16x8x2 curr_g = u8x16_unpack_to_i16x8_wide_shl(curr_px.g, 7);
            i16x8x2 blended_g = { i16x8_lerp(prev_g.v0, curr_g.v0, t_vec), i16x8_lerp(prev_g.v1, curr_g.v1, t_vec) };
            u8x16 g = i16x8x2_pack_to_u8x16_narrow_shr(blended_g, 7);

            i16x8x2 prev_b = u8x16_unpack_to_i16x8_wide_shl(prev_px.b, 7);
            i16x8x2 curr_b = u8x16_unpack_to_i16x8_wide_shl(curr_px.b, 7);
            i16x8x2 blended_b = { i16x8_lerp(prev_b.v0, curr_b.v0, t_vec), i16x8_lerp(prev_b.v1, curr_b.v1, t_vec) };
            u8x16 b = i16x8x2_pack_to_u8x16_narrow_shr(blended_b, 7);

            u8x16_store_interleaved_3(out_row, r, g, b, k * 48);
        }
    }

    // Non-16-aligned tile widths leave a rightmost tail the SIMD loop skips.
    if ((tile_width & 15) != 0) {
        crossfade_tail_to_output(output, output_pitch, prev_image, curr_image, crossfade_t, x0, y0, tile_width,
                                 tile_height);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Direct copy of RGB background to output buffer, bypassing planar tile buffer

static void copy_background_direct_to_output(u8* output, int output_pitch, const ImageData* image, f32x4 tile_aabb) {
    const u8* src = image->data;
    const int img_stride = image->stride;

    const i32x4 int_tile_aabb = f32x4_as_i32x4(tile_aabb);
    const int x0 = i32x4_extract(int_tile_aabb, 0);
    const int y0 = i32x4_extract(int_tile_aabb, 1);
    const int tile_width = i32x4_extract(int_tile_aabb, 2) - x0;
    const int tile_height = i32x4_extract(int_tile_aabb, 3) - y0;

    const int row_bytes = tile_width * 3;

    for (int j = 0; j < tile_height; j++) {
        const u8* src_row = src + (y0 + j) * img_stride + x0 * 3;
        u8* out_row = output + (y0 + j) * output_pitch + x0 * 3;
        memcpy(out_row, src_row, row_bytes);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void render_tiles(RenderData* self, SwSurface* surface) {
    perf_scope_function();

// Scissor rect stack for nested clipping regions
#define MAX_SCISSOR_STACK_DEPTH 16
    f32x4 scissor_stack[MAX_SCISSOR_STACK_DEPTH];
    int scissor_stack_depth = 0;
    // Pushes dropped at capacity; each is matched against a later pop before the real stack unwinds.
    int scissor_ignored_pushes = 0;

    const ImageData* bg_image = nullptr;
    {
        perf_scope("Check background image");

        if (self->background.image_handle != IMAGE_HANDLE_INVALID) {
            // Readiness comes from the core image query, pixels from the SW pixel pool.
            if (image_get_status(image_get_data(self->background.image_handle)) == ImageStatus_Ready) {
                bg_image = image_cache_get_data(self->background.image_handle);
            }
        }
    }

    const ImageData* prev_bg_image = nullptr;
    {
        perf_scope("Check previous background image");

        if (self->background.crossfade_active && self->background.prev_image_handle != IMAGE_HANDLE_INVALID) {
            // Readiness comes from the core image query, pixels from the SW pixel pool.
            if (image_get_status(image_get_data(self->background.prev_image_handle)) == ImageStatus_Ready) {
                prev_bg_image = image_cache_get_data(self->background.prev_image_handle);
            }
        }
    }

    // Background images are pre-scaled to the surface size and the tile-copy paths index them by tile AABB, so
    // an image smaller than the surface is dropped and the clear color is used instead.
    if (bg_image != nullptr && (bg_image->width < surface->screen_width || bg_image->height < surface->screen_height)) {
        bg_image = nullptr;
    }
    if (prev_bg_image != nullptr
        && (prev_bg_image->width < surface->screen_width || prev_bg_image->height < surface->screen_height)) {
        prev_bg_image = nullptr;
    }

    const bool can_crossfade = self->background.crossfade_active && prev_bg_image != nullptr && bg_image != nullptr
                               && prev_bg_image->channels == 3 && bg_image->channels == 3;

    // The SW backend renders into its own offscreen RGB24 buffer; the host presents it.
    u8* output = surface->rgb24_output_buffer;
    int pitch = surface->screen_width * 3;

    // Direct copy fast path: fully opaque RGB background with no crossfade
    const bool can_direct_copy
        = bg_image != nullptr && bg_image->channels == 3 && self->background.alpha == 255 && !can_crossfade;

    for (int i = 0, count = surface->tiles_info_count; i < count; ++i) {
        Tile* tile = &surface->tiles_info[i];

        // Fast path: tiles with no visual render commands bypass the planar tile buffer
        // and write directly to output, saving ~60KB memory traffic per tile
        if (!tile_has_visual_commands(tile, self->render_commands)) {
            if (can_crossfade) {
                crossfade_direct_to_output(output, pitch, prev_bg_image, bg_image, self->background.crossfade_t,
                                           surface->tiles_aabb[i]);
                continue;
            } else if (can_direct_copy) {
                copy_background_direct_to_output(output, pitch, bg_image, surface->tiles_aabb[i]);
                continue;
            }
        }

        const u8 clear_r = self->background.clear_r;
        const u8 clear_g = self->background.clear_g;
        const u8 clear_b = self->background.clear_b;

        if (can_crossfade) {
            crossfade_backgrounds_to_tile(self->tile_buffer, prev_bg_image, bg_image, self->background.crossfade_t,
                                          surface->tiles_aabb[i]);
        } else if (bg_image != nullptr) {
            copy_background_to_tile(self->tile_buffer, bg_image, self->background.alpha, surface->tiles_aabb[i],
                                    clear_r, clear_g, clear_b);
        } else {
            clear_tile_buffer(self->tile_buffer, clear_r, clear_g, clear_b);
        }

        const f32x4 tile_aabb = surface->tiles_aabb[i];
        const TileInfo tile_info = {
            .offsets = f32x4_shuffle(tile_aabb, tile_aabb, 0, 1, 0, 1),
        };

        const f32x4 tile_scissor = f32x4_new(0.0f, 0.0f, (f32)TILE_WIDTH, (f32)TILE_HEIGHT);
        scissor_stack[0] = tile_scissor;
        scissor_stack_depth = 1;
        scissor_ignored_pushes = 0;

        for (int t = 0, id_count = tile->render_ids_count; t < id_count; t++) {
            u32 index = tile->render_ids[t];
            const RenderCommand* render_command = &self->render_commands[index];
            u32 render_flags = render_command->mask;

            if (render_flags & RenderMask_ClipRectBegin) {
                if (scissor_stack_depth < MAX_SCISSOR_STACK_DEPTH) {
                    const f32x4 clip_rect = render_command->aabb;
                    f32x4 tile_relative_clip = f32x4_sub(clip_rect, tile_info.offsets);
                    f32x4 current_scissor = scissor_stack[scissor_stack_depth - 1];
                    f32x4 new_scissor
                        = f32x4_new(fmaxf(f32x4_extract(tile_relative_clip, 0), f32x4_extract(current_scissor, 0)),
                                    fmaxf(f32x4_extract(tile_relative_clip, 1), f32x4_extract(current_scissor, 1)),
                                    fminf(f32x4_extract(tile_relative_clip, 2), f32x4_extract(current_scissor, 2)),
                                    fminf(f32x4_extract(tile_relative_clip, 3), f32x4_extract(current_scissor, 3)));
                    scissor_stack[scissor_stack_depth++] = new_scissor;
                } else {
                    // At capacity: count the dropped push so its matching pop skips the real stack.
                    scissor_ignored_pushes++;
                }
            } else if (render_flags & RenderMask_ClipRectEnd) {
                if (scissor_ignored_pushes > 0) {
                    scissor_ignored_pushes--;
                } else if (scissor_stack_depth > 1) {
                    scissor_stack_depth--;
                }
            } else {
                f32x4 current_scissor = scissor_stack[scissor_stack_depth - 1];
                dispatch_command(self, render_command, &tile_info, current_scissor);
            }
        }

        copy_tile_to_output(output, self->tile_buffer, surface->tiles_aabb[i], pitch);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void bin_tiles(RenderData* self, SwSurface* surface, FlArena* frame_arena,
                      UiRenderCommandsArray render_commands) {
    perf_scope_function();
    (void)self;

    int cmd_count = render_commands.count;
    int tile_count = surface->tiles_info_count;

#ifdef FLOWI_UI_INSPECTOR
    self->stat_bin_alloc_bytes = 0;
    self->stat_clip_broadcasts = 0;
#endif

    if (tile_count == 0) {
        return;
    }

    // Reset tile render_ids; the previous frame's pointers are invalid after fl_arena_rewind.
    if (cmd_count == 0) {
        for (int t = 0; t < tile_count; t++) {
            surface->tiles_info[t].render_ids = nullptr;
            surface->tiles_info[t].render_ids_count = 0;
        }
        return;
    }

    // Tile grid dimensions (tiles are created in row-major order)
    int cols = (surface->screen_width + TILE_WIDTH - 1) / TILE_WIDTH;
    int rows = (surface->screen_height + TILE_HEIGHT - 1) / TILE_HEIGHT;

    i32x4 max_tiles = i32x4_new(cols - 1, rows - 1, cols - 1, rows - 1);

#ifdef FLOWI_UI_INSPECTOR
    void* bin_alloc_before = fl_arena_current_pos(frame_arena);
#endif

    // Single contiguous allocation for all tiles' render_ids (rewound each frame)
    u32* all_render_ids = arena_alloc_array(frame_arena, u32, (u64)tile_count * cmd_count);

#ifdef FLOWI_UI_INSPECTOR
    // Bytes the arena actually consumed, alignment padding included.
    self->stat_bin_alloc_bytes = (u32)((u8*)fl_arena_current_pos(frame_arena) - (u8*)bin_alloc_before);
#endif

    for (int t = 0; t < tile_count; t++) {
        surface->tiles_info[t].render_ids = all_render_ids + (u64)t * cmd_count;
        surface->tiles_info[t].render_ids_count = 0;
    }

    for (int i = 0; i < cmd_count; i++) {
        RenderCommand* cmd = &render_commands.commands[i];

        if ((cmd->mask & RenderMask_ClipRectBegin) || (cmd->mask & RenderMask_ClipRectEnd)) {
            // Clip commands affect the entire screen - add to all tiles
            for (int t = 0; t < tile_count; t++) {
                surface->tiles_info[t].render_ids[surface->tiles_info[t].render_ids_count++] = (u32)i;
            }
#ifdef FLOWI_UI_INSPECTOR
            self->stat_clip_broadcasts += (u32)tile_count;
#endif
        } else {
            TileRange range = tile_range_from_aabb(cmd->aabb, max_tiles);

            for (int ty = range.ty0; ty <= range.ty1; ty++) {
                int row_base = ty * cols;
                for (int tx = range.tx0; tx <= range.tx1; tx++) {
                    int t = row_base + tx;
                    surface->tiles_info[t].render_ids[surface->tiles_info[t].render_ids_count++] = (u32)i;
                }
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The previous handle crosses only while a crossfade is active, so a valid prev is the sole crossfade signal.
// crossfade_t round-trips through 0..1 losslessly for every 0..255 progress value.
static BackgroundState background_from_frame(const FlBackground* bg) {
    return (BackgroundState) {
        .image_handle = (ImageHandle)bg->current,
        .prev_image_handle = (ImageHandle)bg->prev,
        .alpha = bg->alpha,
        .crossfade_t = (u8)(bg->crossfade_t * 255.0f + 0.5f),
        .crossfade_active = bg->prev != FL_IMAGE_HANDLE_INVALID,
        .clear_r = bg->clear.r,
        .clear_g = bg->clear.g,
        .clear_b = bg->clear.b,
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Build the SW glyph cache on first use, once the font cache has been set.
static SwGlyphCache* ensure_glyph_cache(RenderData* self) {
    if (self->glyph_cache == nullptr && self->font_cache != nullptr) {
        self->glyph_cache = sw_glyph_cache_new(self->perm_arena, self->font_cache, self->render_mode);
    }
    return self->glyph_cache;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void render(void* user_data, FlSurface* surface_handle, const FlRenderFrame* frame) {
    perf_scope("sr_render");
    RenderData* self = user_data;
    SwSurface* surface = (SwSurface*)surface_handle;

    // Advance the SW pixel pool's frame clock: drives eviction LRU timestamps and the periodic
    // evicted-metadata cleanup.
    image_cache_update(self->image_cache, self->image_cache_frame++);

    self->background = background_from_frame(&frame->background);

    UiRenderCommandsArray render_commands
        = sr_ir_to_render_commands(self->frame_arena, self->frame_arena_2, ensure_glyph_cache(self), frame->commands,
                                   frame->bytes, frame->global_alpha);

    self->render_commands = render_commands.commands;

#ifdef FLOWI_UI_INSPECTOR
    // Each scaled-texture command this frame drove one calculate_shuffle_tables rebuild in sr_command_setup.
    self->stat_scaled_image_setups = 0;
    for (int i = 0; i < render_commands.count; i++) {
        if (render_commands.commands[i].mask & RenderMask_TextureScaled) {
            self->stat_scaled_image_setups++;
        }
    }
#endif

    bin_tiles(self, surface, self->frame_arena, render_commands);
    render_tiles(self, surface);

    fl_arena_rewind(self->frame_arena);
    fl_arena_rewind(self->frame_arena_2);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Optional pixel-readback capability: hand back the offscreen RGB24 buffer the last frame rendered into. The SW
// buffer is unpadded, so stride is exactly screen_width * 3.
static bool read_pixels(void* user_data, FlSurface* surface_handle, FlPixelReadback* out) {
    (void)user_data;
    SwSurface* surface = (SwSurface*)surface_handle;
    *out = (FlPixelReadback) {
        .pixels = surface->rgb24_output_buffer,
        .width = (u32)surface->screen_width,
        .height = (u32)surface->screen_height,
        .stride = (u32)(surface->screen_width * 3),
    };
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef FLOWI_UI_INSPECTOR
// Hand back the SW work counters the last frame accumulated in bin_tiles. render_commands is left untouched.
void sw_renderer_read_frame_stats(void* user_data, FlFrameStats* out) {
    RenderData* self = user_data;
    out->bin_alloc_bytes = self->stat_bin_alloc_bytes;
    out->clip_broadcasts = self->stat_clip_broadcasts;
    out->scaled_image_setups = self->stat_scaled_image_setups;
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Tear down every arena the backend allocated plus any lazily-built glyph cache. RenderData itself lives in
// the host arena passed to create, so it is not freed here.
static void destroy(void* user_data) {
    RenderData* self = user_data;
    // The SW pixel pool is a process singleton shared across backends; this drops the backend's reference.
    if (self->image_cache != nullptr) {
        image_cache_destroy(self->image_cache);
        self->image_cache = nullptr;
    }
    if (self->glyph_cache != nullptr) {
        sw_glyph_cache_destroy(self->glyph_cache);
        self->glyph_cache = nullptr;
    }
    fl_arena_destroy(self->tile_arena);
    fl_arena_destroy(self->frame_arena_2);
    fl_arena_destroy(self->frame_arena);
    // Per-surface arenas belong to their SwSurface; perm_arena belongs to the host.
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Rebind the shared font layer and drop any glyph cache bound to the previous one; the next render rebuilds it.
static void set_font_cache(void* user_data, struct FlFontCache* font_cache) {
    RenderData* self = user_data;
    self->font_cache = font_cache;
    if (self->glyph_cache != nullptr) {
        sw_glyph_cache_destroy(self->glyph_cache);
        self->glyph_cache = nullptr;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlRenderBackend create_software_renderer(struct FlArena* arena, struct FlFontCache* font_cache,
                                         FontRenderMode render_mode) {
    RenderData* render_data = arena_alloc_zero(arena, RenderData);
    render_data->perm_arena = arena;
    render_data->frame_arena = arena_new();
    render_data->frame_arena_2 = arena_new();
    render_data->tile_arena = arena_new();
    render_data->font_cache = font_cache;
    render_data->render_mode = render_mode;

    // The SW realized-pixel pool is a process singleton created on first use.
    render_data->image_cache = image_cache_new(arena, nullptr, (ImageCacheConfig) { 0 });

    // Per-tile scratch is fixed-size and resolution-independent.
    render_data->tile_buffer = arena_alloc_array(render_data->tile_arena, i16, TILE_STRIDE * TILE_WIDTH * 6);

    // No surface is created here - the host builds per-window output buffers via the surface seam.

    return (FlRenderBackend) {
        .user_data = render_data,
        .create_surface = sw_create_surface,
        .destroy_surface = sw_destroy_surface,
        .change_resolution = change_resolution,
        .render = render,
        .destroy = destroy,
        .scale_changed = scale_changed,
        .set_font_cache = set_font_cache,
        .prepare_image = sw_prepare_image,
        .read_pixels = read_pixels,
        // Runs prepare_image in job context; thread-confined backends (GL, Metal) leave this unset.
        .prepare_image_on_job = true,
    };
}
