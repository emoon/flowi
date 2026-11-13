#pragma once

// Gamepad slot bookkeeping and SDL controller event translation.

#if BACKEND_USE_SDL

#include "../input_types.h"
#include <SDL.h>

typedef struct SdlGamepads {
    SDL_GameController* controllers[UI_INPUT_MAX_GAMEPADS];
    SDL_JoystickID ids[UI_INPUT_MAX_GAMEPADS];
    // Slots opened at init whose Connected event the next flush still owes.
    bool connect_pending[UI_INPUT_MAX_GAMEPADS];
} SdlGamepads;

// Zero the state and open any already-connected controllers.
void sdl_gamepads_init(SdlGamepads* self);

// Push the Connected events owed for controllers opened at init.
void sdl_gamepads_flush_pending(SdlGamepads* self, FlUiCtx* ctx);

// Translate an SDL controller event onto ctx. Events of any other type are ignored.
void sdl_gamepads_handle_event(SdlGamepads* self, FlUiCtx* ctx, const SDL_Event* e, u64 timestamp);

void sdl_gamepads_destroy(SdlGamepads* self);

#endif // BACKEND_USE_SDL
