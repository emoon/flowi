#include "ui_input.h"
#include <platform/input_types.h>
#include <core/string.h>
#include <core/log.h>
#include <core/memory.h>
#include "input_actions.h"
#include "ui_private.h"
#include <math.h>
#include <string.h>

static void ui_input_state_reset(FlInputState* state) {
    memory_zero(state, sizeof(FlInputState));
}

void ui_input_state_init(FlInputState* state) {
    ui_input_state_reset(state);
    for_count(i, UI_INPUT_MAX_GAMEPADS) {
        state->gamepads[i].dead_zone = UI_INPUT_GAMEPAD_DEFAULT_DEAD_ZONE;
    }
}

void ui_input_set_mouse_position(FlUiCtx* ctx, float x, float y) {
    FlInputState* state = &ctx->input_state;
    state->mouse.x = x;
    state->mouse.y = y;
}

static bool is_mouse_pos_valid(float x, float y) {
    return x > UI_MOUSE_INVALID_POS && y > UI_MOUSE_INVALID_POS;
}

static float mouse_distance_sqr(float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    return dx * dx + dy * dy;
}

static void update_mouse_position(MouseState* mouse) {
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
}

static void update_mouse_stationary_timer(MouseState* mouse, double dt) {
    float delta_length_sqr = mouse->delta_x * mouse->delta_x + mouse->delta_y * mouse->delta_y;
    float threshold_sqr = UI_MOUSE_STATIONARY_THRESHOLD * UI_MOUSE_STATIONARY_THRESHOLD;
    bool mouse_stationary = delta_length_sqr <= threshold_sqr;

    if (mouse_stationary) {
        mouse->stationary_timer += (float)dt;
    } else {
        mouse->stationary_timer = 0.0f;
    }
}

static bool is_repeated_click(const FlInputState* state, u32 button) {
    const MouseState* mouse = &state->mouse;
    if (!mouse->position_valid
        || (state->time * 1000.0 - mouse->last_click_time[button]) >= (UI_MOUSE_DOUBLE_CLICK_TIME * 1000.0)) {
        return false;
    }

    float dist_sqr = mouse_distance_sqr(mouse->x, mouse->y, mouse->click_pos_x[button], mouse->click_pos_y[button]);
    return dist_sqr < UI_MOUSE_DOUBLE_CLICK_MAX_DIST * UI_MOUSE_DOUBLE_CLICK_MAX_DIST;
}

static void register_mouse_click(FlInputState* state, u32 button) {
    MouseState* mouse = &state->mouse;

    if (is_repeated_click(state, button)) {
        mouse->click_count[button]++;
    } else {
        mouse->click_count[button] = 1;
    }

    mouse->click_pos_x[button] = mouse->x;
    mouse->click_pos_y[button] = mouse->y;
    mouse->last_click_time[button] = (u64)(state->time * 1000.0);
    mouse->drag_max_distance_sqr[button] = 0.0f;
}

static void track_mouse_drag(MouseState* mouse, u32 button) {
    float drag_dist_sqr = mouse_distance_sqr(mouse->x, mouse->y, mouse->click_pos_x[button], mouse->click_pos_y[button]);
    if (drag_dist_sqr > mouse->drag_max_distance_sqr[button]) {
        mouse->drag_max_distance_sqr[button] = drag_dist_sqr;
    }
}

static void ui_input_update_mouse(FlInputState* state, double dt) {
    MouseState* mouse = &state->mouse;

    update_mouse_position(mouse);
    update_mouse_stationary_timer(mouse, dt);

    for_count(i, FlMouseButton_Count) {
        if (mouse->buttons_pressed[i]) {
            register_mouse_click(state, i);
        }

        if (mouse->buttons[i] && mouse->position_valid) {
            track_mouse_drag(mouse, i);
        }
    }

    mouse->prev_x = mouse->x;
    mouse->prev_y = mouse->y;
}

static void restore_focus_on_activation(FlUiCtx* ctx, const WindowEventState* window_events) {
    if (window_events->focus_lost) {
        if (ctx->focused_id != 0) {
            ctx->last_focused_id = ctx->focused_id;
        }
        ctx->focused_id = 0;
    }
    if (window_events->focus_gained) {
        ctx->focused_id = ctx->last_focused_id;
    }
}

static void snapshot_pressed_released(FlUiCtx* ctx, const FlInputState* state) {
    const KeyboardState* kb = &state->keyboard;

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
}

// This-frame typed-text snapshot, taken before the raw kb->text_input is cleared.
static void snapshot_text_input(FlUiCtx* ctx, const KeyboardState* kb) {
    u64 text_len = kb->text_input.length;
    if (text_len > 0) {
        memcpy(ctx->text_input_buffer, kb->text_input.data, text_len);
    }
    ctx->text_input_this_frame = (FlString) { .data = ctx->text_input_buffer, .length = text_len, .is_static = 0 };
}

// This-frame dropped-file snapshot: paths packed NUL-separated into one buffer; a path that would overflow it
// ends the snapshot.
static void snapshot_dropped_files(FlUiCtx* ctx, const WindowEventState* window_events) {
    ctx->dropped_file_count_this_frame = 0;
    u64 path_bytes = 0;
    for_count(i, window_events->dropped_file_count) {
        FlString path = window_events->dropped_files[i];
        if (path.length + 1 > UI_DROPPED_PATH_BYTES - path_bytes) {
            log_warning("Dropped-file path buffer full, ignoring the rest of this frame's drop");
            break;
        }
        char* slot = ctx->dropped_file_buffer + path_bytes;
        memcpy(slot, path.data, path.length);
        slot[path.length] = '\0';
        ctx->dropped_files_this_frame[i] = (FlString) { .data = slot, .length = path.length, .is_static = 0 };
        ctx->dropped_file_positions_this_frame[i] = window_events->dropped_file_positions[i];
        path_bytes += path.length + 1;
        ctx->dropped_file_count_this_frame++;
    }
}

// Shift held swaps the wheel axes for this frame's accumulated values, then both are cached before the live
// accumulators are cleared.
static void cache_wheel(MouseState* mouse, const KeyboardState* kb) {
    mouse->wheel_axis_swap_requested = (kb->modifiers & FlKeyModifier_Shift) != 0;
    if (mouse->wheel_axis_swap_requested) {
        float temp = mouse->wheel_x;
        mouse->wheel_x = mouse->wheel_y;
        mouse->wheel_y = temp;
    }

    mouse->wheel_x_cached = mouse->wheel_x;
    mouse->wheel_y_cached = mouse->wheel_y;
}

static bool nav_repeat_pressed(FlUiCtx* ctx, FlAction action, const KeyboardState* kb, FlKey key, float dt) {
    return ui_action_pressed(ctx, action) || input_should_nav_repeat(kb, key, UI_NAV_REPEAT_DELAY, UI_NAV_REPEAT_RATE, dt);
}

static void update_nav(FlUiCtx* ctx, const KeyboardState* kb, float dt) {
    ctx->nav_next_pressed = ui_action_pressed(ctx, FlAction_Nav_Next);
    ctx->nav_prev_pressed = ui_action_pressed(ctx, FlAction_Nav_Prev);
    ctx->nav_activate_pressed = ui_action_pressed(ctx, FlAction_Confirm);

    // Software repeat: trigger on initial press or at intervals after delay
    ctx->nav_frame.focused_id = ctx->focused_id;
    ctx->nav_frame.nav_up = nav_repeat_pressed(ctx, FlAction_Nav_Up, kb, FlKey_Up, dt);
    ctx->nav_frame.nav_down = nav_repeat_pressed(ctx, FlAction_Nav_Down, kb, FlKey_Down, dt);
    ctx->nav_frame.nav_left = nav_repeat_pressed(ctx, FlAction_Nav_Left, kb, FlKey_Left, dt);
    ctx->nav_frame.nav_right = nav_repeat_pressed(ctx, FlAction_Nav_Right, kb, FlKey_Right, dt);
}

static void advance_keyboard(KeyboardState* kb, double dt) {
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
}

static void clear_mouse_accumulators(MouseState* mouse) {
    // Cache this frame's accumulated raw relative-motion delta before clearing the accumulator.
    mouse->raw_delta_x_cached = mouse->raw_delta_x;
    mouse->raw_delta_y_cached = mouse->raw_delta_y;

    mouse->wheel_x = 0.0f;
    mouse->wheel_y = 0.0f;
    mouse->delta_x = 0.0f;
    mouse->delta_y = 0.0f;
    mouse->raw_delta_x = 0.0f;
    mouse->raw_delta_y = 0.0f;
}

static void advance_mouse_buttons(MouseState* mouse, double dt) {
    for_count(i, FlMouseButton_Count) {
        if (mouse->buttons[i]) {
            mouse->button_hold_duration[i] += (float)dt;
        }
        mouse->buttons_pressed[i] = false;
        mouse->buttons_released[i] = false;
    }
}

static void advance_gamepads(FlInputState* state, double dt) {
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
}

static void clear_window_events(WindowEventState* window_events) {
    window_events->focus_gained = false;
    window_events->focus_lost = false;
    window_events->resized = false;
    window_events->close_requested = false;
    window_events->dropped_file_count = 0;
}

void ui_input_update(FlUiCtx* ctx, double dt) {
    FlInputState* state = &ctx->input_state;
    KeyboardState* kb = &state->keyboard;

    // Window-activation keyboard focus: focus-lost stashes and clears the live focus; focus-gained restores the
    // stashed widget.
    restore_focus_on_activation(ctx, &state->window_events);

    ctx->delta_time = (float)dt;

    snapshot_pressed_released(ctx, state);
    snapshot_text_input(ctx, kb);
    snapshot_dropped_files(ctx, &state->window_events);
    cache_wheel(&state->mouse, kb);

    // Evaluate actions before the flags are cleared.
    ui_actions_update(ctx);
    update_nav(ctx, kb, (float)dt);

    // Update timing first (needed by mouse update)
    state->time += dt;
    state->delta_time = dt;

    advance_keyboard(kb, dt);
    clear_mouse_accumulators(&state->mouse);
    ui_input_update_mouse(state, dt);
    advance_mouse_buttons(&state->mouse, dt);
    advance_gamepads(state, dt);
    clear_window_events(&state->window_events);
}
