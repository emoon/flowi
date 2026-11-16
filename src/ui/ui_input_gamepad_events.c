#include "ui_input.h"
#include <platform/input_types.h>
#include <core/log.h>
#include "ui_private.h"

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
static bool gamepad_button_out_of_range(FlGamepadEvent event) {
    bool is_button_event = event.type == FlGamepadEvent_Button_Down || event.type == FlGamepadEvent_Button_Up;
    return is_button_event && (u32)event.button.button >= FlGamepadButton_Count;
}

static bool gamepad_axis_out_of_range(FlGamepadEvent event) {
    return event.type == FlGamepadEvent_Axis_Motion && (u32)event.axis.axis >= FlGamepadAxis_Count;
}

static bool gamepad_event_is_valid(FlGamepadEvent event) {
    if (event.gamepad_id >= UI_INPUT_MAX_GAMEPADS) {
        return false;
    }

    if (gamepad_button_out_of_range(event)) {
        log_debug("Ignoring gamepad event with out-of-range button %d", (int)event.button.button);
        return false;
    }

    if (gamepad_axis_out_of_range(event)) {
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
