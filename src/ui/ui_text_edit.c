#include "ui_text_edit.h"
#include <core/arena.h>
#include <core/memory.h>
#include "font_cache.h"
#include "ui_private.h"
#include "ui_text_buffer.h"
#include <ctype.h>
#include <string.h>

// All required macros and callbacks must be defined before including stb_textedit.h; see that header for
// their full documentation. The key codes live in ui_text_edit.h.

#define STB_TEXTEDIT_UNDOSTATECOUNT 99
#define STB_TEXTEDIT_UNDOCHARCOUNT 999
#define STB_TEXTEDIT_STRING FlTextInputState
#define STB_TEXTEDIT_KEYTYPE u32

#define STB_TEXTEDIT_NEWLINE '\n'
#define STB_TEXTEDIT_IS_SPACE(ch) isspace((unsigned char)(ch))

static int STB_TEXTEDIT_STRINGLEN(const FlTextInputState* state) {
    return (int)state->buffer_length;
}

static STB_TEXTEDIT_CHARTYPE STB_TEXTEDIT_GETCHAR(const FlTextInputState* state, int pos) {
    if (pos < 0 || pos >= (int)state->buffer_length) {
        return 0;
    }
    return state->buffer[pos];
}

// Font size in physical px: the cursor overlay and the manual scroll offset reach Clay in screen coordinates
// (layout.c hands manual scroll_pos through unscaled), so text_scale is applied to the font handle's authored size.
static u16 font_size_px(FlUiCtx* ctx) {
    if (!ctx->shared->font_cache) {
        return 16;
    }
    int size = font_cache_get_size(ctx->shared->font_cache, ctx->active_font);
    if (size <= 0) {
        size = 16;
    }
    const float text_scale = ctx->text_scale > 0.0f ? ctx->text_scale : 1.0f;
    return (u16)max((float)size * text_scale + 0.5f, 1.0f);
}

Vec2 ui_text_edit_measure(FlUiCtx* ctx, FlString text) {
    return font_cache_get_text_size(ctx->shared->font_cache, text, ctx->active_font, font_size_px(ctx));
}

static bool can_measure(FlUiCtx* ctx) {
    return ctx && ctx->shared->font_cache;
}

static float STB_TEXTEDIT_GETWIDTH(FlTextInputState* state, int line_start, int char_idx) {
    (void)line_start; // Single-line input, line_start unused
    if (char_idx < 0 || char_idx >= (int)state->buffer_length) {
        return 0.0f;
    }

    FlUiCtx* ctx = ui_get_ctx();
    if (!can_measure(ctx)) {
        return 8.0f; // Fallback width
    }

    FlString single_char = { .data = &state->buffer[char_idx], .length = 1, .is_static = 0 };
    return ui_text_edit_measure(ctx, single_char).x;
}

static void STB_TEXTEDIT_LAYOUTROW(StbTexteditRow* row, FlTextInputState* state, int line_start) {
    FlUiCtx* ctx = ui_get_ctx();
    const float font_height = can_measure(ctx) ? ui_text_edit_measure(ctx, S("M")).y : 16.0f;

    row->x0 = 0.0f;
    row->baseline_y_delta = font_height * 0.8f;
    row->ymin = 0.0f;
    row->ymax = font_height;
    row->num_chars = state->buffer_length - line_start;

    row->x1 = 0.0f;
    for_count(i, row->num_chars) {
        row->x1 += STB_TEXTEDIT_GETWIDTH(state, line_start, line_start + i);
    }
}

static int STB_TEXTEDIT_INSERTCHARS(FlTextInputState* state, int pos, const STB_TEXTEDIT_CHARTYPE* new_text, int len) {
    u32 new_length = state->buffer_length + len;
    text_buffer_grow(state->arena, &state->buffer, &state->buffer_capacity, state->buffer_length, new_length);

    if (pos < (int)state->buffer_length) {
        memmove(state->buffer + pos + len, state->buffer + pos, state->buffer_length - pos);
    }
    memory_copy(state->buffer + pos, state->buffer_capacity - pos, new_text, len);
    state->buffer_length += len;

    return 1;
}

static void STB_TEXTEDIT_DELETECHARS(FlTextInputState* state, int pos, int len) {
    if (pos < 0 || pos + len > (int)state->buffer_length) {
        return;
    }

    memmove(state->buffer + pos, state->buffer + pos + len, state->buffer_length - pos - len);
    state->buffer_length -= len;
}

// Convert keyboard input to character (-1 if not a character)
static int STB_TEXTEDIT_KEYTOTEXT(int key) {
    if (key & 0x10000) { // special-key marker bit
        return -1;
    }
    return key;
}

#define STB_TEXTEDIT_IMPLEMENTATION
// Suppress unused-function warnings for the stb functions this widget does not call.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stb_textedit.h"
#pragma GCC diagnostic pop

void ui_text_input_init(FlTextInputState* state, FlArena* arena) {
    memory_zero(state, sizeof(*state));
    state->arena = arena;
    state->buffer_capacity = 256;
    state->buffer = arena_alloc_array(arena, char, state->buffer_capacity);
    state->buffer_length = 0;
    state->buffer[0] = '\0';

    state->stb_state = (void*)arena_alloc_zero(arena, STB_TexteditState);
    stb_textedit_initialize_state(STB_STATE(state), 1); // 1 = single-line mode
}

FlTextInputState* ui_text_input_create(uint32_t capacity) {
    FlArena* arena = arena_new();
    FlTextInputState* state = arena_alloc_zero(arena, FlTextInputState);
    ui_text_input_init(state, arena);
    // Honour a larger requested capacity; ui_text_input_init's default is a floor for small values.
    if (capacity > state->buffer_capacity) {
        state->buffer = arena_alloc_array(arena, char, capacity);
        state->buffer[0] = '\0';
        state->buffer_capacity = capacity;
        state->buffer_length = 0;
    }
    return state;
}

void ui_text_input_destroy(FlTextInputState* state) {
    if (state) {
        fl_arena_destroy(state->arena);
    }
}

// Collapse the cursor and selection to one position.
static void place_cursor(FlTextInputState* state, int pos) {
    if (!state->stb_state) {
        return;
    }
    STB_STATE(state)->cursor = pos;
    STB_STATE(state)->select_start = pos;
    STB_STATE(state)->select_end = pos;
}

void ui_text_input_set_text(FlTextInputState* state, FlString text) {
    if (!state || !state->buffer) {
        return;
    }

    if (text.length >= state->buffer_capacity) {
        u32 new_capacity = (u32)text.length + 64;
        char* new_buffer = arena_alloc_array(state->arena, char, new_capacity);
        state->buffer = new_buffer;
        state->buffer_capacity = new_capacity;
    }

    if (text.length > 0 && text.data) {
        memory_copy(state->buffer, state->buffer_capacity, text.data, text.length);
    }
    state->buffer_length = (u32)text.length;
    state->buffer[state->buffer_length] = '\0';

    place_cursor(state, (int)text.length);
}

FlString ui_text_input_get_text(const FlTextInputState* state) {
    if (!state || !state->buffer) {
        return (FlString) { .data = nullptr, .length = 0, .is_static = 0 };
    }
    return (FlString) { .data = state->buffer, .length = state->buffer_length, .is_static = 0 };
}

void ui_text_input_clear(FlTextInputState* state) {
    if (!state || !state->buffer) {
        return;
    }
    state->buffer_length = 0;
    state->buffer[0] = '\0';
    place_cursor(state, 0);
}

void ui_text_edit_key(FlTextInputState* state, u32 key) {
    stb_textedit_key(state, state->stb_state, key);
    state->cursor_blink_t = 0.0f;
}

void ui_text_edit_select_all(FlTextInputState* state) {
    STB_STATE(state)->select_start = 0;
    STB_STATE(state)->select_end = (int)state->buffer_length;
    STB_STATE(state)->cursor = (int)state->buffer_length;
}

FlString ui_text_edit_selection(const FlTextInputState* state) {
    const int a = STB_STATE(state)->select_start;
    const int b = STB_STATE(state)->select_end;
    const int start = min(a, b);
    return (FlString) { .data = state->buffer + start, .length = (u64)(max(a, b) - start), .is_static = 0 };
}

void ui_text_edit_cut(FlTextInputState* state) {
    stb_textedit_cut(state, state->stb_state);
    state->cursor_blink_t = 0.0f;
}

// Bytes a paste may add: unlimited without a max_length, otherwise the room left once the selection is replaced.
static u32 paste_room(const FlTextInputState* state, const FlTextInputConfig* cfg) {
    if (cfg->max_length == 0) {
        return UINT32_MAX;
    }
    const u32 kept = state->buffer_length - (u32)ui_text_edit_selection(state).length;
    return cfg->max_length > kept ? cfg->max_length - kept : 0;
}

// Copy the characters of text that pass the allow-list into out, stopping at room. Returns the count copied.
static u32 filter_text(FlString text, FlString allowed_chars, char* out, u32 room) {
    u32 count = 0;
    for (u64 i = 0; i < text.length && count < room; i++) {
        if (text_buffer_filter_char(text.data[i], allowed_chars)) {
            out[count++] = text.data[i];
        }
    }
    return count;
}

bool ui_text_input_paste(FlTextInputState* state, const FlTextInputConfig* cfg, FlString text) {
    if (text.length == 0) {
        return false;
    }

    arena_scratch_auto(temp);
    char* accepted = arena_alloc_array(temp.arena, char, text.length);
    const u32 accepted_length = filter_text(text, cfg->allowed_chars, accepted, paste_room(state, cfg));
    if (accepted_length == 0) {
        return false;
    }

    stb_textedit_paste(state, state->stb_state, accepted, (int)accepted_length);
    state->cursor_blink_t = 0.0f;
    return true;
}
