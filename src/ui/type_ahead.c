///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Type-ahead matcher implementation. See type_ahead.h for the shape of the gesture.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "type_ahead.h"

// ASCII lowercase fold (UTF-8 bytes >= 0x80 pass through unchanged - type-ahead is case-insensitive over ASCII
// only).
static char type_ahead_ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

static bool type_ahead_label_has_prefix(FlString label, const char* prefix, u32 len) {
    if ((u64)len > label.length) {
        return false;
    }
    for (u32 i = 0; i < len; i++) {
        if (type_ahead_ascii_lower(label.data[i]) != type_ahead_ascii_lower(prefix[i])) {
            return false;
        }
    }
    return true;
}

i32 type_ahead_match(TypeAheadState* state, const FlString* labels, u32 count, i32 current_index, FlString typed,
                     f64 now, u32 prefix_cap) {
    if (labels == nullptr) {
        return -1;
    }
    if (typed.length == 0 || typed.data == nullptr) {
        return -1;
    }

    if (now > state->deadline) {
        state->len = 0; // stale prefix: typing paused, start over
    }

    // Append the printable bytes typed this frame (control bytes - Enter, Tab - are not part of a prefix).
    for (u64 i = 0; i < typed.length; i++) {
        char c = typed.data[i];
        if ((unsigned char)c < 0x20 || c == 0x7f) {
            continue;
        }
        // A single repeated letter is a "cycle to next match" gesture, not a growing prefix - keep the prefix
        // at one byte so the search below advances past the current row.
        if (state->len == 1 && type_ahead_ascii_lower(c) == type_ahead_ascii_lower(state->buf[0])) {
            continue;
        }
        if (state->len < prefix_cap) {
            state->buf[state->len++] = c;
        }
    }
    state->deadline = now + TYPE_AHEAD_TIMEOUT;
    if (state->len == 0) {
        return -1;
    }

    // Single-letter prefix advances past the current row (cycle); a longer prefix re-tests the current row
    // first (narrowing). With no cursor yet, start at the top.
    i32 start = (state->len > 1) ? current_index : current_index + 1;
    if (start < 0) {
        start = 0;
    }
    for (u32 k = 0; k < count; k++) {
        u32 idx = ((u32)start + k) % count;
        if (type_ahead_label_has_prefix(labels[idx], state->buf, state->len)) {
            return (i32)idx;
        }
    }
    return -1;
}
