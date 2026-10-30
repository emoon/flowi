#pragma once

#include <core/core.h>
#include <core/simd.h>
#include "software_renderer_internal.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct GradientQuadState {
    // Top edge colors: [TL_R, TL_G, TL_B, TL_A, TR_R, TR_G, TR_B, TR_A]
    i16x8 top;
    // Vertical deltas (bottom - top): [BL_R-TL_R, BL_G-TL_G, BL_B-TL_B, BL_A-TL_A, BR_R-TR_R, BR_G-TR_G, BR_B-TR_B,
    // BR_A-TR_A]
    i16x8 top_bottom_delta;

    i16x8 x_step;
    i16x8 y_step;
    i16x8 x_step_times_8;

    i16x8 step_pattern; // [0,1,2,3,4,5,6,7] * x_step
} GradientQuadState;

typedef struct GradientInterpolationState {
    // Top edge colors: [TL_R, TL_G, TL_B, TL_A, TR_R, TR_G, TR_B, TR_A]
    i16x8 top;
    // Vertical deltas (bottom - top): [BL_R-TL_R, BL_G-TL_G, BL_B-TL_B, BL_A-TL_A, BR_R-TR_R, BR_G-TR_G, BR_B-TR_B,
    // BR_A-TR_A]
    i16x8 top_bottom_delta;

    // Current scanline start colors (left edge) - broadcast ready
    i16x8 r_left;
    i16x8 g_left;
    i16x8 b_left;
    i16x8 a_left;

    // Current scanline horizontal deltas (right - left) - broadcast ready
    i16x8 r_horz_delta;
    i16x8 g_horz_delta;
    i16x8 b_horz_delta;
    i16x8 a_horz_delta;

    i16x8 x_blend_8pixels;
    // Reset point for each scanline
    i16x8 x_blend_initial;
    i16x8 x_step_times_8;

    i16x8 y_blend_lr;
    i16x8 y_step;
} GradientInterpolationState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void gradient_interpolation_step_y(GradientInterpolationState* state) {
    // Result: [TL_R', TL_G', TL_B', TL_A', TR_R', TR_G', TR_B', TR_A'] where ' indicates interpolated values
    i16x8 rgba_lr = i16x8_lerp_diff(state->top, state->top_bottom_delta, state->y_blend_lr);

    // Get right side colors: [TR_R', TR_G', TR_B', TR_A', ?, ?, ?, ?]
    i16x8 rgba_r = i16x8_rotate_4(rgba_lr);

    // Calculate horizontal deltas: [TR_R'-TL_R', TR_G'-TL_G', TR_B'-TL_B', TR_A'-TL_A', ?, ?, ?, ?]
    i16x8 diff = i16x8_sub(rgba_r, rgba_lr);

    state->r_left = i16x8_splat_lane(rgba_lr, 0);
    state->g_left = i16x8_splat_lane(rgba_lr, 1);
    state->b_left = i16x8_splat_lane(rgba_lr, 2);
    state->a_left = i16x8_splat_lane(rgba_lr, 3);

    state->r_horz_delta = i16x8_splat_lane(diff, 0);
    state->g_horz_delta = i16x8_splat_lane(diff, 1);
    state->b_horz_delta = i16x8_splat_lane(diff, 2);
    state->a_horz_delta = i16x8_splat_lane(diff, 3);

    state->x_blend_8pixels = state->x_blend_initial;
    state->y_blend_lr = i16x8_add(state->y_blend_lr, state->y_step);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Once per quad setup - calculates invariant properties
// rect_size = x0y0x1y1 (i32 for each value)

static ALWAYS_INLINE void gradient_interpolation_setup(GradientQuadState* quad_state, i32x4 rect_size, u8x16 colors) {

    // Calculate dimensions: [width, height, ?, ?] = [x1-x0, y1-y0, ?, ?]
    static const uint8_t shuffle_x1_y1[16] = { 8, 9, 10, 11, 12, 13, 14, 15, 0, 1, 2, 3, 4, 5, 6, 7 }; // [x1,y1,x0,y0]

    i32x4 x1_y1_x0_y0 = i32x4_table_shuffle(rect_size, shuffle_x1_y1);
    i32x4 wh = i32x4_sub(x1_y1_x0_y0, rect_size); // [width, height, ?, ?]
    i32x4 safe_divisor = i32x4_max(wh, i32x4_new_splat(1));

    // Vectorized division: [x_step, y_step, ?, ?] = [0x7fff/(width), 0x7fff/(height), ?, ?]
    i32x4 fixed_point_one = i32x4_new_splat(0x7fff);
    i32x4 step_raw = i32x4_div(fixed_point_one, safe_divisor);

    i16x8 steps_i16 = i32x4_pack_i16x8(step_raw); // [x_step, y_step, ?, ?, x_step, y_step, ?, ?]

    quad_state->x_step = i16x8_splat_lane(steps_i16, 0);
    quad_state->y_step = i16x8_splat_lane(steps_i16, 1);

    i16x8 eight_vec = i16x8_new_splat(8);
    quad_state->x_step_times_8 = i16x8_mul(quad_state->x_step, eight_vec);

    static const i16 step_values[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
    i16x8 step_pattern_base = i16x8_load_unaligned(&step_values, 0);
    quad_state->step_pattern = i16x8_mul(step_pattern_base, quad_state->x_step);

    // Unpack colors to 16-bit (8:7 fixed point)
    i16x8x2 colors_wide = u8x16_unpack_to_i16x8_wide_shl(colors, 7);

    // colors_wide.v0 = [TL_R, TL_G, TL_B, TL_A, TR_R, TR_G, TR_B, TR_A]
    // colors_wide.v1 = [BL_R, BL_G, BL_B, BL_A, BR_R, BR_G, BR_B, BR_A]

    quad_state->top = colors_wide.v0;
    i16x8 bottom = colors_wide.v1;

    quad_state->top_bottom_delta = i16x8_sub(bottom, quad_state->top);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-tile setup - applies tile-specific clipping
// clip should contain [clip_x, clip_y, ...] as i16 values

static ALWAYS_INLINE GradientInterpolationState gradient_interpolation_tile_setup(GradientQuadState* quad_state,
                                                                                  i16x8 clip) {
    GradientInterpolationState state;

    state.top = quad_state->top;
    state.top_bottom_delta = quad_state->top_bottom_delta;
    state.x_step_times_8 = quad_state->x_step_times_8;

    i16x8 x_clip_broadcast = i16x8_splat_lane(clip, 0);
    i16x8 y_clip_broadcast = i16x8_splat_lane(clip, 1);

    i16x8 x_start = i16x8_mul(quad_state->x_step, x_clip_broadcast);
    i16x8 y_start = i16x8_mul(quad_state->y_step, y_clip_broadcast);

    state.y_step = quad_state->y_step;
    state.y_blend_lr = y_start; // Already broadcast from y_clip_broadcast multiplication

    // x_start is already broadcast, just need to extract first element and add pattern
    i16x8 x_start_base = i16x8_splat_lane(x_start, 0);
    state.x_blend_initial = i16x8_add(x_start_base, quad_state->step_pattern);
    state.x_blend_8pixels = state.x_blend_initial;

    return state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE SoaRgba gradient_interpolation_step_x(GradientInterpolationState* state) {
    SoaRgba result;

    result.r = i16x8_lerp_diff(state->r_left, state->r_horz_delta, state->x_blend_8pixels);
    result.g = i16x8_lerp_diff(state->g_left, state->g_horz_delta, state->x_blend_8pixels);
    result.b = i16x8_lerp_diff(state->b_left, state->b_horz_delta, state->x_blend_8pixels);
    result.a = i16x8_lerp_diff(state->a_left, state->a_horz_delta, state->x_blend_8pixels);

    state->x_blend_8pixels = i16x8_add(state->x_blend_8pixels, state->x_step_times_8);

    return result;
}
