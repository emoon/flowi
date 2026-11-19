#include "utf8.h"

static bool is_invalid_scalar(u32 cp) {
    return cp > UTF8_MAX_CODEPOINT || (cp >= 0xD800 && cp <= 0xDFFF);
}

static bool decode_continuations(const u8* bytes, u32 len, u32* cp) {
    for (u32 i = 1; i < len; i++) {
        u8 b = bytes[i];
        if (!utf8_is_continuation(b)) {
            return false;
        }
        *cp = (*cp << 6) | (b & 0x3F);
    }

    return true;
}

static bool is_overlong(u32 cp, u32 len) {
    static const u32 min_cp[] = { 0, 0, 0x80, 0x800, 0x10000 };
    return cp < min_cp[len];
}

// Bounds-checked UTF-8 decoder

const char* utf8_decode_safe(const char* ptr, const char* end, u32* codepoint) {
    if (ptr >= end) {
        *codepoint = 0;
        return nullptr;
    }

    u8 b0 = (u8)*ptr;

    // ASCII fast path
    if (b0 < 0x80) {
        *codepoint = b0;
        return ptr + 1;
    }

    u32 len = utf8_sequence_length(b0);
    if (len == 0 || ptr + len > end) {
        *codepoint = UTF8_REPLACEMENT_CHAR;
        return ptr + 1;
    }

    static const u8 masks[] = { 0, 0x7F, 0x1F, 0x0F, 0x07 };
    u32 cp = b0 & masks[len];

    if (!decode_continuations((const u8*)ptr, len, &cp)) {
        *codepoint = UTF8_REPLACEMENT_CHAR;
        return ptr + 1;
    }

    if (is_invalid_scalar(cp) || is_overlong(cp, len)) {
        *codepoint = UTF8_REPLACEMENT_CHAR;
        return ptr + len;
    }

    *codepoint = cp;
    return ptr + len;
}

u64 utf8_codepoint_count(FlString str) {
    u64 count = 0;
    Utf8Iter it = utf8_iter(str);

    while (!utf8_iter_done(&it)) {
        utf8_iter_next(&it);
        count++;
    }

    return count;
}

u32 utf8_encode(char* buf, u32 cp) {
    if (is_invalid_scalar(cp)) {
        return 0;
    }

    if (cp < 0x80) {
        buf[0] = (char)cp;
        return 1;
    }

    if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }

    if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }

    buf[0] = (char)(0xF0 | (cp >> 18));
    buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    buf[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static bool is_literal_replacement_char(const u8* bytes, u64 remaining) {
    return remaining >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBF && bytes[2] == 0xBD;
}

bool utf8_is_valid(FlString str) {
    const char* ptr = str.data;
    const char* end = str.data + str.length;

    while (ptr < end) {
        u32 cp;
        const char* next = utf8_decode_safe(ptr, end, &cp);

        if (cp == UTF8_REPLACEMENT_CHAR && !is_literal_replacement_char((const u8*)ptr, (u64)(end - ptr))) {
            return false;
        }

        ptr = next;
    }

    return true;
}
