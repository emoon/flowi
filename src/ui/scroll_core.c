#include "scroll_core.h"
#include <core/math.h>

#include <math.h>

#define SCROLL_SNAP_EPSILON 0.5f // Snap offset to target once within this many pixels

f64 scroll_smooth_step(f64 offset, f64 target, f64 max, f32 speed, f32 dt) {
    f32 lerp_factor = clamp(speed * dt, 0.0f, 1.0f);
    f64 diff = target - offset;
    if (fabs(diff) > SCROLL_SNAP_EPSILON) {
        offset += diff * lerp_factor;
    } else {
        offset = target;
    }
    return clamp(offset, 0.0, max);
}

f64 scroll_max(f64 content, f64 viewport) {
    return max(0.0, content - viewport);
}

void scroll_visible_range(f64 scroll_offset, f32 viewport, f32 item_stride, u32 total_count, u32 buffer, u32* out_start,
                          u32* out_end) {
    if (item_stride <= 0.0f) {
        *out_start = 0;
        *out_end = 0;
        return;
    }

    i64 total = (i64)total_count;
    i64 first = (i64)(scroll_offset / item_stride) - (i64)buffer;
    i64 last = (i64)((scroll_offset + viewport) / item_stride) + 1 + (i64)buffer;

    first = clamp(first, 0, total);
    last = clamp(last, first, total);

    *out_start = (u32)first;
    *out_end = (u32)last;
}
