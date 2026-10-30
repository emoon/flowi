#include "scroll_core.h"
#include <core/math.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pure scroll math (see header) - single-axis, frame-rate independent.

#define SCROLL_SNAP_EPSILON 0.5f // Snap offset to target once within this many pixels

f32 scroll_smooth_step(f32 offset, f32 target, f32 max, f32 speed, f32 dt) {
    f32 lerp_factor = clamp_f32_impl(speed * dt, 0.0f, 1.0f);
    f32 diff = target - offset;
    if (abs_f32(diff) > SCROLL_SNAP_EPSILON) {
        offset += diff * lerp_factor;
    } else {
        offset = target;
    }
    return clamp_f32_impl(offset, 0.0f, max);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

f32 scroll_max(f32 content, f32 viewport) {
    return max_f32_impl(0.0f, content - viewport);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void scroll_visible_range(f32 scroll_offset, f32 viewport, f32 item_stride, u32 total_count, u32 buffer, u32* out_start,
                          u32* out_end) {
    if (item_stride <= 0.0f) {
        *out_start = 0;
        *out_end = 0;
        return;
    }

    i32 first = (i32)(scroll_offset / item_stride) - (i32)buffer;
    if (first < 0) {
        first = 0;
    }

    i32 visible_count = (i32)(viewport / item_stride) + 1 + (i32)buffer * 2;
    i32 last = first + visible_count;
    if (last > (i32)total_count) {
        last = (i32)total_count;
    }

    *out_start = (u32)first;
    *out_end = (u32)last;
}
