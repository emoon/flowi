// SDL -> flowi enum mappings (see sdl_input_maps.h).

#include "sdl_input_maps.h"

#if BACKEND_USE_SDL

#include <core/core.h>

typedef struct KeyPair {
    SDL_Keycode sdl;
    FlKey fl;
} KeyPair;

// Keys without a contiguous range on both sides.
static const KeyPair k_key_pairs[] = {
    { SDLK_SPACE, FlKey_Space },
    { SDLK_QUOTE, FlKey_Apostrophe },
    { SDLK_COMMA, FlKey_Comma },
    { SDLK_MINUS, FlKey_Minus },
    { SDLK_PERIOD, FlKey_Period },
    { SDLK_SLASH, FlKey_Slash },
    { SDLK_SEMICOLON, FlKey_Semicolon },
    { SDLK_EQUALS, FlKey_Equal },
    { SDLK_LEFTBRACKET, FlKey_Left_Bracket },
    { SDLK_BACKSLASH, FlKey_Backslash },
    { SDLK_RIGHTBRACKET, FlKey_Right_Bracket },
    { SDLK_BACKQUOTE, FlKey_Grave_Accent },
    { SDLK_ESCAPE, FlKey_Escape },
    { SDLK_RETURN, FlKey_Enter },
    { SDLK_TAB, FlKey_Tab },
    { SDLK_BACKSPACE, FlKey_Backspace },
    { SDLK_INSERT, FlKey_Insert },
    { SDLK_DELETE, FlKey_Delete },
    { SDLK_RIGHT, FlKey_Right },
    { SDLK_LEFT, FlKey_Left },
    { SDLK_DOWN, FlKey_Down },
    { SDLK_UP, FlKey_Up },
    { SDLK_PAGEUP, FlKey_Page_Up },
    { SDLK_PAGEDOWN, FlKey_Page_Down },
    { SDLK_HOME, FlKey_Home },
    { SDLK_END, FlKey_End },
    { SDLK_CAPSLOCK, FlKey_Caps_Lock },
    { SDLK_SCROLLLOCK, FlKey_Scroll_Lock },
    { SDLK_NUMLOCKCLEAR, FlKey_Num_Lock },
    { SDLK_PRINTSCREEN, FlKey_Print_Screen },
    { SDLK_PAUSE, FlKey_Pause },
    { SDLK_KP_0, FlKey_Kp_0 },
    { SDLK_KP_DECIMAL, FlKey_Kp_Decimal },
    { SDLK_KP_DIVIDE, FlKey_Kp_Divide },
    { SDLK_KP_MULTIPLY, FlKey_Kp_Multiply },
    { SDLK_KP_MINUS, FlKey_Kp_Subtract },
    { SDLK_KP_PLUS, FlKey_Kp_Add },
    { SDLK_KP_ENTER, FlKey_Kp_Enter },
    { SDLK_KP_EQUALS, FlKey_Kp_Equal },
    { SDLK_LSHIFT, FlKey_Left_Shift },
    { SDLK_LCTRL, FlKey_Left_Control },
    { SDLK_LALT, FlKey_Left_Alt },
    { SDLK_LGUI, FlKey_Left_Super },
    { SDLK_RSHIFT, FlKey_Right_Shift },
    { SDLK_RCTRL, FlKey_Right_Control },
    { SDLK_RALT, FlKey_Right_Alt },
    { SDLK_RGUI, FlKey_Right_Super },
    { SDLK_MENU, FlKey_Menu },
};

// A run of SDL keycodes that maps onto a run of consecutive FlKey values.
typedef struct KeyRange {
    SDL_Keycode sdl_first;
    SDL_Keycode sdl_last;
    FlKey fl_first;
} KeyRange;

static const KeyRange k_key_ranges[] = {
    { SDLK_a, SDLK_z, FlKey_A },
    { SDLK_0, SDLK_9, FlKey_Num0 },
    { SDLK_F1, SDLK_F12, FlKey_F1 },
    { SDLK_KP_1, SDLK_KP_9, FlKey_Kp_1 },
};

static FlKey key_from_ranges(SDL_Keycode sdl_key) {
    for_count(i, sizeof_array(k_key_ranges)) {
        const KeyRange* range = &k_key_ranges[i];
        if (sdl_key >= range->sdl_first && sdl_key <= range->sdl_last) {
            return (FlKey)(range->fl_first + (sdl_key - range->sdl_first));
        }
    }
    return FlKey_Unknown;
}

static FlKey key_from_pairs(SDL_Keycode sdl_key) {
    for_count(i, sizeof_array(k_key_pairs)) {
        if (k_key_pairs[i].sdl == sdl_key) {
            return k_key_pairs[i].fl;
        }
    }
    return FlKey_Unknown;
}

FlKey sdl_key_to_fl_key(SDL_Keycode sdl_key) {
    const FlKey key = key_from_ranges(sdl_key);
    return key != FlKey_Unknown ? key : key_from_pairs(sdl_key);
}

typedef struct ModPair {
    u16 sdl;
    u32 fl;
} ModPair;

static const ModPair k_mod_pairs[] = {
    { KMOD_SHIFT, FlKeyModifier_Shift }, { KMOD_CTRL, FlKeyModifier_Control },   { KMOD_ALT, FlKeyModifier_Alt },
    { KMOD_GUI, FlKeyModifier_Super },   { KMOD_CAPS, FlKeyModifier_Caps_Lock }, { KMOD_NUM, FlKeyModifier_Num_Lock },
};

u32 sdl_keymod_to_fl_keymod(u16 sdl_mod) {
    u32 modifiers = 0;
    for_count(i, sizeof_array(k_mod_pairs)) {
        if (sdl_mod & k_mod_pairs[i].sdl) {
            modifiers |= k_mod_pairs[i].fl;
        }
    }
    return modifiers;
}

FlMouseButton sdl_mouse_button_to_ui_button(u8 sdl_button) {
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

// FlGamepadButton_A (0) is the fallback for the entries left out.
static const FlGamepadButton k_gamepad_buttons[SDL_CONTROLLER_BUTTON_MAX] = {
    [SDL_CONTROLLER_BUTTON_A] = FlGamepadButton_A,
    [SDL_CONTROLLER_BUTTON_B] = FlGamepadButton_B,
    [SDL_CONTROLLER_BUTTON_X] = FlGamepadButton_X,
    [SDL_CONTROLLER_BUTTON_Y] = FlGamepadButton_Y,
    [SDL_CONTROLLER_BUTTON_BACK] = FlGamepadButton_Back,
    [SDL_CONTROLLER_BUTTON_GUIDE] = FlGamepadButton_Guide,
    [SDL_CONTROLLER_BUTTON_START] = FlGamepadButton_Start,
    [SDL_CONTROLLER_BUTTON_LEFTSTICK] = FlGamepadButton_Left_Thumb,
    [SDL_CONTROLLER_BUTTON_RIGHTSTICK] = FlGamepadButton_Right_Thumb,
    [SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = FlGamepadButton_Left_Bumper,
    [SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = FlGamepadButton_Right_Bumper,
    [SDL_CONTROLLER_BUTTON_DPAD_UP] = FlGamepadButton_Dpad_Up,
    [SDL_CONTROLLER_BUTTON_DPAD_DOWN] = FlGamepadButton_Dpad_Down,
    [SDL_CONTROLLER_BUTTON_DPAD_LEFT] = FlGamepadButton_Dpad_Left,
    [SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = FlGamepadButton_Dpad_Right,
};

FlGamepadButton sdl_gamepad_button_to_ui_button(SDL_GameControllerButton sdl_button) {
    if ((u32)sdl_button >= SDL_CONTROLLER_BUTTON_MAX) {
        return FlGamepadButton_A;
    }
    return k_gamepad_buttons[sdl_button];
}

// FlGamepadAxis_Left_X (0) is the fallback for the entries left out.
static const FlGamepadAxis k_gamepad_axes[SDL_CONTROLLER_AXIS_MAX] = {
    [SDL_CONTROLLER_AXIS_LEFTX] = FlGamepadAxis_Left_X,
    [SDL_CONTROLLER_AXIS_LEFTY] = FlGamepadAxis_Left_Y,
    [SDL_CONTROLLER_AXIS_RIGHTX] = FlGamepadAxis_Right_X,
    [SDL_CONTROLLER_AXIS_RIGHTY] = FlGamepadAxis_Right_Y,
    [SDL_CONTROLLER_AXIS_TRIGGERLEFT] = FlGamepadAxis_Left_Trigger,
    [SDL_CONTROLLER_AXIS_TRIGGERRIGHT] = FlGamepadAxis_Right_Trigger,
};

FlGamepadAxis sdl_gamepad_axis_to_fl_axis(SDL_GameControllerAxis sdl_axis) {
    if ((u32)sdl_axis >= SDL_CONTROLLER_AXIS_MAX) {
        return FlGamepadAxis_Left_X;
    }
    return k_gamepad_axes[sdl_axis];
}

static const SDL_SystemCursor k_system_cursors[FlCursorShape_Count] = {
    [FlCursorShape_Arrow] = SDL_SYSTEM_CURSOR_ARROW,
    [FlCursorShape_Hand] = SDL_SYSTEM_CURSOR_HAND,
    [FlCursorShape_ResizeH] = SDL_SYSTEM_CURSOR_SIZEWE,
    [FlCursorShape_ResizeV] = SDL_SYSTEM_CURSOR_SIZENS,
    [FlCursorShape_ResizeDiagonal1] = SDL_SYSTEM_CURSOR_SIZENWSE,
    [FlCursorShape_ResizeDiagonal2] = SDL_SYSTEM_CURSOR_SIZENESW,
    [FlCursorShape_Text] = SDL_SYSTEM_CURSOR_IBEAM,
    [FlCursorShape_Crosshair] = SDL_SYSTEM_CURSOR_CROSSHAIR,
    [FlCursorShape_NotAllowed] = SDL_SYSTEM_CURSOR_NO,
};

SDL_SystemCursor sdl_system_cursor_for_shape(FlCursorShape shape) {
    if ((u32)shape >= FlCursorShape_Count) {
        return SDL_SYSTEM_CURSOR_ARROW;
    }
    return k_system_cursors[shape];
}

#endif // BACKEND_USE_SDL
