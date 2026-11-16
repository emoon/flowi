#include "ui_input.h"
#include <platform/input_types.h>
#include <core/log.h>
#include "ui_private.h"
#include <string.h>

// Copy one dropped path into the frame arena and record it.
static void push_dropped_file(FlUiCtx* ctx, WindowEventState* window_events, FlWindowDropData drop) {
    if (drop.path.length == 0 || drop.path.data == nullptr) {
        return;
    }

    if (window_events->dropped_file_count >= UI_DROPPED_FILES_CAP) {
        log_warning("Dropped-file queue full, ignoring %.*s", (int)drop.path.length, drop.path.data);
        return;
    }
    char* path_copy = arena_alloc_array(ctx->render_frame_allocator, char, drop.path.length + 1);
    memcpy(path_copy, drop.path.data, drop.path.length);
    path_copy[drop.path.length] = '\0';

    u32 slot = window_events->dropped_file_count++;
    window_events->dropped_files[slot] = (FlString) { .data = path_copy, .length = drop.path.length, .is_static = 0 };
    window_events->dropped_file_positions[slot] = (FlVec2) { .x = drop.x, .y = drop.y };
}

void platform_input_push_window_event(FlUiCtx* ctx, FlWindowEvent event) {
    FlInputState* state = &ctx->input_state;
    WindowEventState* window_events = &state->window_events;

    switch (event.type) {
        case FlWindowEvent_None:
            break;

        case FlWindowEvent_Focus_Gained:
            state->window_focused = true;
            window_events->focus_gained = true;
            break;

        case FlWindowEvent_Focus_Lost:
            state->window_focused = false;
            window_events->focus_lost = true;
            break;

        case FlWindowEvent_Resize:
            window_events->resized = true;
            break;

        case FlWindowEvent_Close:
            state->quit_requested = true;
            window_events->close_requested = true;
            break;

        case FlWindowEvent_Drop_File:
            push_dropped_file(ctx, window_events, event.drop);
            break;
    }
}
