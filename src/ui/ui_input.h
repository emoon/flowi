#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-frame input state of a FlUiCtx. The window backends feed it through the platform_input_push_* sink
// (platform/input_types.h), which this module implements; the frame loop advances it once per frame.

typedef struct FlUiCtx FlUiCtx;
typedef struct FlInputState FlInputState;

// Initialize a per-context input state to defaults.
void ui_input_state_init(FlInputState* state);
// Set the absolute mouse position directly, bypassing event delivery.
void ui_input_set_mouse_position(FlUiCtx* ctx, float x, float y);
// Advance per-frame input state by dt seconds (called once per frame).
void ui_input_update(FlUiCtx* ctx, double dt);
