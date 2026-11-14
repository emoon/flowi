#pragma once

// UTF-8 decoding utilities

#include "string.h"
#include "types.h"

// U+FFFD, substituted for invalid UTF-8 sequences
#define UTF8_REPLACEMENT_CHAR 0xFFFD

// Maximum valid Unicode codepoint
#define UTF8_MAX_CODEPOINT 0x10FFFF

// Decode next codepoint with bounds checking.
// Returns nullptr and sets *codepoint to 0 when end is reached.
// Invalid sequences produce UTF8_REPLACEMENT_CHAR.
const char* utf8_decode_safe(const char* ptr, const char* end, u32* codepoint);

// UTF-8 string iteration helper

typedef struct Utf8Iter {
    const char* ptr; // Current position
    const char* end; // End of string
} Utf8Iter;

static inline Utf8Iter utf8_iter(FlString str) {
    return (Utf8Iter) { .ptr = str.data, .end = str.data + str.length };
}

static inline bool utf8_iter_done(const Utf8Iter* it) {
    return it->ptr >= it->end;
}

static inline u32 utf8_iter_next(Utf8Iter* it) {
    if (it->ptr >= it->end) {
        return 0;
    }
    u32 cp;
    it->ptr = utf8_decode_safe(it->ptr, it->end, &cp);
    return cp;
}

// Count codepoints in UTF-8 string (O(n))
u64 utf8_codepoint_count(FlString str);

// Encode codepoint to UTF-8. Buffer must have 4 bytes available.
// Returns bytes written (1-4), or 0 for invalid codepoints.
u32 utf8_encode(char* buf, u32 codepoint);

// True when str is valid UTF-8.
bool utf8_is_valid(FlString str);

static inline bool utf8_is_continuation(u8 byte) {
    return (byte & 0xC0) == 0x80;
}

static inline u32 utf8_sequence_length(u8 first_byte) {
    if (first_byte < 0x80)
        return 1;
    if (first_byte >= 0xC2 && first_byte <= 0xDF)
        return 2;
    if ((first_byte & 0xF0) == 0xE0)
        return 3;
    if (first_byte >= 0xF0 && first_byte <= 0xF4)
        return 4;
    return 0;
}
