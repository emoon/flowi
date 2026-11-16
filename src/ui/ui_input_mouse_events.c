#include "ui_input.h"
#include <platform/input_types.h>
#include <core/log.h>
#include "ui_private.h"

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
