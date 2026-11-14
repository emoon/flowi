#include "kb_route_fixture.h"
#include <core/stats.h>

#include "fixtures/fonts/fallback_test_font.h"
#include "fixtures/fonts/fixed_pitch_test_fonts.h"
#include "fixtures/fonts/glyph_test_fonts.h"

static u64 cache_stat(const KbRoute* route, FlString name) {
    return stats_get_value_by_name(font_cache_stats(route->cache), name, FL_S("shaped_run_cache"));
}

UTEST(shaped_run_font_cache, measure_and_draw_share_one_entry) {
    KbRoute route = kb_route_open();
    ASSERT_TRUE(route.roboto != FONT_ID_INVALID);
    const FlString text = FL_S("AVATAR Wave. To: fjord office");
    const FlTextStyle style = { .font_id = route.roboto, .font_size = 14.0f };

    const float measured = font_cache_measure_text(route.cache, text, style).width;
    const FlShapedRun drawn = font_cache_shape_run(route.cache, route.roboto, 56, text);
    font_cache_shape_run(route.cache, route.roboto, 88, text);

    ASSERT_EQ(measured, (float)drawn.width / 64.0f);
    ASSERT_EQ(cache_stat(&route, FL_S("misses")), 1ULL);
    ASSERT_EQ(cache_stat(&route, FL_S("hits")), 2ULL);

    kb_route_close(&route);
}

UTEST(shaped_run_font_cache, unshaped_faces_never_reach_the_cache) {
    KbRoute route = kb_route_open();
    const FlFontId mono
        = font_cache_load_font_memory(route.cache, k_fixed_pitch_test_ttf, k_fixed_pitch_test_ttf_size, 16);
    const FlFontId builtin = font_cache_load_default_font(route.cache, 2);
    ASSERT_TRUE(font_cache_is_fixed_pitch(route.cache, mono));

    font_cache_shape_run(route.cache, mono, 56, FL_S("AB AB"));
    font_cache_shape_run(route.cache, builtin, 0, FL_S("AV"));
    font_cache_get_text_size(route.cache, FL_S("AB"), mono, 14);

    ASSERT_EQ(cache_stat(&route, FL_S("misses")) + cache_stat(&route, FL_S("hits")), 0ULL);
    ASSERT_EQ(cache_stat(&route, FL_S("bytes_used")), 0ULL);

    kb_route_close(&route);
}

UTEST(shaped_run_font_cache, a_new_fallback_empties_the_cache) {
    KbRoute route = kb_route_open();
    const FlFontId primary = font_cache_load_font_memory(route.cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    const FlFontId fallback
        = font_cache_load_font_memory(route.cache, k_fallback_test_ttf, k_fallback_test_ttf_size, 32);
    const u32 unmapped = font_cache_shape_run(route.cache, primary, 128, FL_S("AoB")).glyphs[2].glyph_index;
    const u64 cached = cache_stat(&route, FL_S("bytes_used"));

    font_cache_set_fallback_font(route.cache, fallback);
    const u64 cleared = cache_stat(&route, FL_S("bytes_used"));
    const FlShapedRun run = font_cache_shape_run(route.cache, primary, 128, FL_S("AoB"));

    ASSERT_EQ((int)unmapped, 0);
    ASSERT_GT(cached, cleared);
    ASSERT_EQ((int)run.glyphs[2].font, (int)fallback);

    kb_route_close(&route);
}

UTEST(shaped_run_font_cache, an_entry_survives_an_update_and_a_wait) {
    KbRoute route = kb_route_open();
    font_cache_shape_run(route.cache, route.roboto, 56, FL_S("office"));

    font_cache_update(route.cache);
    font_cache_wait_loaded(route.cache, route.roboto);
    font_cache_shape_run(route.cache, route.roboto, 56, FL_S("office"));

    ASSERT_EQ(cache_stat(&route, FL_S("hits")), 1ULL);

    kb_route_close(&route);
}
