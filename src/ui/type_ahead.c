#include "type_ahead.h"

// ASCII lowercase fold. UTF-8 bytes >= 0x80 pass through unchanged, so type-ahead is case-insensitive over
// ASCII only.
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

// Control bytes such as Enter and Tab are not part of a prefix.
static bool type_ahead_is_printable(char c) {
    return (unsigned char)c >= 0x20 && c != 0x7f;
}

// A single repeated letter cycles to the next match instead of growing the prefix, so the prefix stays at one
// byte and the search advances past the current row.
static bool type_ahead_repeats_single(const TypeAheadState* state, char c) {
    return state->len == 1 && type_ahead_ascii_lower(c) == type_ahead_ascii_lower(state->buf[0]);
}

static void type_ahead_append(TypeAheadState* state, char c, u32 prefix_cap) {
    if (!type_ahead_is_printable(c) || type_ahead_repeats_single(state, c)) {
        return;
    }
    if (state->len < prefix_cap) {
        state->buf[state->len++] = c;
    }
}

bool type_ahead_feed(TypeAheadState* state, FlString typed, f64 now, u32 prefix_cap) {
    if (typed.length == 0 || typed.data == nullptr) {
        return false;
    }

    if (now > state->deadline) {
        state->len = 0; // stale prefix: typing paused, start over
    }

    for_count(i, typed.length) {
        type_ahead_append(state, typed.data[i], prefix_cap);
    }
    state->deadline = now + TYPE_AHEAD_TIMEOUT;
    return state->len > 0;
}

i32 type_ahead_find(const TypeAheadState* state, TypeAheadLabels labels, i32 current_index) {
    if (state->len == 0) {
        return -1;
    }

    // Single-letter prefix advances past the current row (cycle); a longer prefix re-tests the current row
    // first (narrowing). With no cursor yet, start at the top.
    i32 start = (state->len > 1) ? current_index : current_index + 1;
    if (start < 0) {
        start = 0;
    }
    for_count(k, labels.count) {
        u32 idx = ((u32)start + k) % labels.count;
        if (type_ahead_label_has_prefix(labels.label_at(labels.source, idx), state->buf, state->len)) {
            return (i32)idx;
        }
    }
    return -1;
}
