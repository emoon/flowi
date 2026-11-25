#include "sdl_input_common.h"

#if BACKEND_USE_SDL

#include "app/host.h"
#include <core/arena.h>
#include <core/assert.h>
#include <core/log.h>

struct FlSdlPump {
    SdlInputCommon input;
    u32 window_id;
    bool raw_pads;
};

static void load_controller_db(FlString db) {
    if (db.length == 0) {
        return;
    }
    const int added = SDL_GameControllerAddMappingsFromRW(SDL_RWFromConstMem(db.data, (int)db.length), 1);
    if (added < 0) {
        log_warning("SDL pump: controller database rejected: %s", SDL_GetError());
    } else {
        log_info("SDL pump: %d controller mappings loaded from the controller database", added);
    }
}

FlSdlPump* fl_sdl_pump_create(FlArena* arena, const FlSdlPumpSettings* settings, FlSdlPadFn pad_fn,
                              void* pad_user_data) {
    FL_ASSERT(arena != nullptr);
    FL_ASSERT(settings != nullptr);
    FL_ASSERT(!settings->raw_pads || pad_fn != nullptr);

    if (SDL_InitSubSystem(SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) < 0) {
        log_error("SDL pump: cannot start the event and controller subsystems: %s", SDL_GetError());
        return nullptr;
    }

    // A raw-pad host maps buttons by position, as evdev reports them; SDL otherwise swaps a Nintendo pad's to match
    // its labels.
    if (settings->raw_pads) {
        SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    }
    load_controller_db(settings->controller_db);
    SDL_StartTextInput();

    FlSdlPump* pump = arena_alloc_zero(arena, FlSdlPump);
    pump->window_id = settings->window_id;
    pump->raw_pads = settings->raw_pads;
    SdlPadSink sink = { 0 };
    if (settings->raw_pads) {
        sink = (SdlPadSink) { .fn = pad_fn, .user_data = pad_user_data };
    }
    sdl_input_common_init(&pump->input, sink);
    pump->input.quit_window_id = settings->window_id;
    return pump;
}

void fl_sdl_pump_destroy(FlSdlPump* pump) {
    if (pump == nullptr) {
        return;
    }
    sdl_input_common_destroy(&pump->input);
    SDL_StopTextInput();
    if (pump->raw_pads) {
        SDL_ResetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS);
    }
    SDL_QuitSubSystem(SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER);
}

bool fl_sdl_pump_pump(FlSdlPump* pump, FlHost* host, float mouse_scale) {
    FL_ASSERT(pump != nullptr);
    FL_ASSERT(mouse_scale > 0.0f);
    FlUiCtx* contexts[1] = { host_window_context(host) };
    FL_ASSERT(contexts[0] != nullptr);
    const u32 window_ids[1] = { pump->window_id };

    pump->input.mouse_scale = mouse_scale;
    pump->input.quit_requested = false;
    sdl_input_common_pump_multi(&pump->input, window_ids, contexts, 1);
    return !pump->input.quit_requested;
}

#endif // BACKEND_USE_SDL
