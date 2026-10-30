#pragma once

#include <core/string.h>
#include <core/types.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Type-ahead matcher - the "type a few letters to jump to a row" gesture, over a caller-owned label slice.
// Folding is ASCII case-insensitive only (UTF-8 bytes >= 0x80 compare verbatim).
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

// Fold this frame's typed text into state's prefix and find the row it should jump to. Returns the target
// index into labels, or -1 when there is nothing to type-ahead to (no labels, no text this frame, or no match).
// current_index is the current cursor index (-1 if unset). The search starts AFTER the cursor for a
// single-letter prefix (so repeating a letter cycles through matches) and AT the cursor for a growing multi-byte
// prefix (so the current row stays put while the prefix narrows); it wraps once around the full count. now is
// the caller's input-clock time, used to expire a stale prefix. prefix_cap (<= TYPE_AHEAD_CAP) bounds the prefix
// length.
i32 type_ahead_match(TypeAheadState* state, const FlString* labels, u32 count, i32 current_index, FlString typed,
                     f64 now, u32 prefix_cap);
