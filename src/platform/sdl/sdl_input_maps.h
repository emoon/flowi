#pragma once

// Pure SDL -> flowi enum mappings used by the SDL input translation.

#if BACKEND_USE_SDL

#include "../input_types.h"
#include <SDL.h>

FlKey sdl_key_to_fl_key(SDL_Keycode sdl_key);
u32 sdl_keymod_to_fl_keymod(u16 sdl_mod);
// FlMouseButton_Count marks a button flowi has no mapping for.
FlMouseButton sdl_mouse_button_to_ui_button(u8 sdl_button);
// Unmapped buttons / axes fall back to the first enumerator.
FlGamepadButton sdl_gamepad_button_to_ui_button(SDL_GameControllerButton sdl_button);
FlGamepadAxis sdl_gamepad_axis_to_fl_axis(SDL_GameControllerAxis sdl_axis);
SDL_SystemCursor sdl_system_cursor_for_shape(FlCursorShape shape);

#endif // BACKEND_USE_SDL
