#pragma once

// GPU backend: GpuAabb helpers. Bounds are inclusive-min, exclusive-max.

#include "gpu_ir_to_draw_cmd.h"
#include <math.h>
#include <ui/render_ir.h>

static inline GpuAabb gpu_aabb_from_rect(FlRect r) {
    return (GpuAabb) { r.x, r.y, r.x + r.w, r.y + r.h };
}

static inline GpuAabb gpu_aabb_intersect(GpuAabb a, GpuAabb b) {
    return (GpuAabb) {
        .x0 = fmaxf(a.x0, b.x0),
        .y0 = fmaxf(a.y0, b.y0),
        .x1 = fminf(a.x1, b.x1),
        .y1 = fminf(a.y1, b.y1),
    };
}

static inline bool gpu_aabb_is_empty(GpuAabb a) {
    return a.x1 <= a.x0 || a.y1 <= a.y0;
}

static inline void gpu_aabb_add_point(GpuAabb* bb, f32 x, f32 y) {
    bb->x0 = fminf(bb->x0, x);
    bb->y0 = fminf(bb->y0, y);
    bb->x1 = fmaxf(bb->x1, x);
    bb->y1 = fmaxf(bb->y1, y);
}
