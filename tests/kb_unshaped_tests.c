#include "kb_route_fixture.h"
#include <string.h>

#include "fixtures/fonts/damaged_font.h"
#include "fixtures/fonts/fixed_pitch_test_fonts.h"
#include "fixtures/fonts/glyph_test_fonts.h"

UTEST(kb_unshaped, fixed_pitch_and_builtin_faces_stay_unshaped) {
    KbRoute route = kb_route_open();
    FlFontId mono = font_cache_load_font_memory(route.cache, k_fixed_pitch_test_ttf, k_fixed_pitch_test_ttf_size, 16);
    FlFontId builtin = font_cache_load_default_font(route.cache, 2);
    const u16 q4 = 13 * 4;
    const FlString text = { .data = "AV na\xC3\xAFve", .length = 9, .is_static = 1 };

    const FlShapedRun shaped = font_cache_shape_run(route.cache, mono, q4, text);
    i64 pen = 0;
    const u32 codepoints[] = { 'A', 'V', ' ', 'n', 'a', 0xEF, 'v', 'e' };
    ASSERT_EQ((int)shaped.count, (int)sizeof_array(codepoints));
    for_count(i, sizeof_array(codepoints)) {
        ASSERT_TRUE(shaped.glyphs[i].x == pen);
        pen += font_cache_get_glyph_advance_q4(route.cache, mono, q4, codepoints[i]);
    }
    ASSERT_TRUE(shaped.width == pen);
    ASSERT_TRUE(font_cache_shape_run(route.cache, builtin, 0, FL_S("AV")).width == 2 * 16 * 64);

    kb_route_close(&route);
}

static FlFontId load_damaged_roboto(const KbRoute* route) {
    u8* bytes = arena_alloc_array(route->arena, u8, route->roboto_file.size);
    memcpy(bytes, route->roboto_file.data, route->roboto_file.size);
    damage_table_tail(bytes, "GSUB");
    return font_cache_load_font_memory(route->cache, bytes, (i64)route->roboto_file.size, 16);
}

static i64 summed_advances(FlFontCache* cache, FlFontId font, u16 q4, FlString text) {
    i64 sum = 0;
    for_count(i, text.length) {
        sum += font_cache_get_glyph_advance_q4(cache, font, q4, (unsigned char)text.data[i]);
    }
    return sum;
}

UTEST(kb_unshaped, face_kb_cannot_load_stays_unshaped_and_still_draws) {
    KbRoute route = kb_route_open();
    const FlFontId damaged = load_damaged_roboto(&route);
    const FlString text = FL_S("AVATAR Wave");
    const u16 q4 = 22 * 4;
    ASSERT_TRUE(damaged != FONT_ID_INVALID);

    const i64 sum = summed_advances(route.cache, damaged, q4, text);
    const FlTextStyle style = { .font_id = damaged, .font_size = 22.0f };
    ASSERT_GT(sum, 0);
    ASSERT_EQ(font_cache_measure_text(route.cache, text, style).width, (float)sum / 64.0f);

    const FlShapedRun run = font_cache_shape_run(route.cache, damaged, q4, text);
    ASSERT_EQ((int)run.count, (int)text.length);
    ASSERT_TRUE(run.width == sum);
    ASSERT_EQ((int)run.glyphs[0].glyph_index, (int)font_cache_get_glyph_index(route.cache, damaged, 'A'));
    ASSERT_GT(font_cache_shape_run(route.cache, route.roboto, q4, text).width, 0);
    ASSERT_TRUE(font_cache_shape_run(route.cache, route.roboto, q4, text).width != sum);

    kb_route_close(&route);
}

UTEST(kb_unshaped, fallback_kb_cannot_load_keeps_the_whole_run_unshaped) {
    KbRoute route = kb_route_open();
    const FlFontId primary = font_cache_load_font_memory(route.cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    const FlFontId damaged = load_damaged_roboto(&route);
    font_cache_set_fallback_font(route.cache, damaged);
    const FlString text = FL_S("AoB");
    const u16 q4 = 13 * 4;

    const i64 sum = summed_advances(route.cache, primary, q4, text);
    const FlShapedRun run = font_cache_shape_run(route.cache, primary, q4, text);
    ASSERT_EQ((int)run.count, 3);
    ASSERT_TRUE(run.width == sum);
    ASSERT_EQ((int)run.glyphs[2].font, (int)damaged);
    ASSERT_EQ((int)run.glyphs[2].glyph_index, (int)font_cache_get_glyph_index(route.cache, damaged, 'B'));
    ASSERT_GT(font_cache_get_glyph_advance_q4(route.cache, primary, q4, 'B'), 0);

    kb_route_close(&route);
}
