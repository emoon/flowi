#pragma once

#include <core/core.h>
#include <core/string.h>
// FlCursor / FlCursorShape are authored in cursor.def; api_gen owns them.
#include <flowi/ui/cursor.h>

// The input event types and their discriminant enums are authored in api_gen's input.def and generated into the
// public header below. This file is the internal include seam platform/ui code reaches them through, and it declares
// the push sink the window backends feed a window's FlUiCtx with. The ui module implements the sink; platform code
// only ever holds an opaque FlUiCtx*.
#include <flowi/platform/input.h>

#define UI_INPUT_MAX_GAMEPADS 16

typedef struct FlUiCtx FlUiCtx;

// Push a keyboard event onto the context's input queue.
void platform_input_push_key_event(FlUiCtx* ctx, FlKeyEvent event);
// Push a text-input (composed character) event.
void platform_input_push_text_input_event(FlUiCtx* ctx, FlTextInputEvent event);
// Push a mouse event (move / button / wheel / relative motion).
void platform_input_push_mouse_event(FlUiCtx* ctx, FlMouseEvent event);
// Push a gamepad event (button / axis / connect / disconnect).
void platform_input_push_gamepad_event(FlUiCtx* ctx, FlGamepadEvent event);
// Push a window event (focus / resize / close).
void platform_input_push_window_event(FlUiCtx* ctx, FlWindowEvent event);
