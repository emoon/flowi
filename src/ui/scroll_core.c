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

ScrollRange scroll_visible_range(f64 scroll_offset, f32 viewport, f32 item_stride, u32 total_count, u32 buffer) {
    if (item_stride <= 0.0f) {
        return (ScrollRange) { 0 };
    }

    f64 total = (f64)total_count;
    f64 first = trunc(scroll_offset / item_stride) - (f64)buffer;
    f64 last = trunc((scroll_offset + viewport) / item_stride) + 1.0 + (f64)buffer;
    if (!isfinite(first) || !isfinite(last)) {
        return (ScrollRange) { 0 };
    }

    first = clamp(first, 0.0, total);
    last = clamp(last, first, total);

    return (ScrollRange) { .start = (u32)first, .end = (u32)last };
}
