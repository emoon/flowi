#pragma once

// Gamepad slot bookkeeping and SDL controller event translation.

#if BACKEND_USE_SDL

#include "../input_types.h"
#include <SDL.h>
#include <flowi/app/sdl_pump.h>

// Where gamepad events go instead of the context's gamepad slots. A null fn means the context.
typedef struct SdlPadSink {
    FlSdlPadFn fn;
    void* user_data;
} SdlPadSink;

typedef struct SdlGamepads {
    SdlPadSink raw_sink;
    SDL_GameController* controllers[UI_INPUT_MAX_GAMEPADS];
    SDL_JoystickID ids[UI_INPUT_MAX_GAMEPADS];
    // Slots opened at init whose Connected event the next flush still owes.
    bool connect_pending[UI_INPUT_MAX_GAMEPADS];
} SdlGamepads;

// Zero the state and open any already-connected controllers. With a raw_sink, events go to it with the pad's
// identity and nothing reaches the context's gamepad slots.
void sdl_gamepads_init(SdlGamepads* self, SdlPadSink raw_sink);

// Push the Connected events owed for controllers opened at init.
void sdl_gamepads_flush_pending(SdlGamepads* self, FlUiCtx* ctx);

// Translate an SDL controller event onto ctx. Events of any other type are ignored.
void sdl_gamepads_handle_event(SdlGamepads* self, FlUiCtx* ctx, const SDL_Event* e, u64 timestamp);

void sdl_gamepads_destroy(SdlGamepads* self);

#endif // BACKEND_USE_SDL
