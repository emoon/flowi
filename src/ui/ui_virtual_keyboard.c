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
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Key Grid Definitions
//
// QWERTY layout with 4 pages: lowercase, uppercase, numbers, symbols
// Each grid is row-major (rows x cols), with VK_KEY_EMPTY for gaps

static const char s_keys_lowercase[][13] = {
    { '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', VK_KEY_EMPTY },
    { 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\\' },
    { 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', VK_KEY_EMPTY, VK_KEY_EMPTY },
    { 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY },
    { VK_KEY_SHIFT, VK_KEY_SPACE, VK_KEY_PAGE_123, VK_KEY_BACKSPACE, VK_KEY_ENTER, VK_KEY_CANCEL, VK_KEY_EMPTY,
      VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY },
};

static const char s_keys_uppercase[][13] = {
    { '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', VK_KEY_EMPTY },
    { 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '|' },
    { 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', VK_KEY_EMPTY, VK_KEY_EMPTY },
    { 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY },
    { VK_KEY_SHIFT, VK_KEY_SPACE, VK_KEY_PAGE_123, VK_KEY_BACKSPACE, VK_KEY_ENTER, VK_KEY_CANCEL, VK_KEY_EMPTY,
      VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY },
};

static const char s_keys_numbers[][13] = {
    { '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY },
    { '+', '-', '*', '/', '=', '<', '>', '(', ')', '[', ']', VK_KEY_EMPTY, VK_KEY_EMPTY },
    { '.', ',', ':', ';', '!', '?', '@', '#', '$', '%', '^', '&', VK_KEY_EMPTY },
    { '_', '|', '\\', '~', '`', '\'', '"', VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY,
      VK_KEY_EMPTY },
    { VK_KEY_PAGE_ABC, VK_KEY_SPACE, VK_KEY_PAGE_SYM, VK_KEY_BACKSPACE, VK_KEY_ENTER, VK_KEY_CANCEL, VK_KEY_EMPTY,
      VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY },
};

static const char s_keys_symbols[][13] = {
    { '(', ')', '[', ']', '{', '}', '<', '>', VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY },
    { '!', '@', '#', '$', '%', '^', '&', '*', '_', '+', '=', VK_KEY_EMPTY, VK_KEY_EMPTY },
    { '.', ',', ':', ';', '?', '/', '\\', '|', '~', '`', VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY },
    { '\'', '"', VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY,
      VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY },
    { VK_KEY_PAGE_ABC, VK_KEY_SPACE, VK_KEY_PAGE_123, VK_KEY_BACKSPACE, VK_KEY_ENTER, VK_KEY_CANCEL, VK_KEY_EMPTY,
      VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY, VK_KEY_EMPTY },
};

#define VK_GRID_WIDTH 13
#define VK_GRID_HEIGHT 5

typedef const char VkGridRow[VK_GRID_WIDTH];

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static VkGridRow* get_key_grid(VirtualKeyboardPage page) {
    switch (page) {
        case VK_PAGE_LOWERCASE:
            return s_keys_lowercase;
        case VK_PAGE_UPPERCASE:
            return s_keys_uppercase;
        case VK_PAGE_NUMBERS:
            return s_keys_numbers;
        case VK_PAGE_SYMBOLS:
            return s_keys_symbols;
        default:
            return s_keys_lowercase;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static char get_key_at(VkGridRow* grid, int32_t x, int32_t y) {
    if (x < 0 || x >= VK_GRID_WIDTH || y < 0 || y >= VK_GRID_HEIGHT) {
        return VK_KEY_EMPTY;
    }
    return grid[y][x];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void find_next_valid_key(int32_t* x, int32_t* y, int32_t dx, int32_t dy, VkGridRow* grid) {
    int32_t start_x = *x;
    int32_t start_y = *y;
    int32_t attempts = 0;
    const int32_t max_attempts = VK_GRID_WIDTH * VK_GRID_HEIGHT;

    do {
        *x += dx;
        *y += dy;

        if (*x < 0)
            *x = VK_GRID_WIDTH - 1;
        if (*x >= VK_GRID_WIDTH)
            *x = 0;
        if (*y < 0)
            *y = VK_GRID_HEIGHT - 1;
        if (*y >= VK_GRID_HEIGHT)
            *y = 0;

        char key = get_key_at(grid, *x, *y);
        if (key != VK_KEY_EMPTY) {
            return;
        }

        attempts++;
        if (attempts >= max_attempts) {
            *x = start_x;
            *y = start_y;
            return;
        }
    } while (true);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString get_key_display_string(FlArena* arena, char key) {
    switch (key) {
        case VK_KEY_SPACE:
            return FL_S("[SPACE]");
        case VK_KEY_BACKSPACE:
            return FL_S("[BACK]");
        case VK_KEY_ENTER:
            return FL_S("[ENTER]");
        case VK_KEY_CANCEL:
            return FL_S("[CANCEL]");
        case VK_KEY_SHIFT:
            return FL_S("[SHIFT]");
        case VK_KEY_PAGE_123:
            return FL_S("[123]");
        case VK_KEY_PAGE_ABC:
            return FL_S("[ABC]");
        case VK_KEY_PAGE_SYM:
            return FL_S("[SYM]");
        case VK_KEY_EMPTY:
            return FL_S("");
        default: {
            char* buf = arena_alloc_array(arena, char, 2);
            buf[0] = key;
            buf[1] = '\0';
            return (FlString) { .data = buf, .length = 1, .is_static = 0 };
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vk_delete_char(VirtualKeyboard* self) {
    if (self->buffer_length > 0) {
        self->buffer_length--;
        self->buffer[self->buffer_length] = '\0';
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vk_handle_key_press(VirtualKeyboard* self, char key) {
    switch (key) {
        case VK_KEY_SPACE:
            vk_insert_char(self, ' ');
            break;

        case VK_KEY_BACKSPACE:
            vk_delete_char(self);
            break;

        case VK_KEY_ENTER:
            self->submitted = true;
            self->active = false;
            break;

        case VK_KEY_CANCEL:
            self->cancelled = true;
            self->active = false;
            break;

        case VK_KEY_SHIFT:
            if (self->current_page == VK_PAGE_LOWERCASE) {
                self->current_page = VK_PAGE_UPPERCASE;
            } else if (self->current_page == VK_PAGE_UPPERCASE) {
                self->current_page = VK_PAGE_LOWERCASE;
            }
            break;

        case VK_KEY_PAGE_123:
            self->current_page = VK_PAGE_NUMBERS;
            break;

        case VK_KEY_PAGE_ABC:
            self->current_page = VK_PAGE_LOWERCASE;
            break;

        case VK_KEY_PAGE_SYM:
            self->current_page = VK_PAGE_SYMBOLS;
            break;

        case VK_KEY_EMPTY:
            break;

        default:
            vk_insert_char(self, key);
            break;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Input handling

static void vk_handle_input(VirtualKeyboard* self, FlUiCtx* ctx) {
    FlInputState* input = &ctx->input_state;
    VkGridRow* grid = get_key_grid(self->current_page);

    bool navigation_happened = false;

    // Check navigation BEFORE increment/decrement since they share keys.
    if (ui_action_pressed(ctx, FlAction_Nav_Left)) {
        find_next_valid_key(&self->cursor_x, &self->cursor_y, -1, 0, grid);
        navigation_happened = true;
    }
    if (ui_action_pressed(ctx, FlAction_Nav_Right)) {
        find_next_valid_key(&self->cursor_x, &self->cursor_y, 1, 0, grid);
        navigation_happened = true;
    }
    if (ui_action_pressed(ctx, FlAction_Nav_Up)) {
        find_next_valid_key(&self->cursor_x, &self->cursor_y, 0, -1, grid);
        navigation_happened = true;
    }
    if (ui_action_pressed(ctx, FlAction_Nav_Down)) {
        find_next_valid_key(&self->cursor_x, &self->cursor_y, 0, 1, grid);
        navigation_happened = true;
    }

    // Select key with A button (confirm action)
    if (ui_action_pressed(ctx, FlAction_Confirm)) {
        char key = get_key_at(grid, self->cursor_x, self->cursor_y);
        vk_handle_key_press(self, key);
    }

    if (input->gamepads[0].connected && input->gamepads[0].buttons_pressed[FlGamepadButton_Y]) {
        vk_insert_char(self, ' ');
    }

    // Cancel action (B button on gamepad, ESC on keyboard)
    if (ui_action_pressed(ctx, FlAction_Cancel)) {
        self->cancelled = true;
        self->active = false;
        return;
    }

    // Menu/Start = submit (using menu action)
    if (ui_action_pressed(ctx, FlAction_Menu)) {
        self->submitted = true;
        self->active = false;
        return;
    }

    // Page switching - use increment/decrement actions (shoulder buttons)
    // Only process if no navigation happened (to prevent arrow keys from changing pages)
    if (!navigation_happened) {
        // Decrement = LB/L1 (previous page)
        if (ui_action_pressed(ctx, FlAction_Decrement)) {
            self->current_page = (self->current_page + VK_PAGE_COUNT - 1) % VK_PAGE_COUNT;
            self->cursor_x = 0;
            self->cursor_y = 0;
            VkGridRow* new_grid = get_key_grid(self->current_page);
            if (get_key_at(new_grid, self->cursor_x, self->cursor_y) == VK_KEY_EMPTY) {
                find_next_valid_key(&self->cursor_x, &self->cursor_y, 1, 0, new_grid);
            }
        }

        // Increment = RB/R1 (next page)
        if (ui_action_pressed(ctx, FlAction_Increment)) {
            self->current_page = (self->current_page + 1) % VK_PAGE_COUNT;
            self->cursor_x = 0;
            self->cursor_y = 0;
            VkGridRow* new_grid = get_key_grid(self->current_page);
            if (get_key_at(new_grid, self->cursor_x, self->cursor_y) == VK_KEY_EMPTY) {
                find_next_valid_key(&self->cursor_x, &self->cursor_y, 1, 0, new_grid);
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Rendering

static void vk_render(VirtualKeyboard* self, FlUiCtx* ctx) {
    const struct FlThemeColors* theme = ui_get_theme_colors(ctx);
    VkGridRow* grid = get_key_grid(self->current_page);

    float panel_width = 800.0f;
    float panel_height = 500.0f;

    // Fullscreen dimmed background overlay (modal)
    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .color = (FlColor) { 0, 0, 0, 180 },
                   .child_alignment = ui_align(Center, Center)) {
        ui_layout_area(.sizing = { sizing_fixed(panel_width), sizing_fixed(panel_height) },
                       .color = theme->surface.base, .padding = padding_all(20), .radius = corner_radius(8),
                       .direction = LayoutDirection_TopToBottom, .child_gap = 10) {
            if (self->config.title.length > 0) {
                ui_text_styled(self->config.title, .color = theme->text.primary);
            }

            {
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

            if (self->config.show_character_count && self->config.max_length > 0) {
                arena_scratch_auto(temp);
                FlString counter = fl_sprintf_arena(temp.arena, "%u/%u", self->buffer_length, self->config.max_length);
                ui_text_styled(counter, .color = theme->text.secondary);
            }

            {
                arena_scratch_auto(temp);
                const char* page_name = "abc";
                switch (self->current_page) {
                    case VK_PAGE_LOWERCASE:
                        page_name = "abc";
                        break;
                    case VK_PAGE_UPPERCASE:
                        page_name = "ABC";
                        break;
                    case VK_PAGE_NUMBERS:
                        page_name = "123";
                        break;
                    case VK_PAGE_SYMBOLS:
                        page_name = "SYM";
                        break;
                    default:
                        page_name = "abc";
                        break;
                }
                FlString page_text = fl_sprintf_arena(temp.arena, "Page: %s (LB/RB to switch)", page_name);
                ui_text_styled(page_text, .color = theme->text.secondary);
            }

            ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .direction = LayoutDirection_TopToBottom,
                           .child_gap = 4) {
                for (int32_t y = 0; y < VK_GRID_HEIGHT; y++) {
                    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .direction = LayoutDirection_LeftToRight,
                                   .child_gap = 4) {
                        for (int32_t x = 0; x < VK_GRID_WIDTH; x++) {
                            char key = get_key_at(grid, x, y);
                            if (key == VK_KEY_EMPTY) {
                                ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {}
                                continue;
                            }

                            bool is_selected = (x == self->cursor_x && y == self->cursor_y);

                            FlColor key_bg = is_selected ? theme->interactive.base : theme->surface.elevated;
                            FlColor key_text = is_selected ? ((FlColor) { 255, 255, 255, 255 }) : theme->text.primary;

                            ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .color = key_bg,
                                           .padding = padding_all(4), .child_alignment = ui_align(Center, Center)) {
                                FlString key_str = get_key_display_string(ctx->render_frame_allocator, key);
                                ui_text_styled(key_str, .color = key_text);
                            }
                        }
                    }
                }
            }

            ui_text_styled(FL_S("D-Pad: Move | A: Select | Y: Space | B: Back | Start: Submit"),
                           .color = theme->text.secondary);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Non-blocking API - Create virtual keyboard

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

    VkGridRow* grid = get_key_grid(vk->current_page);
    while (get_key_at(grid, vk->cursor_x, vk->cursor_y) == VK_KEY_EMPTY) {
        find_next_valid_key(&vk->cursor_x, &vk->cursor_y, 1, 0, grid);
    }

    return vk;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Non-blocking API - Update virtual keyboard state

void ui_virtual_keyboard_update(VirtualKeyboard* self, FlUiCtx* ctx) {
    if (!self || !self->active) {
        return;
    }

    vk_handle_input(self, ctx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Non-blocking API - Render virtual keyboard

void ui_virtual_keyboard_render(VirtualKeyboard* self, FlUiCtx* ctx) {
    if (!self || !self->active) {
        return;
    }

    vk_render(self, ctx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Virtual keyboard glue between the UI context and the keyboard widget

void ui_set_physical_keyboard_available(FlUiCtx* ctx, bool available) {
    ctx->physical_keyboard_available = available;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_virtual_keyboard_update_and_render(FlUiCtx* ctx) {
    if (!ctx->active_virtual_keyboard) {
        return;
    }

    ui_virtual_keyboard_update(ctx->active_virtual_keyboard, ctx);

    ui_virtual_keyboard_render(ctx->active_virtual_keyboard, ctx);

    ui_virtual_keyboard_finish_if_inactive(ctx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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
