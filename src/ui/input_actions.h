#pragma once

#include <platform/input_types.h>
#include <core/types.h>
#include <flowi/ui/action.h> // @generated FlAction (api_gen)
#include <flowi/ui/key.h>    // @generated FlKey (api_gen)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// UI Actions - High-level input actions for UI navigation and interaction
//
// Decouples physical inputs (keyboard keys, gamepad buttons) from logical actions; several physical inputs can
// trigger the same action. Bindings are hardcoded defaults, no remapping.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef enum InputType {
    INPUT_TYPE_KEYBOARD,
    INPUT_TYPE_MOUSE,
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
            FlMouseButton button;
        } mouse;

        struct {
            u32 gamepad_index; // 0-3
            FlGamepadButton button;
        } gamepad;

        struct {
            u32 gamepad_index;
            FlGamepadAxis axis;
            float threshold; // Dead zone (default 0.5)
            bool positive;   // True = positive direction, false = negative
        } gamepad_axis;
    };
} InputBinding;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Action System State - Per-frame state for each action

typedef struct ActionState {
    bool pressed;  // Just went down this frame
    bool held;     // Is currently down
    bool released; // Just went up this frame
} ActionState;

typedef struct ActionSystemState {
    ActionState actions[FlAction_Count];
} ActionSystemState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Query API

typedef struct FlUiCtx FlUiCtx;

// Action went down this frame.
bool ui_action_pressed(FlUiCtx* ctx, FlAction action);

// Whether a held action should fire a software repeat this frame.
// Backed by interval-based key-repeat timing; keyboard bindings only, with exact modifier match.
bool ui_action_repeated(FlUiCtx* ctx, FlAction action);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal API (used by UI system)

// Called during UI context creation.
void ui_actions_init(FlUiCtx* ctx);

// Called at the start of fl_platform_input_update.
void ui_actions_update(FlUiCtx* ctx);
