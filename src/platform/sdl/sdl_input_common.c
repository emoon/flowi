// Shared SDL event translation + input-device state (see sdl_input_common.h).

#include "sdl_input_common.h"
#include "../clipboard.h"

#if BACKEND_USE_SDL

#include "../input_types.h"
#include <core/arena.h>
#include <core/log.h>
#include <core/memory.h>
#include <core/os/os.h>
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlKey sdl_key_to_fl_key(SDL_Keycode sdl_key) {
    switch (sdl_key) {
        case SDLK_a:
            return FlKey_A;
        case SDLK_b:
            return FlKey_B;
        case SDLK_c:
            return FlKey_C;
        case SDLK_d:
            return FlKey_D;
        case SDLK_e:
            return FlKey_E;
        case SDLK_f:
            return FlKey_F;
        case SDLK_g:
            return FlKey_G;
        case SDLK_h:
            return FlKey_H;
        case SDLK_i:
            return FlKey_I;
        case SDLK_j:
            return FlKey_J;
        case SDLK_k:
            return FlKey_K;
        case SDLK_l:
            return FlKey_L;
        case SDLK_m:
            return FlKey_M;
        case SDLK_n:
            return FlKey_N;
        case SDLK_o:
            return FlKey_O;
        case SDLK_p:
            return FlKey_P;
        case SDLK_q:
            return FlKey_Q;
        case SDLK_r:
            return FlKey_R;
        case SDLK_s:
            return FlKey_S;
        case SDLK_t:
            return FlKey_T;
        case SDLK_u:
            return FlKey_U;
        case SDLK_v:
            return FlKey_V;
        case SDLK_w:
            return FlKey_W;
        case SDLK_x:
            return FlKey_X;
        case SDLK_y:
            return FlKey_Y;
        case SDLK_z:
            return FlKey_Z;
        case SDLK_0:
            return FlKey_Num0;
        case SDLK_1:
            return FlKey_Num1;
        case SDLK_2:
            return FlKey_Num2;
        case SDLK_3:
            return FlKey_Num3;
        case SDLK_4:
            return FlKey_Num4;
        case SDLK_5:
            return FlKey_Num5;
        case SDLK_6:
            return FlKey_Num6;
        case SDLK_7:
            return FlKey_Num7;
        case SDLK_8:
            return FlKey_Num8;
        case SDLK_9:
            return FlKey_Num9;
        case SDLK_SPACE:
            return FlKey_Space;
        case SDLK_QUOTE:
            return FlKey_Apostrophe;
        case SDLK_COMMA:
            return FlKey_Comma;
        case SDLK_MINUS:
            return FlKey_Minus;
        case SDLK_PERIOD:
            return FlKey_Period;
        case SDLK_SLASH:
            return FlKey_Slash;
        case SDLK_SEMICOLON:
            return FlKey_Semicolon;
        case SDLK_EQUALS:
            return FlKey_Equal;
        case SDLK_LEFTBRACKET:
            return FlKey_Left_Bracket;
        case SDLK_BACKSLASH:
            return FlKey_Backslash;
        case SDLK_RIGHTBRACKET:
            return FlKey_Right_Bracket;
        case SDLK_BACKQUOTE:
            return FlKey_Grave_Accent;
        case SDLK_ESCAPE:
            return FlKey_Escape;
        case SDLK_RETURN:
            return FlKey_Enter;
        case SDLK_TAB:
            return FlKey_Tab;
        case SDLK_BACKSPACE:
            return FlKey_Backspace;
        case SDLK_INSERT:
            return FlKey_Insert;
        case SDLK_DELETE:
            return FlKey_Delete;
        case SDLK_RIGHT:
            return FlKey_Right;
        case SDLK_LEFT:
            return FlKey_Left;
        case SDLK_DOWN:
            return FlKey_Down;
        case SDLK_UP:
            return FlKey_Up;
        case SDLK_PAGEUP:
            return FlKey_Page_Up;
        case SDLK_PAGEDOWN:
            return FlKey_Page_Down;
        case SDLK_HOME:
            return FlKey_Home;
        case SDLK_END:
            return FlKey_End;
        case SDLK_CAPSLOCK:
            return FlKey_Caps_Lock;
        case SDLK_SCROLLLOCK:
            return FlKey_Scroll_Lock;
        case SDLK_NUMLOCKCLEAR:
            return FlKey_Num_Lock;
        case SDLK_PRINTSCREEN:
            return FlKey_Print_Screen;
        case SDLK_PAUSE:
            return FlKey_Pause;
        case SDLK_F1:
            return FlKey_F1;
        case SDLK_F2:
            return FlKey_F2;
        case SDLK_F3:
            return FlKey_F3;
        case SDLK_F4:
            return FlKey_F4;
        case SDLK_F5:
            return FlKey_F5;
        case SDLK_F6:
            return FlKey_F6;
        case SDLK_F7:
            return FlKey_F7;
        case SDLK_F8:
            return FlKey_F8;
        case SDLK_F9:
            return FlKey_F9;
        case SDLK_F10:
            return FlKey_F10;
        case SDLK_F11:
            return FlKey_F11;
        case SDLK_F12:
            return FlKey_F12;
        case SDLK_KP_0:
            return FlKey_Kp_0;
        case SDLK_KP_1:
            return FlKey_Kp_1;
        case SDLK_KP_2:
            return FlKey_Kp_2;
        case SDLK_KP_3:
            return FlKey_Kp_3;
        case SDLK_KP_4:
            return FlKey_Kp_4;
        case SDLK_KP_5:
            return FlKey_Kp_5;
        case SDLK_KP_6:
            return FlKey_Kp_6;
        case SDLK_KP_7:
            return FlKey_Kp_7;
        case SDLK_KP_8:
            return FlKey_Kp_8;
        case SDLK_KP_9:
            return FlKey_Kp_9;
        case SDLK_KP_DECIMAL:
            return FlKey_Kp_Decimal;
        case SDLK_KP_DIVIDE:
            return FlKey_Kp_Divide;
        case SDLK_KP_MULTIPLY:
            return FlKey_Kp_Multiply;
        case SDLK_KP_MINUS:
            return FlKey_Kp_Subtract;
        case SDLK_KP_PLUS:
            return FlKey_Kp_Add;
        case SDLK_KP_ENTER:
            return FlKey_Kp_Enter;
        case SDLK_KP_EQUALS:
            return FlKey_Kp_Equal;
        case SDLK_LSHIFT:
            return FlKey_Left_Shift;
        case SDLK_LCTRL:
            return FlKey_Left_Control;
        case SDLK_LALT:
            return FlKey_Left_Alt;
        case SDLK_LGUI:
            return FlKey_Left_Super;
        case SDLK_RSHIFT:
            return FlKey_Right_Shift;
        case SDLK_RCTRL:
            return FlKey_Right_Control;
        case SDLK_RALT:
            return FlKey_Right_Alt;
        case SDLK_RGUI:
            return FlKey_Right_Super;
        case SDLK_MENU:
            return FlKey_Menu;
        default:
            return FlKey_Unknown;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static u32 sdl_keymod_to_fl_keymod(u16 sdl_mod) {
    u32 modifiers = 0;
    if (sdl_mod & KMOD_SHIFT)
        modifiers |= FlKeyModifier_Shift;
    if (sdl_mod & KMOD_CTRL)
        modifiers |= FlKeyModifier_Control;
    if (sdl_mod & KMOD_ALT)
        modifiers |= FlKeyModifier_Alt;
    if (sdl_mod & KMOD_GUI)
        modifiers |= FlKeyModifier_Super;
    if (sdl_mod & KMOD_CAPS)
        modifiers |= FlKeyModifier_Caps_Lock;
    if (sdl_mod & KMOD_NUM)
        modifiers |= FlKeyModifier_Num_Lock;
    return modifiers;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// FlMouseButton_Count marks a button flowi has no mapping for.
static FlMouseButton sdl_mouse_button_to_ui_button(u8 sdl_button) {
    switch (sdl_button) {
        case SDL_BUTTON_LEFT:
            return FlMouseButton_Left;
        case SDL_BUTTON_RIGHT:
            return FlMouseButton_Right;
        case SDL_BUTTON_MIDDLE:
            return FlMouseButton_Middle;
        case SDL_BUTTON_X1:
            return FlMouseButton_X1;
        case SDL_BUTTON_X2:
            return FlMouseButton_X2;
        default:
            return FlMouseButton_Count;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlGamepadButton sdl_gamepad_button_to_ui_button(SDL_GameControllerButton sdl_button) {
    switch (sdl_button) {
        case SDL_CONTROLLER_BUTTON_A:
            return FlGamepadButton_A;
        case SDL_CONTROLLER_BUTTON_B:
            return FlGamepadButton_B;
        case SDL_CONTROLLER_BUTTON_X:
            return FlGamepadButton_X;
        case SDL_CONTROLLER_BUTTON_Y:
            return FlGamepadButton_Y;
        case SDL_CONTROLLER_BUTTON_BACK:
            return FlGamepadButton_Back;
        case SDL_CONTROLLER_BUTTON_GUIDE:
            return FlGamepadButton_Guide;
        case SDL_CONTROLLER_BUTTON_START:
            return FlGamepadButton_Start;
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:
            return FlGamepadButton_Left_Thumb;
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
            return FlGamepadButton_Right_Thumb;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            return FlGamepadButton_Left_Bumper;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            return FlGamepadButton_Right_Bumper;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            return FlGamepadButton_Dpad_Up;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            return FlGamepadButton_Dpad_Down;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            return FlGamepadButton_Dpad_Left;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            return FlGamepadButton_Dpad_Right;
        default:
            return FlGamepadButton_A;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlGamepadAxis sdl_gamepad_axis_to_fl_axis(SDL_GameControllerAxis sdl_axis) {
    switch (sdl_axis) {
        case SDL_CONTROLLER_AXIS_LEFTX:
            return FlGamepadAxis_Left_X;
        case SDL_CONTROLLER_AXIS_LEFTY:
            return FlGamepadAxis_Left_Y;
        case SDL_CONTROLLER_AXIS_RIGHTX:
            return FlGamepadAxis_Right_X;
        case SDL_CONTROLLER_AXIS_RIGHTY:
            return FlGamepadAxis_Right_Y;
        case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
            return FlGamepadAxis_Left_Trigger;
        case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
            return FlGamepadAxis_Right_Trigger;
        default:
            return FlGamepadAxis_Left_X;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int find_gamepad_slot(SdlInputCommon* self, SDL_JoystickID id) {
    for (int i = 0; i < UI_INPUT_MAX_GAMEPADS; i++) {
        if (self->gamepad_ids[i] == id) {
            return i;
        }
    }
    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int find_free_gamepad_slot(SdlInputCommon* self) {
    for (int i = 0; i < UI_INPUT_MAX_GAMEPADS; i++) {
        if (self->gamepads[i] == nullptr) {
            return i;
        }
    }
    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void sdl_input_common_init(SdlInputCommon* self) {
    memory_zero(self, sizeof(SdlInputCommon));

    // Enable drop-file events.
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    for (int i = 0; i < UI_INPUT_MAX_GAMEPADS; i++) {
        self->gamepad_ids[i] = -1;
    }

    int num_joysticks = SDL_NumJoysticks();
    for (int i = 0; i < num_joysticks && i < UI_INPUT_MAX_GAMEPADS; i++) {
        if (SDL_IsGameController(i)) {
            self->gamepads[i] = SDL_GameControllerOpen(i);
            if (self->gamepads[i]) {
                self->gamepad_ids[i] = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(self->gamepads[i]));
                self->gamepad_connect_pending[i] = true;
                log_info("Gamepad found: %s", SDL_GameControllerName(self->gamepads[i]));
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The SDL windowID an event is addressed to, or 0 when it is not window-addressed (SDL_QUIT, gamepad device/axis).

static u32 sdl_event_window_id(const SDL_Event* e) {
    switch (e->type) {
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            return e->key.windowID;
        case SDL_TEXTINPUT:
            return e->text.windowID;
        case SDL_MOUSEMOTION:
            return e->motion.windowID;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            return e->button.windowID;
        case SDL_MOUSEWHEEL:
            return e->wheel.windowID;
        case SDL_WINDOWEVENT:
            return e->window.windowID;
        case SDL_DROPFILE:
            return e->drop.windowID;
        default:
            return 0;
    }
}

// Physical pixels per SDL window coordinate unit (drawable size / window size).
static float sdl_window_pixel_scale(SDL_Window* window) {
    if (window == nullptr) {
        return 1.0f;
    }
    int win_w = 0;
    int win_h = 0;
    SDL_GetWindowSize(window, &win_w, &win_h);
    int px_w = 0;
    int px_h = 0;
    const Uint32 flags = SDL_GetWindowFlags(window);
    SDL_Renderer* renderer = SDL_GetRenderer(window);
    if (renderer != nullptr) {
        SDL_GetRendererOutputSize(renderer, &px_w, &px_h);
    } else if (flags & SDL_WINDOW_METAL) {
        SDL_Metal_GetDrawableSize(window, &px_w, &px_h);
    } else if (flags & SDL_WINDOW_OPENGL) {
        SDL_GL_GetDrawableSize(window, &px_w, &px_h);
    }
    if (win_w <= 0 || px_w <= 0) {
        return 1.0f;
    }
    return (float)px_w / (float)win_w;
}

static float sdl_pointer_scale(Uint32 window_id) {
    return sdl_window_pixel_scale(SDL_GetWindowFromID(window_id));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Translate a single SDL event and push the resulting Fl event(s) onto ctx. SDL_QUIT and a per-window close both
// push FlWindowEvent_Close; neither controls the drain loop, so one window's close does not stop the others'
// events being routed in the same pump.

static void translate_event(SdlInputCommon* self, FlUiCtx* ctx, const SDL_Event e, u64 timestamp) {
    {
        switch (e.type) {
            case SDL_QUIT: {
                FlWindowEvent event = { .type = FlWindowEvent_Close, .timestamp = timestamp };
                fl_platform_input_push_window_event(ctx, event);
                break;
            }

            case SDL_WINDOWEVENT: {
                switch (e.window.event) {
                    case SDL_WINDOWEVENT_CLOSE: {
                        FlWindowEvent event = { .type = FlWindowEvent_Close, .timestamp = timestamp };
                        fl_platform_input_push_window_event(ctx, event);
                        break;
                    }
                    case SDL_WINDOWEVENT_FOCUS_GAINED: {
                        FlWindowEvent event = { .type = FlWindowEvent_Focus_Gained, .timestamp = timestamp };
                        fl_platform_input_push_window_event(ctx, event);
                        break;
                    }
                    case SDL_WINDOWEVENT_FOCUS_LOST: {
                        FlWindowEvent event = { .type = FlWindowEvent_Focus_Lost, .timestamp = timestamp };
                        fl_platform_input_push_window_event(ctx, event);
                        break;
                    }
                    case SDL_WINDOWEVENT_SIZE_CHANGED: {
                        FlWindowEvent event
                            = { .type = FlWindowEvent_Resize,
                                .resize = { .width = (u32)e.window.data1, .height = (u32)e.window.data2 },
                                .timestamp = timestamp };
                        fl_platform_input_push_window_event(ctx, event);
                        break;
                    }
                }
                break;
            }

            case SDL_KEYDOWN: {
                FlKey key = sdl_key_to_fl_key(e.key.keysym.sym);
                if (key != FlKey_Unknown) {
                    FlKeyEvent event = { .type = e.key.repeat ? FlKeyEvent_Repeat : FlKeyEvent_Down,
                                         .key = key,
                                         .modifiers = sdl_keymod_to_fl_keymod(e.key.keysym.mod),
                                         .repeat = e.key.repeat != 0,
                                         .timestamp = timestamp };
                    fl_platform_input_push_key_event(ctx, event);
                }
                break;
            }

            case SDL_KEYUP: {
                FlKey key = sdl_key_to_fl_key(e.key.keysym.sym);
                if (key != FlKey_Unknown) {
                    FlKeyEvent event = { .type = FlKeyEvent_Up,
                                         .key = key,
                                         .modifiers = sdl_keymod_to_fl_keymod(e.key.keysym.mod),
                                         .repeat = false,
                                         .timestamp = timestamp };
                    fl_platform_input_push_key_event(ctx, event);
                }
                break;
            }

            // The drop position is the global cursor rebased onto the target window. e.drop.file is SDL-owned and
            // freed after the push copies it.
            case SDL_DROPFILE: {
                if (e.drop.file == nullptr) {
                    break;
                }
                int global_x = 0;
                int global_y = 0;
                SDL_GetGlobalMouseState(&global_x, &global_y);
                int window_x = 0;
                int window_y = 0;
                SDL_Window* window = SDL_GetWindowFromID(e.drop.windowID);
                if (window != nullptr) {
                    SDL_GetWindowPosition(window, &window_x, &window_y);
                }
                const float scale = sdl_window_pixel_scale(window);
                FlWindowEvent event
                    = { .type = FlWindowEvent_Drop_File,
                        .drop
                        = { .path = (FlString) { .data = e.drop.file, .length = strlen(e.drop.file), .is_static = 1 },
                            .x = (float)(global_x - window_x) * scale,
                            .y = (float)(global_y - window_y) * scale },
                        .timestamp = timestamp };
                fl_platform_input_push_window_event(ctx, event);
                SDL_free(e.drop.file);
                break;
            }

            case SDL_TEXTINPUT: {
                FlTextInputEvent event
                    = { .text = (FlString) { .data = e.text.text, .length = strlen(e.text.text), .is_static = 1 },
                        .timestamp = timestamp };
                fl_platform_input_push_text_input_event(ctx, event);
                break;
            }

            case SDL_MOUSEMOTION: {
                // In relative mode only the raw delta is delivered.
                if (self->relative_mouse_mode) {
                    FlMouseEvent event
                        = { .type = FlMouseEvent_Relative_Motion,
                            .relative = { .delta_x = (float)e.motion.xrel, .delta_y = (float)e.motion.yrel },
                            .timestamp = timestamp };
                    fl_platform_input_push_mouse_event(ctx, event);
                    break;
                }
                const float scale = sdl_pointer_scale(e.motion.windowID);
                FlMouseEvent event = { .type = FlMouseEvent_Move,
                                       .move = { .x = (float)e.motion.x * scale,
                                                 .y = (float)e.motion.y * scale,
                                                 .delta_x = (float)e.motion.xrel * scale,
                                                 .delta_y = (float)e.motion.yrel * scale },
                                       .timestamp = timestamp };
                fl_platform_input_push_mouse_event(ctx, event);
                break;
            }

            case SDL_MOUSEBUTTONDOWN: {
                const FlMouseButton button = sdl_mouse_button_to_ui_button(e.button.button);
                if (button == FlMouseButton_Count) {
                    break;
                }
                const float scale = sdl_pointer_scale(e.button.windowID);
                FlMouseEvent event = { .type = FlMouseEvent_Button_Down,
                                       .button = { .button = button,
                                                   .x = (float)e.button.x * scale,
                                                   .y = (float)e.button.y * scale,
                                                   .click_count = e.button.clicks },
                                       .timestamp = timestamp };
                fl_platform_input_push_mouse_event(ctx, event);
                break;
            }

            case SDL_MOUSEBUTTONUP: {
                const FlMouseButton button = sdl_mouse_button_to_ui_button(e.button.button);
                if (button == FlMouseButton_Count) {
                    break;
                }
                const float scale = sdl_pointer_scale(e.button.windowID);
                FlMouseEvent event = { .type = FlMouseEvent_Button_Up,
                                       .button = { .button = button,
                                                   .x = (float)e.button.x * scale,
                                                   .y = (float)e.button.y * scale,
                                                   .click_count = 0 },
                                       .timestamp = timestamp };
                fl_platform_input_push_mouse_event(ctx, event);
                break;
            }

            case SDL_MOUSEWHEEL: {
                FlMouseEvent event = { .type = FlMouseEvent_Wheel,
                                       .wheel = { .x = e.wheel.preciseX, .y = e.wheel.preciseY },
                                       .timestamp = timestamp };
                fl_platform_input_push_mouse_event(ctx, event);
                break;
            }

            case SDL_CONTROLLERDEVICEADDED: {
                // ADDED carries a device index; resolve it to an instance id and skip devices that already own a
                // slot (their Connected goes out from the pending flush).
                SDL_JoystickID instance_id = SDL_JoystickGetDeviceInstanceID(e.cdevice.which);
                if (instance_id < 0 || find_gamepad_slot(self, instance_id) >= 0) {
                    break;
                }
                int slot = find_free_gamepad_slot(self);
                if (slot >= 0) {
                    SDL_GameController* controller = SDL_GameControllerOpen(e.cdevice.which);
                    if (controller) {
                        self->gamepads[slot] = controller;
                        self->gamepad_ids[slot] = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
                        log_info("Gamepad connected: %s (slot %d)", SDL_GameControllerName(controller), slot);

                        FlGamepadEvent event
                            = { .type = FlGamepadEvent_Connected, .gamepad_id = (u32)slot, .timestamp = timestamp };
                        fl_platform_input_push_gamepad_event(ctx, event);
                    }
                }
                break;
            }

            case SDL_CONTROLLERDEVICEREMOVED: {
                int slot = find_gamepad_slot(self, e.cdevice.which);
                if (slot >= 0) {
                    log_info("Gamepad disconnected (slot %d)", slot);
                    SDL_GameControllerClose(self->gamepads[slot]);
                    self->gamepads[slot] = nullptr;
                    self->gamepad_ids[slot] = -1;

                    FlGamepadEvent event
                        = { .type = FlGamepadEvent_Disconnected, .gamepad_id = (u32)slot, .timestamp = timestamp };
                    fl_platform_input_push_gamepad_event(ctx, event);
                }
                break;
            }

            case SDL_CONTROLLERBUTTONDOWN: {
                int slot = find_gamepad_slot(self, e.cbutton.which);
                if (slot >= 0) {
                    FlGamepadEvent event = { .type = FlGamepadEvent_Button_Down,
                                             .gamepad_id = (u32)slot,
                                             .button = { .button = sdl_gamepad_button_to_ui_button(e.cbutton.button) },
                                             .timestamp = timestamp };
                    fl_platform_input_push_gamepad_event(ctx, event);
                }
                break;
            }

            case SDL_CONTROLLERBUTTONUP: {
                int slot = find_gamepad_slot(self, e.cbutton.which);
                if (slot >= 0) {
                    FlGamepadEvent event = { .type = FlGamepadEvent_Button_Up,
                                             .gamepad_id = (u32)slot,
                                             .button = { .button = sdl_gamepad_button_to_ui_button(e.cbutton.button) },
                                             .timestamp = timestamp };
                    fl_platform_input_push_gamepad_event(ctx, event);
                }
                break;
            }

            case SDL_CONTROLLERAXISMOTION: {
                int slot = find_gamepad_slot(self, e.caxis.which);
                if (slot >= 0) {
                    // Normalize axis value from [-32768, 32767] to [-1.0, 1.0]
                    float value = (float)e.caxis.value / 32767.0f;
                    FlGamepadEvent event
                        = { .type = FlGamepadEvent_Axis_Motion,
                            .gamepad_id = (u32)slot,
                            .axis = { .axis = sdl_gamepad_axis_to_fl_axis(e.caxis.axis), .value = value },
                            .timestamp = timestamp };
                    fl_platform_input_push_gamepad_event(ctx, event);
                }
                break;
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Multi-window pump (the WindowBackend). Drains the shared queue once, demuxing each event to its window's FlUiCtx
// by SDL windowID: window_ids[i] pairs with contexts[i]. Callers pass at least the main window.

void sdl_input_common_pump_multi(SdlInputCommon* self, const u32* window_ids, FlUiCtx** contexts, int count) {
    if (count <= 0) {
        return;
    }

    for (int i = 0; i < UI_INPUT_MAX_GAMEPADS; i++) {
        if (self->gamepad_connect_pending[i]) {
            self->gamepad_connect_pending[i] = false;
            FlGamepadEvent event
                = { .type = FlGamepadEvent_Connected, .gamepad_id = (u32)i, .timestamp = get_current_time_ms_os() };
            fl_platform_input_push_gamepad_event(contexts[0], event);
        }
    }

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        u64 timestamp = get_current_time_ms_os();
        u32 target_id = sdl_event_window_id(&e);
        FlUiCtx* ctx = contexts[0]; // default: main window (non-addressed events, or an id with no live match)
        if (target_id != 0) {
            for (int i = 0; i < count; i++) {
                if (window_ids[i] == target_id) {
                    ctx = contexts[i];
                    break;
                }
            }
        }
        translate_event(self, ctx, e, timestamp);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cursor

static SDL_SystemCursor sdl_system_cursor_for_shape(FlCursorShape shape) {
    switch (shape) {
        case FlCursorShape_Arrow:
            return SDL_SYSTEM_CURSOR_ARROW;
        case FlCursorShape_Hand:
            return SDL_SYSTEM_CURSOR_HAND;
        case FlCursorShape_ResizeH:
            return SDL_SYSTEM_CURSOR_SIZEWE;
        case FlCursorShape_ResizeV:
            return SDL_SYSTEM_CURSOR_SIZENS;
        case FlCursorShape_ResizeDiagonal1:
            return SDL_SYSTEM_CURSOR_SIZENWSE;
        case FlCursorShape_ResizeDiagonal2:
            return SDL_SYSTEM_CURSOR_SIZENESW;
        case FlCursorShape_Text:
            return SDL_SYSTEM_CURSOR_IBEAM;
        case FlCursorShape_Crosshair:
            return SDL_SYSTEM_CURSOR_CROSSHAIR;
        case FlCursorShape_NotAllowed:
            return SDL_SYSTEM_CURSOR_NO;
        case FlCursorShape_Count:
            break;
    }
    return SDL_SYSTEM_CURSOR_ARROW;
}

void sdl_input_common_set_cursor(SdlInputCommon* self, FlCursor cursor) {
    FlCursorShape shape = cursor.shape;
    if ((u32)shape >= FlCursorShape_Count) {
        shape = FlCursorShape_Arrow;
    }

    // Create the system cursor on first use and cache it per shape.
    if (!self->system_cursors[shape]) {
        self->system_cursors[shape] = SDL_CreateSystemCursor(sdl_system_cursor_for_shape(shape));
    }
    if (self->system_cursors[shape]) {
        SDL_SetCursor(self->system_cursors[shape]);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void sdl_input_common_set_cursor_visible(bool visible) {
    SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Hidden-drag mechanics

void sdl_input_common_set_relative_mouse_mode(SdlInputCommon* self, bool enabled) {
    self->relative_mouse_mode = enabled;
    SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void sdl_input_common_warp_mouse(SDL_Window* window, float x, float y) {
    // x, y are drawable pixels; a null window targets the mouse-focus window.
    const float scale = sdl_window_pixel_scale(window);
    SDL_WarpMouseInWindow(window, (int)(x / scale), (int)(y / scale));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString backend_get_clipboard_text(struct FlArena* arena) {
    if (!SDL_HasClipboardText()) {
        return string_empty();
    }

    char* sdl_text = SDL_GetClipboardText();
    if (!sdl_text) {
        return string_empty();
    }

    // Copy to the arena and free the SDL text.
    u64 len = strlen(sdl_text);
    char* text_copy = arena_alloc_array(arena, char, len + 1);
    memcpy(text_copy, sdl_text, len);
    text_copy[len] = '\0';
    SDL_free(sdl_text);

    return (FlString) { .data = text_copy, .length = len, .is_static = 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool backend_set_clipboard_text(FlString text) {
    // SDL takes a null-terminated string.
    char* temp = malloc(text.length + 1);
    if (!temp) {
        return false;
    }

    memcpy(temp, text.data, text.length);
    temp[text.length] = '\0';

    int result = SDL_SetClipboardText(temp);
    free(temp);

    return result == 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void sdl_input_common_destroy(SdlInputCommon* self) {
    for (int i = 0; i < UI_INPUT_MAX_GAMEPADS; i++) {
        if (self->gamepads[i]) {
            SDL_GameControllerClose(self->gamepads[i]);
            self->gamepads[i] = nullptr;
        }
    }

    for (int i = 0; i < FlCursorShape_Count; i++) {
        if (self->system_cursors[i]) {
            SDL_FreeCursor(self->system_cursors[i]);
            self->system_cursors[i] = nullptr;
        }
    }
}

#endif // BACKEND_USE_SDL
