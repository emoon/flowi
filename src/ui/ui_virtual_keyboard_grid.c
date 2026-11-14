#include "ui_virtual_keyboard_grid.h"

// Key grids: a QWERTY layout with 4 pages (lowercase, uppercase, numbers, symbols). Each grid is row-major
// (rows x cols), with VK_KEY_EMPTY for gaps.

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

typedef const char VkGridRow[VK_GRID_WIDTH];

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

static bool index_in_range(int32_t index, int32_t count) {
    return index >= 0 && index < count;
}

static int32_t wrap_index(int32_t index, int32_t count) {
    if (index < 0)
        index = count - 1;
    if (index >= count)
        index = 0;
    return index;
}

char vk_key_at(const VirtualKeyboard* self, int32_t x, int32_t y) {
    if (!index_in_range(x, VK_GRID_WIDTH) || !index_in_range(y, VK_GRID_HEIGHT)) {
        return VK_KEY_EMPTY;
    }
    return get_key_grid(self->current_page)[y][x];
}

char vk_key_under_cursor(const VirtualKeyboard* self) {
    return vk_key_at(self, self->cursor_x, self->cursor_y);
}

void vk_move_cursor(VirtualKeyboard* self, int32_t dx, int32_t dy) {
    int32_t x = self->cursor_x;
    int32_t y = self->cursor_y;

    for (int32_t attempts = 0; attempts < VK_GRID_WIDTH * VK_GRID_HEIGHT; attempts++) {
        x = wrap_index(x + dx, VK_GRID_WIDTH);
        y = wrap_index(y + dy, VK_GRID_HEIGHT);
        if (vk_key_at(self, x, y) != VK_KEY_EMPTY) {
            self->cursor_x = x;
            self->cursor_y = y;
            return;
        }
    }
}

void vk_reset_cursor(VirtualKeyboard* self) {
    self->cursor_x = 0;
    self->cursor_y = 0;
    if (vk_key_under_cursor(self) == VK_KEY_EMPTY) {
        vk_move_cursor(self, 1, 0);
    }
}
