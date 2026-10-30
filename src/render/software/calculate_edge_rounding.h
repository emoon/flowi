#pragma once

#include <core/core.h>
#include <core/math.h>
#include <core/simd.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct CornerRoundingState {
    f16x8 y_step;
    f16x8 x_step;
    f16x8 y_current;
    f16x8 x_current;
    f16x8 border_radius_v;
    f16x8 inner_radius_v; // For dual-radii border rendering (0 for solid corners)
    f16x8 circle_center_x;
    f16x8 circle_center_y;
    f16x8 circle_y2;
} CornerRoundingState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void corner_rounding_init_dual(CornerRoundingState* self, float y_offset, float outer_radius,
                                                    float inner_radius, int direction) {
    const Vec2 s_corner_offsets[4] = {
        { 1.0f, 1.0f }, // TopLeft: No shift
        { 0.0f, 1.0f }, // TopRight: Shift down
        { 1.0f, 0.0f }, // BottomLeft: Shift right
        { 0.0f, 0.0f }, // BottomRight: Shift right and down
    };

    const Vec2 center_adjust = s_corner_offsets[direction & 3];

    self->border_radius_v = f16x8_new_splat(outer_radius);
    self->inner_radius_v = f16x8_new_splat(inner_radius);

    // Circle center = radius * offset - 0.5, symmetric across all corners.
    const f32 center_x = (outer_radius * center_adjust.x) - 0.5f;
    const f32 center_y = (outer_radius * center_adjust.y) - 0.5f;
    self->circle_center_x = f16x8_new_splat(center_x);
    self->circle_center_y = f16x8_new_splat(center_y);
    self->circle_y2 = f16x8_new_splat(0.0f);
    self->y_current = f16x8_new_splat(y_offset);
    self->y_step = f16x8_new_splat(1.0f);
    self->x_step = f16x8_new_splat(8.0);
    self->x_current = f16x8_new_splat(0.0f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void corner_rounding_init(CornerRoundingState* self, float y_offset, float radius, int direction) {
    corner_rounding_init_dual(self, y_offset, radius, 0.0f, direction);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE void corner_rounding_step_y(CornerRoundingState* self, const i32x4 clip_diff) {
    const f16x8 const_0123467 = f16x8_new(0.0, 1.0f, 2.0f, 3.0, 4.0f, 5.0f, 6.0f, 7.0f);
    const f16x8 t0 = f16x8_sub(self->y_current, self->circle_center_y);
    const f32 x_start = (f32)i32x4_extract(clip_diff, 0);
    self->circle_y2 = f16x8_mul(t0, t0);
    self->x_current = f16x8_add(f16x8_new_splat(x_start), const_0123467);
    self->y_current = f16x8_add(self->y_current, self->y_step);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Calculates the blending factor for rounded corners in vectorized form. circle_y2 holds the squared
/// y-distances from the circle centers. Returns anti-aliasing blending factors scaled to 0..32767.
static ALWAYS_INLINE i16x8 calculate_edge_rounding(f16x8 circle_y2, f16x8 current_x, f16x8 circle_center_x,
                                                   f16x8 border_radius_v) {
    f16x8 t0 = f16x8_sub(current_x, circle_center_x);
    f16x8 circle_x2 = f16x8_mul(t0, t0);

    f16x8 sum = f16x8_add(circle_x2, circle_y2);
    f16x8 dist = f16x8_sqrt_approx(sum);

    f16x8 dist_to_edge = f16x8_sub(dist, border_radius_v);

    f16x8 one = f16x8_new_splat(1.0f);
    f16x8 zero = f16x8_new_splat(0.0f);

    // Standard anti-aliasing: transition from edge (alpha=1) to 1px outside (alpha=0)
    f16x8 clamped = f16x8_clamp(dist_to_edge, zero, one);
    f16x8 alpha = f16x8_sub(one, clamped);

    f16x8 scale = f16x8_new_splat(32767.0f);
    f16x8 scaled = f16x8_mul(alpha, scale);

    return f16x8_as_i16x8(scaled);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Calculates the blending factor for rounded corners with dual radii (inner and outer).
///
/// The result is 32767 when inside the border ring, 0 when outside outer or inside inner,
/// and smoothly interpolated at the edges.

static ALWAYS_INLINE i16x8 calculate_edge_rounding_dual(f16x8 circle_y2, f16x8 current_x, f16x8 circle_center_x,
                                                        f16x8 outer_radius_v, f16x8 inner_radius_v) {
    f16x8 t0 = f16x8_sub(current_x, circle_center_x);
    f16x8 circle_x2 = f16x8_mul(t0, t0);

    f16x8 sum = f16x8_add(circle_x2, circle_y2);
    f16x8 dist = f16x8_sqrt_approx(sum);

    f16x8 one = f16x8_new_splat(1.0f);
    f16x8 zero = f16x8_new_splat(0.0f);
    f16x8 scale = f16x8_new_splat(32767.0f);

    // Outer edge: 1.0 inside, 0.0 outside (with 1px AA)
    f16x8 outer_dist = f16x8_sub(dist, outer_radius_v);
    f16x8 outer_clamped = f16x8_clamp(outer_dist, zero, one);
    f16x8 outer_factor = f16x8_sub(one, outer_clamped);

    // Inner edge: 0.0 inside, 1.0 outside (with 1px AA)
    f16x8 inner_dist = f16x8_sub(dist, inner_radius_v);
    f16x8 inner_offset = f16x8_add(inner_dist, one);
    f16x8 inner_clamped = f16x8_clamp(inner_offset, zero, one);

    // Combined: only visible in the ring between inner and outer
    f16x8 combined = f16x8_mul(outer_factor, inner_clamped);
    f16x8 scaled = f16x8_mul(combined, scale);

    return f16x8_as_i16x8(scaled);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE i16x8 corner_rounding_step_8(CornerRoundingState* self) {
    i16x8 v = calculate_edge_rounding(self->circle_y2, self->x_current, self->circle_center_x, self->border_radius_v);

    self->x_current = f16x8_add(self->x_current, self->x_step);

    return v;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE i16x8 corner_rounding_step_8_dual(CornerRoundingState* self) {
    i16x8 v = calculate_edge_rounding_dual(self->circle_y2, self->x_current, self->circle_center_x,
                                           self->border_radius_v, self->inner_radius_v);

    self->x_current = f16x8_add(self->x_current, self->x_step);

    return v;
}
