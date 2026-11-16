#include "ui_input.h"
#include <platform/input_types.h>
#include <core/string.h>
#include <core/log.h>
#include <core/memory.h>
#include "ui_private.h"
#include <string.h>

void platform_input_push_key_event(FlUiCtx* ctx, FlKeyEvent event) {
    FlInputState* state = &ctx->input_state;
    KeyboardState* kb = &state->keyboard;

    // The unsigned cast also rejects negative key codes.
    if ((u32)event.key >= FlKey_Count) {
        log_debug("Ignoring key event with out-of-range key code %d", (int)event.key);
        return;
    }

    switch (event.type) {
        case FlKeyEvent_Down:
            if (!kb->keys[event.key]) {
                kb->keys[event.key] = true;
                kb->keys_pressed[event.key] = true;
                kb->key_hold_duration[event.key] = 0.0f;
            }
            kb->modifiers = event.modifiers;
            break;

        case FlKeyEvent_Up:
            kb->keys[event.key] = false;
            kb->keys_released[event.key] = true;
            kb->key_hold_duration[event.key] = 0.0f;
            kb->modifiers = event.modifiers;
            break;

        case FlKeyEvent_Repeat:
            break;
    }
}

// Bytes of text that fit in the frame's remaining room, backing off to a codepoint start so a multi-byte character
// is dropped whole, not split.
static u64 text_input_fit_count(FlString text, u64 room) {
    u64 count = min(text.length, room);
    if (count < text.length) {
        while (count > 0 && (text.data[count] & 0xC0) == 0x80) {
            count--;
        }
    }
    return count;
}

void platform_input_push_text_input_event(FlUiCtx* ctx, FlTextInputEvent event) {
    FlInputState* state = &ctx->input_state;

    // Append each event's text into the frame's buffer, keeping the earliest text on overflow.
    KeyboardState* kb = &state->keyboard;
    u64 used = kb->text_input.length;
    u64 count = text_input_fit_count(event.text, UI_TEXT_INPUT_THIS_FRAME_CAP - used);
    if (count == 0) {
        return;
    }
    memcpy(kb->text_input_storage + used, event.text.data, count);
    kb->text_input = (FlString) { .data = kb->text_input_storage, .length = used + count, .is_static = 0 };
}

static void mouse_button_down(MouseState* mouse, FlMouseEvent event) {
    if (mouse->buttons[event.button.button]) {
        return;
    }
    mouse->buttons[event.button.button] = true;
    mouse->buttons_pressed[event.button.button] = true;
    mouse->button_press_timestamp[event.button.button] = event.timestamp;
    mouse->button_hold_duration[event.button.button] = 0.0f;
}

static void mouse_button_up(MouseState* mouse, FlMouseEvent event) {
    mouse->buttons[event.button.button] = false;
    mouse->buttons_released[event.button.button] = true;
    mouse->button_hold_duration[event.button.button] = 0.0f;
}

static bool mouse_event_has_bad_button(FlMouseEvent event) {
    // The unsigned cast also rejects negative button codes.
    bool is_button_event = event.type == FlMouseEvent_Button_Down || event.type == FlMouseEvent_Button_Up;
    return is_button_event && (u32)event.button.button >= FlMouseButton_Count;
}

static void mouse_push_motion_event(MouseState* mouse, FlMouseEvent event) {
    switch (event.type) {
        case FlMouseEvent_Move:
            mouse->x = event.move.x;
            mouse->y = event.move.y;
            // Delta is not accumulated here; ui_input_update_mouse derives it from the position difference
            break;

        case FlMouseEvent_Wheel:
            mouse->wheel_x += event.wheel.x;
            mouse->wheel_y += event.wheel.y;
            break;

        case FlMouseEvent_Enter:
            mouse->is_inside_window = true;
            break;

        case FlMouseEvent_Leave:
            mouse->is_inside_window = false;
            break;

        case FlMouseEvent_Relative_Motion:
            // Raw relative motion (hidden drag): the unbounded delta accumulates separately from absolute position
            mouse->raw_delta_x += event.relative.delta_x;
            mouse->raw_delta_y += event.relative.delta_y;
            break;

        case FlMouseEvent_Button_Down:
        case FlMouseEvent_Button_Up:
            break;
    }
}

void platform_input_push_mouse_event(FlUiCtx* ctx, FlMouseEvent event) {
    FlInputState* state = &ctx->input_state;
    MouseState* mouse = &state->mouse;

    if (mouse_event_has_bad_button(event)) {
        log_debug("Ignoring mouse event with out-of-range button %d", (int)event.button.button);
        return;
    }

    switch (event.type) {
        case FlMouseEvent_Button_Down:
            // The click count is calculated from timing.
            mouse_button_down(mouse, event);
            break;

        case FlMouseEvent_Button_Up:
            mouse_button_up(mouse, event);
            break;

        default:
            mouse_push_motion_event(mouse, event);
            break;
    }
}

static void gamepad_button_down(GamepadState* gamepad, FlGamepadEvent event) {
    if (gamepad->buttons[event.button.button]) {
        return;
    }
    gamepad->buttons[event.button.button] = true;
    gamepad->buttons_pressed[event.button.button] = true;
    gamepad->button_press_timestamp[event.button.button] = event.timestamp;
    gamepad->button_hold_duration[event.button.button] = 0.0f;
}

static void gamepad_button_up(GamepadState* gamepad, FlGamepadEvent event) {
    gamepad->buttons[event.button.button] = false;
    gamepad->buttons_released[event.button.button] = true;
    gamepad->button_hold_duration[event.button.button] = 0.0f;
}

// The unsigned casts also reject negative button/axis codes.
static bool gamepad_event_is_valid(FlGamepadEvent event) {
    if (event.gamepad_id >= UI_INPUT_MAX_GAMEPADS) {
        return false;
    }

    bool is_button_event = event.type == FlGamepadEvent_Button_Down || event.type == FlGamepadEvent_Button_Up;
    if (is_button_event && (u32)event.button.button >= FlGamepadButton_Count) {
        log_debug("Ignoring gamepad event with out-of-range button %d", (int)event.button.button);
        return false;
    }

    if (event.type == FlGamepadEvent_Axis_Motion && (u32)event.axis.axis >= FlGamepadAxis_Count) {
        log_debug("Ignoring gamepad event with out-of-range axis %d", (int)event.axis.axis);
        return false;
    }

    return true;
}

void platform_input_push_gamepad_event(FlUiCtx* ctx, FlGamepadEvent event) {
    FlInputState* state = &ctx->input_state;

    if (!gamepad_event_is_valid(event)) {
        return;
    }

    GamepadState* gamepad = &state->gamepads[event.gamepad_id];

    switch (event.type) {
        case FlGamepadEvent_Button_Down:
            gamepad_button_down(gamepad, event);
            break;

        case FlGamepadEvent_Button_Up:
            gamepad_button_up(gamepad, event);
            break;

        case FlGamepadEvent_Axis_Motion:
            gamepad->axes[event.axis.axis] = event.axis.value;
            break;

        case FlGamepadEvent_Connected:
            gamepad->connected = true;
            gamepad->id = event.gamepad_id;
            break;

        case FlGamepadEvent_Disconnected:
            gamepad->connected = false;
            break;
    }
}

// Copy one dropped path into the frame arena and record it.
static void push_dropped_file(FlUiCtx* ctx, WindowEventState* window_events, FlWindowDropData drop) {
    if (drop.path.length == 0 || drop.path.data == nullptr) {
        return;
    }
    if (window_events->dropped_file_count >= UI_DROPPED_FILES_CAP) {
        log_warning("Dropped-file queue full, ignoring %.*s", (int)drop.path.length, drop.path.data);
        return;
    }
    char* path_copy = arena_alloc_array(ctx->render_frame_allocator, char, drop.path.length + 1);
    memcpy(path_copy, drop.path.data, drop.path.length);
    path_copy[drop.path.length] = '\0';

    u32 slot = window_events->dropped_file_count++;
    window_events->dropped_files[slot] = (FlString) { .data = path_copy, .length = drop.path.length, .is_static = 0 };
    window_events->dropped_file_positions[slot] = (FlVec2) { .x = drop.x, .y = drop.y };
}

void platform_input_push_window_event(FlUiCtx* ctx, FlWindowEvent event) {
    FlInputState* state = &ctx->input_state;
    WindowEventState* window_events = &state->window_events;

    switch (event.type) {
        case FlWindowEvent_None:
            break;

        case FlWindowEvent_Focus_Gained:
            state->window_focused = true;
            window_events->focus_gained = true;
            break;

        case FlWindowEvent_Focus_Lost:
            state->window_focused = false;
            window_events->focus_lost = true;
            break;

        case FlWindowEvent_Resize:
            window_events->resized = true;
            break;

        case FlWindowEvent_Close:
            state->quit_requested = true;
            window_events->close_requested = true;
            break;

        case FlWindowEvent_Drop_File:
            push_dropped_file(ctx, window_events, event.drop);
            break;
    }
}
