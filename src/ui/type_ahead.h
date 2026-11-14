#pragma once

#include <core/string.h>
#include <core/types.h>

// Type-ahead matcher: the "type a few letters to jump to a row" gesture over caller-owned labels.
// Folding is ASCII case-insensitive only (UTF-8 bytes >= 0x80 compare verbatim).

// How many bytes of typed prefix accumulate, and the idle gap (seconds) after which a keystroke starts a new
// prefix.
#define TYPE_AHEAD_CAP 31
#define TYPE_AHEAD_TIMEOUT 1.0

// Type-ahead prefix accumulated across frames. Zero-initialize before first use (a deadline of 0 makes the first
// keystroke start a clean prefix).
typedef struct TypeAheadState {
    char buf[TYPE_AHEAD_CAP + 1];
    u32 len;
    f64 deadline; // input-clock time after which the prefix is considered stale
} TypeAheadState;

typedef struct TypeAheadLabels {
    const void* source;
    FlString (*label_at)(const void* source, u32 row);
    u32 count;
} TypeAheadLabels;

// Fold this frame's typed text into state's prefix. Returns false when there is nothing to search for (no text
// this frame, or only control bytes so far). now is the caller's input-clock time, used to expire a stale prefix.
// prefix_cap (<= TYPE_AHEAD_CAP) bounds the prefix length.
bool type_ahead_feed(TypeAheadState* state, FlString typed, f64 now, u32 prefix_cap);

// Find the row the prefix in state should jump to, or -1 when no label matches. current_index is the current
// cursor index (-1 if unset). The search starts after the cursor for a single-letter prefix (so repeating a
// letter cycles through matches) and at the cursor for a growing multi-byte prefix (so the current row stays put
// while the prefix narrows); it wraps once around the full count.
i32 type_ahead_find(const TypeAheadState* state, TypeAheadLabels labels, i32 current_index);
