#include "ui_virtual_keyboard.h"
#include <core/arena.h>
#include <platform/input_types.h>
#include <core/log.h>
#include <core/memory.h>
#include <core/sprintf.h>
#include <core/string.h>
#include "external/clay/clay.h"
#include "font_cache.h"
#include "input_actions.h"
#include "layout.h"
#include <ui/ui_theme.h>
#include "ui.h"
#include "ui_private.h"
#include "ui_text_buffer.h"
#include "ui_text_input.h"
#include "ui_virtual_keyboard_grid.h"
#include <string.h>

static const struct {
    char key;
    FlString label;
} s_key_labels[] = {
    { VK_KEY_SPACE, FL_S_("[SPACE]") },   { VK_KEY_BACKSPACE, FL_S_("[BACK]") }, { VK_KEY_ENTER, FL_S_("[ENTER]") },
    { VK_KEY_CANCEL, FL_S_("[CANCEL]") }, { VK_KEY_SHIFT, FL_S_("[SHIFT]") },    { VK_KEY_PAGE_123, FL_S_("[123]") },
    { VK_KEY_PAGE_ABC, FL_S_("[ABC]") },  { VK_KEY_PAGE_SYM, FL_S_("[SYM]") },   { VK_KEY_EMPTY, FL_S_("") },
};

static const struct {
    FlAction action;
    int32_t dx;
    int32_t dy;
} s_cursor_moves[] = {
    { FlAction_Nav_Left, -1, 0 },
    { FlAction_Nav_Right, 1, 0 },
    { FlAction_Nav_Up, 0, -1 },
    { FlAction_Nav_Down, 0, 1 },
};

static FlString get_key_display_string(FlArena* arena, char key) {
    for (size_t i = 0; i < sizeof_array(s_key_labels); i++) {
        if (s_key_labels[i].key == key) {
            return s_key_labels[i].label;
        }
    }

    char* buf = arena_alloc_array(arena, char, 2);
    buf[0] = key;
    buf[1] = '\0';
    return (FlString) { .data = buf, .length = 1, .is_static = 0 };
}

static void vk_insert_char(VirtualKeyboard* self, char c) {
    if (self->config.max_length > 0 && self->buffer_length >= self->config.max_length) {
        return;
    }

    if (!text_buffer_filter_char(c, self->config.allowed_chars)) {
        return;
    }

    // Ensure capacity for the new char plus its NUL terminator (shared grow strategy)
    text_buffer_grow(self->arena, &self->buffer, &self->buffer_capacity, self->buffer_length, self->buffer_length + 2);

    self->buffer[self->buffer_length++] = c;
    self->buffer[self->buffer_length] = '\0';
}

static void vk_delete_char(VirtualKeyboard* self) {
    if (self->buffer_length > 0) {
        self->buffer_length--;
        self->buffer[self->buffer_length] = '\0';
    }
}

static void vk_submit(VirtualKeyboard* self) {
    self->submitted = true;
    self->active = false;
}

static void vk_cancel(VirtualKeyboard* self) {
    self->cancelled = true;
    self->active = false;
}

static void vk_toggle_case(VirtualKeyboard* self) {
    if (self->current_page == VK_PAGE_LOWERCASE) {
        self->current_page = VK_PAGE_UPPERCASE;
    } else if (self->current_page == VK_PAGE_UPPERCASE) {
        self->current_page = VK_PAGE_LOWERCASE;
    }
}

// Applies a page-switch key; returns false when the key is not one.
static bool vk_handle_page_key(VirtualKeyboard* self, char key) {
    switch (key) {
        case VK_KEY_SHIFT:
            vk_toggle_case(self);
            return true;
        case VK_KEY_PAGE_123:
            self->current_page = VK_PAGE_NUMBERS;
            return true;
        case VK_KEY_PAGE_ABC:
            self->current_page = VK_PAGE_LOWERCASE;
            return true;
        case VK_KEY_PAGE_SYM:
            self->current_page = VK_PAGE_SYMBOLS;
            return true;
        default:
            return false;
    }
}

static void vk_handle_key_press(VirtualKeyboard* self, char key) {
    switch (key) {
        case VK_KEY_SPACE:
            vk_insert_char(self, ' ');
            break;

        case VK_KEY_BACKSPACE:
            vk_delete_char(self);
            break;

        case VK_KEY_ENTER:
            vk_submit(self);
            break;

        case VK_KEY_CANCEL:
            vk_cancel(self);
            break;

        case VK_KEY_EMPTY:
            break;

        default:
            if (!vk_handle_page_key(self, key)) {
                vk_insert_char(self, key);
            }
            break;
    }
}

// Check navigation before increment/decrement since they share keys.
static bool vk_handle_navigation(VirtualKeyboard* self, FlUiCtx* ctx) {
    bool navigation_happened = false;

    for (size_t i = 0; i < sizeof_array(s_cursor_moves); i++) {
        if (ui_action_pressed(ctx, s_cursor_moves[i].action)) {
            vk_move_cursor(self, s_cursor_moves[i].dx, s_cursor_moves[i].dy);
            navigation_happened = true;
        }
    }

    return navigation_happened;
}

static void vk_cycle_page(VirtualKeyboard* self, int32_t step) {
    self->current_page = (self->current_page + VK_PAGE_COUNT + step) % VK_PAGE_COUNT;
    vk_reset_cursor(self);
}

// Decrement = LB/L1 (previous page), increment = RB/R1 (next page)
static void vk_handle_page_switch(VirtualKeyboard* self, FlUiCtx* ctx) {
    if (ui_action_pressed(ctx, FlAction_Decrement)) {
        vk_cycle_page(self, -1);
    }

    if (ui_action_pressed(ctx, FlAction_Increment)) {
        vk_cycle_page(self, 1);
    }
}

static bool gamepad_space_pressed(const FlUiCtx* ctx) {
    const GamepadState* pad = &ctx->input_state.gamepads[0];
    return pad->connected && pad->buttons_pressed[FlGamepadButton_Y];
}

static void vk_handle_input(VirtualKeyboard* self, FlUiCtx* ctx) {
    bool navigation_happened = vk_handle_navigation(self, ctx);

    // Select key with A button (confirm action)
    if (ui_action_pressed(ctx, FlAction_Confirm)) {
        vk_handle_key_press(self, vk_key_under_cursor(self));
    }

    if (gamepad_space_pressed(ctx)) {
        vk_insert_char(self, ' ');
    }

    // Cancel action (B button on gamepad, ESC on keyboard)
    if (ui_action_pressed(ctx, FlAction_Cancel)) {
        vk_cancel(self);
        return;
    }

    // Menu/Start = submit (using menu action)
    if (ui_action_pressed(ctx, FlAction_Menu)) {
        vk_submit(self);
        return;
    }

    // Page switching on the increment/decrement actions (shoulder buttons), skipped when navigation happened so
    // the arrow keys do not change pages.
    if (!navigation_happened) {
        vk_handle_page_switch(self, ctx);
    }
}

static const char* page_name(VirtualKeyboardPage page) {
    switch (page) {
        case VK_PAGE_UPPERCASE:
            return "ABC";
        case VK_PAGE_NUMBERS:
            return "123";
        case VK_PAGE_SYMBOLS:
            return "SYM";
        default:
            return "abc";
    }
}

static void vk_render_text_field(const VirtualKeyboard* self, const FlThemeColors* theme) {
    FlString current_text = { .data = self->buffer, .length = self->buffer_length, .is_static = 0 };
    ui_layout_area(.sizing = { sizing_grow(), sizing_fixed(40) }, .color = theme->surface.elevated,
                   .padding = padding_all(8), .child_alignment = ui_align(Left, Center)) {
        if (self->buffer_length > 0) {
            ui_text_styled(current_text, .color = theme->text.primary);
        } else {
            ui_text_styled(FL_S("(empty)"), .color = theme->text.secondary);
        }
    }
}

static void vk_render_status_lines(const VirtualKeyboard* self, const FlThemeColors* theme) {
    if (self->config.show_character_count && self->config.max_length > 0) {
        arena_scratch_auto(temp);
        FlString counter = fl_sprintf_arena(temp.arena, "%u/%u", self->buffer_length, self->config.max_length);
        ui_text_styled(counter, .color = theme->text.secondary);
    }

    arena_scratch_auto(temp);
    FlString page_text = fl_sprintf_arena(temp.arena, "Page: %s (LB/RB to switch)", page_name(self->current_page));
    ui_text_styled(page_text, .color = theme->text.secondary);
}

static void vk_render_key(const VirtualKeyboard* self, FlUiCtx* ctx, int32_t x, int32_t y) {
    const FlThemeColors* theme = ui_get_theme_colors(ctx);
    char key = vk_key_at(self, x, y);
    if (key == VK_KEY_EMPTY) {
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {}
        return;
    }

    bool is_selected = (x == self->cursor_x && y == self->cursor_y);

    FlColor key_bg = is_selected ? theme->interactive.base : theme->surface.elevated;
    FlColor key_text = is_selected ? ((FlColor) { 255, 255, 255, 255 }) : theme->text.primary;

    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .color = key_bg, .padding = padding_all(4),
                   .child_alignment = ui_align(Center, Center)) {
        FlString key_str = get_key_display_string(ctx->render_frame_allocator, key);
        ui_text_styled(key_str, .color = key_text);
    }
}

static void vk_render_key_grid(const VirtualKeyboard* self, FlUiCtx* ctx) {
    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .direction = LayoutDirection_TopToBottom,
                   .child_gap = 4) {
        for (int32_t y = 0; y < VK_GRID_HEIGHT; y++) {
            ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .direction = LayoutDirection_LeftToRight,
                           .child_gap = 4) {
                for (int32_t x = 0; x < VK_GRID_WIDTH; x++) {
                    vk_render_key(self, ctx, x, y);
                }
            }
        }
    }
}

static void vk_render_panel(const VirtualKeyboard* self, FlUiCtx* ctx) {
    const FlThemeColors* theme = ui_get_theme_colors(ctx);

    if (self->config.title.length > 0) {
        ui_text_styled(self->config.title, .color = theme->text.primary);
    }

    vk_render_text_field(self, theme);
    vk_render_status_lines(self, theme);
    vk_render_key_grid(self, ctx);

    ui_text_styled(FL_S("D-Pad: Move | A: Select | Y: Space | B: Back | Start: Submit"),
                   .color = theme->text.secondary);
}

static void vk_render(VirtualKeyboard* self, FlUiCtx* ctx) {
    const FlThemeColors* theme = ui_get_theme_colors(ctx);
    float panel_width = 800.0f;
    float panel_height = 500.0f;

    // Fullscreen dimmed background overlay (modal)
    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .color = (FlColor) { 0, 0, 0, 180 },
                   .child_alignment = ui_align(Center, Center)) {
        ui_layout_area(.sizing = { sizing_fixed(panel_width), sizing_fixed(panel_height) },
                       .color = theme->surface.base, .padding = padding_all(20), .radius = corner_radius(8),
                       .direction = LayoutDirection_TopToBottom, .child_gap = 10) {
            vk_render_panel(self, ctx);
        }
    }
}

VirtualKeyboard* ui_virtual_keyboard_create(FlArena* arena, VirtualKeyboardConfig config) {
    VirtualKeyboard* vk = arena_alloc_zero(arena, VirtualKeyboard);
    vk->config = config;
    vk->arena = arena;

    // Own the caller FlStrings that outlive this call: title is read every render and allowed_chars every
    // keypress, but the keyboard lives across many frames while the caller's string storage may not.
    vk->config.title = fl_string_copy(arena, config.title);
    vk->config.allowed_chars = fl_string_copy(arena, config.allowed_chars);
    vk->buffer_capacity = 256;
    vk->buffer = arena_alloc_array(arena, char, vk->buffer_capacity);
    vk->buffer_length = 0;
    vk->current_page = config.initial_page;
    vk->cursor_x = 0;
    vk->cursor_y = 0;
    vk->active = true;
    vk->submitted = false;
    vk->cancelled = false;

    if (config.initial_text.length > 0) {
        u32 copy_len = (u32)config.initial_text.length;
        if (config.max_length > 0 && copy_len > config.max_length) {
            copy_len = config.max_length;
        }
        if (copy_len >= vk->buffer_capacity) {
            vk->buffer_capacity = copy_len + 64;
            vk->buffer = arena_alloc_array(arena, char, vk->buffer_capacity);
        }
        memory_copy(vk->buffer, vk->buffer_capacity, config.initial_text.data, copy_len);
        vk->buffer_length = copy_len;
        vk->buffer[vk->buffer_length] = '\0';
    }

    vk_reset_cursor(vk);

    return vk;
}

void ui_virtual_keyboard_update(VirtualKeyboard* self, FlUiCtx* ctx) {
    if (!self || !self->active) {
        return;
    }

    vk_handle_input(self, ctx);
}

void ui_virtual_keyboard_render(VirtualKeyboard* self, FlUiCtx* ctx) {
    if (!self || !self->active) {
        return;
    }

    vk_render(self, ctx);
}

// Virtual keyboard glue between the UI context and the keyboard widget

void ui_set_physical_keyboard_available(FlUiCtx* ctx, bool available) {
    ctx->physical_keyboard_available = available;
}

void ui_virtual_keyboard_update_and_render(FlUiCtx* ctx) {
    if (!ctx->active_virtual_keyboard) {
        return;
    }

    ui_virtual_keyboard_update(ctx->active_virtual_keyboard, ctx);

    ui_virtual_keyboard_render(ctx->active_virtual_keyboard, ctx);

    ui_virtual_keyboard_finish_if_inactive(ctx);
}

// Retire a virtual keyboard that closed itself (submit or cancel), committing submitted text to the target input.

void ui_virtual_keyboard_finish_if_inactive(FlUiCtx* ctx) {
    VirtualKeyboard* vk = ctx->active_virtual_keyboard;
    if (vk->active) {
        return;
    }

    if (ctx->vk_target_text_input && vk->submitted) {
        FlString text = { .data = vk->buffer, .length = vk->buffer_length, .is_static = 0 };
        ui_text_input_set_text(ctx->vk_target_text_input, text);
    }

    ctx->active_virtual_keyboard = nullptr;
    ctx->vk_target_text_input = nullptr;
}
