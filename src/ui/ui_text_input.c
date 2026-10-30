#include "ui_text_input.h"
#include <core/arena.h>
#include <platform/clipboard.h>
#include <core/log.h>
#include <core/memory.h>
#include <core/sprintf.h>
#include "external/clay/clay.h"
#include "font_cache.h"
#include "input_actions.h"
#include <ui/ui_theme.h>
#include <ui/ui_types.h>
#include "ui.h"
#include "ui_private.h"
#include "ui_text_buffer.h"
#include "ui_tokens.h"
#include "ui_variant_helpers.h"
#include "ui_virtual_keyboard.h"
#include <ctype.h>
#include <math.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// stb_textedit integration
//
// All required macros and callbacks must be defined before including stb_textedit.h; see that header for
// their full documentation.

#define STB_TEXTEDIT_UNDOSTATECOUNT 99
#define STB_TEXTEDIT_UNDOCHARCOUNT 999
#define STB_TEXTEDIT_STRING FlTextInputState
#define STB_TEXTEDIT_KEYTYPE u32

// Key definitions for stb_textedit (bit flags, can be combined with shift/ctrl)
#define STB_TEXTEDIT_K_SHIFT 0x400000
#define STB_TEXTEDIT_K_CONTROL 0x200000
#define STB_TEXTEDIT_K_LEFT (0x10000 | 1)
#define STB_TEXTEDIT_K_RIGHT (0x10000 | 2)
#define STB_TEXTEDIT_K_UP (0x10000 | 3)
#define STB_TEXTEDIT_K_DOWN (0x10000 | 4)
#define STB_TEXTEDIT_K_LINESTART (0x10000 | 5)
#define STB_TEXTEDIT_K_LINEEND (0x10000 | 6)
#define STB_TEXTEDIT_K_TEXTSTART (0x10000 | 7)
#define STB_TEXTEDIT_K_TEXTEND (0x10000 | 8)
#define STB_TEXTEDIT_K_DELETE (0x10000 | 9)
#define STB_TEXTEDIT_K_BACKSPACE (0x10000 | 10)
#define STB_TEXTEDIT_K_UNDO (0x10000 | 11)
#define STB_TEXTEDIT_K_REDO (0x10000 | 12)
#define STB_TEXTEDIT_K_WORDLEFT (0x10000 | 13)
#define STB_TEXTEDIT_K_WORDRIGHT (0x10000 | 14)
#define STB_TEXTEDIT_K_PGUP (0x10000 | 15)
#define STB_TEXTEDIT_K_PGDOWN (0x10000 | 16)

#define STB_TEXTEDIT_NEWLINE '\n'
#define STB_TEXTEDIT_IS_SPACE(ch) isspace((unsigned char)(ch))

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int STB_TEXTEDIT_STRINGLEN(const FlTextInputState* state) {
    return (int)state->buffer_length;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static STB_TEXTEDIT_CHARTYPE STB_TEXTEDIT_GETCHAR(const FlTextInputState* state, int pos) {
    if (pos < 0 || pos >= (int)state->buffer_length) {
        return 0;
    }
    return state->buffer[pos];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers: the two scales this widget works in
//
// Everything this file measures or positions ends up in screen coordinates, reached by two different
// multipliers: type (glyph sizes, text widths) carries text_scale, chrome (padding, heights, hairlines)
// carries ui_scale. The two are independent and are applied separately, never folded into one factor.

// Font size in physical px: the cursor overlay and the manual scroll offset reach Clay in screen coordinates
// (layout.c hands manual scroll_pos through unscaled), so text_scale is applied to the font handle's authored size.
static inline u16 get_current_font_size(FlUiCtx* ctx) {
    if (!ctx || !ctx->shared->font_cache) {
        return 16; // Fallback
    }
    int size = font_cache_get_size(ctx->shared->font_cache, ctx->active_font);
    if (size <= 0) {
        size = 16;
    }
    const float text_scale = ctx->text_scale > 0.0f ? ctx->text_scale : 1.0f;
    return (u16)max((float)size * text_scale + 0.5f, 1.0f);
}

// The chrome half: the ui_scale applied to the cursor's authored lengths (the cursor is configured through Clay
// directly, not through fl_floating_configure).
static inline float ui_scale_of(FlUiCtx* ctx) {
    return (ctx && ctx->ui_scale > 0.0f) ? ctx->ui_scale : 1.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static float STB_TEXTEDIT_GETWIDTH(FlTextInputState* state, int line_start, int char_idx) {
    (void)line_start; // Single-line input, line_start unused
    if (char_idx < 0 || char_idx >= (int)state->buffer_length) {
        return 0.0f;
    }

    FlUiCtx* ctx = ui_get_ctx();
    if (!ctx || !ctx->shared->font_cache) {
        return 8.0f; // Fallback width
    }

    u16 font_size = get_current_font_size(ctx);
    char c = state->buffer[char_idx];
    FlString single_char = { .data = &c, .length = 1, .is_static = 0 };
    Vec2 size = font_cache_get_text_size(ctx->shared->font_cache, single_char, ctx->active_font, font_size);
    return size.x;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void STB_TEXTEDIT_LAYOUTROW(StbTexteditRow* row, FlTextInputState* state, int line_start) {
    FlUiCtx* ctx = ui_get_ctx();
    float font_height = 16.0f;

    if (ctx && ctx->shared->font_cache) {
        u16 font_size = get_current_font_size(ctx);
        FlString test_str = { .data = "M", .length = 1, .is_static = 1 };
        Vec2 size = font_cache_get_text_size(ctx->shared->font_cache, test_str, ctx->active_font, font_size);
        font_height = size.y;
    }

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void STB_TEXTEDIT_DELETECHARS(FlTextInputState* state, int pos, int len) {
    if (pos < 0 || pos + len > (int)state->buffer_length) {
        return;
    }

    memmove(state->buffer + pos, state->buffer + pos + len, state->buffer_length - pos - len);
    state->buffer_length -= len;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#define STB_STATE(state) ((STB_TexteditState*)(state)->stb_state)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper Functions

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline float measure_text_to_pos(FlFontCache* font_cache, FlFontId font, u16 font_size, const char* buffer,
                                        u64 pos) {
    if (pos == 0) {
        return 0.0f;
    }
    FlString text = { .data = buffer, .length = pos, .is_static = 0 };
    Vec2 size = font_cache_get_text_size(font_cache, text, font, font_size);
    return size.x;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_text_input_destroy(FlTextInputState* state) {
    if (state) {
        fl_arena_destroy(state->arena);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

    // Cursor and selection collapse to the end of the new text.
    if (state->stb_state) {
        STB_STATE(state)->cursor = (int)text.length;
        STB_STATE(state)->select_start = (int)text.length;
        STB_STATE(state)->select_end = (int)text.length;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString ui_text_input_get_text(const FlTextInputState* state) {
    if (!state || !state->buffer) {
        return (FlString) { .data = nullptr, .length = 0, .is_static = 0 };
    }
    return (FlString) { .data = state->buffer, .length = state->buffer_length, .is_static = 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_text_input_clear(FlTextInputState* state) {
    if (!state || !state->buffer) {
        return;
    }
    state->buffer_length = 0;
    state->buffer[0] = '\0';
    if (state->stb_state) {
        STB_STATE(state)->cursor = 0;
        STB_STATE(state)->select_start = 0;
        STB_STATE(state)->select_end = 0;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ui_text_input_paste(FlTextInputState* state, const FlTextInputConfig* cfg, FlString text) {
    if (text.length == 0) {
        return false;
    }

    const int sel_start = STB_STATE(state)->select_start;
    const int sel_end = STB_STATE(state)->select_end;
    const u32 replaced = (u32)(sel_start > sel_end ? sel_start - sel_end : sel_end - sel_start);

    u32 capacity = UINT32_MAX;
    if (cfg->max_length > 0) {
        const u32 kept = state->buffer_length - replaced;
        capacity = cfg->max_length > kept ? cfg->max_length - kept : 0;
    }

    arena_scratch_auto(temp);
    char* accepted = arena_alloc_array(temp.arena, char, text.length);
    u32 accepted_length = 0;
    for (u64 i = 0; i < text.length && accepted_length < capacity; i++) {
        if (text_buffer_filter_char(text.data[i], cfg->allowed_chars)) {
            accepted[accepted_length++] = text.data[i];
        }
    }

    if (accepted_length == 0) {
        return false;
    }

    stb_textedit_paste(state, state->stb_state, accepted, (int)accepted_length);
    state->cursor_blink_t = 0.0f;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void handle_text_input_keyboard(FlTextInputState* state, FlUiCtx* ctx, FlTextInputConfig* cfg,
                                       bool* text_changed, bool* submitted, bool* cancelled) {
    FlInputState* input = &ctx->input_state;
    KeyboardState* kb = &input->keyboard;

    // Read the per-frame snapshot, not the raw kb->text_input, which fl_platform_input_update has already
    // cleared by the time the frame runs.
    FlString typed = ctx->text_input_this_frame;
    if (typed.length > 0) {
        for (u64 i = 0; i < typed.length; i++) {
            char c = typed.data[i];

            if (cfg->max_length > 0 && state->buffer_length >= cfg->max_length) {
                continue;
            }

            if (!text_buffer_filter_char(c, cfg->allowed_chars)) {
                continue;
            }

            stb_textedit_key(state, state->stb_state, (u32)c);
            *text_changed = true;
            state->cursor_blink_t = 0.0f;
        }
    }

    u32 modifiers = kb->modifiers;
    bool shift = (modifiers & FlKeyModifier_Shift) != 0;
    u32 shift_flag = shift ? STB_TEXTEDIT_K_SHIFT : 0;

    // Navigation - cursor movement (action press OR software repeat while held)
    if (ui_action_pressed(ctx, FlAction_Text_Move_Left) || ui_action_repeated(ctx, FlAction_Text_Move_Left)) {
        stb_textedit_key(state, state->stb_state, STB_TEXTEDIT_K_LEFT | shift_flag);
        // Consume left navigation to prevent widget navigation while editing text
        ctx->nav_frame.nav_left = false;
        state->cursor_blink_t = 0.0f;
    }
    if (ui_action_pressed(ctx, FlAction_Text_Move_Right) || ui_action_repeated(ctx, FlAction_Text_Move_Right)) {
        stb_textedit_key(state, state->stb_state, STB_TEXTEDIT_K_RIGHT | shift_flag);
        // Consume right navigation to prevent widget navigation while editing text
        ctx->nav_frame.nav_right = false;
        state->cursor_blink_t = 0.0f;
    }
    if (ui_action_pressed(ctx, FlAction_Text_Move_Word_Left) || ui_action_repeated(ctx, FlAction_Text_Move_Word_Left)) {
        stb_textedit_key(state, state->stb_state, STB_TEXTEDIT_K_WORDLEFT | shift_flag);
        // Consume left navigation (Ctrl+Left also uses left arrow)
        ctx->nav_frame.nav_left = false;
        state->cursor_blink_t = 0.0f;
    }
    if (ui_action_pressed(ctx, FlAction_Text_Move_Word_Right)
        || ui_action_repeated(ctx, FlAction_Text_Move_Word_Right)) {
        stb_textedit_key(state, state->stb_state, STB_TEXTEDIT_K_WORDRIGHT | shift_flag);
        // Consume right navigation (Ctrl+Right also uses right arrow)
        ctx->nav_frame.nav_right = false;
        state->cursor_blink_t = 0.0f;
    }

    if (ui_action_pressed(ctx, FlAction_Text_Home)) {
        stb_textedit_key(state, state->stb_state, STB_TEXTEDIT_K_LINESTART | shift_flag);
        state->cursor_blink_t = 0.0f;
    }
    if (ui_action_pressed(ctx, FlAction_Text_End)) {
        stb_textedit_key(state, state->stb_state, STB_TEXTEDIT_K_LINEEND | shift_flag);
        state->cursor_blink_t = 0.0f;
    }

    // Backspace/Delete (action press OR software repeat while held)
    if (ui_action_pressed(ctx, FlAction_Text_Backspace) || ui_action_repeated(ctx, FlAction_Text_Backspace)) {
        stb_textedit_key(state, state->stb_state, STB_TEXTEDIT_K_BACKSPACE);
        *text_changed = true;
        state->cursor_blink_t = 0.0f;
    }
    if (ui_action_pressed(ctx, FlAction_Text_Delete) || ui_action_repeated(ctx, FlAction_Text_Delete)) {
        stb_textedit_key(state, state->stb_state, STB_TEXTEDIT_K_DELETE);
        *text_changed = true;
        state->cursor_blink_t = 0.0f;
    }

    if (ui_action_pressed(ctx, FlAction_Text_Undo)) {
        stb_textedit_key(state, state->stb_state, STB_TEXTEDIT_K_UNDO);
        *text_changed = true;
        state->cursor_blink_t = 0.0f;
    }
    if (ui_action_pressed(ctx, FlAction_Text_Redo)) {
        stb_textedit_key(state, state->stb_state, STB_TEXTEDIT_K_REDO);
        *text_changed = true;
        state->cursor_blink_t = 0.0f;
    }

    if (ui_action_pressed(ctx, FlAction_Text_Select_All)) {
        STB_STATE(state)->select_start = 0;
        STB_STATE(state)->select_end = (int)state->buffer_length;
        STB_STATE(state)->cursor = (int)state->buffer_length;
    }

    if (ui_action_pressed(ctx, FlAction_Text_Copy)) {
        int start = STB_STATE(state)->select_start;
        int end = STB_STATE(state)->select_end;
        if (start != end) {
            // Normalize selection
            if (start > end) {
                int tmp = start;
                start = end;
                end = tmp;
            }
            FlString selected = { .data = state->buffer + start, .length = (u64)(end - start), .is_static = 0 };
            backend_set_clipboard_text(selected);
        }
    }

    if (ui_action_pressed(ctx, FlAction_Text_Cut)) {
        int start = STB_STATE(state)->select_start;
        int end = STB_STATE(state)->select_end;
        if (start != end) {
            // Normalize selection
            if (start > end) {
                int tmp = start;
                start = end;
                end = tmp;
            }
            FlString selected = { .data = state->buffer + start, .length = (u64)(end - start), .is_static = 0 };
            backend_set_clipboard_text(selected);
            stb_textedit_cut(state, state->stb_state);
            *text_changed = true;
            state->cursor_blink_t = 0.0f;
        }
    }

    if (ui_action_pressed(ctx, FlAction_Text_Paste)) {
        FlString clipboard_text = backend_get_clipboard_text(ctx->render_frame_allocator);
        if (ui_text_input_paste(state, cfg, clipboard_text)) {
            *text_changed = true;
        }
    }

    if (ui_key_pressed(FlKey_Enter) || ui_key_pressed(FlKey_Kp_Enter)) {
        *submitted = true;
    }
    if (ui_action_pressed(ctx, FlAction_Cancel)) {
        *cancelled = true;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void render_text_input(FlTextInputState* state, FlTextInputConfig* cfg, FlUiCtx* ctx, bool is_focused,
                              float visible_width) {
    const struct FlThemeColors* theme = ui_get_theme_colors(ctx);

    if (state->buffer_length == 0 && cfg->placeholder.length > 0) {
        ui_text_styled(cfg->placeholder, .color = theme->text.secondary, .valign = FlTextVAlign_Center);
    } else if (state->buffer_length > 0) {
        FlString text = { .data = state->buffer, .length = state->buffer_length, .is_static = 0 };

        // Calculate scroll offset to keep cursor visible
        if (is_focused && state->stb_state) {
            STB_TexteditState* stb = (STB_TexteditState*)state->stb_state;
            u64 cursor_pos = (u64)stb->cursor;
            u16 font_size = get_current_font_size(ctx);

            float cursor_x
                = measure_text_to_pos(ctx->shared->font_cache, ctx->active_font, font_size, state->buffer, cursor_pos);

            // The scroll offset and cursor_x are in screen coordinates (Clay takes manual scroll_pos unscaled), so
            // the authored viewport width and margin are scaled into the same space.
            const float scale = ui_scale_of(ctx);
            const float visible_px = visible_width * scale;
            const float margin_px = 10.0f * scale;

            float cursor_screen_x = cursor_x - state->scroll_offset_x;

            // Scroll right if cursor is past right edge (leave a 10px authored margin)
            if (cursor_screen_x > visible_px - margin_px) {
                state->target_scroll_x = cursor_x - visible_px + margin_px;
            }
            // Scroll left if cursor is past left edge
            else if (cursor_screen_x < 0.0f) {
                state->target_scroll_x = cursor_x;
            }

            Vec2 text_size = font_cache_get_text_size(ctx->shared->font_cache, text, ctx->active_font, font_size);
            float max_scroll = text_size.x - visible_px;
            if (max_scroll < 0.0f)
                max_scroll = 0.0f;

            if (state->target_scroll_x < 0.0f)
                state->target_scroll_x = 0.0f;
            if (state->target_scroll_x > max_scroll)
                state->target_scroll_x = max_scroll;

            // Smooth scroll interpolation
            state->scroll_offset_x += (state->target_scroll_x - state->scroll_offset_x) * 0.3f;
        }

        // TODO: Render selection highlight as a colored rectangle behind text

        // Wrap text in a growing container to prevent it from affecting parent layout
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {
            ui_text_styled(text, .color = theme->text.primary, .valign = FlTextVAlign_Center);
        }
    }

    if (cfg->show_character_count && cfg->max_length > 0) {
        arena_scratch_auto(temp);
        FlString counter = fl_sprintf_arena(temp.arena, "%u/%u", state->buffer_length, cfg->max_length);
        ui_text_styled(counter, .color = theme->text.secondary, .valign = FlTextVAlign_Center);
    }
}

// Render the blinking text cursor as a floating element attached to the input container.
// Caller is responsible for the focus/stb_state guard before invoking.
static void render_text_cursor(FlTextInputState* state, FlUiCtx* ctx, u32 clay_id, float inner_padding,
                               float inner_height) {
    const struct FlThemeColors* theme = ui_get_theme_colors(ctx);

    state->cursor_blink_t += ctx->delta_time;

    STB_TexteditState* stb = (STB_TexteditState*)state->stb_state;
    u64 cursor_pos = (u64)stb->cursor;
    u16 font_size = get_current_font_size(ctx);

    float cursor_x
        = measure_text_to_pos(ctx->shared->font_cache, ctx->active_font, font_size, state->buffer, cursor_pos);

    // 1.0 second blink period, on for the first half.
    float blink_phase = fmodf(state->cursor_blink_t, 1.0f);
    bool cursor_visible = (blink_phase < 0.5f);

    if (cursor_visible) {
        // Everything below is screen coordinates. cursor_x and the font metrics are measured at the scaled size;
        // inner_padding, inner_height and the hairline constants are authored lengths and are scaled here.
        const float scale = ui_scale_of(ctx);

        float cursor_screen_x = inner_padding * scale + cursor_x - state->scroll_offset_x;

        Vec2 font_metrics = font_cache_get_text_size(ctx->shared->font_cache, (FlString) { .data = "M", .length = 1 },
                                                     ctx->active_font, font_size);

        // Slightly taller than the text, offset 1px up so it extends 1px above and 3px below.
        float cursor_height = font_metrics.y + 4.0f * scale;
        float text_center_y = (inner_height * scale - font_metrics.y) / 2.0f;
        float cursor_y = text_center_y - 1.0f * scale;
        float cursor_width = 2.0f * scale;

        // Unique per-frame id for the cursor element.
        u32 cursor_element_id_value = 0xFFFF0000 + (u32)(ctx->frame_counter & 0xFFFF);
        Clay_ElementId cursor_element_id = { .id = cursor_element_id_value };
        Clay__OpenElementWithId(cursor_element_id);

        Clay_ElementDeclaration cursor_decl
            = { .layout
                = { .sizing = { .width = { .type = CLAY__SIZING_TYPE_FIXED,
                                           .size = { .minMax = { .min = cursor_width, .max = cursor_width } } },
                                .height = { .type = CLAY__SIZING_TYPE_FIXED,
                                            .size = { .minMax = { .min = cursor_height, .max = cursor_height } } } } },
                .backgroundColor = (Clay_Color) { theme->text.primary.r, theme->text.primary.g, theme->text.primary.b,
                                                  theme->text.primary.a },

                .floating
                = { .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID,
                    .parentId = clay_id,
                    .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP },
                    .offset = { .x = cursor_screen_x, .y = cursor_y },
                    .zIndex = 10 } };

        Clay__ConfigureOpenElement(cursor_decl);
        Clay__CloseElement();
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main Widget Function

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// clang-format off
FlSignal ui_text_input(FlString label, FlTextInputState* state, FlTextInputConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();
    if (!ctx || !state) {
        return (FlSignal) { 0 };
    }

    if (!state->buffer) {
        ui_text_input_init(state, ctx->persistant);
    }

    FlString id_string = cfg.widget.id_override.length > 0 ? cfg.widget.id_override : label;
    FlLayoutId input_id = (FlLayoutId) { .name = id_string, .id = 0 };

    FlItemFlags flags = FlItemFlags_clickable | FlItemFlags_focusable | FlItemFlags_hot_animation | FlItemFlags_focus_animation;
    FlSignal signal = ui_item_behavior(input_id, flags, cfg.widget);

    u32 clay_id = ui_layout_id_to_clay_id(input_id);

    // Text input needs explicit focus when clicked (UI doesn't auto-focus clickable widgets)
    if (signal.clicked) {
        ctx->focused_id = clay_id;
    }

    ItemState* item = hashmap_get(&ctx->shared->item_state, clay_id);

    const struct FlThemeColors* theme = ui_get_theme_colors(ctx);
    float min_width = cfg.min_width > 0.0f ? cfg.min_width : 200.0f;

    FlColor base_bg = theme->surface.elevated;
    FlColor base_border = theme->border.default_color;

    FlColor bg_color = ui_apply_state_color(base_bg, item);
    FlColor border_color = ui_apply_focus_border(base_border, item, theme);

    // Virtual keyboard: shown when focused and no physical keyboard is available.
    bool focus_gained = signal.focused && !state->was_focused_last_frame;
    if (focus_gained && !ctx->physical_keyboard_available && !ctx->active_virtual_keyboard) {
        FlString current_text = ui_text_input_get_text(state);
        VirtualKeyboardConfig vk_config = { .max_length = cfg.max_length,
                                            .allowed_chars = cfg.allowed_chars,
                                            .title = label,
                                            .initial_text = current_text,
                                            .show_character_count = cfg.max_length > 0,
                                            .initial_page = VK_PAGE_LOWERCASE };
        // No keyboard is active here (guarded above), so the dedicated keyboard arena holds nothing live; rewind it
        // to reclaim the previous cycle's keyboard and string copies.
        fl_arena_rewind(ctx->vk_arena);
        ctx->active_virtual_keyboard = ui_virtual_keyboard_create(ctx->vk_arena, vk_config);
        ctx->vk_target_text_input = state;
    }

    state->was_focused_last_frame = signal.focused;

    if (signal.focused) {
        ctx->text_input_focused_this_frame = true;
        // handle_text_input_keyboard only ever writes true, on the paths that fire.
        bool text_changed = false, submitted = false, cancelled = false;
        handle_text_input_keyboard(state, ctx, &cfg, &text_changed, &submitted, &cancelled);
        signal.text_changed = text_changed;
        signal.submitted = submitted;
        signal.cancelled = cancelled;
    }

    const float widget_height = (float)FlWidgetHeight_SM;
    const float border_width = (float)FlBorderWidth_Medium;
    const float inner_padding = (float)FlSpacing_SM;
    const float inner_width = min_width - (border_width * 2.0f);
    const float inner_height = widget_height - (border_width * 2.0f);
    const float visible_width = inner_width - (inner_padding * 2.0f);

    // Outer border container (focus ring)
    ui_layout_area(.sizing = { sizing_fixed(min_width), sizing_fixed(widget_height) },
                   .padding = padding_all(border_width), .radius = corner_radius(FlRadius_MD),
                   .color = border_color) {
        // Inner content area with clipping for horizontal scroll
        ui_layout_area_id(
            input_id, .sizing = { sizing_fixed(inner_width), sizing_fixed(inner_height) },
            .padding = padding_all(inner_padding), .color = bg_color,
            .radius = corner_radius(FlRadius_MD - border_width),
            .child_alignment = ui_align(Left, Center),
            .clip = { .horizontal = true,
                      .vertical = false,
                      .manual_scroll = true,
                      .scroll_pos = { .x = -state->scroll_offset_x, .y = 0.0f } }) {
            render_text_input(state, &cfg, ctx, signal.focused, visible_width);
        }

        // Render blinking cursor when focused (outside the clipped container so it can attach to it)
        if (signal.focused && state->stb_state) {
            render_text_cursor(state, ctx, clay_id, inner_padding, inner_height);
        }
    }

    if (ctx->active_virtual_keyboard && ctx->vk_target_text_input == state) {
        ui_virtual_keyboard_render(ctx->active_virtual_keyboard, ctx);
    }

    return signal;
}
// clang-format on
