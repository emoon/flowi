#include "utf8.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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
    if (len == 0) {
        // Invalid lead byte
        *codepoint = UTF8_REPLACEMENT_CHAR;
        return ptr + 1;
    }

    if (ptr + len > end) {
        *codepoint = UTF8_REPLACEMENT_CHAR;
        return ptr + 1;
    }

    // Extract initial bits from lead byte
    static const u8 masks[] = { 0, 0x7F, 0x1F, 0x0F, 0x07 };
    u32 cp = b0 & masks[len];

    for (u32 i = 1; i < len; i++) {
        u8 b = (u8)ptr[i];
        if (!utf8_is_continuation(b)) {
            *codepoint = UTF8_REPLACEMENT_CHAR;
            return ptr + 1;
        }
        cp = (cp << 6) | (b & 0x3F);
    }

    // Validate: not a surrogate, not out of range
    if (cp > UTF8_MAX_CODEPOINT || (cp >= 0xD800 && cp <= 0xDFFF)) {
        *codepoint = UTF8_REPLACEMENT_CHAR;
        return ptr + len;
    }

    // Validate: not overlong
    static const u32 min_cp[] = { 0, 0, 0x80, 0x800, 0x10000 };
    if (cp < min_cp[len]) {
        *codepoint = UTF8_REPLACEMENT_CHAR;
        return ptr + len;
    }

    *codepoint = cp;
    return ptr + len;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u64 utf8_codepoint_count(FlString str) {
    u64 count = 0;
    Utf8Iter it = utf8_iter(str);

    while (!utf8_iter_done(&it)) {
        utf8_iter_next(&it);
        count++;
    }

    return count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u32 utf8_encode(char* buf, u32 cp) {
    if (cp > UTF8_MAX_CODEPOINT || (cp >= 0xD800 && cp <= 0xDFFF)) {
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool utf8_is_valid(FlString str) {
    const char* ptr = str.data;
    const char* end = str.data + str.length;

    while (ptr < end) {
        u32 cp;
        const char* next = utf8_decode_safe(ptr, end, &cp);

        // Replacement char means invalid (unless input actually contains U+FFFD)
        if (cp == UTF8_REPLACEMENT_CHAR && ptr < end) {
            // Check if it's actually U+FFFD in the input (EF BF BD)
            if (end - ptr >= 3 && (u8)ptr[0] == 0xEF && (u8)ptr[1] == 0xBF && (u8)ptr[2] == 0xBD) {
                ptr = next;
                continue;
            }
            return false;
        }

        ptr = next;
        if (ptr == nullptr) {
            break;
        }
    }

    return true;
}
