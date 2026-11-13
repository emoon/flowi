// Gamepad slot bookkeeping and SDL controller event translation (see sdl_gamepads.h).

#include "sdl_gamepads.h"

#if BACKEND_USE_SDL

#include "sdl_input_maps.h"
#include <core/log.h>
#include <core/memory.h>
#include <core/os/os.h>

static int find_slot(const SdlGamepads* self, SDL_JoystickID id) {
    for_count(i, UI_INPUT_MAX_GAMEPADS) {
        if (self->ids[i] == id) {
            return i;
        }
    }
    return -1;
}

static int find_free_slot(const SdlGamepads* self) {
    for_count(i, UI_INPUT_MAX_GAMEPADS) {
        if (self->controllers[i] == nullptr) {
            return i;
        }
    }
    return -1;
}

void sdl_gamepads_init(SdlGamepads* self) {
    memory_zero(self, sizeof(SdlGamepads));

    for_count(i, UI_INPUT_MAX_GAMEPADS) {
        self->ids[i] = -1;
    }

    const int num_joysticks = SDL_NumJoysticks();
    for (int i = 0; i < num_joysticks && i < UI_INPUT_MAX_GAMEPADS; i++) {
        if (!SDL_IsGameController(i)) {
            continue;
        }
        self->controllers[i] = SDL_GameControllerOpen(i);
        if (self->controllers[i] == nullptr) {
            continue;
        }
        self->ids[i] = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(self->controllers[i]));
        self->connect_pending[i] = true;
        log_info("Gamepad found: %s", SDL_GameControllerName(self->controllers[i]));
    }
}

void sdl_gamepads_flush_pending(SdlGamepads* self, FlUiCtx* ctx) {
    for_count(i, UI_INPUT_MAX_GAMEPADS) {
        if (!self->connect_pending[i]) {
            continue;
        }
        self->connect_pending[i] = false;
        FlGamepadEvent event
            = { .type = FlGamepadEvent_Connected, .gamepad_id = (u32)i, .timestamp = get_current_time_ms_os() };
        platform_input_push_gamepad_event(ctx, event);
    }
}

// ADDED carries a device index; resolve it to an instance id and skip devices that already own a slot (their
// Connected goes out from the pending flush).
static void handle_device_added(SdlGamepads* self, FlUiCtx* ctx, int device_index, u64 timestamp) {
    const SDL_JoystickID instance_id = SDL_JoystickGetDeviceInstanceID(device_index);
    if (instance_id < 0 || find_slot(self, instance_id) >= 0) {
        return;
    }
    const int slot = find_free_slot(self);
    if (slot < 0) {
        return;
    }
    SDL_GameController* controller = SDL_GameControllerOpen(device_index);
    if (controller == nullptr) {
        return;
    }
    self->controllers[slot] = controller;
    self->ids[slot] = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller));
    log_info("Gamepad connected: %s (slot %d)", SDL_GameControllerName(controller), slot);

    FlGamepadEvent event = { .type = FlGamepadEvent_Connected, .gamepad_id = (u32)slot, .timestamp = timestamp };
    platform_input_push_gamepad_event(ctx, event);
}

static void handle_device_removed(SdlGamepads* self, FlUiCtx* ctx, SDL_JoystickID id, u64 timestamp) {
    const int slot = find_slot(self, id);
    if (slot < 0) {
        return;
    }
    log_info("Gamepad disconnected (slot %d)", slot);
    SDL_GameControllerClose(self->controllers[slot]);
    self->controllers[slot] = nullptr;
    self->ids[slot] = -1;

    FlGamepadEvent event = { .type = FlGamepadEvent_Disconnected, .gamepad_id = (u32)slot, .timestamp = timestamp };
    platform_input_push_gamepad_event(ctx, event);
}

static void handle_button(const SdlGamepads* self, FlUiCtx* ctx, const SDL_ControllerButtonEvent* e, u64 timestamp) {
    const int slot = find_slot(self, e->which);
    if (slot < 0) {
        return;
    }
    FlGamepadEvent event
        = { .type = e->type == SDL_CONTROLLERBUTTONDOWN ? FlGamepadEvent_Button_Down : FlGamepadEvent_Button_Up,
            .gamepad_id = (u32)slot,
            .button = { .button = sdl_gamepad_button_to_ui_button(e->button) },
            .timestamp = timestamp };
    platform_input_push_gamepad_event(ctx, event);
}

static void handle_axis(const SdlGamepads* self, FlUiCtx* ctx, const SDL_ControllerAxisEvent* e, u64 timestamp) {
    const int slot = find_slot(self, e->which);
    if (slot < 0) {
        return;
    }
    // Normalize axis value from [-32768, 32767] to [-1.0, 1.0]
    const float value = (float)e->value / 32767.0f;
    FlGamepadEvent event = { .type = FlGamepadEvent_Axis_Motion,
                             .gamepad_id = (u32)slot,
                             .axis = { .axis = sdl_gamepad_axis_to_fl_axis(e->axis), .value = value },
                             .timestamp = timestamp };
    platform_input_push_gamepad_event(ctx, event);
}

void sdl_gamepads_handle_event(SdlGamepads* self, FlUiCtx* ctx, const SDL_Event* e, u64 timestamp) {
    switch (e->type) {
        case SDL_CONTROLLERDEVICEADDED:
            handle_device_added(self, ctx, e->cdevice.which, timestamp);
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            handle_device_removed(self, ctx, e->cdevice.which, timestamp);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
            handle_button(self, ctx, &e->cbutton, timestamp);
            break;
        case SDL_CONTROLLERAXISMOTION:
            handle_axis(self, ctx, &e->caxis, timestamp);
            break;
    }
}

void sdl_gamepads_destroy(SdlGamepads* self) {
    for_count(i, UI_INPUT_MAX_GAMEPADS) {
        if (self->controllers[i]) {
            SDL_GameControllerClose(self->controllers[i]);
            self->controllers[i] = nullptr;
        }
    }
}

#endif // BACKEND_USE_SDL
