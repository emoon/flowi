#pragma once

#include <platform/input_types.h>
#include <core/types.h>
#include <flowi/ui/action.h> // @generated FlAction (api_gen)
#include <flowi/ui/key.h>    // @generated FlKey (api_gen)

// High-level input actions for UI navigation and interaction.
//
// Logical actions are decoupled from physical inputs (keyboard keys, gamepad buttons), and several physical inputs
// can trigger the same action. Bindings are hardcoded defaults in input_actions.c and cannot be remapped.

typedef struct FlUiCtx FlUiCtx;
typedef struct ActionSystemState ActionSystemState;

// Action went down this frame.
bool ui_action_pressed(FlUiCtx* ctx, FlAction action);

// Whether a held action should fire a software repeat this frame.
// Backed by interval-based key-repeat timing; keyboard bindings only, with exact modifier match.
bool ui_action_repeated(FlUiCtx* ctx, FlAction action);

// Action went down this frame or fires a software repeat.
bool ui_action_pressed_or_repeated(FlUiCtx* ctx, FlAction action);

// Called during UI context creation.
void ui_actions_init(FlUiCtx* ctx);

// Called at the start of ui_input_update.
void ui_actions_update(FlUiCtx* ctx);
