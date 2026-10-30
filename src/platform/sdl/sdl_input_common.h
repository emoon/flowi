#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared SDL event translation + input-device state: the SDL->Fl event pump (keyboard, text, mouse, wheel,
// gamepad, window events) and the per-window OS mechanics (system-cursor cache, relative-mouse mode, warp,
// clipboard) the SDL window backend delegates to.

#if BACKEND_USE_SDL

#include "../input_types.h"
#include <SDL.h>
#include <ui/ui_private.h>

typedef struct SdlInputCommon {
    SDL_GameController* gamepads[UI_INPUT_MAX_GAMEPADS];
    SDL_JoystickID gamepad_ids[UI_INPUT_MAX_GAMEPADS];
    // Slots opened at init whose Connected event the next pump still owes the main window.
    bool gamepad_connect_pending[UI_INPUT_MAX_GAMEPADS];

    // Relative-mouse mode (a hidden drag). While true, motion is delivered as raw FlMouseEvent_Relative_Motion
    // deltas rather than absolute FlMouseEvent_Move, and SDL has hidden + unbounded the cursor.
    bool relative_mouse_mode;

    // System cursors, created lazily and cached by shape.
    SDL_Cursor* system_cursors[FlCursorShape_Count];
} SdlInputCommon;

// Zero the device state and open any already-connected controllers. The caller is responsible for having
// brought up the SDL event + game-controller subsystems.
void sdl_input_common_init(SdlInputCommon* self);

// Multi-window pump: drain the shared SDL queue ONCE and demux each window-addressed event to its window's context
// by SDL windowID, window_ids[i] pairing with contexts[i]. Non-window-addressed events (SDL_QUIT, gamepad) and any
// id with no live match route to the main window (contexts[0]); callers always pass the main window as index 0.
void sdl_input_common_pump_multi(SdlInputCommon* self, const u32* window_ids, FlUiCtx** contexts, int count);

// Per-frame cursor + hidden-drag mechanics, flushed by the host each frame.
void sdl_input_common_set_cursor(SdlInputCommon* self, FlCursor cursor);
void sdl_input_common_set_cursor_visible(bool visible);
void sdl_input_common_set_relative_mouse_mode(SdlInputCommon* self, bool enabled);
// Warp the OS cursor to (x, y) in the given window's coordinates; a null window targets the mouse-focus window.
void sdl_input_common_warp_mouse(SDL_Window* window, float x, float y);

// Close gamepad handles and free the cached cursors.
void sdl_input_common_destroy(SdlInputCommon* self);

#endif // BACKEND_USE_SDL
