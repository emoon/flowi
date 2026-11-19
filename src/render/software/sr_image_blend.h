#pragma once

#include <core/simd.h>
#include "calculate_edge_rounding.h"
#include "software_renderer_internal.h"
#include "sr_tile_row.h"

// Fused premultiplied-over composite for a premultiplied RGBA source. src.r/g/b are the premultiplied sampled
// color (R*A), src.a the sampled alpha with the frame's global alpha folded in (A*ga). Result:
// out = src*color_scale + bg*(1 - effective_alpha).

static ALWAYS_INLINE SoaRgb premul_composite(SoaRgba src, SoaRgb bg, i16x8 effective_alpha, i16x8 color_scale) {
    i16x8 inv = i16x8_sub(i16x8_new_splat(0x7fff), effective_alpha);
    return (SoaRgb) {
        .r = i16x8_add(i16x8_mul_high(src.r, color_scale), i16x8_mul_high(bg.r, inv)),
        .g = i16x8_add(i16x8_mul_high(src.g, color_scale), i16x8_mul_high(bg.g, inv)),
        .b = i16x8_add(i16x8_mul_high(src.b, color_scale), i16x8_mul_high(bg.b, inv)),
    };
}

// out = src*ga + bg*(1 - A*ga)
static ALWAYS_INLINE SoaRgb premul_over_8(SoaRgba src, i16x8 global_alpha, SoaRgb bg) {
    return premul_composite(src, bg, src.a, global_alpha);
}

// As premul_over_8 with corner_alpha, the anti-aliased edge coverage: out = src*(ga*coverage) + bg*(1 - A*ga*coverage)
static ALWAYS_INLINE SoaRgb premul_over_8_rounded(SoaRgba src, i16x8 global_alpha, SoaRgb bg, i16x8 corner_alpha) {
    return premul_composite(src, bg, i16x8_mul_high(src.a, corner_alpha), i16x8_mul_high(global_alpha, corner_alpha));
}
