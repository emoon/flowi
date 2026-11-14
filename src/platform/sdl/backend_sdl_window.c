// SDL window backend: owns the OS connection (SDL video) and creates per-window OS windows in one of three
// construction modes (FlWindowBackendMode). Window lifecycle, the event pump (demuxed per window by SDL windowID
// via the shared sdl_input_common module), cursor mechanics and per-window drawable size + content scale are
// shared by every mode; construction, teardown and presentation live in the mode's own file:
//   GL mode, sdl_window_gl.c: SDL-GL windows carrying a single shared GL context + the fullscreen-quad
//     compositor. Requires the GPU build (FL_WINDOW_HAVE_GPU).
//   Metal mode, sdl_window_metal.c: SDL-Metal windows carrying a CAMetalLayer.
//   Software mode, sdl_window_software.c: plain SDL_Renderer windows (no GL context).

#include "../window_backend.h"

#if BACKEND_USE_SDL

#include "sdl_window_internal.h"
#include <core/log.h>

#define SDL_WINDOW_PUMP_MAX 32 // upper bound on live windows the per-pump windowID array holds

// The mode's table, or nullptr (logged) when this build lacks it.
static const SdlWindowMode* sdl_window_mode_for(FlWindowBackendMode mode) {
    switch (mode) {
        case FlWindowBackendMode_Software:
#if FL_WINDOW_HAVE_SOFTWARE
            return &k_sdl_window_mode_software;
#else
            log_error("SDL window backend: software mode is not built for this platform");
            return nullptr;
#endif
        case FlWindowBackendMode_Metal:
#if FL_WINDOW_HAVE_METAL
            return &k_sdl_window_mode_metal;
#else
            log_error("SDL window backend: Metal mode requires the Metal build (APPLE)");
            return nullptr;
#endif
        case FlWindowBackendMode_Gl:
        default:
#if FL_WINDOW_HAVE_GPU
            return &k_sdl_window_mode_gl;
#else
            log_error("SDL window backend: GL mode requires the GPU build");
            return nullptr;
#endif
    }
}

bool sdl_create_base_window(FlWindow* window, const SdlWindowParams* params, u32 mode_flags, const char* mode_label) {
    const u32 resizable_flag = params->resizable ? SDL_WINDOW_RESIZABLE : 0;
    window->window = SDL_CreateWindow(params->title, params->pos_x, params->pos_y, params->width, params->height,
                                      mode_flags | params->fullscreen_flag | resizable_flag | SDL_WINDOW_ALLOW_HIGHDPI);
    if (window->window == nullptr) {
        log_error("fl_app window: SDL_CreateWindow (%s) failed: %s", mode_label, SDL_GetError());
        return false;
    }
    window->window_id = SDL_GetWindowID(window->window);
    return true;
}

// FL_WINDOW_POS_DEFAULT centers the window; any other value is an OS-screen coordinate.
static int sdl_window_coordinate(int requested) {
    return requested != FL_WINDOW_POS_DEFAULT ? requested : (int)SDL_WINDOWPOS_CENTERED;
}

static SdlWindowParams sdl_window_params(SdlWindowContext* self, const FlWindowConfig* config) {
    SdlWindowParams params = {
        .title = "flowi",
        .pos_x = (int)SDL_WINDOWPOS_CENTERED,
        .pos_y = (int)SDL_WINDOWPOS_CENTERED,
        .resizable = true,
    };
    if (config == nullptr) {
        return params;
    }
    if (config->title.data) {
        params.title = fl_string_to_cstr(self->arena, config->title);
    }
    params.pos_x = sdl_window_coordinate(config->x);
    params.pos_y = sdl_window_coordinate(config->y);
    params.width = config->width;
    params.height = config->height;
    params.fullscreen_flag = config->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
    params.resizable = config->resizable;
    return params;
}

static FlWindow* sdl_create_window(void* ud, const FlWindowConfig* config) {
    SdlWindowContext* self = (SdlWindowContext*)ud;
    const SdlWindowParams params = sdl_window_params(self, config);
    FlWindow* window = arena_alloc_zero(self->arena, FlWindow);
    if (!self->ops->create_window(self, window, &params)) {
        return nullptr;
    }
    return window;
}

static void sdl_destroy_window(void* ud, FlWindow* window) {
    SdlWindowContext* self = (SdlWindowContext*)ud;
    if (window == nullptr || window->window == nullptr) {
        return;
    }
    self->ops->destroy_window(self, window);
    SDL_DestroyWindow(window->window);
    window->window = nullptr;
}

// Event pump: build the parallel windowID array the shared pump demuxes against (windows[i] -> contexts[i])

static void sdl_pump_events(void* ud, FlWindow** windows, FlUiCtx** contexts, int count) {
    SdlWindowContext* self = (SdlWindowContext*)ud;
    if (count > SDL_WINDOW_PUMP_MAX) {
        count = SDL_WINDOW_PUMP_MAX;
    }
    u32 window_ids[SDL_WINDOW_PUMP_MAX];
    for (int i = 0; i < count; i++) {
        window_ids[i] = windows[i] ? windows[i]->window_id : 0;
    }
    sdl_input_common_pump_multi(&self->input, window_ids, contexts, count);
}

static void sdl_drawable_size(void* ud, FlWindow* window, int* width, int* height) {
    SdlWindowContext* self = (SdlWindowContext*)ud;
    if (window == nullptr || window->window == nullptr) {
        return;
    }
    int draw_w = 0, draw_h = 0;
    self->ops->pixel_size(window, &draw_w, &draw_h);
    if (width) {
        *width = draw_w;
    }
    if (height) {
        *height = draw_h;
    }
}

// The display's content scale (ddpi / 96) on the Wayland video driver. Returns 0 on any other driver or when the
// DPI is unavailable.
static float sdl_display_index_content_scale(int display_index) {
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver == nullptr || SDL_strcmp(driver, "wayland") != 0) {
        return 0.0f;
    }
    float ddpi = 0.0f, hdpi = 0.0f, vdpi = 0.0f;
    if (SDL_GetDisplayDPI(display_index, &ddpi, &hdpi, &vdpi) != 0 || ddpi <= 0.0f) {
        return 0.0f;
    }
    return ddpi / 96.0f;
}

static float sdl_display_content_scale(SDL_Window* window) {
    return sdl_display_index_content_scale(SDL_GetWindowDisplayIndex(window));
}

// The primary display's scale for sizing a window before it exists; 0 where window sizes are not physical pixels.
static float sdl_primary_display_content_scale(void* ud) {
    UNUSED(ud);
    return sdl_display_index_content_scale(0);
}

static float sdl_content_scale(void* ud, FlWindow* window) {
    SdlWindowContext* self = (SdlWindowContext*)ud;
    if (window == nullptr || window->window == nullptr) {
        return 1.0f;
    }
    // Content scale = backbuffer (pixel) size / logical window size.
    int win_w = 0, win_h = 0, px_w = 0, px_h = 0;
    SDL_GetWindowSize(window->window, &win_w, &win_h);
    self->ops->pixel_size(window, &px_w, &px_h);
    if (win_w <= 0 || px_w <= 0) {
        return 1.0f;
    }
    const float ratio = (float)px_w / (float)win_w;
    if (ratio > 1.0f) {
        return ratio;
    }
    // A ratio of 1 falls back to the display's content scale.
    const float display_scale = sdl_display_content_scale(window->window);
    return display_scale > 0.0f ? display_scale : ratio;
}

// Cursor + hidden-drag mechanics are delegated to the shared SDL input state

static void sdl_set_cursor(void* ud, FlWindow* window, FlCursor cursor) {
    UNUSED(window);
    SdlWindowContext* self = (SdlWindowContext*)ud;
    sdl_input_common_set_cursor(&self->input, cursor);
}

static void sdl_set_cursor_visible(void* ud, FlWindow* window, bool visible) {
    UNUSED(ud);
    UNUSED(window);
    sdl_input_common_set_cursor_visible(visible);
}

static void sdl_set_relative_mouse_mode(void* ud, FlWindow* window, bool enabled) {
    UNUSED(window);
    SdlWindowContext* self = (SdlWindowContext*)ud;
    sdl_input_common_set_relative_mouse_mode(&self->input, enabled);
}

static void sdl_warp_mouse(void* ud, FlWindow* window, float x, float y) {
    UNUSED(ud);
    sdl_input_common_warp_mouse(window ? window->window : nullptr, x, y);
}

static void sdl_window_destroy(void* ud) {
    SdlWindowContext* self = (SdlWindowContext*)ud;
    sdl_input_common_destroy(&self->input);
    // SDL subsystems are left up (no SDL_Quit).
}

FlWindowBackend sdl_window_backend_create(struct FlArena* arena, FlWindowBackendMode mode) {
    // A mode this build lacks returns a zeroed backend.
    const SdlWindowMode* mode_ops = sdl_window_mode_for(mode);
    if (mode_ops == nullptr) {
        return (FlWindowBackend) { 0 };
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        log_error("fl_app window: SDL_Init(VIDEO) failed: %s", SDL_GetError());
        return (FlWindowBackend) { 0 };
    }
    if (!(SDL_WasInit(SDL_INIT_GAMECONTROLLER) & SDL_INIT_GAMECONTROLLER)) {
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0) {
            log_warning("fl_app window: SDL game controller init failed: %s", SDL_GetError());
        }
    }

    SdlWindowContext* self = arena_alloc_zero(arena, SdlWindowContext);
    self->arena = arena;
    self->ops = mode_ops;
    sdl_input_common_init(&self->input);

    log_info("Window Backend: SDL initialized (%s mode)", mode_ops->name);

    FlWindowBackend backend = {
        .user_data = self,
        .create_window = sdl_create_window,
        .destroy_window = sdl_destroy_window,
        .pump_events = sdl_pump_events,
        .drawable_size = sdl_drawable_size,
        .content_scale = sdl_content_scale,
        .display_content_scale = sdl_primary_display_content_scale,
        .set_cursor = sdl_set_cursor,
        .set_cursor_visible = sdl_set_cursor_visible,
        .set_relative_mouse_mode = sdl_set_relative_mouse_mode,
        .warp_mouse = sdl_warp_mouse,
        .destroy = sdl_window_destroy,
    };
    // An unrecognised mode gets no present slot.
    if (mode_ops->mode == mode) {
        backend.present = mode_ops->present;
        backend.present_software = mode_ops->present_software;
        backend.make_current = mode_ops->make_current;
    }
    return backend;
}

#else // !BACKEND_USE_SDL

#include <core/core.h>
#include <core/log.h>

FlWindowBackend sdl_window_backend_create(struct FlArena* arena, FlWindowBackendMode mode) {
    UNUSED(arena);
    UNUSED(mode);
    log_error("SDL window backend not available (requires BACKEND_USE_SDL)");
    return (FlWindowBackend) { 0 };
}

#endif // BACKEND_USE_SDL
