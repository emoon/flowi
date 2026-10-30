#pragma once

#include <core/simd.h>
#include "calculate_edge_rounding.h"
#include "software_renderer_internal.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ALWAYS_INLINE SoaRgba splat_color(u8x16 color) {
    i16x8x2 corner_colors = u8x16_unpack_to_i16x8_wide_shl(color, 7);
    return (SoaRgba) {
        .r = i16x8_splat_lane(corner_colors.v0, 0),
        .g = i16x8_splat_lane(corner_colors.v0, 1),
        .b = i16x8_splat_lane(corner_colors.v0, 2),
        .a = i16x8_splat_lane(corner_colors.v0, 3),
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Fused premultiplied-over composite for a premultiplied RGBA source. r/g/b_res are the premultiplied sampled
// color (R*A), a_res the sampled alpha with the frame's global alpha folded in (A*ga). corner_alpha is the
// anti-aliased edge coverage. Result: out = src*(ga*coverage) + bg*(1 - A*ga*coverage).

static ALWAYS_INLINE SoaRgb premul_over_8(const bool rounding, CornerRoundingState* rounding_state, i16x8 r_res,
                                          i16x8 g_res, i16x8 b_res, i16x8 a_res, i16x8 global_alpha, i16x8 bg_r,
                                          i16x8 bg_g, i16x8 bg_b) {
    i16x8 effective_alpha, color_scale;
    if (rounding) {
        i16x8 corner_alpha = corner_rounding_step_8(rounding_state);
        effective_alpha = i16x8_mul_high(a_res, corner_alpha);    // A·ga·coverage
        color_scale = i16x8_mul_high(global_alpha, corner_alpha); // ga·coverage (A already in r_res)
    } else {
        effective_alpha = a_res;    // A·ga
        color_scale = global_alpha; // ga
    }
    i16x8 inv = i16x8_sub(i16x8_new_splat(0x7fff), effective_alpha);
    return (SoaRgb) {
        .r = i16x8_add(i16x8_mul_high(r_res, color_scale), i16x8_mul_high(bg_r, inv)),
        .g = i16x8_add(i16x8_mul_high(g_res, color_scale), i16x8_mul_high(bg_g, inv)),
        .b = i16x8_add(i16x8_mul_high(b_res, color_scale), i16x8_mul_high(bg_b, inv)),
    };
}
