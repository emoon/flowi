#include "gpu_draw_pack.h"
#include <math.h>

u32 gpu_f32_bits(f32 v) {
    union {
        f32 f;
        u32 u;
    } pun = { .f = v };
    return pun.u;
}

u32 gpu_pack_color(FlColor c, u8 global_alpha) {
    u32 a = ((u32)c.a * (u32)global_alpha + 127u) / 255u;
    return (u32)c.r | ((u32)c.g << 8) | ((u32)c.b << 16) | (a << 24);
}

u32 gpu_pack_tint_premultiplied(FlColor tint, u8 global_alpha) {
    u32 a = ((u32)tint.a * (u32)global_alpha + 127u) / 255u;
    u32 r = ((u32)tint.r * a + 127u) / 255u;
    u32 g = ((u32)tint.g * a + 127u) / 255u;
    u32 b = ((u32)tint.b * a + 127u) / 255u;
    return r | (g << 8) | (b << 16) | (a << 24);
}

static u32 dash_units(f32 len) {
    f32 max_len = (f32)GPU_DRAW_DASH_MASK / (f32)GPU_DRAW_DASH_UNITS_PER_PX;
    return (u32)(fminf(len, max_len) * (f32)GPU_DRAW_DASH_UNITS_PER_PX + 0.5f);
}

u32 gpu_pack_dash(FlStrokeStyle stroke) {
    if (!(stroke.dash_len > 0.0f) || !(stroke.gap_len > 0.0f)) {
        return 0u;
    }
    u32 dash = dash_units(stroke.dash_len);
    u32 gap = dash_units(stroke.gap_len);
    if (dash == 0u || gap == 0u) {
        return 0u;
    }
    return (dash << GPU_DRAW_DASH_SHIFT) | (gap << GPU_DRAW_GAP_SHIFT);
}

void gpu_pack_effect(GpuDrawCmd* cmd, const FlEffect* e, u8 global_alpha) {
    cmd->flags |= ((u32)e->kind << GPU_DRAW_EFFECT_SHIFT) & GPU_DRAW_EFFECT_MASK;
    cmd->effect[0] = gpu_f32_bits(e->off_x);
    cmd->effect[1] = gpu_f32_bits(e->off_y);
    cmd->effect[2] = gpu_f32_bits(fmaxf(e->blur, 0.0f));
    cmd->effect[3] = gpu_pack_color(e->color, global_alpha);
}
