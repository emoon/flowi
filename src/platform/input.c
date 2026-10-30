#include "input_types.h"
#include <core/string.h>
#include <core/log.h>
#include <core/memory.h>
#include "ui/input_actions.h"
#include "ui/ui_private.h"
#include <math.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void fl_platform_input_state_reset(FlInputState* state) {
    memory_zero(state, sizeof(FlInputState));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_platform_input_state_init(FlInputState* state) {
    fl_platform_input_state_reset(state);
    for_count(i, UI_INPUT_MAX_GAMEPADS) {
        state->gamepads[i].dead_zone = UI_INPUT_GAMEPAD_DEFAULT_DEAD_ZONE;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_platform_input_push_key_event(FlUiCtx* ctx, FlKeyEvent event) {
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_platform_input_push_text_input_event(FlUiCtx* ctx, FlTextInputEvent event) {
    FlInputState* state = &ctx->input_state;

    // Append each event's text into the frame's buffer, keeping the earliest text on overflow.
    KeyboardState* kb = &state->keyboard;
    u64 used = kb->text_input.length;
    u64 room = UI_TEXT_INPUT_THIS_FRAME_CAP - used;
    u64 count = min(event.text.length, room);
    if (count < event.text.length) {
        // Back off to a codepoint start so a multi-byte character is dropped whole, not split.
        while (count > 0 && (event.text.data[count] & 0xC0) == 0x80) {
            count--;
        }
    }
    if (count == 0) {
        return;
    }
    memcpy(kb->text_input_storage + used, event.text.data, count);
    kb->text_input = (FlString) { .data = kb->text_input_storage, .length = used + count, .is_static = 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_platform_input_push_mouse_event(FlUiCtx* ctx, FlMouseEvent event) {
    FlInputState* state = &ctx->input_state;
    MouseState* mouse = &state->mouse;

    // The unsigned cast also rejects negative button codes.
    if ((event.type == FlMouseEvent_Button_Down || event.type == FlMouseEvent_Button_Up)
        && (u32)event.button.button >= FlMouseButton_Count) {
        log_debug("Ignoring mouse event with out-of-range button %d", (int)event.button.button);
        return;
    }

    switch (event.type) {
        case FlMouseEvent_Move:
            mouse->x = event.move.x;
            mouse->y = event.move.y;
            // Delta is not accumulated here; fl_platform_input_update_mouse derives it from the position difference
            break;

        case FlMouseEvent_Button_Down:
            if (!mouse->buttons[event.button.button]) {
                mouse->buttons[event.button.button] = true;
                mouse->buttons_pressed[event.button.button] = true;
                mouse->button_press_timestamp[event.button.button] = event.timestamp;
                mouse->button_hold_duration[event.button.button] = 0.0f;
            }
            // The click count is calculated from timing.
            break;

        case FlMouseEvent_Button_Up:
            mouse->buttons[event.button.button] = false;
            mouse->buttons_released[event.button.button] = true;
            mouse->button_hold_duration[event.button.button] = 0.0f;
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
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_platform_input_push_gamepad_event(FlUiCtx* ctx, FlGamepadEvent event) {
    FlInputState* state = &ctx->input_state;

    if (event.gamepad_id >= UI_INPUT_MAX_GAMEPADS) {
        return;
    }

    // The unsigned casts also reject negative button/axis codes.
    if ((event.type == FlGamepadEvent_Button_Down || event.type == FlGamepadEvent_Button_Up)
        && (u32)event.button.button >= FlGamepadButton_Count) {
        log_debug("Ignoring gamepad event with out-of-range button %d", (int)event.button.button);
        return;
    }

    if (event.type == FlGamepadEvent_Axis_Motion && (u32)event.axis.axis >= FlGamepadAxis_Count) {
        log_debug("Ignoring gamepad event with out-of-range axis %d", (int)event.axis.axis);
        return;
    }

    GamepadState* gamepad = &state->gamepads[event.gamepad_id];

    switch (event.type) {
        case FlGamepadEvent_Button_Down:
            if (!gamepad->buttons[event.button.button]) {
                gamepad->buttons[event.button.button] = true;
                gamepad->buttons_pressed[event.button.button] = true;
                gamepad->button_press_timestamp[event.button.button] = event.timestamp;
                gamepad->button_hold_duration[event.button.button] = 0.0f;
            }
            break;

        case FlGamepadEvent_Button_Up:
            gamepad->buttons[event.button.button] = false;
            gamepad->buttons_released[event.button.button] = true;
            gamepad->button_hold_duration[event.button.button] = 0.0f;
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_platform_input_push_window_event(FlUiCtx* ctx, FlWindowEvent event) {
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_platform_input_set_mouse_position(FlUiCtx* ctx, float x, float y) {
    FlInputState* state = &ctx->input_state;
    state->mouse.x = x;
    state->mouse.y = y;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool is_mouse_pos_valid(float x, float y) {
    return x > UI_MOUSE_INVALID_POS && y > UI_MOUSE_INVALID_POS;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static float mouse_distance_sqr(float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    return dx * dx + dy * dy;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void fl_platform_input_update_mouse(FlInputState* state, double dt) {
    MouseState* mouse = &state->mouse;

    if (is_mouse_pos_valid(mouse->x, mouse->y)) {
        mouse->x = floorf(mouse->x + 0.5f);
        mouse->y = floorf(mouse->y + 0.5f);
        mouse->position_valid = true;
    } else {
        mouse->position_valid = false;
    }

    if (mouse->position_valid && is_mouse_pos_valid(mouse->prev_x, mouse->prev_y)) {
        mouse->delta_x = mouse->x - mouse->prev_x;
        mouse->delta_y = mouse->y - mouse->prev_y;
    } else {
        mouse->delta_x = 0.0f;
        mouse->delta_y = 0.0f;
    }

    float delta_length_sqr = mouse->delta_x * mouse->delta_x + mouse->delta_y * mouse->delta_y;
    float threshold_sqr = UI_MOUSE_STATIONARY_THRESHOLD * UI_MOUSE_STATIONARY_THRESHOLD;
    bool mouse_stationary = delta_length_sqr <= threshold_sqr;

    if (mouse_stationary) {
        mouse->stationary_timer += (float)dt;
    } else {
        mouse->stationary_timer = 0.0f;
    }

    for_count(i, FlMouseButton_Count) {
        if (mouse->buttons_pressed[i]) {
            bool is_repeated_click = false;

            if (mouse->position_valid
                && (state->time * 1000.0 - mouse->last_click_time[i]) < (UI_MOUSE_DOUBLE_CLICK_TIME * 1000.0)) {

                float dist_sqr = mouse_distance_sqr(mouse->x, mouse->y, mouse->click_pos_x[i], mouse->click_pos_y[i]);

                if (dist_sqr < UI_MOUSE_DOUBLE_CLICK_MAX_DIST * UI_MOUSE_DOUBLE_CLICK_MAX_DIST) {
                    is_repeated_click = true;
                }
            }

            if (is_repeated_click) {
                mouse->click_count[i]++;
            } else {
                mouse->click_count[i] = 1;
            }

            mouse->click_pos_x[i] = mouse->x;
            mouse->click_pos_y[i] = mouse->y;
            mouse->last_click_time[i] = (u64)(state->time * 1000.0);
            mouse->drag_max_distance_sqr[i] = 0.0f;
        }

        if (mouse->buttons[i] && mouse->position_valid) {
            float drag_dist_sqr = mouse_distance_sqr(mouse->x, mouse->y, mouse->click_pos_x[i], mouse->click_pos_y[i]);
            if (drag_dist_sqr > mouse->drag_max_distance_sqr[i]) {
                mouse->drag_max_distance_sqr[i] = drag_dist_sqr;
            }
        }
    }

    mouse->prev_x = mouse->x;
    mouse->prev_y = mouse->y;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_platform_input_update(FlUiCtx* ctx, double dt) {
    FlInputState* state = &ctx->input_state;
    KeyboardState* kb = &state->keyboard;

    // Window-activation keyboard focus: focus-lost stashes and clears the live focus; focus-gained restores the
    // stashed widget.
    if (state->window_events.focus_lost) {
        if (ctx->focused_id != 0) {
            ctx->last_focused_id = ctx->focused_id;
        }
        ctx->focused_id = 0;
    }
    if (state->window_events.focus_gained) {
        ctx->focused_id = ctx->last_focused_id;
    }

    ctx->delta_time = (float)dt;

    // Capture mouse button state for IMGUI before we clear the flags
    ctx->mouse_pressed_this_frame = state->mouse.buttons_pressed[FlMouseButton_Left];
    ctx->mouse_released_this_frame = state->mouse.buttons_released[FlMouseButton_Left];

    for_count(i, FlMouseButton_Count) {
        ctx->mouse_buttons_pressed_this_frame[i] = state->mouse.buttons_pressed[i];
        ctx->mouse_buttons_released_this_frame[i] = state->mouse.buttons_released[i];
    }

    for_count(i, FlKey_Count) {
        ctx->keys_pressed_this_frame[i] = kb->keys_pressed[i];
        ctx->keys_released_this_frame[i] = kb->keys_released[i];
    }

    for_count(pad, UI_INPUT_MAX_GAMEPADS) {
        for_count(i, FlGamepadButton_Count) {
            ctx->gamepad_buttons_pressed_this_frame[pad][i] = state->gamepads[pad].buttons_pressed[i];
            ctx->gamepad_buttons_released_this_frame[pad][i] = state->gamepads[pad].buttons_released[i];
        }
    }

    // This-frame typed-text snapshot, taken before the raw kb->text_input is cleared below.
    u64 text_len = kb->text_input.length;
    if (text_len > 0) {
        memcpy(ctx->text_input_buffer, kb->text_input.data, text_len);
    }
    ctx->text_input_this_frame = (FlString) { .data = ctx->text_input_buffer, .length = text_len, .is_static = 0 };

    // This-frame dropped-file snapshot: paths packed NUL-separated into one buffer; a path that would overflow it
    // ends the snapshot.
    ctx->dropped_file_count_this_frame = 0;
    u64 path_bytes = 0;
    for_count(i, state->window_events.dropped_file_count) {
        FlString path = state->window_events.dropped_files[i];
        if (path.length + 1 > UI_DROPPED_PATH_BYTES - path_bytes) {
            log_warning("Dropped-file path buffer full, ignoring the rest of this frame's drop");
            break;
        }
        char* slot = ctx->dropped_file_buffer + path_bytes;
        memcpy(slot, path.data, path.length);
        slot[path.length] = '\0';
        ctx->dropped_files_this_frame[i] = (FlString) { .data = slot, .length = path.length, .is_static = 0 };
        ctx->dropped_file_positions_this_frame[i] = state->window_events.dropped_file_positions[i];
        path_bytes += path.length + 1;
        ctx->dropped_file_count_this_frame++;
    }

    // Shift held swaps the wheel axes for this frame's accumulated values, before they are cached and cleared.
    state->mouse.wheel_axis_swap_requested = (kb->modifiers & FlKeyModifier_Shift) != 0;
    if (state->mouse.wheel_axis_swap_requested) {
        float temp = state->mouse.wheel_x;
        state->mouse.wheel_x = state->mouse.wheel_y;
        state->mouse.wheel_y = temp;
    }

    // Cache the wheel values for the frame before the live accumulators are cleared further down.
    state->mouse.wheel_x_cached = state->mouse.wheel_x;
    state->mouse.wheel_y_cached = state->mouse.wheel_y;

    // Update input action system (evaluates actions before we clear flags)
    ui_actions_update(ctx);

    ctx->nav_next_pressed = ui_action_pressed(ctx, FlAction_Nav_Next);
    ctx->nav_prev_pressed = ui_action_pressed(ctx, FlAction_Nav_Prev);
    ctx->nav_activate_pressed = ui_action_pressed(ctx, FlAction_Confirm);

    // Software repeat: trigger on initial press OR at intervals after delay
    ctx->nav_frame.focused_id = ctx->focused_id;
    ctx->nav_frame.nav_up
        = ui_action_pressed(ctx, FlAction_Nav_Up)
          || input_should_nav_repeat(kb, FlKey_Up, UI_NAV_REPEAT_DELAY, UI_NAV_REPEAT_RATE, (float)dt);
    ctx->nav_frame.nav_down
        = ui_action_pressed(ctx, FlAction_Nav_Down)
          || input_should_nav_repeat(kb, FlKey_Down, UI_NAV_REPEAT_DELAY, UI_NAV_REPEAT_RATE, (float)dt);
    ctx->nav_frame.nav_left
        = ui_action_pressed(ctx, FlAction_Nav_Left)
          || input_should_nav_repeat(kb, FlKey_Left, UI_NAV_REPEAT_DELAY, UI_NAV_REPEAT_RATE, (float)dt);
    ctx->nav_frame.nav_right
        = ui_action_pressed(ctx, FlAction_Nav_Right)
          || input_should_nav_repeat(kb, FlKey_Right, UI_NAV_REPEAT_DELAY, UI_NAV_REPEAT_RATE, (float)dt);

    // Update timing first (needed by mouse update)
    state->time += dt;
    state->delta_time = dt;

    for_count(i, FlKey_Count) {
        kb->keys_pressed[i] = false;
        kb->keys_released[i] = false;
    }

    kb->text_input = (FlString) { .data = nullptr, .length = 0, .is_static = 0 };

    for_count(i, FlKey_Count) {
        if (kb->keys[i]) {
            kb->key_hold_duration[i] += (float)dt;
        }
    }

    // Cache this frame's accumulated raw relative-motion delta before clearing the accumulator.
    state->mouse.raw_delta_x_cached = state->mouse.raw_delta_x;
    state->mouse.raw_delta_y_cached = state->mouse.raw_delta_y;

    state->mouse.wheel_x = 0.0f;
    state->mouse.wheel_y = 0.0f;
    state->mouse.delta_x = 0.0f;
    state->mouse.delta_y = 0.0f;
    state->mouse.raw_delta_x = 0.0f;
    state->mouse.raw_delta_y = 0.0f;

    fl_platform_input_update_mouse(state, dt);

    for_count(i, FlMouseButton_Count) {
        if (state->mouse.buttons[i]) {
            state->mouse.button_hold_duration[i] += (float)dt;
        }
        state->mouse.buttons_pressed[i] = false;
        state->mouse.buttons_released[i] = false;
    }

    for_count(gamepad_idx, UI_INPUT_MAX_GAMEPADS) {
        GamepadState* gamepad = &state->gamepads[gamepad_idx];
        for_count(i, FlGamepadButton_Count) {
            if (gamepad->buttons[i]) {
                gamepad->button_hold_duration[i] += (float)dt;
            }
            gamepad->buttons_pressed[i] = false;
            gamepad->buttons_released[i] = false;
        }

        for_count(i, FlGamepadAxis_Count) {
            gamepad->axes_prev[i] = gamepad->axes[i];
        }
    }

    state->window_events.focus_gained = false;
    state->window_events.focus_lost = false;
    state->window_events.resized = false;
    state->window_events.close_requested = false;
    state->window_events.dropped_file_count = 0;
}
