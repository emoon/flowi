// Headless window backend: a fixed drawable size + neutral content scale, an injected-event queue the pump drains
// into each window's FlUiCtx, and recorded cursor / hidden-drag state for read-back.

#include "window_backend.h"

#include <core/arena.h>
#include <core/log.h>
#include <core/memory.h>
#include <core/os/os.h>
#include "ui/ui_private.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constants

#define MAX_QUEUED_EVENTS 256

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Event queue types

typedef enum QueuedEventType {
    QueuedEvent_Key,
    QueuedEvent_Mouse,
    QueuedEvent_Text,
    QueuedEvent_Window,
} QueuedEventType;

typedef struct QueuedEvent {
    QueuedEventType type;
    union {
        FlKeyEvent key;
        FlMouseEvent mouse;
        FlTextInputEvent text;
        FlWindowEvent window;
    };
    // Backing storage for the string-carrying variants (text input, dropped path); their FlString points here.
    char text_storage[1024];
} QueuedEvent;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-window handle - a fixed-size offscreen record with its own injected-event queue.

typedef struct FlWindow {
    int width;
    int height;

    // Content scale (physical px / logical px); 1.0 until a test overrides it.
    float content_scale;

    QueuedEvent events[MAX_QUEUED_EVENTS];
    u32 event_count;

    // The modifier mask held by the injected modifier keys, stamped onto each key event queued after them.
    u32 modifiers;
} FlWindow;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Headless Window Backend context

typedef struct HeadlessWindowContext {
    struct FlArena* arena;

    // Cursor + hidden-drag state the core flushed, recorded for test read-back.
    FlCursor last_cursor;
    bool cursor_visible;
    bool relative_mouse_mode;
    FlVec2 last_warp;
} HeadlessWindowContext;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Window lifecycle

static FlWindow* headless_create_window(void* ud, const FlWindowConfig* config) {
    HeadlessWindowContext* self = (HeadlessWindowContext*)ud;
    FlWindow* window = arena_alloc_zero(self->arena, FlWindow);
    window->width = config ? config->width : 0;
    window->height = config ? config->height : 0;
    window->content_scale = 1.0f;
    return window;
}

static void headless_destroy_window(void* ud, FlWindow* window) {
    UNUSED(ud);
    UNUSED(window);
    // Arena-allocated; nothing to free.
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Event pump - drain each window's injected queue into its own context

static void headless_drain_window(FlWindow* window, FlUiCtx* ctx) {
    for (u32 i = 0; i < window->event_count; i++) {
        QueuedEvent* qe = &window->events[i];
        switch (qe->type) {
            case QueuedEvent_Key:
                fl_platform_input_push_key_event(ctx, qe->key);
                break;
            case QueuedEvent_Mouse:
                fl_platform_input_push_mouse_event(ctx, qe->mouse);
                break;
            case QueuedEvent_Text:
                fl_platform_input_push_text_input_event(ctx, qe->text);
                break;
            case QueuedEvent_Window:
                fl_platform_input_push_window_event(ctx, qe->window);
                break;
        }
    }
    window->event_count = 0;
}

static void headless_pump_events(void* ud, FlWindow** windows, FlUiCtx** contexts, int count) {
    UNUSED(ud);
    for (int i = 0; i < count; i++) {
        if (windows[i] != nullptr && contexts[i] != nullptr) {
            headless_drain_window(windows[i], contexts[i]);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Drawable size + content scale

static void headless_drawable_size(void* ud, FlWindow* window, int* width, int* height) {
    UNUSED(ud);
    if (window == nullptr) {
        return;
    }
    if (width) {
        *width = window->width;
    }
    if (height) {
        *height = window->height;
    }
}

static float headless_content_scale(void* ud, FlWindow* window) {
    UNUSED(ud);
    if (window == nullptr) {
        return 1.0f;
    }
    return window->content_scale;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cursor + hidden-drag - record the last-requested state

static void headless_set_cursor(void* ud, FlWindow* window, FlCursor cursor) {
    UNUSED(window);
    HeadlessWindowContext* self = (HeadlessWindowContext*)ud;
    self->last_cursor = cursor;
}

static void headless_set_cursor_visible(void* ud, FlWindow* window, bool visible) {
    UNUSED(window);
    HeadlessWindowContext* self = (HeadlessWindowContext*)ud;
    self->cursor_visible = visible;
}

static void headless_set_relative_mouse_mode(void* ud, FlWindow* window, bool enabled) {
    UNUSED(window);
    HeadlessWindowContext* self = (HeadlessWindowContext*)ud;
    self->relative_mouse_mode = enabled;
}

static void headless_warp_mouse(void* ud, FlWindow* window, float x, float y) {
    UNUSED(window);
    HeadlessWindowContext* self = (HeadlessWindowContext*)ud;
    self->last_warp = (FlVec2) { .x = x, .y = y };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Presentation - no-op; the signature matches the seam's present slot (void* id<MTLTexture> on APPLE, u32 GL name
// elsewhere).
#if __APPLE__
static void headless_present(void* ud, FlWindow* window, void* surface_texture, u32 tex_width, u32 tex_height) {
#else
static void headless_present(void* ud, FlWindow* window, u32 surface_texture, u32 tex_width, u32 tex_height) {
#endif
    UNUSED(ud);
    UNUSED(window);
    UNUSED(surface_texture);
    UNUSED(tex_width);
    UNUSED(tex_height);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void headless_destroy(void* ud) {
    UNUSED(ud);
    // Arena-allocated; nothing to free.
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory

FlWindowBackend headless_window_backend_create(struct FlArena* arena) {
    HeadlessWindowContext* self = arena_alloc_zero(arena, HeadlessWindowContext);
    self->arena = arena;
    self->last_cursor = (FlCursor) { .shape = FlCursorShape_Arrow };
    self->cursor_visible = true;
    self->relative_mouse_mode = false;
    self->last_warp = (FlVec2) { .x = 0.0f, .y = 0.0f };

    log_info("Window Backend: Headless initialized");

    return (FlWindowBackend) {
        .user_data = self,
        .create_window = headless_create_window,
        .destroy_window = headless_destroy_window,
        .pump_events = headless_pump_events,
        .drawable_size = headless_drawable_size,
        .content_scale = headless_content_scale,
        .set_cursor = headless_set_cursor,
        .set_cursor_visible = headless_set_cursor_visible,
        .set_relative_mouse_mode = headless_set_relative_mouse_mode,
        .warp_mouse = headless_warp_mouse,
        .present = headless_present,
        .destroy = headless_destroy,
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void queue_event(FlWindow* window, QueuedEvent event) {
    if (window == nullptr) {
        return;
    }
    if (window->event_count >= MAX_QUEUED_EVENTS) {
        log_warning("Headless window event queue full, dropping event");
        return;
    }
    window->events[window->event_count++] = event;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test harness - inject events the next pump_events drains into the given window's FlUiCtx

void window_backend_headless_inject_mouse_move(FlWindowBackend* backend, FlWindow* window, i32 x, i32 y) {
    UNUSED(backend);
    u64 timestamp = get_current_time_ms_os();
    QueuedEvent event = { .type = QueuedEvent_Mouse,
                          .mouse = { .type = FlMouseEvent_Move,
                                     .move = { .x = (float)x, .y = (float)y, .delta_x = 0, .delta_y = 0 },
                                     .timestamp = timestamp } };
    queue_event(window, event);
}

void window_backend_headless_inject_mouse_button(FlWindowBackend* backend, FlWindow* window, FlMouseButton button,
                                                 bool down) {
    UNUSED(backend);
    if (button < 0 || button >= FlMouseButton_Count) {
        return;
    }
    u64 timestamp = get_current_time_ms_os();
    QueuedEvent event = { .type = QueuedEvent_Mouse,
                          .mouse = { .type = down ? FlMouseEvent_Button_Down : FlMouseEvent_Button_Up,
                                     .button = { .button = button, .x = 0, .y = 0 },
                                     .timestamp = timestamp } };
    queue_event(window, event);
}

void window_backend_headless_inject_mouse_delta(FlWindowBackend* backend, FlWindow* window, f32 delta_x, f32 delta_y) {
    UNUSED(backend);
    u64 timestamp = get_current_time_ms_os();
    QueuedEvent event = { .type = QueuedEvent_Mouse,
                          .mouse = { .type = FlMouseEvent_Relative_Motion,
                                     .relative = { .delta_x = delta_x, .delta_y = delta_y },
                                     .timestamp = timestamp } };
    queue_event(window, event);
}

void window_backend_headless_inject_wheel(FlWindowBackend* backend, FlWindow* window, f32 x, f32 y) {
    UNUSED(backend);
    u64 timestamp = get_current_time_ms_os();
    QueuedEvent event
        = { .type = QueuedEvent_Mouse,
            .mouse = { .type = FlMouseEvent_Wheel, .wheel = { .x = x, .y = y }, .timestamp = timestamp } };
    queue_event(window, event);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The modifier bit a key carries, or zero for a key that is not a modifier.

static u32 headless_modifier_bit(FlKey key) {
    switch (key) {
        case FlKey_Left_Shift:
        case FlKey_Right_Shift:
            return FlKeyModifier_Shift;
        case FlKey_Left_Control:
        case FlKey_Right_Control:
            return FlKeyModifier_Control;
        case FlKey_Left_Alt:
        case FlKey_Right_Alt:
            return FlKeyModifier_Alt;
        case FlKey_Left_Super:
        case FlKey_Right_Super:
            return FlKeyModifier_Super;
        default:
            return 0;
    }
}

void window_backend_headless_inject_key(FlWindowBackend* backend, FlWindow* window, FlKey key, bool down) {
    UNUSED(backend);
    if (key < 0 || key >= FlKey_Count) {
        return;
    }
    if (window != nullptr) {
        u32 bit = headless_modifier_bit(key);
        if (down) {
            window->modifiers |= bit;
        } else {
            window->modifiers &= ~bit;
        }
    }
    u64 timestamp = get_current_time_ms_os();
    QueuedEvent event = { .type = QueuedEvent_Key,
                          .key = { .type = down ? FlKeyEvent_Down : FlKeyEvent_Up,
                                   .key = key,
                                   .modifiers = window != nullptr ? window->modifiers : 0,
                                   .repeat = false,
                                   .timestamp = timestamp } };
    queue_event(window, event);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Claim the next queue slot and copy src into its text_storage, truncated to fit and null-terminated. Returns null
// when window is null or its queue is full; *out_len is the copied byte count.

static QueuedEvent* queue_slot_with_string(FlWindow* window, FlString src, u64* out_len) {
    if (window == nullptr) {
        return nullptr;
    }
    if (window->event_count >= MAX_QUEUED_EVENTS) {
        log_warning("Headless window event queue full, dropping event");
        return nullptr;
    }
    QueuedEvent* slot = &window->events[window->event_count++];
    u64 copy_len = src.length < sizeof(slot->text_storage) - 1 ? src.length : sizeof(slot->text_storage) - 1;
    memory_copy(slot->text_storage, sizeof(slot->text_storage), src.data, copy_len);
    slot->text_storage[copy_len] = '\0';
    *out_len = copy_len;
    return slot;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void window_backend_headless_inject_text(FlWindowBackend* backend, FlWindow* window, FlString text) {
    UNUSED(backend);
    u64 copy_len = 0;
    QueuedEvent* slot = queue_slot_with_string(window, text, &copy_len);
    if (slot == nullptr) {
        return;
    }

    slot->type = QueuedEvent_Text;
    slot->text = (FlTextInputEvent) { .text = { .data = slot->text_storage, .length = copy_len, .is_static = 1 },
                                      .timestamp = get_current_time_ms_os() };
}

void window_backend_headless_inject_drop_file(FlWindowBackend* backend, FlWindow* window, FlString path, f32 x, f32 y) {
    UNUSED(backend);
    u64 copy_len = 0;
    QueuedEvent* slot = queue_slot_with_string(window, path, &copy_len);
    if (slot == nullptr) {
        return;
    }

    slot->type = QueuedEvent_Window;
    slot->window = (FlWindowEvent) {
        .type = FlWindowEvent_Drop_File,
        .drop = { .path = { .data = slot->text_storage, .length = copy_len, .is_static = 1 }, .x = x, .y = y },
        .timestamp = get_current_time_ms_os(),
    };
}

void window_backend_headless_inject_window_close(FlWindowBackend* backend, FlWindow* window) {
    UNUSED(backend);
    u64 timestamp = get_current_time_ms_os();
    QueuedEvent event
        = { .type = QueuedEvent_Window, .window = { .type = FlWindowEvent_Close, .timestamp = timestamp } };
    queue_event(window, event);
}

void window_backend_headless_inject_window_focus(FlWindowBackend* backend, FlWindow* window, bool gained) {
    UNUSED(backend);
    u64 timestamp = get_current_time_ms_os();
    QueuedEvent event = { .type = QueuedEvent_Window,
                          .window = { .type = gained ? FlWindowEvent_Focus_Gained : FlWindowEvent_Focus_Lost,
                                      .timestamp = timestamp } };
    queue_event(window, event);
}

void window_backend_headless_set_content_scale(FlWindow* window, float scale) {
    if (window == nullptr) {
        return;
    }
    window->content_scale = scale;
}

FlString window_backend_headless_queued_text(FlWindow* window, u32 index) {
    if (window == nullptr || index >= window->event_count || window->events[index].type != QueuedEvent_Text) {
        return (FlString) { 0 };
    }
    return window->events[index].text.text;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Test harness - read back the cursor / hidden-drag state the core flushed

FlCursor window_backend_headless_last_cursor(FlWindowBackend* backend) {
    HeadlessWindowContext* self = (HeadlessWindowContext*)backend->user_data;
    return self->last_cursor;
}

bool window_backend_headless_cursor_visible(FlWindowBackend* backend) {
    HeadlessWindowContext* self = (HeadlessWindowContext*)backend->user_data;
    return self->cursor_visible;
}

bool window_backend_headless_relative_mouse_mode(FlWindowBackend* backend) {
    HeadlessWindowContext* self = (HeadlessWindowContext*)backend->user_data;
    return self->relative_mouse_mode;
}

FlVec2 window_backend_headless_last_warp(FlWindowBackend* backend) {
    HeadlessWindowContext* self = (HeadlessWindowContext*)backend->user_data;
    return self->last_warp;
}
