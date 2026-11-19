// GCC -fanalyzer false positives on SIMD intrinsic wrappers
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 12
#pragma GCC diagnostic ignored "-Wanalyzer-use-of-uninitialized-value"
#endif

#include <core/core.h>
#include <core/math.h>
#include <core/log.h>
#include <core/simd.h>
#include "calculate_edge_rounding.h"
#include "sr_command.h"
#include "software_renderer_internal.h"
#include "sr_gradient_interpolation.h"
#include "sr_tile_row.h"
#include <math.h>

typedef struct FillMode {
    bool gradient;
    bool rounding;
    bool blend_with_background;
} FillMode;

typedef struct FillState {
    SoaRgba fixed_color;
    GradientInterpolationState gradient_state;
    CornerRoundingState rounding_state;
} FillState;

static ALWAYS_INLINE SoaRgb process_8_pixels(FillMode mode, FillState* state, const PlaneRow* row) {
    SoaRgba source = state->fixed_color;

    if (mode.gradient) {
        source = gradient_interpolation_step_x(&state->gradient_state);
    }

    SoaRgb color = { .r = source.r, .g = source.g, .b = source.b };

    if (mode.rounding) {
        color = lerp_over_background(color, row, corner_rounding_step_8(&state->rounding_state));
    }

    if (mode.blend_with_background) {
        color = lerp_over_background(color, row, source.a);
    }

    return color;
}

static ALWAYS_INLINE SoaRgb process_8_pixels_border(bool blend_with_background, FillState* state, const PlaneRow* row) {
    const SoaRgba source = state->fixed_color;
    SoaRgb color = { .r = source.r, .g = source.g, .b = source.b };

    color = lerp_over_background(color, row, corner_rounding_step_8_dual(&state->rounding_state));

    if (blend_with_background) {
        color = lerp_over_background(color, row, source.a);
    }

    return color;
}

static ALWAYS_INLINE void fill_internal(const FillMode mode, const TileTarget* target, const RenderCommand* command) {
    i16* output = target->output;
    FillState state;

    TileClip clip;
    if (!tile_clip_setup(command->aabb, target, &clip)) {
        return;
    }

    state.fixed_color = splat_color(command->corner_colors);
    i16x8 write_mask = i16x8_write_mask(clip.xlen);

    if (mode.gradient) {
        i16x8 clip_diff_i16 = i16x8_add(i32x4_pack_i16x8(clip.clip_diff), command->clip_adjust);
        state.gradient_state
            = gradient_interpolation_tile_setup((GradientQuadState*)command->gradient_data, clip_diff_i16);
    }

    if (mode.rounding) {
        const int corner_direction = render_mask_corner_of(command->mask) - 1;
        const int y_diff = i32x4_extract(clip.clip_diff, 1);

        corner_rounding_init(&state.rounding_state, y_diff, command->radius, corner_direction);
    }

    for_count(y, clip.ylen) {
        PlaneRow row = plane_row_new(output, clip.write_offset);

        if (mode.rounding) {
            corner_rounding_step_y(&state.rounding_state, clip.clip_diff);
        }

        if (mode.gradient) {
            gradient_interpolation_step_y(&state.gradient_state);
        }

        for_count(x, (clip.xlen >> 3)) {
            store_rgb(process_8_pixels(mode, &state, &row), &row);
            row.line_output += 8;
        }

        if ((clip.xlen & 0x7) > 0) {
            store_rgb_masked(process_8_pixels(mode, &state, &row), write_mask, &row);
        }

        output += TILE_STRIDE;
    }
}

static ALWAYS_INLINE void fill_border_internal(const bool blend_with_background, const TileTarget* target,
                                               const RenderCommand* command) {
    i16* output = target->output;
    FillState state;

    TileClip clip;
    if (!tile_clip_setup(command->aabb, target, &clip)) {
        return;
    }

    state.fixed_color = splat_color(command->corner_colors);
    i16x8 write_mask = i16x8_write_mask(clip.xlen);

    const int corner_direction = render_mask_corner_of(command->mask) - 1;
    const int y_diff = i32x4_extract(clip.clip_diff, 1);

    corner_rounding_init_dual(&state.rounding_state, y_diff, command->radius, command->inner_radius, corner_direction);

    for_count(y, clip.ylen) {
        PlaneRow row = plane_row_new(output, clip.write_offset);

        corner_rounding_step_y(&state.rounding_state, clip.clip_diff);

        for_count(x, (clip.xlen >> 3)) {
            store_rgb(process_8_pixels_border(blend_with_background, &state, &row), &row);
            row.line_output += 8;
        }

        if ((clip.xlen & 0x7) > 0) {
            store_rgb_masked(process_8_pixels_border(blend_with_background, &state, &row), write_mask, &row);
        }

        output += TILE_STRIDE;
    }
}

static ALWAYS_INLINE void fill_border(const TileTarget* target, const RenderCommand* command) {
    fill_border_internal(false, target, command);
}

static ALWAYS_INLINE void fill_border_blend(const TileTarget* target, const RenderCommand* command) {
    fill_border_internal(true, target, command);
}

static ALWAYS_INLINE void fill_solid(const TileTarget* target, const RenderCommand* command) {
    fill_internal((FillMode) { .gradient = false, .rounding = false, .blend_with_background = false }, target, command);
}

static ALWAYS_INLINE void fill_solid_rounded(const TileTarget* target, const RenderCommand* command) {
    fill_internal((FillMode) { .gradient = false, .rounding = true, .blend_with_background = false }, target, command);
}

static ALWAYS_INLINE void fill_gradient(const TileTarget* target, const RenderCommand* command) {
    fill_internal((FillMode) { .gradient = true, .rounding = false, .blend_with_background = false }, target, command);
}

static ALWAYS_INLINE void fill_gradient_rounded(const TileTarget* target, const RenderCommand* command) {
    fill_internal((FillMode) { .gradient = true, .rounding = true, .blend_with_background = false }, target, command);
}

static ALWAYS_INLINE void fill_solid_blend(const TileTarget* target, const RenderCommand* command) {
    fill_internal((FillMode) { .gradient = false, .rounding = false, .blend_with_background = true }, target, command);
}

static ALWAYS_INLINE void fill_solid_rounded_blend(const TileTarget* target, const RenderCommand* command) {
    fill_internal((FillMode) { .gradient = false, .rounding = true, .blend_with_background = true }, target, command);
}

static ALWAYS_INLINE void fill_gradient_blend(const TileTarget* target, const RenderCommand* command) {
    fill_internal((FillMode) { .gradient = true, .rounding = false, .blend_with_background = true }, target, command);
}

static ALWAYS_INLINE void fill_gradient_rounded_blend(const TileTarget* target, const RenderCommand* command) {
    fill_internal((FillMode) { .gradient = true, .rounding = true, .blend_with_background = true }, target, command);
}

typedef void (*fill_func_t)(const TileTarget* target, const RenderCommand* command);

// Lookup table for all 8 permutations: [blend_with_background][gradient][rounding]

// clang-format off
static const fill_func_t fill_dispatch_table[2][2][2] = {
    // blend_with_background = false
    { // gradient = false
      { fill_solid, fill_solid_rounded },
      // gradient = true
      { fill_gradient, fill_gradient_rounded } 
    },
    // blend_with_background = true
    { // gradient = false
      { fill_solid_blend, fill_solid_rounded_blend },
      // gradient = true
      { fill_gradient_blend, fill_gradient_rounded_blend } 
    }
};
// clang-format on

void sr_render_fill(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect, const TileInfo* tile) {
    u32 render_mask = command->mask;
    int rounding = !!(render_mask & RenderMask_Corners);
    int gradient = !!(render_mask & RenderMask_ColorGradient);
    int blend_with_background = !!(render_mask & RenderMask_BlendWithBackground);

    const TileTarget target = { .output = tile_output, .scissor_rect = scissor_rect, .tile = tile };
    fill_dispatch_table[blend_with_background][gradient][rounding](&target, command);
}

// Lookup table for border rendering: [blend_with_background]

// clang-format off
static const fill_func_t border_dispatch_table[2] = {
    fill_border,
    fill_border_blend
};
// clang-format on

void sr_render_border(i16* tile_output, const RenderCommand* command, f32x4 scissor_rect, const TileInfo* tile) {
    int blend_with_background = !!(command->mask & RenderMask_BlendWithBackground);
    const TileTarget target = { .output = tile_output, .scissor_rect = scissor_rect, .tile = tile };
    border_dispatch_table[blend_with_background](&target, command);
}
