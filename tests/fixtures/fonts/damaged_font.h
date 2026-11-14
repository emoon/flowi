#pragma once

#include <core/types.h>
#include <string.h>

static inline u32 font_read_be32(const u8* bytes) {
    return ((u32)bytes[0] << 24) | ((u32)bytes[1] << 16) | ((u32)bytes[2] << 8) | bytes[3];
}

static inline void damage_table_tail(u8* font, const char* tag) {
    const u32 table_count = ((u32)font[4] << 8) | font[5];
    for_count(i, table_count) {
        const u8* record = font + 12 + i * 16;
        if (memcmp(record, tag, 4) == 0) {
            const u32 offset = font_read_be32(record + 8);
            const u32 length = font_read_be32(record + 12);
            memset(font + offset + length / 2, 0xFF, length - length / 2);
        }
    }
}
