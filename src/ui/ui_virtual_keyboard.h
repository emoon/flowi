#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Virtual Keyboard - Internal API
//
// A console-style on-screen keyboard for gamepad users: a grid QWERTY layout with wrapping D-pad navigation,
// page switching (lowercase, uppercase, numbers, symbols) and the Space/Backspace/Enter/Cancel actions.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <core/core.h>
#include <core/string.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Forward declarations

typedef struct FlArena FlArena;
typedef struct FlUiCtx FlUiCtx;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Virtual Keyboard Types

// Special key codes for virtual keyboard (used in key grids)
#define VK_KEY_SPACE ' '
#define VK_KEY_BACKSPACE 0x01
#define VK_KEY_ENTER 0x02
#define VK_KEY_CANCEL 0x03
#define VK_KEY_SHIFT 0x04
#define VK_KEY_PAGE_123 0x05
#define VK_KEY_PAGE_ABC 0x06
#define VK_KEY_PAGE_SYM 0x07
#define VK_KEY_EMPTY 0x00 // Empty cell in grid

// Keyboard page/layout
typedef enum VirtualKeyboardPage {
    VK_PAGE_LOWERCASE = 0, // a-z, common punctuation
    VK_PAGE_UPPERCASE = 1, // A-Z, common punctuation
    VK_PAGE_NUMBERS = 2,   // 0-9, mathematical symbols
    VK_PAGE_SYMBOLS = 3,   // Special characters (!@#$%^&*...)
    VK_PAGE_COUNT = 4
} VirtualKeyboardPage;

typedef struct VirtualKeyboardConfig {
    // Text constraints
    uint32_t max_length;    // Character limit (0 = unlimited)
    FlString allowed_chars; // Allowed character set (empty = all)

    // Visual
    FlString title;            // Title text ("Enter save name:")
    FlString initial_text;     // Pre-filled text
    bool show_character_count; // Show "15/100" counter

    // Behavior
    VirtualKeyboardPage initial_page; // Starting page (default: lowercase)
} VirtualKeyboardConfig;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Virtual Keyboard State (Internal)

typedef struct VirtualKeyboard {
    // Configuration
    VirtualKeyboardConfig config;
    FlArena* arena; // Arena for allocations

    // Text buffer
    char* buffer;
    uint32_t buffer_capacity;
    uint32_t buffer_length;

    // Grid state
    VirtualKeyboardPage current_page;
    int32_t cursor_x; // Grid cursor position
    int32_t cursor_y;

    // Result
    bool submitted;
    bool cancelled;
    bool active; // Still running
} VirtualKeyboard;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main API Function

// Non-blocking API for integration with immediate-mode UI.
// The arena is dedicated to a single active keyboard; the caller rewinds it before each create.
VirtualKeyboard* ui_virtual_keyboard_create(FlArena* arena, VirtualKeyboardConfig config);
void ui_virtual_keyboard_update(VirtualKeyboard* self, FlUiCtx* ctx);
void ui_virtual_keyboard_render(VirtualKeyboard* self, FlUiCtx* ctx);

#ifdef __cplusplus
}
#endif
