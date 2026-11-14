#pragma once

// Key layout and cursor movement for the virtual keyboard: the four page grids and navigation over them.

#include "ui_virtual_keyboard.h"

#define VK_GRID_WIDTH 13
#define VK_GRID_HEIGHT 5

// Key at a grid cell of the current page; VK_KEY_EMPTY for gaps and cells outside the grid.
char vk_key_at(const VirtualKeyboard* self, int32_t x, int32_t y);

char vk_key_under_cursor(const VirtualKeyboard* self);

// Moves the cursor one step, wrapping at the edges and skipping gaps. The cursor stays put when the page has no keys.
void vk_move_cursor(VirtualKeyboard* self, int32_t dx, int32_t dy);

// Sends the cursor to the first key of the current page.
void vk_reset_cursor(VirtualKeyboard* self);
