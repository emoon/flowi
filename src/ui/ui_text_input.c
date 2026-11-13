#include "ui_text_input.h"
#include <core/arena.h>
#include <core/core.h>
#include <platform/clipboard.h>
#include <core/sprintf.h>
#include "external/clay/clay.h"
#include "input_actions.h"
#include <ui/ui_theme.h>
#include <ui/ui_types.h>
#include "ui.h"
#include "ui_private.h"
#include "ui_text_buffer.h"
#include "ui_text_edit.h"
#include "ui_variant_helpers.h"
#include "ui_virtual_keyboard.h"
#include <math.h>

// Everything this file measures or positions ends up in screen coordinates, reached by two different
// multipliers: type (glyph sizes, text widths) carries text_scale, chrome (padding, heights, hairlines)
// carries ui_scale. The two are independent and are applied separately, never folded into one factor.
// ui_text_edit_measure covers the type half; this is the chrome half, applied to the authored lengths below.
static inline float ui_scale_of(FlUiCtx* ctx) {
    return ctx->ui_scale > 0.0f ? ctx->ui_scale : 1.0f;
}

// Authored lengths of the field's chrome.
static const float s_widget_height = (float)FlWidgetHeight_SM;
static const float s_border_width = (float)FlBorderWidth_Medium;
static const float s_inner_padding = (float)FlSpacing_SM;
static const float s_inner_height = (float)FlWidgetHeight_SM - (float)FlBorderWidth_Medium * 2.0f;

// One text input for the duration of a frame.
typedef struct TextField {
    FlTextInputState* state;
    FlUiCtx* ctx;
    const FlTextInputConfig* cfg;
} TextField;

// Insert the frame's typed characters that the config accepts.
static bool insert_typed(const TextField* f) {
    FlTextInputState* state = f->state;
    const u32 max_length = f->cfg->max_length;
    bool changed = false;

    // Read the per-frame snapshot, not the raw kb->text_input, which ui_input_update has already
    // cleared by the time the frame runs.
    const FlString typed = f->ctx->text_input_this_frame;
    for_count(i, typed.length) {
        const char c = typed.data[i];
        const bool has_room = max_length == 0 || state->buffer_length < max_length;
        if (has_room && text_buffer_filter_char(c, f->cfg->allowed_chars)) {
            ui_text_edit_key(state, (u32)c);
            changed = true;
        }
    }
    return changed;
}

typedef enum NavDir {
    NavDir_None,
    NavDir_Left,
    NavDir_Right,
} NavDir;

// An action that feeds one key to the editor.
typedef struct KeyBinding {
    FlAction action;
    u32 key;
    // Fires on software repeat while held, not only on the press.
    bool repeats;
    // Changes the text. The other keys move the cursor, and extend the selection while shift is held.
    bool edits;
    // The arrow keys also drive widget navigation; a move takes that direction for itself so focus stays put.
    NavDir nav;
} KeyBinding;

static const KeyBinding s_key_bindings[] = {
    { FlAction_Text_Move_Left, STB_TEXTEDIT_K_LEFT, .repeats = true, .nav = NavDir_Left },
    { FlAction_Text_Move_Right, STB_TEXTEDIT_K_RIGHT, .repeats = true, .nav = NavDir_Right },
    { FlAction_Text_Move_Word_Left, STB_TEXTEDIT_K_WORDLEFT, .repeats = true, .nav = NavDir_Left },
    { FlAction_Text_Move_Word_Right, STB_TEXTEDIT_K_WORDRIGHT, .repeats = true, .nav = NavDir_Right },
    { FlAction_Text_Home, STB_TEXTEDIT_K_LINESTART },
    { FlAction_Text_End, STB_TEXTEDIT_K_LINEEND },
    { FlAction_Text_Backspace, STB_TEXTEDIT_K_BACKSPACE, .repeats = true, .edits = true },
    { FlAction_Text_Delete, STB_TEXTEDIT_K_DELETE, .repeats = true, .edits = true },
    { FlAction_Text_Undo, STB_TEXTEDIT_K_UNDO, .edits = true },
    { FlAction_Text_Redo, STB_TEXTEDIT_K_REDO, .edits = true },
};

static bool binding_fired(FlUiCtx* ctx, const KeyBinding* binding) {
    return binding->repeats ? ui_action_pressed_or_repeated(ctx, binding->action)
                            : ui_action_pressed(ctx, binding->action);
}

static void consume_nav(FlUiCtx* ctx, NavDir dir) {
    if (dir == NavDir_Left) {
        ctx->nav_frame.nav_left = false;
    } else if (dir == NavDir_Right) {
        ctx->nav_frame.nav_right = false;
    }
}

// Run the cursor and editing keys that fired this frame. Returns true when one of them changes the text.
static bool apply_key_bindings(FlTextInputState* state, FlUiCtx* ctx) {
    const bool shift = (ctx->input_state.keyboard.modifiers & FlKeyModifier_Shift) != 0;
    const u32 shift_flag = shift ? STB_TEXTEDIT_K_SHIFT : 0;
    bool changed = false;

    for_count(i, sizeof_array(s_key_bindings)) {
        const KeyBinding* binding = &s_key_bindings[i];
        if (!binding_fired(ctx, binding)) {
            continue;
        }
        ui_text_edit_key(state, binding->edits ? binding->key : binding->key | shift_flag);
        consume_nav(ctx, binding->nav);
        changed |= binding->edits;
    }
    return changed;
}

// Put the selection on the clipboard. Returns false when nothing is selected.
static bool copy_selection(const FlTextInputState* state) {
    const FlString selected = ui_text_edit_selection(state);
    if (selected.length == 0) {
        return false;
    }
    backend_set_clipboard_text(selected);
    return true;
}

// Select all, copy, cut and paste. Returns true when the text changes.
static bool apply_clipboard_actions(const TextField* f) {
    FlTextInputState* state = f->state;
    FlUiCtx* ctx = f->ctx;
    bool changed = false;

    if (ui_action_pressed(ctx, FlAction_Text_Select_All)) {
        ui_text_edit_select_all(state);
    }
    if (ui_action_pressed(ctx, FlAction_Text_Copy)) {
        copy_selection(state);
    }
    if (ui_action_pressed(ctx, FlAction_Text_Cut) && copy_selection(state)) {
        ui_text_edit_cut(state);
        changed = true;
    }
    if (ui_action_pressed(ctx, FlAction_Text_Paste)) {
        const FlString clipboard_text = backend_get_clipboard_text(ctx->render_frame_allocator);
        changed |= ui_text_input_paste(state, f->cfg, clipboard_text);
    }
    return changed;
}

static void handle_keyboard(const TextField* f, FlSignal* signal) {
    const bool typed = insert_typed(f);
    const bool keyed = apply_key_bindings(f->state, f->ctx);
    const bool clipped = apply_clipboard_actions(f);

    signal->text_changed = typed || keyed || clipped;
    signal->submitted = ui_key_pressed(FlKey_Enter) || ui_key_pressed(FlKey_Kp_Enter);
    signal->cancelled = ui_action_pressed(f->ctx, FlAction_Cancel);
}

// Distance from the start of the text to the cursor, in screen px.
static float cursor_offset(const TextField* f) {
    const u64 cursor = (u64)STB_STATE(f->state)->cursor;
    if (cursor == 0) {
        return 0.0f;
    }
    const FlString before = { .data = f->state->buffer, .length = cursor, .is_static = 0 };
    return ui_text_edit_measure(f->ctx, before).x;
}

// Keep the cursor inside the visible width: retarget the horizontal scroll when the cursor leaves either edge,
// then ease toward the target.
static void scroll_to_cursor(const TextField* f, float visible_width) {
    FlTextInputState* state = f->state;
    if (state->buffer_length == 0 || !state->stb_state) {
        return;
    }

    // The scroll offset and cursor_x are in screen coordinates (Clay takes manual scroll_pos unscaled), so
    // the authored viewport width and margin are scaled into the same space.
    const float scale = ui_scale_of(f->ctx);
    const float visible_px = visible_width * scale;
    const float margin_px = 10.0f * scale;
    const float cursor_x = cursor_offset(f);
    const float cursor_screen_x = cursor_x - state->scroll_offset_x;

    if (cursor_screen_x > visible_px - margin_px) {
        state->target_scroll_x = cursor_x - visible_px + margin_px;
    } else if (cursor_screen_x < 0.0f) {
        state->target_scroll_x = cursor_x;
    }

    const FlString text = ui_text_input_get_text(state);
    const float max_scroll = max(ui_text_edit_measure(f->ctx, text).x - visible_px, 0.0f);
    state->target_scroll_x = min(max(state->target_scroll_x, 0.0f), max_scroll);

    state->scroll_offset_x += (state->target_scroll_x - state->scroll_offset_x) * 0.3f;
}

// The text, or the placeholder while empty, followed by the optional character counter.
static void render_content(const TextField* f) {
    const FlTextInputState* state = f->state;
    const FlTextInputConfig* cfg = f->cfg;
    const struct FlThemeColors* theme = ui_get_theme_colors(f->ctx);

    if (state->buffer_length > 0) {
        // TODO: Render selection highlight as a colored rectangle behind text

        // Wrap text in a growing container to prevent it from affecting parent layout
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {
            ui_text_styled(ui_text_input_get_text(state), .color = theme->text.primary, .valign = FlTextVAlign_Center);
        }
    } else if (cfg->placeholder.length > 0) {
        ui_text_styled(cfg->placeholder, .color = theme->text.secondary, .valign = FlTextVAlign_Center);
    }

    if (cfg->show_character_count && cfg->max_length > 0) {
        arena_scratch_auto(temp);
        FlString counter = fl_sprintf_arena(temp.arena, "%u/%u", state->buffer_length, cfg->max_length);
        ui_text_styled(counter, .color = theme->text.secondary, .valign = FlTextVAlign_Center);
    }
}

static Clay_SizingAxis clay_fixed(float size) {
    return (Clay_SizingAxis) { .type = CLAY__SIZING_TYPE_FIXED, .size = { .minMax = { .min = size, .max = size } } };
}

// Render the blinking text cursor as a floating element attached to the input container.
// Caller is responsible for the focus/stb_state guard before invoking.
static void render_cursor(const TextField* f, u32 clay_id) {
    FlTextInputState* state = f->state;
    FlUiCtx* ctx = f->ctx;

    // 1.0 second blink period, on for the first half.
    state->cursor_blink_t += ctx->delta_time;
    if (fmodf(state->cursor_blink_t, 1.0f) >= 0.5f) {
        return;
    }

    // Everything below is screen coordinates. The cursor offset and the font metrics are measured at the scaled
    // size; the chrome lengths and the hairline constants are authored lengths and are scaled here.
    const float scale = ui_scale_of(ctx);
    const float text_height = ui_text_edit_measure(ctx, S("M")).y;
    const float text_top = (s_inner_height * scale - text_height) / 2.0f;
    const FlColor color = ui_get_theme_colors(ctx)->text.primary;

    // Unique per-frame id for the cursor element.
    Clay__OpenElementWithId((Clay_ElementId) { .id = 0xFFFF0000 + (u32)(ctx->frame_counter & 0xFFFF) });

    // Slightly taller than the text, offset 1px up so it extends 1px above and 3px below.
    Clay_ElementDeclaration cursor_decl = {
        .layout = { .sizing = { .width = clay_fixed(2.0f * scale), .height = clay_fixed(text_height + 4.0f * scale) } },
        .backgroundColor = (Clay_Color) { color.r, color.g, color.b, color.a },
        .floating = { .attachTo = CLAY_ATTACH_TO_ELEMENT_WITH_ID,
                      .parentId = clay_id,
                      .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP },
                      .offset = { .x = s_inner_padding * scale + cursor_offset(f) - state->scroll_offset_x,
                                  .y = text_top - 1.0f * scale },
                      .zIndex = 10 },
    };

    Clay__ConfigureOpenElement(cursor_decl);
    Clay__CloseElement();
}

// Show the virtual keyboard for a field that just gained focus when no physical keyboard is available.
static void open_virtual_keyboard(const TextField* f, FlString label) {
    FlUiCtx* ctx = f->ctx;
    if (ctx->physical_keyboard_available || ctx->active_virtual_keyboard) {
        return;
    }

    VirtualKeyboardConfig vk_config = { .max_length = f->cfg->max_length,
                                        .allowed_chars = f->cfg->allowed_chars,
                                        .title = label,
                                        .initial_text = ui_text_input_get_text(f->state),
                                        .show_character_count = f->cfg->max_length > 0,
                                        .initial_page = VK_PAGE_LOWERCASE };
    // No keyboard is active here (guarded above), so the dedicated keyboard arena holds nothing live; rewind it
    // to reclaim the previous cycle's keyboard and string copies.
    fl_arena_rewind(ctx->vk_arena);
    ctx->active_virtual_keyboard = ui_virtual_keyboard_create(ctx->vk_arena, vk_config);
    ctx->vk_target_text_input = f->state;
}

static FlLayoutId field_id(FlString label, const FlTextInputConfig* cfg) {
    const FlString name = cfg->widget.id_override.length > 0 ? cfg->widget.id_override : label;
    return (FlLayoutId) { .name = name, .id = 0 };
}

// Per-frame work of a focused field: the virtual keyboard on the frame focus arrives, then the keyboard.
static void update_focused(const TextField* f, FlString label, FlSignal* signal) {
    FlTextInputState* state = f->state;
    const bool gained = signal->focused && !state->was_focused_last_frame;
    state->was_focused_last_frame = signal->focused;

    if (gained) {
        open_virtual_keyboard(f, label);
    }
    if (signal->focused) {
        f->ctx->text_input_focused_this_frame = true;
        handle_keyboard(f, signal);
    }
}

// clang-format off
// The bordered field: focus ring, clipped scrolling content and the cursor.
static void render_field(const TextField* f, FlLayoutId input_id, bool focused) {
    FlTextInputState* state = f->state;
    const u32 clay_id = ui_layout_id_to_clay_id(input_id);
    ItemState* item = hashmap_get(&f->ctx->shared->item_state, clay_id);
    const struct FlThemeColors* theme = ui_get_theme_colors(f->ctx);

    const FlColor bg_color = ui_apply_state_color(theme->surface.elevated, item);
    const FlColor border_color = ui_apply_focus_border(theme->border.default_color, item, theme);

    const float min_width = f->cfg->min_width > 0.0f ? f->cfg->min_width : 200.0f;
    const float inner_width = min_width - (s_border_width * 2.0f);
    const float visible_width = inner_width - (s_inner_padding * 2.0f);

    // Outer border container (focus ring)
    ui_layout_area(.sizing = { sizing_fixed(min_width), sizing_fixed(s_widget_height) },
                   .padding = padding_all(s_border_width), .radius = corner_radius(FlRadius_MD),
                   .color = border_color) {
        // Inner content area with clipping for horizontal scroll
        ui_layout_area_id(
            input_id, .sizing = { sizing_fixed(inner_width), sizing_fixed(s_inner_height) },
            .padding = padding_all(s_inner_padding), .color = bg_color,
            .radius = corner_radius(FlRadius_MD - s_border_width),
            .child_alignment = ui_align(Left, Center),
            .clip = { .horizontal = true,
                      .vertical = false,
                      .manual_scroll = true,
                      .scroll_pos = { .x = -state->scroll_offset_x, .y = 0.0f } }) {
            if (focused) {
                scroll_to_cursor(f, visible_width);
            }
            render_content(f);
        }

        // Render blinking cursor when focused (outside the clipped container so it can attach to it)
        if (focused && state->stb_state) {
            render_cursor(f, clay_id);
        }
    }
}

FlSignal ui_text_input(FlString label, FlTextInputState* state, FlTextInputConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();
    if (!ctx || !state) {
        return (FlSignal) { 0 };
    }

    if (!state->buffer) {
        ui_text_input_init(state, ctx->persistant);
    }

    const TextField field = { .state = state, .ctx = ctx, .cfg = &cfg };
    const FlLayoutId input_id = field_id(label, &cfg);

    FlItemFlags flags = FlItemFlags_clickable | FlItemFlags_focusable | FlItemFlags_hot_animation | FlItemFlags_focus_animation;
    FlSignal signal = ui_item_behavior(input_id, flags, cfg.widget);

    // Text input needs explicit focus when clicked (UI doesn't auto-focus clickable widgets)
    if (signal.clicked) {
        ctx->focused_id = ui_layout_id_to_clay_id(input_id);
    }

    update_focused(&field, label, &signal);
    render_field(&field, input_id, signal.focused);

    if (ctx->active_virtual_keyboard && ctx->vk_target_text_input == state) {
        ui_virtual_keyboard_render(ctx->active_virtual_keyboard, ctx);
    }

    return signal;
}
// clang-format on
