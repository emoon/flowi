#include "input_actions.h"
#include <core/core.h>
#include <core/log.h>
#include <core/memory.h>
#include "ui_private.h"
#include <math.h>
#include <string.h>

typedef enum InputType {
    INPUT_TYPE_KEYBOARD,
    INPUT_TYPE_GAMEPAD,
    INPUT_TYPE_GAMEPAD_AXIS,
} InputType;

typedef enum UiModifiers {
    UI_MOD_NONE = 0,
    UI_MOD_SHIFT = (1 << 0),
    UI_MOD_CTRL = (1 << 1),
    UI_MOD_ALT = (1 << 2),
    UI_MOD_SUPER = (1 << 3), // Command key on macOS, Windows key on Windows
} UiModifiers;

// One physical input source that can trigger an action
typedef struct InputBinding {
    InputType type;

    union {
        struct {
            FlKey key;
            UiModifiers modifiers;
        } keyboard;

        struct {
            u32 gamepad_index; // 0-3
            FlGamepadButton button;
        } gamepad;

        struct {
            u32 gamepad_index;
            FlGamepadAxis axis;
            float threshold; // Dead zone (default 0.5)
            bool positive;
        } gamepad_axis;
    };
} InputBinding;

// Which actions went down this frame.
struct ActionSystemState {
    bool pressed[FlAction_Count];
};

// Default bindings (hardcoded).

#define BINDING_KEYBOARD(k) \
    { .type = INPUT_TYPE_KEYBOARD, .keyboard = { .key = k, .modifiers = UI_MOD_NONE } }
#define BINDING_KEYBOARD_MOD(k, m) \
    { .type = INPUT_TYPE_KEYBOARD, .keyboard = { .key = k, .modifiers = m } }
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
    // Mouse clicks go through the hot/active system, not through actions.
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

// The cursor actions also fire with Shift held: modifiers match exactly, and the text input extends the
// selection when a cursor action fires under Shift.
static const InputBinding s_bindings_text_home[] = {
    BINDING_KEYBOARD(FlKey_Home),
    BINDING_KEYBOARD_MOD(FlKey_Home, UI_MOD_SHIFT),
};

static const InputBinding s_bindings_text_end[] = {
    BINDING_KEYBOARD(FlKey_End),
    BINDING_KEYBOARD_MOD(FlKey_End, UI_MOD_SHIFT),
};

static const InputBinding s_bindings_text_move_left[] = {
    BINDING_KEYBOARD(FlKey_Left),
    BINDING_KEYBOARD_MOD(FlKey_Left, UI_MOD_SHIFT),
};

static const InputBinding s_bindings_text_move_right[] = {
    BINDING_KEYBOARD(FlKey_Right),
    BINDING_KEYBOARD_MOD(FlKey_Right, UI_MOD_SHIFT),
};

static const InputBinding s_bindings_text_move_word_left[] = {
    BINDING_KEYBOARD_MOD(FlKey_Left, UI_MOD_CTRL),
    BINDING_KEYBOARD_MOD(FlKey_Left, UI_MOD_CTRL | UI_MOD_SHIFT),
};

static const InputBinding s_bindings_text_move_word_right[] = {
    BINDING_KEYBOARD_MOD(FlKey_Right, UI_MOD_CTRL),
    BINDING_KEYBOARD_MOD(FlKey_Right, UI_MOD_CTRL | UI_MOD_SHIFT),
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

typedef struct BindingSet {
    const InputBinding* bindings;
    u32 count;
} BindingSet;

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
    // Scroll_Up and Scroll_Down have no bindings; ui_actions_update reads the mouse wheel.
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

static bool key_pressed(const KeyboardState* kb, const InputBinding* binding) {
    return keyboard_modifiers_match(kb->modifiers, binding->keyboard.modifiers)
           && kb->keys_pressed[binding->keyboard.key];
}

// The gamepad in a slot, or nullptr when the slot is out of range or empty.
static const GamepadState* connected_gamepad(const FlInputState* input, u32 index) {
    if (index >= UI_INPUT_MAX_GAMEPADS || !input->gamepads[index].connected) {
        return nullptr;
    }
    return &input->gamepads[index];
}

static bool gamepad_button_pressed(const FlInputState* input, const InputBinding* binding) {
    const GamepadState* pad = connected_gamepad(input, binding->gamepad.gamepad_index);
    return pad && pad->buttons_pressed[binding->gamepad.button];
}

// An axis counts as pressed on the frame it crosses the threshold in the binding's direction.
static bool gamepad_axis_pressed(const FlInputState* input, const InputBinding* binding) {
    const GamepadState* pad = connected_gamepad(input, binding->gamepad_axis.gamepad_index);
    if (!pad) {
        return false;
    }

    const float threshold = binding->gamepad_axis.threshold;
    const float value = pad->axes[binding->gamepad_axis.axis];
    const float prev_value = pad->axes_prev[binding->gamepad_axis.axis];

    if (binding->gamepad_axis.positive) {
        return (prev_value <= threshold) && (value > threshold);
    }
    return (prev_value >= -threshold) && (value < -threshold);
}

static bool binding_pressed(const FlInputState* input, const InputBinding* binding) {
    switch (binding->type) {
        case INPUT_TYPE_KEYBOARD:
            return key_pressed(&input->keyboard, binding);
        case INPUT_TYPE_GAMEPAD:
            return gamepad_button_pressed(input, binding);
        case INPUT_TYPE_GAMEPAD_AXIS:
            return gamepad_axis_pressed(input, binding);
    }
    return false;
}

static bool action_pressed(const FlInputState* input, FlAction action) {
    const BindingSet* set = &s_default_bindings[action];

    for_count(i, set->count) {
        if (binding_pressed(input, &set->bindings[i])) {
            return true;
        }
    }

    return false;
}

void ui_actions_init(FlUiCtx* ctx) {
    ctx->action_state = arena_alloc(ctx->persistant, ActionSystemState);
    memory_zero(ctx->action_state, sizeof(ActionSystemState));
}

void ui_actions_update(FlUiCtx* ctx) {
    ActionSystemState* state = ctx->action_state;
    if (!state) {
        log_error("Action system not initialized");
        return;
    }

    const FlInputState* input = &ctx->input_state;

    for_count(i, FlAction_Count) {
        state->pressed[i] = action_pressed(input, (FlAction)i);
    }

    // The mouse wheel has no bindings. Reads the per-frame cached value (set by ui_input_update before it calls
    // this, and before the live accumulator is cleared), so the actions observe the same values (including the
    // Shift axis swap) as every other wheel consumer.
    state->pressed[FlAction_Scroll_Up] = (input->mouse.wheel_y_cached > 0.0f);
    state->pressed[FlAction_Scroll_Down] = (input->mouse.wheel_y_cached < 0.0f);
}

bool ui_action_pressed(FlUiCtx* ctx, FlAction action) {
    if (action >= FlAction_Count) {
        return false;
    }

    const ActionSystemState* state = ctx->action_state;
    if (!state) {
        return false;
    }

    return state->pressed[action];
}

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

bool ui_action_pressed_or_repeated(FlUiCtx* ctx, FlAction action) {
    return ui_action_pressed(ctx, action) || ui_action_repeated(ctx, action);
}
