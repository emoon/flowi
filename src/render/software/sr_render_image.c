// GCC -fanalyzer false positives on SIMD intrinsic wrappers
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 12
#pragma GCC diagnostic ignored "-Wanalyzer-use-of-uninitialized-value"
#endif

#include <core/log.h>
#include <core/simd.h>
#include "software_renderer_internal.h"
#include "sr_image_blend.h"
#include "sr_tile_row.h"
#include <ui/image/image.h>
#include <math.h>

// Nearest-neighbor (point-sampled) image rendering.

typedef struct NearestMode {
    bool blend_with_background;
    bool rounding;
    int channel_count;
} NearestMode;

typedef struct NearestState {
    CornerRoundingState rounding_state;
    SoaRgba tint;
    i16x8 global_alpha;
} NearestState;

// Where the 8 source texels of each plane start for the current output pixel group.
typedef struct NearestSource {
    const u8* line;
    int r_plane;
    int g_plane;
    int b_plane;
    int a_plane;
} NearestSource;

static ALWAYS_INLINE i16x8 load_plane_8(const NearestSource* src, int plane) {
    return u8x8_load_to_i16x8_wide_shl(src->line, plane, 7);
}

// Single alpha channel: coverage for the command tint, no vertical neighbor.
static ALWAYS_INLINE SoaRgb nearest_1ch(const NearestState* state, const NearestSource* src, const PlaneRow* row) {
    i16x8 coverage = i16x8_lerp(i16x8_new_splat(0), load_plane_8(src, src->r_plane), state->global_alpha);
    SoaRgb tint = { .r = state->tint.r, .g = state->tint.g, .b = state->tint.b };
    return lerp_over_background(tint, row, coverage);
}

// Premultiplied RGBA, always composited over the background.
static ALWAYS_INLINE SoaRgb nearest_4ch(const NearestMode mode, NearestState* state, const NearestSource* src,
                                        const PlaneRow* row) {
    SoaRgba texel = {
        .r = load_plane_8(src, src->r_plane),
        .g = load_plane_8(src, src->g_plane),
        .b = load_plane_8(src, src->b_plane),
        .a = i16x8_lerp(i16x8_new_splat(0), load_plane_8(src, src->a_plane), state->global_alpha),
    };
    const SoaRgb bg = plane_load_rgb(row);

    if (mode.rounding) {
        return premul_over_8_rounded(texel, state->global_alpha, bg, corner_rounding_step_8(&state->rounding_state));
    }
    return premul_over_8(texel, state->global_alpha, bg);
}

// RGB: one texel load per pixel (no interpolation), opaque unless faded by the global alpha.
static ALWAYS_INLINE SoaRgb nearest_3ch(const NearestMode mode, NearestState* state, const NearestSource* src,
                                        const PlaneRow* row) {
    SoaRgb color = {
        .r = load_plane_8(src, src->r_plane),
        .g = load_plane_8(src, src->g_plane),
        .b = load_plane_8(src, src->b_plane),
    };

    if (mode.rounding) {
        color = lerp_over_background(color, row, corner_rounding_step_8(&state->rounding_state));
    }

    if (mode.blend_with_background) {
        color = lerp_over_background(color, row, state->global_alpha);
    }

    return color;
}

static ALWAYS_INLINE SoaRgb process_8_pixels_nearest(const NearestMode mode, NearestState* state,
                                                     const NearestSource* src, const PlaneRow* row) {
    if (mode.channel_count == 1) {
        return nearest_1ch(state, src, row);
    }

    if (mode.channel_count == 4) {
        return nearest_4ch(mode, state, src, row);
    }

    return nearest_3ch(mode, state, src, row);
}

static ALWAYS_INLINE NearestSource nearest_source_new(const RenderCommand* command, const TileClip* clip) {
    const ImageData* image = command->image;
    const int texture_stride = image->padded_width;
    const int plane_size = texture_stride * (int)image->padded_height;

    const int u = command->image_src_x + i32x4_extract(clip->clip_diff, 0) + i16x8_extract(command->clip_adjust, 0);
    const int v = command->image_src_y + i32x4_extract(clip->clip_diff, 1) + i16x8_extract(command->clip_adjust, 1);

    return (NearestSource) {
        .line = image->data + v * texture_stride + u,
        .r_plane = 0,
        .g_plane = plane_size,
        .b_plane = plane_size * 2,
        .a_plane = plane_size * 3,
    };
}

static ALWAYS_INLINE void image_internal_nearest(const NearestMode mode, const TileTarget* target,
                                                 const RenderCommand* command) {
    TileClip clip;
    if (!tile_clip_setup(command->aabb, target, &clip)) {
        return;
    }

    NearestState state;
    state.tint = splat_color(command->corner_colors);
    state.global_alpha = i16x8_new_splat((i16)((command->global_alpha * 0x7FFF) / 255));

    if (mode.rounding) {
        const int corner_direction = render_mask_corner_of(command->mask) - 1;
        corner_rounding_init(&state.rounding_state, i32x4_extract(clip.clip_diff, 1), command->radius,
                             corner_direction);
    }

    const int texture_stride = command->image->padded_width;
    const NearestSource first_line = nearest_source_new(command, &clip);
    const i16x8 write_mask = i16x8_write_mask(clip.xlen);

    for_count(y, clip.ylen) {
        NearestSource src = first_line;
        src.line += y * texture_stride;
        PlaneRow row = plane_row_new(target->output + y * TILE_STRIDE, clip.write_offset);

        if (mode.rounding) {
            corner_rounding_step_y(&state.rounding_state, clip.clip_diff);
        }

        for_count(x, (clip.xlen >> 3)) {
            store_rgb(process_8_pixels_nearest(mode, &state, &src, &row), &row);
            src.line += 8;
            row.line_output += 8;
        }

        if ((clip.xlen & 0x7) > 0) {
            store_rgb_masked(process_8_pixels_nearest(mode, &state, &src, &row), write_mask, &row);
        }
    }
}

#define NEAREST_VARIANT(name, blend, round, channels)                                        \
    static ALWAYS_INLINE void name(const TileTarget* target, const RenderCommand* command) { \
        image_internal_nearest((NearestMode) { blend, round, channels }, target, command);   \
    }

// 1-channel (alpha-only) permutations
NEAREST_VARIANT(render_image_nearest_solid_1ch, false, false, 1)
NEAREST_VARIANT(render_image_nearest_blend_1ch, true, false, 1)
NEAREST_VARIANT(render_image_nearest_rounded_1ch, false, true, 1)
NEAREST_VARIANT(render_image_nearest_blend_rounded_1ch, true, true, 1)

// 3-channel (RGB) permutations
NEAREST_VARIANT(render_image_nearest_solid_3ch, false, false, 3)
NEAREST_VARIANT(render_image_nearest_blend_3ch, true, false, 3)
NEAREST_VARIANT(render_image_nearest_rounded_3ch, false, true, 3)
NEAREST_VARIANT(render_image_nearest_blend_rounded_3ch, true, true, 3)

// 4-channel (RGBA) permutations: always blend with background because of the alpha channel
NEAREST_VARIANT(render_image_nearest_blend_4ch, true, false, 4)
NEAREST_VARIANT(render_image_nearest_blend_rounded_4ch, true, true, 4)

typedef void (*nearest_func_t)(const TileTarget* target, const RenderCommand* command);

static const nearest_func_t image_nearest_dispatch_table[2][2][3] = {
    {
        // No rounding
        { render_image_nearest_solid_1ch, render_image_nearest_solid_3ch,
          render_image_nearest_blend_4ch }, // No blend (4ch always blends)
        { render_image_nearest_blend_1ch, render_image_nearest_blend_3ch,
          render_image_nearest_blend_4ch }, // With blend
    },
    {
        // With rounding
        { render_image_nearest_rounded_1ch, render_image_nearest_rounded_3ch,
          render_image_nearest_blend_rounded_4ch }, // No blend (4ch always blends)
        { render_image_nearest_blend_rounded_1ch, render_image_nearest_blend_rounded_3ch,
          render_image_nearest_blend_rounded_4ch }, // With blend
    },
};

void sr_render_image_nearest(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect, const TileInfo* tile) {
    const ImageData* image = command->image;
    u32 render_mask = command->mask;
    int rounding = !!(render_mask & RenderMask_Corners);
    int blend_with_background = !!(render_mask & RenderMask_BlendWithBackground);

    // Map channel count to dispatch table index: 1->0, 3->1, 4->2
    int channel_index = (image->channels == 1) ? 0 : (image->channels == 3) ? 1 : 2;

    if (channel_index == 2)
        blend_with_background = 1;

    if (command->global_alpha < 255)
        blend_with_background = 1;

    const TileTarget target = { .output = tile_output, .scissor_rect = scissor_rect, .tile = tile };
    image_nearest_dispatch_table[rounding][blend_with_background][channel_index](&target, command);
}
