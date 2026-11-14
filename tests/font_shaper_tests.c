#include "utest/utest.h"
#include <core/arena.h>
#include <core/core.h>
#include <core/os/os.h>
#include <core/string.h>
#include <limits.h>
#include <string.h>
#include <ui/font_shaper.h>

#include "fixtures/fonts/glyph_test_fonts.h"

UTEST(font_shaper, creates_a_font_from_font_bytes) {
    FlArena* arena = arena_new();
    FontShaperFont* font = font_shaper_font_new(arena, k_glyph_test_ttf, k_glyph_test_ttf_size);
    ASSERT_TRUE(font != nullptr);
    fl_arena_destroy(arena);
}

UTEST(font_shaper, rejects_bytes_that_are_not_a_font) {
    static const u8 garbage[64] = { 1, 2, 3, 4 };
    FlArena* arena = arena_new();
    ASSERT_TRUE(font_shaper_font_new(arena, garbage, sizeof(garbage)) == nullptr);
    ASSERT_TRUE(font_shaper_font_new(arena, nullptr, 0) == nullptr);
    ASSERT_TRUE(font_shaper_font_new(arena, garbage, 0) == nullptr);
    ASSERT_TRUE(font_shaper_font_new(arena, garbage, (i64)INT_MAX + 1) == nullptr);
    fl_arena_destroy(arena);
}

static u32 read_be32(const u8* bytes) {
    return ((u32)bytes[0] << 24) | ((u32)bytes[1] << 16) | ((u32)bytes[2] << 8) | bytes[3];
}

static void damage_table_tail(u8* font, const char* tag) {
    const u32 table_count = ((u32)font[4] << 8) | font[5];
    for_count(i, table_count) {
        const u8* record = font + 12 + i * 16;
        if (memcmp(record, tag, 4) == 0) {
            const u32 offset = read_be32(record + 8);
            const u32 length = read_be32(record + 12);
            memset(font + offset + length / 2, 0xFF, length - length / 2);
        }
    }
}

UTEST(font_shaper, rejects_a_font_with_damaged_shaping_tables) {
    FileMmap file = file_mmap_open_os(FL_S(FLOWI_TEST_DATA_DIR "/Roboto-Regular.ttf"));
    ASSERT_TRUE(file.data != nullptr);

    FlArena* arena = arena_new();
    u8* font = arena_alloc_array(arena, u8, file.size);
    memcpy(font, file.data, file.size);
    ASSERT_TRUE(font_shaper_font_new(arena, font, (i64)file.size) != nullptr);

    damage_table_tail(font, "GSUB");
    ASSERT_TRUE(font_shaper_font_new(arena, font, (i64)file.size) == nullptr);

    fl_arena_destroy(arena);
    file_mmap_close_os(&file);
}
