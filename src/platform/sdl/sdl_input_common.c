// Shared SDL event translation + input-device state (see sdl_input_common.h).

#include "sdl_input_common.h"

#if BACKEND_USE_SDL

#include "sdl_input_maps.h"
#include <core/memory.h>
#include <core/os/os.h>
#include <string.h>

static u32 mouse_event_window_id(const SDL_Event* e) {
    switch (e->type) {
        case SDL_MOUSEMOTION:
            return e->motion.windowID;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            return e->button.windowID;
        case SDL_MOUSEWHEEL:
            return e->wheel.windowID;
        default:
            return 0;
    }
}

// The SDL windowID an event is addressed to, or 0 when it is not window-addressed (SDL_QUIT, gamepad device/axis).
static u32 sdl_event_window_id(const SDL_Event* e) {
    switch (e->type) {
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            return e->key.windowID;
        case SDL_TEXTINPUT:
            return e->text.windowID;
        case SDL_WINDOWEVENT:
            return e->window.windowID;
        case SDL_DROPFILE:
            return e->drop.windowID;
        default:
            return mouse_event_window_id(e);
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

static void push_window_event(FlUiCtx* ctx, FlWindowEventType type, u64 timestamp) {
    FlWindowEvent event = { .type = type, .timestamp = timestamp };
    platform_input_push_window_event(ctx, event);
}

static void translate_window_subevent(FlUiCtx* ctx, const SDL_WindowEvent* w, u64 timestamp) {
    switch (w->event) {
        case SDL_WINDOWEVENT_CLOSE:
            push_window_event(ctx, FlWindowEvent_Close, timestamp);
            break;
        case SDL_WINDOWEVENT_FOCUS_GAINED:
            push_window_event(ctx, FlWindowEvent_Focus_Gained, timestamp);
            break;
        case SDL_WINDOWEVENT_FOCUS_LOST:
            push_window_event(ctx, FlWindowEvent_Focus_Lost, timestamp);
            break;
        case SDL_WINDOWEVENT_SIZE_CHANGED: {
            FlWindowEvent event = { .type = FlWindowEvent_Resize,
                                    .resize = { .width = (u32)w->data1, .height = (u32)w->data2 },
                                    .timestamp = timestamp };
            platform_input_push_window_event(ctx, event);
            break;
        }
    }
}

// The drop position is the global cursor rebased onto the target window. drop->file is SDL-owned and freed after the
// push copies it.
static void translate_drop_file(FlUiCtx* ctx, const SDL_DropEvent* drop, u64 timestamp) {
    if (drop->file == nullptr) {
        return;
    }
    int global_x = 0;
    int global_y = 0;
    SDL_GetGlobalMouseState(&global_x, &global_y);
    int window_x = 0;
    int window_y = 0;
    SDL_Window* window = SDL_GetWindowFromID(drop->windowID);
    if (window != nullptr) {
        SDL_GetWindowPosition(window, &window_x, &window_y);
    }
    const float scale = sdl_window_pixel_scale(window);
    FlWindowEvent event
        = { .type = FlWindowEvent_Drop_File,
            .drop = { .path = (FlString) { .data = drop->file, .length = strlen(drop->file), .is_static = 1 },
                      .x = (float)(global_x - window_x) * scale,
                      .y = (float)(global_y - window_y) * scale },
            .timestamp = timestamp };
    platform_input_push_window_event(ctx, event);
    SDL_free(drop->file);
}

// SDL_QUIT and a per-window close both push FlWindowEvent_Close; neither controls the drain loop, so one window's
// close does not stop the others' events being routed in the same pump.
static bool translate_window_family(FlUiCtx* ctx, const SDL_Event* e, u64 timestamp) {
    switch (e->type) {
        case SDL_QUIT:
            push_window_event(ctx, FlWindowEvent_Close, timestamp);
            return true;
        case SDL_WINDOWEVENT:
            translate_window_subevent(ctx, &e->window, timestamp);
            return true;
        case SDL_DROPFILE:
            translate_drop_file(ctx, &e->drop, timestamp);
            return true;
    }
    return false;
}

static void translate_key(FlUiCtx* ctx, const SDL_KeyboardEvent* k, bool is_down, u64 timestamp) {
    const FlKey key = sdl_key_to_fl_key(k->keysym.sym);
    if (key == FlKey_Unknown) {
        return;
    }
    FlKeyEventType type = FlKeyEvent_Up;
    if (is_down) {
        type = k->repeat ? FlKeyEvent_Repeat : FlKeyEvent_Down;
    }
    FlKeyEvent event = { .type = type,
                         .key = key,
                         .modifiers = sdl_keymod_to_fl_keymod(k->keysym.mod),
                         .repeat = is_down && k->repeat != 0,
                         .timestamp = timestamp };
    platform_input_push_key_event(ctx, event);
}

static void translate_text_input(FlUiCtx* ctx, const SDL_TextInputEvent* t, u64 timestamp) {
    FlTextInputEvent event
        = { .text = (FlString) { .data = t->text, .length = strlen(t->text), .is_static = 1 }, .timestamp = timestamp };
    platform_input_push_text_input_event(ctx, event);
}

static bool translate_keyboard_family(FlUiCtx* ctx, const SDL_Event* e, u64 timestamp) {
    switch (e->type) {
        case SDL_KEYDOWN:
            translate_key(ctx, &e->key, true, timestamp);
            return true;
        case SDL_KEYUP:
            translate_key(ctx, &e->key, false, timestamp);
            return true;
        case SDL_TEXTINPUT:
            translate_text_input(ctx, &e->text, timestamp);
            return true;
    }
    return false;
}

static void translate_mouse_motion(SdlInputCommon* self, FlUiCtx* ctx, const SDL_MouseMotionEvent* m, u64 timestamp) {
    // In relative mode only the raw delta is delivered.
    if (self->relative_mouse_mode) {
        FlMouseEvent event = { .type = FlMouseEvent_Relative_Motion,
                               .relative = { .delta_x = (float)m->xrel, .delta_y = (float)m->yrel },
                               .timestamp = timestamp };
        platform_input_push_mouse_event(ctx, event);
        return;
    }
    const float scale = sdl_pointer_scale(m->windowID);
    FlMouseEvent event = { .type = FlMouseEvent_Move,
                           .move = { .x = (float)m->x * scale,
                                     .y = (float)m->y * scale,
                                     .delta_x = (float)m->xrel * scale,
                                     .delta_y = (float)m->yrel * scale },
                           .timestamp = timestamp };
    platform_input_push_mouse_event(ctx, event);
}

static void translate_mouse_button(FlUiCtx* ctx, const SDL_MouseButtonEvent* b, bool is_down, u64 timestamp) {
    const FlMouseButton button = sdl_mouse_button_to_ui_button(b->button);
    if (button == FlMouseButton_Count) {
        return;
    }
    const float scale = sdl_pointer_scale(b->windowID);
    FlMouseEvent event = { .type = is_down ? FlMouseEvent_Button_Down : FlMouseEvent_Button_Up,
                           .button = { .button = button,
                                       .x = (float)b->x * scale,
                                       .y = (float)b->y * scale,
                                       .click_count = is_down ? b->clicks : 0 },
                           .timestamp = timestamp };
    platform_input_push_mouse_event(ctx, event);
}

static void translate_mouse_wheel(FlUiCtx* ctx, const SDL_MouseWheelEvent* w, u64 timestamp) {
    FlMouseEvent event
        = { .type = FlMouseEvent_Wheel, .wheel = { .x = w->preciseX, .y = w->preciseY }, .timestamp = timestamp };
    platform_input_push_mouse_event(ctx, event);
}

static bool translate_mouse_family(SdlInputCommon* self, FlUiCtx* ctx, const SDL_Event* e, u64 timestamp) {
    switch (e->type) {
        case SDL_MOUSEMOTION:
            translate_mouse_motion(self, ctx, &e->motion, timestamp);
            return true;
        case SDL_MOUSEBUTTONDOWN:
            translate_mouse_button(ctx, &e->button, true, timestamp);
            return true;
        case SDL_MOUSEBUTTONUP:
            translate_mouse_button(ctx, &e->button, false, timestamp);
            return true;
        case SDL_MOUSEWHEEL:
            translate_mouse_wheel(ctx, &e->wheel, timestamp);
            return true;
    }
    return false;
}

// Translate a single SDL event and push the resulting Fl event(s) onto ctx.
static void translate_event(SdlInputCommon* self, FlUiCtx* ctx, const SDL_Event* e, u64 timestamp) {
    if (translate_window_family(ctx, e, timestamp)) {
        return;
    }
    if (translate_keyboard_family(ctx, e, timestamp)) {
        return;
    }
    if (translate_mouse_family(self, ctx, e, timestamp)) {
        return;
    }
    sdl_gamepads_handle_event(&self->gamepads, ctx, e, timestamp);
}

// The context an event is addressed to: its window's, or the main window (contexts[0]) for non-addressed events and
// an id with no live match.
static FlUiCtx* route_context(const SDL_Event* e, const u32* window_ids, FlUiCtx** contexts, int count) {
    const u32 target_id = sdl_event_window_id(e);
    if (target_id == 0) {
        return contexts[0];
    }
    for (int i = 0; i < count; i++) {
        if (window_ids[i] == target_id) {
            return contexts[i];
        }
    }
    return contexts[0];
}

void sdl_input_common_init(SdlInputCommon* self) {
    memory_zero(self, sizeof(SdlInputCommon));

    // Enable drop-file events.
    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    sdl_gamepads_init(&self->gamepads);
}

// Multi-window pump (the WindowBackend). Drains the shared queue once, demuxing each event to its window's FlUiCtx
// by SDL windowID: window_ids[i] pairs with contexts[i]. Callers pass at least the main window.
void sdl_input_common_pump_multi(SdlInputCommon* self, const u32* window_ids, FlUiCtx** contexts, int count) {
    if (count <= 0) {
        return;
    }

    sdl_gamepads_flush_pending(&self->gamepads, contexts[0]);

    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        const u64 timestamp = get_current_time_ms_os();
        translate_event(self, route_context(&e, window_ids, contexts, count), &e, timestamp);
    }
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

void sdl_input_common_set_cursor_visible(bool visible) {
    SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE);
}

void sdl_input_common_set_relative_mouse_mode(SdlInputCommon* self, bool enabled) {
    self->relative_mouse_mode = enabled;
    SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE);
}

void sdl_input_common_warp_mouse(SDL_Window* window, float x, float y) {
    // x, y are drawable pixels; a null window targets the mouse-focus window.
    const float scale = sdl_window_pixel_scale(window);
    SDL_WarpMouseInWindow(window, (int)(x / scale), (int)(y / scale));
}

void sdl_input_common_destroy(SdlInputCommon* self) {
    sdl_gamepads_destroy(&self->gamepads);

    for (int i = 0; i < FlCursorShape_Count; i++) {
        if (self->system_cursors[i]) {
            SDL_FreeCursor(self->system_cursors[i]);
            self->system_cursors[i] = nullptr;
        }
    }
}

#endif // BACKEND_USE_SDL
