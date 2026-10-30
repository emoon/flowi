#include "input_actions.h"
#include <core/core.h>
#include <core/log.h>
#include <core/memory.h>
#include "ui_private.h"
#include <math.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Default Bindings - Hardcoded sensible defaults that work out-of-the-box

#define BINDING_KEYBOARD(k) \
    { .type = INPUT_TYPE_KEYBOARD, .keyboard = { .key = k, .modifiers = UI_MOD_NONE } }
#define BINDING_KEYBOARD_MOD(k, m) \
    { .type = INPUT_TYPE_KEYBOARD, .keyboard = { .key = k, .modifiers = m } }
#define BINDING_MOUSE(btn) \
    { .type = INPUT_TYPE_MOUSE, .mouse = { .button = btn } }
#define BINDING_GAMEPAD(idx, btn) \
    { .type = INPUT_TYPE_GAMEPAD, .gamepad = { .gamepad_index = idx, .button = btn } }
#define BINDING_GAMEPAD_AXIS(idx, ax, thresh, pos)                                                 \
    {                                                                                              \
        .type = INPUT_TYPE_GAMEPAD_AXIS,                                                           \
        .gamepad_axis = { .gamepad_index = idx, .axis = ax, .threshold = thresh, .positive = pos } \
    }

// clang-format off
static const InputBinding s_bindings_confirm[] = {
    BINDING_KEYBOARD(FlKey_Enter),
    BINDING_KEYBOARD(FlKey_Space),
    BINDING_GAMEPAD(0, FlGamepadButton_A),
    // Note: Mouse clicks are handled separately via the hot/active system, not through actions
};

static const InputBinding s_bindings_cancel[] = {
    BINDING_KEYBOARD(FlKey_Escape),
    BINDING_GAMEPAD(0, FlGamepadButton_B),
};

static const InputBinding s_bindings_menu[] = {
    BINDING_KEYBOARD(FlKey_Escape),
    BINDING_GAMEPAD(0, FlGamepadButton_Start),
};

static const InputBinding s_bindings_nav_up[] = {
    BINDING_KEYBOARD(FlKey_Up),
    BINDING_GAMEPAD(0, FlGamepadButton_Dpad_Up),
    BINDING_GAMEPAD_AXIS(0, FlGamepadAxis_Left_Y, 0.5f, false), // Negative Y = up
};

static const InputBinding s_bindings_nav_down[] = {
    BINDING_KEYBOARD(FlKey_Down),
    BINDING_GAMEPAD(0, FlGamepadButton_Dpad_Down),
    BINDING_GAMEPAD_AXIS(0, FlGamepadAxis_Left_Y, 0.5f, true), // Positive Y = down
};

static const InputBinding s_bindings_nav_left[] = {
    BINDING_KEYBOARD(FlKey_Left),
    BINDING_GAMEPAD(0, FlGamepadButton_Dpad_Left),
    BINDING_GAMEPAD_AXIS(0, FlGamepadAxis_Left_X, 0.5f, false), // Negative X = left
};

static const InputBinding s_bindings_nav_right[] = {
    BINDING_KEYBOARD(FlKey_Right),
    BINDING_GAMEPAD(0, FlGamepadButton_Dpad_Right),
    BINDING_GAMEPAD_AXIS(0, FlGamepadAxis_Left_X, 0.5f, true), // Positive X = right
};
// clang-format on

static const InputBinding s_bindings_nav_next[] = {
    BINDING_KEYBOARD(FlKey_Tab),
    BINDING_GAMEPAD(0, FlGamepadButton_Right_Bumper),
};

static const InputBinding s_bindings_nav_prev[] = {
    BINDING_KEYBOARD_MOD(FlKey_Tab, UI_MOD_SHIFT),
    BINDING_GAMEPAD(0, FlGamepadButton_Left_Bumper),
};

static const InputBinding s_bindings_increment[] = {
    BINDING_KEYBOARD(FlKey_Right),
    BINDING_KEYBOARD(FlKey_Equal), // '+' key (usually)
};

static const InputBinding s_bindings_decrement[] = {
    BINDING_KEYBOARD(FlKey_Left),
    BINDING_KEYBOARD(FlKey_Minus), // '-' key
};

static const InputBinding s_bindings_toggle[] = {
    BINDING_KEYBOARD(FlKey_Space),
    BINDING_GAMEPAD(0, FlGamepadButton_A),
};

static const InputBinding s_bindings_text_backspace[] = {
    BINDING_KEYBOARD(FlKey_Backspace),
};

static const InputBinding s_bindings_text_delete[] = {
    BINDING_KEYBOARD(FlKey_Delete),
};

static const InputBinding s_bindings_text_home[] = {
    BINDING_KEYBOARD(FlKey_Home),
};

static const InputBinding s_bindings_text_end[] = {
    BINDING_KEYBOARD(FlKey_End),
};

static const InputBinding s_bindings_text_move_left[] = {
    BINDING_KEYBOARD(FlKey_Left),
};

static const InputBinding s_bindings_text_move_right[] = {
    BINDING_KEYBOARD(FlKey_Right),
};

static const InputBinding s_bindings_text_move_word_left[] = {
    BINDING_KEYBOARD_MOD(FlKey_Left, UI_MOD_CTRL),
};

static const InputBinding s_bindings_text_move_word_right[] = {
    BINDING_KEYBOARD_MOD(FlKey_Right, UI_MOD_CTRL),
};

static const InputBinding s_bindings_text_select_all[] = {
    BINDING_KEYBOARD_MOD(FlKey_A, UI_MOD_CTRL),  // Windows/Linux
    BINDING_KEYBOARD_MOD(FlKey_A, UI_MOD_SUPER), // macOS
};

static const InputBinding s_bindings_text_copy[] = {
    BINDING_KEYBOARD_MOD(FlKey_C, UI_MOD_CTRL),  // Windows/Linux
    BINDING_KEYBOARD_MOD(FlKey_C, UI_MOD_SUPER), // macOS
};

static const InputBinding s_bindings_text_cut[] = {
    BINDING_KEYBOARD_MOD(FlKey_X, UI_MOD_CTRL),  // Windows/Linux
    BINDING_KEYBOARD_MOD(FlKey_X, UI_MOD_SUPER), // macOS
};

static const InputBinding s_bindings_text_paste[] = {
    BINDING_KEYBOARD_MOD(FlKey_V, UI_MOD_CTRL),  // Windows/Linux
    BINDING_KEYBOARD_MOD(FlKey_V, UI_MOD_SUPER), // macOS
};

static const InputBinding s_bindings_text_undo[] = {
    BINDING_KEYBOARD_MOD(FlKey_Z, UI_MOD_CTRL),  // Windows/Linux
    BINDING_KEYBOARD_MOD(FlKey_Z, UI_MOD_SUPER), // macOS
};

static const InputBinding s_bindings_text_redo[] = {
    BINDING_KEYBOARD_MOD(FlKey_Y, UI_MOD_CTRL),                 // Windows/Linux
    BINDING_KEYBOARD_MOD(FlKey_Z, UI_MOD_SUPER | UI_MOD_SHIFT), // macOS (Cmd+Shift+Z)
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct BindingSet {
    const InputBinding* bindings;
    u32 count;
} BindingSet;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const BindingSet s_default_bindings[FlAction_Count] = {
    [FlAction_None] = { nullptr, 0 },
    [FlAction_Confirm] = { s_bindings_confirm, sizeof_array(s_bindings_confirm) },
    [FlAction_Cancel] = { s_bindings_cancel, sizeof_array(s_bindings_cancel) },
    [FlAction_Menu] = { s_bindings_menu, sizeof_array(s_bindings_menu) },
    [FlAction_Nav_Up] = { s_bindings_nav_up, sizeof_array(s_bindings_nav_up) },
    [FlAction_Nav_Down] = { s_bindings_nav_down, sizeof_array(s_bindings_nav_down) },
    [FlAction_Nav_Left] = { s_bindings_nav_left, sizeof_array(s_bindings_nav_left) },
    [FlAction_Nav_Right] = { s_bindings_nav_right, sizeof_array(s_bindings_nav_right) },
    [FlAction_Nav_Next] = { s_bindings_nav_next, sizeof_array(s_bindings_nav_next) },
    [FlAction_Nav_Prev] = { s_bindings_nav_prev, sizeof_array(s_bindings_nav_prev) },
    [FlAction_Increment] = { s_bindings_increment, sizeof_array(s_bindings_increment) },
    [FlAction_Decrement] = { s_bindings_decrement, sizeof_array(s_bindings_decrement) },
    [FlAction_Toggle] = { s_bindings_toggle, sizeof_array(s_bindings_toggle) },
    // SCROLL_UP and SCROLL_DOWN are handled directly via mouse wheel in ui_actions_update
    [FlAction_Text_Backspace] = { s_bindings_text_backspace, sizeof_array(s_bindings_text_backspace) },
    [FlAction_Text_Delete] = { s_bindings_text_delete, sizeof_array(s_bindings_text_delete) },
    [FlAction_Text_Home] = { s_bindings_text_home, sizeof_array(s_bindings_text_home) },
    [FlAction_Text_End] = { s_bindings_text_end, sizeof_array(s_bindings_text_end) },
    [FlAction_Text_Move_Left] = { s_bindings_text_move_left, sizeof_array(s_bindings_text_move_left) },
    [FlAction_Text_Move_Right] = { s_bindings_text_move_right, sizeof_array(s_bindings_text_move_right) },
    [FlAction_Text_Move_Word_Left] = { s_bindings_text_move_word_left, sizeof_array(s_bindings_text_move_word_left) },
    [FlAction_Text_Move_Word_Right]
    = { s_bindings_text_move_word_right, sizeof_array(s_bindings_text_move_word_right) },
    [FlAction_Text_Select_All] = { s_bindings_text_select_all, sizeof_array(s_bindings_text_select_all) },
    [FlAction_Text_Copy] = { s_bindings_text_copy, sizeof_array(s_bindings_text_copy) },
    [FlAction_Text_Cut] = { s_bindings_text_cut, sizeof_array(s_bindings_text_cut) },
    [FlAction_Text_Paste] = { s_bindings_text_paste, sizeof_array(s_bindings_text_paste) },
    [FlAction_Text_Undo] = { s_bindings_text_undo, sizeof_array(s_bindings_text_undo) },
    [FlAction_Text_Redo] = { s_bindings_text_redo, sizeof_array(s_bindings_text_redo) },
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Binding check dispatch - unified function that routes to appropriate checker based on event type

typedef enum BindingEventType {
    BINDING_EVENT_HELD = 0,
    BINDING_EVENT_PRESSED = 1,
    BINDING_EVENT_RELEASED = 2,
} BindingEventType;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Exact match of the active keyboard modifiers against a binding's required modifiers.
// Only Shift/Ctrl/Alt/Super participate; all other modifier bits are ignored.
static bool keyboard_modifiers_match(u32 active_modifiers, UiModifiers required) {
    u32 expected_mods = 0;
    if (required & UI_MOD_SHIFT)
        expected_mods |= FlKeyModifier_Shift;
    if (required & UI_MOD_CTRL)
        expected_mods |= FlKeyModifier_Control;
    if (required & UI_MOD_ALT)
        expected_mods |= FlKeyModifier_Alt;
    if (required & UI_MOD_SUPER)
        expected_mods |= FlKeyModifier_Super;

    const u32 relevant_mask = FlKeyModifier_Shift | FlKeyModifier_Control | FlKeyModifier_Alt | FlKeyModifier_Super;
    return (active_modifiers & relevant_mask) == expected_mods;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool check_binding(const FlInputState* input, const InputBinding* binding, BindingEventType event_type) {
    switch (binding->type) {
        case INPUT_TYPE_KEYBOARD: {
            const KeyboardState* kb = &input->keyboard;
            FlKey key = binding->keyboard.key;

            const bool* key_states[3] = {
                kb->keys,         // BINDING_EVENT_HELD (0)
                kb->keys_pressed, // BINDING_EVENT_PRESSED (1)
                kb->keys_released // BINDING_EVENT_RELEASED (2)
            };

            // Modifiers must match exactly for pressed/held (release ignores modifiers)
            if (event_type != BINDING_EVENT_RELEASED
                && !keyboard_modifiers_match(kb->modifiers, binding->keyboard.modifiers)) {
                return false;
            }

            return key_states[event_type][key];
        }

        case INPUT_TYPE_MOUSE: {
            const bool* button_states[3] = {
                input->mouse.buttons,         // BINDING_EVENT_HELD (0)
                input->mouse.buttons_pressed, // BINDING_EVENT_PRESSED (1)
                input->mouse.buttons_released // BINDING_EVENT_RELEASED (2)
            };
            return button_states[event_type][binding->mouse.button];
        }

        case INPUT_TYPE_GAMEPAD: {
            u32 idx = binding->gamepad.gamepad_index;
            if (idx >= UI_INPUT_MAX_GAMEPADS)
                return false;

            const GamepadState* pad = &input->gamepads[idx];
            if (!pad->connected)
                return false;

            const bool* button_states[3] = {
                pad->buttons,         // BINDING_EVENT_HELD (0)
                pad->buttons_pressed, // BINDING_EVENT_PRESSED (1)
                pad->buttons_released // BINDING_EVENT_RELEASED (2)
            };
            return button_states[event_type][binding->gamepad.button];
        }

        case INPUT_TYPE_GAMEPAD_AXIS: {
            u32 idx = binding->gamepad_axis.gamepad_index;
            if (idx >= UI_INPUT_MAX_GAMEPADS)
                return false;

            const GamepadState* pad = &input->gamepads[idx];
            if (!pad->connected)
                return false;

            FlGamepadAxis axis = binding->gamepad_axis.axis;
            float threshold = binding->gamepad_axis.threshold;
            bool positive = binding->gamepad_axis.positive;
            float value = pad->axes[axis];
            float prev_value = pad->axes_prev[axis];

            if (positive) {
                if (event_type == BINDING_EVENT_PRESSED) {
                    return (prev_value <= threshold) && (value > threshold);
                } else if (event_type == BINDING_EVENT_HELD) {
                    return value > threshold;
                } else { // BINDING_EVENT_RELEASED
                    return (prev_value > threshold) && (value <= threshold);
                }
            } else {
                if (event_type == BINDING_EVENT_PRESSED) {
                    return (prev_value >= -threshold) && (value < -threshold);
                } else if (event_type == BINDING_EVENT_HELD) {
                    return value < -threshold;
                } else { // BINDING_EVENT_RELEASED
                    return (prev_value < -threshold) && (value >= -threshold);
                }
            }
        }
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Action Evaluation

static bool evaluate_action(const FlInputState* input, FlAction action, BindingEventType event_type) {
    const BindingSet* set = &s_default_bindings[action];

    for_count(i, set->count) {
        if (check_binding(input, &set->bindings[i], event_type)) {
            return true;
        }
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal API Implementation

void ui_actions_init(FlUiCtx* ctx) {
    ctx->action_state = arena_alloc(ctx->persistant, ActionSystemState);
    memory_zero(ctx->action_state, sizeof(ActionSystemState));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_actions_update(FlUiCtx* ctx) {
    ActionSystemState* state = ctx->action_state;
    if (!state) {
        log_error("Action system not initialized");
        return;
    }

    const FlInputState* input = &ctx->input_state;

    for_count(i, FlAction_Count) {
        state->actions[i].pressed = evaluate_action(input, (FlAction)i, BINDING_EVENT_PRESSED);
        state->actions[i].held = evaluate_action(input, (FlAction)i, BINDING_EVENT_HELD);
        state->actions[i].released = evaluate_action(input, (FlAction)i, BINDING_EVENT_RELEASED);
    }

    // Mouse wheel has no "held" state, just up/down events. Reads the per-frame cached value (set by
    // fl_platform_input_update before it calls this, and before the live accumulator is cleared), so the actions
    // observe the same values - including the Shift axis swap - as every other wheel consumer.
    state->actions[FlAction_Scroll_Up].pressed = (input->mouse.wheel_y_cached > 0.0f);
    state->actions[FlAction_Scroll_Up].held = false;
    state->actions[FlAction_Scroll_Up].released = false;

    state->actions[FlAction_Scroll_Down].pressed = (input->mouse.wheel_y_cached < 0.0f);
    state->actions[FlAction_Scroll_Down].held = false;
    state->actions[FlAction_Scroll_Down].released = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Public Query API Implementation

bool ui_action_pressed(FlUiCtx* ctx, FlAction action) {
    if (action >= FlAction_Count) {
        return false;
    }

    const ActionSystemState* state = ctx->action_state;
    if (!state) {
        return false;
    }

    return state->actions[action].pressed;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ui_action_repeated(FlUiCtx* ctx, FlAction action) {
    if (action >= FlAction_Count) {
        return false;
    }

    // Software repeat applies to held keyboard bindings only. Each binding's modifiers must match
    // exactly, so actions sharing a key but differing by modifier (e.g. Left vs Ctrl+Left) never
    // both fire on the same frame.
    const KeyboardState* kb = &ctx->input_state.keyboard;
    const BindingSet* set = &s_default_bindings[action];

    for_count(i, set->count) {
        const InputBinding* binding = &set->bindings[i];
        if (binding->type != INPUT_TYPE_KEYBOARD) {
            continue;
        }
        if (!keyboard_modifiers_match(kb->modifiers, binding->keyboard.modifiers)) {
            continue;
        }
        if (input_should_nav_repeat(kb, binding->keyboard.key, UI_NAV_REPEAT_DELAY, UI_NAV_REPEAT_RATE,
                                    ctx->delta_time)) {
            return true;
        }
    }

    return false;
}
