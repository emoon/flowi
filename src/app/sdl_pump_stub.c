#include <flowi/app/sdl_pump.h>

#if !FL_APP_HAVE_SDL

#include <core/core.h>
#include <core/log.h>

FlSdlPump* fl_sdl_pump_create(FlArena* arena, const FlSdlPumpSettings* settings, FlSdlPadFn pad_fn,
                              void* pad_user_data) {
    UNUSED(arena);
    UNUSED(settings);
    UNUSED(pad_fn);
    UNUSED(pad_user_data);
    log_error("SDL pump not available (flowi was built without SDL)");
    return nullptr;
}

void fl_sdl_pump_destroy(FlSdlPump* pump) {
    UNUSED(pump);
}

bool fl_sdl_pump_pump(FlSdlPump* pump, FlHost* host, float mouse_scale) {
    UNUSED(pump);
    UNUSED(host);
    UNUSED(mouse_scale);
    return false;
}

#endif // !FL_APP_HAVE_SDL
