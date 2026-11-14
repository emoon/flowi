#include "gpu_translate.h"
#include "gpu_aabb.h"

// Expand a shape's bounds to cover where its effect reaches, before the bounds meet the clip: a drop shadow
// shifts by its offset and dilates by its blur, a glow stays centered and just dilates. The union with the
// unshifted shape keeps the primitive's own tiles.
static GpuAabb effect_expand_bounds(GpuAabb b, const FlEffect* e) {
    if (e == nullptr || e->kind == FlEffectKind_None) {
        return b;
    }
    f32 blur = fmaxf(e->blur, 0.0f);
    bool shadow = e->kind == FlEffectKind_DropShadow;
    f32 ox = shadow ? e->off_x : 0.0f; // glow is centered
    f32 oy = shadow ? e->off_y : 0.0f;
    GpuAabb reach = { b.x0 + ox - blur, b.y0 + oy - blur, b.x1 + ox + blur, b.y1 + oy + blur };
    return (GpuAabb) {
        .x0 = fminf(b.x0, reach.x0),
        .y0 = fminf(b.y0, reach.y0),
        .x1 = fmaxf(b.x1, reach.x1),
        .y1 = fmaxf(b.y1, reach.y1),
    };
}

static GpuAabb clipped_bounds(const GpuTranslate* tr, GpuAabb bounds) {
    return gpu_aabb_intersect(effect_expand_bounds(bounds, tr->effect), gpu_clip_stack_current(&tr->stack));
}

bool gpu_clip_visible(const GpuTranslate* tr, GpuAabb bounds) {
    return !gpu_aabb_is_empty(clipped_bounds(tr, bounds));
}

void gpu_emit(GpuTranslate* tr, GpuAabb bounds, GpuDrawCmd cmd) {
    GpuAabb clipped = clipped_bounds(tr, bounds);
    if (gpu_aabb_is_empty(clipped)) {
        return;
    }
    u32 slot = tr->n++;
    tr->out->aabbs[slot] = clipped;
    cmd.clip = gpu_clip_rects_find_or_append(tr->out, gpu_clip_stack_current(&tr->stack));
    tr->out->cmds[slot] = cmd;
}
