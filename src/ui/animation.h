#pragma once

#include <core/types.h>
#include <math.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Exponential ease-out animation, frame-rate aware: one rate per frame from anim_exp_ease_out_rate(), then one
// anim_exp_toward() per animated value.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Speed multiplier; higher is faster. The default produces ~200-250ms animations.
#define ANIM_SPEED_DEFAULT 16.0f

#define ANIM_SPEED_SLOW 8.0f

#define ANIM_SPEED_FAST 24.0f

// Widget state animation speeds
#define ANIM_SPEED_HOVER 22.0f // ~150ms
#define ANIM_SPEED_PRESS 32.0f // ~100ms
#define ANIM_SPEED_FOCUS 20.0f // ~160ms

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The per-frame animation rate for speed (an ANIM_SPEED_* constant) over dt seconds.

static inline f32 anim_exp_ease_out_rate(f32 speed, f32 dt) {
    return 1.0f - powf(2.0f, -speed * dt);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Step value toward target at rate (from anim_exp_ease_out_rate). Settles asymptotically, never exactly.

static inline f32 anim_exp_toward(f32 value, f32 target, f32 rate) {
    return value + rate * (target - value);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// anim_exp_toward with a snap to target once within snap_threshold.

static inline f32 anim_exp_toward_snap(f32 value, f32 target, f32 rate, f32 snap_threshold) {
    f32 new_value = value + rate * (target - value);
    if (fabsf(new_value - target) < snap_threshold) {
        return target;
    }
    return new_value;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ease a 0..1 toggle value toward 1.0 (on) or 0.0, snapping within ANIM_TOGGLE_SNAP so a settled toggle
// reads as exactly 0.0 or 1.0.

#define ANIM_TOGGLE_SNAP 0.001f

static inline f32 anim_toggle_toward(f32 value, bool on, f32 speed, f32 dt) {
    f32 rate = anim_exp_ease_out_rate(speed, dt);
    return anim_exp_toward_snap(value, on ? 1.0f : 0.0f, rate, ANIM_TOGGLE_SNAP);
}
