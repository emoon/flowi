#include "kb_route_fixture.h"

static float measured(const KbRoute* route, FlString text, float size, float spacing) {
    const FlTextStyle style = { .font_id = route->roboto, .font_size = size, .letter_spacing = spacing };
    return font_cache_measure_text(route->cache, text, style).width;
}

UTEST(kb_route, kerned_pair_is_narrower_than_its_glyphs_alone) {
    KbRoute route = kb_route_open();
    ASSERT_TRUE(route.roboto != FONT_ID_INVALID);

    const float pair = measured(&route, FL_S("AV"), 28.0f, 0.0f);
    const float apart = measured(&route, FL_S("A"), 28.0f, 0.0f) + measured(&route, FL_S("V"), 28.0f, 0.0f);
    ASSERT_GT(pair, 0.0f);
    ASSERT_LT(pair, apart);

    kb_route_close(&route);
}

static bool measure_matches_shape(const KbRoute* route, FlString text, float size, float spacing) {
    const FlShapedRun run = font_cache_shape_run(route->cache, route->roboto, font_cache_quantize_size_q4(size), text);
    float width = (float)run.width / 64.0f;
    if (run.count > 1) {
        width += spacing * (float)(run.count - 1);
    }
    return measured(route, text, size, spacing) == width;
}

UTEST(kb_route, measure_equals_the_shaped_width) {
    KbRoute route = kb_route_open();
    const FlString text = FL_S("AVATAR Wave. To: fjord office");
    const float sizes[] = { 14.0f, 22.25f };
    const float spacings[] = { 0.0f, 1.25f };

    for_count(i, sizeof_array(sizes) * sizeof_array(spacings)) {
        const float size = sizes[i % sizeof_array(sizes)];
        const float spacing = spacings[i / sizeof_array(sizes)];
        ASSERT_TRUE_MSG(measure_matches_shape(&route, text, size, spacing), "measure and draw share one shaped width");
    }
    ASSERT_EQ_MSG(font_cache_get_text_size(route.cache, text, route.roboto, 14).x, measured(&route, text, 14.0f, 0.0f),
                  "the whole-px measure is shaped too");

    kb_route_close(&route);
}

UTEST(kb_route, glyph_positions_run_left_to_right) {
    KbRoute route = kb_route_open();
    const FlString text = FL_S("AVATAR Wave. To: fjord");
    const FlShapedRun run = font_cache_shape_run(route.cache, route.roboto, 14 * 4, text);

    ASSERT_GT(run.count, 0u);
    ASSERT_EQ(run.glyphs[0].x, 0);
    for_count(i, run.count - 1) {
        ASSERT_LE(run.glyphs[i].x, run.glyphs[i + 1].x);
        ASSERT_EQ((int)run.glyphs[i].font, (int)route.roboto);
    }
    ASSERT_LT((i64)run.glyphs[run.count - 1].x, run.width);

    kb_route_close(&route);
}

UTEST(kb_route, shaped_width_scales_with_the_size_key) {
    KbRoute route = kb_route_open();
    const FlString text = FL_S("AVATAR Wave");
    const i64 small = font_cache_shape_run(route.cache, route.roboto, 14 * 4, text).width;
    const i64 large = font_cache_shape_run(route.cache, route.roboto, 28 * 4, text).width;

    ASSERT_GT(small, 0);
    ASSERT_LE(large - 2 * small, 1);
    ASSERT_GE(large - 2 * small, -1);

    kb_route_close(&route);
}

UTEST(kb_route, empty_and_invalid_text_leave_the_shaper_usable) {
    KbRoute route = kb_route_open();
    const FlString invalid = { .data = "A\xFF\xC3", .length = 3, .is_static = 1 };

    ASSERT_EQ((int)font_cache_shape_run(route.cache, route.roboto, 56, FL_S("")).count, 0);
    font_cache_shape_run(route.cache, route.roboto, 56, invalid);
    ASSERT_EQ((int)font_cache_shape_run(route.cache, route.roboto, 56, FL_S("AV")).count, 2);

    kb_route_close(&route);
}

UTEST(kb_route, measuring_keeps_a_shaped_run_valid) {
    KbRoute route = kb_route_open();
    const FlString text = FL_S("AVATAR Wave");
    const FlShapedRun run = font_cache_shape_run(route.cache, route.roboto, 56, text);
    const i32 last_x = run.glyphs[run.count - 1].x;
    const u32 last_index = run.glyphs[run.count - 1].glyph_index;

    measured(&route, FL_S("Toy Tracker, Today. A much longer line than the shaped one"), 22.0f, 0.0f);
    font_cache_get_text_size(route.cache, FL_S("office fjord"), route.roboto, 14);

    ASSERT_EQ(run.glyphs[run.count - 1].x, last_x);
    ASSERT_EQ((int)run.glyphs[run.count - 1].glyph_index, (int)last_index);
    ASSERT_GT(last_x, 0);

    kb_route_close(&route);
}
