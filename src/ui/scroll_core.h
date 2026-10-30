#pragma once

#include <core/types.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scroll core - shared pure scroll math (single-axis, frame-rate independent, stateless).
//
// ui_scroll, list_uniform and content_view all feed their own positioning policy (target / content extent)
// into these functions; none of them owns the math.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Default smooth-scroll lerp factor (higher is snappier); callers may override.
#define SCROLL_CORE_DEFAULT_SMOOTH_SPEED 8.0f

// Ease offset toward target, scaled by real frame time dt so the result is the same
// regardless of frame rate (speed is the lerp factor - higher is snappier). Snaps to the
// target once within half a pixel, then clamps the result to [0, max].
f32 scroll_smooth_step(f32 offset, f32 target, f32 max, f32 speed, f32 dt);

// Maximum scroll offset for a content extent inside a viewport. Never negative.
f32 scroll_max(f32 content, f32 viewport);

// Index range [*out_start, *out_end) of items intersecting the viewport for a uniform
// virtualized list of item_stride-spaced items, padded by buffer items on each side.
void scroll_visible_range(f32 scroll_offset, f32 viewport, f32 item_stride, u32 total_count, u32 buffer, u32* out_start,
                          u32* out_end);
