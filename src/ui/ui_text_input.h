#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Text Input Widget - Internal API
//
// Single-line editing built on stb_textedit.h: cursor + blink, selection (mouse drag, shift+arrows),
// clipboard, undo/redo, horizontal scrolling, placeholder text and a character counter.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <core/core.h>
#include <core/string.h>
#include <flowi/ui/signal.h>        // @generated FlSignal (api_gen)
#include <flowi/ui/widget_config.h> // @generated FlTextInputConfig (api_gen)
#include <ui/ui_types.h>
#include <stdbool.h>

// Include stb_textedit in header-only mode to get type definitions
#define STB_TEXTEDIT_CHARTYPE char
#define STB_TEXTEDIT_POSITIONTYPE int
#include "stb_textedit.h"

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Forward declarations

typedef struct FlArena FlArena;

// Editor state; the public API declares FlTextInputState opaquely.
struct FlTextInputState {
    void* stb_state;
    char* buffer;
    u32 buffer_capacity;
    u32 buffer_length;
    f32 cursor_blink_t;
    f32 scroll_offset_x;
    f32 target_scroll_x;
    bool was_focused_last_frame;
    u64 focus_gained_frame;
    FlArena* arena;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper Functions

// Initialize text input state (call once before first use)
void ui_text_input_init(FlTextInputState* state, FlArena* arena);

// Create a handle backed by its own arena; capacity is the requested initial buffer size in bytes (a built-in
// floor applies for small values). Pair with ui_text_input_destroy.
FlTextInputState* ui_text_input_create(uint32_t capacity);

// Free a handle from ui_text_input_create along with its arena. No-op on nullptr.
void ui_text_input_destroy(FlTextInputState* state);

void ui_text_input_set_text(FlTextInputState* state, FlString text);

FlString ui_text_input_get_text(const FlTextInputState* state);

void ui_text_input_clear(FlTextInputState* state);

// Insert text at the caret, replacing the selection. Characters cfg rejects are dropped and the rest is cut to fit
// cfg->max_length. Returns false, leaving the buffer and selection untouched, when nothing is left to insert.
bool ui_text_input_paste(FlTextInputState* state, const FlTextInputConfig* cfg, FlString text);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main Widget Function

// Signal flags:
//   .clicked        - Input was clicked with mouse
//   .focused        - Input has keyboard focus
//   .hovered        - Mouse is over input
//   .text_changed   - Text was modified this frame
//   .submitted      - User pressed Enter this frame
//   .cancelled      - User pressed ESC this frame
FlSignal ui_text_input(FlString label, FlTextInputState* state, FlTextInputConfig cfg);

#ifdef __cplusplus
}
#endif
