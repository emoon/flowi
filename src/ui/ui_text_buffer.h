#pragma once

#include <core/arena.h>
#include <core/memory.h>
#include <core/string.h>
#include <ctype.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared text-buffer helpers
//
// Character filtering and arena-backed buffer growth shared by the text-input widget
// (ui_text_input.c) and the on-screen virtual keyboard (ui_virtual_keyboard.c).

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Decide whether a character may be inserted into a text buffer.
//
// With no explicit allow-list, printable characters plus '\n', '\r' and '\t' are permitted.
// When allowed_chars is non-empty it is treated as an exact allow-list: only characters present
// in it pass, regardless of the rule above.

static inline bool text_buffer_filter_char(char c, FlString allowed_chars) {
    if (allowed_chars.length == 0) {
        return isprint((unsigned char)c) || c == '\n' || c == '\r' || c == '\t';
    }

    for (u64 i = 0; i < allowed_chars.length; i++) {
        if (c == allowed_chars.data[i]) {
            return true;
        }
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Grow an arena-backed text buffer so it can hold at least min_capacity bytes.
//
// No-op when the buffer already has room. Otherwise it grows by capacity-doubling with a 256-byte
// floor, clamped up to min_capacity if doubling is still too small, and copies the first length
// existing bytes into the new allocation. *buffer and *capacity are updated in place; callers do
// their own write.

static inline void text_buffer_grow(FlArena* arena, char** buffer, u32* capacity, u32 length, u32 min_capacity) {
    if (*capacity >= min_capacity) {
        return;
    }

    u32 new_capacity = *capacity * 2;
    if (new_capacity < 256) {
        new_capacity = 256;
    }
    if (new_capacity < min_capacity) {
        new_capacity = min_capacity;
    }

    char* new_buffer = arena_alloc_array(arena, char, new_capacity);
    if (length > 0) {
        memory_copy(new_buffer, new_capacity, *buffer, length);
    }
    *buffer = new_buffer;
    *capacity = new_capacity;
}
