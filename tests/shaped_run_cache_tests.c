#include "shaped_run_cache_fixture.h"

#include "fixtures/fonts/fallback_test_font.h"
#include "fixtures/fonts/glyph_test_fonts.h"

static FlShapedRun kept(RunCacheRig* rig, FlShapedRun run) {
    FlShapedGlyph* copy = arena_alloc_array(rig->arena, FlShapedGlyph, run.count + 1);
    memcpy(copy, run.glyphs, run.count * sizeof(FlShapedGlyph));
    run.glyphs = copy;
    return run;
}

static bool hit_matches_miss(RunCacheRig* rig, const FontShaperRun* run, FlString text) {
    FlShapedRun miss = { 0 };
    FlShapedRun hit = { 0 };
    const u64 hits = run_cache_stat(rig, FL_S("hits"));
    shaped_run_cache_get(rig->cache, run, text, &miss);
    miss = kept(rig, miss);
    shaped_run_cache_get(rig->cache, run, text, &hit);
    const bool counted = run_cache_stat(rig, FL_S("hits")) == hits + 1;
    return counted && miss.count > 0 && run_cache_same_glyphs(miss, hit);
}

UTEST(shaped_run_cache, a_hit_gives_the_glyphs_of_the_miss) {
    RunCacheRig rig = run_cache_rig_open(KB(64));
    ASSERT_TRUE(run_cache_rig_ready(&rig));

    ASSERT_TRUE(hit_matches_miss(&rig, &rig.draw, FL_S("Toy Tracker, Today. AVATAR Wave")));
    ASSERT_TRUE_MSG(hit_matches_miss(&rig, &rig.draw, FL_S("x\xCC\x81 e\xCC\x8A")), "GPOS offsets are kept");
    ASSERT_EQ(run_cache_stat(&rig, FL_S("misses")), 2ULL);

    run_cache_rig_close(&rig);
}

UTEST(shaped_run_cache, a_run_with_fallback_glyphs_keeps_its_faces) {
    RunCacheRig rig = run_cache_rig_open(KB(64));
    const FontShaperRun run = {
        .primary = { .font = font_shaper_font_new(rig.arena, k_glyph_test_ttf, k_glyph_test_ttf_size), .id = 2 },
        .fallback = { .font = font_shaper_font_new(rig.arena, k_fallback_test_ttf, k_fallback_test_ttf_size), .id = 3 },
        .size_q4 = 128,
        .glyph_arena = rig.glyphs,
    };
    FlShapedRun hit = { 0 };

    ASSERT_TRUE(hit_matches_miss(&rig, &run, FL_S("BoAoBBAoA")));
    shaped_run_cache_get(rig.cache, &run, FL_S("AoB"), &hit);
    shaped_run_cache_get(rig.cache, &run, FL_S("AoB"), &hit);
    ASSERT_EQ((int)hit.glyphs[1].font, 2);
    ASSERT_EQ((int)hit.glyphs[2].font, 3);

    run_cache_rig_close(&rig);
}

UTEST(shaped_run_cache, one_entry_serves_every_size) {
    RunCacheRig rig = run_cache_rig_open(KB(64));
    RunCacheRig fresh = run_cache_rig_open(KB(64));
    const FlString text = FL_S("AVATAR Wave. To: fjord office");
    run_cache_draw(&rig, text);

    rig.draw.size_q4 = 89;
    fresh.draw.size_q4 = 89;
    const FlShapedRun hit = run_cache_draw(&rig, text);
    const FlShapedRun miss = run_cache_draw(&fresh, text);

    ASSERT_EQ(run_cache_stat(&rig, FL_S("misses")), 1ULL);
    ASSERT_EQ(run_cache_stat(&rig, FL_S("hits")), 1ULL);
    ASSERT_TRUE(run_cache_same_glyphs(hit, miss));

    run_cache_rig_close(&rig);
    run_cache_rig_close(&fresh);
}

UTEST(shaped_run_cache, measure_then_draw_shapes_once) {
    RunCacheRig rig = run_cache_rig_open(KB(64));
    const FlString text = FL_S("Now Playing: Yesterday We Told You");
    FlShapedRun measured = { 0 };

    ASSERT_TRUE(shaped_run_cache_get(rig.cache, &rig.measure, text, &measured));
    const FlShapedRun drawn = run_cache_draw(&rig, text);

    ASSERT_TRUE(measured.glyphs == nullptr);
    ASSERT_EQ(measured.width, drawn.width);
    ASSERT_EQ(run_cache_stat(&rig, FL_S("misses")) + 10 * run_cache_stat(&rig, FL_S("hits")), 11ULL);

    run_cache_rig_close(&rig);
}

UTEST(shaped_run_cache, another_font_or_fallback_is_another_entry) {
    RunCacheRig rig = run_cache_rig_open(KB(64));
    const FlString text = FL_S("office fjord");
    run_cache_draw(&rig, text);

    rig.draw.fallback = (FontShaperFace) { .font = rig.draw.primary.font, .id = 7 };
    run_cache_draw(&rig, text);
    rig.draw.primary.id = 8;
    run_cache_draw(&rig, text);

    ASSERT_EQ(run_cache_stat(&rig, FL_S("misses")), 3ULL);
    ASSERT_EQ(run_cache_stat(&rig, FL_S("hits")), 0ULL);

    run_cache_rig_close(&rig);
}

UTEST(shaped_run_cache, text_over_the_length_limit_is_never_cached) {
    RunCacheRig rig = run_cache_rig_open(KB(64));
    char* bytes = arena_alloc_array(rig.arena, char, 1025);
    memset(bytes, 'a', 1025);
    const FlString longest = { .data = bytes, .length = 1024 };
    const FlString too_long = { .data = bytes, .length = 1025 };

    ASSERT_EQ((int)run_cache_draw(&rig, too_long).count, 1025);
    ASSERT_EQ((int)run_cache_draw(&rig, too_long).count, 1025);
    ASSERT_EQ(run_cache_stat(&rig, FL_S("misses")), 2ULL);
    ASSERT_EQ(run_cache_stat(&rig, FL_S("bytes_used")), 0ULL);

    run_cache_draw(&rig, longest);
    run_cache_draw(&rig, longest);
    ASSERT_EQ(run_cache_stat(&rig, FL_S("hits")), 1ULL);

    run_cache_rig_close(&rig);
}

UTEST(shaped_run_cache, empty_text_is_an_empty_run) {
    RunCacheRig rig = run_cache_rig_open(KB(64));
    FlShapedRun out = { .count = 9, .width = 9 };

    for_count(i, 2) {
        ASSERT_TRUE(shaped_run_cache_get(rig.cache, &rig.draw, FL_S(""), &out));
        ASSERT_EQ((int)out.count, 0);
        ASSERT_EQ(out.width, (i64)0);
    }

    run_cache_rig_close(&rig);
}
