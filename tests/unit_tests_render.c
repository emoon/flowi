// software/GPU render, image and font-cache tests

#include "unit_tests_common.h"

#include <core/jobsys.h>
#include <flowi/core/memory_tracker.h>
#include "app/host.h"
#include <render/software/sr_render_helpers.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// font_cache: quantize bucket boundaries.

UTEST(font_cache, quantize_buckets) {
    ASSERT_EQ_MSG((int)font_cache_quantize_size(1), 2, "quantize(1) -> 2px (2px bucket)");
    ASSERT_EQ_MSG((int)font_cache_quantize_size(29), 30, "quantize(29) -> 30px (2px bucket)");
    ASSERT_EQ_MSG((int)font_cache_quantize_size(30), 32, "quantize(30) -> 32px (4px bucket)");
    ASSERT_EQ_MSG((int)font_cache_quantize_size(59), 60, "quantize(59) -> 60px (4px bucket)");
    ASSERT_EQ_MSG((int)font_cache_quantize_size(60), 64, "quantize(60) -> 64px (8px bucket)");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// font_cache: quarter-pixel size keys for the fractional-size text path (round(size × 4)).

UTEST(font_cache, quantize_q4_quarter_steps) {
    ASSERT_EQ_MSG((int)font_cache_quantize_size_q4(7.5f), 30, "7.5px resolves exactly to key 30");
    ASSERT_EQ_MSG((int)font_cache_quantize_size_q4(9.5f), 38, "9.5px resolves exactly to key 38");
    ASSERT_EQ_MSG((int)font_cache_quantize_size_q4(10.5f), 42, "10.5px resolves exactly to key 42");
    ASSERT_EQ_MSG((int)font_cache_quantize_size_q4(16.0f), 64, "whole px maps to size × 4");
    ASSERT_EQ_MSG((int)font_cache_quantize_size_q4(16.1f), 64, "within a quarter step rounds to the same key");
    ASSERT_EQ_MSG((int)font_cache_quantize_size_q4(16.25f), 65, "next quarter step gets its own key");
    ASSERT_EQ_MSG((int)font_cache_quantize_size_q4(0.0f), 0, "zero stays zero");
    ASSERT_EQ_MSG((int)font_cache_quantize_size_q4(-4.0f), 0, "negative clamps to zero");
    ASSERT_EQ_MSG((int)font_cache_quantize_size_q4(1.0e9f), 65535, "huge sizes clamp to the u16 key range");
}

// quantize is not idempotent at the bucket band boundaries, so a key formed by font_cache_whole_px_to_q4
// must come back bucketed exactly once (29 -> 30, never 30 -> 32).
UTEST(font_cache, q4_to_whole_px_round_trip) {
    ASSERT_EQ_MSG((int)font_cache_q4_to_whole_px(30), 8, "a 7.5px key rasterizes at the nearest whole px (8)");
    ASSERT_EQ_MSG((int)font_cache_q4_to_whole_px(29), 7, "a 7.25px key rounds down to 7");
    ASSERT_EQ_MSG((int)font_cache_q4_to_whole_px(0), 1, "a degenerate zero key clamps to 1px");
    ASSERT_EQ_MSG((int)font_cache_whole_px_to_q4(29), 120, "29px buckets to 30px when the key is formed");
    ASSERT_EQ_MSG((int)font_cache_q4_to_whole_px(font_cache_whole_px_to_q4(29)), 30,
                  "the 29px round trip lands on 30 — bucketed once, not re-bucketed to 32");
    ASSERT_EQ_MSG((int)font_cache_q4_to_whole_px(font_cache_whole_px_to_q4(59)), 60,
                  "the 59px round trip lands on 60 — the 4px-band boundary is not re-bucketed to 64");
    ASSERT_EQ_MSG((int)font_cache_q4_to_whole_px(font_cache_whole_px_to_q4(16)), 16,
                  "an in-band whole px round trips unchanged");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// font_cache: the shared face-slot allocator every load path funnels through. The default font needs no
// font data, so it can fill the table on its own.

UTEST(font_cache, face_slots_are_unique_and_bounded) {
    FlFontCache* cache = font_cache_new(s_arena);

    FlFontId first = font_cache_load_default_font(cache, 1);
    FlFontId second = font_cache_load_default_font(cache, 2);
    ASSERT_TRUE_MSG(first != FONT_ID_INVALID && second != FONT_ID_INVALID, "the first loads must succeed");
    ASSERT_TRUE_MSG(first != second, "each claimed slot gets its own id");
    ASSERT_EQ_MSG((int)font_cache_get_font_status(cache, first), (int)FontStatus_Ready,
                  "the built-in font is ready on return");
    ASSERT_EQ_MSG(font_cache_get_size(cache, second), 16, "scale 2 loads at 8*scale px");

    // 128 slots total; two are spoken for. Fill the rest.
    for (int i = 2; i < 128; i++) {
        ASSERT_TRUE_MSG(font_cache_load_default_font(cache, 1) != FONT_ID_INVALID, "slots below the cap must load");
    }
    ASSERT_EQ_MSG((int)font_cache_load_default_font(cache, 1), (int)FONT_ID_INVALID, "the cap refuses a 129th face");

    ASSERT_EQ_MSG((int)font_cache_get_font_status(cache, first), (int)FontStatus_Ready,
                  "a refused load leaves earlier faces intact");

    font_cache_destroy(cache);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// font_cache: single-line fractional-size measurement (font_cache_measure_text / font_cache_get_font_metrics).
// The embedded glyph test font ('A' solid square, 'o' ring; 0.8 em advances) provides known glyphs.

UTEST(font_cache, missing_font_file_settles_to_failed) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();

    FlFontCache* cache = font_cache_new(s_arena);
    FlFontId font = font_cache_load_font(cache, mount.mount, FL_S("no_such_font.ttf"), 16);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "the async load starts even though the file is absent");

    vfs_wait_all();
    ASSERT_EQ_MSG((int)font_cache_get_font_status(cache, font), (int)FontStatus_Failed,
                  "a read that never delivers data must still settle the face, not leave it pending");

    font_cache_destroy(cache);
    fl_vfs_mount_close(mount.mount);
}

UTEST(font_cache, measure_text_freetype) {
    FlFontCache* cache = font_cache_new(s_arena);
    FlFontId font = font_cache_load_font_memory(cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "memory-loaded font must be ready synchronously");

    int adv = font_cache_get_glyph_advance(cache, font, 32, 'A');
    ASSERT_TRUE_MSG(adv > 0, "'A' advance must be non-zero");
    // 1600 = 25.0px in 26.6: the font's 0.8 em design advance (25.6px at 32px) after hinting.
    ASSERT_EQ_MSG(adv, 1600, "whole-px 'A' advance is pinned to its historical value");
    FlString aa = { .data = "AA", .length = 2, .is_static = 1, .is_ascii = 1 };
    FlTextMetrics m = font_cache_measure_text(cache, aa, font, 32.0f, 0.0f);
    ASSERT_NEAR_MSG(m.width, (float)(2 * adv) / 64.0f, 0.0001f, "width = summed advances of the layout path");
    ASSERT_TRUE_MSG(m.height > 0.0f && m.ascent > 0.0f, "line metrics are filled");
    ASSERT_NEAR_MSG(m.height, m.ascent + m.descent, 2.0f, "line height tracks ascent + descent");

    FlString aa_utf8 = { .data = "AA", .length = 2, .is_static = 1 };
    ASSERT_NEAR_MSG(font_cache_measure_text(cache, aa_utf8, font, 32.0f, 0.0f).width, m.width, 0.0001f,
                    "utf8 iteration agrees with the ascii fast path");

    FlTextMetrics spaced = font_cache_measure_text(cache, aa, font, 32.0f, 1.5f);
    ASSERT_NEAR_MSG(spaced.width, m.width + 1.5f, 0.0001f, "two glyphs get exactly one spacing gap");
    FlString a = { .data = "A", .length = 1, .is_static = 1, .is_ascii = 1 };
    ASSERT_NEAR_MSG(font_cache_measure_text(cache, a, font, 32.0f, 1.5f).width,
                    font_cache_measure_text(cache, a, font, 32.0f, 0.0f).width, 0.0001f,
                    "a single glyph gets no spacing gap");
    FlString aaa = { .data = "AAA", .length = 3, .is_static = 1, .is_ascii = 1 };
    ASSERT_NEAR_MSG(font_cache_measure_text(cache, aaa, font, 32.0f, 1.5f).width, 3.0f * ((float)adv / 64.0f) + 3.0f,
                    0.0001f, "three glyphs get exactly two spacing gaps (n−1, no trailing gap)");

    FlTextMetrics empty = font_cache_measure_text(cache, string_empty(), font, 32.0f, 4.0f);
    ASSERT_NEAR_MSG(empty.width, 0.0f, 0.0001f, "empty text has zero width");
    ASSERT_NEAR_MSG(empty.height, m.height, 0.0001f, "empty text keeps the line metrics");

    FlTextMetrics q_lo = font_cache_measure_text(cache, aa, font, 7.5f, 0.0f);
    FlTextMetrics q_hi = font_cache_measure_text(cache, aa, font, 7.6f, 0.0f);
    ASSERT_TRUE_MSG(q_lo.width > 0.0f && q_lo.width < m.width, "7.5px measures smaller than 32px");
    ASSERT_NEAR_MSG(q_lo.width, q_hi.width, 0.0001f, "7.5px and 7.6px share one quarter-px key");
    ASSERT_NEAR_MSG(font_cache_measure_text(cache, aa, font, 0.0f, 0.0f).width, m.width, 0.0001f,
                    "size 0 falls back to the load size");

    FlFontMetrics fm = font_cache_get_font_metrics(cache, font, 32.0f);
    ASSERT_NEAR_MSG(fm.ascent, m.ascent, 0.0001f, "font metrics ascent matches measurement");
    ASSERT_NEAR_MSG(fm.descent, m.descent, 0.0001f, "font metrics descent matches measurement");
    ASSERT_NEAR_MSG(fm.line_height, m.height, 0.0001f, "font metrics line height matches measurement");

    FlTextMetrics unknown = font_cache_measure_text(cache, aa, 9999, 32.0f, 0.0f);
    ASSERT_TRUE_MSG(unknown.width == 0.0f && unknown.height == 0.0f && unknown.ascent == 0.0f
                        && unknown.descent == 0.0f,
                    "unknown font measures as zeroed metrics");
    FlFontMetrics unknown_fm = font_cache_get_font_metrics(cache, 9999, 32.0f);
    ASSERT_TRUE_MSG(unknown_fm.ascent == 0.0f && unknown_fm.descent == 0.0f && unknown_fm.line_height == 0.0f,
                    "unknown font reports zeroed font metrics");

    font_cache_destroy(cache);
}

UTEST(font_cache, measure_text_default_font) {
    FlFontCache* cache = font_cache_new(s_arena);
    FlFontId font = font_cache_load_default_font(cache, 2); // built-in 8x8 font at scale 2

    FlString ab = { .data = "AB", .length = 2, .is_static = 1, .is_ascii = 1 };
    FlTextMetrics m = font_cache_measure_text(cache, ab, font, 0.0f, 0.0f);
    ASSERT_NEAR_MSG(m.width, 32.0f, 0.0001f, "two cells at 8px × scale 2");
    ASSERT_NEAR_MSG(m.height, 16.0f, 0.0001f, "cell height at scale 2");
    ASSERT_TRUE_MSG(m.ascent == 0.0f && m.descent == 0.0f,
                    "the built-in font's baseline sits at the cell top (zero ascent/descent)");
    ASSERT_NEAR_MSG(font_cache_measure_text(cache, ab, font, 0.0f, 2.0f).width, 34.0f, 0.0001f,
                    "letter spacing applies to the built-in font too");
    ASSERT_NEAR_MSG(font_cache_measure_text(cache, ab, font, 12.34f, 0.0f).width, m.width, 0.0001f,
                    "the built-in font ignores the requested size");
    FlString abc = { .data = "ABC", .length = 3, .is_static = 1, .is_ascii = 1 };
    ASSERT_NEAR_MSG(font_cache_measure_text(cache, abc, font, 0.0f, 2.0f).width, 52.0f, 0.0001f,
                    "three cells plus exactly two spacing gaps (n−1)");
    ASSERT_NEAR_MSG(font_cache_get_font_metrics(cache, font, 0.0f).line_height, 16.0f, 0.0001f,
                    "font metrics report the cell height");

    font_cache_destroy(cache);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// font_cache: the glyph-outline service over the embedded test fonts (tests/fixtures/fonts/glyph_test_fonts.h): 'A' is a
// solid square, 'o' a ring with an opposite-wound counter; one font per outline flavor (TrueType quadratics,
// CFF cubics). Light-hinted loading may nudge coordinates, so geometry assertions are ranges, not exact.

// forbidden is the segment kind the flavor must never produce, expected the curve kind it must.
// (utest's ASSERT macros write through utest_result, threaded in from the calling UTEST case.)
static void check_outline_font(int* utest_result, const unsigned char* data, long data_size,
                               FlGlyphSegmentKind expected, FlGlyphSegmentKind forbidden) {
    FlFontCache* cache = font_cache_new(s_arena);
    const u16 size = 32;
    const u16 size_q4 = font_cache_whole_px_to_q4(size); // the outline service is keyed in quarter-px

    FlFontId font = font_cache_load_font_memory(cache, data, data_size, size);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "memory-loaded font must be ready synchronously");
    ASSERT_EQ_MSG((int)font_cache_get_font_status(cache, font), (int)FontStatus_Ready, "status must be Ready");

    u32 glyph_a = font_cache_get_glyph_index(cache, font, 'A');
    u32 glyph_o = font_cache_get_glyph_index(cache, font, 'o');
    ASSERT_TRUE_MSG(glyph_a != 0 && glyph_o != 0, "'A' and 'o' must map to glyphs");
    ASSERT_EQ_MSG((int)font_cache_get_glyph_index(cache, font, 'Q'), 0, "unmapped codepoints return index 0");

    FlGlyphOutline outline;
    ASSERT_TRUE_MSG(font_cache_get_glyph_outline(cache, font, size_q4, glyph_a, s_arena, &outline),
                    "'A' outline must load");
    ASSERT_TRUE_MSG(outline.segment_count >= 4, "the square has at least its 4 edges");
    for (u32 i = 0; i < outline.segment_count; i++) {
        ASSERT_EQ_MSG((int)outline.segments[i].kind, (int)FlGlyphSegment_Line, "the square is lines only");
    }

    ASSERT_TRUE_MSG(font_cache_get_glyph_outline(cache, font, size_q4, glyph_o, s_arena, &outline),
                    "'o' outline must load");
    bool saw_expected = false;
    for (u32 i = 0; i < outline.segment_count; i++) {
        ASSERT_TRUE_MSG((int)outline.segments[i].kind != (int)forbidden,
                        "outline carries the wrong curve kind for its font flavor");
        saw_expected |= outline.segments[i].kind == expected;
    }
    ASSERT_TRUE_MSG(saw_expected, "the ring must decompose to curve segments");

    // Per contour the edge vectors telescope to zero, so the whole soup must too.
    f32 sum_x = 0.0f;
    f32 sum_y = 0.0f;
    for (u32 i = 0; i < outline.segment_count; i++) {
        const FlGlyphSegment* seg = &outline.segments[i];
        int last = seg->kind == FlGlyphSegment_Line ? 1 : (seg->kind == FlGlyphSegment_Quadratic ? 2 : 3);
        sum_x += seg->pts[last].x - seg->pts[0].x;
        sum_y += seg->pts[last].y - seg->pts[0].y;
    }
    ASSERT_TRUE_MSG(sum_x > -0.001f && sum_x < 0.001f && sum_y > -0.001f && sum_y < 0.001f,
                    "every contour must be closed (edge vectors telescope to zero)");

    // Design advance is 0.8 em = 25.6 px at 32 px; the box spans 0.6 x 0.7 em.
    ASSERT_TRUE_MSG(outline.advance > 20.0f && outline.advance < 30.0f, "'o' advance lands near 0.8 em");
    ASSERT_TRUE_MSG(outline.max_x > outline.min_x && outline.max_y > outline.min_y, "'o' has a real bbox");
    for (u32 i = 0; i < outline.segment_count; i++) {
        for (int p = 0; p < 4; p++) {
            FlVec2 pt = outline.segments[i].pts[p];
            if (p > 0 && pt.x == 0.0f && pt.y == 0.0f) {
                continue; // unused trailing points are zero
            }
            ASSERT_TRUE_MSG(pt.x >= outline.min_x - 0.01f && pt.x <= outline.max_x + 0.01f
                                && pt.y >= outline.min_y - 0.01f && pt.y <= outline.max_y + 0.01f,
                            "every control point sits inside the control box");
        }
    }

    font_cache_destroy(cache);
}

UTEST(font_cache, glyph_outline_truetype_quadratic) {
    check_outline_font(utest_result, k_glyph_test_ttf, k_glyph_test_ttf_size, FlGlyphSegment_Quadratic,
                       FlGlyphSegment_Cubic);
}

UTEST(font_cache, glyph_outline_cff_cubic) {
    check_outline_font(utest_result, k_glyph_test_cff, k_glyph_test_cff_size, FlGlyphSegment_Cubic,
                       FlGlyphSegment_Quadratic);
}

UTEST(font_cache, glyph_outline_fractional_size) {
    FlFontCache* cache = font_cache_new(s_arena);
    FlFontId font = font_cache_load_font_memory(cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "memory-loaded font must be ready synchronously");
    u32 glyph_a = font_cache_get_glyph_index(cache, font, 'A');
    ASSERT_TRUE_MSG(glyph_a != 0, "'A' must map to a glyph");

    FlGlyphOutline big;
    FlGlyphOutline small;
    ASSERT_TRUE_MSG(font_cache_get_glyph_outline(cache, font, font_cache_whole_px_to_q4(32), glyph_a, s_arena, &big),
                    "the 32px outline loads");
    ASSERT_TRUE_MSG(
        font_cache_get_glyph_outline(cache, font, font_cache_quantize_size_q4(7.5f), glyph_a, s_arena, &small),
        "the 7.5px outline loads");
    ASSERT_TRUE_MSG(small.advance > 0.0f && small.advance < big.advance,
                    "the 7.5px advance is smaller than the 32px one");
    ASSERT_TRUE_MSG(small.max_x - small.min_x < big.max_x - big.min_x, "the 7.5px geometry is smaller");
    ASSERT_FALSE_MSG(font_cache_get_glyph_outline(cache, font, 0, glyph_a, s_arena, &big),
                     "a zero size key refuses to load");

    font_cache_destroy(cache);
}

UTEST(font_cache, glyph_outline_rejects_non_freetype) {
    FlFontCache* cache = font_cache_new(s_arena);

    FlGlyphOutline outline;
    ASSERT_FALSE_MSG(font_cache_get_glyph_outline(cache, 99, 32, 1, s_arena, &outline),
                     "unknown font ids have no outlines");
    ASSERT_EQ_MSG((int)font_cache_get_glyph_index(cache, 99, 'A'), 0, "unknown font ids have no glyph indices");

    FlFontId def = font_cache_load_default_font(cache, 2);
    ASSERT_TRUE_MSG(def != FONT_ID_INVALID, "default font loads");
    ASSERT_FALSE_MSG(font_cache_get_glyph_outline(cache, def, 32, 1, s_arena, &outline),
                     "bitmap-style faces (default font) have no outlines");

    font_cache_destroy(cache);
}

// sw_glyph_cache: the built-in 8x8 default font is generated at an integer scale. bearing_y is in pixels, like
// width/height, and positions the baseline as glyph_y = baseline_y - bearing_y; for the full-cell 8x8 font the
// whole glyph sits above the baseline, so its ascent equals the cell height (8 * scale).
UTEST(sw_glyph_cache, default_font_bearing_y_scales) {
    FlFontCache* shared = font_cache_new(s_arena);
    SwGlyphCache* sw = sw_glyph_cache_new(s_arena, shared, FontRenderMode_Grayscale);

    for (u16 scale = 1; scale <= 4; scale++) {
        FlFontId font = font_cache_load_default_font(shared, scale);
        ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "default font loads at every scale");

        // Default font is a single fixed-size atlas; the size argument is ignored for it.
        const Glyph* glyph = sw_glyph_cache_get(sw, font, 8 * scale, 'A');
        ASSERT_TRUE_MSG(glyph != nullptr, "default-font ASCII glyph must realize");

        const int expected = 8 * (int)scale;
        ASSERT_EQ_MSG(glyph->bearing_y, expected, "bearing_y (ascent) scales as 8 * scale");
        ASSERT_EQ_MSG((int)glyph->height, expected, "glyph height scales as 8 * scale");
        ASSERT_EQ_MSG(glyph->bearing_y, (int)glyph->height, "bearing_y stays equal to the scaled cell height");
    }

    FlFontId font1 = font_cache_load_default_font(shared, 1);
    const Glyph* g1 = sw_glyph_cache_get(sw, font1, 8, 'A');
    ASSERT_TRUE_MSG(g1 != nullptr, "scale-1 default glyph realizes");
    ASSERT_EQ_MSG(g1->bearing_y, 8, "scale-1 bearing_y is unchanged at 8");

    sw_glyph_cache_destroy(sw);
    font_cache_destroy(shared);
}

// sw_glyph_cache: atlas jobs each own an ephemeral FT_Library, leaving the cache's shared library
// main-thread-only. Bursting concurrent atlas jobs against the main-thread on-demand path is deterministic
// today; it only becomes a real data-race probe under the manual TSAN build, not on CI.
UTEST(sw_glyph_cache, concurrent_atlas_jobs_and_on_demand) {
    FlFontCache* shared = font_cache_new(s_arena);
    SwGlyphCache* sw = sw_glyph_cache_new(s_arena, shared, FontRenderMode_Grayscale);

    const u16 sizes[] = { 12, 16, 24, 32 };
    const int size_count = (int)sizeof_array(sizes);

    FlFontId font = font_cache_load_font_memory(shared, k_glyph_test_ttf, k_glyph_test_ttf_size, sizes[0]);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "memory-loaded FreeType font must be ready synchronously");

    // The first get per size enqueues that size's atlas rasterization job.
    for (int i = 0; i < size_count; i++) {
        sw_glyph_cache_get(sw, font, sizes[i], 'A');
    }

    // Poll until every size's 'A' is realized, firing the main-thread on-demand path (a non-ASCII codepoint) as
    // each atlas turns Ready so it overlaps the jobs still running. The on-demand result is intentionally
    // unchecked: the embedded test font maps no non-ASCII codepoint.
    const Glyph* glyphs[4] = { 0 };
    u64 deadline_ns = get_monotonic_time_ns_os() + 5000000000ull;
    bool all_ready = false;
    while (!all_ready) {
        all_ready = true;
        for (int i = 0; i < size_count; i++) {
            if (glyphs[i] != nullptr) {
                continue;
            }
            glyphs[i] = sw_glyph_cache_get(sw, font, sizes[i], 'A');
            if (glyphs[i] == nullptr) {
                all_ready = false;
            } else {
                (void)sw_glyph_cache_get(sw, font, sizes[i], 0x00E9);
            }
        }
        ASSERT_TRUE_MSG(get_monotonic_time_ns_os() < deadline_ns,
                        "atlas rasterization jobs did not all complete within the deadline");
        if (!all_ready) {
            sleep_us(200);
        }
    }

    for (int i = 0; i < size_count; i++) {
        ASSERT_TRUE_MSG(glyphs[i] != nullptr, "'A' must realize at every requested size");
        ASSERT_TRUE_MSG(glyphs[i]->advance_x != 0, "realized 'A' carries a nonzero advance at every size");
    }

    sw_glyph_cache_destroy(sw);
    font_cache_destroy(shared);
}

// The primary font (glyph_test_fonts.h) maps only 'A'/'o'; the fallback font (fallback_test_font.h) maps 'B'.
UTEST(font_cache, text_size_includes_fallback_advance) {
    FlFontCache* cache = font_cache_new(s_arena);
    const u16 size = 32;

    FlFontId primary = font_cache_load_font_memory(cache, k_glyph_test_ttf, k_glyph_test_ttf_size, size);
    ASSERT_TRUE_MSG(primary != FONT_ID_INVALID, "primary font must load");
    ASSERT_EQ_MSG((int)font_cache_get_glyph_index(cache, primary, 'B'), 0, "'B' is absent from the primary face");

    const FlString b = FL_S("B");

    ASSERT_EQ_MSG(font_cache_get_text_size(cache, b, primary, size).x, 0.0f,
                  "no fallback -> missing codepoint measures zero width");
    ASSERT_EQ_MSG(font_cache_measure_text(cache, b, primary, 32.0f, 0.0f).width, 0.0f,
                  "no fallback -> the fractional path measures zero width too");

    FlFontId fallback = font_cache_load_font_memory(cache, k_fallback_test_ttf, k_fallback_test_ttf_size, size);
    ASSERT_TRUE_MSG(fallback != FONT_ID_INVALID, "fallback font must load");
    font_cache_set_fallback_font(cache, fallback);

    int advance = font_cache_get_glyph_advance(cache, primary, size, 'B');
    ASSERT_TRUE_MSG(advance > 0, "fallback supplies a nonzero advance for 'B'");
    ASSERT_EQ_MSG(font_cache_get_text_size(cache, b, primary, size).x, (float)advance / 64.0f,
                  "measured width equals the per-codepoint laid-out advance (fallback included)");
    ASSERT_NEAR_MSG(font_cache_measure_text(cache, b, primary, 32.0f, 0.0f).width, (float)advance / 64.0f, 0.0001f,
                    "the fractional path picks up the fallback advance too");

    font_cache_destroy(cache);
}

// font_cache: the glyph-resolution seam every backend prepares outlines through. A resolved glyph index is
// meaningful only against the face it resolved to.
UTEST(font_cache, resolve_glyph_uses_the_fallback_face) {
    FlFontCache* cache = font_cache_new(s_arena);
    const u16 size = 32;

    FlFontId primary = font_cache_load_font_memory(cache, k_glyph_test_ttf, k_glyph_test_ttf_size, size);
    FlFontId fallback = font_cache_load_font_memory(cache, k_fallback_test_ttf, k_fallback_test_ttf_size, size);
    ASSERT_TRUE_MSG(primary != FONT_ID_INVALID && fallback != FONT_ID_INVALID, "both test fonts must load");

    ASSERT_EQ_MSG((int)font_cache_resolve_glyph(cache, primary, 'B').glyph_index, 0,
                  "no fallback -> a primary-missing codepoint resolves to .notdef");

    font_cache_set_fallback_font(cache, fallback);

    FontGlyphRef mapped = font_cache_resolve_glyph(cache, primary, 'A');
    ASSERT_EQ_MSG((int)mapped.font, (int)primary, "a primary-mapped codepoint stays on the primary face");
    ASSERT_EQ_MSG((int)mapped.glyph_index, (int)font_cache_get_glyph_index(cache, primary, 'A'),
                  "a primary-mapped codepoint keeps the primary face's index");

    FontGlyphRef missing = font_cache_resolve_glyph(cache, primary, 'B');
    ASSERT_EQ_MSG((int)missing.font, (int)fallback, "a primary-missing codepoint resolves onto the fallback face");
    ASSERT_EQ_MSG((int)missing.glyph_index, (int)font_cache_get_glyph_index(cache, fallback, 'B'),
                  "the index is the one from the face it resolved to");
    ASSERT_TRUE_MSG(missing.glyph_index != 0, "the fallback face maps 'B'");

    FlGlyphOutline outline;
    ASSERT_TRUE_MSG(font_cache_get_glyph_outline(cache, missing.font, font_cache_whole_px_to_q4(size),
                                                 missing.glyph_index, s_arena, &outline),
                    "the resolved face/index pair loads a real outline");
    ASSERT_TRUE_MSG(outline.segment_count > 0, "the fallback glyph is a real shape, not an empty .notdef");

    FontGlyphRef nowhere = font_cache_resolve_glyph(cache, primary, 0x2735); // in neither face
    ASSERT_EQ_MSG((int)nowhere.font, (int)primary, "a codepoint neither face maps stays on the primary");
    ASSERT_EQ_MSG((int)nowhere.glyph_index, 0, "a codepoint neither face maps resolves to .notdef");

    font_cache_destroy(cache);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// reference_fit: the opt-in 1080p scale-to-fit policy (ui/reference_fit.h). ui_reference_fit(height) must reproduce
// the numbers the core computes via ui_apply_scale_preset: identity at the 1080p reference, and a PAL-boosted
// half-scale at 540p.

UTEST(reference_fit, reproduces_reference_and_pal_numbers) {
    UiScalePair ref = ui_reference_fit(1080);
    EXPECT_EQ(ref.ui_scale, 1.0f);
    EXPECT_EQ(ref.text_scale, 1.0f);

    // At 540p: half UI scale, with the SD/PAL text boost (1.10) folded in: 0.5 * 1.10 = 0.55.
    UiScalePair pal = ui_reference_fit(540);
    EXPECT_EQ(pal.ui_scale, 0.5f);
    EXPECT_EQ(pal.text_scale, 0.55f);

    // The helper is the core's preset computation, repackaged into the pair.
    float ui_scale, text_factor, text_scale;
    ui_apply_scale_preset(&ui_scale, &text_factor, &text_scale, 540);
    EXPECT_EQ(pal.ui_scale, ui_scale);
    EXPECT_EQ(pal.text_scale, text_scale);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// color_oklch: the perceptual color module (ui/color_oklch.c). Pure value math, no renderer: sRGB<->OKLab
// <->OKLCH conversions, out-of-gamut clamping, perceptual lighten/darken/desaturate, and the luminance-based
// readable-foreground pick.

// Reference OKLab/OKLCH values for the sRGB primaries, from the CSS Color 4 / Ottosson reference.
UTEST(color_oklch, known_values_primaries) {
    FlColorOKLab white = color_rgb_to_oklab((FlColor) { 255, 255, 255, 255 });
    EXPECT_NEAR(white.L, 1.0f, 0.005f);
    EXPECT_NEAR(white.a, 0.0f, 0.005f);
    EXPECT_NEAR(white.b, 0.0f, 0.005f);

    EXPECT_NEAR(color_rgb_to_oklab((FlColor) { 0, 0, 0, 255 }).L, 0.0f, 0.005f);

    FlColorOKLab red = color_rgb_to_oklab((FlColor) { 255, 0, 0, 255 });
    EXPECT_NEAR(red.L, 0.6279554f, 0.005f);
    EXPECT_NEAR(red.a, 0.2248631f, 0.005f);
    EXPECT_NEAR(red.b, 0.1258463f, 0.005f);

    FlColorOKLCH red_lch = color_rgb_to_oklch((FlColor) { 255, 0, 0, 255 });
    EXPECT_NEAR(red_lch.l, 0.6279554f, 0.005f);
    EXPECT_NEAR(red_lch.c, 0.2576833f, 0.005f);
    EXPECT_NEAR(red_lch.h, 29.234f, 0.5f);

    FlColorOKLCH green_lch = color_rgb_to_oklch((FlColor) { 0, 255, 0, 255 });
    EXPECT_NEAR(green_lch.h, 142.495f, 0.5f);
    FlColorOKLCH blue_lch = color_rgb_to_oklch((FlColor) { 0, 0, 255, 255 });
    EXPECT_NEAR(blue_lch.h, 264.052f, 0.5f);
}

UTEST(color_oklch, roundtrip_srgb) {
    const FlColor samples[] = {
        { 18, 52, 86, 255 }, { 200, 120, 40, 255 },  { 12, 200, 175, 255 }, { 255, 0, 0, 255 },
        { 0, 128, 64, 255 }, { 130, 130, 130, 255 }, { 0, 0, 0, 255 },      { 255, 255, 255, 255 },
    };
    for (int i = 0; i < (int)(sizeof(samples) / sizeof(samples[0])); i++) {
        FlColor in = samples[i];
        FlColor out = color_oklch_to_rgb(color_rgb_to_oklch(in));
        EXPECT_NEAR((f32)out.r, (f32)in.r, 2.0f);
        EXPECT_NEAR((f32)out.g, (f32)in.g, 2.0f);
        EXPECT_NEAR((f32)out.b, (f32)in.b, 2.0f);

        FlColor out_lab = color_oklab_to_rgb(color_rgb_to_oklab(in));
        EXPECT_NEAR((f32)out_lab.r, (f32)in.r, 2.0f);
        EXPECT_NEAR((f32)out_lab.g, (f32)in.g, 2.0f);
        EXPECT_NEAR((f32)out_lab.b, (f32)in.b, 2.0f);
    }
}

// Chroma far past the sRGB gamut must come back clamped, not wrapped.
UTEST(color_oklch, out_of_gamut_clamped) {
    FlColorOKLCH wild = { .l = 0.7f, .c = 0.5f, .h = 29.0f }; // c=0.5 is well outside sRGB for this L/h
    FlColor rgb = color_oklch_to_rgb(wild);
    EXPECT_LE((int)rgb.r, 255);
    EXPECT_GE((int)rgb.r, 0);

    FlColorOKLCH back = color_rgb_to_oklch(rgb);
    EXPECT_LE(back.c, wild.c + 0.001f);
    EXPECT_GT(back.c, 0.0f);
}

UTEST(color_oklch, perceptual_lighten_darken_monotonic) {
    FlColor base = { 90, 110, 160, 255 };
    f32 l0 = color_perceptual_lightness(base);
    f32 l_up = color_perceptual_lightness(color_lighten_perceptual(base, 0.15f));
    f32 l_dn = color_perceptual_lightness(color_darken_perceptual(base, 0.15f));
    EXPECT_GT(l_up, l0);
    EXPECT_LT(l_dn, l0);

    // Alpha is preserved through derivation.
    FlColor with_alpha = { 90, 110, 160, 128 };
    EXPECT_EQ((int)color_lighten_perceptual(with_alpha, 0.1f).a, 128);
    EXPECT_EQ((int)color_darken_perceptual(with_alpha, 0.1f).a, 128);
}

UTEST(color_oklch, lerp_oklab_endpoints_and_alpha) {
    FlColor a = { 200, 30, 40, 255 };
    FlColor b = { 255, 255, 255, 255 };

    EXPECT_NEAR(color_perceptual_lightness(color_lerp_oklab(a, b, 0.0f)), color_perceptual_lightness(a), 0.01f);
    EXPECT_NEAR(color_perceptual_lightness(color_lerp_oklab(a, b, 1.0f)), color_perceptual_lightness(b), 0.01f);

    // Midpoint lightness lies strictly between the endpoints (monotone in OKLab L).
    f32 lm = color_perceptual_lightness(color_lerp_oklab(a, b, 0.5f));
    EXPECT_GT(lm, color_perceptual_lightness(a));
    EXPECT_LT(lm, color_perceptual_lightness(b));

    // Alpha is interpolated linearly in sRGB, independent of the OKLab path.
    FlColor opaque = { 0, 0, 0, 0 };
    FlColor solid = { 0, 0, 0, 255 };
    EXPECT_EQ((int)color_lerp_oklab(opaque, solid, 0.0f).a, 0);
    EXPECT_EQ((int)color_lerp_oklab(opaque, solid, 1.0f).a, 255);
    EXPECT_EQ((int)color_lerp_oklab(opaque, solid, 0.5f).a, 127);
}

UTEST(color_oklch, desaturate_reduces_chroma) {
    FlColor base = { 200, 40, 40, 255 };
    FlColorOKLCH c0 = color_rgb_to_oklch(base);
    FlColorOKLCH c_half = color_oklch_desaturate(c0, 0.5f);
    EXPECT_NEAR(c_half.c, c0.c * 0.5f, 0.0001f);
    EXPECT_LT(c_half.c, c0.c);

    FlColor desat = color_desaturate_perceptual((FlColor) { 200, 40, 40, 77 }, 0.6f);
    EXPECT_LT(color_rgb_to_oklch(desat).c, c0.c);
    EXPECT_EQ((int)desat.a, 77);
}

UTEST(color_oklch, readable_foreground_picks_by_lightness) {
    FlColor light = { 245, 245, 245, 255 };
    FlColor dark = { 20, 20, 20, 255 };

    EXPECT_EQ((int)color_readable_foreground((FlColor) { 255, 255, 255, 255 }, light, dark).r, (int)dark.r);
    EXPECT_EQ((int)color_readable_foreground((FlColor) { 0, 0, 0, 255 }, light, dark).r, (int)light.r);

    // Saturated cases: bright yellow reads dark; deep blue reads light.
    EXPECT_EQ((int)color_readable_foreground((FlColor) { 255, 220, 0, 255 }, light, dark).r, (int)dark.r);
    EXPECT_EQ((int)color_readable_foreground((FlColor) { 20, 20, 140, 255 }, light, dark).r, (int)light.r);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// vector_image: the GPU SVG vector core, all headless / no GL.

static const char k_svg_mono[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"80\">"
                                 "<rect x=\"10\" y=\"10\" width=\"30\" height=\"30\" fill=\"#ff8000\"/>"
                                 "<rect x=\"50\" y=\"40\" width=\"20\" height=\"20\" fill=\"#ff8000\"/>"
                                 "</svg>";

static const char k_svg_multi[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
                                  "<rect x=\"10\" y=\"10\" width=\"30\" height=\"30\" fill=\"#ff0000\"/>"
                                  "<rect x=\"50\" y=\"50\" width=\"30\" height=\"30\" fill=\"#0000ff\"/>"
                                  "</svg>";

static const char k_svg_gradient[]
    = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
      "<defs><linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"100\" y2=\"0\" gradientUnits=\"userSpaceOnUse\">"
      "<stop offset=\"0\" stop-color=\"#ff0000\"/><stop offset=\"1\" stop-color=\"#0000ff\"/>"
      "</linearGradient></defs>"
      "<rect x=\"0\" y=\"0\" width=\"100\" height=\"100\" fill=\"url(#g)\"/></svg>";

static const char k_svg_stroke[]
    = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
      "<rect x=\"10\" y=\"10\" width=\"30\" height=\"30\" fill=\"#00ff00\" stroke=\"#000000\" stroke-width=\"3\"/>"
      "</svg>";

// One valid solid-fill shape followed by one gradient shape.
static const char k_svg_mixed[]
    = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
      "<defs><linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"100\" y2=\"0\" gradientUnits=\"userSpaceOnUse\">"
      "<stop offset=\"0\" stop-color=\"#ff0000\"/><stop offset=\"1\" stop-color=\"#0000ff\"/></linearGradient></defs>"
      "<rect x=\"0\" y=\"0\" width=\"50\" height=\"50\" fill=\"#ff0000\"/>"
      "<rect x=\"50\" y=\"0\" width=\"50\" height=\"50\" fill=\"url(#g)\"/></svg>";

static const char k_svg_evenodd[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
                                    "<rect x=\"10\" y=\"10\" width=\"30\" height=\"30\" fill=\"#112233\" "
                                    "fill-rule=\"evenodd\"/></svg>";

// Edge cases that must reject cleanly (no crash) on the GPU vector path.
static const char k_svg_nofill[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
                                   "<rect x=\"10\" y=\"10\" width=\"30\" height=\"30\" fill=\"none\"/></svg>";

static const char k_svg_whitespace[] = "   \n\t  ";

static const char k_svg_garbage[] = "not an svg <<< &&& >>>";

// Multi-color with a per-shape opacity on the second shape: red at full opacity, then blue at 0.5.
static const char k_svg_color_opacity[]
    = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
      "<rect x=\"10\" y=\"10\" width=\"30\" height=\"30\" fill=\"#ff0000\"/>"
      "<rect x=\"50\" y=\"50\" width=\"30\" height=\"30\" fill=\"#0000ff\" opacity=\"0.5\"/></svg>";

UTEST(vector_image, parse_solid_fill_monochrome) {
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_mono, sizeof(k_svg_mono) - 1, s_arena, &img),
                    "a solid-fill SVG parses");
    ASSERT_EQ_MSG((int)img.shape_count, 2, "both rects become shapes");
    ASSERT_EQ_MSG((int)img.classification, (int)FlVectorClass_Monochrome, "one resolved color -> monochrome");
    ASSERT_EQ_MSG((int)img.mono_color.r, 255, "mono color R (#ff8000)");
    ASSERT_EQ_MSG((int)img.mono_color.g, 128, "mono color G (#ff8000)");
    ASSERT_EQ_MSG((int)img.mono_color.b, 0, "mono color B (#ff8000)");
    ASSERT_TRUE_MSG(img.width > 99.0f && img.width < 101.0f, "document width preserved");
    ASSERT_TRUE_MSG(img.height > 79.0f && img.height < 81.0f, "document height preserved");
    for (u32 i = 0; i < img.shape_count; i++) {
        ASSERT_TRUE_MSG(img.shapes[i].segment_count > 0, "each shape has contour segments (never rasterized)");
        ASSERT_EQ_MSG((int)img.shapes[i].fill_rule, 0, "default fill rule is nonzero");
        ASSERT_TRUE_MSG(img.shapes[i].opacity > 0.99f, "default shape opacity is 1");
    }
}

UTEST(vector_image, classify_multicolor) {
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_multi, sizeof(k_svg_multi) - 1, s_arena, &img),
                    "a multi-color solid-fill SVG parses");
    ASSERT_EQ_MSG((int)img.classification, (int)FlVectorClass_MultiColor, ">=2 distinct colors -> multi-color");
}

UTEST(vector_image, reject_gradient) {
    FlVectorImage img = { 0 };
    ASSERT_FALSE_MSG(vector_image_parse((const u8*)k_svg_gradient, sizeof(k_svg_gradient) - 1, s_arena, &img),
                     "a gradient fill is unsupported -> rejected for the GPU path");
}

UTEST(vector_image, reject_stroke) {
    FlVectorImage img = { 0 };
    ASSERT_FALSE_MSG(vector_image_parse((const u8*)k_svg_stroke, sizeof(k_svg_stroke) - 1, s_arena, &img),
                     "a stroke is unsupported -> rejected for the GPU path");
}

UTEST(vector_image, reject_mixed_per_image_granularity) {
    FlVectorImage img = { 0 };
    ASSERT_FALSE_MSG(vector_image_parse((const u8*)k_svg_mixed, sizeof(k_svg_mixed) - 1, s_arena, &img),
                     "one unsupported shape in a multi-shape SVG rejects the whole image");
}

UTEST(vector_image, reject_zero_solid_shape) {
    // fill="none" with no stroke contributes nothing: no solid fill, so the image is rejected.
    FlVectorImage img = { 0 };
    ASSERT_FALSE_MSG(vector_image_parse((const u8*)k_svg_nofill, sizeof(k_svg_nofill) - 1, s_arena, &img),
                     "an SVG with no visible solid fill is rejected");
}

UTEST(vector_image, reject_empty) {
    FlVectorImage img = { 0 };
    ASSERT_FALSE_MSG(vector_image_parse((const u8*)"", 0, s_arena, &img), "empty input rejects cleanly");
}

UTEST(vector_image, reject_whitespace) {
    FlVectorImage img = { 0 };
    ASSERT_FALSE_MSG(vector_image_parse((const u8*)k_svg_whitespace, sizeof(k_svg_whitespace) - 1, s_arena, &img),
                     "whitespace-only input rejects cleanly");
}

UTEST(vector_image, reject_garbage) {
    FlVectorImage img = { 0 };
    ASSERT_FALSE_MSG(vector_image_parse((const u8*)k_svg_garbage, sizeof(k_svg_garbage) - 1, s_arena, &img),
                     "garbage / non-SVG input rejects cleanly");
}

UTEST(vector_image, sw_path_decodes_what_gpu_rejects) {
    // A gradient SVG the GPU vector path rejects still decodes to RGBA via the shared CPU path (decode_svg).
    ImageDecodeOptions opts = { 0 };
    FlImageDecodeResult* result = image_decode(s_arena, k_svg_gradient, sizeof(k_svg_gradient) - 1, &opts);
    ASSERT_TRUE_MSG(result != NULL, "SW decode_svg still rasterizes a gradient SVG");
    ASSERT_EQ_MSG((int)result->channels, 4, "SW path produces RGBA");
    ASSERT_TRUE_MSG(result->width > 0 && result->height > 0, "SW path produces non-empty pixels");
}

UTEST(vector_image, parse_evenodd_fill_rule) {
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_evenodd, sizeof(k_svg_evenodd) - 1, s_arena, &img),
                    "an evenodd solid-fill SVG parses");
    ASSERT_EQ_MSG((int)img.shape_count, 1, "one shape");
    ASSERT_EQ_MSG((int)img.shapes[0].fill_rule, 1, "fill-rule=evenodd maps to 1");
}

UTEST(vector_image, lower_segments_known_geometry) {
    // The vector path reuses the shared glyph lowering. A line lowers to one quadratic whose control is the
    // segment midpoint, in padded-cell-local y-down coordinates: cell = (p.x - origin_x, origin_y - p.y).
    FlGlyphSegment line = { .kind = FlGlyphSegment_Line, .pts = { { 2.0f, 3.0f }, { 5.0f, 9.0f } } };
    f32 out[GPU_GLYPH_LOWER_MAX_QUADS_PER_SEGMENT * 6];
    u32 quads = gpu_glyph_lower_segments(&line, 1, 1.0f, 10.0f, out);
    ASSERT_EQ_MSG((int)quads, 1, "a line yields one quadratic");
    ASSERT_TRUE_MSG(out[0] == 1.0f && out[1] == 7.0f, "p0 = (2-1, 10-3)");
    ASSERT_TRUE_MSG(out[2] == 2.5f && out[3] == 4.0f, "line control is the midpoint");
    ASSERT_TRUE_MSG(out[4] == 4.0f && out[5] == 1.0f, "p1 = (5-1, 10-9)");
}

UTEST(vector_image, lower_cubic_known_geometry) {
    // A cubic with collinear, evenly-spaced control points is a degree-elevated straight line on y=0. The
    // shared lowering halves it into four quadratics; with all input y at 0 every emitted point stays on
    // y=0 (after the cell y-flip origin_y - p.y = 0), and the chain spans x = 0..3 end to end.
    FlGlyphSegment cubic = {
        .kind = FlGlyphSegment_Cubic,
        .pts = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 2.0f, 0.0f }, { 3.0f, 0.0f } },
    };
    f32 out[GPU_GLYPH_LOWER_MAX_QUADS_PER_SEGMENT * 6];
    u32 quads = gpu_glyph_lower_segments(&cubic, 1, 0.0f, 0.0f, out);
    ASSERT_EQ_MSG((int)quads, 4, "a cubic yields four quadratics");
    ASSERT_TRUE_MSG(out[0] == 0.0f && out[1] == 0.0f, "the chain starts at (0,0)");
    ASSERT_TRUE_MSG(out[(4 - 1) * 6 + 4] == 3.0f && out[(4 - 1) * 6 + 5] == 0.0f, "the chain ends at (3,0)");
    for (u32 q = 0; q < quads; q++) {
        ASSERT_TRUE_MSG(out[q * 6 + 1] == 0.0f && out[q * 6 + 3] == 0.0f && out[q * 6 + 5] == 0.0f,
                        "every lowered point stays on the line y=0");
    }
}

UTEST(vector_image, lower_parsed_shape) {
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_mono, sizeof(k_svg_mono) - 1, s_arena, &img), "parses");
    const FlVectorShape* shape = &img.shapes[0];
    f32 out[512 * 6];
    u32 quads = gpu_glyph_lower_segments(shape->segments, shape->segment_count, 0.0f, img.height, out);
    ASSERT_TRUE_MSG(quads >= shape->segment_count, "every contour segment lowers to at least one quadratic");
}

// Color lowering: each shape lowers independently into a contiguous quad range, in document order, with its
// solid fill + opacity baked into one premultiplied RGBA8 word.
UTEST(vector_image, color_lower_grouping_and_bake) {
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_color_opacity, sizeof(k_svg_color_opacity) - 1, s_arena, &img),
                    "a multi-color SVG with opacity parses");
    ASSERT_EQ_MSG((int)img.classification, (int)FlVectorClass_MultiColor, "two distinct colors -> multi-color");
    ASSERT_EQ_MSG((int)img.shape_count, 2, "both rects become shapes");

    u32 total_segments = 0;
    for (u32 s = 0; s < img.shape_count; s++) {
        total_segments += img.shapes[s].segment_count;
    }
    f32* floats = arena_alloc_array(s_arena, f32, (size_t)total_segments * GPU_GLYPH_LOWER_MAX_QUADS_PER_SEGMENT * 6);
    GpuColorShape shapes[2] = { 0 };
    u32 shape_count = 0;
    u32 total_quads = gpu_svg_color_lower(s_arena, &img, 1.0f, img.height, floats, shapes, &shape_count);

    ASSERT_EQ_MSG((int)shape_count, 2, "the shape count mirrors the document's shape count");

    // Quad ranges are contiguous and cover every lowered quad - the shader walks [quad_start, +quad_count).
    ASSERT_EQ_MSG((int)shapes[0].quad_start, 0, "the first shape starts at quad 0");
    ASSERT_EQ_MSG((int)shapes[1].quad_start, (int)shapes[0].quad_count,
                  "the second shape starts where the first ends (contiguous, no gap)");
    ASSERT_EQ_MSG((int)(shapes[0].quad_count + shapes[1].quad_count), (int)total_quads,
                  "the ranges cover exactly the total quad count");
    ASSERT_TRUE_MSG(shapes[0].quad_count >= img.shapes[0].segment_count
                        && shapes[1].quad_count >= img.shapes[1].segment_count,
                    "each contour segment lowers to at least one quadratic");

    ASSERT_EQ_MSG((int)shapes[0].fill_rule, 0, "shape 0 fill rule is nonzero");
    ASSERT_EQ_MSG((int)shapes[1].fill_rule, 0, "shape 1 fill rule is nonzero");

    // The color is premultiplied RGBA8 with R in the low byte; opacity is baked into both the alpha and the
    // premultiplied channels.
    ASSERT_EQ_MSG((int)(shapes[0].color & 0xff), 255, "shape 0 (red) premultiplied R = 255");
    ASSERT_EQ_MSG((int)((shapes[0].color >> 16) & 0xff), 0, "shape 0 (red) premultiplied B = 0");
    ASSERT_EQ_MSG((int)((shapes[0].color >> 24) & 0xff), 255, "shape 0 (red) alpha = 255 (fully opaque)");
    ASSERT_EQ_MSG((int)(shapes[1].color & 0xff), 0, "shape 1 (blue) premultiplied R = 0");
    ASSERT_EQ_MSG((int)((shapes[1].color >> 16) & 0xff), 128, "shape 1 (blue) premultiplied B halves to 128");
    ASSERT_EQ_MSG((int)((shapes[1].color >> 24) & 0xff), 128, "shape 1 (blue) alpha = 128 (opacity 0.5)");
}

// Even-odd fill rule passes through the lowering verbatim; the shader folds it per shape.
UTEST(vector_image, color_lower_evenodd_passthrough) {
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_evenodd, sizeof(k_svg_evenodd) - 1, s_arena, &img),
                    "an evenodd SVG parses");
    f32* floats = arena_alloc_array(s_arena, f32,
                                    (size_t)img.shapes[0].segment_count * GPU_GLYPH_LOWER_MAX_QUADS_PER_SEGMENT * 6);
    GpuColorShape shape = { 0 };
    u32 shape_count = 0;
    u32 total_quads = gpu_svg_color_lower(s_arena, &img, 1.0f, img.height, floats, &shape, &shape_count);

    ASSERT_EQ_MSG((int)shape_count, 1, "one shape");
    ASSERT_EQ_MSG((int)shape.fill_rule, 1, "the even-odd fill rule passes through to the wire record");
    ASSERT_EQ_MSG((int)shape.quad_start, 0, "the only shape starts at quad 0");
    ASSERT_EQ_MSG((int)shape.quad_count, (int)total_quads, "its range covers every lowered quad");
    ASSERT_EQ_MSG((int)(shape.color & 0xff), 0x11, "premultiplied R = 0x11");
    ASSERT_EQ_MSG((int)((shape.color >> 8) & 0xff), 0x22, "premultiplied G = 0x22");
    ASSERT_EQ_MSG((int)((shape.color >> 16) & 0xff), 0x33, "premultiplied B = 0x33");
    ASSERT_EQ_MSG((int)((shape.color >> 24) & 0xff), 0xff, "alpha = 0xff (opaque)");
}

UTEST(vector_image, atlas_one_live_cell_per_handle) {
    GpuSvgAtlas atlas = { 0 };
    gpu_svg_atlas_init(&atlas, s_arena, GpuSvgTexel_R8);

    const FlColor mono = { 255, 128, 0, 255 };
    const FlImageHandle h = 7;
    const GpuSvgEntry* a = gpu_svg_atlas_place(&atlas, h, 32, 20, 24, FlVectorClass_Monochrome, mono, NULL);
    ASSERT_TRUE_MSG(a != NULL, "an icon places");
    ASSERT_EQ_MSG((int)a->size, 32, "cell keyed at the requested size");
    ASSERT_TRUE_MSG(a->live, "the placed cell is live");
    ASSERT_EQ_MSG((int)a->cls, (int)FlVectorClass_Monochrome, "the cell records the classification");
    ASSERT_TRUE_MSG(a->mono_color.r == 255 && a->mono_color.g == 128 && a->mono_color.b == 0,
                    "the cell records the authored fill (the mono draw default)");
    ASSERT_TRUE_MSG(gpu_svg_atlas_find(&atlas, h) == a, "the handle resolves to its live cell");

    u32 count_before = atlas.entry_count;
    const GpuSvgEntry* a2 = gpu_svg_atlas_place(&atlas, h, 32, 20, 24, FlVectorClass_Monochrome, mono, NULL);
    ASSERT_TRUE_MSG(a2 == a, "re-placing at the same size returns the same cell (idempotent)");
    ASSERT_EQ_MSG((int)atlas.entry_count, (int)count_before, "no new entry on an idempotent re-place");

    const GpuSvgEntry* b = gpu_svg_atlas_place(&atlas, h, 64, 40, 48, FlVectorClass_Monochrome, mono, NULL);
    ASSERT_TRUE_MSG(b != NULL && b != a, "a size-step change appends a new cell");
    ASSERT_EQ_MSG((int)atlas.entry_count, (int)count_before + 1, "the new size adds exactly one entry");
    ASSERT_TRUE_MSG(gpu_svg_atlas_find(&atlas, h) == b, "the handle now resolves to the new cell");
    ASSERT_FALSE_MSG(a->live, "the prior cell is orphaned");

    const FlImageHandle g = 9;
    const GpuSvgEntry* c = gpu_svg_atlas_place(&atlas, g, 32, 16, 16, FlVectorClass_Monochrome, mono, NULL);
    ASSERT_TRUE_MSG(c != NULL && gpu_svg_atlas_find(&atlas, g) == c, "a second handle places independently");
    ASSERT_TRUE_MSG(gpu_svg_atlas_find(&atlas, h) == b, "the first handle is unaffected by the second");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The backend-agnostic prepare core: the glyph cell extent, the SVG uniform fit, the mono contour scaling, and the
// compaction-retry the GL and Metal prepare seams both drive. Headless CPU is the only cross-platform coverage
// this math gets - gpu_resource_prepare.c (the GL half) is not compiled at all on macOS.

UTEST(gpu_prepare_core, glyph_cell_rounds_control_box_out_plus_pad) {
    // Control box x [1.25, 6.5], y [-2.1, 9.75] rounds out to x [1, 7] and y [-3, 10]: a 6 x 13 texel box, plus
    // the padding ring on every side.
    FlGlyphSegment seg = { .kind = FlGlyphSegment_Line, .pts = { { 2.0f, 0.0f }, { 5.0f, 8.0f } } };
    FlGlyphOutline outline = {
        .segments = &seg,
        .segment_count = 1,
        .advance = 7.0f,
        .min_x = 1.25f,
        .min_y = -2.1f,
        .max_x = 6.5f,
        .max_y = 9.75f,
    };

    GpuGlyphCell cell = gpu_prepare_glyph_cell(&outline);
    ASSERT_FALSE_MSG(cell.empty, "a glyph with contours and a real control box earns a cell");
    ASSERT_EQ_MSG((int)cell.pw, 6 + 2 * GPU_GLYPH_ATLAS_PAD, "width = ceil(6.5) - floor(1.25), plus the ring");
    ASSERT_EQ_MSG((int)cell.ph, 13 + 2 * GPU_GLYPH_ATLAS_PAD, "height = ceil(9.75) - floor(-2.1), plus the ring");
    // The cell origin is its top-left corner in outline space. Flooring the origin - rather than translating min
    // to 0 - is what keeps the autohinter's subpixel stem positions.
    ASSERT_TRUE_MSG(cell.left == 1.0f - (f32)GPU_GLYPH_ATLAS_PAD, "left = floor(min_x) - pad");
    ASSERT_TRUE_MSG(cell.top == 10.0f + (f32)GPU_GLYPH_ATLAS_PAD, "top = ceil(max_y) + pad");
}

UTEST(gpu_prepare_core, glyph_cell_empty_glyph_is_metrics_only) {
    // A space: no contours, zeroed box. The advance is real, so the caller still places a metrics-only entry -
    // but there is no cell and no raster dispatch.
    FlGlyphOutline space = { .advance = 4.0f };
    GpuGlyphCell cell = gpu_prepare_glyph_cell(&space);
    ASSERT_TRUE_MSG(cell.empty, "an outline with no segments is empty");
    ASSERT_EQ_MSG((int)cell.pw, 0, "an empty glyph carries no cell width");
    ASSERT_EQ_MSG((int)cell.ph, 0, "an empty glyph carries no cell height");

    // A glyph that HAS contours but whose control box rounds out to nothing is empty too: a zero-texel cell
    // would otherwise dispatch a zero-sized raster.
    FlGlyphSegment seg = { .kind = FlGlyphSegment_Line, .pts = { { 0.0f, 0.0f }, { 0.0f, 0.0f } } };
    FlGlyphOutline degenerate = { .segments = &seg, .segment_count = 1 };
    ASSERT_TRUE_MSG(gpu_prepare_glyph_cell(&degenerate).empty, "a zero-extent control box is empty");
}

UTEST(gpu_prepare_core, svg_fit_lands_the_larger_document_side_on_the_raster_size) {
    // A 100x80 document at raster size 32: the LARGER side (100) lands on 32, so scale = 0.32 and the content is
    // 32 x ceil(80 * 0.32) = 32 x 26. Aspect is preserved - the fit is uniform, never per-axis.
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_mono, sizeof(k_svg_mono) - 1, s_arena, &img), "parses");
    ASSERT_TRUE_MSG(img.width == 100.0f && img.height == 80.0f, "the document extent is 100x80");

    GpuSvgCellFit fit = { 0 };
    ASSERT_TRUE_MSG(gpu_prepare_svg_fit(&img, 32, &fit), "a real document fits");
    ASSERT_TRUE_MSG(fit.scale == 32.0f / 100.0f, "the larger side scales onto the raster size");
    ASSERT_EQ_MSG((int)fit.pw, 32 + 2 * GPU_SVG_ATLAS_PAD, "content width = the raster size, plus the ring");
    ASSERT_EQ_MSG((int)fit.ph, 26 + 2 * GPU_SVG_ATLAS_PAD, "content height = ceil(80 * 0.32) = 26, plus the ring");

    // A document with no extent has nothing to place - the fit rejects it rather than dividing by zero.
    FlVectorImage no_extent = img;
    no_extent.width = 0.0f;
    no_extent.height = 0.0f;
    ASSERT_FALSE_MSG(gpu_prepare_svg_fit(&no_extent, 32, &fit), "a zero-extent document does not fit");

    // A document with extent on one axis only fits to an empty cell - also rejected, since a zero-texel cell
    // would dispatch a zero-sized raster.
    FlVectorImage no_width = img;
    no_width.width = 0.0f;
    ASSERT_FALSE_MSG(gpu_prepare_svg_fit(&no_width, 32, &fit), "a fit that collapses the width to zero is rejected");

    FlVectorImage no_height = img;
    no_height.height = 0.0f;
    ASSERT_FALSE_MSG(gpu_prepare_svg_fit(&no_height, 32, &fit), "a fit that collapses the height to zero is rejected");
}

UTEST(gpu_prepare_core, svg_cell_geometry_rejects_a_cell_with_nothing_to_raster) {
    // A cell whose geometry is missing, or whose shapes carry no contours, lowers to nothing.
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_mono, sizeof(k_svg_mono) - 1, s_arena, &img), "parses");

    GpuSvgCellFit fit = { 0 };
    ASSERT_TRUE_MSG(gpu_prepare_svg_fit(&img, 32, &fit), "fits");

    GpuSvgAtlas atlas = { 0 };
    gpu_svg_atlas_init(&atlas, s_arena, GpuSvgTexel_R8);
    const GpuSvgEntry* entry
        = gpu_svg_atlas_place(&atlas, 1, 32, fit.pw, fit.ph, img.classification, img.mono_color, &img);
    ASSERT_TRUE_MSG(entry != NULL, "the cell places");

    GpuSvgCellGeometry geom = { 0 };
    ASSERT_FALSE_MSG(gpu_prepare_svg_cell_geometry(s_arena, entry, NULL, false, &geom),
                     "a cell with no retained geometry lowers to nothing");

    FlVectorImage contourless = img;
    FlVectorShape empty_shapes[2] = { img.shapes[0], img.shapes[1] };
    empty_shapes[0].segment_count = 0;
    empty_shapes[1].segment_count = 0;
    contourless.shapes = empty_shapes;
    ASSERT_FALSE_MSG(gpu_prepare_svg_cell_geometry(s_arena, entry, &contourless, false, &geom),
                     "shapes with no contours lower to nothing");
}

typedef struct PreparedExtent {
    f32 min_x, max_x, min_y, max_y;
} PreparedExtent;

static PreparedExtent lowered_extent(const GpuSvgCellGeometry* geom) {
    PreparedExtent e = {
        .min_x = geom->floats[0],
        .max_x = geom->floats[0],
        .min_y = geom->floats[1],
        .max_y = geom->floats[1],
    };
    for (u32 i = 0; i < geom->quad_count * 6; i += 2) {
        f32 x = geom->floats[i];
        f32 y = geom->floats[i + 1];
        e.min_x = x < e.min_x ? x : e.min_x;
        e.max_x = x > e.max_x ? x : e.max_x;
        e.min_y = y < e.min_y ? y : e.min_y;
        e.max_y = y > e.max_y ? y : e.max_y;
    }
    return e;
}

UTEST(gpu_prepare_core, svg_cell_geometry_lowers_mono_contours_into_the_padded_cell) {
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_mono, sizeof(k_svg_mono) - 1, s_arena, &img), "parses");
    ASSERT_EQ_MSG((int)img.classification, (int)FlVectorClass_Monochrome, "one shared fill -> monochrome");

    // Raster at the document's larger side, so the fit is 1:1 and the lowered coordinates are the document's own
    // (modulo the y flip into the cell) - the padding ring is then the only offset in play.
    GpuSvgCellFit fit = { 0 };
    ASSERT_TRUE_MSG(gpu_prepare_svg_fit(&img, 100, &fit), "fits at 1:1");
    ASSERT_TRUE_MSG(fit.scale == 1.0f, "a raster size equal to the larger document side is a 1:1 fit");

    GpuSvgAtlas atlas = { 0 };
    gpu_svg_atlas_init(&atlas, s_arena, GpuSvgTexel_R8);
    const GpuSvgEntry* entry
        = gpu_svg_atlas_place(&atlas, 1, 100, fit.pw, fit.ph, img.classification, img.mono_color, &img);
    ASSERT_TRUE_MSG(entry != NULL, "the cell places");

    GpuSvgCellGeometry geom = { 0 };
    ASSERT_TRUE_MSG(gpu_prepare_svg_cell_geometry(s_arena, entry, &img, false, &geom), "the mono cell lowers");
    ASSERT_TRUE_MSG(geom.shapes == NULL && geom.shape_count == 0,
                    "the mono path lowers one undifferentiated soup — nothing per-shape to say");

    u32 total_segments = 0;
    for (u32 s = 0; s < img.shape_count; s++) {
        total_segments += img.shapes[s].segment_count;
    }
    ASSERT_TRUE_MSG(geom.quad_count >= total_segments,
                    "the soup carries every shape's contours — each segment lowers to at least one quadratic");

    // Two flips compose here: vector_image_parse turns y-down SVG into the y-up document space the shared
    // lowering wants (y_up = height - y_svg), and the lowering turns it back into the y-down cell
    // (cell_y = content_h + PAD - y_up). At 1:1 they cancel exactly, so cell_y = y_svg + PAD - and cell_x is
    // x_svg + PAD, since x is never flipped. k_svg_mono's two rects span SVG x [10, 70] and y [10, 60].
    PreparedExtent ext = lowered_extent(&geom);
    const f32 pad = (f32)GPU_SVG_ATLAS_PAD;
    ASSERT_TRUE_MSG(ext.min_x == 10.0f + pad, "the leftmost contour is SVG x=10, offset by the ring");
    ASSERT_TRUE_MSG(ext.max_x == 70.0f + pad, "the rightmost contour is SVG x=70, offset by the ring");
    ASSERT_TRUE_MSG(ext.min_y == 10.0f + pad, "the parse's y-up flip and the lowering's y-down flip cancel at 1:1");
    ASSERT_TRUE_MSG(ext.max_y == 60.0f + pad, "so the bottom-most contour is SVG y=60, offset by the ring");
}

UTEST(gpu_prepare_core, svg_cell_geometry_scales_mono_contours_by_the_fit) {
    // The same geometry at half the raster size. Every contour coordinate scales by the fit before it is lowered,
    // so cell_x/cell_y become scale * svg + PAD.
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_mono, sizeof(k_svg_mono) - 1, s_arena, &img), "parses");

    GpuSvgCellFit fit = { 0 };
    ASSERT_TRUE_MSG(gpu_prepare_svg_fit(&img, 50, &fit), "fits at half the larger document side");
    ASSERT_TRUE_MSG(fit.scale == 0.5f, "the fit halves the document");

    GpuSvgAtlas atlas = { 0 };
    gpu_svg_atlas_init(&atlas, s_arena, GpuSvgTexel_R8);
    const GpuSvgEntry* entry
        = gpu_svg_atlas_place(&atlas, 1, 50, fit.pw, fit.ph, img.classification, img.mono_color, &img);
    ASSERT_TRUE_MSG(entry != NULL, "the cell places");

    GpuSvgCellGeometry geom = { 0 };
    ASSERT_TRUE_MSG(gpu_prepare_svg_cell_geometry(s_arena, entry, &img, false, &geom), "the mono cell lowers");

    PreparedExtent ext = lowered_extent(&geom);
    const f32 pad = (f32)GPU_SVG_ATLAS_PAD;
    ASSERT_TRUE_MSG(ext.min_x == 5.0f + pad, "SVG x=10 scales to 5");
    ASSERT_TRUE_MSG(ext.max_x == 35.0f + pad, "SVG x=70 scales to 35");
    ASSERT_TRUE_MSG(ext.min_y == 5.0f + pad, "SVG y=10 scales to 5 (the two y flips still cancel)");
    ASSERT_TRUE_MSG(ext.max_y == 30.0f + pad, "SVG y=60 scales to 30");
}

UTEST(gpu_prepare_core, svg_cell_geometry_keeps_shapes_distinct_on_the_color_path) {
    // The color path's shapes must stay separable - the shader composites them in document order - where the mono
    // path deliberately loses that distinction.
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_multi, sizeof(k_svg_multi) - 1, s_arena, &img), "parses");
    ASSERT_EQ_MSG((int)img.classification, (int)FlVectorClass_MultiColor, "distinct fills -> multi-color");
    ASSERT_EQ_MSG((int)img.shape_count, 2, "both rects become shapes");

    GpuSvgCellFit fit = { 0 };
    ASSERT_TRUE_MSG(gpu_prepare_svg_fit(&img, 64, &fit), "fits");

    GpuSvgAtlas atlas = { 0 };
    gpu_svg_atlas_init(&atlas, s_arena, GpuSvgTexel_Rgba8);
    const GpuSvgEntry* entry
        = gpu_svg_atlas_place(&atlas, 3, 64, fit.pw, fit.ph, img.classification, img.mono_color, &img);
    ASSERT_TRUE_MSG(entry != NULL, "the cell places");

    GpuSvgCellGeometry geom = { 0 };
    ASSERT_TRUE_MSG(gpu_prepare_svg_cell_geometry(s_arena, entry, &img, true, &geom), "the color cell lowers");
    ASSERT_EQ_MSG((int)geom.shape_count, (int)img.shape_count, "one wire record per document shape");
    ASSERT_EQ_MSG((int)geom.shapes[0].quad_start, 0, "the first shape starts at quad 0");
    ASSERT_EQ_MSG((int)(geom.shapes[0].quad_count + geom.shapes[1].quad_count), (int)geom.quad_count,
                  "the per-shape ranges cover exactly the lowered quads");
}

// The prepare core reaches back into the backend only through a GpuRasterCellFn - the one genuinely GL-or-Metal
// step. A counting stub stands in for that dispatch.
typedef struct FakeRasterBackend {
    u32 raster_calls;
} FakeRasterBackend;

static void fake_raster_cell(void* backend, const GpuSvgEntry* entry, const FlVectorImage* vector, bool color) {
    (void)entry;
    (void)vector;
    (void)color;
    ((FakeRasterBackend*)backend)->raster_calls++;
}

UTEST(gpu_prepare_core, vector_into_rasters_once_per_size_step) {
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_mono, sizeof(k_svg_mono) - 1, s_arena, &img), "parses");

    GpuSvgAtlas atlas = { 0 };
    gpu_svg_atlas_init(&atlas, s_arena, GpuSvgTexel_R8);
    FakeRasterBackend fake = { 0 };
    u32 compact_count = 0;

    gpu_prepare_vector_into(&fake, &atlas, 1, 32, &img, false, fake_raster_cell, &compact_count);
    ASSERT_EQ_MSG((int)fake.raster_calls, 1, "a fresh icon rasters once");
    ASSERT_TRUE_MSG(gpu_svg_atlas_find(&atlas, 1) != NULL, "and it is placed live");

    gpu_prepare_vector_into(&fake, &atlas, 1, 32, &img, false, fake_raster_cell, &compact_count);
    ASSERT_EQ_MSG((int)fake.raster_calls, 1, "re-preparing at the same size does not re-dispatch");

    gpu_prepare_vector_into(&fake, &atlas, 1, 64, &img, false, fake_raster_cell, &compact_count);
    ASSERT_EQ_MSG((int)fake.raster_calls, 2, "a size step rasters into a fresh cell");
    ASSERT_EQ_MSG((int)compact_count, 0, "a roomy atlas never compacts");

    // A document with no extent is dropped before it ever reaches the atlas.
    FlVectorImage no_extent = img;
    no_extent.width = 0.0f;
    no_extent.height = 0.0f;
    gpu_prepare_vector_into(&fake, &atlas, 2, 32, &no_extent, false, fake_raster_cell, &compact_count);
    ASSERT_EQ_MSG((int)fake.raster_calls, 2, "a zero-extent document neither places nor rasters");
    ASSERT_TRUE_MSG(gpu_svg_atlas_find(&atlas, 2) == NULL, "and nothing is live for it");
}

UTEST(gpu_prepare_core, vector_into_compacts_and_re_rasters_survivors_when_the_atlas_fills) {
    // Zoom one handle up the size ladder. Each step orphans the prior cell (the packer is append-only), so the
    // atlas eventually refuses a placement. The core then compacts wholesale, re-rasters every survivor from its
    // retained geometry - the texels still sit at their pre-compaction positions - and retries the placement once.
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_mono, sizeof(k_svg_mono) - 1, s_arena, &img), "parses");

    GpuSvgAtlas atlas = { 0 };
    gpu_svg_atlas_init(&atlas, s_arena, GpuSvgTexel_R8);
    FakeRasterBackend fake = { 0 };
    u32 compact_count = 0;

    // Near the top of the mono ladder each cell is roughly 2000x1600, so only a handful fit in the 4096^2 atlas.
    u32 steps = 0;
    for (u16 size = 2000; size < 2064 && compact_count == 0; size++) {
        gpu_prepare_vector_into(&fake, &atlas, 1, size, &img, false, fake_raster_cell, &compact_count);
        steps++;
    }

    ASSERT_EQ_MSG((int)compact_count, 1, "the zoom fills the atlas and compacts exactly once");
    ASSERT_TRUE_MSG(steps > 1, "it took several size steps to fill the atlas");
    ASSERT_TRUE_MSG(fake.raster_calls > steps,
                    "the compacting step rastered the surviving cell on top of the newly placed one");
    ASSERT_TRUE_MSG(gpu_svg_atlas_find(&atlas, 1) != NULL, "the retry placed the icon after the compaction");

    // The entry table collapsed from steps cells to the one survivor, and the retried placement then appended
    // the new cell, orphaning that survivor in turn - one live cell per handle.
    u32 live = 0;
    for (u32 i = 0; i < atlas.entry_count; i++) {
        live += atlas.entries[i].live ? 1u : 0u;
    }
    ASSERT_EQ_MSG((int)atlas.entry_count, 2, "the survivor plus the newly placed cell — every orphan is gone");
    ASSERT_EQ_MSG((int)live, 1, "the handle keeps exactly one live cell");
}

UTEST(gpu_prepare_core, vector_into_drops_an_icon_too_big_for_even_a_fresh_atlas) {
    // The other side of the compaction retry: an icon whose cell cannot fit the atlas at ANY packing still fails
    // the retried placement. It must be dropped - not rastered against a NULL entry, and not retried forever.
    FlVectorImage img = { 0 };
    ASSERT_TRUE_MSG(vector_image_parse((const u8*)k_svg_mono, sizeof(k_svg_mono) - 1, s_arena, &img), "parses");

    GpuSvgAtlas atlas = { 0 };
    gpu_svg_atlas_init(&atlas, s_arena, GpuSvgTexel_R8);
    FakeRasterBackend fake = { 0 };
    u32 compact_count = 0;

    // The document's larger side is 100, so a raster size past the atlas extent gives a cell wider than the whole
    // 4096-texel atlas - unplaceable even against a freshly compacted one.
    const u16 oversized = GPU_SVG_ATLAS_SIZE_MONO + 1000;
    GpuSvgCellFit fit = { 0 };
    ASSERT_TRUE_MSG(gpu_prepare_svg_fit(&img, oversized, &fit),
                    "the fit itself succeeds — it is the atlas that refuses");
    ASSERT_TRUE_MSG(fit.pw > GPU_SVG_ATLAS_SIZE_MONO, "the cell really is wider than the atlas");

    gpu_prepare_vector_into(&fake, &atlas, 1, oversized, &img, false, fake_raster_cell, &compact_count);

    ASSERT_EQ_MSG((int)fake.raster_calls, 0, "an unplaceable icon is never rastered");
    ASSERT_EQ_MSG((int)compact_count, 1, "the atlas was compacted once in the attempt to make room");
    ASSERT_TRUE_MSG(gpu_svg_atlas_find(&atlas, 1) == NULL, "and the icon is dropped, not left half-placed");
}

// An overflow part-way through a frame's prepare pass must not wipe the atlas inline - glyphs already
// placed this frame have to keep resolving so they don't blank at draw. Overflow flags a deferred wipe that
// the backend honors at the end of render(); the next frame then repacks from a fresh atlas.
UTEST(gpu_glyph_atlas, earlier_glyph_survives_same_frame_overflow) {
    GpuGlyphAtlas atlas = { 0 };
    gpu_glyph_atlas_init(&atlas, s_arena);

    const FlFontId font = 1;
    const u16 size = 16;

    const GpuGlyphEntry* early = gpu_glyph_atlas_place(&atlas, font, size, 'A', 8, 8, 0.0f, 0.0f, 8.0f);
    ASSERT_TRUE_MSG(early != NULL, "the early glyph places into the fresh atlas");

    // Keep placing oversized cells until the skyline packer refuses.
    bool overflowed = false;
    for (u32 i = 0; i < GPU_GLYPH_ATLAS_MAX_ENTRIES && !overflowed; i++) {
        const GpuGlyphEntry* e = gpu_glyph_atlas_place(&atlas, font, size, 1000 + i, 256, 256, 0.0f, 0.0f, 0.0f);
        if (e == NULL) {
            gpu_glyph_atlas_on_overflow(&atlas); // the production overflow response prepare_glyph runs
            overflowed = true;
        }
    }
    ASSERT_TRUE_MSG(overflowed, "the oversized cells exhaust the atlas");
    ASSERT_TRUE_MSG(atlas.needs_wipe, "overflow flags the deferred wipe");

    ASSERT_TRUE_MSG(gpu_glyph_atlas_find(&atlas, font, size, 'A') != NULL,
                    "the early glyph survives a same-frame overflow");

    // End of render(): honor the deferred wipe.
    gpu_glyph_atlas_wipe_if_needed(&atlas, s_arena);
    ASSERT_FALSE_MSG(atlas.needs_wipe, "honoring the wipe clears the flag");
    ASSERT_TRUE_MSG(gpu_glyph_atlas_find(&atlas, font, size, 'A') == NULL,
                    "after the frame-boundary wipe the atlas is empty");

    // A frame with no overflow flags nothing: the honor call is then a no-op and must not wipe a populated atlas.
    const GpuGlyphEntry* fresh = gpu_glyph_atlas_place(&atlas, font, size, 'B', 8, 8, 0.0f, 0.0f, 8.0f);
    ASSERT_TRUE_MSG(fresh != NULL, "a glyph places into the wiped atlas");
    ASSERT_FALSE_MSG(atlas.needs_wipe, "a normal placement does not flag a wipe");
    gpu_glyph_atlas_wipe_if_needed(&atlas, s_arena);
    ASSERT_TRUE_MSG(gpu_glyph_atlas_find(&atlas, font, size, 'B') != NULL,
                    "an unflagged honor call is a no-op and keeps the live set intact");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// gpu_orchestrator: the backend-agnostic per-frame sequence + the shared text-glyph walk, exercised on CPU
// through a fake GpuRenderOps that records the op call order and every prepare_glyph invocation. This is the only
// coverage the shared orchestrator gets on the GL/Linux build (its render path never runs in CI); on Metal the
// end-to-end gpu_smoke_test drives the same code.

typedef enum {
    ORCH_FRAME_BEGIN = 1,
    ORCH_TRANSLATE,
    ORCH_BEGIN_ENCODER,
    ORCH_UPLOAD_INPUTS,
    ORCH_CLASSIFY,
    ORCH_BARRIER,
    ORCH_RASTER,
    ORCH_END_ENCODER,
} OrchOpId;

typedef struct OrchProbe {
    struct FlFontCache* font_cache; // handed back by get_font_cache (NULL exercises the walk short-circuit)
    GpuDrawList translate_result;   // what translate returns (its .count must reach classify as cmd_count)

    OrchOpId ops_seen[16];
    u32 op_count;
    u32 classify_cmd_count;

    struct {
        FlFontId font;
        u16 size;
        u32 glyph_id;
    } glyphs[16];
    u32 glyph_count;
} OrchProbe;

static void orch_log(OrchProbe* p, OrchOpId id) {
    if (p->op_count < 16) {
        p->ops_seen[p->op_count++] = id;
    }
}

static void orch_frame_begin(void* b) {
    orch_log((OrchProbe*)b, ORCH_FRAME_BEGIN);
}

static struct FlFontCache* orch_get_font_cache(void* b) {
    return ((OrchProbe*)b)->font_cache;
}

static void orch_prepare_glyph(void* b, FlFontId font, u16 size, u32 glyph_id, const struct FlGlyphOutline* outline) {
    OrchProbe* p = b;
    (void)outline;
    if (p->glyph_count < 16) {
        p->glyphs[p->glyph_count].font = font;
        p->glyphs[p->glyph_count].size = size;
        p->glyphs[p->glyph_count].glyph_id = glyph_id;
        p->glyph_count++;
    }
}

static GpuDrawList orch_translate(void* b, const struct FlRenderFrame* frame, u32 w, u32 h) {
    (void)frame;
    (void)w;
    (void)h;
    orch_log((OrchProbe*)b, ORCH_TRANSLATE);
    return ((OrchProbe*)b)->translate_result;
}

static void orch_begin_encoder(void* b, void* surface) {
    (void)surface;
    orch_log((OrchProbe*)b, ORCH_BEGIN_ENCODER);
}

static void orch_upload_inputs(void* b, const GpuDrawList* list) {
    (void)list;
    orch_log((OrchProbe*)b, ORCH_UPLOAD_INPUTS);
}

static void orch_classify(void* b, void* surface, u32 cmd_count) {
    (void)surface;
    ((OrchProbe*)b)->classify_cmd_count = cmd_count;
    orch_log((OrchProbe*)b, ORCH_CLASSIFY);
}

static void orch_barrier(void* b) {
    orch_log((OrchProbe*)b, ORCH_BARRIER);
}

static void orch_raster(void* b, void* surface) {
    (void)surface;
    orch_log((OrchProbe*)b, ORCH_RASTER);
}

static void orch_end_encoder(void* b) {
    orch_log((OrchProbe*)b, ORCH_END_ENCODER);
}

static const GpuRenderOps k_orch_probe_ops = {
    .frame_begin = orch_frame_begin,
    .get_font_cache = orch_get_font_cache,
    .prepare_glyph = orch_prepare_glyph,
    .translate = orch_translate,
    .begin_encoder = orch_begin_encoder,
    .upload_inputs = orch_upload_inputs,
    .classify = orch_classify,
    .barrier = orch_barrier,
    .raster = orch_raster,
    .end_encoder = orch_end_encoder,
};

static FlRenderFrame orch_make_text_frame(u8* buf, FlFontId font, u16 size, const u32* codepoints, u32 n) {
    FlCmdHeader* hdr = (FlCmdHeader*)buf;
    FlCmdTextRun* run = (FlCmdTextRun*)(hdr + 1);
    FlGlyph* glyphs = (FlGlyph*)(run + 1);
    hdr->kind = FlCmd_TextRun;
    hdr->flags = 0;
    hdr->byte_size = (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdTextRun) + (size_t)n * sizeof(FlGlyph));
    run->font = font;
    run->size = size;
    run->glyph_count = n;
    run->scale = 1.0f;
    for (u32 i = 0; i < n; i++) {
        glyphs[i].glyph_id = codepoints[i];
        glyphs[i].x = 0.0f;
        glyphs[i].y = 0.0f;
    }
    FlRenderFrame frame = { 0 };
    frame.commands = buf;
    frame.bytes = hdr->byte_size;
    return frame;
}

UTEST(gpu_orchestrator, canonical_sequence_and_null_font_cache_short_circuit) {
    GpuGlyphAtlas glyph_atlas = { 0 };
    gpu_glyph_atlas_init(&glyph_atlas, s_arena);
    FlArena* frame_arena = arena_new();

    OrchProbe probe = { 0 };
    probe.font_cache = NULL; // no cache -> the walk must not realize any glyph
    probe.translate_result = (GpuDrawList) { .count = 7, .clip_count = 1 };

    u8 buf[256];
    const u32 codepoints[] = { 'A', 'o' };
    FlRenderFrame frame = orch_make_text_frame(buf, 1, 32, codepoints, 2);

    gpu_orchestrate_frame(&k_orch_probe_ops, &probe, NULL, &frame, 128, 64, &glyph_atlas, frame_arena, s_arena);

    static const OrchOpId expected[] = { ORCH_FRAME_BEGIN, ORCH_TRANSLATE, ORCH_BEGIN_ENCODER, ORCH_UPLOAD_INPUTS,
                                         ORCH_CLASSIFY,    ORCH_BARRIER,   ORCH_RASTER,        ORCH_END_ENCODER };
    ASSERT_EQ_MSG((int)probe.op_count, 8, "every op runs exactly once");
    for (u32 i = 0; i < probe.op_count; i++) {
        ASSERT_EQ_MSG((int)probe.ops_seen[i], (int)expected[i], "ops run in the canonical order");
    }
    ASSERT_EQ_MSG((int)probe.classify_cmd_count, 7, "translate's list.count reaches classify as cmd_count");
    ASSERT_EQ_MSG((int)probe.glyph_count, 0, "a NULL font cache short-circuits the glyph walk");

    fl_arena_destroy(frame_arena);
}

UTEST(gpu_orchestrator, text_run_realizes_each_glyph_through_op) {
    GpuGlyphAtlas glyph_atlas = { 0 };
    gpu_glyph_atlas_init(&glyph_atlas, s_arena);
    FlArena* frame_arena = arena_new();

    FlFontCache* cache = font_cache_new(s_arena);
    const u16 size = 32;
    const u16 size_q4 = font_cache_whole_px_to_q4(size); // text runs carry the quarter-px size key
    FlFontId font = font_cache_load_font_memory(cache, k_glyph_test_ttf, k_glyph_test_ttf_size, size);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "the embedded test font loads");

    OrchProbe probe = { 0 };
    probe.font_cache = cache;
    probe.translate_result = (GpuDrawList) { 0 };

    u8 buf[256];
    const u32 codepoints[] = { 'A', 'o' };
    FlRenderFrame frame = orch_make_text_frame(buf, font, size_q4, codepoints, 2);

    gpu_orchestrate_frame(&k_orch_probe_ops, &probe, NULL, &frame, 128, 64, &glyph_atlas, frame_arena, s_arena);

    ASSERT_EQ_MSG((int)probe.glyph_count, 2, "both mapped glyphs realize through the prepare_glyph op");
    ASSERT_EQ_MSG((int)probe.glyphs[0].glyph_id, (int)'A', "first glyph keeps its codepoint key");
    ASSERT_EQ_MSG((int)probe.glyphs[1].glyph_id, (int)'o', "second glyph keeps its codepoint key");
    for (u32 i = 0; i < probe.glyph_count; i++) {
        ASSERT_EQ_MSG((int)probe.glyphs[i].font, (int)font, "the run's font id is passed through");
        ASSERT_EQ_MSG((int)probe.glyphs[i].size, (int)size_q4, "the run's raster size key is passed through");
    }

    fl_arena_destroy(frame_arena);
}

UTEST(vector_image, svg_quantize_ladder) {
    const FlVectorClass mono = FlVectorClass_Monochrome;
    ASSERT_EQ_MSG((int)svg_quantize_size(1, mono), 12, "below the ladder snaps to the first step");
    ASSERT_EQ_MSG((int)svg_quantize_size(33, mono), 48, "33 snaps up to 48");
    ASSERT_EQ_MSG((int)svg_quantize_size(64, mono), 64, "a step value maps to itself");
    ASSERT_EQ_MSG((int)svg_quantize_size(9999, mono), 2048, "above the ladder clamps to the mono top step (2048)");

    u16 prev = 0;
    for (u16 s = 1; s <= 2100; s++) {
        u16 q = svg_quantize_size(s, mono);
        ASSERT_EQ_MSG((int)svg_quantize_size(q, mono), (int)q, "quantize is idempotent at a step");
        ASSERT_TRUE_MSG(q >= prev, "quantize is monotonic non-decreasing");
        prev = q;
    }
}

// Per-class ceiling: the same ladder, but a multi-color icon caps at 1024 (the RGBA8 atlas is 2048^2) while a
// monochrome icon caps at 2048 (the R8 atlas is 4096^2).
UTEST(vector_image, svg_quantize_per_class_clamp) {
    const FlVectorClass mono = FlVectorClass_Monochrome;
    const FlVectorClass color = FlVectorClass_MultiColor;

    // Below both caps the class makes no difference - the ladder is shared.
    ASSERT_EQ_MSG((int)svg_quantize_size(500, mono), (int)svg_quantize_size(500, color),
                  "below either cap the class does not change the step");
    ASSERT_EQ_MSG((int)svg_quantize_size(1024, mono), 1024, "1024 is an exact step for both classes");
    ASSERT_EQ_MSG((int)svg_quantize_size(1024, color), 1024, "1024 is the color cap, still an exact step");

    // Just past the color cap: a color icon clamps to 1024, a mono icon climbs to the next step (1536).
    ASSERT_EQ_MSG((int)svg_quantize_size(1025, color), 1024, "past the color cap clamps to 1024");
    ASSERT_EQ_MSG((int)svg_quantize_size(1025, mono), 1536, "a mono icon keeps climbing past 1024");
    ASSERT_EQ_MSG((int)svg_quantize_size(1536, mono), 1536, "1536 is a mono-only step");

    // Mono cap boundary: 2048 is the top, anything above clamps there; color stays pinned at 1024.
    ASSERT_EQ_MSG((int)svg_quantize_size(2048, mono), 2048, "2048 is the mono cap, an exact step");
    ASSERT_EQ_MSG((int)svg_quantize_size(2049, mono), 2048, "past the mono cap clamps to 2048");
    ASSERT_EQ_MSG((int)svg_quantize_size(9999, color), 1024, "a color icon never exceeds its 1024 cap");

    ASSERT_EQ_MSG((int)svg_quantize_size(svg_quantize_size(9999, color), color), 1024,
                  "color clamp is idempotent at the cap");
    ASSERT_EQ_MSG((int)svg_quantize_size(svg_quantize_size(9999, mono), mono), 2048,
                  "mono clamp is idempotent at the cap");
}

// The builder-side bridge: a continuously-zooming host quantizes its physical dst size onto the ladder to key
// the prepare seam, then draws the cell at the exact dst. Crispness comes from quantizing the UNIFORM (larger)
// side; a non-uniform stretch rides linear sampling and must never bump the raster size on the short axis.
UTEST(vector_image, raster_size_for_dst) {
    const FlVectorClass mono = FlVectorClass_Monochrome;
    ASSERT_EQ_MSG((int)svg_raster_size_for_dst(40.0f, 30.0f, mono), 48,
                  "the larger side (40) drives the ladder: snaps up to 48");
    ASSERT_EQ_MSG((int)svg_raster_size_for_dst(30.0f, 40.0f, mono), 48,
                  "axis order does not matter — uniform = max side");
    ASSERT_EQ_MSG((int)svg_raster_size_for_dst(64.0f, 64.0f, mono), 64, "a square dst on a step maps to that step");
    ASSERT_EQ_MSG((int)svg_raster_size_for_dst(32.1f, 10.0f, mono), 48,
                  "a fractional size ceils before quantizing (33 -> 48)");
    ASSERT_EQ_MSG((int)svg_raster_size_for_dst(0.0f, 0.0f, mono), 12, "a degenerate dst snaps to the smallest step");

    // A non-uniform stretch: 96x40 and 96x90 quantize to the SAME raster size, so the short axis re-uses one
    // cell - the per-axis softness is sampled, not re-rastered.
    ASSERT_EQ_MSG((int)svg_raster_size_for_dst(96.0f, 40.0f, mono), (int)svg_raster_size_for_dst(96.0f, 90.0f, mono),
                  "the short axis does not change the raster size — non-uniform stretch samples one cell");

    // The size is continuous across the host's zoom while the raster size jumps only at ladder steps: sweeping
    // the uniform size from 17..24 stays on step 24 (no re-key), and 24->25 is the only crossing into 32.
    for (f32 s = 17.0f; s <= 24.0f; s += 1.0f) {
        ASSERT_EQ_MSG((int)svg_raster_size_for_dst(s, s, mono), 24, "within a step, the raster size is constant");
    }
    ASSERT_EQ_MSG((int)svg_raster_size_for_dst(25.0f, 25.0f, mono), 32, "crossing a step boundary keys the next size");

    // A mono dst beyond 512 keeps sharpening: 800px keys step 1024.
    ASSERT_EQ_MSG((int)svg_raster_size_for_dst(800.0f, 600.0f, mono), 1024,
                  "a >512px mono dst keys a larger-than-512 step (no longer capped at 512)");
    ASSERT_EQ_MSG((int)svg_raster_size_for_dst(1600.0f, 1200.0f, mono), 2048,
                  "a very large mono dst climbs to the 2048 mono cap");
}

// The format only records what the GL layer will store; the CPU bookkeeping never branches on it, so an RGBA8
// atlas places / resolves / orphans / re-keys identically to the R8 one.
UTEST(vector_image, atlas_format_agnostic) {
    GpuSvgAtlas r8 = { 0 };
    GpuSvgAtlas rgba = { 0 };
    gpu_svg_atlas_init(&r8, s_arena, GpuSvgTexel_R8);
    gpu_svg_atlas_init(&rgba, s_arena, GpuSvgTexel_Rgba8);
    ASSERT_EQ_MSG((int)r8.format, (int)GpuSvgTexel_R8, "the atlas records its format");
    ASSERT_EQ_MSG((int)rgba.format, (int)GpuSvgTexel_Rgba8, "the rgba atlas records its format");

    const FlColor color = { 10, 20, 30, 255 };
    const FlImageHandle h = 3;
    const GpuSvgEntry* a8 = gpu_svg_atlas_place(&r8, h, 32, 20, 24, FlVectorClass_MultiColor, color, NULL);
    const GpuSvgEntry* ar = gpu_svg_atlas_place(&rgba, h, 32, 20, 24, FlVectorClass_MultiColor, color, NULL);
    ASSERT_TRUE_MSG(a8 != NULL && ar != NULL, "both formats place the cell");
    ASSERT_TRUE_MSG(a8->cell.x == ar->cell.x && a8->cell.y == ar->cell.y && a8->cell.w == ar->cell.w
                        && a8->cell.h == ar->cell.h,
                    "cell geometry is identical across formats (the packer is format-blind)");

    const GpuSvgEntry* b8 = gpu_svg_atlas_place(&r8, h, 64, 40, 48, FlVectorClass_MultiColor, color, NULL);
    const GpuSvgEntry* br = gpu_svg_atlas_place(&rgba, h, 64, 40, 48, FlVectorClass_MultiColor, color, NULL);
    ASSERT_TRUE_MSG(b8 != NULL && br != NULL, "a step change appends a new cell in both");
    ASSERT_FALSE_MSG(a8->live, "the R8 prior cell orphans");
    ASSERT_FALSE_MSG(ar->live, "the RGBA8 prior cell orphans identically");
    ASSERT_TRUE_MSG(gpu_svg_atlas_find(&r8, h) == b8 && gpu_svg_atlas_find(&rgba, h) == br,
                    "both resolve the handle to the new live cell");
    ASSERT_EQ_MSG((int)r8.entry_count, (int)rgba.entry_count, "identical entry counts after the same sequence");
}

// Compaction safety valve: the skyline packer is append-only, so re-keying a handle onto new size steps leaks
// its prior cells as orphans until a placement fails. gpu_svg_atlas_compact repacks only the live cells into a
// fresh atlas, dropping every orphan and retaining each live handle at its current size and geometry. The
// bookkeeping moves cells, never texels, so R8 and RGBA8 compact to the identical live set.

typedef struct CompactObs {
    bool overflowed;     // a placement failed (the append-only atlas filled)
    u32 orphans_before;  // orphaned cells present at overflow
    u32 count_before;    // entry count at overflow (live handles + orphans)
    u32 count_after;     // entry count after compaction (the live set)
    bool all_live_after; // every surviving entry is live
    bool a_resolved;     // the re-keyed handle A resolves after compaction...
    u16 a_size_after;    // ...at this size
    u16 a_last_placed;   // A's last successfully-placed (current) size
    bool b_resolved;     // the sibling B resolves after compaction...
    u16 b_size_after;    // ...at this size
    bool refit;          // a placement the full atlas refused now succeeds
    bool geometry_kept;  // the retained geometry pointers survived compaction
} CompactObs;

static CompactObs run_compaction_sequence(GpuSvgAtlas* atlas, const FlVectorImage* geomA, const FlVectorImage* geomB) {
    const FlColor mono = { 1, 2, 3, 255 };
    const FlImageHandle A = 1, B = 2;
    CompactObs obs = { 0 };

    // A small sibling that must survive compaction at its current size.
    gpu_svg_atlas_place(atlas, B, 16, 16, 16, FlVectorClass_Monochrome, mono, geomB);

    // Re-key A across distinct big sizes; each step orphans A's prior cell (several fit before the packer fills).
    for (u16 s = 1; s <= 256; s++) {
        const GpuSvgEntry* e = gpu_svg_atlas_place(atlas, A, s, 360, 360, FlVectorClass_Monochrome, mono, geomA);
        if (e == NULL) {
            obs.overflowed = true; // the failing placement leaves the prior live cell intact
            break;
        }
        obs.a_last_placed = s;
    }
    obs.count_before = atlas->entry_count;
    for (u32 i = 0; i < atlas->entry_count; i++) {
        if (!atlas->entries[i].live) {
            obs.orphans_before++;
        }
    }

    gpu_svg_atlas_compact(atlas);

    obs.count_after = atlas->entry_count;
    obs.all_live_after = true;
    for (u32 i = 0; i < atlas->entry_count; i++) {
        if (!atlas->entries[i].live) {
            obs.all_live_after = false;
        }
    }
    const GpuSvgEntry* la = gpu_svg_atlas_find(atlas, A);
    const GpuSvgEntry* lb = gpu_svg_atlas_find(atlas, B);
    obs.a_resolved = la != NULL;
    obs.a_size_after = la != NULL ? la->size : 0;
    obs.b_resolved = lb != NULL;
    obs.b_size_after = lb != NULL ? lb->size : 0;
    obs.geometry_kept = la != NULL && lb != NULL && la->vector == geomA && lb->vector == geomB;

    // Space reclaimed: a fresh big placement the overflowed atlas refused now succeeds.
    const GpuSvgEntry* refit = gpu_svg_atlas_place(atlas, 3, 7, 360, 360, FlVectorClass_Monochrome, mono, geomA);
    obs.refit = refit != NULL;
    return obs;
}

UTEST(vector_image, atlas_compaction) {
    FlVectorImage geomA = { 0 }, geomB = { 0 }; // distinct geometry sentinels; bookkeeping never dereferences them

    GpuSvgAtlas r8 = { 0 };
    gpu_svg_atlas_init(&r8, s_arena, GpuSvgTexel_R8);
    CompactObs r = run_compaction_sequence(&r8, &geomA, &geomB);

    ASSERT_TRUE_MSG(r.overflowed, "re-keying a big cell across sizes overflows the append-only atlas");
    ASSERT_TRUE_MSG(r.orphans_before > 0, "orphaned cells accumulate before compaction");
    ASSERT_TRUE_MSG(r.count_before > 2, "the atlas holds the two live handles plus orphans before compaction");
    ASSERT_EQ_MSG((int)r.count_after, 2, "compaction keeps exactly the live cells (both handles, no orphans)");
    ASSERT_TRUE_MSG(r.all_live_after, "every surviving cell is live after compaction");
    ASSERT_TRUE_MSG(r.a_resolved && r.a_size_after == r.a_last_placed,
                    "the re-keyed handle is retained at its current size");
    ASSERT_TRUE_MSG(r.b_resolved && r.b_size_after == 16, "the sibling handle is retained at its current size");
    ASSERT_TRUE_MSG(r.geometry_kept, "compaction preserves each live cell's retained geometry pointer");
    ASSERT_TRUE_MSG(r.refit, "compaction reclaims orphaned space for a placement the full atlas refused");

    // Format-agnostic: an RGBA8 atlas driven through the identical sequence compacts to the same live set.
    GpuSvgAtlas rgba = { 0 };
    gpu_svg_atlas_init(&rgba, s_arena, GpuSvgTexel_Rgba8);
    CompactObs g = run_compaction_sequence(&rgba, &geomA, &geomB);
    ASSERT_TRUE_MSG(g.overflowed && g.all_live_after && g.a_resolved && g.b_resolved && g.refit && g.geometry_kept,
                    "RGBA8 compaction behaves identically to R8 (format-blind bookkeeping)");
    ASSERT_EQ_MSG((int)g.count_after, (int)r.count_after, "RGBA8 compacts to the same live-cell count as R8");
    // The two atlases differ in extent (R8 4096^2, RGBA8 2048^2), so A reaches a different size before each
    // overflows: EACH atlas retains A at its own last-placed size, not at a shared overflow point.
    ASSERT_TRUE_MSG(g.a_resolved && g.a_size_after == g.a_last_placed,
                    "RGBA8 retains the re-keyed handle at its current size (its own overflow point)");
}

UTEST(vector_image, translate_routing) {
    GpuAtlasCell cell = { .x = 10, .y = 20, .w = 34, .h = 26 }; // full padded extent
    FlRect dst = { 100.0f, 200.0f, 48.0f, 48.0f };
    FlColor tint = { 12, 34, 56, 255 };

    FlCmdImage mono = gpu_svg_image_cmd(5, cell, FlVectorClass_Monochrome, tint, dst);
    ASSERT_EQ_MSG((int)mono.mode, FlImageMode_AlphaCoverage, "monochrome -> alpha-coverage mode");
    ASSERT_EQ_MSG((int)mono.tint.r, 12, "monochrome carries the requested tint (recolor)");
    ASSERT_EQ_MSG((int)mono.tint.g, 34, "monochrome tint G");
    ASSERT_EQ_MSG((int)mono.source.kind, FlImageKind_Static, "static cached handle source");
    ASSERT_EQ_MSG((int)mono.source.handle, 5, "the icon's image handle");
    ASSERT_TRUE_MSG(mono.src.x == (f32)(10 + GPU_SVG_ATLAS_PAD), "src x insets the pad border");
    ASSERT_TRUE_MSG(mono.src.y == (f32)(20 + GPU_SVG_ATLAS_PAD), "src y insets the pad border");
    ASSERT_TRUE_MSG(mono.src.w == (f32)(34 - 2 * GPU_SVG_ATLAS_PAD), "src w drops both pads");
    ASSERT_TRUE_MSG(mono.src.h == (f32)(26 - 2 * GPU_SVG_ATLAS_PAD), "src h drops both pads");
    ASSERT_TRUE_MSG(mono.dst.x == 100.0f && mono.dst.w == 48.0f, "dst is the caller's rectangle");

    FlCmdImage color = gpu_svg_image_cmd(5, cell, FlVectorClass_MultiColor, tint, dst);
    ASSERT_EQ_MSG((int)color.mode, FlImageMode_Color, "multi-color -> RGBA color mode");
    ASSERT_EQ_MSG((int)color.tint.r, 255, "multi-color uses a white tint (no modulation)");
    ASSERT_EQ_MSG((int)color.tint.g, 255, "white tint G");
    ASSERT_EQ_MSG((int)color.tint.b, 255, "white tint B");
    ASSERT_EQ_MSG((int)color.tint.a, 255, "white tint A");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// background: controller state machine and FlBackground snapshot (ui/background.{c,h}).

// ImageHandle invalid is 0, so any nonzero value stands in for a real VFS handle.
#define H_A ((ImageHandle)7)

#define H_B ((ImageHandle)42)

UTEST(background, defaults) {
    FlBackgroundController c;
    background_controller_init(&c);

    ASSERT_TRUE_MSG(c.state.current == IMAGE_HANDLE_INVALID, "init: current should be invalid");
    ASSERT_TRUE_MSG(c.state.prev == IMAGE_HANDLE_INVALID, "init: prev should be invalid (no blend)");
    ASSERT_TRUE_MSG(c.state.alpha == 255, "init: alpha should be opaque");
    ASSERT_TRUE_MSG(c.state.clear_r == 14 && c.state.clear_g == 14 && c.state.clear_b == 16,
                    "init: clear color default");
}

UTEST(background, set_alpha_clear_color) {
    FlBackgroundController c;
    background_controller_init(&c);

    background_controller_set(&c, H_A);
    ASSERT_TRUE_MSG(c.state.current == H_A, "set: current updated");
    background_controller_set_alpha(&c, 128);
    ASSERT_TRUE_MSG(c.state.alpha == 128, "set_alpha: alpha updated");
    background_controller_set_clear_color(&c, 1, 2, 3);
    ASSERT_TRUE_MSG(c.state.clear_r == 1 && c.state.clear_g == 2 && c.state.clear_b == 3, "set_clear_color: updated");

    // set(valid) unconditionally resets alpha to opaque.
    background_controller_set(&c, H_B);
    ASSERT_TRUE_MSG(c.state.current == H_B, "set(valid): current updated");
    ASSERT_TRUE_MSG(c.state.alpha == 255, "set(valid): alpha reset to opaque");

    // set(INVALID) clears the image and resets alpha to opaque.
    background_controller_set_alpha(&c, 42);
    background_controller_set(&c, IMAGE_HANDLE_INVALID);
    ASSERT_TRUE_MSG(c.state.current == IMAGE_HANDLE_INVALID, "set(invalid): current cleared");
    ASSERT_TRUE_MSG(c.state.alpha == 255, "set(invalid): alpha reset to opaque");
}

// set_blend is stateless: it stores prev/current/t and nothing else. A plain set() drops the blend by
// clearing prev - that is how an app-side crossfade "completes" (set(next)).
UTEST(background, set_blend) {
    FlBackgroundController c;
    background_controller_init(&c);

    background_controller_set_blend(&c, H_A, H_B, 128);
    ASSERT_TRUE_MSG(c.state.prev == H_A, "set_blend: prev stored");
    ASSERT_TRUE_MSG(c.state.current == H_B, "set_blend: current stored");
    ASSERT_TRUE_MSG(c.state.t == 128, "set_blend: t stored");

    background_controller_set(&c, H_B);
    ASSERT_TRUE_MSG(c.state.prev == IMAGE_HANDLE_INVALID, "set after blend: prev cleared");
    ASSERT_TRUE_MSG(c.state.current == H_B, "set after blend: current retained");
}

UTEST(background, zero_initialised_handles_are_invalid) {
    const FlListItem item = { 0 };
    const FlBackground bg = { 0 };
    ASSERT_EQ_MSG(item.icon, FL_IMAGE_HANDLE_INVALID, "a zeroed list item has no icon");
    ASSERT_EQ_MSG(bg.current, FL_IMAGE_HANDLE_INVALID, "a zeroed background has no image");
    ASSERT_EQ_MSG(bg.prev, FL_IMAGE_HANDLE_INVALID, "a zeroed background has no crossfade");
}

UTEST(background, snapshot_handle_translation) {
    FlBackgroundController c;
    background_controller_init(&c);
    FlBackground snap = background_controller_snapshot(&c);
    ASSERT_TRUE_MSG(snap.current == FL_IMAGE_HANDLE_INVALID, "snapshot: no image -> invalid handle");
    ASSERT_TRUE_MSG(snap.prev == FL_IMAGE_HANDLE_INVALID, "snapshot: no crossfade -> prev invalid");
    ASSERT_TRUE_MSG(snap.alpha == 255, "snapshot: alpha carried");
    ASSERT_TRUE_MSG(snap.clear.r == 14 && snap.clear.g == 14 && snap.clear.b == 16 && snap.clear.a == 255,
                    "snapshot: clear color carried (opaque)");

    background_controller_set(&c, H_A);
    background_controller_set_alpha(&c, 77);
    snap = background_controller_snapshot(&c);
    ASSERT_TRUE_MSG(snap.current == (FlImageHandle)H_A, "snapshot: valid handle passes through numerically");
    ASSERT_TRUE_MSG(snap.prev == FL_IMAGE_HANDLE_INVALID, "snapshot: prev invalid without a blend (single image)");
    ASSERT_TRUE_MSG(snap.alpha == 77, "snapshot: alpha value carried");

    // A blend carries both handles; t reaches the backend normalized 0..1 (0 = full prev, 1 = full current).
    background_controller_set_blend(&c, H_A, H_B, 0);
    snap = background_controller_snapshot(&c);
    ASSERT_TRUE_MSG(snap.prev == (FlImageHandle)H_A, "snapshot: blend prev carried");
    ASSERT_TRUE_MSG(snap.current == (FlImageHandle)H_B, "snapshot: blend current carried");
    ASSERT_TRUE_MSG(snap.crossfade_t == 0.0f, "snapshot: t=0 -> full prev");

    background_controller_set_blend(&c, H_A, H_B, 255);
    snap = background_controller_snapshot(&c);
    ASSERT_TRUE_MSG(snap.crossfade_t == 1.0f, "snapshot: t=255 -> full current");

    // set_blend with an invalid prev degrades to the single-image path: no blend crosses the boundary, and
    // the current image rides its alpha (set_blend does not touch alpha, so the earlier value is preserved).
    background_controller_set_alpha(&c, 90);
    background_controller_set_blend(&c, IMAGE_HANDLE_INVALID, H_B, 128);
    snap = background_controller_snapshot(&c);
    ASSERT_TRUE_MSG(snap.prev == FL_IMAGE_HANDLE_INVALID, "snapshot: invalid prev -> single image, no blend");
    ASSERT_TRUE_MSG(snap.current == (FlImageHandle)H_B, "snapshot: single-image current carried");
    ASSERT_TRUE_MSG(snap.alpha == 90, "snapshot: single image rides its alpha");
}

UTEST(background, blend_t_round_trip) {
    // The snapshot normalizes the u8 blend to 0..1; (t*255 + 0.5) must recover every 0..255 value exactly.
    FlBackgroundController c;
    background_controller_init(&c);
    for (int t = 0; t <= 255; t++) {
        background_controller_set_blend(&c, H_A, H_B, (u8)t);
        FlBackground s = background_controller_snapshot(&c);
        u8 reconstructed = (u8)(s.crossfade_t * 255.0f + 0.5f);
        ASSERT_TRUE_MSG(reconstructed == (u8)t, "blend t round-trip not lossless");
    }
}

// The background family no-ops on the pre-init null context, so ui_background_set_blend before ui_create must
// be harmless. This case must run in the pre-init window, so it asserts the context really is null first.
UTEST(background, set_blend_before_init_is_noop) {
    render_fixture_teardown();
    ASSERT_TRUE_MSG(ui_get_ctx() == nullptr, "precondition: must run before any ui_create (s_ctx still null)");

    ui_background_set_blend(H_A, H_B, 128); // must not crash on the null context
}

// sr_command_split_rounded: the general 9-grid case only emits a corner command for a non-zero corner, so the
// arena advance (which the caller reads via arena_count) must equal corners-written + 5. Every written command
// is a copy of initial_command, so a valid slot always carries the base mask bit; an uninitialized slot on a
// fresh arena reads back as a zero mask.
UTEST(sr_command, split_rounded_count_matches_written_commands) {
    struct {
        float tl, tr, bl, br;
        int expected; // commands the caller must see; 0 corners never reaches the general case
        int corners;  // corner-flagged commands expected among them
        const char* what;
    } cases[] = {
        { 8, 8, 8, 0, 8, 3, "br zero" },      // single zero corner, each position
        { 8, 8, 0, 8, 8, 3, "bl zero" },      //
        { 8, 0, 8, 8, 8, 3, "tr zero" },      //
        { 0, 8, 8, 8, 8, 3, "tl zero" },      //
        { 8, 0, 0, 8, 7, 2, "diagonal" },     // two zero corners (general, not an edge special case)
        { 8, 6, 4, 2, 9, 4, "all positive" }, // non-uniform, no zeros: the previously-working 9 path
        { 8, 8, 8, 8, 7, 4, "uniform" },      // uniform fast path (7 commands), unchanged
        { 0, 0, 0, 0, 1, 0, "no rounding" },  // single passthrough command, unchanged
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        FlArena* arena = arena_new();
        fl_arena_align(arena, 16);
        RenderCommand* start = (RenderCommand*)fl_arena_current_pos(arena);

        RenderCommand initial = { 0 };
        initial.mask = RenderMask_DrawQuad;
        initial.aabb = f32x4_new(0.0f, 0.0f, 100.0f, 100.0f);

        f32x4 corners = f32x4_new(cases[i].tl, cases[i].tr, cases[i].bl, cases[i].br);
        sr_command_split_rounded(arena, &initial, corners);

        int count = arena_count(arena, start, RenderCommand);
        ASSERT_EQ_MSG(count, cases[i].expected, cases[i].what);

        int corner_cmds = 0;
        u32 seen_corners = 0;
        for (int c = 0; c < count; ++c) {
            // A slot the split intended to write is a copy of initial_command, so it keeps the base mask
            // bit. A slot left uninitialized (the bug) reads back as zero on a fresh arena.
            ASSERT_TRUE_MSG((start[c].mask & RenderMask_DrawQuad) != 0, cases[i].what);
            if (start[c].mask & RenderMask_Corners) {
                corner_cmds++;
                // The selector is a value in the corners field, not a bitmask: a split names each corner at
                // most once, and every corner command names exactly one.
                RenderCorner corner = render_mask_corner_of(start[c].mask);
                ASSERT_TRUE_MSG(corner >= RenderCorner_TopLeft && corner <= RenderCorner_BottomRight, cases[i].what);
                ASSERT_TRUE_MSG((seen_corners & (1u << corner)) == 0, cases[i].what);
                seen_corners |= 1u << corner;
            }
        }
        ASSERT_EQ_MSG(corner_cmds, cases[i].corners, cases[i].what);

        fl_arena_destroy(arena);
    }
}

// The render cases reach the 3-channel deinterleave and keep_alpha=false planar paths; these cover the
// remaining branches: the keep_alpha=true alpha plane and the XRGB format.
UTEST(sr_image_convert, deinterleave_pixels_all_branches) {
    // 20 pixels = one 16-wide SIMD block plus a 4-pixel scalar tail, so both code paths run.
    enum { COUNT = 20 };
    u8 src4[COUNT * 4];
    u8 src3[COUNT * 3];
    for (int i = 0; i < COUNT; i++) {
        src4[i * 4 + 0] = (u8)(i + 1);   // R
        src4[i * 4 + 1] = (u8)(i + 50);  // G
        src4[i * 4 + 2] = (u8)(i + 100); // B
        src4[i * 4 + 3] = (u8)(i + 150); // A
        src3[i * 3 + 0] = (u8)(i + 2);
        src3[i * 3 + 1] = (u8)(i + 60);
        src3[i * 3 + 2] = (u8)(i + 120);
    }

    u8 r[COUNT], g[COUNT], b[COUNT], a[COUNT];

    // 4-channel, keep_alpha=true: all four planes written.
    for (int i = 0; i < COUNT; i++) {
        a[i] = 0xEE;
    }
    sr_deinterleave_pixels(src4, r, g, b, a, COUNT, 4, true);
    for (int i = 0; i < COUNT; i++) {
        ASSERT_EQ((int)r[i], i + 1);
        ASSERT_EQ((int)g[i], i + 50);
        ASSERT_EQ((int)b[i], i + 100);
        ASSERT_EQ((int)a[i], i + 150);
    }

    // 4-channel, keep_alpha=false: RGB written, alpha buffer left untouched.
    for (int i = 0; i < COUNT; i++) {
        a[i] = 0xEE;
    }
    sr_deinterleave_pixels(src4, r, g, b, a, COUNT, 4, false);
    for (int i = 0; i < COUNT; i++) {
        ASSERT_EQ((int)r[i], i + 1);
        ASSERT_EQ((int)g[i], i + 50);
        ASSERT_EQ((int)b[i], i + 100);
        ASSERT_EQ((int)a[i], 0xEE);
    }

    // 3-channel source: RGB deinterleave path.
    sr_deinterleave_pixels(src3, r, g, b, nullptr, COUNT, 3, false);
    for (int i = 0; i < COUNT; i++) {
        ASSERT_EQ((int)r[i], i + 2);
        ASSERT_EQ((int)g[i], i + 60);
        ASSERT_EQ((int)b[i], i + 120);
    }
}

UTEST(sr_image_convert, convert_to_planar_xrgb_and_alpha) {
    FlArena* arena = arena_new();

    // 3x2 interleaved source; pixel (x,y) gets distinct per-channel bytes to catch channel swaps.
    const int w = 3, h = 2;
    u8 pixels[3 * 2 * 4];
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            u8* p = pixels + (y * w + x) * 4;
            p[0] = (u8)(y * 10 + x + 1);  // R
            p[1] = (u8)(y * 10 + x + 31); // G
            p[2] = (u8)(y * 10 + x + 61); // B
            p[3] = (u8)(y * 10 + x + 91); // A / unused X
        }
    }

    ImageData src = {
        .data = pixels,
        .width = w,
        .height = h,
        .padded_width = w,
        .padded_height = h,
        .stride = w * 4,
        .channels = 4,
        .format = FlImageFormat_XRGB,
    };

    // XRGB drops the X byte (keep_alpha=false) -> 3 planes, with bilinear padding (+1 w/h).
    ImageData* xrgb = sr_convert_to_planar(&src, false, arena);
    ASSERT_TRUE(xrgb != nullptr);
    ASSERT_EQ(xrgb->channels, 3);
    ASSERT_EQ((int)xrgb->format, (int)FlImageFormat_Deinterleaved);
    ASSERT_EQ(xrgb->padded_width, w + 1);
    ASSERT_EQ(xrgb->padded_height, h + 1);

    int plane = xrgb->padded_width * xrgb->padded_height;
    const u8* rp = xrgb->data;
    const u8* gp = xrgb->data + plane;
    const u8* bp = xrgb->data + plane * 2;
    int s = xrgb->stride; // == padded_width

    // Logical pixel (0,0) and (2,1) deinterleaved correctly.
    ASSERT_EQ((int)rp[0], 1);
    ASSERT_EQ((int)gp[0], 31);
    ASSERT_EQ((int)bp[0], 61);
    ASSERT_EQ((int)rp[1 * s + 2], (int)(u8)(13)); // R of (2,1) = 1*10+2+1
    // Last column padding duplicates the prior column on row 0.
    ASSERT_EQ((int)rp[w], (int)rp[w - 1]);
    // Last (padding) row duplicates the previous row.
    ASSERT_EQ((int)rp[h * s], (int)rp[(h - 1) * s]);

    // Same source as RGBA with keep_alpha=true -> 4 planes including alpha.
    src.format = FlImageFormat_RGBA;
    ImageData* rgba = sr_convert_to_planar(&src, true, arena);
    ASSERT_TRUE(rgba != nullptr);
    ASSERT_EQ(rgba->channels, 4);
    const u8* ap = rgba->data + (rgba->padded_width * rgba->padded_height) * 3;
    ASSERT_EQ((int)ap[0], 91); // A of (0,0)

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// sr_image_convert: a zero extent has no last pixel or row to duplicate, so the edge padding would read
// before the plane. The conversion must reject it rather than produce that read.

UTEST(sr_image_convert, convert_to_planar_rejects_a_zero_extent) {
    FlArena* arena = arena_new();

    u8 pixel[4] = { 1, 2, 3, 4 };
    ImageData src = {
        .data = pixel,
        .width = 1,
        .height = 1,
        .padded_width = 1,
        .padded_height = 1,
        .stride = 4,
        .channels = 4,
        .format = FlImageFormat_RGBA,
    };

    ASSERT_TRUE_MSG(sr_convert_to_planar(&src, false, arena) != nullptr, "a 1x1 source still converts");

    src.width = 0;
    ASSERT_TRUE_MSG(sr_convert_to_planar(&src, false, arena) == nullptr, "a zero width is rejected");

    src.width = 1;
    src.height = 0;
    ASSERT_TRUE_MSG(sr_convert_to_planar(&src, false, arena) == nullptr, "a zero height is rejected");

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// prepare_image: stub backend records how prepare_image was called.

typedef struct PrepareStats {
    _Atomic int call_count;
    u64 thread;         // thread that made the last call
    bool decoded_valid; // decoded != null in the last call
    bool during_pump;   // last call happened inside image_system_begin_frame
} PrepareStats;

// Raised around image_system_begin_frame so the stub can tell a pump call from a job call.
static _Atomic bool s_in_pump = false;

static void stub_prepare_image(void* user_data, FlImageLoadRequestId request_id, const FlImageDecodeResult* decoded,
                               FlArena* scratch, const FlImageLoadRequest* request) {
    (void)request_id;
    (void)scratch;
    (void)request;

    PrepareStats* stats = (PrepareStats*)user_data;
    stats->thread = os_thread_get_current_id();
    stats->decoded_valid = (decoded != nullptr);
    stats->during_pump = atomic_load(&s_in_pump);
    atomic_fetch_add(&stats->call_count, 1);
}

// A tiny valid image so image_decode succeeds (SVG keeps it embeddable as text).
static const char s_svg[] = "<svg width=\"8\" height=\"8\"><rect width=\"8\" height=\"8\" fill=\"#ff0000\"/></svg>";

#define PREPARE_POLL_TIMEOUT_MS 5000

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// prepare_image_on_job unset - the call is deferred to the begin-frame pump on this (main/render) thread.

UTEST(prepare_image, deferred_to_begin_frame_pump) {
    PrepareStats stats = { 0 };
    FlRenderBackend backend = {
        .user_data = &stats,
        .prepare_image = stub_prepare_image,
        .prepare_image_on_job = false,
    };
    ImageSystem* sys = image_system_create(s_arena, &backend);
    ASSERT_TRUE(sys != nullptr);

    FlImageLoadRequestId id = image_system_load_from_memory(sys, s_svg, sizeof(s_svg) - 1, nullptr, nullptr);
    ASSERT_NE(id, (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);

    // Decode runs in the job worker; once complete the request must be queued - not invoked.
    int waited = 0;
    while (!image_system_prepare_pending(sys, id) && waited < PREPARE_POLL_TIMEOUT_MS) {
        sleep_ms(1);
        waited++;
    }
    ASSERT_TRUE(image_system_prepare_pending(sys, id));
    ASSERT_EQ(atomic_load(&stats.call_count), 0); // never invoked from job context

    // The pump makes the call, exactly once, on this thread.
    atomic_store(&s_in_pump, true);
    image_system_begin_frame(sys);
    atomic_store(&s_in_pump, false);

    ASSERT_EQ(atomic_load(&stats.call_count), 1);
    ASSERT_EQ(stats.thread, os_thread_get_current_id());
    ASSERT_TRUE(stats.during_pump);
    ASSERT_TRUE(stats.decoded_valid);
    ASSERT_FALSE(image_system_prepare_pending(sys, id));

    // Further pumps must not re-invoke for the same decode.
    image_system_begin_frame(sys);
    image_system_begin_frame(sys);
    ASSERT_EQ(atomic_load(&stats.call_count), 1);

    image_system_destroy(sys);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// prepare_image_on_job set - the call happens in the job worker thread (SW shape, unchanged).

UTEST(prepare_image, job_context_when_flag_set) {
    PrepareStats stats = { 0 };
    FlRenderBackend backend = {
        .user_data = &stats,
        .prepare_image = stub_prepare_image,
        .prepare_image_on_job = true,
    };
    ImageSystem* sys = image_system_create(s_arena, &backend);
    ASSERT_TRUE(sys != nullptr);

    FlImageLoadRequestId id = image_system_load_from_memory(sys, s_svg, sizeof(s_svg) - 1, nullptr, nullptr);
    ASSERT_NE(id, (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);

    // No pumping: the decode job itself must invoke the seam.
    int waited = 0;
    while (atomic_load(&stats.call_count) == 0 && waited < PREPARE_POLL_TIMEOUT_MS) {
        sleep_ms(1);
        waited++;
    }
    ASSERT_EQ(atomic_load(&stats.call_count), 1);
    ASSERT_NE(stats.thread, os_thread_get_current_id());
    ASSERT_FALSE(stats.during_pump);
    ASSERT_TRUE(stats.decoded_valid);
    ASSERT_FALSE(image_system_prepare_pending(sys, id));

    image_system_destroy(sys);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A memory load whose decode succeeds but that no backend acknowledges ends failed, not active-pending: either
// there is no backend, or the backend's prepare_image never realizes the image into the registry.

static bool wait_synthetic_settled(ImageSystem* sys, FlImageLoadRequestId id) {
    for (int waited = 0; waited < PREPARE_POLL_TIMEOUT_MS; waited++) {
        if (!image_system_load_is_active_synthetic(sys, id)) {
            return true;
        }
        sleep_ms(1);
    }
    return false;
}

UTEST(prepare_image, memory_load_without_backend_fails) {
    ImageSystem* sys = image_system_create(s_arena, nullptr);
    ASSERT_TRUE(sys != nullptr);

    FlImageLoadRequestId id = image_system_load_from_memory(sys, s_svg, sizeof(s_svg) - 1, nullptr, nullptr);
    ASSERT_NE(id, (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);

    ASSERT_TRUE_MSG(wait_synthetic_settled(sys, id), "a load nothing can acknowledge must leave the active set");
    ASSERT_TRUE_MSG(image_system_load_failed(sys, id), "an unacknowledged memory load must be marked failed");

    image_system_destroy(sys);
}

UTEST(prepare_image, memory_load_unacknowledged_by_job_backend_fails) {
    PrepareStats stats = { 0 };
    FlRenderBackend backend = {
        .user_data = &stats,
        .prepare_image = stub_prepare_image,
        .prepare_image_on_job = true,
    };
    ImageSystem* sys = image_system_create(s_arena, &backend);
    ASSERT_TRUE(sys != nullptr);

    FlImageLoadRequestId id = image_system_load_from_memory(sys, s_svg, sizeof(s_svg) - 1, nullptr, nullptr);
    ASSERT_NE(id, (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);

    ASSERT_TRUE_MSG(wait_synthetic_settled(sys, id), "the load must leave the active set once the backend returned");
    ASSERT_EQ(atomic_load(&stats.call_count), 1);
    ASSERT_TRUE_MSG(image_system_load_failed(sys, id), "a backend that never acks must leave the load failed");

    image_system_destroy(sys);
}

UTEST(prepare_image, memory_load_stays_active_until_deferred_pump) {
    PrepareStats stats = { 0 };
    FlRenderBackend backend = {
        .user_data = &stats,
        .prepare_image = stub_prepare_image,
        .prepare_image_on_job = false,
    };
    ImageSystem* sys = image_system_create(s_arena, &backend);
    ASSERT_TRUE(sys != nullptr);

    FlImageLoadRequestId id = image_system_load_from_memory(sys, s_svg, sizeof(s_svg) - 1, nullptr, nullptr);
    ASSERT_NE(id, (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);

    for (int waited = 0; !image_system_prepare_pending(sys, id) && waited < PREPARE_POLL_TIMEOUT_MS; waited++) {
        sleep_ms(1);
    }
    ASSERT_TRUE(image_system_prepare_pending(sys, id));
    ASSERT_TRUE_MSG(image_system_load_is_active_synthetic(sys, id), "a queued prepare is still awaiting its ack");
    ASSERT_FALSE(image_system_load_failed(sys, id));

    image_system_begin_frame(sys);

    ASSERT_FALSE_MSG(image_system_load_is_active_synthetic(sys, id), "the pump settles the load");
    ASSERT_TRUE_MSG(image_system_load_failed(sys, id), "a pumped prepare that never acks leaves the load failed");

    image_system_destroy(sys);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// image_system_load must copy the caller's path FlString before returning: the worker-thread VFS callback
// reads the path after the call returns, but ui.h's contract lets the caller free/reuse its buffer
// immediately ("path need only live for this call"). The path is built in a mutable stack buffer so
// is_static is 0 and the copy path is exercised.

typedef struct PathCaptureStats {
    _Atomic int call_count;
    char path[256];
    int path_len;
} PathCaptureStats;

static void capture_path_prepare_image(void* user_data, FlImageLoadRequestId request_id,
                                       const FlImageDecodeResult* decoded, FlArena* scratch,
                                       const FlImageLoadRequest* request) {
    (void)request_id;
    (void)decoded;
    (void)scratch;
    PathCaptureStats* stats = (PathCaptureStats*)user_data;
    int n = (int)request->relative_path.length;
    if (n > (int)sizeof(stats->path) - 1) {
        n = (int)sizeof(stats->path) - 1;
    }
    memcpy(stats->path, request->relative_path.data, (size_t)n);
    stats->path[n] = '\0';
    stats->path_len = n;
    atomic_fetch_add(&stats->call_count, 1);
}

UTEST(image_load, copies_caller_path_before_async_read) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all(); // drain the async mount before reading
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    PathCaptureStats stats = { 0 };
    FlRenderBackend backend = {
        .user_data = &stats,
        .prepare_image = capture_path_prepare_image,
        .prepare_image_on_job = true, // the call lands in the worker thread - no begin-frame pump needed
    };
    ImageSystem* sys = image_system_create(s_arena, &backend);
    ASSERT_TRUE(sys != nullptr);

    char path_buf[] = STUB_VFS_FILE;
    FlString path = string_from_cstr_len(path_buf, sizeof(path_buf) - 1);

    FlImageLoadRequestId id
        = image_system_load(sys, mount.mount, path, nullptr, nullptr, nullptr, FL_IMAGE_LOAD_REQUEST_ID_INVALID);
    ASSERT_NE(id, (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);

    // Scribble over the caller's buffer right away, exactly as the contract permits.
    memset(path_buf, 'X', sizeof(path_buf) - 1);

    int waited = 0;
    while (atomic_load(&stats.call_count) == 0 && waited < PREPARE_POLL_TIMEOUT_MS) {
        sleep_ms(1);
        waited++;
    }
    ASSERT_EQ(atomic_load(&stats.call_count), 1);
    ASSERT_EQ(stats.path_len, (int)sizeof(STUB_VFS_FILE) - 1);
    ASSERT_EQ_MSG(memcmp(stats.path, STUB_VFS_FILE, sizeof(STUB_VFS_FILE) - 1), 0,
                  "worker must read the owned path copy, not the caller's overwritten buffer");

    image_system_destroy(sys);
    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Every image load takes a JobContext pool slot, and nothing in the job system recycles it - the slot only
// comes back if image_system releases it. The pool's free list is LIFO, so a slot parked on it before a load
// reappears from the next allocation exactly when the load gave it back.
//
// The pool is process-global and shared with the glyph-atlas jobs, so a single round can be perturbed by
// unrelated traffic. Rounds are counted instead: a leak misses every round, interference misses a few.

#define CONTEXT_RECYCLE_ROUNDS 8

static int count_recycled_context_slots(ImageSystem* sys, FlVfsMount* mount, FlString path) {
    int recycled = 0;

    for (int i = 0; i < CONTEXT_RECYCLE_ROUNDS; i++) {
        void* parked = fl_jobs_alloc_context(sizeof(void*));
        if (!parked) {
            return -1;
        }
        jobs_context_free(parked);

        FlImageLoadRequestId id
            = image_system_load(sys, mount, path, nullptr, nullptr, nullptr, FL_IMAGE_LOAD_REQUEST_ID_INVALID);
        if (id == (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID) {
            return -1;
        }
        vfs_wait_all();

        void* reclaimed = fl_jobs_alloc_context(sizeof(void*));
        if (reclaimed == parked) {
            recycled++;
        }
        jobs_context_free(reclaimed);
        fl_vfs_close(id);
    }

    return recycled;
}

UTEST(image_load, load_recycles_job_context_slot) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();

    FlRenderBackend backend = { 0 };
    ImageSystem* sys = image_system_create(s_arena, &backend);
    ASSERT_TRUE(sys != nullptr);

    int completed = count_recycled_context_slots(sys, mount.mount, FL_S(STUB_VFS_FILE));
    ASSERT_TRUE_MSG(completed >= CONTEXT_RECYCLE_ROUNDS - 2, "a completed load must return its context slot");

    // A missing file fails inside the read job, not at prepare time, so the slot comes back only because the
    // VFS notifies the callback on failure.
    int missing = count_recycled_context_slots(sys, mount.mount, FL_S("no_such_file.png"));
    ASSERT_TRUE_MSG(missing >= CONTEXT_RECYCLE_ROUNDS - 2, "a failed read must return its context slot");

    image_system_destroy(sys);
    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Teardown: image_system_destroy must not return while a worker can still reach the backend, and jobs that have
// not reached the backend yet must skip it once teardown has begun.

typedef struct GatedBackend {
    _Atomic bool gate_open; // workers block in prepare_image until this is set
    _Atomic int entered;    // prepare_image calls that started
    _Atomic int finished;   // prepare_image calls that returned
} GatedBackend;

static void gated_prepare_image(void* user_data, FlImageLoadRequestId request_id, const FlImageDecodeResult* decoded,
                                FlArena* scratch, const FlImageLoadRequest* request) {
    (void)request_id;
    (void)decoded;
    (void)scratch;
    (void)request;

    GatedBackend* b = (GatedBackend*)user_data;
    atomic_fetch_add(&b->entered, 1);
    while (!atomic_load(&b->gate_open)) {
        sleep_ms(1);
    }
    atomic_fetch_add(&b->finished, 1);
}

// Runs image_system_destroy (or only image_system_close) off the test thread so the test can observe it
// blocking.
typedef struct DestroyThread {
    ImageSystem* sys;
    bool close_only;
    _Atomic bool done;
} DestroyThread;

static void* destroy_thread_main(void* user_data) {
    DestroyThread* d = (DestroyThread*)user_data;
    if (d->close_only) {
        image_system_close(d->sys);
    } else {
        image_system_destroy(d->sys);
    }
    atomic_store(&d->done, true);
    return nullptr;
}

static bool wait_for_entered(GatedBackend* b, int count) {
    int waited = 0;
    while (atomic_load(&b->entered) < count && waited < PREPARE_POLL_TIMEOUT_MS) {
        sleep_ms(1);
        waited++;
    }
    return atomic_load(&b->entered) >= count;
}

#define DESTROY_BLOCKED_PROBE_MS 100

static const char s_not_an_image[] = "this is not an image";

UTEST(image_teardown, destroy_waits_for_worker_in_prepare) {
    GatedBackend b = { 0 };
    FlRenderBackend backend = { .user_data = &b, .prepare_image = gated_prepare_image, .prepare_image_on_job = true };
    ImageSystem* sys = image_system_create(s_arena, &backend);

    ASSERT_NE(image_system_load_from_memory(sys, s_svg, sizeof(s_svg) - 1, nullptr, nullptr),
              (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);
    ASSERT_TRUE(wait_for_entered(&b, 1));

    DestroyThread d = { .sys = sys };
    Thread thread;
    ASSERT_TRUE(os_thread_create(&thread, destroy_thread_main, &d));

    sleep_ms(DESTROY_BLOCKED_PROBE_MS);
    const bool done_while_gated = atomic_load(&d.done);
    const int finished_while_gated = atomic_load(&b.finished);

    atomic_store(&b.gate_open, true);
    os_thread_join(&thread);
    ASSERT_FALSE(done_while_gated);
    ASSERT_EQ(finished_while_gated, 0);
    ASSERT_TRUE(atomic_load(&d.done));
    ASSERT_EQ(atomic_load(&b.finished), 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A burst well past the old 64-entry list: every load is tracked, and jobs still queued when teardown begins
// skip the backend instead of calling one the caller is about to free.

#define TEARDOWN_BURST 200

UTEST(image_teardown, burst_past_pending_capacity_is_tracked_and_drained) {
    GatedBackend b = { 0 };
    FlRenderBackend backend = { .user_data = &b, .prepare_image = gated_prepare_image, .prepare_image_on_job = true };
    ImageSystem* sys = image_system_create(s_arena, &backend);

    for (int i = 0; i < TEARDOWN_BURST; i++) {
        ASSERT_NE(image_system_load_from_memory(sys, s_svg, sizeof(s_svg) - 1, nullptr, nullptr),
                  (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);
    }
    ASSERT_TRUE(wait_for_entered(&b, 1));
    ASSERT_EQ(image_system_pending_job_count(sys), (u32)TEARDOWN_BURST);

    DestroyThread d = { .sys = sys };
    Thread thread;
    ASSERT_TRUE(os_thread_create(&thread, destroy_thread_main, &d));
    sleep_ms(DESTROY_BLOCKED_PROBE_MS);
    const bool done_while_gated = atomic_load(&d.done);

    atomic_store(&b.gate_open, true);
    os_thread_join(&thread);
    ASSERT_FALSE(done_while_gated);
    ASSERT_TRUE(atomic_load(&d.done));

    // Only the workers already inside the backend ran; the rest of the burst was skipped, and nothing
    // reaches the backend after destroy returned.
    int entered = atomic_load(&b.entered);
    ASSERT_LT(entered, TEARDOWN_BURST);
    ASSERT_EQ(atomic_load(&b.finished), entered);
    sleep_ms(DESTROY_BLOCKED_PROBE_MS);
    ASSERT_EQ(atomic_load(&b.entered), entered);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A failed decode still reaches the backend and then records its failure; teardown waits for that record so the
// failed-load state it writes is not destroyed under it.

UTEST(image_teardown, close_waits_for_failed_decode_in_prepare) {
    GatedBackend b = { 0 };
    FlRenderBackend backend = { .user_data = &b, .prepare_image = gated_prepare_image, .prepare_image_on_job = true };
    ImageSystem* sys = image_system_create(s_arena, &backend);

    FlImageLoadRequestId bad
        = image_system_load_from_memory(sys, s_not_an_image, sizeof(s_not_an_image), nullptr, nullptr);
    ASSERT_NE(bad, (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);
    ASSERT_TRUE(wait_for_entered(&b, 1));
    ASSERT_FALSE(image_system_load_failed(sys, bad));

    DestroyThread d = { .sys = sys, .close_only = true };
    Thread thread;
    ASSERT_TRUE(os_thread_create(&thread, destroy_thread_main, &d));
    sleep_ms(DESTROY_BLOCKED_PROBE_MS);
    const bool done_while_gated = atomic_load(&d.done);

    atomic_store(&b.gate_open, true);
    os_thread_join(&thread);
    ASSERT_FALSE(done_while_gated);
    ASSERT_TRUE(atomic_load(&d.done));
    ASSERT_TRUE(image_system_load_failed(sys, bad));

    image_system_destroy(sys);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A failed decode on a deferred backend reaches the pump queue with a null result; teardown discards it, as it
// does a successful one, without ever calling the backend.

UTEST(image_teardown, deferred_prepares_discarded_without_backend_call) {
    PrepareStats stats = { 0 };
    FlRenderBackend backend
        = { .user_data = &stats, .prepare_image = stub_prepare_image, .prepare_image_on_job = false };
    ImageSystem* sys = image_system_create(s_arena, &backend);

    FlImageLoadRequestId ok = image_system_load_from_memory(sys, s_svg, sizeof(s_svg) - 1, nullptr, nullptr);
    FlImageLoadRequestId bad
        = image_system_load_from_memory(sys, s_not_an_image, sizeof(s_not_an_image), nullptr, nullptr);
    ASSERT_NE(ok, (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);
    ASSERT_NE(bad, (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);

    int waited = 0;
    while ((!image_system_prepare_pending(sys, ok) || !image_system_prepare_pending(sys, bad))
           && waited < PREPARE_POLL_TIMEOUT_MS) {
        sleep_ms(1);
        waited++;
    }
    ASSERT_TRUE(image_system_prepare_pending(sys, ok));
    ASSERT_TRUE(image_system_prepare_pending(sys, bad));

    image_system_destroy(sys);
    ASSERT_EQ(atomic_load(&stats.call_count), 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// VFS read callbacks are producers too: teardown waits for one held in the backend, and refuses new VFS loads.

UTEST(image_teardown, destroy_waits_for_vfs_load_in_prepare) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount.mount));

    GatedBackend b = { 0 };
    FlRenderBackend backend = { .user_data = &b, .prepare_image = gated_prepare_image, .prepare_image_on_job = true };
    ImageSystem* sys = image_system_create(s_arena, &backend);

    FlImageLoadRequestId id = image_system_load(sys, mount.mount, FL_S(STUB_VFS_FILE), nullptr, nullptr, nullptr,
                                                FL_IMAGE_LOAD_REQUEST_ID_INVALID);
    ASSERT_NE(id, (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);
    ASSERT_TRUE(wait_for_entered(&b, 1));

    DestroyThread d = { .sys = sys, .close_only = true };
    Thread thread;
    ASSERT_TRUE(os_thread_create(&thread, destroy_thread_main, &d));
    sleep_ms(DESTROY_BLOCKED_PROBE_MS);
    const bool done_while_gated = atomic_load(&d.done);
    const int finished_while_gated = atomic_load(&b.finished);

    atomic_store(&b.gate_open, true);
    os_thread_join(&thread);
    ASSERT_FALSE(done_while_gated);
    ASSERT_EQ(finished_while_gated, 0);
    ASSERT_TRUE(atomic_load(&d.done));
    ASSERT_EQ(atomic_load(&b.finished), 1);

    ASSERT_EQ(image_system_load(sys, mount.mount, FL_S(STUB_VFS_FILE), nullptr, nullptr, nullptr,
                                FL_IMAGE_LOAD_REQUEST_ID_INVALID),
              (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);

    image_system_destroy(sys);
    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Once teardown has begun, new in-memory loads are refused.

UTEST(image_teardown, loads_refused_after_close) {
    PrepareStats stats = { 0 };
    FlRenderBackend backend
        = { .user_data = &stats, .prepare_image = stub_prepare_image, .prepare_image_on_job = true };
    ImageSystem* sys = image_system_create(s_arena, &backend);

    image_system_close(sys);
    ASSERT_EQ(image_system_load_from_memory(sys, s_svg, sizeof(s_svg) - 1, nullptr, nullptr),
              (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);
    ASSERT_EQ(atomic_load(&stats.call_count), 0);

    image_system_destroy(sys);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Host teardown: the host destroys its surfaces, shared tier and backend, so it must drain image workers before
// any of that. The surface teardown is the observable first step of the backend side.

static GatedBackend s_host_gate;
static _Atomic bool s_host_surface_destroyed;
static void (*s_host_real_destroy_surface)(void*, FlSurface*);

static void host_gated_prepare_image(void* user_data, FlImageLoadRequestId request_id,
                                     const FlImageDecodeResult* decoded, FlArena* scratch,
                                     const FlImageLoadRequest* request) {
    (void)user_data;
    gated_prepare_image(&s_host_gate, request_id, decoded, scratch, request);
}

static void host_recording_destroy_surface(void* user_data, FlSurface* surface) {
    atomic_store(&s_host_surface_destroyed, true);
    s_host_real_destroy_surface(user_data, surface);
}

typedef struct HostDestroyThread {
    FlHost* host;
    _Atomic bool done;
} HostDestroyThread;

static void* host_destroy_thread_main(void* user_data) {
    HostDestroyThread* d = (HostDestroyThread*)user_data;
    host_destroy(d->host);
    atomic_store(&d->done, true);
    return nullptr;
}

UTEST(image_teardown, host_destroy_drains_image_worker_before_backend_teardown) {
    s_host_gate = (GatedBackend) { 0 };
    atomic_store(&s_host_surface_destroyed, false);

    FlRenderBackend backend = create_software_renderer(s_arena, nullptr, FontRenderMode_Grayscale);
    backend.prepare_image = host_gated_prepare_image;
    backend.prepare_image_on_job = true;
    s_host_real_destroy_surface = backend.destroy_surface;
    backend.destroy_surface = host_recording_destroy_surface;

    FlHostSettings settings = { 0 };
    FlHost* host = host_create(s_arena, &settings, &backend);
    ASSERT_TRUE(host != nullptr);
    ASSERT_TRUE(host_surface_create(host, 64, 64) != nullptr);

    FlImageLoadOptions options = { 0 };
    ASSERT_NE(image_load_from_memory(s_svg, sizeof(s_svg) - 1, options), (ImageHandle)IMAGE_HANDLE_INVALID);
    ASSERT_TRUE(wait_for_entered(&s_host_gate, 1));

    HostDestroyThread d = { .host = host };
    Thread thread;
    ASSERT_TRUE(os_thread_create(&thread, host_destroy_thread_main, &d));
    sleep_ms(DESTROY_BLOCKED_PROBE_MS);
    const bool done_while_gated = atomic_load(&d.done);
    const bool surface_destroyed_while_gated = atomic_load(&s_host_surface_destroyed);

    atomic_store(&s_host_gate.gate_open, true);
    os_thread_join(&thread);
    ASSERT_FALSE(done_while_gated);
    ASSERT_FALSE_MSG(surface_destroyed_while_gated, "backend torn down while an image worker was running");
    ASSERT_TRUE(atomic_load(&d.done));
    ASSERT_TRUE(atomic_load(&s_host_surface_destroyed));
    ASSERT_EQ(atomic_load(&s_host_gate.finished), 1);

    // The adopted backend stays the caller's; it is released only after the host is gone.
    backend.destroy(backend.user_data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.smoke - the full pipeline end-to-end without any display/SDL/plugins.

UTEST(render, smoke) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // Drive the layout at a small logical size; the software renderer itself is a fixed 1920x1080
    // offscreen RGB24 buffer (built by the fixture above).
    const int layout_w = 256;
    const int layout_h = 256;
    ctx->layout_width = (float)layout_w;
    ctx->layout_height = (float)layout_h;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)layout_w, .height = (float)layout_h });

    // A distinctive red so we can tell it apart from the (dark) clear color.
    const FlColor rect_color = ui_color(200, 40, 50, 255);

    // ui_update_end() rasterizes into the offscreen buffer.
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = rect_color) {}
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(ctx->shared->renderer.read_pixels != nullptr, "backend has no pixel-readback capability");
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "render buffer data is null");
    ASSERT_TRUE_MSG(buf.width > 0 && buf.height > 0, "render buffer has zero size");

    // Sample a pixel that should be well inside the rendered rectangle.
    const int px = 16;
    const int py = 16;
    ASSERT_TRUE_MSG(px < (int)buf.width && py < (int)buf.height && px < layout_w && py < layout_h,
                    "sample point out of range");

    const u8* pixels = (const u8*)buf.pixels;
    const size_t offset = (size_t)py * (size_t)buf.stride + (size_t)px * 3u;
    const u8 r = pixels[offset + 0];
    const u8 g = pixels[offset + 1];
    const u8 b = pixels[offset + 2];

    ASSERT_TRUE_MSG(r > 100, "expected a strong red channel inside the rendered rectangle");
    ASSERT_TRUE_MSG(r > g && r > b, "expected red to dominate green and blue");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.layout_primitives - drives a frame through the layout primitives directly (fl_layout_start /
// fl_layout_start_auto / fl_layout_configure / fl_layout_end) rather than the ui_layout_area macro.

UTEST(render, layout_primitives) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const int layout_w = 256;
    const int layout_h = 256;
    ctx->layout_width = (float)layout_w;
    ctx->layout_height = (float)layout_h;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)layout_w, .height = (float)layout_h });

    const FlColor outer_color = ui_color(40, 40, 200, 255); // blue backdrop
    const FlColor child_color = ui_color(40, 200, 60, 255); // green nested child, anchored top-left

    ui_update_begin(ctx);
    fl_layout_start(ID("layout_primitives_outer"));
    fl_layout_configure(
        (FlLayoutConfig) { .sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = outer_color });
    fl_layout_start_auto();
    fl_layout_configure((FlLayoutConfig) { .sizing = { sizing_fixed(120), sizing_fixed(120) }, .color = child_color });
    fl_layout_end();
    fl_layout_end();
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "render buffer data is null");

    // Sample inside the nested 120x120 child (top-left): green dominates, proving the nested open/close took.
    const int px = 16, py = 16;
    const u8* pixels = (const u8*)buf.pixels;
    const size_t offset = (size_t)py * (size_t)buf.stride + (size_t)px * 3u;
    const u8 r = pixels[offset + 0];
    const u8 g = pixels[offset + 1];
    const u8 b = pixels[offset + 2];
    ASSERT_TRUE_MSG(g > 100, "expected a strong green channel inside the nested child");
    ASSERT_TRUE_MSG(g > r && g > b, "expected green (the nested child) to dominate");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.inspector_seam - the debug-only render-IR inspector: snapshot serialize, enumerate, and
// hit-test by id and by name. Guarded on the same gate as the seam so a build without the inspector drops the case
// cleanly rather than failing to link.

#ifdef FLOWI_UI_INSPECTOR
UTEST(render, inspector_seam) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const int layout_w = 256;
    const int layout_h = 256;
    ctx->layout_width = (float)layout_w;
    ctx->layout_height = (float)layout_h;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)layout_w, .height = (float)layout_h });

    // Two elements (not one) prove enumerate/hit-test actually discriminate rather than returning the sole
    // candidate. The second element's name embeds a quote and a backslash to exercise the snapshot's JSON
    // escaping. The fills make each emit a render command carrying its id/name/bounds.
    const float probe_w = 80.0f;
    const float probe_h = 40.0f;
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_layout_area_id(ID("inspector_probe"), .sizing = { sizing_fixed(probe_w), sizing_fixed(probe_h) },
                          .color = ui_color(40, 200, 60, 255)) {}
        ui_layout_area_id(ID("a\"b\\c"), .sizing = { sizing_fixed(30), sizing_fixed(20) },
                          .color = ui_color(200, 40, 60, 255)) {}
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    char snap[4096];
    const int32_t needed = fl_inspector_snapshot(snap, (int32_t)sizeof(snap));
    ASSERT_TRUE_MSG(needed > 0, "snapshot of a non-empty frame reports a non-zero length");
    ASSERT_TRUE_MSG(needed < (int32_t)sizeof(snap), "4KB snapshot buffer was large enough (not truncated)");
    ASSERT_EQ_MSG((int32_t)strlen(snap), needed, "snapshot NUL-terminates at exactly the reported length");
    ASSERT_TRUE_MSG(strstr(snap, "inspector_probe") != nullptr, "snapshot text mentions the known element");
    // The name a"b\c must appear JSON-escaped as a\"b\\c (the C literal below is that exact byte sequence).
    ASSERT_TRUE_MSG(strstr(snap, "a\\\"b\\\\c") != nullptr, "snapshot escapes quotes and backslashes in names");

    // snprintf-style truncation contract.
    ASSERT_EQ_MSG(fl_inspector_snapshot(nullptr, 0), needed, "a zero-size buffer still reports the full length");
    char small[10];
    const int32_t small_needed = fl_inspector_snapshot(small, (int32_t)sizeof(small));
    ASSERT_EQ_MSG(small_needed, needed, "a too-small buffer still reports the full length to retry with");
    ASSERT_EQ_MSG(small[sizeof(small) - 1], '\0', "a truncated snapshot is still NUL-terminated");
    ASSERT_EQ_MSG((int32_t)strlen(small), (int32_t)sizeof(small) - 1, "a truncated snapshot fills up to the NUL");

    const int32_t n = fl_inspector_element_count();
    ASSERT_TRUE_MSG(n > 0, "the frame enumerates at least one element");
    FlInspectorElement probe = { 0 };
    bool found = false;
    for (int32_t i = 0; i < n; ++i) {
        FlInspectorElement e;
        ASSERT_TRUE_MSG(fl_inspector_element_get(i, &e), "enumerate returns each in-range element");
        if (e.name.length == (u64)(sizeof("inspector_probe") - 1)
            && memcmp(e.name.data, "inspector_probe", sizeof("inspector_probe") - 1) == 0) {
            probe = e;
            found = true;
            break;
        }
    }
    ASSERT_TRUE_MSG(found, "the known element is present in the enumeration");
    ASSERT_TRUE_MSG(probe.width == probe_w && probe.height == probe_h,
                    "the enumerated element carries the fixed size it was laid out with");
    FlInspectorElement oob;
    ASSERT_FALSE_MSG(fl_inspector_element_get(n, &oob), "an out-of-range index is rejected");

    FlInspectorHit by_id;
    ASSERT_TRUE_MSG(fl_inspector_hit_test_id(probe.id, &by_id), "hit-test by id finds the element");
    ASSERT_TRUE_MSG(by_id.x > probe.x && by_id.x < probe.x + probe.width,
                    "hit-test center x is inside the element's bounds");
    ASSERT_TRUE_MSG(by_id.y > probe.y && by_id.y < probe.y + probe.height,
                    "hit-test center y is inside the element's bounds");

    FlInspectorHit by_name;
    ASSERT_TRUE_MSG(
        fl_inspector_hit_test_name(string_from_cstr_len("inspector_probe", sizeof("inspector_probe") - 1), &by_name),
        "hit-test by name finds the element");
    ASSERT_EQ_MSG(by_name.id, by_id.id, "by-name and by-id hit-tests resolve to the same element");
    ASSERT_TRUE_MSG(by_name.x == by_id.x && by_name.y == by_id.y, "by-name and by-id centers agree");

    FlInspectorHit miss;
    ASSERT_FALSE_MSG(
        fl_inspector_hit_test_name(string_from_cstr_len("no_such_element", sizeof("no_such_element") - 1), &miss),
        "hit-test by an absent name reports no hit");

    // Clay never assigns id 0 - it reserves the zero hash as null.
    FlInspectorHit miss_id;
    ASSERT_FALSE_MSG(fl_inspector_hit_test_id(0u, &miss_id), "hit-test by an absent id reports no hit");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.inspector_capture_owns_strings - the frame capture must own the strings it hands back, not borrow them.
//
// An element's name and text reach Clay as bare pointers into caller memory (ui_text_styled_impl passes
// string.data straight through), and the inspector's readers run between frames, after that memory has been
// reused. The test stands in for a per-frame arena with two plain caller buffers and scribbles over them once
// the frame is captured, so a borrowed pointer fails as a wrong-value assert in an ordinary build, not only
// as a use-after-poison under asan.

#define CANARY_TEXT "capture_canary_text"
#define CANARY_NAME "capture_canary_name"

UTEST(render, inspector_capture_owns_strings) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });

    // Ids are built by hand rather than with ID(): that macro marks its name a static literal, and a static
    // string is exempt from copying. Only a non-static (runtime) string exercises the copy.
    char text_storage[] = CANARY_TEXT;
    char name_storage[] = CANARY_NAME;
    const u64 text_length = sizeof(CANARY_TEXT) - 1;
    const u64 name_length = sizeof(CANARY_NAME) - 1;
    const FlLayoutId dynamic_id = { .name = { .data = name_storage, .length = name_length } };

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_layout_area_id(dynamic_id, .sizing = { sizing_fixed(120), sizing_fixed(40) },
                          .color = ui_color(40, 200, 60, 255)) {
            ui_text(string_from_cstr_len(text_storage, text_length));
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    // Stand in for the frame arena being rewound and reused under the capture's feet.
    memory_set(text_storage, sizeof(text_storage), 'X', text_length);
    memory_set(name_storage, sizeof(name_storage), 'X', name_length);

    char snap[4096];
    const int32_t needed = fl_inspector_snapshot(snap, (int32_t)sizeof(snap));
    ASSERT_TRUE_MSG(needed > 0 && needed < (int32_t)sizeof(snap), "snapshot fits the buffer");
    ASSERT_TRUE_MSG(strstr(snap, CANARY_TEXT) != nullptr,
                    "captured text survives the caller's buffer being overwritten");
    ASSERT_TRUE_MSG(strstr(snap, CANARY_NAME) != nullptr,
                    "captured element name survives the caller's buffer being overwritten");
    ASSERT_TRUE_MSG(strstr(snap, "XXXX") == nullptr, "no scribbled bytes leak into the snapshot");

    // The same ownership has to hold for every reader over the capture, not just the JSON one.
    bool found = false;
    const int32_t n = fl_inspector_element_count();
    for (int32_t i = 0; i < n; ++i) {
        FlInspectorElement e;
        ASSERT_TRUE_MSG(fl_inspector_element_get(i, &e), "enumerate returns each in-range element");
        if (e.name.length == name_length && memcmp(e.name.data, CANARY_NAME, name_length) == 0) {
            found = true;
            break;
        }
    }
    ASSERT_TRUE_MSG(found, "the enumerated name is the captured copy, not the overwritten original");

    FlInspectorHit hit;
    ASSERT_TRUE_MSG(fl_inspector_hit_test_name(string_from_cstr_len(CANARY_NAME, name_length), &hit),
                    "hit-test by name matches against the captured copy");
}

#undef CANARY_TEXT
#undef CANARY_NAME
#endif // FLOWI_UI_INSPECTOR

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// perf_fence: the performance regression fence. scene_raster_fill isolates the SW raster fill /
// border / rounded-rect paths and gates on tier-1 deterministic work counters (render_commands,
// bin_alloc_bytes, clip_broadcasts). Tier-3 wall-clock ns is printed, never asserted.

// Merges the core-side command count with the SW backend's bin/clip counters. Call after a rendered frame.
static FlFrameStats perf_fence_frame_stats(FlUiCtx* ctx) {
    FlFrameStats stats = { 0 };
    stats.render_commands = (u32)ui_get_render_command_count();
    sw_renderer_read_frame_stats(ctx->shared->renderer.user_data, &stats);
    return stats;
}

// One headless frame: a full-screen container holding N fixed-size children cycling through a solid fill, a
// bordered rect, and a rounded rect.
static void scene_raster_fill(FlUiCtx* ctx, int n) {
    const FlColor fill = ui_color(80, 120, 200, 255);
    const FlColor border = ui_color(220, 200, 60, 255);

    ui_update_begin(ctx);
    fl_layout_start(ID("scene_raster_fill_root"));
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { .width = sizing_grow(), .height = sizing_grow() },
        .direction = FlLayoutDirection_TopToBottom,
        .color = ui_color(20, 20, 24, 255),
        // Vertical clip: exercises the SW clip-broadcast path (a scissor pair fanned out to every tile) so
        // clip_broadcasts is a live counter, not a trivial zero.
        .clip = { .vertical = true },
    });
    for (int i = 0; i < n; i++) {
        fl_layout_start(ID_INT("scene_raster_fill_child", i));
        FlLayoutConfig cfg = {
            .sizing = { sizing_fixed(200), sizing_fixed(120) },
            .color = fill,
        };
        switch (i % 3) {
            case 1:
                cfg.border
                    = (FlLayoutBorder) { .color = border, .width = { .left = 2, .right = 2, .top = 2, .bottom = 2 } };
                break;
            case 2:
                cfg.radius = (FlCornerRadius) { .tl = 12, .tr = 12, .bl = 12, .br = 12 };
                break;
            default:
                break;
        }
        fl_layout_configure(cfg);
        fl_layout_end();
    }
    fl_layout_end();
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(perf_fence, scene_raster_fill) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const int n = 128;
    const int layout_w = 1920;
    const int layout_h = 1080;
    ctx->layout_width = (float)layout_w;
    ctx->layout_height = (float)layout_h;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)layout_w, .height = (float)layout_h });

    const u64 t0 = get_monotonic_time_ns_os();
    scene_raster_fill(ctx, n);
    const u64 t1 = get_monotonic_time_ns_os();

    const FlFrameStats stats = perf_fence_frame_stats(ctx);

    // Tier-3: wall-clock, printed for human sanity, never asserted.
    printf("[perf_fence] scene_raster_fill(N=%d): %llu ns, render_commands=%u bin_alloc_bytes=%u clip_broadcasts=%u\n",
           n, (unsigned long long)(t1 - t0), stats.render_commands, stats.bin_alloc_bytes, stats.clip_broadcasts);

    // Tier-1: deterministic work counters.
    ASSERT_EQ_MSG(stats.render_commands, 16u, "render_commands baseline drift");
    ASSERT_EQ_MSG(stats.bin_alloc_bytes, 87720u, "bin_alloc_bytes baseline drift");
    ASSERT_EQ_MSG(stats.clip_broadcasts, 1020u, "clip_broadcasts baseline drift");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.font_style_registry - fl_font_load_default / fl_font_style_register / fl_font_style_set end-to-end
// on the built-in default font (headless-safe; no system font deps).

UTEST(render, font_style_registry) {
    FlUiCtx* ctx = render_fixture_get(); // also makes the fixture context current (g_ui_ctx), which these fl_* read
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const FlFontId font = fl_font_load_default(0);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "fl_font_load_default must return a valid font id");

    fl_font_style_register(font, FlFontStyle_Bold);
    fl_font_style_set(FlFontStyle_Bold);
    ASSERT_EQ_MSG(ctx->active_font, font, "fl_font_style_set must make the registered style's font active");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.icon_registry - fl_icon_load / fl_icon_get over the hermetic stub VFS driver. image_load_async hands
// back a request-id handle synchronously, so the handle is valid even though the stub serves no svg.

UTEST(render, icon_registry) {
    FlUiCtx* ctx = render_fixture_get(); // wires VFS + image system and makes g_ui_ctx current (fl_icon_* read it)
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // Before any load the registry is the zeroed UiShared.icons[], which reads back as FL_IMAGE_HANDLE_INVALID.
    ASSERT_EQ_MSG(fl_icon_get(FlIcon_Checkmark), (FlImageHandle)FL_IMAGE_HANDLE_INVALID,
                  "icon must be FL_IMAGE_HANDLE_INVALID before fl_icon_load");

    vfs_test_register_driver(&s_stub_vfs_plugin); // a mount source for fl_icon_load
    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    ASSERT_TRUE(mount.mount != NULL);
    vfs_wait_all();

    // Null-mount guard is a no-op: nothing loads.
    fl_icon_load(NULL);
    ASSERT_EQ_MSG(fl_icon_get(FlIcon_Checkmark), (FlImageHandle)FL_IMAGE_HANDLE_INVALID,
                  "fl_icon_load(NULL) must not load anything");

    fl_icon_load(mount.mount);
    // Fetching all three slots guards against a wrong path/index in ICON_TABLE at any one of them.
    const FlImageHandle checkmark = fl_icon_get(FlIcon_Checkmark);
    const FlImageHandle chevron = fl_icon_get(FlIcon_ChevronDown);
    const FlImageHandle warning = fl_icon_get(FlIcon_Warning);
    ASSERT_NE_MSG(checkmark, (FlImageHandle)FL_IMAGE_HANDLE_INVALID,
                  "fl_icon_get(Checkmark) must return a loaded handle");
    ASSERT_NE_MSG(chevron, (FlImageHandle)FL_IMAGE_HANDLE_INVALID,
                  "fl_icon_get(ChevronDown) must return a loaded handle");
    ASSERT_NE_MSG(warning, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "fl_icon_get(Warning) must return a loaded handle");
    ASSERT_EQ_MSG(fl_icon_get(FlIcon_Count), (FlImageHandle)FL_IMAGE_HANDLE_INVALID,
                  "out-of-range icon must return FL_IMAGE_HANDLE_INVALID");
    ASSERT_EQ_MSG(fl_icon_get((FlIcon)-1), (FlImageHandle)FL_IMAGE_HANDLE_INVALID,
                  "negative icon index must return FL_IMAGE_HANDLE_INVALID");

    fl_vfs_mount_close(mount.mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.modal_scope - fl_modal_begin_frame / begin / end / close end-to-end: focus is saved when the scope
// opens and restored (with all popup-scope state cleared) when it closes.

UTEST(render, modal_begin_frame_keeps_armed_scope) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const u32 held_scope = 0x68656C64u;  // 'held'
    const u32 modal_scope = 0x6D6F6461u; // 'moda'
    ctx->popup_focus_scope_id = held_scope;
    ctx->saved_focused_id = 0x1111u;
    ctx->focused_id = 0x2222u;
    ctx->popup_needs_initial_focus = false;

    fl_modal_begin_frame(modal_scope);

    ASSERT_EQ_MSG(ctx->popup_focus_scope_id, held_scope, "an already-armed focus scope must stay armed");
    ASSERT_EQ_MSG(ctx->saved_focused_id, 0x1111u, "the saved focus must not be overwritten");
    ASSERT_FALSE_MSG(ctx->popup_needs_initial_focus, "no initial-focus request when the scope was not claimed");

    ctx->modal_scope_id = 0;
    ctx->popup_focus_scope_id = 0;
    ctx->saved_focused_id = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(render, modal_scope) {
    FlUiCtx* ctx = render_fixture_get(); // also makes the fixture context current (g_ui_ctx), which these fl_* read
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const u32 scope_id = 0x6D6F6461u; // 'moda'
    const u32 prior_focus = 0xABCD1234u;

    // A non-zero popup_focus_scope_id would make fl_modal_begin_frame skip the save.
    ctx->popup_focus_scope_id = 0;
    ctx->saved_focused_id = 0;

    const int layout_w = 256, layout_h = 256;
    ctx->layout_width = (float)layout_w;
    ctx->layout_height = (float)layout_h;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)layout_w, .height = (float)layout_h });

    ui_update_begin(ctx);

    // Simulate a widget owning focus before the modal opens, then arm the scope (as a dialog does at frame top).
    ctx->focused_id = prior_focus;
    fl_modal_begin_frame(scope_id);
    ASSERT_EQ_MSG(ctx->saved_focused_id, prior_focus, "fl_modal_begin_frame must save the pre-modal focus on open");
    ASSERT_EQ_MSG(ctx->popup_focus_scope_id, scope_id, "fl_modal_begin_frame must arm the modal focus scope");

    fl_modal_begin(scope_id);
    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .color = ui_color(40, 40, 48, 255)) {}
    fl_modal_end();

    ui_update_end(ctx, 1.0f / 60.0f);

    fl_modal_close();
    ASSERT_EQ_MSG(ctx->focused_id, prior_focus, "fl_modal_close must restore the pre-modal focus");
    ASSERT_EQ_MSG(ctx->popup_focus_scope_id, 0u, "fl_modal_close must clear the modal focus scope");
    ASSERT_EQ_MSG(ctx->saved_focused_id, 0u, "fl_modal_close must clear the saved focus");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.layout_area_macros_nest - fl_layout_area / fl_layout_area_id / FL_ID nested ≥2 deep. Open/close pairs
// must balance: Clay's open-element id is sampled at base, inside the innermost body, and after the macros
// unwind, so a leaked or duplicated fl_layout_end shows up as a residual.

UTEST(render, layout_area_macros_nest) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const int layout_w = 256;
    const int layout_h = 256;
    ctx->layout_width = (float)layout_w;
    ctx->layout_height = (float)layout_h;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)layout_w, .height = (float)layout_h });

    const FlColor outer_color = ui_color(40, 40, 200, 255); // blue backdrop
    const FlColor inner_color = ui_color(40, 200, 60, 255); // green innermost child, anchored top-left

    ui_update_begin(ctx);

    // Clay_GetOpenElementId() reads the innermost open element; ui_update_begin has the root open, so this is
    // the baseline.
    const uint32_t base_open = Clay_GetOpenElementId();

    uint32_t inner_open = base_open;
    fl_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .color = outer_color) {
        fl_layout_area_id(FL_ID("layout_area_inner"), .sizing = { sizing_fixed(120), sizing_fixed(120) },
                          .color = inner_color) {
            inner_open = Clay_GetOpenElementId();
        }
    }

    ASSERT_NE_MSG(base_open, inner_open, "nested fl_layout_area macros must open their containers");
    ASSERT_EQ_MSG(base_open, Clay_GetOpenElementId(),
                  "fl_layout_area macros must close exactly as many containers as they open");

    ui_update_end(ctx, 1.0f / 60.0f);

    // The innermost 120x120 green child sits top-left; sampling it proves the nesting structurally opened.
    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "render buffer data is null");
    const int px = 16, py = 16;
    const u8* pixels = (const u8*)buf.pixels;
    const size_t offset = (size_t)py * (size_t)buf.stride + (size_t)px * 3u;
    ASSERT_TRUE_MSG(pixels[offset + 1] > 100, "expected a strong green channel inside the nested child");
    ASSERT_TRUE_MSG(pixels[offset + 1] > pixels[offset + 0] && pixels[offset + 1] > pixels[offset + 2],
                    "expected green (the innermost child) to dominate");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.floating_scroll_area_macros - fl_floating_area / fl_floating_area_id / fl_scroll_area_id. Each must
// open exactly one logical container and unwind it, so Clay's open-element id returns to the baseline after
// every macro. The no-id fl_floating_area proves the FL_ID_INT(__LINE__) seed expands to a usable id.

UTEST(render, floating_scroll_area_macros) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });

    ui_update_begin(ctx);

    // Baseline open element (the root opened by ui_update_begin). Every macro below must return here.
    const uint32_t base_open = Clay_GetOpenElementId();

    // No-id floating: FL_ID_INT("_Floating", __LINE__) seeds a unique per-site id under the hood.
    uint32_t floating_open = base_open;
    fl_floating_area(.attach_to = FlFloatingAttachTo_Root, .z_index = 100,
                     .sizing = { sizing_fixed(120), sizing_fixed(120) }) {
        floating_open = Clay_GetOpenElementId();
    }
    ASSERT_NE_MSG(base_open, floating_open, "fl_floating_area must open its container");
    ASSERT_EQ_MSG(base_open, Clay_GetOpenElementId(), "fl_floating_area must close exactly what it opened");

    // Explicit-id floating: closes via the shared fl_layout_end primitive.
    fl_floating_area_id(FL_ID("floating_dialog"), .attach_to = FlFloatingAttachTo_Root, .z_index = 101,
                        .sizing = { sizing_fixed(80), sizing_fixed(80) }) {}
    ASSERT_EQ_MSG(base_open, Clay_GetOpenElementId(), "fl_floating_area_id must close exactly what it opened");

    // Scroll area: opens a clipping container and closes with fl_scroll_area_end.
    uint32_t scroll_open = base_open;
    fl_scroll_area_id(FL_ID("scroll_region"), .sizing = { sizing_grow(), sizing_grow() }, .show_scrollbar = true) {
        scroll_open = Clay_GetOpenElementId();
    }
    ASSERT_NE_MSG(base_open, scroll_open, "fl_scroll_area_id must open its container");
    ASSERT_EQ_MSG(base_open, Clay_GetOpenElementId(), "fl_scroll_area_id must close exactly what it opened");

    ui_update_end(ctx, 1.0f / 60.0f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.scroll_widget_visibility_reroute - ui_scroll's visibility + scroll-to-widget + content-extent paths
// read the scroll container's screen rect from item_state (populated because fl_scroll_area_begin registers it
// via ui_track_element). A fixed 200x80 viewport holds eight 60px buttons (480px of content), so only the top
// button is on-screen.

// One frame of the visibility bench: a grow-to-fill scroll area holding eight MD buttons stacked top-to-bottom.
static void scroll_vis_frame(FlUiCtx* ctx) {
    ui_update_begin(ctx);
    fl_scroll_area_id(FL_ID("scroll_vis_area"), .sizing = { sizing_grow(), sizing_grow() },
                      .direction = FlLayoutDirection_TopToBottom) {
        ui_button(FL_S("svb0"));
        ui_button(FL_S("svb1"));
        ui_button(FL_S("svb2"));
        ui_button(FL_S("svb3"));
        ui_button(FL_S("svb4"));
        ui_button(FL_S("svb5"));
        ui_button(FL_S("svb6"));
        ui_button(FL_S("svb7"));
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(render, scroll_widget_visibility_reroute) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 200.0f;
    ctx->layout_height = 80.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 200.0f, .height = 80.0f });

    // Two frames: frame 1 lays out and populates item_state + scroll content/viewport, frame 2 reads them back -
    // the item_state reads lag one frame.
    scroll_vis_frame(ctx);
    scroll_vis_frame(ctx);

    // svb0 sits at the top of the 80px viewport. svb7 is 420px down, fully clipped - Clay emits no command for
    // it, so it is absent from item_state, which reports as NOT visible.
    ASSERT_TRUE_MSG(ui_is_widget_visible(ID("svb0")), "top child must be visible in the scroll viewport");
    ASSERT_FALSE_MSG(ui_is_widget_visible(ID("svb7")),
                     "last child is fully clipped below the viewport and must not be visible");

    // Content-extent wrapper returns the real content dimensions (not the viewport-size fallback): 480px > 80px.
    FlScrollContainerExtent extent = ui_get_scroll_container_extent(ui_layout_id_to_clay_id(FL_ID("scroll_vis_area")));
    ASSERT_TRUE_MSG(extent.found, "scroll container extent must be queryable after two frames");
    ASSERT_TRUE_MSG(extent.content_extent.y > extent.viewport_size.y,
                    "content extent must exceed the viewport (real content dims, not the viewport fallback)");

    // svb1 straddles the viewport's bottom edge (y 60..120), so it is partially emitted and present in
    // item_state - the only supported target class. Immediate mode sets the offset now; calling before the next
    // frame's scroll_area_begin lets that frame's clip pick it up.
    ui_scroll_to_widget_ex(ID("svb1"), (FlScrollToOptions) { .immediate = true });
    scroll_vis_frame(ctx);

    ASSERT_TRUE_MSG(ui_is_widget_visible(ID("svb1")), "after scroll-to-widget svb1 must be visible");
    ASSERT_FALSE_MSG(ui_is_widget_visible(ID("svb0")),
                     "after scrolling down, the former top child must no longer be visible");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.scroll_wheel_hover_gate - fl_scroll_area_begin only applies the wheel tick to the scroll area the
// cursor is actually over. Two sibling scroll areas are open in the same frame; the shared cached wheel delta
// must not scroll both. The hover test reads the container's screen rect from the previous frame's layout
// (item_state), so the scene is driven for one warm-up frame before the wheel frame.

// One frame: two 100x80 vertical scroll areas (A left, B right) separated by a 100px spacer so their rects
// never touch. Each holds eight buttons (content taller than the 80px viewport → room to scroll).
static void scroll_wheel_frame(FlUiCtx* ctx, f32 mx, f32 my, f32 wheel_y) {
    ctx->input_state.mouse.x = mx;
    ctx->input_state.mouse.y = my;
    ctx->input_state.mouse.wheel_x_cached = 0.0f;
    ctx->input_state.mouse.wheel_y_cached = wheel_y;

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {
        fl_scroll_area_id(FL_ID("wheel_area_a"), .sizing = { sizing_fixed(100), sizing_fixed(80) },
                          .direction = FlLayoutDirection_TopToBottom) {
            ui_button(FL_S("wa0"));
            ui_button(FL_S("wa1"));
            ui_button(FL_S("wa2"));
            ui_button(FL_S("wa3"));
            ui_button(FL_S("wa4"));
            ui_button(FL_S("wa5"));
            ui_button(FL_S("wa6"));
            ui_button(FL_S("wa7"));
        }
        ui_layout_area(.sizing = { sizing_fixed(100), sizing_fixed(80) }) {}
        fl_scroll_area_id(FL_ID("wheel_area_b"), .sizing = { sizing_fixed(100), sizing_fixed(80) },
                          .direction = FlLayoutDirection_TopToBottom) {
            ui_button(FL_S("wb0"));
            ui_button(FL_S("wb1"));
            ui_button(FL_S("wb2"));
            ui_button(FL_S("wb3"));
            ui_button(FL_S("wb4"));
            ui_button(FL_S("wb5"));
            ui_button(FL_S("wb6"));
            ui_button(FL_S("wb7"));
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(render, scroll_wheel_hover_gate) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // Identity scale at a fixed size → logical == physical, so mouse coords compare directly to the container rects.
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });

    const u32 id_a = ui_layout_id_to_clay_id(FL_ID("wheel_area_a"));
    const u32 id_b = ui_layout_id_to_clay_id(FL_ID("wheel_area_b"));

    // Warm-up: no wheel, cursor parked away from both areas, so both containers' screen rects land in
    // item_state and their content/viewport sizes are recorded for next frame's max-scroll.
    scroll_wheel_frame(ctx, 1000.0f, 1000.0f, 0.0f);
    scroll_wheel_frame(ctx, 1000.0f, 1000.0f, 0.0f);

    ScrollAreaState* sa = hashmap_get(&ctx->shared->scroll_states, id_a);
    ScrollAreaState* sb = hashmap_get(&ctx->shared->scroll_states, id_b);
    ASSERT_TRUE_MSG(sa != nullptr && sb != nullptr, "both scroll areas must have persistent state after layout");
    ASSERT_NEAR_MSG(0.0, (double)sa->scroll_offset.y, 0.001, "area A starts unscrolled");
    ASSERT_NEAR_MSG(0.0, (double)sb->scroll_offset.y, 0.001, "area B starts unscrolled");

    // Wheel frame: cursor over area A (its rect is [0..100]x[0..80] at root origin), negative wheel delta →
    // positive scroll. Only A is hovered, so only A consumes the tick.
    scroll_wheel_frame(ctx, 50.0f, 40.0f, -2.0f);

    sa = hashmap_get(&ctx->shared->scroll_states, id_a);
    sb = hashmap_get(&ctx->shared->scroll_states, id_b);
    ASSERT_TRUE_MSG(sa->scroll_offset.y > 0.5f, "the hovered scroll area consumes the wheel and scrolls");
    ASSERT_NEAR_MSG(0.0, (double)sb->scroll_offset.y, 0.001,
                    "the unhovered scroll area must not move when the wheel is delivered to its sibling");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.scroll_wheel_nested - the wheel goes to the innermost hovered scroll area only, and hover is cut by the
// clip chain the hit tester uses. The outer area (200x80) holds a header_h spacer, then an inner area (100x60 at
// y = header_h), then a tall spacer so the outer has room to scroll.

static void scroll_nested_frame(FlUiCtx* ctx, f32 mx, f32 my, f32 wheel_y, f32 header_h) {
    ctx->input_state.mouse.x = mx;
    ctx->input_state.mouse.y = my;
    ctx->input_state.mouse.wheel_x_cached = 0.0f;
    ctx->input_state.mouse.wheel_y_cached = wheel_y;

    ui_update_begin(ctx);
    fl_scroll_area_id(FL_ID("nest_outer"), .sizing = { sizing_fixed(200), sizing_fixed(80) },
                      .direction = FlLayoutDirection_TopToBottom) {
        ui_layout_area(.sizing = { sizing_fixed(100), sizing_fixed(header_h) }) {}
        fl_scroll_area_id(FL_ID("nest_inner"), .sizing = { sizing_fixed(100), sizing_fixed(60) },
                          .direction = FlLayoutDirection_TopToBottom) {
            ui_layout_area(.sizing = { sizing_fixed(100), sizing_fixed(400) }) {}
        }
        ui_layout_area(.sizing = { sizing_fixed(100), sizing_fixed(400) }) {}
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

static void scroll_nested_setup(FlUiCtx* ctx, f32 header_h) {
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });

    // Start both areas unscrolled.
    for_each_hashmap(&ctx->shared->scroll_states, id, state) {
        state->scroll_offset = (FlVec2) { 0 };
        state->scroll_target = (FlVec2) { 0 };
    }

    // Two warm-up frames with the cursor parked away so layout rects and content sizes are recorded.
    scroll_nested_frame(ctx, 1000.0f, 1000.0f, 0.0f, header_h);
    scroll_nested_frame(ctx, 1000.0f, 1000.0f, 0.0f, header_h);
}

static void scroll_nested_reset_input(FlUiCtx* ctx) {
    ctx->input_state.mouse.wheel_x_cached = 0.0f;
    ctx->input_state.mouse.wheel_y_cached = 0.0f;
    ctx->input_state.mouse.x = 0.0f;
    ctx->input_state.mouse.y = 0.0f;
}

UTEST(render, scroll_wheel_nested) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const u32 outer_id = ui_layout_id_to_clay_id(FL_ID("nest_outer"));
    const u32 inner_id = ui_layout_id_to_clay_id(FL_ID("nest_inner"));

    // Inner sits at y 10..70, fully inside the outer viewport (0..80).
    scroll_nested_setup(ctx, 10.0f);

    ScrollAreaState* outer = hashmap_get(&ctx->shared->scroll_states, outer_id);
    ScrollAreaState* inner = hashmap_get(&ctx->shared->scroll_states, inner_id);
    ASSERT_TRUE_MSG(outer != nullptr && inner != nullptr, "both scroll areas must have persistent state");
    ASSERT_NEAR_MSG(0.0, (double)outer->scroll_offset.y, 0.001, "outer starts unscrolled");
    ASSERT_NEAR_MSG(0.0, (double)inner->scroll_offset.y, 0.001, "inner starts unscrolled");

    // Over the inner area: only the inner scrolls.
    scroll_nested_frame(ctx, 50.0f, 40.0f, -2.0f, 10.0f);
    outer = hashmap_get(&ctx->shared->scroll_states, outer_id);
    inner = hashmap_get(&ctx->shared->scroll_states, inner_id);
    ASSERT_TRUE_MSG(inner->scroll_offset.y > 0.5f, "a wheel tick over the inner area scrolls the inner area");
    ASSERT_NEAR_MSG(0.0, (double)outer->scroll_offset.y, 0.001, "a wheel tick over the inner area leaves the outer");

    // Over the outer area but outside the inner (the header strip): only the outer scrolls.
    f32 inner_before = inner->scroll_offset.y;
    scroll_nested_frame(ctx, 50.0f, 5.0f, -2.0f, 10.0f);
    outer = hashmap_get(&ctx->shared->scroll_states, outer_id);
    inner = hashmap_get(&ctx->shared->scroll_states, inner_id);
    ASSERT_TRUE_MSG(outer->scroll_offset.y > 0.5f, "a wheel tick over the outer-only region scrolls the outer");
    ASSERT_NEAR_MSG((double)inner_before, (double)inner->scroll_offset.y, 0.001,
                    "a wheel tick over the outer-only region leaves the inner");

    scroll_nested_reset_input(ctx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.scroll_wheel_clipped_away - the part of a scroll area cut away by an enclosing clip must not take the
// wheel. Here the inner area (y 60..120) hangs below the outer viewport (0..80); the cursor at y = 100 is inside the
// inner's own rect but outside every visible region.

UTEST(render, scroll_wheel_clipped_away) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const u32 outer_id = ui_layout_id_to_clay_id(FL_ID("nest_outer"));
    const u32 inner_id = ui_layout_id_to_clay_id(FL_ID("nest_inner"));

    scroll_nested_setup(ctx, 60.0f);
    ScrollAreaState* outer = hashmap_get(&ctx->shared->scroll_states, outer_id);
    ScrollAreaState* inner = hashmap_get(&ctx->shared->scroll_states, inner_id);
    ASSERT_TRUE_MSG(outer != nullptr && inner != nullptr, "both scroll areas must have persistent state");

    scroll_nested_frame(ctx, 50.0f, 100.0f, -2.0f, 60.0f);
    outer = hashmap_get(&ctx->shared->scroll_states, outer_id);
    inner = hashmap_get(&ctx->shared->scroll_states, inner_id);
    ASSERT_NEAR_MSG(0.0, (double)inner->scroll_offset.y, 0.001, "the clipped-away part of the inner takes no wheel");
    ASSERT_NEAR_MSG(0.0, (double)outer->scroll_offset.y, 0.001, "the outer takes no wheel outside its own rect");

    // The visible part of the inner (y 60..80) still scrolls it.
    scroll_nested_frame(ctx, 50.0f, 70.0f, -2.0f, 60.0f);
    inner = hashmap_get(&ctx->shared->scroll_states, inner_id);
    ASSERT_TRUE_MSG(inner->scroll_offset.y > 0.5f, "the visible part of the inner area scrolls it");

    scroll_nested_reset_input(ctx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.scroll_state_alloc_failure_stack_balance - the fallback path fl_scroll_area_begin takes when
// scroll-state allocation fails must still push a (null-state) stack entry, so the paired fl_scroll_area_end
// pops its own entry instead of the enclosing scroll area's. The failure is forced via the ui_scroll test seam
// for the inner area only; the outer area's entry and depth must come through untouched.

UTEST(render, scroll_state_alloc_failure_stack_balance) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });

    const u32 outer_id = ui_layout_id_to_clay_id(FL_ID("balance_outer"));
    const u32 inner_id = ui_layout_id_to_clay_id(FL_ID("balance_inner"));

    ui_update_begin(ctx);

    fl_scroll_area_id(FL_ID("balance_outer"), .sizing = { sizing_fixed(200), sizing_fixed(160) },
                      .direction = FlLayoutDirection_TopToBottom) {
        ASSERT_EQ_MSG(1u, ctx->scroll_area_stack_depth, "outer scroll area must push one stack entry");
        ASSERT_EQ_MSG(outer_id, ctx->scroll_area_stack[0].scroll_area_id, "stack top is the outer area");
        ScrollAreaState* outer_state = ctx->scroll_area_stack[0].state;
        ASSERT_TRUE_MSG(outer_state != nullptr, "outer area has real scroll state");

        ui_scroll_test_force_state_alloc_failure(true);
        fl_scroll_area_id(FL_ID("balance_inner"), .sizing = { sizing_fixed(100), sizing_fixed(80) },
                          .direction = FlLayoutDirection_TopToBottom) {
            ASSERT_EQ_MSG(2u, ctx->scroll_area_stack_depth, "fallback begin must still push a stack entry");
            ASSERT_EQ_MSG(inner_id, ctx->scroll_area_stack[1].scroll_area_id, "fallback entry carries the inner id");
            ASSERT_TRUE_MSG(ctx->scroll_area_stack[1].state == nullptr, "fallback entry has no scroll state");
            // A widget inside the fallback area runs against a stack entry with no scroll state.
            ui_button(FL_S("balance_inner_btn"));
        }
        ui_scroll_test_force_state_alloc_failure(false);

        ASSERT_EQ_MSG(1u, ctx->scroll_area_stack_depth,
                      "inner begin/end pair must leave the enclosing depth untouched");
        ASSERT_EQ_MSG(outer_id, ctx->scroll_area_stack[0].scroll_area_id,
                      "outer stack entry survives the inner fallback scope");
        ASSERT_TRUE_MSG(ctx->scroll_area_stack[0].state == outer_state,
                        "outer stack entry still points at the outer scroll state");
        ui_button(FL_S("balance_outer_btn"));
    }
    ASSERT_EQ_MSG(0u, ctx->scroll_area_stack_depth, "all scroll scopes unwound");

    ui_update_end(ctx, 1.0f / 60.0f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.resize - the software backend's change_resolution seam, on a standalone backend (no UI fixture /
// global context). A pixel near the far corner of the GROWN buffer must carry the freshly rendered clear
// color, which fails if change_resolution forgot to reallocate the RGB24 target.

UTEST(render, resize) {
    FlArena* arena = arena_new();
    FlRenderBackend backend = create_software_renderer(arena, nullptr, FontRenderMode_Grayscale);
    ASSERT_TRUE_MSG(backend.change_resolution != nullptr, "backend has no change_resolution seam");
    ASSERT_TRUE_MSG(backend.read_pixels != nullptr, "backend has no pixel-readback capability");

    // The output buffer lives on the surface; the change_resolution seam below resizes it in place.
    FlSurface* surface = backend.create_surface(backend.user_data, 64, 48);

    // An empty command stream still clears the whole buffer to the background clear color, which is enough to
    // prove the target was allocated and rendered at the current resolution.
    const FlColor clear = { .r = 200, .g = 40, .b = 50, .a = 255 };
    const FlRenderFrame frame = {
        .commands = nullptr,
        .bytes = 0,
        .global_alpha = 255,
        .background = { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID, .clear = clear },
    };

    render_seam_render(&backend, surface, &frame);
    FlPixelReadback before;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&backend, surface, &before), "read_pixels failed");
    ASSERT_EQ((u32)64, before.width);
    ASSERT_EQ((u32)48, before.height);
    ASSERT_EQ((u32)(64 * 3), before.stride);

    render_seam_change_resolution(&backend, surface, 128, 96);
    render_seam_render(&backend, surface, &frame);
    FlPixelReadback after;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&backend, surface, &after), "read_pixels failed");
    ASSERT_EQ((u32)128, after.width);
    ASSERT_EQ((u32)96, after.height);
    ASSERT_EQ((u32)(128 * 3), after.stride);

    // A pixel near the far corner only exists in the reallocated, freshly rendered buffer.
    const int px = 120;
    const int py = 90;
    const u8* pixels = (const u8*)after.pixels;
    const size_t offset = (size_t)py * (size_t)after.stride + (size_t)px * 3u;
    ASSERT_EQ((u32)clear.r, (u32)pixels[offset + 0]);
    ASSERT_EQ((u32)clear.g, (u32)pixels[offset + 1]);
    ASSERT_EQ((u32)clear.b, (u32)pixels[offset + 2]);

    backend.destroy_surface(backend.user_data, surface);
    if (backend.destroy != nullptr) {
        backend.destroy(backend.user_data);
    }
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.surface_cycle_no_perm_arena_growth - a SW surface owns a dedicated arena and the SwSurface
// struct lives in that arena, not the renderer's perm_arena, so a full create/destroy cycle must leave
// perm_arena's used-bytes high-water mark exactly where it started.

UTEST(render, surface_cycle_no_perm_arena_growth) {
    FlArena* arena = arena_new();
    FlRenderBackend backend = create_software_renderer(arena, nullptr, FontRenderMode_Grayscale);
    ASSERT_TRUE_MSG(backend.create_surface != nullptr, "backend has no create_surface seam");
    ASSERT_TRUE_MSG(backend.destroy_surface != nullptr, "backend has no destroy_surface seam");

    // Warm up one cycle so any first-touch, one-time cost settles before snapshotting the baseline.
    backend.destroy_surface(backend.user_data, backend.create_surface(backend.user_data, 64, 64));

    // perm_arena is the arena we passed in; its used-bytes mark is the current write position.
    const char* used_before = (const char*)fl_arena_current_pos(arena);

    const int iterations = 1000;
    for (int i = 0; i < iterations; ++i) {
        FlSurface* surface = backend.create_surface(backend.user_data, 64, 64);
        backend.destroy_surface(backend.user_data, surface);
    }

    const char* used_after = (const char*)fl_arena_current_pos(arena);
    // Any per-iteration growth would show up as ~iterations * sizeof(SwSurface).
    ASSERT_EQ_MSG((ptrdiff_t)0, (ptrdiff_t)(used_after - used_before),
                  "perm_arena grew across surface create/destroy cycles — SwSurface struct is leaking");

    if (backend.destroy != nullptr) {
        backend.destroy(backend.user_data);
    }
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.scissor_stack_deep_nesting_balanced - the SW renderer's scissor stack must stay paired with the IR
// clip nesting even when nesting exceeds MAX_SCISSOR_STACK_DEPTH (16). A push past the cap is dropped, but the
// matching pop must NOT pop a still-in-scope rect off the real stack; it decrements an ignored-pushes counter
// instead, mirroring the GPU backend's ignored_pushes in gpu_ir_to_draw_cmd.c.
//
// Scene on a 64x64 (single-tile) surface: 15 clips fit, the 15th restricting to the top-left 16x16. A 16th
// push is dropped and immediately popped; the 16x16 clip must still be innermost when the fill is drawn.

UTEST(render, scissor_stack_deep_nesting_balanced) {
    FlArena* arena = arena_new();
    FlRenderBackend backend = create_software_renderer(arena, nullptr, FontRenderMode_Grayscale);
    FlSurface* surface = backend.create_surface(backend.user_data, 64, 64);

    FlArena* ir = arena_new();
    fl_arena_align(ir, 8);
    const u8* start = (const u8*)fl_arena_current_pos(ir);

    const FlColor red = { 220, 40, 50, 255 };
    const FlColor red4[4] = { red, red, red, red };
    const FlRect full = { 0.0f, 0.0f, 64.0f, 64.0f };
    const FlRect corner = { 0.0f, 0.0f, 16.0f, 16.0f };

    // 15 pushes fit (depth caps at MAX_SCISSOR_STACK_DEPTH == 16, slot 0 is the tile scissor). The last one
    // that fits restricts to the top-left 16x16; the earlier 14 leave the region unrestricted.
    for (int i = 0; i < 14; ++i) {
        fl_ir_clip_push(ir, full);
    }
    fl_ir_clip_push(ir, corner); // 15th push - the innermost rect that must survive the over-cap pop below

    // 16th push is past the cap: dropped by the renderer. Its matching pop must cancel the dropped push, not
    // pop the still-active 16x16 clip off the real stack.
    fl_ir_clip_push(ir, full);
    fl_ir_clip_pop(ir);

    // Full-surface fill drawn while the 16x16 clip should still be the innermost scissor.
    fl_ir_fill_rect(ir, full, (FlCornerRadius) { 0 }, red4, nullptr);

    // Balance the stream: pop the 15 real pushes.
    for (int i = 0; i < 15; ++i) {
        fl_ir_clip_pop(ir);
    }

    const size_t bytes = (size_t)((const u8*)fl_arena_current_pos(ir) - start);

    const FlColor clear = { 10, 10, 10, 255 };
    const FlRenderFrame frame = {
        .commands = start,
        .bytes = bytes,
        .global_alpha = 255,
        .background = { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID, .clear = clear },
    };
    render_seam_render(&backend, surface, &frame);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&backend, surface, &buf), "read_pixels failed");
    const u8* pixels = (const u8*)buf.pixels;

    // Control: inside the 16x16 clip the fill is drawn (red) regardless of the bug - proves the fill ran.
    const size_t in = (size_t)8 * buf.stride + (size_t)8 * 3u;
    ASSERT_TRUE_MSG(pixels[in + 0] > 150 && pixels[in + 0] > pixels[in + 1] && pixels[in + 0] > pixels[in + 2],
                    "fill must be visible (red) inside the innermost 16x16 clip");

    // Discriminator: outside the 16x16 clip but inside the 14th (full-surface) clip - clipped away, or the
    // over-cap pop exposed the full clip and the fill leaks through as red.
    const size_t out = (size_t)32 * buf.stride + (size_t)32 * 3u;
    ASSERT_EQ_MSG((u32)clear.r, (u32)pixels[out + 0], "fill leaked past the 16x16 clip — scissor pop/push imbalance");
    ASSERT_EQ_MSG((u32)clear.g, (u32)pixels[out + 1], "fill leaked past the 16x16 clip — scissor pop/push imbalance");
    ASSERT_EQ_MSG((u32)clear.b, (u32)pixels[out + 2], "fill leaked past the 16x16 clip — scissor pop/push imbalance");

    fl_arena_destroy(ir);
    backend.destroy_surface(backend.user_data, surface);
    if (backend.destroy != nullptr) {
        backend.destroy(backend.user_data);
    }
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.text_alpha_linear_fade - the SW text/coverage raster must fold the frame's global_alpha into text
// exactly once, matching the GPU backend. global_alpha is applied at command-emit time (sr_color_premul /
// pack_tint_premultiplied), so draw_text must NOT re-multiply by command->global_alpha.
//
// A full-coverage (0xFF) alpha-coverage quad with an OPAQUE BLACK tint over a WHITE background, swept across
// global_alpha values. Black tint premultiplies to zero RGB, so the covered pixel's darkening away from white
// isolates the coverage/alpha term. The darkening must be LINEAR in global_alpha: pixel == 255 - fold(255,
// global_alpha), where fold is the shared round-to-nearest /255 both backends use. A quadratic (double-applied)
// fade lands far outside tolerance at partial alpha (ga=128 -> ~192, not 128).

UTEST(render, text_alpha_linear_fade) {
    FlArena* arena = arena_new();
    FlRenderBackend backend = create_software_renderer(arena, nullptr, FontRenderMode_Grayscale);
    FlSurface* surface = backend.create_surface(backend.user_data, 32, 32);

    // 16x16 single-channel coverage buffer, fully opaque (the SW backend blits coverage 1:1).
    u8 coverage[16 * 16];
    memset(coverage, 0xFF, sizeof(coverage));

    // Opaque black tint over a white clear color: the covered pixel darkens from 255 toward 0 purely by the
    // alpha/coverage term. Sample a pixel well inside the dst rect (8,8)-(24,24).
    const FlColor black = { 0, 0, 0, 255 };
    const FlColor white = { 255, 255, 255, 255 };
    const int sample_x = 16, sample_y = 16;

    const u8 alphas[] = { 0, 64, 128, 192, 255 };
    for (size_t i = 0; i < sizeof(alphas) / sizeof(alphas[0]); ++i) {
        const u8 ga = alphas[i];

        FlArena* ir = arena_new();
        fl_arena_align(ir, 8);
        const u8* start = (const u8*)fl_arena_current_pos(ir);
        FlImageSource src = { .kind = FlImageKind_Dynamic };
        src.dynamic.pixels = coverage;
        src.dynamic.w = 16;
        src.dynamic.h = 16;
        src.dynamic.stride = 16;
        fl_ir_image(ir, src, FlImageMode_AlphaCoverage, (FlRect) { 8.0f, 8.0f, 16.0f, 16.0f },
                    (FlRect) { 0.0f, 0.0f, 0.0f, 0.0f }, black, FlImageFilter_Nearest, nullptr);
        const size_t bytes = (size_t)((const u8*)fl_arena_current_pos(ir) - start);

        const FlRenderFrame frame = {
            .commands = start,
            .bytes = bytes,
            .global_alpha = ga,
            .background = { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID, .clear = white },
        };
        render_seam_render(&backend, surface, &frame);

        FlPixelReadback buf;
        ASSERT_TRUE_MSG(render_seam_read_pixels(&backend, surface, &buf), "read_pixels failed");
        const u8* px = (const u8*)buf.pixels + (size_t)sample_y * buf.stride + (size_t)sample_x * 3u;

        // The single, round-to-nearest /255 fold both backends apply; linear output = white minus it.
        const int folded = (ga == 255) ? 255 : (255 * ga + 127) / 255;
        const int expected = 255 - folded;

        // Tolerance absorbs the tile's Q7/Q15 coverage quantization (~1-2 LSB); a quadratic fade misses by tens.
        for (int c = 0; c < 3; ++c) {
            const int got = px[c];
            ASSERT_TRUE_MSG(abs(got - expected) <= 4, "SW text global_alpha must fade linearly (matching GPU)");
        }
        fl_arena_destroy(ir);
    }

    backend.destroy_surface(backend.user_data, surface);
    if (backend.destroy != nullptr) {
        backend.destroy(backend.user_data);
    }
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.dynamic_image_stride_zero_tight - a dynamic image supplied with stride == 0 (the "tightly packed"
// convention the GPU backend honors, tex_sub_image_strided: `if (stride == 0) stride = w * bpp;`) must render
// consecutive source rows on the SW backend too, not row 0 repeated. Both emit_image sites are exercised: the
// alpha-coverage path (1 byte/pixel) and the dynamic color path (4 bytes/pixel).

UTEST(render, dynamic_image_stride_zero_tight) {
    FlArena* arena = arena_new();
    FlRenderBackend backend = create_software_renderer(arena, nullptr, FontRenderMode_Grayscale);
    FlSurface* surface = backend.create_surface(backend.user_data, 32, 32);
    const FlColor white = { 255, 255, 255, 255 };

    // dst maps the 16x16 source 1:1 into (8,8)-(24,24). A top sample lands in source rows 0..7, a bottom
    // sample in rows 8..15. Give the two halves different content so a row-0-repeated read is detectable.
    const FlRect dst = { 8.0f, 8.0f, 16.0f, 16.0f };
    const int top_x = 16, top_y = 11; // inside dst, upper half -> source rows 0..7
    const int bot_x = 16, bot_y = 20; // inside dst, lower half -> source rows 8..15

    // --- Alpha-coverage path (1 byte/pixel): top half uncovered (0x00), bottom half fully covered (0xFF).
    // Opaque black tint over white: uncovered stays white, covered darkens toward black.
    {
        u8 coverage[16 * 16];
        for (int y = 0; y < 16; ++y) {
            memset(coverage + (size_t)y * 16, y < 8 ? 0x00 : 0xFF, 16);
        }
        const FlColor black = { 0, 0, 0, 255 };

        FlArena* ir = arena_new();
        fl_arena_align(ir, 8);
        const u8* start = (const u8*)fl_arena_current_pos(ir);
        FlImageSource src = { .kind = FlImageKind_Dynamic };
        src.dynamic.pixels = coverage;
        src.dynamic.w = 16;
        src.dynamic.h = 16;
        src.dynamic.stride = 0; // tightly packed; must be read as w*1, not literal 0
        fl_ir_image(ir, src, FlImageMode_AlphaCoverage, dst, (FlRect) { 0.0f, 0.0f, 0.0f, 0.0f }, black,
                    FlImageFilter_Nearest, nullptr);
        const size_t bytes = (size_t)((const u8*)fl_arena_current_pos(ir) - start);

        const FlRenderFrame frame = {
            .commands = start,
            .bytes = bytes,
            .global_alpha = 255,
            .background = { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID, .clear = white },
        };
        render_seam_render(&backend, surface, &frame);

        FlPixelReadback buf;
        ASSERT_TRUE_MSG(render_seam_read_pixels(&backend, surface, &buf), "read_pixels failed");
        const u8* top = (const u8*)buf.pixels + (size_t)top_y * buf.stride + (size_t)top_x * 3u;
        const u8* bot = (const u8*)buf.pixels + (size_t)bot_y * buf.stride + (size_t)bot_x * 3u;

        // A stride-0 read would take source row 0 (uncovered) for every row, leaving the bottom white too.
        ASSERT_TRUE_MSG(top[0] > 200, "alpha-coverage top half (source rows 0..7) must stay ~white (uncovered)");
        ASSERT_TRUE_MSG(bot[0] < 80, "alpha-coverage bottom half must darken — stride 0 wrongly read as literal 0");
        fl_arena_destroy(ir);
    }

    // --- Dynamic color path (4 bytes/pixel RGBA): top half blue, bottom half red. White tint = no modulation.
    {
        u8 rgba[16 * 16 * 4];
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                u8* p = rgba + ((size_t)y * 16 + (size_t)x) * 4;
                p[0] = y < 8 ? 0 : 255;
                p[1] = 0;
                p[2] = y < 8 ? 255 : 0;
                p[3] = 255;
            }
        }

        FlArena* ir = arena_new();
        fl_arena_align(ir, 8);
        const u8* start = (const u8*)fl_arena_current_pos(ir);
        FlImageSource src = { .kind = FlImageKind_Dynamic };
        src.dynamic.pixels = rgba;
        src.dynamic.w = 16;
        src.dynamic.h = 16;
        src.dynamic.stride = 0; // tightly packed; must be read as w*4, not literal 0
        fl_ir_image(ir, src, FlImageMode_Color, dst, (FlRect) { 0.0f, 0.0f, 0.0f, 0.0f }, white, FlImageFilter_Nearest,
                    nullptr);
        const size_t bytes = (size_t)((const u8*)fl_arena_current_pos(ir) - start);

        const FlRenderFrame frame = {
            .commands = start,
            .bytes = bytes,
            .global_alpha = 255,
            .background = { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID, .clear = white },
        };
        render_seam_render(&backend, surface, &frame);

        FlPixelReadback buf;
        ASSERT_TRUE_MSG(render_seam_read_pixels(&backend, surface, &buf), "read_pixels failed");
        const u8* top = (const u8*)buf.pixels + (size_t)top_y * buf.stride + (size_t)top_x * 3u;
        const u8* bot = (const u8*)buf.pixels + (size_t)bot_y * buf.stride + (size_t)bot_x * 3u;

        // A stride-0 read would take source row 0 (blue) for every destination row.
        ASSERT_TRUE_MSG(top[2] > 200 && top[0] < 80, "color top half (source rows 0..7) must be blue");
        ASSERT_TRUE_MSG(bot[0] > 200 && bot[2] < 80,
                        "color bottom half must be red — stride 0 wrongly read as literal 0");
        fl_arena_destroy(ir);
    }

    backend.destroy_surface(backend.user_data, surface);
    if (backend.destroy != nullptr) {
        backend.destroy(backend.user_data);
    }
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Composite a GPU-packed straight color (packed RGBA8, R low byte / A high byte - GLSL unpackUnorm4x8 order)
// over an OPAQUE background at full coverage, exactly as the GPU raster shader does: premultiply then over,
// out = rgb·a + bg·(1−a), round-to-nearest /255. An SW pixel compared against this is compared against the GPU
// reference, not a hand-typed constant.
static void gpu_straight_over_ref(u32 packed, const FlColor* bg, int out[3]) {
    const int a = (int)((packed >> 24) & 0xffu);
    const int c[3] = { (int)(packed & 0xffu), (int)((packed >> 8) & 0xffu), (int)((packed >> 16) & 0xffu) };
    const int b[3] = { bg->r, bg->g, bg->b };
    for (int i = 0; i < 3; ++i) {
        out[i] = (c[i] * a + b[i] * (255 - a) + 127) / 255;
    }
}

static bool render_ir_to_surface(FlRenderBackend* backend, FlSurface* surface, const FlColor* clear, const u8* start,
                                 size_t bytes, FlPixelReadback* out) {
    const FlRenderFrame frame = {
        .commands = start,
        .bytes = bytes,
        .global_alpha = 255,
        .background = { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID, .clear = *clear },
    };
    render_seam_render(backend, surface, &frame);
    return render_seam_read_pixels(backend, surface, out);
}

static bool px_matches_gpu_over(const FlPixelReadback* buf, int x, int y, u32 packed, const FlColor* bg, int tol) {
    int ref[3];
    gpu_straight_over_ref(packed, bg, ref);
    const u8* p = (const u8*)buf->pixels + (size_t)y * buf->stride + (size_t)x * 3u;
    return abs((int)p[0] - ref[0]) <= tol && abs((int)p[1] - ref[1]) <= tol && abs((int)p[2] - ref[2]) <= tol;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// render.translucent_fill_border_over - the SW fill/border/gradient blend must apply the source alpha exactly
// once (straight-over: out = rgb·a + bg·(1−a)), matching the GPU backend, not rgb·a². Colors reach the renderer
// with global_alpha folded in, so premultiplying the rgb there would apply alpha a second time via the lerp.
// Each SW pixel is compared against the GPU backend's own compositing of the GPU-packed color for the same IR
// (gpu_ir_to_draw_cmds → gpu_straight_over_ref).
//
// Covers all four translator sites that use straight packing: solid fill, a square (radius-0) border
// (straight edges through the fill path), a two-color gradient fill (RenderMask_ColorGradient), and a rounded
// border (whose corners render through process_8_pixels_border).

// A layout border's dash pattern has to survive FlLayoutBorder.stroke -> Clay element config -> render data ->
// clay_to_render_ir, and it is the only field on that path with no other consumer. Driven at ui_scale 2
// because the lengths scale, and at scale 1 an unscaled pattern is indistinguishable from a correct one.
UTEST(render, layout_border_dash_pattern_reaches_the_ir_scaled) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(2.0f, 2.0f);

    ui_update_begin(ctx);
    fl_layout_start(ID("dashed_border_root"));
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { .width = sizing_grow(), .height = sizing_grow() },
    });
    fl_layout_start(ID("dashed_border_child"));
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_fixed(100), sizing_fixed(100) },
        .border = { .color = ui_color(220, 200, 60, 255),
                    .width = { .left = 2, .right = 2, .top = 2, .bottom = 2 },
                    .stroke = { 3.0f, 2.5f } },
    });
    fl_layout_end();
    fl_layout_end();
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_TRUE_MSG(s_render.border_found, "a configured border must reach the frame as a border record");
    ASSERT_TRUE_MSG(s_render.submitted_border.stroke.dash_len == 6.0f
                        && s_render.submitted_border.stroke.gap_len == 5.0f,
                    "the dash pattern arrives scaled by the UI scale, like every other length");
}

// Text follows a scale change made AFTER the font was loaded. A font handle names the size the caller
// authored; text_scale is multiplied in on the way out, so nothing about a face is frozen at the scale it
// happened to be loaded under. Both halves are covered: an explicit config.font_size, and font_size 0 falling
// back to the load size. Driven at scale 2 because at scale 1 an unmultiplied size looks like a correct one.
UTEST(render, text_size_follows_a_scale_change_after_the_font_loads) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(1.0f, 1.0f);

    // Loaded while the scale is still 1: everything below asserts this handle follows the later change.
    const u16 authored = 12;
    FlFontId font = font_cache_load_font_memory(s_render.font_cache, k_glyph_test_ttf, k_glyph_test_ttf_size, authored);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "the embedded test font loads");
    ASSERT_EQ_MSG(font_cache_get_size(s_render.font_cache, font), (int)authored,
                  "the handle keeps the authored size, unscaled");

    ui_set_scale(2.0f, 2.0f);

    // Explicit size: 20px authored must reach the IR as a 40px raster key.
    ui_update_begin(ctx);
    fl_layout_start(ID("scaled_text_root"));
    fl_layout_configure((FlLayoutConfig) { .sizing = { .width = sizing_grow(), .height = sizing_grow() } });
    ui_text_styled(FL_S("Ao"), .font_id = font, .font_size = 20, .color = ui_color(255, 255, 255, 255));
    fl_layout_end();
    ui_update_end(ctx, 1.0f / 60.0f);

    bool explicit_found = s_render.text_run_found;
    u16 explicit_size = s_render.submitted_text_run.size;

    // Size 0: the load-size fallback must scale the same way, which only holds if the load size is authored.
    ui_update_begin(ctx);
    fl_layout_start(ID("scaled_text_default_root"));
    fl_layout_configure((FlLayoutConfig) { .sizing = { .width = sizing_grow(), .height = sizing_grow() } });
    ui_text_styled(FL_S("Ao"), .font_id = font, .color = ui_color(255, 255, 255, 255));
    fl_layout_end();
    ui_update_end(ctx, 1.0f / 60.0f);

    bool default_found = s_render.text_run_found;
    u16 default_size = s_render.submitted_text_run.size;

    // A font loaded AFTER the change must land on the same size as the one loaded before it.
    FlFontId late = font_cache_load_font_memory(s_render.font_cache, k_glyph_test_ttf, k_glyph_test_ttf_size, authored);
    int late_size = font_cache_get_size(s_render.font_cache, late);

    ASSERT_TRUE_MSG(explicit_found, "the styled text must reach the frame as a text run");
    ASSERT_EQ_MSG((int)explicit_size, (int)font_cache_whole_px_to_q4(40),
                  "an authored 20px reaches the IR at 40px when text_scale is 2");
    ASSERT_TRUE_MSG(default_found, "the default-size text must reach the frame as a text run");
    ASSERT_EQ_MSG((int)default_size, (int)font_cache_whole_px_to_q4((u16)(authored * 2)),
                  "the load-size fallback scales too, so the load size must have stayed authored");
    ASSERT_EQ_MSG(late_size, (int)authored, "a font loaded under scale 2 keeps the same authored size");
}

// Letter spacing and line height are authored px too, so they must track text_scale with the glyphs. Both are
// stated relatively - a scale-2 frame against a scale-1 frame authored at double - so no Clay line-box rule is
// baked into the assertion. The third frame is the vacuity guard: same authored values at scale 1, so an
// assertion that passes for the wrong reason still has to explain why that one differs.
UTEST(render, text_letter_spacing_and_line_height_follow_the_scale) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlFontId font = font_cache_load_font_memory(s_render.font_cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 16);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "the embedded test font loads");

    // Each frame emits "AA" and reports the first pen step plus the laid-out text box height.
    struct SpacedFrame {
        float scale;
        u16 font_size;
        u16 spacing;
        u16 line_height;
        float pen_step;
        float box_height;
    };
    // Frames 1 and 2 resolve to the SAME physical size/spacing/line-height by different routes; frame 3 keeps
    // frame 2's authored values but runs at scale 1, so everything it resolves is half.
    struct SpacedFrame frames[] = {
        { .scale = 1.0f, .font_size = 20, .spacing = 6, .line_height = 40 },
        { .scale = 2.0f, .font_size = 10, .spacing = 3, .line_height = 20 },
        { .scale = 1.0f, .font_size = 10, .spacing = 3, .line_height = 20 },
    };

    for (u32 i = 0; i < sizeof(frames) / sizeof(frames[0]); i++) {
        ui_set_scale(frames[i].scale, frames[i].scale);
        ui_update_begin(ctx);
        fl_layout_start(ID("spaced_text_root"));
        fl_layout_configure((FlLayoutConfig) { .sizing = { .width = sizing_grow(), .height = sizing_grow() } });
        ui_text_styled(FL_S("AA"), .font_id = font, .font_size = frames[i].font_size,
                       .letter_spacing = frames[i].spacing, .line_height = frames[i].line_height,
                       .color = ui_color(255, 255, 255, 255));
        fl_layout_end();
        ui_update_end(ctx, 1.0f / 60.0f);

        ASSERT_TRUE_MSG(s_render.text_run_found, "the styled text must reach the frame as a text run");
        ASSERT_EQ_MSG((int)s_render.text_run_glyph_count, 2, "one glyph per codepoint");
        frames[i].pen_step = s_render.text_run_glyphs[1].x - s_render.text_run_glyphs[0].x;

        // The laid-out text box is only visible through the debug render-command snapshot.
        const int n = ui_get_render_command_count();
        bool box_found = false;
        for (int c = 0; c < n; c++) {
            UiRenderCommand cmd;
            if (ui_get_render_command(c, &cmd) && cmd.type == UiRenderCmd_Text) {
                frames[i].box_height = cmd.height;
                box_found = true;
                break;
            }
        }
        ASSERT_TRUE_MSG(box_found, "the text element must appear in the render-command snapshot");
    }

    ASSERT_TRUE_MSG(frames[0].pen_step > 0.0f, "the pen actually advances");
    ASSERT_NEAR_MSG(frames[1].pen_step, frames[0].pen_step, 0.0001f,
                    "10px + 3px spacing at scale 2 steps like 20px + 6px at scale 1");
    ASSERT_NEAR_MSG(frames[1].box_height, frames[0].box_height, 0.0001f,
                    "an authored 20px line height at scale 2 lays out like 40px at scale 1");
    ASSERT_TRUE_MSG(frames[2].pen_step < frames[1].pen_step - 0.0001f,
                    "the same authored values at scale 1 must step less, or the scale is not reaching the pen");
    ASSERT_TRUE_MSG(frames[2].box_height < frames[1].box_height - 0.0001f,
                    "and must lay out shorter, or the scale is not reaching the line box");
}

// A tracked single-word label is laid out as wide as its glyphs are painted: the element width equals the pen
// span (first glyph origin to the last glyph's advance end), and zero spacing leaves the plain advance sum.
UTEST(render, tracked_single_word_element_width_matches_the_painted_extent) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(1.0f, 1.0f);

    const u16 size = 15;
    FlFontId font = font_cache_load_font_memory(s_render.font_cache, k_glyph_test_ttf, k_glyph_test_ttf_size, size);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "the embedded test font loads");

    const u16 spacings[] = { 0, 5 };
    float element_width[2] = { 0 };
    float painted_extent[2] = { 0 };

    for (u32 i = 0; i < 2; i++) {
        ui_update_begin(ctx);
        fl_layout_start(ID("tracked_word_root"));
        fl_layout_configure((FlLayoutConfig) { .sizing = { .width = sizing_grow(), .height = sizing_grow() } });
        ui_text_styled(FL_S("AoA"), .font_id = font, .font_size = size, .letter_spacing = spacings[i],
                       .color = ui_color(255, 255, 255, 255));
        fl_layout_end();
        ui_update_end(ctx, 1.0f / 60.0f);

        ASSERT_TRUE_MSG(s_render.text_run_found, "the styled text must reach the frame as a text run");
        ASSERT_EQ_MSG((int)s_render.text_run_glyph_count, 3, "one glyph per codepoint");

        UiRenderCommand text;
        ASSERT_EQ_MSG(render_cmd_collect(UiRenderCmd_Text, &text, 1), 1, "the text element is in the snapshot");
        element_width[i] = text.width;

        const float last_advance
            = (float)font_cache_get_glyph_advance_q4(s_render.font_cache, font, font_cache_whole_px_to_q4(size), 'A')
              / 64.0f;
        painted_extent[i] = s_render.text_run_glyphs[2].x - s_render.text_run_glyphs[0].x + last_advance;
    }

    ASSERT_NEAR_MSG(element_width[0], painted_extent[0], 1.0f, "an untracked label fits its advance sum");
    ASSERT_NEAR_MSG(element_width[1], painted_extent[1], 1.0f, "a tracked label's box fits its painted ink");
    ASSERT_NEAR_MSG(element_width[1] - element_width[0], 10.0f, 1.0f, "two gaps of 5px separate the two widths");
}

// font_cache_resolve_size_px is the one home of the "size <= 0 means the face's load size" rule and the point
// the caller's type scale is applied. The refusals matter too: a scale of 0 reaching a draw path would
// quantize to a zero key and silently drop the run.
UTEST(font_cache, resolve_size_px_applies_the_scale_over_the_zero_fallback) {
    FlFontCache* cache = font_cache_new(s_arena);
    FlFontId font = font_cache_load_font_memory(cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "memory-loaded font must be ready synchronously");

    ASSERT_NEAR_MSG(font_cache_resolve_size_px(cache, font, 12.0f, 1.0f), 12.0f, 0.0001f,
                    "an explicit size at identity scale is itself");
    ASSERT_NEAR_MSG(font_cache_resolve_size_px(cache, font, 12.0f, 2.5f), 30.0f, 0.0001f,
                    "an explicit size is multiplied by the scale");
    ASSERT_NEAR_MSG(font_cache_resolve_size_px(cache, font, 0.0f, 1.0f), 32.0f, 0.0001f,
                    "size 0 falls back to the face's authored load size");
    ASSERT_NEAR_MSG(font_cache_resolve_size_px(cache, font, 0.0f, 2.0f), 64.0f, 0.0001f,
                    "the load-size fallback is scaled too, which is what stops a font freezing at load scale");
    ASSERT_NEAR_MSG(font_cache_resolve_size_px(cache, font, -1.0f, 2.0f), 64.0f, 0.0001f,
                    "a negative size takes the same fallback as 0");

    ASSERT_EQ_MSG(font_cache_resolve_size_px(cache, font, 12.0f, 0.0f), 0.0f, "a zero scale resolves to nothing");
    ASSERT_EQ_MSG(font_cache_resolve_size_px(cache, font, 12.0f, -2.0f), 0.0f, "so does a negative one");
    ASSERT_EQ_MSG(font_cache_resolve_size_px(cache, 9999, 12.0f, 2.0f), 0.0f, "an unknown font resolves to nothing");

    font_cache_destroy(cache);
}

// The same path with no pattern configured: the zeroed default must stay zeroed rather than picking up
// whatever the previous frame left in the Clay element config.
UTEST(render, layout_border_without_a_dash_pattern_stays_solid) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(1.0f, 1.0f);

    ui_update_begin(ctx);
    fl_layout_start(ID("solid_border_root"));
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { .width = sizing_grow(), .height = sizing_grow() },
    });
    fl_layout_start(ID("solid_border_child"));
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_fixed(100), sizing_fixed(100) },
        .border = { .color = ui_color(220, 200, 60, 255), .width = { .left = 2, .right = 2, .top = 2, .bottom = 2 } },
    });
    fl_layout_end();
    fl_layout_end();
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_TRUE_MSG(s_render.border_found, "a configured border must reach the frame as a border record");
    ASSERT_TRUE_MSG(s_render.submitted_border.stroke.dash_len == 0.0f
                        && s_render.submitted_border.stroke.gap_len == 0.0f,
                    "an unconfigured stroke stays solid all the way to the IR");
}

UTEST(render, translucent_fill_border_over) {
    FlArena* arena = arena_new();
    FlRenderBackend backend = create_software_renderer(arena, nullptr, FontRenderMode_Grayscale);
    FlSurface* surface = backend.create_surface(backend.user_data, 128, 128);
    FlArena* commands_arena = arena_new();

    const FlColor clear = { .r = 60, .g = 60, .b = 60, .a = 255 };
    const FlColor tA = { .r = 200, .g = 80, .b = 20, .a = 128 }; // ~50% alpha
    const FlColor tB = { .r = 40, .g = 160, .b = 220, .a = 96 }; // a second translucent color for the gradient
    FlPixelReadback buf;

    // --- Solid translucent fill: sample deep inside, away from the AA edges. ---
    {
        FlArena* ir = arena_new();
        fl_arena_align(ir, 8);
        const u8* start = (const u8*)fl_arena_current_pos(ir);
        const FlColor corners[4] = { tA, tA, tA, tA };
        fl_ir_fill_rect(ir, (FlRect) { 10.0f, 10.0f, 40.0f, 40.0f }, (FlCornerRadius) { 0 }, corners, nullptr);
        const size_t bytes = (size_t)((const u8*)fl_arena_current_pos(ir) - start);
        GpuDrawList g = gpu_ir_to_draw_cmds(commands_arena, start, bytes, 255, 128, 128, nullptr, nullptr, nullptr,
                                            nullptr, nullptr, nullptr);
        ASSERT_TRUE(g.count >= 1);
        ASSERT_TRUE_MSG(render_ir_to_surface(&backend, surface, &clear, start, bytes, &buf), "read_pixels failed");
        ASSERT_TRUE_MSG(px_matches_gpu_over(&buf, 30, 30, g.cmds[0].color[0], &clear, 2),
                        "translucent fill must composite straight-over (rgb·a + bg·(1−a)), not rgb·a²");
        fl_arena_destroy(ir);
    }

    // --- Square (radius-0) border: straight edges render through the fill path. Sample the top band. ---
    {
        FlArena* ir = arena_new();
        fl_arena_align(ir, 8);
        const u8* start = (const u8*)fl_arena_current_pos(ir);
        fl_ir_border(ir, (FlRect) { 70.0f, 10.0f, 40.0f, 40.0f }, 12.0f, 12.0f, 12.0f, 12.0f, (FlCornerRadius) { 0 },
                     tA);
        const size_t bytes = (size_t)((const u8*)fl_arena_current_pos(ir) - start);
        GpuDrawList g = gpu_ir_to_draw_cmds(commands_arena, start, bytes, 255, 128, 128, nullptr, nullptr, nullptr,
                                            nullptr, nullptr, nullptr);
        ASSERT_TRUE(g.count >= 1);
        ASSERT_TRUE_MSG(render_ir_to_surface(&backend, surface, &clear, start, bytes, &buf), "read_pixels failed");
        // Top edge band x=70..110, y=10..22; the GPU border packs the color in color[0].
        ASSERT_TRUE_MSG(px_matches_gpu_over(&buf, 90, 16, g.cmds[0].color[0], &clear, 2),
                        "translucent square border must composite straight-over");
        fl_arena_destroy(ir);
    }

    // --- Two-color gradient fill: differing corners take the RenderMask_ColorGradient path (interpolated then
    // composited per pixel). The left corners are tA, the right tB; the leftmost interior column is dominated
    // by tA (a half-texel of interpolation bleed toward tB), hence the ±3 tolerance. ---
    {
        FlArena* ir = arena_new();
        fl_arena_align(ir, 8);
        const u8* start = (const u8*)fl_arena_current_pos(ir);
        const FlColor corners[4] = { tA, tB, tA, tB }; // TL, TR, BL, BR
        fl_ir_fill_rect(ir, (FlRect) { 10.0f, 70.0f, 48.0f, 48.0f }, (FlCornerRadius) { 0 }, corners, nullptr);
        const size_t bytes = (size_t)((const u8*)fl_arena_current_pos(ir) - start);
        GpuDrawList g = gpu_ir_to_draw_cmds(commands_arena, start, bytes, 255, 128, 128, nullptr, nullptr, nullptr,
                                            nullptr, nullptr, nullptr);
        ASSERT_TRUE(g.count >= 1);
        ASSERT_TRUE_MSG(render_ir_to_surface(&backend, surface, &clear, start, bytes, &buf), "read_pixels failed");
        // color[0] = TL corner (tA); sample the leftmost interior column, dominated by tA.
        ASSERT_TRUE_MSG(px_matches_gpu_over(&buf, 11, 90, g.cmds[0].color[0], &clear, 3),
                        "translucent gradient corner must composite straight-over, not rgb·a²");
        fl_arena_destroy(ir);
    }

    // --- Rounded border: the corners render through process_8_pixels_border (dual-radius). A 24px-wide,
    // radius-24 corner is a filled quarter disk (inner radius 0), so a pixel well inside it is fully covered.
    // ±3 absorbs any sub-pixel corner-AA falloff. ---
    {
        FlArena* ir = arena_new();
        fl_arena_align(ir, 8);
        const u8* start = (const u8*)fl_arena_current_pos(ir);
        const FlCornerRadius r = { .tl = 24.0f, .tr = 24.0f, .bl = 24.0f, .br = 24.0f };
        fl_ir_border(ir, (FlRect) { 70.0f, 70.0f, 48.0f, 48.0f }, 24.0f, 24.0f, 24.0f, 24.0f, r, tA);
        const size_t bytes = (size_t)((const u8*)fl_arena_current_pos(ir) - start);
        GpuDrawList g = gpu_ir_to_draw_cmds(commands_arena, start, bytes, 255, 128, 128, nullptr, nullptr, nullptr,
                                            nullptr, nullptr, nullptr);
        ASSERT_TRUE(g.count >= 1);
        ASSERT_TRUE_MSG(render_ir_to_surface(&backend, surface, &clear, start, bytes, &buf), "read_pixels failed");
        // (88,88) is inside the top-left corner disk centered at (94,94), radius 24 - fully covered.
        ASSERT_TRUE_MSG(px_matches_gpu_over(&buf, 88, 88, g.cmds[0].color[0], &clear, 3),
                        "translucent rounded border corner must composite straight-over, not rgb·a²");
        fl_arena_destroy(ir);
    }

    backend.destroy_surface(backend.user_data, surface);
    if (backend.destroy != nullptr) {
        backend.destroy(backend.user_data);
    }
    fl_arena_destroy(commands_arena);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.primitives_and_background_blend - drives the software renderer over two frames and asserts pixels
// probed at positions the geometry fixes:
//   * "primitives" - the full primitive set (rect / rounded / border / clip / gradient / static-image /
//     dynamic-image RGBA + alpha / painter) over a single background image composited with alpha
//     over the clear color.
//   * "crossfade" - a background-only frame exercising the crossfade compositor, which is a distinct
//     path that does not read background alpha or the clear color.
//
// image_load_from_memory decodes on a jobs worker thread, so the test blocks on the image cache until the
// decode lands before rendering. That barrier is test-only.

// 4x4 solid-color truecolor PNGs (color type 2, 3 channels) used for the static
// image and the two background crossfade slots.
static const unsigned char k_png_red[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x04, 0x08, 0x02, 0x00, 0x00, 0x00, 0x26, 0x93, 0x09, 0x29, 0x00, 0x00, 0x00, 0x10, 0x49,
    0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0x38, 0xa1, 0x61, 0x04, 0x47, 0x0c, 0xc4, 0x71, 0x00, 0xe0, 0xe3, 0x12, 0x21,
    0x8b, 0x47, 0xbb, 0xdf, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

static const unsigned char k_png_blue[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x04, 0x08, 0x02, 0x00, 0x00, 0x00, 0x26, 0x93, 0x09, 0x29, 0x00, 0x00, 0x00, 0x10, 0x49,
    0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xd0, 0x70, 0x3b, 0x01, 0x47, 0x0c, 0xc4, 0x71, 0x00, 0xee, 0x03, 0x13, 0x61,
    0xe2, 0x06, 0x77, 0xde, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

static uint64_t fnv1a_64(const void* data, size_t size) {
    const uint8_t* bytes = (const uint8_t*)data;
    uint64_t hash = 1469598103934665603ULL; // FNV offset basis
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL; // FNV prime
    }
    return hash;
}

// Test-only barrier: block until an asynchronous in-memory decode has landed in the image cache.
static bool wait_image_ready(ImageHandle handle) {
    for (int i = 0; i < 5000; ++i) {
        if (image_cache_get_data(handle) != nullptr) {
            return true;
        }
        sleep_ms(1);
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlImageStatus wait_public_image_terminal(FlImageHandle handle) {
    FlImageStatus status = FlImageStatus_Pending;
    for (int i = 0; i < 5000 && status == FlImageStatus_Pending; ++i) {
        status = fl_image_status(handle);
        if (status == FlImageStatus_Pending) {
            sleep_ms(1);
        }
    }
    return status;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(render, public_mounted_image_load_and_status) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    vfs_test_register_driver(&s_stub_vfs_plugin);
    s_stub_vfs_override_data = k_png_red;
    s_stub_vfs_override_size = sizeof(k_png_red);

    FlVfsMountResult first_mount = vfs_mount(FL_S("/stub-image-a"));
    FlVfsMountResult second_mount = vfs_mount(FL_S("/stub-image-b"));
    ASSERT_EQ(first_mount.status, FlVfsMountErrorStatus_Success);
    ASSERT_EQ(second_mount.status, FlVfsMountErrorStatus_Success);
    vfs_wait_all();

    FlImageLoadOptions resize_options = { .target_width = 2, .target_height = 2, .preserve_aspect_ratio = true };
    FlImageHandle direct = fl_image_load_mount(first_mount.mount, FL_S(STUB_VFS_FILE), resize_options);
    ASSERT_NE_MSG(direct, FL_IMAGE_HANDLE_INVALID, "direct mounted load should start");
    // Whether the load has already completed is a race with the job system, so the fresh handle is only
    // required to be pollable and not already failed.
    FlImageStatus started = fl_image_status(direct);
    ASSERT_TRUE_MSG(started == FlImageStatus_Pending || started == FlImageStatus_Ready,
                    "new asynchronous load should be pending or already ready");

    FlImageHandle dedup = fl_image_load_mount(first_mount.mount, FL_S(STUB_VFS_FILE), resize_options);
    ASSERT_EQ_MSG(dedup, direct, "same mount/path/options should deduplicate");

    FlImageHandle composed = fl_image_load_mount(
        second_mount.mount, FL_S(STUB_VFS_FILE),
        (FlImageLoadOptions) { .target_width = 2, .target_height = 2, .preserve_aspect_ratio = true });
    ASSERT_NE_MSG(composed, FL_IMAGE_HANDLE_INVALID, "composed mounted load should start");
    ASSERT_NE_MSG(composed, direct, "the same relative path on different mounts must not deduplicate");

    ASSERT_EQ_MSG(wait_public_image_terminal(direct), FlImageStatus_Ready, "direct mounted load should realize");
    ASSERT_EQ_MSG(wait_public_image_terminal(composed), FlImageStatus_Ready, "composed mounted load should realize");
    ASSERT_EQ_MSG(fl_image_status(direct), FlImageStatus_Ready, "ready status should remain terminal");
    ImageResult resized = image_get_data((ImageHandle)direct);
    ASSERT_EQ_MSG(resized.width, 2, "target_width option should reach the decoder");
    ASSERT_EQ_MSG(resized.height, 2, "target_height option should reach the decoder");

    FlImageHandle failed = fl_image_load_mount(first_mount.mount, FL_S("missing.png"), (FlImageLoadOptions) { 0 });
    ASSERT_NE_MSG(failed, FL_IMAGE_HANDLE_INVALID, "a valid asynchronous request returns a pollable handle");
    ASSERT_EQ_MSG(wait_public_image_terminal(failed), FlImageStatus_Error, "missing path should resolve to error");
    ASSERT_EQ_MSG(fl_image_status(failed), FlImageStatus_Error, "error status should remain terminal");
    ASSERT_EQ_MSG(fl_image_status(FL_IMAGE_HANDLE_INVALID), FlImageStatus_Error, "invalid handle should report error");
    ASSERT_EQ_MSG(fl_image_status(UINT32_MAX - 1), FlImageStatus_Error,
                  "an arbitrary absent handle should report error, not pending");
    ASSERT_EQ_MSG(fl_image_load_mount(NULL, FL_S(STUB_VFS_FILE), (FlImageLoadOptions) { 0 }), FL_IMAGE_HANDLE_INVALID,
                  "null mount should fail synchronously");

    image_free((ImageHandle)direct);
    image_free((ImageHandle)composed);
    image_free((ImageHandle)failed);
    fl_vfs_mount_close(first_mount.mount);
    fl_vfs_mount_close(second_mount.mount);
    s_stub_vfs_override_data = NULL;
    s_stub_vfs_override_size = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void write_ppm(const char* path, const u8* rgb24, int width, int height) {
    FILE* f = fopen(path, "wb");
    if (!f) {
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", width, height);
    fwrite(rgb24, 1, (size_t)width * (size_t)height * 3u, f);
    fclose(f);
}

// Write a rendered frame to frame_dump_<label>.ppm when DUMP_FRAMES is set, so a deliberate rendering change
// can be reviewed as an image.
//
// The frames are never compared as a whole: every case asserts probes computed from the geometry.
static void dump_frame(const char* label, const FlPixelReadback* buf) {
    if (getenv("DUMP_FRAMES") == nullptr) {
        return;
    }

    char ppm_path[64];
    snprintf(ppm_path, sizeof ppm_path, "frame_dump_%s.ppm", label);
    write_ppm(ppm_path, (const u8*)buf->pixels, (int)buf->width, (int)buf->height);
}

static const u8* readback_rgb_at(const FlPixelReadback* buf, int x, int y) {
    return (const u8*)buf->pixels + (size_t)y * (size_t)buf->stride + (size_t)x * 3u;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static bool px_near(const FlPixelReadback* buf, int x, int y, FlColor want, int tol) {
    const u8* p = readback_rgb_at(buf, x, y);
    return abs((int)p[0] - (int)want.r) <= tol && abs((int)p[1] - (int)want.g) <= tol
           && abs((int)p[2] - (int)want.b) <= tol;
}

static void fill_dynamic_rgba(FlDynamicImage* img) {
    for (int y = 0; y < img->height; ++y) {
        u8* row = img->user_buffer + (size_t)y * (size_t)img->user_stride;
        for (int x = 0; x < img->width; ++x) {
            u8* px = row + (size_t)x * 4u;
            px[0] = (u8)(x * 6);
            px[1] = (u8)(y * 6);
            px[2] = (u8)(160);
            px[3] = 255;
        }
    }
}

static void fill_dynamic_alpha(FlDynamicImage* img) {
    for (int y = 0; y < img->height; ++y) {
        u8* row = img->user_buffer + (size_t)y * (size_t)img->user_stride;
        for (int x = 0; x < img->width; ++x) {
            row[x] = (u8)((x ^ y) * 8);
        }
    }
}

// The Tint background mode composites the decoded image right-aligned onto a wider canvas filled with the tint
// color, then blends a short tint->image gradient at the left seam. The right-alignment offset
// (canvas_w - image_width) feeds both the canvas blit and the gradient seam: the source must land flush
// against the right edge, with pure tint to the left.
UTEST(image_decode, tint_mode_right_aligns_source) {
    // Reference decode (no canvas) to learn the source pixels - k_png_red is a 4x4 RGB PNG.
    ImageDecodeOptions ref_opts = { .ui_scale = 1.0f };
    FlImageDecodeResult* src = image_decode(s_arena, k_png_red, sizeof(k_png_red), &ref_opts);
    ASSERT_TRUE_MSG(src != nullptr, "reference decode of k_png_red failed");
    ASSERT_EQ_MSG((int)src->channels, 3, "RGB source");
    ASSERT_EQ_MSG(src->width, 4, "source is 4px wide");

    // Tint onto an 8x4 canvas. Aspect-preserved keeps the 4x4 image 4px wide, so it right-aligns at x=4.
    ImageDecodeOptions opts = {
        .ui_scale = 1.0f,
        .target_width = 8,
        .target_height = 4,
        .preserve_aspect_ratio = true,
    };
    ImageThumbnailOptions thumb = {
        .background_mode = ImageDecodeBackgroundMode_Tint,
        .tint_r = 10,
        .tint_g = 200,
        .tint_b = 30,
    };
    FlImageDecodeResult* out = image_decode(s_arena, k_png_red, sizeof(k_png_red), &opts);
    ASSERT_TRUE_MSG(out != nullptr, "tint decode failed");
    ASSERT_TRUE_MSG(out->needs_thumbnail_compose, "raster decode is raw, pending the compose stage");
    image_thumbnail_compose(s_arena, out, &opts, &thumb);
    ASSERT_FALSE_MSG(out->needs_thumbnail_compose, "compose clears the pending flag");
    ASSERT_EQ_MSG(out->width, 8, "output spans the full canvas width");
    ASSERT_EQ_MSG(out->height, 4, "output spans the full canvas height");
    ASSERT_EQ_MSG((int)out->channels, 3, "tint canvas is RGB");

    // Left fill region (x < image_start_x = 4) is pure tint color, untouched by the gradient seam.
    const u8* p_left = out->pixels; // (0,0)
    ASSERT_EQ_MSG((int)p_left[0], 10, "left column is tint red");
    ASSERT_EQ_MSG((int)p_left[1], 200, "left column is tint green");
    ASSERT_EQ_MSG((int)p_left[2], 30, "left column is tint blue");

    // Rightmost canvas column (x=7) maps to source column 3 and lies past the gradient seam, so it equals the
    // source pixel exactly.
    const u8* p_right = out->pixels + 7 * 3;   // (7,0)
    const u8* src_right = src->pixels + 3 * 3; // source (3,0)
    ASSERT_EQ_MSG((int)p_right[0], (int)src_right[0], "right column keeps source red");
    ASSERT_EQ_MSG((int)p_right[1], (int)src_right[1], "right column keeps source green");
    ASSERT_EQ_MSG((int)p_right[2], (int)src_right[2], "right column keeps source blue");
}

// Center background mode letterboxes the aspect-preserved source onto a fill-colored canvas. With a 4x4
// source on an 8x4 canvas the source stays 4px wide and centers at offset_x=2, leaving black fill columns
// at the edges.
UTEST(image_decode, center_mode_letterboxes_source) {
    ImageDecodeOptions ref_opts = { .ui_scale = 1.0f };
    FlImageDecodeResult* src = image_decode(s_arena, k_png_red, sizeof(k_png_red), &ref_opts);
    ASSERT_TRUE_MSG(src != nullptr, "reference decode of k_png_red failed");
    ASSERT_EQ_MSG(src->width, 4, "source is 4px wide");

    ImageDecodeOptions opts = {
        .ui_scale = 1.0f,
        .target_width = 8,
        .target_height = 4,
        .preserve_aspect_ratio = true,
    };
    ImageThumbnailOptions thumb = {
        .background_mode = ImageDecodeBackgroundMode_Center,
        .tint_r = 0, // Center fills letterbox margins with black (matches the caller's policy)
        .tint_g = 0,
        .tint_b = 0,
    };
    FlImageDecodeResult* out = image_decode(s_arena, k_png_red, sizeof(k_png_red), &opts);
    ASSERT_TRUE_MSG(out != nullptr, "center decode failed");
    image_thumbnail_compose(s_arena, out, &opts, &thumb);
    ASSERT_EQ_MSG(out->width, 8, "output spans the full canvas width");
    ASSERT_EQ_MSG(out->height, 4, "output spans the full canvas height");
    ASSERT_EQ_MSG((int)out->channels, 3, "center canvas is RGB");

    // Left margin (x=0, before the centered source at x=2) is the black fill color.
    const u8* p_left = out->pixels; // (0,0)
    ASSERT_EQ_MSG((int)p_left[0], 0, "left margin is black fill R");
    ASSERT_EQ_MSG((int)p_left[1], 0, "left margin is black fill G");
    ASSERT_EQ_MSG((int)p_left[2], 0, "left margin is black fill B");

    // Canvas column 3 sits inside the centered source (offset_x=2) and maps to source column 1.
    const u8* p_mid = out->pixels + 3 * 3;
    const u8* src_mid = src->pixels + 1 * 3;
    ASSERT_EQ_MSG((int)p_mid[0], (int)src_mid[0], "centered source keeps red");
    ASSERT_EQ_MSG((int)p_mid[1], (int)src_mid[1], "centered source keeps green");
    ASSERT_EQ_MSG((int)p_mid[2], (int)src_mid[2], "centered source keeps blue");
}

// A 4x4 binary-grayscale PGM (stb PNM decodes P5 to a single channel), feeding the channel-agnostic compose
// path end-to-end.
static const u8 k_pgm_gray4x4[] = {
    'P',  '5',  '\n', '4',  ' ', '4', '\n', '2', '5', '5', '\n', // header: P5 4x4 maxval 255
    0x10, 0x20, 0x30, 0x40,                                      // row 0
    0x50, 0x60, 0x70, 0x80,                                      // row 1
    0x90, 0xA0, 0xB0, 0xC0,                                      // row 2
    0xD0, 0xE0, 0xF0, 0xFF,                                      // row 3
};

// compose_source_into_canvas_rgb must not read past a source pixel's actual channel data: for a 1-channel
// source it replicates channel 0 into R/G/B. The source is exactly width*height bytes, so R==G==B==gray also
// pins that no stray neighbour byte leaked into G/B.
UTEST(image_decode, compose_grayscale_source_replicates_gray) {
    const u8 gray[6] = { 11, 22, 33, 44, 55, 66 }; // 3x2, 1 channel, stride 3
    InternalDecodeData img = {
        .data = (u8*)gray,
        .width = 3,
        .height = 2,
        .stride = 3,
        .channels = 1,
    };

    u8 canvas[3 * 2 * 3];
    composite_source_into_canvas_rgb(&img, canvas, 3, 2, 5, 5, 5, 0, 0);

    for (int i = 0; i < 3 * 2; i++) {
        ASSERT_EQ_MSG((int)canvas[i * 3 + 0], (int)gray[i], "grayscale value replicated into R");
        ASSERT_EQ_MSG((int)canvas[i * 3 + 1], (int)gray[i], "grayscale value replicated into G");
        ASSERT_EQ_MSG((int)canvas[i * 3 + 2], (int)gray[i], "grayscale value replicated into B");
    }
}

// Same guarantee for a 2-channel (grayscale + alpha) source: the gray value is channel 0, alpha is channel 1
// and must be dropped, not read into G/B. Source is exactly width*height*2 bytes.
UTEST(image_decode, compose_gray_alpha_source_ignores_alpha) {
    const u8 ga[6] = { 100, 255, 150, 128, 200, 0 }; // 3x1, 2 channels (gray, alpha), stride 6
    InternalDecodeData img = {
        .data = (u8*)ga,
        .width = 3,
        .height = 1,
        .stride = 6,
        .channels = 2,
    };

    u8 canvas[3 * 1 * 3];
    composite_source_into_canvas_rgb(&img, canvas, 3, 1, 0, 0, 0, 0, 0);

    const int expected_gray[3] = { 100, 150, 200 }; // channel-0 gray of each pixel; alpha ignored
    for (int i = 0; i < 3; i++) {
        ASSERT_EQ_MSG((int)canvas[i * 3 + 0], expected_gray[i], "gray-alpha: channel 0 into R");
        ASSERT_EQ_MSG((int)canvas[i * 3 + 1], expected_gray[i], "gray-alpha: channel 0 into G (not alpha)");
        ASSERT_EQ_MSG((int)canvas[i * 3 + 2], expected_gray[i], "gray-alpha: channel 0 into B (not alpha)");
    }
}

// The black-border predicates once handled only 4/3/1 channels and fell through to "all black" for anything
// else. stb hands back 2 channels for a grey+alpha PNG when force_rgb is off, so a non-black GA row must
// report false on its grey byte, and a high alpha byte must not make a black row report non-black.
UTEST(image_decode, black_predicates_read_gray_alpha_channel) {
    const u8 bright_ga[6] = { 200, 0, 200, 0, 200, 0 }; // 3x1 GA: bright grey, zero alpha
    ASSERT_FALSE_MSG(image_decode_test_is_row_all_black(bright_ga, 3, 2),
                     "a bright grey+alpha row must not report all-black");

    const u8 black_ga[6] = { 0, 255, 0, 255, 0, 255 }; // 3x1 GA: black grey, opaque alpha
    ASSERT_TRUE_MSG(image_decode_test_is_row_all_black(black_ga, 3, 2),
                    "a black grey+alpha row must report all-black regardless of alpha");

    ASSERT_FALSE_MSG(image_decode_test_is_column_all_black(bright_ga, 1, 0, 1, 6, 2),
                     "a bright grey+alpha column must not report all-black");
    ASSERT_TRUE_MSG(image_decode_test_is_column_all_black(black_ga, 1, 0, 1, 6, 2),
                    "a black grey+alpha column must report all-black regardless of alpha");
}

// When a resize fails to shrink an oversized source, the Tint/Center offsets go negative. The clamped blit
// must crop the source to the canvas and touch nothing outside it; guard bytes flanking a 2x2 canvas catch
// any out-of-bounds write.
UTEST(image_decode, compose_oversized_source_clamps_to_canvas) {
    enum { CW = 2, CH = 2, CANVAS_BYTES = CW * CH * 3, GUARD = 8 };

    // 4x4 RGB source, each pixel a distinct value keyed off its linear index.
    u8 src[4 * 4 * 3];
    for (int i = 0; i < 4 * 4; i++) {
        src[i * 3 + 0] = (u8)(i * 3 + 0);
        src[i * 3 + 1] = (u8)(i * 3 + 1);
        src[i * 3 + 2] = (u8)(i * 3 + 2);
    }
    InternalDecodeData img = {
        .data = src,
        .width = 4,
        .height = 4,
        .stride = 4 * 3,
        .channels = 3,
    };

    // Guarded buffer: [GUARD 0xAB | canvas | GUARD 0xAB]. canvas points into the middle.
    u8 buf[GUARD + CANVAS_BYTES + GUARD];
    for (int i = 0; i < (int)sizeof(buf); i++) {
        buf[i] = 0xAB;
    }
    u8* canvas = buf + GUARD;

    // offset (-1,-1) mimics a Center offset for a source larger than the canvas ((2-4)/2). dst_x = -1 + x, so
    // x==0 falls before the canvas (dropped), x==3 falls past it (dropped), x==1/2 map to canvas cols 0/1.
    composite_source_into_canvas_rgb(&img, canvas, CW, CH, 7, 8, 9, -1, -1);

    for (int i = 0; i < GUARD; i++) {
        ASSERT_EQ_MSG((int)buf[i], 0xAB, "leading guard byte untouched (no OOB write before canvas)");
        ASSERT_EQ_MSG((int)buf[GUARD + CANVAS_BYTES + i], 0xAB,
                      "trailing guard byte untouched (no OOB write after canvas)");
    }

    // Content check: canvas (col,row) == source (col+1,row+1). Confirms the surviving region is the correct crop.
    for (int row = 0; row < CH; row++) {
        for (int col = 0; col < CW; col++) {
            int src_idx = (row + 1) * 4 + (col + 1);
            const u8* c = canvas + (row * CW + col) * 3;
            ASSERT_EQ_MSG((int)c[0], (int)src[src_idx * 3 + 0], "cropped source R lands at the clamped offset");
            ASSERT_EQ_MSG((int)c[1], (int)src[src_idx * 3 + 1], "cropped source G lands at the clamped offset");
            ASSERT_EQ_MSG((int)c[2], (int)src[src_idx * 3 + 2], "cropped source B lands at the clamped offset");
        }
    }
}

// End-to-end companion to the direct compose tests: a real 1-channel decode (P5 PGM -> channels==1) must
// survive the Center compose path without reading past the source.
UTEST(image_decode, compose_grayscale_decode_center_no_overread) {
    ImageDecodeOptions ref_opts = { .ui_scale = 1.0f };
    FlImageDecodeResult* src = image_decode(s_arena, k_pgm_gray4x4, sizeof(k_pgm_gray4x4), &ref_opts);
    ASSERT_TRUE_MSG(src != nullptr, "reference decode of k_pgm_gray4x4 failed");
    ASSERT_EQ_MSG((int)src->channels, 1, "P5 PGM decodes to a single grayscale channel");
    ASSERT_EQ_MSG(src->width, 4, "grayscale source is 4px wide");

    ImageDecodeOptions opts = {
        .ui_scale = 1.0f,
        .target_width = 8,
        .target_height = 4,
        .preserve_aspect_ratio = true,
    };
    ImageThumbnailOptions thumb = {
        .background_mode = ImageDecodeBackgroundMode_Center,
        .tint_r = 1,
        .tint_g = 2,
        .tint_b = 3,
    };
    FlImageDecodeResult* out = image_decode(s_arena, k_pgm_gray4x4, sizeof(k_pgm_gray4x4), &opts);
    ASSERT_TRUE_MSG(out != nullptr, "grayscale center decode failed");
    image_thumbnail_compose(s_arena, out, &opts, &thumb);
    ASSERT_EQ_MSG(out->width, 8, "output spans the full canvas width");
    ASSERT_EQ_MSG((int)out->channels, 3, "compose canvas is RGB");

    // Left margin (x=0, before the centered 4px source at offset_x=2) is the fill color.
    const u8* p_left = out->pixels;
    ASSERT_EQ_MSG((int)p_left[0], 1, "left margin is fill R");
    ASSERT_EQ_MSG((int)p_left[1], 2, "left margin is fill G");
    ASSERT_EQ_MSG((int)p_left[2], 3, "left margin is fill B");

    // Canvas column 2 is the first centered source column; a grayscale source yields R==G==B there.
    const u8* p_src0 = out->pixels + 2 * 3;
    ASSERT_EQ_MSG((int)p_src0[0], (int)p_src0[1], "centered grayscale pixel: R==G");
    ASSERT_EQ_MSG((int)p_src0[1], (int)p_src0[2], "centered grayscale pixel: G==B");
    ASSERT_EQ_MSG((int)p_src0[0], (int)src->pixels[0], "centered pixel matches decoded gray value");
}

// The vertical fade pass needs a 3-channel image (force_rgb provides it) and blends each row from the
// original color at the top toward the target color at the bottom. With fade_start=0 the top row stays
// original and the bottom row reaches the target color.
UTEST(image_decode, vertical_fade_blends_toward_target) {
    ImageDecodeOptions ref_opts = { .ui_scale = 1.0f };
    FlImageDecodeResult* src = image_decode(s_arena, k_png_red, sizeof(k_png_red), &ref_opts);
    ASSERT_TRUE_MSG(src != nullptr, "reference decode of k_png_red failed");

    ImageDecodeOptions opts = { .ui_scale = 1.0f, .force_rgb = true };
    ImageThumbnailOptions thumb = {
        .apply_vertical_fade = true,
        .fade_start = 0.0f,
        .fade_r = 0,
        .fade_g = 0,
        .fade_b = 255, // fade toward blue
    };
    FlImageDecodeResult* out = image_decode(s_arena, k_png_red, sizeof(k_png_red), &opts);
    ASSERT_TRUE_MSG(out != nullptr, "fade decode failed");
    ASSERT_EQ_MSG((int)out->channels, 3, "force_rgb yields a 3-channel decode for the fade pass");
    image_thumbnail_compose(s_arena, out, &opts, &thumb);
    ASSERT_EQ_MSG((int)out->channels, 3, "faded image stays RGB");

    // Top row keeps the source color (blend factor full); bottom row is pulled toward the blue target.
    const u8* top = out->pixels;                                      // (0,0)
    const u8* bottom = out->pixels + (out->height - 1) * out->stride; // (0, h-1)
    ASSERT_EQ_MSG((int)top[0], (int)src->pixels[0], "top row keeps the original red");
    ASSERT_TRUE_MSG((int)bottom[2] > (int)top[2], "bottom row blends toward the blue fade target");
}

// ImageDecodeResizeFilter is load-time resampling, chosen per load and applied once in the compose stage -
// entirely separate from the draw-time FlImageFilter the render command carries. Upscaling a hard black/white
// edge separates the two: the default filter interpolates across it, point sampling can only reproduce the two
// source values.
UTEST(image_decode, resize_filter_selects_the_load_time_resampler) {
    u8 src_pixels[2 * 2 * 3];
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            u8* px = src_pixels + ((size_t)y * 2 + (size_t)x) * 3;
            px[0] = px[1] = px[2] = x == 0 ? 0 : 255;
        }
    }

    const ImageDecodeResizeFilter filters[2] = { ImageDecodeResizeFilter_Default, ImageDecodeResizeFilter_Nearest };
    int intermediates[2] = { 0, 0 };

    for (int f = 0; f < 2; ++f) {
        FlImageDecodeResult result = {
            .pixels = src_pixels,
            .width = 2,
            .height = 2,
            .stride = 2 * 3,
            .channels = 3,
            .premultiplied = false,
            .needs_thumbnail_compose = true,
        };
        ImageDecodeOptions opts = {
            .ui_scale = 1.0f,
            .target_width = 8,
            .target_height = 8,
            .resize_filter = filters[f],
        };
        ImageThumbnailOptions thumb = { .background_mode = ImageDecodeBackgroundMode_Regular };
        image_thumbnail_compose(s_arena, &result, &opts, &thumb);

        ASSERT_EQ_MSG(result.width, 8, "compose resized to the requested width");
        ASSERT_EQ_MSG(result.height, 8, "compose resized to the requested height");

        const u8* row = result.pixels + (size_t)4 * result.stride;
        for (int x = 0; x < 8; ++x) {
            if (row[x * 3] > 40 && row[x * 3] < 215) {
                intermediates[f]++;
            }
        }
    }

    ASSERT_TRUE_MSG(intermediates[0] > 0, "the default resize filter interpolates across the edge");
    ASSERT_EQ_MSG(intermediates[1], 0, "the nearest resize filter point-samples, producing no new values");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define CROP_GRAY_BORDER 2
#define CROP_GRAY_INNER 8
#define CROP_GRAY_SIZE (CROP_GRAY_INNER + 2 * CROP_GRAY_BORDER)
#define CROP_GRAY_VALUE 100

// RGBA image: opaque black border around an opaque gray interior. When `odd_last_row` is set, the bottom interior
// row carries one colored pixel.
static void fill_black_bordered_gray(u8* pixels, bool odd_last_row) {
    for (int y = 0; y < CROP_GRAY_SIZE; ++y) {
        for (int x = 0; x < CROP_GRAY_SIZE; ++x) {
            bool inside = x >= CROP_GRAY_BORDER && x < CROP_GRAY_SIZE - CROP_GRAY_BORDER && y >= CROP_GRAY_BORDER
                          && y < CROP_GRAY_SIZE - CROP_GRAY_BORDER;
            u8 v = inside ? CROP_GRAY_VALUE : 0;
            u8* px = pixels + ((size_t)y * CROP_GRAY_SIZE + (size_t)x) * 4;
            px[0] = px[1] = px[2] = v;
            px[3] = 255;
        }
    }

    if (odd_last_row) {
        u8* px = pixels + ((size_t)(CROP_GRAY_SIZE - CROP_GRAY_BORDER - 1) * CROP_GRAY_SIZE + CROP_GRAY_BORDER) * 4;
        px[1] = 10;
    }
}

static FlImageDecodeResult compose_black_bordered(u8* pixels, int target) {
    FlImageDecodeResult result = {
        .pixels = pixels,
        .width = CROP_GRAY_SIZE,
        .height = CROP_GRAY_SIZE,
        .stride = CROP_GRAY_SIZE * 4,
        .channels = 4,
        .needs_thumbnail_compose = true,
    };
    ImageDecodeOptions opts = {
        .ui_scale = 1.0f,
        .target_width = target,
        .target_height = target,
        .detect_grayscale = true,
    };
    ImageThumbnailOptions thumb = { .detect_black_border = true };
    image_thumbnail_compose(s_arena, &result, &opts, &thumb);
    return result;
}

// The black-border crop is zero-copy, so the cropped image keeps the parent's row stride. Grayscale detection and
// conversion must walk rows by that stride rather than treating the interior as packed.
UTEST(image_decode, crop_then_grayscale_honors_stride) {
    u8 pixels[CROP_GRAY_SIZE * CROP_GRAY_SIZE * 4];
    fill_black_bordered_gray(pixels, false);

    FlImageDecodeResult result = compose_black_bordered(pixels, 0);

    ASSERT_EQ_MSG(result.width, CROP_GRAY_INNER, "border cropped");
    ASSERT_EQ_MSG(result.height, CROP_GRAY_INNER, "border cropped");
    ASSERT_EQ_MSG((int)result.channels, 1, "gray interior converts to alpha only");
    for (int i = 0; i < CROP_GRAY_INNER * CROP_GRAY_INNER; ++i) {
        ASSERT_TRUE_MSG(abs((int)result.pixels[i] - CROP_GRAY_VALUE) <= 1,
                        "alpha comes from the interior, not the border");
    }
}

UTEST(image_decode, crop_then_color_row_refuses_grayscale) {
    u8 pixels[CROP_GRAY_SIZE * CROP_GRAY_SIZE * 4];
    fill_black_bordered_gray(pixels, true);

    FlImageDecodeResult result = compose_black_bordered(pixels, 0);

    ASSERT_EQ_MSG(result.width, CROP_GRAY_INNER, "border cropped");
    ASSERT_EQ_MSG((int)result.channels, 4, "a colored pixel in the last interior row keeps the image RGBA");
}

UTEST(image_decode, crop_then_resize_grayscale_stays_correct) {
    u8 pixels[CROP_GRAY_SIZE * CROP_GRAY_SIZE * 4];
    fill_black_bordered_gray(pixels, false);

    FlImageDecodeResult result = compose_black_bordered(pixels, 4);

    ASSERT_EQ_MSG(result.width, 4, "resized to the target");
    ASSERT_EQ_MSG((int)result.channels, 1, "gray interior converts to alpha only");
    for (int i = 0; i < 4 * 4; ++i) {
        ASSERT_TRUE_MSG(abs((int)result.pixels[i] - CROP_GRAY_VALUE) <= 1, "resized alpha matches the interior");
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(image_decode, checked_pixel_size_rejects_unrepresentable_sizes) {
    size_t size = 0;
    ASSERT_TRUE(image_checked_pixel_size(3, 2, 4, &size));
    ASSERT_EQ((size_t)24, size);
    ASSERT_FALSE(image_checked_pixel_size(0, 2, 4, &size));
    ASSERT_FALSE(image_checked_pixel_size(2, -1, 4, &size));
    ASSERT_FALSE(image_checked_pixel_size(2, 2, 0, &size));
    ASSERT_FALSE(image_checked_pixel_size(INT_MAX, INT_MAX, SIZE_MAX / 4, &size));

#if SIZE_MAX > UINT32_MAX
    // Larger than any int product, still representable: the full byte count comes back.
    ASSERT_TRUE(image_checked_pixel_size(50000, 50000, 4, &size));
    ASSERT_EQ((size_t)10000000000u, size);
#endif

    int stride = 0;
    ASSERT_TRUE(image_checked_stride(INT_MAX / 4, 4, &stride));
    ASSERT_EQ((INT_MAX / 4) * 4, stride);
    ASSERT_FALSE(image_checked_stride(INT_MAX / 4 + 1, 4, &stride));
}

UTEST(image_decode, dynamic_image_refuses_stride_overflow) {
    ASSERT_TRUE(dynamic_image_create(INT_MAX / 4 + 1, 1, s_arena) == nullptr);
    FlDynamicImage* ok = dynamic_image_create(8, 4, s_arena);
    ASSERT_TRUE(ok != nullptr);
    ASSERT_EQ(8 * 4, ok->user_stride);
}

static const char k_svg_declared_huge[]
    = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40000\" height=\"40000\">"
      "<rect x=\"0\" y=\"0\" width=\"40000\" height=\"40000\" fill=\"#ff0000\"/></svg>";

static const char k_svg_at_cap[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8192\" height=\"16\">"
                                   "<rect x=\"0\" y=\"0\" width=\"8192\" height=\"16\" fill=\"#ff0000\"/></svg>";

static const char k_svg_over_cap[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"8193\" height=\"16\">"
                                     "<rect x=\"0\" y=\"0\" width=\"8193\" height=\"16\" fill=\"#ff0000\"/></svg>";

// A hostile declared size is rejected before any rasterizer or pixel allocation exists.
UTEST(image_decode, svg_rejects_absurd_declared_size) {
    ImageDecodeOptions opts = { .ui_scale = 1.0f };
    ASSERT_TRUE(image_decode(s_arena, k_svg_declared_huge, sizeof(k_svg_declared_huge) - 1, &opts) == nullptr);
    ASSERT_TRUE(image_decode(s_arena, k_svg_over_cap, sizeof(k_svg_over_cap) - 1, &opts) == nullptr);
}

UTEST(image_decode, svg_at_size_cap_still_decodes) {
    ImageDecodeOptions opts = { .ui_scale = 1.0f };
    FlImageDecodeResult* out = image_decode(s_arena, k_svg_at_cap, sizeof(k_svg_at_cap) - 1, &opts);
    ASSERT_TRUE(out != nullptr);
    ASSERT_EQ(8192, out->width);
}

// The cap applies to the final raster, so a huge declared size scaled down by a target still decodes.
UTEST(image_decode, svg_huge_declared_size_with_small_target_decodes) {
    ImageDecodeOptions opts
        = { .ui_scale = 1.0f, .target_width = 32, .target_height = 32, .preserve_aspect_ratio = true };
    FlImageDecodeResult* out = image_decode(s_arena, k_svg_declared_huge, sizeof(k_svg_declared_huge) - 1, &opts);
    ASSERT_TRUE(out != nullptr);
    ASSERT_EQ(32, out->width);
}

UTEST(image_decode, svg_refuses_oversized_target) {
    ImageDecodeOptions opts = { .ui_scale = 1.0f, .target_width = INT_MAX / 4 + 1, .target_height = 1 };
    ASSERT_TRUE(image_decode(s_arena, k_svg_gradient, sizeof(k_svg_gradient) - 1, &opts) == nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// SVG decodes are final: image_decode leaves needs_thumbnail_compose false and the compose stage must be a
// no-op for them (even when compositing flags like detect_black_border are requested).
UTEST(image_decode, svg_skips_thumbnail_compose) {
    ImageDecodeOptions opts = { .ui_scale = 1.0f, .target_width = 16, .target_height = 16 };
    FlImageDecodeResult* out = image_decode(s_arena, k_svg_gradient, sizeof(k_svg_gradient) - 1, &opts);
    ASSERT_TRUE_MSG(out != nullptr, "svg decode failed");
    ASSERT_FALSE_MSG(out->needs_thumbnail_compose, "svg decode is final, no compose pending");

    int w = out->width;
    int h = out->height;
    int ch = out->channels;
    const u8* px = out->pixels;

    ImageThumbnailOptions thumb = { .detect_black_border = true, .background_mode = ImageDecodeBackgroundMode_Center };
    image_thumbnail_compose(s_arena, out, &opts, &thumb);
    ASSERT_EQ_MSG(out->width, w, "compose left svg width untouched");
    ASSERT_EQ_MSG(out->height, h, "compose left svg height untouched");
    ASSERT_EQ_MSG((int)out->channels, ch, "compose left svg channels untouched");
    ASSERT_TRUE_MSG(out->pixels == px, "compose left svg pixels untouched");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.image_pool_evict_self_heals - the image ownership split. The SW realized-pixel pool is owned by the
// software backend and its residency is invisible to the core readiness registry: a realized image can drop
// out of the pool and be re-realized later without the core ever observing a "ready-but-not-resident" state.
//
// The headless harness has no VFS mount, so it cannot drive the real async reload; the test hooks drive only
// the pool residency transition.

UTEST(render, image_pool_evict_self_heals) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ImageHandle handle = image_load_from_memory(k_png_red, sizeof(k_png_red), (FlImageLoadOptions) { 0 });
    ASSERT_TRUE_MSG(handle != IMAGE_HANDLE_INVALID, "image_load_from_memory returned invalid handle");
    ASSERT_TRUE_MSG(wait_image_ready(handle), "image never realized into the SW pool");

    ASSERT_TRUE_MSG(image_cache_get_data(handle) != nullptr, "pixels should be resident after realize");
    ASSERT_TRUE_MSG(fl_image_is_ready(handle), "core registry should report ready after realize");
    ASSERT_EQ_MSG((int)image_get_status(image_get_data(handle)), (int)ImageStatus_Ready,
                  "an acknowledged memory load must report ready, not pending or failed");

    // Evict the pixels from the pool (no longer resident), keeping the entry so the test can re-make it resident.
    void* entry = image_cache_test_evict_keep(handle);
    ASSERT_TRUE_MSG(entry != nullptr, "expected a resident entry to evict");

    ASSERT_TRUE_MSG(image_cache_get_data(handle) == nullptr, "pixels should not be resident after eviction");
    ASSERT_TRUE_MSG(fl_image_is_ready(handle), "core readiness must survive a SW pool eviction");

    // The miss self-heals: the image becomes resident again, as the reload completing would make it.
    ASSERT_TRUE_MSG(image_cache_test_restore(handle, entry), "restore failed");
    ASSERT_TRUE_MSG(image_cache_get_data(handle) != nullptr, "pixels should be resident again after reload");
    ASSERT_TRUE_MSG(fl_image_is_ready(handle), "core readiness unchanged across the whole cycle");

    image_free(handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.image_status_realized_after_unready_poll - a memory load that is realized and out of the active set
// when its load state is resolved reads as ready, never error.

UTEST(render, image_status_realized_after_unready_poll) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const ImageHandle handle = (ImageHandle)0x80ffe001u;
    image_registry_mark_realized(handle, 4, 4);

    ImageResult result = image_resolve_unrealized(handle);
    ASSERT_EQ_MSG((int)image_get_status(result), (int)ImageStatus_Ready,
                  "a handle realized after the first readiness check must not report error");
    ASSERT_EQ(result.width, 4);

    image_registry_remove(handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.image_load_from_memory_bad_data_reports_error - a failed in-memory decode has no VFS handle to carry
// an error status, so the failure must be recorded against the request id (memory_load_job) and surface from
// the poll path as an error rather than pending forever.

UTEST(render, image_load_from_memory_bad_data_reports_error) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // Not a valid header for any supported format; the decode job fails immediately.
    static const u8 k_garbage[]
        = { 0xde, 0xad, 0xbe, 0xef, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b };
    ImageHandle handle = image_load_from_memory(k_garbage, sizeof(k_garbage), (FlImageLoadOptions) { 0 });
    ASSERT_TRUE_MSG(handle != IMAGE_HANDLE_INVALID, "image_load_from_memory returned invalid handle");

    // Bounded poll: the load must leave pending within the window, and only ever to the error state.
    ImageStatus status = ImageStatus_Pending;
    for (int i = 0; i < 5000 && status == ImageStatus_Pending; ++i) {
        status = image_get_status(image_get_data(handle));
        if (status == ImageStatus_Pending) {
            sleep_ms(1);
        }
    }
    ASSERT_EQ_MSG((int)status, (int)ImageStatus_Error,
                  "corrupt in-memory load must resolve to error, not pend forever");
    ASSERT_FALSE_MSG(fl_image_is_ready(handle), "corrupt in-memory load must never report ready");

    // Repeat polls stay in the error state (the record persists until the handle is freed).
    ASSERT_EQ_MSG((int)image_get_status(image_get_data(handle)), (int)ImageStatus_Error,
                  "error status must be stable across polls");

    image_free(handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.image_cache_defer_free_drains_at_frame_boundary - the image cache's eviction/free paths run on a job
// worker thread and must not tlsf_free pixels the render thread may still be reading through an ImageData* it
// obtained via image_cache_get_data earlier in the frame. Freeing is deferred onto a pending list and drained
// by image_cache_update, which runs at the top of render() on the render thread before any of the frame's
// get_data calls.

UTEST(render, image_cache_defer_free_drains_at_frame_boundary) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ImageCache* cache = image_cache_get_sys();
    ASSERT_TRUE_MSG(cache != nullptr, "image cache singleton not initialized");

    // Drain anything earlier render cases left pending so the count deltas below are unambiguous.
    image_cache_update(cache, 1);
    ASSERT_EQ_MSG((int)image_cache_test_pending_free_count(), 0, "pending free list should start empty");

    ImageHandle handle = image_load_from_memory(k_png_red, sizeof(k_png_red), (FlImageLoadOptions) { 0 });
    ASSERT_TRUE_MSG(handle != IMAGE_HANDLE_INVALID, "image_load_from_memory returned invalid handle");
    ASSERT_TRUE_MSG(wait_image_ready(handle), "image never realized into the SW pool");

    ImageData* data = image_cache_get_data(handle);
    ASSERT_TRUE_MSG(data != nullptr, "pixels should be resident after realize");
    ASSERT_TRUE_MSG(data->data != nullptr, "resident entry carries pixel data");

    // Snapshot a byte the render thread would read; the free below must not return this block to TLSF yet.
    u8* pixels = (u8*)data->data;
    u8 first_byte = pixels[0];

    // free_handle is one of the two job-thread free sites: it must queue the block, not tlsf_free it immediately.
    image_cache_free_handle(handle);
    ASSERT_TRUE_MSG(image_cache_test_pending_free_count() > 0, "free_handle should defer, not free immediately");
    ASSERT_EQ_MSG((int)pixels[0], (int)first_byte, "deferred block must stay intact until the frame-boundary drain");

    // Frame boundary on the render thread: image_cache_update drains the queue and returns the block to TLSF.
    image_cache_update(cache, 2);
    ASSERT_EQ_MSG((int)image_cache_test_pending_free_count(), 0, "image_cache_update must drain the pending list");

    // The reclaimed space is usable again: a fresh same-size load realizes into the pool.
    ImageHandle reload = image_load_from_memory(k_png_red, sizeof(k_png_red), (FlImageLoadOptions) { 0 });
    ASSERT_TRUE_MSG(reload != IMAGE_HANDLE_INVALID, "reload after drain returned invalid handle");
    ASSERT_TRUE_MSG(wait_image_ready(reload), "same-size alloc should succeed after the drain reclaims TLSF space");
    ASSERT_TRUE_MSG(image_cache_get_data(reload) != nullptr, "reloaded pixels should be resident");

    image_free(reload);
    image_free(handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// utest's ASSERT_* macros return from the case, so a mid-body failure would otherwise strand realized images in
// the fixed-size SW pixel pool. Clearing the background first keeps the compositor from holding a freed handle.
typedef struct RenderImages {
    ImageHandle fg;
    ImageHandle bg_curr;
    ImageHandle bg_prev;
} RenderImages;

static void render_images_cleanup(RenderImages* imgs) {
    ui_background_set(IMAGE_HANDLE_INVALID);
    const ImageHandle handles[] = { imgs->fg, imgs->bg_curr, imgs->bg_prev };
    for (size_t i = 0; i < sizeof(handles) / sizeof(handles[0]); ++i) {
        if (handles[i] != IMAGE_HANDLE_INVALID) {
            image_free(handles[i]);
        }
    }
}

#define render_images_auto(name)                                      \
    __attribute__((cleanup(render_images_cleanup))) RenderImages name \
        = { .fg = IMAGE_HANDLE_INVALID, .bg_curr = IMAGE_HANDLE_INVALID, .bg_prev = IMAGE_HANDLE_INVALID }

UTEST(render, primitives_and_background_blend) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // Identity scale (ui_set_scale 1.0) so painter/Clay coordinates map 1:1 to the offscreen buffer. The
    // renderer is already 1920x1080.
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    ASSERT_TRUE_MSG(ctx->ui_scale == 1.0f, "expected identity ui_scale 1.0");

    // Foreground images load as Regular (the SW backend deinterleaves them into the planar form the texture
    // sampler expects). Background images load with a background_mode so the SW backend keeps them
    // interleaved RGB, which is what the background compositor (copy/crossfade) reads.
    render_images_auto(imgs);
    imgs.fg = image_load_from_memory(k_png_red, sizeof(k_png_red), (FlImageLoadOptions) { 0 });
    imgs.bg_curr = image_load_from_memory(k_png_red, sizeof(k_png_red),
                                          (FlImageLoadOptions) { .background_mode = FlBackgroundMode_Center,
                                                                 .target_width = 1920,
                                                                 .target_height = 1080 });
    imgs.bg_prev = image_load_from_memory(k_png_blue, sizeof(k_png_blue),
                                          (FlImageLoadOptions) { .background_mode = FlBackgroundMode_Center,
                                                                 .target_width = 1920,
                                                                 .target_height = 1080 });
    const ImageHandle fg_image = imgs.fg, bg_curr = imgs.bg_curr, bg_prev = imgs.bg_prev;
    ASSERT_TRUE_MSG(fg_image != IMAGE_HANDLE_INVALID && bg_curr != IMAGE_HANDLE_INVALID
                        && bg_prev != IMAGE_HANDLE_INVALID,
                    "image load returned invalid handle");
    ASSERT_TRUE_MSG(wait_image_ready(fg_image), "foreground image did not decode in time");
    ASSERT_TRUE_MSG(wait_image_ready(bg_curr), "background (current) image did not decode in time");
    ASSERT_TRUE_MSG(wait_image_ready(bg_prev), "background (previous) image did not decode in time");

    // Dynamic images (covers both the RGBA and alpha-only conversion paths).
    FlDynamicImage* dyn_rgba = dynamic_image_create(48, 48, s_arena);
    FlDynamicImage* dyn_alpha = dynamic_image_create_alpha(48, 48, s_arena);
    ASSERT_TRUE_MSG(dyn_rgba != nullptr && dyn_alpha != nullptr, "dynamic image creation failed");
    fill_dynamic_rgba(dyn_rgba);
    fill_dynamic_alpha(dyn_alpha);

    // ---------------------------------------------------------------------------------------
    // Frame 1: the full primitive set drawn over a single background image composited with alpha
    // over the clear color - the only path that reads background.alpha and the clear color.
    // ---------------------------------------------------------------------------------------
    ui_background_set_clear_color(10, 12, 16);
    ui_background_set(bg_curr);
    ui_background_set_alpha(200);

    ui_update_begin(ctx);

    // Clay layout path: the primitives the widget/layout system expresses directly -
    // rect / rounded / border / clip.
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_LeftToRight, .padding = fl_padding_md(), .child_gap = FlSpacing_LG) {
        ui_layout_area(.sizing = { sizing_fixed(120), sizing_fixed(120) }, .color = ui_color(220, 60, 60, 255)) {}

        ui_layout_area(.sizing = { sizing_fixed(120), sizing_fixed(120) }, .color = ui_color(60, 200, 120, 255),
                       .radius = corner_radius(28)) {}

        ui_layout_area(.sizing = { sizing_fixed(120), sizing_fixed(120) }, .color = ui_color(40, 40, 60, 255),
                       .border = fl_border(ui_color(240, 240, 80, 255), 6)) {}

        // Clip: a fixed box clipping an oversized rounded child.
        ui_layout_area(.sizing = { sizing_fixed(120), sizing_fixed(120) },
                       .clip = { .horizontal = true, .vertical = true }) {
            ui_layout_area(.sizing = { sizing_fixed(260), sizing_fixed(260) }, .color = ui_color(200, 120, 220, 255),
                           .radius = corner_radius(40)) {}
        }

        // Painter path: gradient / static-image / dynamic-image (RGBA + alpha) / text /
        // translucent fill, painted inside this full-window container (origin 0,0, so element-local
        // coordinates equal absolute pixels). The paint draws at the container's z - under the child boxes
        // above - but the painted region (y >= 400) does not overlap them. Images go through the painter
        // because it is the texture path that draws at the image's native, deterministic size.
        Painter* p = ctx->painter;

        // Gradient primitive - the IR's gradient has no widget/Clay emitter, so this is
        // the only path that exercises RenderMask_ColorGradient.
        painter_gradient_rect(p, 100.0f, 400.0f, 320.0f, 200.0f, ui_color(255, 0, 0, 255), ui_color(0, 255, 0, 255),
                              ui_color(0, 0, 255, 255), ui_color(255, 255, 0, 255));

        // Static image (drawn at native size).
        ASSERT_TRUE_MSG(
            painter_image(p, 470.0f, 400.0f, 0.0f, 0.0f, (FlImageHandle)fg_image, (FlColor) { 255, 255, 255, 255 }),
            "painter_image: foreground not ready");

        painter_dynamic_image(p, 540.0f, 400.0f, dyn_rgba, ui_color(255, 255, 255, 255));

        // Alpha-only dynamic image, colorized via tint.
        painter_dynamic_image(p, 620.0f, 400.0f, dyn_alpha, ui_color(80, 220, 255, 255));

        // Translucent painter rectangle (exercises alpha blend-with-background).
        painter_rect(p, 760.0f, 400.0f, 260.0f, 200.0f, ui_color(255, 120, 0, 120));
    }

    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(ctx->shared->renderer.read_pixels != nullptr, "backend has no pixel-readback capability");
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "render buffer data is null");
    ASSERT_TRUE_MSG(buf.width == 1920 && buf.height == 1080, "unexpected render buffer dimensions");
    dump_frame("primitives", &buf);

    // Layout path: boxes at padding 16, gap 24, each 120 wide, probed at known points inside them.
    const int box_pitch = 120 + FlSpacing_LG;
    const int box_origin = FlSpacing_MD;
    const int box_mid = box_origin + 60;
    ASSERT_TRUE_MSG(px_near(&buf, box_mid, box_mid, ui_color(220, 60, 60, 255), 2), "solid box center");
    ASSERT_TRUE_MSG(px_near(&buf, box_mid + box_pitch, box_mid, ui_color(60, 200, 120, 255), 2), "rounded box center");
    ASSERT_TRUE_MSG(px_near(&buf, box_mid + 2 * box_pitch, box_mid, ui_color(40, 40, 60, 255), 2),
                    "bordered box interior");
    ASSERT_TRUE_MSG(px_near(&buf, box_origin + 2 * box_pitch + 3, box_mid, ui_color(240, 240, 80, 255), 2),
                    "bordered box edge carries the border color");
    ASSERT_TRUE_MSG(px_near(&buf, box_mid + 3 * box_pitch, box_mid, ui_color(200, 120, 220, 255), 2),
                    "clipped child fills the clip box");
    ASSERT_FALSE_MSG(
        px_near(&buf, box_origin + 4 * box_pitch - FlSpacing_LG + 20, box_mid, ui_color(200, 120, 220, 255), 2),
        "the oversized child does not paint past the clip box");

    // Gradient corners lean toward their corner colors.
    ASSERT_TRUE_MSG(px_near(&buf, 102, 402, ui_color(255, 0, 0, 255), 24), "gradient top-left corner");
    ASSERT_TRUE_MSG(px_near(&buf, 417, 402, ui_color(0, 255, 0, 255), 24), "gradient top-right corner");
    ASSERT_TRUE_MSG(px_near(&buf, 102, 597, ui_color(0, 0, 255, 255), 24), "gradient bottom-left corner");
    ASSERT_TRUE_MSG(px_near(&buf, 417, 597, ui_color(255, 255, 0, 255), 24), "gradient bottom-right corner");

    // The translucent fill is the 120/255 blend of its color over the pixels around it.
    {
        const u8* around = readback_rgb_at(&buf, 1100, 500);
        const u8* inside = readback_rgb_at(&buf, 890, 500);
        const int a = 120;
        ASSERT_NEAR_MSG((double)inside[0], (double)((around[0] * (255 - a) + 255 * a) / 255), 3.0,
                        "translucent fill red channel");
        ASSERT_NEAR_MSG((double)inside[1], (double)((around[1] * (255 - a) + 120 * a) / 255), 3.0,
                        "translucent fill green channel");
        ASSERT_NEAR_MSG((double)inside[2], (double)((around[2] * (255 - a) + 0 * a) / 255), 3.0,
                        "translucent fill blue channel");
    }

    // ---------------------------------------------------------------------------------------
    // Frame 2: a two-image blend between the two background images. The blend compositor is a
    // distinct path that alpha/clear do not feed; the half blend sits between the two endpoints.
    // ---------------------------------------------------------------------------------------
    u8 blend_px[3][3];
    const u8 blend_t[3] = { 0, 255, 128 };
    for (int i = 0; i < 3; i++) {
        ui_background_set_blend(bg_prev, bg_curr, blend_t[i]);
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {} // background only
        ui_update_end(ctx, 1.0f / 60.0f);

        FlPixelReadback buf_xfade;
        ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf_xfade),
                        "read_pixels failed");
        memcpy(blend_px[i], readback_rgb_at(&buf_xfade, 960, 540), 3);
        if (i == 2) {
            dump_frame("crossfade", &buf_xfade);
        }
    }
    ASSERT_GT_MSG(abs((int)blend_px[0][0] - (int)blend_px[1][0]), 100, "the two blend endpoints differ");
    for (int c = 0; c < 3; c++) {
        ASSERT_NEAR_MSG((double)blend_px[2][c], ((double)blend_px[0][c] + (double)blend_px[1][c]) * 0.5, 3.0,
                        "a half blend lands midway between the endpoints");
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.background_resize_larger_no_oob - a background image is pre-scaled to the surface size and the SW
// compositor addresses it by the surface's current tile grid on that invariant. change_resolution rebuilds the
// tile grid but does not re-fit an already-set background image, so a resize LARGER than the image would drive
// the tile-copy paths to read rows/columns far past the image buffer. The renderer drops any background image
// the surface has outgrown and falls back to the clear color; shrinking below the image must still composite.
UTEST(render, background_resize_larger_no_oob) {
    FlUiCtx* ctx = render_fixture_get(); // brings up the image system + SW pixel pool this exercises
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlRenderBackend* backend = &ctx->shared->renderer;

    // A Center-mode background with target == a solid source fills the whole target, so the image is a uniform
    // red RGB buffer of exactly SMALL_W x SMALL_H - the size the surface is about to outgrow.
    const int SMALL_W = 128, SMALL_H = 96;
    ImageHandle bg = image_load_from_memory(k_png_red, sizeof(k_png_red),
                                            (FlImageLoadOptions) { .background_mode = FlBackgroundMode_Center,
                                                                   .target_width = SMALL_W,
                                                                   .target_height = SMALL_H });
    ASSERT_TRUE_MSG(bg != IMAGE_HANDLE_INVALID, "background image load returned invalid handle");
    ASSERT_TRUE_MSG(wait_image_ready(bg), "background image did not decode in time");
    const ImageData* bg_data = image_cache_get_data(bg);
    ASSERT_TRUE_MSG(bg_data != nullptr, "background pixels not resident after realize");
    ASSERT_EQ_MSG(bg_data->channels, 3, "background must be interleaved RGB for the compositor");
    ASSERT_TRUE_MSG(bg_data->width == SMALL_W && bg_data->height == SMALL_H,
                    "background image dims must equal the load target for this test's coverage math");

    // Own surface so the grow/shrink cycle never touches the shared 1920x1080 fixture surface.
    FlSurface* surface = backend->create_surface(backend->user_data, SMALL_W, SMALL_H);
    ASSERT_TRUE_MSG(surface != nullptr, "surface creation failed");

    const FlColor clear = { .r = 10, .g = 12, .b = 16, .a = 255 };
    const u8 clear_rgb[3] = { clear.r, clear.g, clear.b };
    const FlRenderFrame frame = {
        .commands = nullptr,
        .bytes = 0,
        .global_alpha = 255,
        .background = { .current = (FlImageHandle)bg,
                        .prev = FL_IMAGE_HANDLE_INVALID,
                        .crossfade_t = 0.0f,
                        .alpha = 255,
                        .clear = clear },
    };

    // Matched size: the opaque RGB background covers the surface and composites, proving the image path runs
    // at all before the grow step drops it.
    render_seam_render(backend, surface, &frame);
    FlPixelReadback base;
    ASSERT_TRUE_MSG(render_seam_read_pixels(backend, surface, &base), "read_pixels failed");
    ASSERT_TRUE_MSG(base.width == (u32)SMALL_W && base.height == (u32)SMALL_H, "matched-size readback dims");
    ASSERT_TRUE_MSG(memcmp(readback_rgb_at(&base, SMALL_W / 2, SMALL_H / 2), clear_rgb, 3) != 0,
                    "matched-size background must composite, not fall back to the clear color");

    // Grow past the image without re-fitting it. Every tile now addresses rows/columns beyond the SMALL_W x
    // SMALL_H buffer, so the coverage guard must drop the outgrown image rather than let the direct-copy path
    // read far OOB.
    const int BIG_W = 256, BIG_H = 192;
    render_seam_change_resolution(backend, surface, BIG_W, BIG_H);
    render_seam_render(backend, surface, &frame);
    FlPixelReadback grown;
    ASSERT_TRUE_MSG(render_seam_read_pixels(backend, surface, &grown), "read_pixels failed");
    ASSERT_TRUE_MSG(grown.width == (u32)BIG_W && grown.height == (u32)BIG_H, "grown readback dims");
    // A pixel the outgrown image never covered: pure fallback clear, never OOB image bytes read past the buffer.
    ASSERT_TRUE_MSG(memcmp(readback_rgb_at(&grown, BIG_W - 2, BIG_H - 2), clear_rgb, 3) == 0,
                    "outgrown background: far corner must fall back to the clear color, not read past the buffer");
    // A pixel inside the old image extent is clear too: the image is dropped wholesale, not partially composited.
    ASSERT_TRUE_MSG(memcmp(readback_rgb_at(&grown, 2, 2), clear_rgb, 3) == 0,
                    "outgrown background: the whole frame falls back to the clear color");

    // Shrink below the image: it still fully covers the surface, so it must keep compositing (not be
    // spuriously dropped) while reading only a safe sub-region of its buffer.
    const int TINY_W = 64, TINY_H = 48;
    render_seam_change_resolution(backend, surface, TINY_W, TINY_H);
    render_seam_render(backend, surface, &frame);
    FlPixelReadback shrunk;
    ASSERT_TRUE_MSG(render_seam_read_pixels(backend, surface, &shrunk), "read_pixels failed");
    ASSERT_TRUE_MSG(shrunk.width == (u32)TINY_W && shrunk.height == (u32)TINY_H, "shrunk readback dims");
    ASSERT_TRUE_MSG(memcmp(readback_rgb_at(&shrunk, TINY_W / 2, TINY_H / 2), clear_rgb, 3) != 0,
                    "shrink below image: background still covers and must composite, not drop to the clear color");

    backend->destroy_surface(backend->user_data, surface);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.non16_width_right_edge - both SW tile output paths process each tile row in 16-px SIMD chunks plus a
// scalar tail, so the rightmost (tile_width & 15) columns of a non-16-aligned surface are the cold path. A
// horizontally-uniform frame is rendered and the ENTIRE sampled row must equal its midpoint pixel - a skipped
// tail (or any gap/overlap at the body/tail seam) leaves a column that differs. Two widths cover both regimes
// of the rightmost tile column (TILE_WIDTH == 64):
//   * W=168 -> rightmost tile is 40px: a 32px SIMD body FOLLOWED BY an 8px tail (the body/tail seam).
//   * W=200 -> rightmost tile is  8px: a pure tail, zero SIMD chunks.
// Both output paths are covered: copy_tile_to_output (a solid full-window fill - every tile has a visual
// command) and crossfade_direct_to_output (a background-only blend - no visual command, so the fast path).
//
// The surface is swapped away from the 1920x1080 fixture surface. utest ASSERTs early-return from the function,
// so results are captured into booleans while the swapped surface is alive and the fixture surface is restored
// before any assertion.
UTEST(render, non16_width_right_edge) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlSurface* saved = ctx->surface;
    const int H = 120;
    const int widths[] = { 168, 200 }; // 168 -> body+tail rightmost tile; 200 -> pure-tail rightmost tile

    bool setup_ok = true; // surfaces created, frames read back, crossfade actually composited
    bool fill_ok = true;  // every column of the fill row equals the row midpoint
    bool xfade_ok = true; // every column of the crossfade row equals the row midpoint
    int bad_w = 0, bad_x = -1;
    const char* bad_path = "";

    for (size_t wi = 0; wi < sizeof(widths) / sizeof(widths[0]) && setup_ok; ++wi) {
        const int W = widths[wi];
        const int ry = H / 2;
        FlSurface* surf = s_render.backend.create_surface(s_render.backend.user_data, W, H);
        if (surf == nullptr) {
            setup_ok = false;
            break;
        }
        ui_set_surface(ctx, surf);
        ctx->layout_width = (float)W;
        ctx->layout_height = (float)H;
        ui_set_scale(1.0f, 1.0f);
        Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)W, .height = (float)H });

        // --- Path 1: copy_tile_to_output via a solid, opaque, full-window fill (uniform across the row). ---
        ui_background_set(IMAGE_HANDLE_INVALID);
        ui_background_set_clear_color(10, 12, 16);
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                       .color = ui_color(200, 40, 50, 255)) {}
        ui_update_end(ctx, 1.0f / 60.0f);

        FlPixelReadback fbuf;
        if (render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &fbuf) && fbuf.width == (u32)W) {
            const u8* mid = readback_rgb_at(&fbuf, W / 2, ry);
            if (!(mid[0] > 100 && mid[0] > mid[1] && mid[0] > mid[2])) {
                setup_ok = false; // fill did not render at all - control pixel is not the expected red
            }
            for (int x = 0; x < W && fill_ok; ++x) {
                if (memcmp(readback_rgb_at(&fbuf, x, ry), mid, 3) != 0) {
                    fill_ok = false;
                    bad_w = W;
                    bad_x = x;
                    bad_path = "fill";
                }
            }
        } else {
            setup_ok = false;
        }

        // --- Path 2: crossfade_direct_to_output via a background-only two-image blend. ---
        // Center-mode backgrounds with target == surface size stretch each solid source to fill the whole
        // canvas, so the row is a single uniform red/blue blend. A midpoint differing from the clear color
        // proves the crossfade fast path ran at all; the full-row scan then covers every tail/seam column.
        ImageHandle bg_prev = image_load_from_memory(
            k_png_blue, sizeof(k_png_blue),
            (FlImageLoadOptions) { .background_mode = FlBackgroundMode_Center, .target_width = W, .target_height = H });
        ImageHandle bg_curr = image_load_from_memory(
            k_png_red, sizeof(k_png_red),
            (FlImageLoadOptions) { .background_mode = FlBackgroundMode_Center, .target_width = W, .target_height = H });
        if (bg_prev != IMAGE_HANDLE_INVALID && bg_curr != IMAGE_HANDLE_INVALID && wait_image_ready(bg_prev)
            && wait_image_ready(bg_curr)) {
            ui_background_set_blend(bg_prev, bg_curr, 128);
            ui_update_begin(ctx);
            ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {} // background only
            ui_update_end(ctx, 1.0f / 60.0f);

            FlPixelReadback xbuf;
            if (render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &xbuf)) {
                const u8* mid = readback_rgb_at(&xbuf, W / 2, ry);
                const u8 clear_rgb[3] = { 10, 12, 16 };
                if (memcmp(mid, clear_rgb, 3) == 0) {
                    setup_ok = false; // crossfade did not composite - row is still the clear color
                }
                for (int x = 0; x < W && xfade_ok; ++x) {
                    if (memcmp(readback_rgb_at(&xbuf, x, ry), mid, 3) != 0) {
                        xfade_ok = false;
                        bad_w = W;
                        bad_x = x;
                        bad_path = "crossfade";
                    }
                }
            } else {
                setup_ok = false;
            }
        } else {
            setup_ok = false;
        }

        // Restore the fixture surface, release the per-iteration images and destroy the temp surface on every
        // iteration, unconditionally, so nothing outlives this test even on failure.
        ui_background_set(IMAGE_HANDLE_INVALID);
        if (bg_prev != IMAGE_HANDLE_INVALID) {
            image_free(bg_prev);
        }
        if (bg_curr != IMAGE_HANDLE_INVALID) {
            image_free(bg_curr);
        }
        ui_set_surface(ctx, saved);
        s_render.backend.destroy_surface(s_render.backend.user_data, surf);
    }

    if (!fill_ok || !xfade_ok) {
        fprintf(stderr, "non16_width_right_edge: %s path W=%d column %d differs from row midpoint\n", bad_path, bad_w,
                bad_x);
    }
    ASSERT_TRUE_MSG(setup_ok, "edge-width render setup failed (surface/readback/decode, or a path did not render)");
    ASSERT_TRUE_MSG(fill_ok, "copy_tile_to_output left a non-16 tail/seam column unwritten");
    ASSERT_TRUE_MSG(xfade_ok, "crossfade_direct_to_output left a non-16 tail/seam column unwritten");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.theme_seam - theme colors resolve through the seam (ui_get_theme_colors), not the raw base
// pointer: with the override stack empty the seam resolves to the host-injected base theme, an un-injected
// context falls back to the built-in default theme, and the box colors of a frame all come through the seam.

UTEST(render, theme_seam) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // Identity scale (ui_set_scale 1.0) so colors land unmodulated; no background image, fixed clear color.
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(12, 12, 14);

    // A deterministic theme with a distinct value per semantic slot.
    static const FlThemeColors known = {
        .interactive.base = { 200, 30, 40, 255 },
        .interactive.hover = { 220, 60, 70, 255 },
        .interactive.press = { 170, 20, 30, 255 },
        .interactive.focus = { 255, 255, 255, 255 },
        .interactive.disabled = { 200, 30, 40, 128 },
        .surface.base = { 18, 20, 24, 255 },
        .surface.elevated = { 36, 38, 44, 255 },
        .surface.overlay = { 8, 9, 11, 255 },
        .surface.selected = { 200, 30, 40, 255 },
        .border.default_color = { 50, 52, 60, 255 },
        .border.focus = { 230, 80, 92, 255 },
        .border.separator = { 44, 46, 52, 255 },
        .text.primary = { 240, 240, 240, 255 },
        .text.secondary = { 150, 150, 150, 255 },
        .text.accent = { 200, 30, 40, 255 },
        .feedback.success = { 76, 175, 80, 255 },
        .feedback.warning = { 255, 152, 0, 255 },
        .feedback.error = { 244, 67, 54, 255 },
        .feedback.info = { 33, 150, 243, 255 },
    };

    ui_set_theme(ctx, &known);
    const FlThemeColors* resolved = ui_get_theme_colors(ctx);
    ASSERT_TRUE_MSG(resolved == &known, "seam must resolve to the injected base theme");

    // Eight boxes, one per semantic group, each colored through the seam.
    const FlColor slots[8] = {
        resolved->surface.base,      resolved->surface.elevated,     resolved->interactive.base,
        resolved->interactive.hover, resolved->border.default_color, resolved->text.primary,
        resolved->feedback.success,  resolved->feedback.error,
    };

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = resolved->surface.base,
                   .direction = FlLayoutDirection_LeftToRight, .padding = fl_padding_md(), .child_gap = FlSpacing_LG) {
        for (int i = 0; i < 8; i++) {
            ui_layout_area(.sizing = { sizing_fixed(160), sizing_fixed(160) }, .color = slots[i]) {}
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(ctx->shared->renderer.read_pixels != nullptr, "backend has no pixel-readback capability");
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "render buffer data is null");
    ASSERT_TRUE_MSG(buf.width == 1920 && buf.height == 1080, "unexpected render buffer dimensions");
    dump_frame("theme_seam", &buf);

    // An un-injected context falls back to the built-in default theme (the bottom of the resolution stack).
    ui_set_theme(ctx, nullptr);
    const FlThemeColors* def = ui_get_theme_colors(ctx);
    ASSERT_TRUE_MSG(def != nullptr, "default resolution must never return null");
    const FlColor want_surface = (FlColor)FL_THEME_DEFAULT_SURFACE_BASE;
    const FlColor want_text = (FlColor)FL_THEME_DEFAULT_TEXT_PRIMARY;
    ASSERT_TRUE_MSG(def->surface.base.r == want_surface.r && def->surface.base.g == want_surface.g
                        && def->surface.base.b == want_surface.b && def->surface.base.a == want_surface.a,
                    "default fallback surface.base must match the default theme");
    ASSERT_TRUE_MSG(def->text.primary.r == want_text.r && def->text.primary.g == want_text.g
                        && def->text.primary.b == want_text.b && def->text.primary.a == want_text.a,
                    "default fallback text.primary must match the default theme");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.image_scaled - painter_image honors its w/h params, documented as a "scaled to fit" target
// size. A 4x4 image drawn at a 200x200 logical size is checked by pixels inside and outside the requested
// rect. A non-positive w/h falls back to native size; that path is
// exercised by render.primitives_and_background_blend.

UTEST(render, image_scaled) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // Identity scale so painter coordinates map 1:1 to the offscreen buffer; fixed clear color, no background.
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    ui_background_set(IMAGE_HANDLE_INVALID);
    const u8 clear_r = 12, clear_g = 12, clear_b = 14;
    ui_background_set_clear_color(clear_r, clear_g, clear_b);

    // k_png_red is a 4x4 RGB PNG. Drawing it at a 200x200 logical size proves the w/h params scale the draw.
    ImageHandle fg_image = image_load_from_memory(k_png_red, sizeof(k_png_red), (FlImageLoadOptions) { 0 });
    ASSERT_TRUE_MSG(fg_image != IMAGE_HANDLE_INVALID, "image load returned invalid handle");
    ASSERT_TRUE_MSG(wait_image_ready(fg_image), "foreground image did not decode in time");

    const float draw_x = 100.0f, draw_y = 100.0f, draw_w = 200.0f, draw_h = 200.0f;

    ui_update_begin(ctx);
    // Full-window container at origin (0,0) so element-local painter coords equal absolute pixels.
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        Painter* p = ctx->painter;
        ASSERT_TRUE_MSG(
            painter_image(p, draw_x, draw_y, draw_w, draw_h, (FlImageHandle)fg_image, (FlColor) { 255, 255, 255, 255 }),
            "painter_image: foreground not ready");
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(ctx->shared->renderer.read_pixels != nullptr, "backend has no pixel-readback capability");
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "render buffer data is null");
    ASSERT_TRUE_MSG(buf.width == 1920 && buf.height == 1080, "unexpected render buffer dimensions");

    // The center of the requested 200x200 rect would equal the clear color if painter_image had drawn at the
    // 4x4 native size instead of honoring w/h.
    const u8* center
        = buf.pixels + (size_t)(draw_y + draw_h * 0.5f) * buf.stride + (size_t)(draw_x + draw_w * 0.5f) * 3u;
    ASSERT_TRUE_MSG(!(center[0] == clear_r && center[1] == clear_g && center[2] == clear_b),
                    "center of requested rect equals clear color — painter_image ignored w/h");
    // A pixel comfortably outside the requested rect stays clear color.
    const u8* outside
        = buf.pixels + (size_t)(draw_y + draw_h * 0.5f) * buf.stride + (size_t)(draw_x + draw_w + 20) * 3u;
    ASSERT_TRUE_MSG(outside[0] == clear_r && outside[1] == clear_g && outside[2] == clear_b,
                    "pixel outside requested rect is not clear color");

    dump_frame("image_scaled", &buf);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.image_downscale_gather - scaled-image sampling stays correct past the ~4.4x downscale threshold.
//
// The SW scaled sampler precomputes an 8-pixel shuffle over a fixed 32-byte source window. Once the downscale
// ratio pushes the 8 destination pixels' source columns more than 31 bytes apart (roughly >4.4x) the window
// can no longer cover them, so those commands route through a scalar gather (exact nearest-neighbor per pixel).
//
// The source is an opaque XRGB horizontal ramp (column c -> value c*3, every row identical). XRGB drops the X
// byte, so it converts to a 3-plane RGB layout (blue last); identical rows keep the vertical scale from
// perturbing the horizontal invariant. It is drawn at 8x (scalar-gather path) and 2x (fast windowed path)
// downscale, and each destination pixel is checked against the reference sample floor(dst_x * x_step).

UTEST(render, image_downscale_gather) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    ui_background_set(IMAGE_HANDLE_INVALID);
    const u8 clear_r = 3, clear_g = 3, clear_b = 3;
    ui_background_set_clear_color(clear_r, clear_g, clear_b);

    // XRGB is opaque (no alpha plane -> 3-plane RGB), so the scaled draw composites the exact source color.
    // Byte order is R,G,B,X.
    enum { SRC_W = 80, SRC_H = 8 };
    u8 ramp[SRC_W * SRC_H * 4];
    for (int y = 0; y < SRC_H; y++) {
        for (int c = 0; c < SRC_W; c++) {
            u8 v = (u8)(c * 3);
            u8* px = ramp + ((size_t)y * SRC_W + c) * 4;
            px[0] = v;
            px[1] = v;
            px[2] = v;
            px[3] = 0; // X byte, dropped by the XRGB -> planar conversion
        }
    }
    FlImageHandle ramp_img = fl_image_create_from_pixels(ramp, SRC_W, SRC_H, FlImageFormat_XRGB);
    ASSERT_NE_MSG(ramp_img, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "ramp image create returned invalid handle");
    ASSERT_TRUE_MSG(fl_image_is_ready(ramp_img), "ramp image must be ready immediately");

    // 8x downscale (80 -> 10) exercises the scalar-gather path; 2x downscale (80 -> 40) the fast path.
    const float x8 = 100.0f, y8 = 100.0f, w8 = 10.0f, h8 = 8.0f;
    const float x2 = 100.0f, y2 = 300.0f, w2 = 40.0f, h2 = 8.0f;

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        Painter* p = ctx->painter;
        ASSERT_TRUE_MSG(painter_image(p, x8, y8, w8, h8, ramp_img, ui_color(255, 255, 255, 255)),
                        "painter_image: 8x ramp not ready");
        ASSERT_TRUE_MSG(painter_image(p, x2, y2, w2, h2, ramp_img, ui_color(255, 255, 255, 255)),
                        "painter_image: 2x ramp not ready");
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(ctx->shared->renderer.read_pixels != nullptr, "backend has no pixel-readback capability");
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "render buffer data is null");

    // Walk the interior of the middle destination row, skipping the boundary pixels the fractional AABB may
    // only partially cover. Destination pixel i (0-based from the draw's left edge) maps to source column
    // floor(i * x_step) under nearest-neighbor.
    const int tol = 2; // compositing/Q7 round-trip slack
    const float draws[2][4] = { { x8, y8, w8, h8 }, { x2, y2, w2, h2 } };
    for (int d = 0; d < 2; d++) {
        int row = (int)(draws[d][1] + draws[d][3] * 0.5f);
        int dw = (int)draws[d][2];
        float x_step = (float)SRC_W / (float)dw;
        int first = -1, last = -1;
        for (int i = 1; i < dw - 1; i++) {
            int px = (int)draws[d][0] + i;
            int r = readback_rgb_at(&buf, px, row)[0];
            int ref_col = (int)floorf((float)i * x_step);
            if (ref_col > SRC_W - 1) {
                ref_col = SRC_W - 1;
            }
            int ref = ref_col * 3;
            ASSERT_TRUE_MSG(r >= ref - tol && r <= ref + tol,
                            "sampled pixel does not match the reference nearest-neighbor source column");
            if (first < 0) {
                first = r;
            }
            last = r;
        }
        ASSERT_TRUE_MSG(last - first > 100, "downscaled ramp did not span the expected range");
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.image_scale_plane_over_read_guard - the scaled sampler's fixed 32-byte window stays in bounds.
//
// The scaled sampler (sample_plane_8 in sr_render_image_scale.c) issues two unaligned 16-byte loads per
// plane row (at load_offset and load_offset + 16), a 32-byte window. Near the bottom-right of the last (blue)
// plane that window reaches up to ~30 bytes past the logical 3-plane layout, so sr_convert_to_planar pads the
// allocation by SR_SCALE_PLANE_LOAD_GUARD bytes.
//
// The plane geometry and guard come from the real sr_convert_to_planar and the shared constant, so the test
// cannot silently drift from production. The worst-case window is replayed twice: once on the real converted
// buffer, and once on a tightly malloc-backed replica. The replica is the actual ASan teeth - the production
// planar buffer is arena-allocated, and arena sub-allocations carry no per-allocation redzone.

UTEST(render, image_scale_plane_over_read_guard) {
    // width/height are chosen so the padded 3-plane layout is non-trivial; keep_alpha=false drops the X byte,
    // leaving blue as the last plane.
    enum { SRC_W = 39, SRC_H = 7 };
    u8 src_pixels[SRC_W * SRC_H * 4];
    memset(src_pixels, 0x5a, sizeof src_pixels);
    ImageData src = {
        .data = src_pixels,
        .width = SRC_W,
        .height = SRC_H,
        .padded_width = SRC_W,
        .padded_height = SRC_H,
        .stride = SRC_W * 4,
        .channels = 4,
        .format = FlImageFormat_XRGB,
    };

    FlArena* arena = arena_new();
    ASSERT_TRUE_MSG(arena != nullptr, "arena_new failed");
    ImageData* planar = sr_convert_to_planar(&src, false, arena);
    ASSERT_TRUE_MSG(planar != nullptr, "sr_convert_to_planar returned null");
    ASSERT_EQ_MSG(planar->channels, 3, "XRGB (keep_alpha=false) must convert to a 3-plane blue-last layout");

    const int plane_size = planar->padded_width * planar->padded_height;
    const int total_size = plane_size * planar->channels; // logical 3-plane size, before the guard

    // Worst case: last plane (blue), last row, rightmost load offset. load_offset is the clamped source
    // column, whose maximum is width - 1 == padded_width - 2 (padded_width = width + 1).
    const size_t load_offset = (size_t)(planar->padded_width - 2);
    const size_t row_offset = (size_t)(planar->channels - 1) * (size_t)plane_size
                              + (size_t)(planar->padded_height - 1) * (size_t)planar->padded_width;
    const size_t highest = row_offset + load_offset + 31; // highest byte offset the 32-byte window addresses

    ASSERT_TRUE_MSG(highest >= (size_t)total_size,
                    "guard is load-bearing: the window reaches past the unguarded 3-plane size");
    ASSERT_TRUE_MSG(highest < (size_t)total_size + (size_t)SR_SCALE_PLANE_LOAD_GUARD,
                    "guard is sufficient: the window stays within the guarded allocation");

    u8 sink[32];

    // Replay the sampler's two loads on the real converted buffer - must not crash (guard present).
    const u8* real_row = planar->data + row_offset;
    u8x16_store_unaligned(u8x16_load_unaligned(real_row, load_offset), sink, 0);
    u8x16_store_unaligned(u8x16_load_unaligned(real_row, load_offset + 16), sink, 16);

    // Replay the identical window on a tightly malloc-backed replica - this is what ASan can actually
    // police (a malloc redzone follows the guarded allocation).
    u8* replica = (u8*)malloc((size_t)total_size + (size_t)SR_SCALE_PLANE_LOAD_GUARD);
    ASSERT_TRUE_MSG(replica != nullptr, "planar replica allocation failed");
    memset(replica, 0, (size_t)total_size + (size_t)SR_SCALE_PLANE_LOAD_GUARD);
    const u8* rep_row = replica + row_offset;
    u8x16_store_unaligned(u8x16_load_unaligned(rep_row, load_offset), sink, 0);
    u8x16_store_unaligned(u8x16_load_unaligned(rep_row, load_offset + 16), sink, 16);
    ASSERT_TRUE_MSG(sink[0] == 0 && sink[31] == 0, "guarded window read cleared bytes");

    free(replica);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A zero-width image destination must be dropped, not rasterized. The scaled path precomputes a shuffle table
// sized to the integer destination width; at dst_width == 0 the padding-fill loop in calculate_shuffle_tables
// would index result[dst_width - 1] == result[-1], which as a size_t subscript underflows to a huge
// out-of-bounds access. The painter clamps degenerate sizes, so this drives the raw IR path (Clay images /
// direct fl_ir_image) that can carry a zero-width dst.

UTEST(render, zero_width_image_dst_is_dropped) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // A ready 2x2 static image so the color path resolves; the only variable across the two draws below is the
    // destination width.
    u8 rgb_pixels[12] = { 220, 20, 30, 20, 220, 30, 20, 30, 220, 240, 240, 40 };
    FlImageHandle img = fl_image_create_from_pixels(rgb_pixels, 2, 2, FlImageFormat_RGB);
    ASSERT_NE_MSG(img, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "raw upload returned invalid handle");
    ASSERT_TRUE_MSG(image_cache_get_data((ImageHandle)img) != nullptr, "image pixels should be resident");

    FlImageSource source = { .kind = FlImageKind_Static };
    source.handle = img;
    const FlColor white = { 255, 255, 255, 255 };

    FlArena* ir_arena = arena_new();
    FlArena* commands_arena = arena_new();
    FlArena* state_arena = arena_new();

    // Positive control: an 8x8 destination for the 2x2 source takes the scaled path and emits one command.
    fl_arena_align(ir_arena, 8);
    const u8* pos_start = (const u8*)fl_arena_current_pos(ir_arena);
    fl_ir_image(ir_arena, source, FlImageMode_Color, (FlRect) { 4.0f, 4.0f, 8.0f, 8.0f }, (FlRect) { 0 }, white,
                FlImageFilter_Linear, nullptr);
    size_t pos_bytes = (size_t)((const u8*)fl_arena_current_pos(ir_arena) - pos_start);
    UiRenderCommandsArray pos
        = sr_ir_to_render_commands(commands_arena, state_arena, nullptr, pos_start, pos_bytes, 255);
    ASSERT_EQ_MSG(pos.count, 1u, "a positive-width scaled image emits one render command");
    ASSERT_TRUE_MSG((pos.commands[0].mask & RenderMask_TextureScaled) != 0, "8x8 dst for 2x2 src is the scaled path");

    // The identical draw with dst.w == 0 must be dropped before command setup, so calculate_shuffle_tables
    // never underflows its padding index.
    fl_arena_align(ir_arena, 8);
    const u8* zero_start = (const u8*)fl_arena_current_pos(ir_arena);
    fl_ir_image(ir_arena, source, FlImageMode_Color, (FlRect) { 4.0f, 4.0f, 0.0f, 8.0f }, (FlRect) { 0 }, white,
                FlImageFilter_Linear, nullptr);
    size_t zero_bytes = (size_t)((const u8*)fl_arena_current_pos(ir_arena) - zero_start);
    UiRenderCommandsArray zero
        = sr_ir_to_render_commands(commands_arena, state_arena, nullptr, zero_start, zero_bytes, 255);
    ASSERT_EQ_MSG(zero.count, 0u, "a zero-width image destination is dropped, not rasterized");

    // A destination narrower than one whole pixel truncates to zero columns and is dropped the same way.
    fl_arena_align(ir_arena, 8);
    const u8* thin_start = (const u8*)fl_arena_current_pos(ir_arena);
    fl_ir_image(ir_arena, source, FlImageMode_Color, (FlRect) { 4.0f, 4.0f, 0.5f, 8.0f }, (FlRect) { 0 }, white,
                FlImageFilter_Linear, nullptr);
    size_t thin_bytes = (size_t)((const u8*)fl_arena_current_pos(ir_arena) - thin_start);
    UiRenderCommandsArray thin
        = sr_ir_to_render_commands(commands_arena, state_arena, nullptr, thin_start, thin_bytes, 255);
    ASSERT_EQ_MSG(thin.count, 0u, "a sub-pixel-wide image destination is dropped, not rasterized");

    fl_arena_destroy(ir_arena);
    fl_arena_destroy(commands_arena);
    fl_arena_destroy(state_arena);
    image_free((ImageHandle)img);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Draw an image through the SW backend onto a clear-colored 32x32 surface and read it back. The 8x8 dst for
// the 2x2 sources below is always the scaled path.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool render_image_scaled(FlImageSource source, FlColor tint, u8 frame_global_alpha, FlColor clear,
                                FlPixelReadback* out, FlArena* backend_arena, FlRenderBackend* backend,
                                FlSurface** surface) {
    *backend = create_software_renderer(backend_arena, nullptr, FontRenderMode_Grayscale);
    *surface = backend->create_surface(backend->user_data, 32, 32);

    FlArena* ir = arena_new();
    fl_arena_align(ir, 8);
    const u8* start = (const u8*)fl_arena_current_pos(ir);
    fl_ir_image(ir, source, FlImageMode_Color, (FlRect) { 8.0f, 8.0f, 8.0f, 8.0f }, (FlRect) { 0 }, tint,
                FlImageFilter_Linear, nullptr);
    const size_t bytes = (size_t)((const u8*)fl_arena_current_pos(ir) - start);

    const FlRenderFrame frame = {
        .commands = start,
        .bytes = bytes,
        .global_alpha = frame_global_alpha,
        .background = { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID, .clear = clear },
    };
    render_seam_render(backend, *surface, &frame);
    bool ok = render_seam_read_pixels(backend, *surface, out);
    fl_arena_destroy(ir);
    return ok;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void destroy_scaled_render(FlRenderBackend* backend, FlSurface* surface, FlArena* backend_arena) {
    backend->destroy_surface(backend->user_data, surface);
    if (backend->destroy != nullptr) {
        backend->destroy(backend->user_data);
    }
    fl_arena_destroy(backend_arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlImageSource static_source(FlImageHandle img) {
    FlImageSource source = { .kind = FlImageKind_Static };
    source.handle = img;
    return source;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const u8* scaled_probe_px(const FlPixelReadback* buf) {
    return (const u8*)buf->pixels + (size_t)12 * buf->stride + (size_t)12 * 3u; // inside the 8x8 dst at (8,8)
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.scaled_image_blends_source_alpha - a scaled RGBA image with a translucent alpha plane composites over the
// background (premultiplied-over) instead of painting its color opaque.

UTEST(render, scaled_image_blends_source_alpha) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // Straight (0, 0, 255, a=128); upload premultiplies it to (0, 0, 128, 128).
    u8 rgba[16];
    for (int i = 0; i < 4; ++i) {
        rgba[i * 4 + 0] = 0;
        rgba[i * 4 + 1] = 0;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = 128;
    }
    FlImageHandle img = fl_image_create_from_pixels(rgba, 2, 2, FlImageFormat_RGBA);
    ASSERT_NE_MSG(img, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "raw upload returned invalid handle");

    FlArena* arena = arena_new();
    FlRenderBackend backend;
    FlSurface* surface;
    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_image_scaled(static_source(img), (FlColor) { 255, 255, 255, 255 }, 255,
                                        (FlColor) { 200, 200, 200, 255 }, &buf, arena, &backend, &surface),
                    "read_pixels failed");
    const u8* px = scaled_probe_px(&buf);
    // r = 200 * (1 - 128/255) ~ 100, b = 128 + 100 ~ 228. An alpha-ignoring draw would give r = 0, b = 128.
    ASSERT_TRUE_MSG(px[0] > 90 && px[0] < 110, "scaled RGBA must blend its alpha plane over the background");
    ASSERT_TRUE_MSG(px[2] > 218 && px[2] < 238, "scaled RGBA blue must be the premultiplied-over result");

    destroy_scaled_render(&backend, surface, arena);
    image_free((ImageHandle)img);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.scaled_image_honours_global_alpha - a scaled opaque image under a frame fade blends toward the
// background, for both the 4-plane static upload and the 3-plane dynamic buffer.

UTEST(render, scaled_image_honours_global_alpha) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    u8 rgb[12];
    u8 rgba[16];
    for (int i = 0; i < 4; ++i) {
        rgb[i * 3 + 0] = 0;
        rgb[i * 3 + 1] = 0;
        rgb[i * 3 + 2] = 255;
        rgba[i * 4 + 0] = 0;
        rgba[i * 4 + 1] = 0;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = 255;
    }
    FlImageHandle img = fl_image_create_from_pixels(rgb, 2, 2, FlImageFormat_RGB);
    ASSERT_NE_MSG(img, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "raw upload returned invalid handle");
    FlImageSource dynamic = { .kind = FlImageKind_Dynamic };
    dynamic.dynamic.pixels = rgba;
    dynamic.dynamic.w = 2;
    dynamic.dynamic.h = 2;

    const FlImageSource sources[2] = { static_source(img), dynamic };
    for (int i = 0; i < 2; ++i) {
        FlArena* arena = arena_new();
        FlRenderBackend backend;
        FlSurface* surface;
        FlPixelReadback buf;
        ASSERT_TRUE_MSG(render_image_scaled(sources[i], (FlColor) { 255, 255, 255, 255 }, 128,
                                            (FlColor) { 200, 200, 200, 255 }, &buf, arena, &backend, &surface),
                        "read_pixels failed");
        const u8* px = scaled_probe_px(&buf);
        // 50% fade: r ~ 100, b ~ 228. An ignored fade would paint r = 0, b = 255.
        ASSERT_TRUE_MSG(px[0] > 90 && px[0] < 110, "faded scaled image must blend toward the background");
        ASSERT_TRUE_MSG(px[2] > 218 && px[2] < 238, "faded scaled image blue must be the lerp toward the background");
        destroy_scaled_render(&backend, surface, arena);
    }

    image_free((ImageHandle)img);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.scaled_alpha_image_is_tinted_coverage - a scaled 1-channel image is coverage for the tint: full coverage
// paints the tint, zero coverage leaves the background. Only the one plane is sampled.

UTEST(render, scaled_alpha_image_is_tinted_coverage) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    u8 full[4] = { 255, 255, 255, 255 };
    u8 none[4] = { 0, 0, 0, 0 };
    FlImageHandle img_full = fl_image_create_from_pixels(full, 2, 2, FlImageFormat_Alpha);
    FlImageHandle img_none = fl_image_create_from_pixels(none, 2, 2, FlImageFormat_Alpha);
    ASSERT_NE_MSG(img_full, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "raw upload returned invalid handle");
    ASSERT_NE_MSG(img_none, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "raw upload returned invalid handle");

    const FlColor tint = { 10, 20, 30, 255 };
    const FlColor clear = { 200, 200, 200, 255 };

    FlArena* arena = arena_new();
    FlRenderBackend backend;
    FlSurface* surface;
    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_image_scaled(static_source(img_full), tint, 255, clear, &buf, arena, &backend, &surface),
                    "read_pixels failed");
    const u8* px = scaled_probe_px(&buf);
    ASSERT_TRUE_MSG(abs(px[0] - 10) <= 3 && abs(px[1] - 20) <= 3 && abs(px[2] - 30) <= 3,
                    "full coverage must paint the tint");
    destroy_scaled_render(&backend, surface, arena);

    arena = arena_new();
    ASSERT_TRUE_MSG(render_image_scaled(static_source(img_none), tint, 255, clear, &buf, arena, &backend, &surface),
                    "read_pixels failed");
    px = scaled_probe_px(&buf);
    ASSERT_TRUE_MSG(abs(px[0] - 200) <= 3 && abs(px[1] - 200) <= 3 && abs(px[2] - 200) <= 3,
                    "zero coverage must leave the background");
    destroy_scaled_render(&backend, surface, arena);

    image_free((ImageHandle)img_full);
    image_free((ImageHandle)img_none);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.static_image_blocks_carry_the_sampler_guard - every static image block is allocated with
// SR_SCALE_PLANE_LOAD_GUARD bytes past its padded planes, zeroed. The sizes below are exact multiples of the
// allocator granularity, so an unguarded block has no slack.

UTEST(render, static_image_blocks_carry_the_sampler_guard) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // 15x15 pads to 16x16 planes of 256 bytes: one plane for Alpha, four (RGB plus opaque alpha) for RGB.
    u8 alpha[15 * 15];
    u8 rgb[15 * 15 * 3];
    memset(alpha, 0x7f, sizeof alpha);
    memset(rgb, 0x7f, sizeof rgb);
    FlImageHandle h_alpha = fl_image_create_from_pixels(alpha, 15, 15, FlImageFormat_Alpha);
    FlImageHandle h_rgb = fl_image_create_from_pixels(rgb, 15, 15, FlImageFormat_RGB);
    ASSERT_NE_MSG(h_alpha, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "raw upload returned invalid handle");
    ASSERT_NE_MSG(h_rgb, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "raw upload returned invalid handle");

    const struct {
        FlImageHandle handle;
        size_t planes_size;
    } cases[2] = { { h_alpha, 256 }, { h_rgb, 1024 } };

    for (int i = 0; i < 2; ++i) {
        ImageData* data = image_cache_get_data((ImageHandle)cases[i].handle);
        ASSERT_TRUE_MSG(data != nullptr, "image pixels should be resident");
        ASSERT_TRUE_MSG(tlsf_block_size(data->data) >= cases[i].planes_size + SR_SCALE_PLANE_LOAD_GUARD,
                        "static image block must extend SR_SCALE_PLANE_LOAD_GUARD past its planes");
        for (int g = 0; g < SR_SCALE_PLANE_LOAD_GUARD; ++g) {
            ASSERT_EQ_MSG(data->data[cases[i].planes_size + (size_t)g], 0, "guard bytes must be zeroed");
        }
    }

    image_free((ImageHandle)h_alpha);
    image_free((ImageHandle)h_rgb);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.premultiply_keeps_opaque_channels - an opaque pixel premultiplies to itself, and a half-alpha channel
// rounds to nearest.

UTEST(render, premultiply_keeps_opaque_channels) {
    const u8 in[16] = { 255, 200, 1, 255, 255, 200, 1, 255, 255, 200, 1, 255, 255, 200, 1, 255 };
    u8 out[16];
    u8x16_store_unaligned(sr_premultiply_rgba(u8x16_new(in)), out, 0);
    ASSERT_EQ_MSG(out[0], 255, "opaque 255 must stay 255");
    ASSERT_EQ_MSG(out[1], 200, "opaque channel must be unchanged");
    ASSERT_EQ_MSG(out[2], 1, "opaque channel must be unchanged");
    ASSERT_EQ_MSG(out[3], 255, "alpha lane must be preserved");

    const u8 half[16] = { 255, 100, 0, 128, 255, 100, 0, 128, 255, 100, 0, 128, 255, 100, 0, 128 };
    u8x16_store_unaligned(sr_premultiply_rgba(u8x16_new(half)), out, 0);
    ASSERT_EQ_MSG(out[0], 128, "255 at alpha 128 premultiplies to 128");
    ASSERT_EQ_MSG(out[1], 50, "100 at alpha 128 premultiplies to 50");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.image_from_pixels - the raw upload is synchronous and copies caller memory into a
// backend-owned static image, so the source buffers are mutated before drawing. Covers RGBA, Alpha, RGB and
// XRGB handles.

UTEST(render, image_from_pixels) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    ui_background_set(IMAGE_HANDLE_INVALID);
    const u8 clear_r = 5, clear_g = 7, clear_b = 11;
    ui_background_set_clear_color(clear_r, clear_g, clear_b);

    u8 rgba_pixels[16] = {
        220, 20, 30, 255, 20, 220, 30, 255, 20, 30, 220, 255, 240, 240, 40, 255,
    };
    u8 alpha_pixels[4] = { 0, 255, 128, 255 };
    u8 rgb_pixels[3] = { 35, 180, 90 };
    u8 xrgb_pixels[4] = { 190, 70, 210, 0 };

    FlImageHandle rgba = fl_image_create_from_pixels(rgba_pixels, 2, 2, FlImageFormat_RGBA);
    FlImageHandle alpha = fl_image_create_from_pixels(alpha_pixels, 2, 2, FlImageFormat_Alpha);
    FlImageHandle rgb = fl_image_create_from_pixels(rgb_pixels, 1, 1, FlImageFormat_RGB);
    FlImageHandle xrgb = fl_image_create_from_pixels(xrgb_pixels, 1, 1, FlImageFormat_XRGB);

    ASSERT_NE_MSG(rgba, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "RGBA raw upload returned invalid handle");
    ASSERT_NE_MSG(alpha, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "Alpha raw upload returned invalid handle");
    ASSERT_NE_MSG(rgb, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "RGB raw upload returned invalid handle");
    ASSERT_NE_MSG(xrgb, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "XRGB raw upload returned invalid handle");
    ASSERT_TRUE_MSG(fl_image_is_ready(rgba), "direct RGBA handle must be ready immediately");
    ASSERT_TRUE_MSG(fl_image_is_ready(alpha), "Alpha handle must be ready immediately");
    ASSERT_TRUE_MSG(fl_image_is_ready(rgb), "RGB handle must be ready immediately");
    ASSERT_TRUE_MSG(fl_image_is_ready(xrgb), "direct XRGB handle must be ready immediately");
    ASSERT_TRUE_MSG(image_cache_get_data((ImageHandle)rgba) != nullptr, "RGBA pixels should be resident");
    ASSERT_TRUE_MSG(image_cache_get_data((ImageHandle)alpha) != nullptr, "Alpha pixels should be resident");
    ASSERT_TRUE_MSG(image_cache_get_data((ImageHandle)rgb) != nullptr, "RGB pixels should be resident");
    ASSERT_TRUE_MSG(image_cache_get_data((ImageHandle)xrgb) != nullptr, "XRGB pixels should be resident");

    memset(rgba_pixels, 0, sizeof rgba_pixels);
    memset(alpha_pixels, 0, sizeof alpha_pixels);
    memset(rgb_pixels, 0, sizeof rgb_pixels);
    memset(xrgb_pixels, 0, sizeof xrgb_pixels);

    const FlColor alpha_tint = ui_color(40, 200, 250, 255);

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        Painter* p = ctx->painter;
        ASSERT_TRUE_MSG(painter_image(p, 100.0f, 100.0f, 0.0f, 0.0f, rgba, ui_color(255, 255, 255, 255)),
                        "painter_image: RGBA raw handle not ready");
        ASSERT_TRUE_MSG(painter_image(p, 108.0f, 100.0f, 0.0f, 0.0f, alpha, alpha_tint),
                        "painter_image: Alpha raw handle not ready");
        ASSERT_TRUE_MSG(painter_image(p, 116.0f, 100.0f, 0.0f, 0.0f, rgb, ui_color(255, 255, 255, 255)),
                        "painter_image: RGB raw handle not ready");
        ASSERT_TRUE_MSG(painter_image(p, 120.0f, 100.0f, 0.0f, 0.0f, xrgb, ui_color(255, 255, 255, 255)),
                        "painter_image: XRGB raw handle not ready");
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(ctx->shared->renderer.read_pixels != nullptr, "backend has no pixel-readback capability");
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "render buffer data is null");
    ASSERT_TRUE_MSG(buf.width == 1920 && buf.height == 1080, "unexpected render buffer dimensions");

    const u8* rgba_red = readback_rgb_at(&buf, 100, 100);
    ASSERT_TRUE_MSG(rgba_red[0] > 180 && rgba_red[1] < 70 && rgba_red[2] < 80,
                    "RGBA copied pixels did not render the original red texel");

    const u8* alpha_zero = readback_rgb_at(&buf, 108, 100);
    ASSERT_TRUE_MSG(alpha_zero[0] == clear_r && alpha_zero[1] == clear_g && alpha_zero[2] == clear_b,
                    "Alpha zero texel should leave the clear color unchanged");
    const u8* alpha_full = readback_rgb_at(&buf, 109, 100);
    ASSERT_TRUE_MSG(alpha_full[0] > 25 && alpha_full[1] > 150 && alpha_full[2] > 190,
                    "Alpha full texel should tint through painter_image color");

    const u8* rgb_texel = readback_rgb_at(&buf, 116, 100);
    ASSERT_TRUE_MSG(rgb_texel[0] > 20 && rgb_texel[1] > 140 && rgb_texel[2] > 60,
                    "RGB copied pixels did not render the original texel");
    const u8* xrgb_texel = readback_rgb_at(&buf, 120, 100);
    ASSERT_TRUE_MSG(xrgb_texel[0] > 150 && xrgb_texel[1] > 45 && xrgb_texel[2] > 170,
                    "XRGB copied pixels did not render the original texel");

    dump_frame("image_from_pixels", &buf);

    image_free((ImageHandle)rgba);
    image_free((ImageHandle)alpha);
    image_free((ImageHandle)rgb);
    image_free((ImageHandle)xrgb);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.image_update_pixels - re-uploading into a live handle keeps the handle's identity, dimensions, and
// format, and respects the same caller-buffer contract create_from_pixels does.

UTEST(render, image_update_pixels) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(5, 7, 11);

    // Dedicated: under the software backend the flag changes nothing, but it is what the GPU backend keeps
    // off its atlas.
    u8 pixels[4] = { 220, 30, 40, 255 };
    FlImageHandle handle = fl_image_create_dedicated(pixels, 1, 1, FlImageFormat_RGBA);
    ASSERT_NE_MSG(handle, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "dedicated raw upload returned invalid handle");
    ASSERT_TRUE_MSG(fl_image_is_ready(handle), "dedicated handle must be ready immediately");

    u8 rebaked[4] = { 30, 210, 60, 255 };
    ASSERT_TRUE_MSG(fl_image_update_pixels(handle, rebaked), "update_pixels should re-upload into a live handle");
    memset(rebaked, 0, sizeof rebaked); // the caller may free its buffer the moment the call returns
    ASSERT_TRUE_MSG(fl_image_is_ready(handle), "the handle stays ready across a re-upload");

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ASSERT_TRUE_MSG(painter_image(ctx->painter, 200.0f, 200.0f, 0.0f, 0.0f, handle, ui_color(255, 255, 255, 255)),
                        "painter_image: re-uploaded handle not ready");
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    const u8* texel = readback_rgb_at(&buf, 200, 200);
    ASSERT_TRUE_MSG(texel[1] > 150 && texel[0] < 90 && texel[2] < 110,
                    "the re-uploaded (green) pixels must render, not the original red ones");

    ASSERT_FALSE_MSG(fl_image_update_pixels((FlImageHandle)0x7fffffff, pixels),
                     "update_pixels on a handle never created from pixels must fail");

    image_free((ImageHandle)handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.viewport3d_software_placeholder - a Viewport3d under a backend with no 3D seam.
//
// The software backend binds none of mesh_create / mesh_destroy / viewport3d_submit: the element must still
// lay out and fill itself with the placeholder, mesh creation must fail rather than hand back a handle nothing
// can draw, and every 3D call must be a logged no-op instead of a crash. The API's misuse guards are pinned
// here too, since none of them need a GPU.

UTEST(render, viewport3d_software_placeholder) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(5, 7, 11);

    ASSERT_TRUE_MSG(ctx->shared->renderer.viewport3d_submit == nullptr,
                    "the software backend must not bind the 3D seam");

    // Misuse outside any bracket: each of these logs and returns rather than touching the open-viewport state.
    fl_viewport3d_end();
    fl_viewport3d_draw_mesh((FlMeshHandle)1, FL_IMAGE_HANDLE_INVALID, FlImageFilter_Linear);
    fl_viewport3d_draw_lines((FlMeshHandle)1, false);
    static const float identity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    };
    fl_viewport3d_set_view_projection(identity, 16);

    // Mesh creation has no backend to reach, so it must report failure instead of returning a handle.
    FlMeshVertex verts[3] = {
        { .x = 0.0f, .y = 0.0f, .z = 0.0f, .color = { 255, 255, 255, 255 } },
        { .x = 1.0f, .y = 0.0f, .z = 0.0f, .color = { 255, 255, 255, 255 } },
        { .x = 0.0f, .y = 1.0f, .z = 0.0f, .color = { 255, 255, 255, 255 } },
    };
    static const uint16_t tri[3] = { 0, 1, 2 };
    ASSERT_EQ_MSG(fl_mesh_create(verts, 3, tri, 3), (FlMeshHandle)FL_MESH_HANDLE_INVALID,
                  "mesh_create must fail when the backend has no 3D seam");
    ASSERT_EQ_MSG(fl_mesh_create(verts, 3, tri, 4), (FlMeshHandle)FL_MESH_HANDLE_INVALID,
                  "mesh_create must reject an index count that is not whole triangles");
    ASSERT_EQ_MSG(fl_mesh_create_lines(verts, 3, tri, 2), (FlMeshHandle)FL_MESH_HANDLE_INVALID,
                  "mesh_create_lines must fail when the backend has no 3D seam");
    ASSERT_EQ_MSG(fl_mesh_create_lines(verts, 3, tri, 3), (FlMeshHandle)FL_MESH_HANDLE_INVALID,
                  "mesh_create_lines must reject an index count that is not whole segments");
    fl_mesh_destroy(FL_MESH_HANDLE_INVALID); // no-op, must not reach a null seam

    // Two frames: the first lays the element out (so begin reports zero), the second reads that rect back.
    FlVec2 size = { 0 };
    for (int frame = 0; frame < 2; frame++) {
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
            size = fl_viewport3d_begin((FlLayoutId) { .name = FL_S("sw_viewport") });
            fl_viewport3d_set_view_projection(identity, 3); // wrong arity: logged, matrix left untouched
            fl_viewport3d_draw_mesh(FL_MESH_HANDLE_INVALID, FL_IMAGE_HANDLE_INVALID, FlImageFilter_Linear);
            fl_viewport3d_end();
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }

    ASSERT_TRUE_MSG(size.x > 0.0f && size.y > 0.0f,
                    "a Viewport3d must lay out and report its rect even with no 3D backend");

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    const u8* px = readback_rgb_at(&buf, 400, 400);
    ASSERT_TRUE_MSG(px[0] == 24 && px[1] == 24 && px[2] == 28,
                    "a Viewport3d with no 3D backend must fill itself with the placeholder");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.viewport3d_retires_unused_slots - more distinct viewport ids than the table holds, each shown for two
// frames and then dropped, must all lay out: slots not begun in the last frame are reused.

UTEST(render, viewport3d_retires_unused_slots) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 640.0f;
    ctx->layout_height = 480.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 640.0f, .height = 480.0f });

    static const char* const names[]
        = { "vp_a", "vp_b", "vp_c", "vp_d", "vp_e", "vp_f", "vp_g", "vp_h", "vp_i", "vp_j", "vp_k", "vp_l" };
    for (u32 n = 0; n < sizeof(names) / sizeof(names[0]); n++) {
        FlVec2 size = { 0 };
        for (int frame = 0; frame < 2; frame++) {
            ui_update_begin(ctx);
            ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
                size = fl_viewport3d_begin((FlLayoutId) { .name = string_from_cstr(names[n]) });
                fl_viewport3d_end();
            }
            ui_update_end(ctx, 1.0f / 60.0f);
        }
        ASSERT_TRUE_MSG(size.x > 0.0f && size.y > 0.0f, "every viewport id must get a slot, however many came before");
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.viewport3d_live_slots_never_displaced - eight abandoned viewports are retired by eight live ones; with
// the table full of viewports begun every frame, a ninth id is refused and none of the eight is displaced.

UTEST(render, viewport3d_live_slots_never_displaced) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 640.0f;
    ctx->layout_height = 480.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 640.0f, .height = 480.0f });

    static const char* const names[]
        = { "live_a", "live_b", "live_c", "live_d", "live_e", "live_f", "live_g", "live_h", "live_i" };
    // Fill the table with viewports that are then abandoned; the live set below must displace them.
    static const char* const idle_names[]
        = { "idle_a", "idle_b", "idle_c", "idle_d", "idle_e", "idle_f", "idle_g", "idle_h" };
    for (u32 n = 0; n < 8; n++) {
        for (int frame = 0; frame < 2; frame++) {
            ui_update_begin(ctx);
            ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
                fl_viewport3d_begin((FlLayoutId) { .name = string_from_cstr(idle_names[n]) });
                fl_viewport3d_end();
            }
            ui_update_end(ctx, 1.0f / 60.0f);
        }
    }

    FlVec2 sizes[9] = { 0 };
    for (int frame = 0; frame < 4; frame++) {
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
            for (u32 n = 0; n < 9; n++) {
                sizes[n] = fl_viewport3d_begin((FlLayoutId) { .name = string_from_cstr(names[n]) });
                if (sizes[n].x > 0.0f || n < 8) {
                    fl_viewport3d_end();
                }
            }
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }

    for (u32 n = 0; n < 8; n++) {
        ASSERT_TRUE_MSG(sizes[n].x > 0.0f && sizes[n].y > 0.0f, "a viewport begun every frame keeps its slot");
    }
    ASSERT_TRUE_MSG(sizes[8].x == 0.0f && sizes[8].y == 0.0f, "a ninth live viewport has no slot");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.scroll_area_overflow_stays_paired - scroll areas nested past the stack capacity must not pop an
// enclosing area's entry on end.

UTEST(render, scroll_area_overflow_stays_paired) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });

    const u32 total = SCROLL_AREA_STACK_MAX_DEPTH + 3;
    char names[SCROLL_AREA_STACK_MAX_DEPTH + 3][16];

    ui_update_begin(ctx);
    for (u32 i = 0; i < total; i++) {
        snprintf(names[i], sizeof(names[i]), "ovf_%u", i);
        fl_scroll_area_begin((FlLayoutId) { .name = string_from_cstr(names[i]) },
                             (FlScrollAreaConfig) { .sizing = { sizing_fixed(200), sizing_fixed(160) } });
    }
    ASSERT_EQ_MSG((u32)SCROLL_AREA_STACK_MAX_DEPTH, ctx->scroll_area_stack_depth, "stack depth is capped");
    ASSERT_EQ_MSG(3u, ctx->scroll_area_overflow, "areas past the cap are counted");

    for (u32 i = 0; i < total; i++) {
        fl_scroll_area_end();
        if (i < 3) {
            ASSERT_EQ_MSG((u32)SCROLL_AREA_STACK_MAX_DEPTH, ctx->scroll_area_stack_depth,
                          "ending an overflowed area must leave the tracked stack untouched");
        }
    }
    ASSERT_EQ_MSG(0u, ctx->scroll_area_stack_depth, "all tracked areas unwound");
    ASSERT_EQ_MSG(0u, ctx->scroll_area_overflow, "all overflowed areas retired");
    ui_update_end(ctx, 1.0f / 60.0f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.window_teardown_releases_painter_arena - each window context owns a painter arena that ui_destroy
// releases, so opening and closing windows leaves the live arena count where it started.

UTEST(render, window_teardown_releases_painter_arena) {
    FlUiCtx* fixture = render_fixture_get();
    ASSERT_TRUE_MSG(fixture != nullptr, "render fixture creation failed");

    FlMemoryStats before;
    fl_memory_tracker_get_stats(&before);

    // The arena count is process-wide, so threads outside this case can move it by a few; a context that leaked an
    // arena would add at least one per cycle.
    enum { CYCLES = 16, ARENA_COUNT_NOISE = 4 };
    for (int i = 0; i < CYCLES; i++) {
        FlUiCtx* ctx = ui_create(s_arena, fixture->shared);
        ASSERT_TRUE_MSG(ctx != nullptr, "ui_create failed");
        ui_destroy(ctx);
    }
    ui_make_current(fixture);

    FlMemoryStats after;
    fl_memory_tracker_get_stats(&after);
    ASSERT_LE_MSG(after.live_arena_count, before.live_arena_count + ARENA_COUNT_NOISE,
                  "ui_destroy must release every arena ui_create made");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.translucent_image_over - the RGBA (4-channel) image blend must apply the source alpha exactly once
// (premultiplied-over), matching the GPU backend, not rgb·a². create_from_pixels premultiplies the RGBA source
// with the same round-to-nearest /255 rule the GPU's pack_tint_premultiplied uses, and the SW sampler carries
// that premultiplied color + its alpha straight through. The expected value below is that shared premultiply
// followed by premultiplied-over - exactly the GPU backend's compositing.
//
// This exercises the bilinear same-size path (process_8_pixels). The nearest path composites 4-channel texels
// through the same premul_over_8 helper, so it is covered too. The image corner-rounding branch of
// premul_over_8 is not exercised because emit_image never sets RenderMask_Corners.

UTEST(render, translucent_image_over) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    ui_background_set(IMAGE_HANDLE_INVALID);
    const u8 clear_r = 60, clear_g = 60, clear_b = 60;
    ui_background_set_clear_color(clear_r, clear_g, clear_b);

    // A uniform 8x8 RGBA image at 50% alpha. create_from_pixels premultiplies and marks it premultiplied.
    u8 rgba_pixels[8 * 8 * 4];
    for (int i = 0; i < 8 * 8; ++i) {
        rgba_pixels[i * 4 + 0] = 200;
        rgba_pixels[i * 4 + 1] = 80;
        rgba_pixels[i * 4 + 2] = 20;
        rgba_pixels[i * 4 + 3] = 128;
    }
    FlImageHandle img = fl_image_create_from_pixels(rgba_pixels, 8, 8, FlImageFormat_RGBA);
    ASSERT_NE_MSG(img, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "RGBA raw upload returned invalid handle");
    ASSERT_TRUE_MSG(fl_image_is_ready(img), "direct RGBA handle must be ready immediately");

    // Premultiplied-over of the premultiplied source over the clear color (global_alpha 255). The premultiply
    // uses the same round-to-nearest /255 both backends share, so out = premul(c,a) + bg·(255−a)/255.
    const int a = 128;
    const int premul_r = (200 * a + 127) / 255, premul_g = (80 * a + 127) / 255, premul_b = (20 * a + 127) / 255;
    const int exp_r = premul_r + (clear_r * (255 - a) + 127) / 255; // 130
    const int exp_g = premul_g + (clear_g * (255 - a) + 127) / 255; // 70
    const int exp_b = premul_b + (clear_b * (255 - a) + 127) / 255; // 40

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        Painter* p = ctx->painter;
        ASSERT_TRUE_MSG(painter_image(p, 200.0f, 200.0f, 0.0f, 0.0f, img, ui_color(255, 255, 255, 255)),
                        "painter_image: RGBA handle not ready");
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "render buffer data is null");

    const u8* px = readback_rgb_at(&buf, 204, 204); // interior of the 8x8 image
    ASSERT_TRUE_MSG(abs((int)px[0] - exp_r) <= 2 && abs((int)px[1] - exp_g) <= 2 && abs((int)px[2] - exp_b) <= 2,
                    "translucent RGBA image must composite premultiplied-over (once), not rgb·a²");

    image_free((ImageHandle)img);
}

UTEST(render, image_sub_atlas) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    ui_background_set(IMAGE_HANDLE_INVALID);
    const u8 clear_r = 3, clear_g = 5, clear_b = 7;
    ui_background_set_clear_color(clear_r, clear_g, clear_b);

    u8 atlas_pixels[8] = {
        0, 255, 255, 0, 128, 255, 0, 128,
    };
    FlImageHandle atlas = fl_image_create_from_pixels(atlas_pixels, 4, 2, FlImageFormat_Alpha);
    ASSERT_NE_MSG(atlas, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "Alpha atlas upload returned invalid handle");
    ASSERT_TRUE_MSG(fl_image_is_ready(atlas), "alpha atlas handle must be ready immediately");

    memset(atlas_pixels, 0, sizeof atlas_pixels);

    const FlColor left_tint = ui_color(40, 220, 80, 255);
    const FlColor right_tint = ui_color(220, 70, 240, 255);

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ASSERT_TRUE_MSG(fl_painter_image_sub(100.0f, 100.0f, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, 2.0f, atlas, left_tint),
                        "direct image_sub draw failed");
        ASSERT_TRUE_MSG(fl_painter_image_sub(108.0f, 100.0f, 2.0f, 2.0f, 2.0f, 0.0f, 2.0f, 2.0f, atlas, right_tint),
                        "second image_sub draw failed");
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(ctx->shared->renderer.read_pixels != nullptr, "backend has no pixel-readback capability");
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "render buffer data is null");
    ASSERT_TRUE_MSG(buf.width == 1920 && buf.height == 1080, "unexpected render buffer dimensions");

    const u8* left_zero = readback_rgb_at(&buf, 100, 100);
    ASSERT_TRUE_MSG(left_zero[0] == clear_r && left_zero[1] == clear_g && left_zero[2] == clear_b,
                    "left cell alpha-zero texel should leave clear color unchanged");
    const u8* left_full = readback_rgb_at(&buf, 101, 100);
    ASSERT_TRUE_MSG(left_full[0] > 25 && left_full[1] > 170 && left_full[2] > 55,
                    "left cell full-alpha texel should use the left tint");
    const u8* left_half = readback_rgb_at(&buf, 100, 101);
    ASSERT_TRUE_MSG(left_half[0] > clear_r && left_half[1] > 80 && left_half[2] > clear_b,
                    "left cell half-alpha texel should blend the left tint");

    const u8* right_full = readback_rgb_at(&buf, 108, 100);
    ASSERT_TRUE_MSG(right_full[0] > 170 && right_full[1] > 45 && right_full[2] > 180,
                    "right cell full-alpha texel should use the right tint");
    const u8* right_zero = readback_rgb_at(&buf, 109, 100);
    ASSERT_TRUE_MSG(right_zero[0] == clear_r && right_zero[1] == clear_g && right_zero[2] == clear_b,
                    "right cell alpha-zero texel should leave clear color unchanged");
    const u8* outside = readback_rgb_at(&buf, 112, 100);
    ASSERT_TRUE_MSG(outside[0] == clear_r && outside[1] == clear_g && outside[2] == clear_b,
                    "pixel outside atlas-cell draws should remain clear");

    dump_frame("image_sub", &buf);

    image_free((ImageHandle)atlas);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// perf_fence CPU micro-scenes - the remaining isolating scenes beyond scene_raster_fill. Each
// isolates ONE hot path so a regression points at a cause, and reuses the tier-1/3 harness above.

// scene_text_elided: N narrow boxes each holding a long single-word label that must elide (truncate to the
// longest fitting prefix + U+2026). Stresses the uncached O(chars^2) elide-measurement path - every label is
// re-measured each frame.
static void scene_text_elided(FlUiCtx* ctx, int n) {
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 2)); // built-in 8x8 font at scale 2
    const FlString label = { .data = "AAAAAAAAAAAAAAAAAAAA", .length = 20, .is_static = 1 }; // 20 glyphs; box fits 4
    const FlColor white = { 240, 240, 240, 255 };

    ui_update_begin(ctx);
    fl_layout_start(ID("scene_text_elided_root"));
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { .width = sizing_grow(), .height = sizing_grow() },
        .direction = FlLayoutDirection_TopToBottom,
        .color = ui_color(20, 20, 24, 255),
        .clip = { .vertical = true },
    });
    for (int i = 0; i < n; i++) {
        fl_layout_start(ID_INT("scene_text_elided_box", i));
        fl_layout_configure((FlLayoutConfig) { .sizing = { sizing_fixed(64), sizing_fixed(20) } });
        ui_text_styled(label, .color = white, .no_wrap = true, .overflow = FlOverflow_Elide);
        fl_layout_end();
    }
    fl_layout_end();
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(perf_fence, scene_text_elided) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const int n = 256;
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });

    const u64 t0 = get_monotonic_time_ns_os();
    scene_text_elided(ctx, n);
    const u64 t1 = get_monotonic_time_ns_os();

    const FlFrameStats stats = perf_fence_frame_stats(ctx);
    printf("[perf_fence] scene_text_elided(N=%d): %llu ns, render_commands=%u bin_alloc_bytes=%u\n", n,
           (unsigned long long)(t1 - t0), stats.render_commands, stats.bin_alloc_bytes);

    ASSERT_EQ_MSG(stats.render_commands, 58u, "render_commands baseline drift");
    ASSERT_EQ_MSG(stats.bin_alloc_bytes, 454920u, "bin_alloc_bytes baseline drift");
}

UTEST(perf_fence, scene_many_elements) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const int n = 16384; // > Clay's 8192 default cap: proves the fixture's Clay_SetMaxElementCount is load-bearing
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });

    const u64 t0 = get_monotonic_time_ns_os();
    scene_many_elements(ctx, n);
    const u64 t1 = get_monotonic_time_ns_os();

    const FlFrameStats stats = perf_fence_frame_stats(ctx);
    printf(
        "[perf_fence] scene_many_elements(N=%d): %llu ns, render_commands=%u bin_alloc_bytes=%u clip_broadcasts=%u\n",
        n, (unsigned long long)(t1 - t0), stats.render_commands, stats.bin_alloc_bytes, stats.clip_broadcasts);

    ASSERT_EQ_MSG(stats.render_commands, 274u, "render_commands baseline drift");
    ASSERT_EQ_MSG(stats.bin_alloc_bytes, 558960u, "bin_alloc_bytes baseline drift");
}

// scene_floating: N floating overlays attached to root, each a distinct z_index holding a small colored fill.
// Stresses the z-index root bubble sort (O(N^2) over floating roots).
static void scene_floating(FlUiCtx* ctx, int n) {
    const FlColor fill = ui_color(200, 120, 60, 255);

    ui_update_begin(ctx);
    fl_layout_start(ID("scene_floating_root"));
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { .width = sizing_grow(), .height = sizing_grow() },
        .color = ui_color(20, 20, 24, 255),
    });
    for (int i = 0; i < n; i++) {
        fl_floating_area_id(FL_ID_INT("scene_floating_overlay", i), .attach_to = FlFloatingAttachTo_Root,
                            .z_index = (int16_t)i, .sizing = { sizing_fixed(16), sizing_fixed(16) }) {
            fl_layout_start(ID_INT("scene_floating_fill", i));
            fl_layout_configure((FlLayoutConfig) { .sizing = { sizing_grow(), sizing_grow() }, .color = fill });
            fl_layout_end();
        }
    }
    fl_layout_end();
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(perf_fence, scene_floating) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const int n = 256; // distinct z_index 0..255 → 256^2 bubble-sort comparisons
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });

    const u64 t0 = get_monotonic_time_ns_os();
    scene_floating(ctx, n);
    const u64 t1 = get_monotonic_time_ns_os();

    const FlFrameStats stats = perf_fence_frame_stats(ctx);
    printf("[perf_fence] scene_floating(N=%d): %llu ns, render_commands=%u bin_alloc_bytes=%u\n", n,
           (unsigned long long)(t1 - t0), stats.render_commands, stats.bin_alloc_bytes);

    ASSERT_EQ_MSG(stats.render_commands, 257u, "render_commands baseline drift");
    ASSERT_EQ_MSG(stats.bin_alloc_bytes, 524280u, "bin_alloc_bytes baseline drift");
}

// scene_dyn_images: N draws of one small source image at a scaled destination size, so each draw is a
// RenderMask_TextureScaled command that rebuilds its shuffle table every frame (calculate_shuffle_tables).
// Isolates the per-frame re-deinterleave / shuffle-table-rebuild hot path; stat.scaled_image_setups equals N
// and is the tier-1 anchor that a caching optimization would drop.
static ImageHandle s_dyn_image
    = IMAGE_HANDLE_INVALID; // set by the UTEST before the run; the fixed-signature scene reads it

static void scene_dyn_images(FlUiCtx* ctx, int n) {
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        Painter* p = ctx->painter;
        for (int i = 0; i < n; i++) {
            const float x = (float)((i % 60) * 20); // 60-wide grid, 16px draws → all on-screen within 1920x1080
            const float y = (float)((i / 60) * 20);
            painter_image(p, x, y, 16.0f, 16.0f, (FlImageHandle)s_dyn_image, (FlColor) { 255, 255, 255, 255 });
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(perf_fence, scene_dyn_images) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const int n = 256;
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(20, 20, 24);

    // k_png_red is a 4x4 RGB PNG; drawing it at 16x16 (dst != src width) forces the scaled-texture path.
    s_dyn_image = image_load_from_memory(k_png_red, sizeof(k_png_red), (FlImageLoadOptions) { 0 });
    ASSERT_TRUE_MSG(s_dyn_image != IMAGE_HANDLE_INVALID, "image load returned invalid handle");
    ASSERT_TRUE_MSG(wait_image_ready(s_dyn_image), "image did not decode in time");

    const u64 t0 = get_monotonic_time_ns_os();
    scene_dyn_images(ctx, n);
    const u64 t1 = get_monotonic_time_ns_os();

    const FlFrameStats stats = perf_fence_frame_stats(ctx);
    printf(
        "[perf_fence] scene_dyn_images(N=%d): %llu ns, render_commands=%u scaled_image_setups=%u bin_alloc_bytes=%u\n",
        n, (unsigned long long)(t1 - t0), stats.render_commands, stats.scaled_image_setups, stats.bin_alloc_bytes);

    ASSERT_EQ_MSG(stats.scaled_image_setups, 256u,
                  "scaled_image_setups baseline drift"); // one shuffle rebuild per image
    ASSERT_EQ_MSG(
        stats.render_commands, 3u,
        "render_commands baseline drift"); // painter draws map to one CUSTOM span, not per-image Clay commands
}

// scene_widget_gallery: a representative full frame adapted from examples/widget_gallery. No input is pumped, so
// every interaction is a no-op and the fixed-value widget state makes each run identical (the two dialogs' open
// bools are false, so ui_message_box early-returns). n is the (virtualized) List row count.
#define GALLERY_DECL_ROWS 12

#define GALLERY_SECTION                                                                                   \
    ui_layout_area(.direction = LayoutDirection_TopToBottom, .child_gap = 10, .padding = padding_all(16), \
                   .sizing = { .width = sizing_grow() }, .color = ui_color(32, 34, 42, 255),              \
                   .radius = corner_radius(8))

static void gallery_section_header(FlString title) {
    ui_text_styled(title, .color = ui_color(150, 200, 255, 255));
}

static void scene_widget_gallery(FlUiCtx* ctx, int n) {
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 2)); // built-in bitmap font, no asset dep

    // Clay holds text char pointers until ui_update_end (no copy), so any dynamic label must outlive the frame.
    char clicked_buf[32];

    int clicks = 3;
    bool checkbox = true, toggle = true;
    float volume = 0.5f;
    int count = 5, dropdown_choice = 1, radio_choice = 2, tab = 1;
    bool dropdown_open = false, alert_open = false, confirm_open = false;

    static u64 list_ids[512];
    static FlString list_labels[512];
    static char list_label_buf[512][16];
    if (n > 512) {
        n = 512;
    }
    for (int i = 0; i < n; i++) {
        list_ids[i] = (u64)(i + 1);
        const int len = snprintf(list_label_buf[i], sizeof(list_label_buf[i]), "Item %d", i + 1);
        list_labels[i] = (FlString) { .data = list_label_buf[i], .length = (u32)len };
    }
    static FlListItem decl_items[GALLERY_DECL_ROWS];
    static char decl_label_buf[GALLERY_DECL_ROWS][16];
    for (int i = 0; i < GALLERY_DECL_ROWS; i++) {
        const int len = snprintf(decl_label_buf[i], sizeof(decl_label_buf[i]), "Row %d", i + 1);
        decl_items[i] = (FlListItem) { .id = (u64)(i + 1),
                                       .label = (FlString) { .data = decl_label_buf[i], .length = (u32)len },
                                       .icon = FL_IMAGE_HANDLE_INVALID };
    }
    FlSelectionSet* list_sel = selection_set_create(64);
    FlSelectionSet* decl_sel = selection_set_create(16);

    ui_update_begin(ctx);

    ui_message_box(FL_S("Heads up"), FL_S("This is a single-button alert."), &alert_open,
                   .confirm_label = FL_S("Got it"));
    ui_message_box(FL_S("Delete item?"), FL_S("This can't be undone."), &confirm_open, .confirm_label = FL_S("Delete"),
                   .cancel_label = FL_S("Keep"));

    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = ui_color(20, 21, 27, 255),
                   .direction = LayoutDirection_TopToBottom, .child_gap = 0) {
        ui_menubar(ID("gallery_menubar")) {
            ui_menubar_menu(FL_S("File")) {
                ui_menu_item(FL_S("New"), .shortcut = FL_S("Ctrl+N"));
                ui_menu_item(FL_S("Open"), .shortcut = FL_S("Ctrl+O"));
                ui_menu_separator();
                ui_menu_item(FL_S("Save"), .shortcut = FL_S("Ctrl+S"), .disabled = true);
                ui_submenu(FL_S("Recent")) {
                    ui_menu_item(FL_S("project.em"));
                    ui_submenu(FL_S("Older")) {
                        ui_menu_item(FL_S("draft.em"));
                        ui_menu_item(FL_S("scratch.em"));
                    }
                }
            }
            ui_menubar_menu(FL_S("Edit")) {
                ui_menu_item(FL_S("Undo"), .shortcut = FL_S("Ctrl+Z"));
                ui_menu_item(FL_S("Redo"), .shortcut = FL_S("Ctrl+Shift+Z"));
                ui_menu_separator();
                ui_menu_header(FL_S("Clipboard"));
                ui_menu_item(FL_S("Cut"), .shortcut = FL_S("Ctrl+X"));
                ui_menu_item(FL_S("Copy"), .shortcut = FL_S("Ctrl+C"));
                ui_menu_item(FL_S("Paste"), .shortcut = FL_S("Ctrl+V"));
            }
            ui_menubar_menu(FL_S("View")) {
                ui_menu_check(FL_S("Feature enabled"), &checkbox);
                ui_menu_separator();
                ui_menu_header(FL_S("Quality"));
                ui_menu_radio(FL_S("Low quality"), &radio_choice, 0);
                ui_menu_radio(FL_S("Medium quality"), &radio_choice, 1);
                ui_menu_radio(FL_S("High quality"), &radio_choice, 2);
            }
        }

        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                       .direction = LayoutDirection_TopToBottom, .child_gap = 16, .padding = padding_all(24)) {
            ui_text_styled(FL_S("flowi — Widget Gallery"), .color = ui_color(235, 235, 245, 255));

            ui_scroll_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                           .direction = LayoutDirection_TopToBottom, .child_gap = 16, .show_scrollbar = true,
                           .smooth_scroll = true) {
                GALLERY_SECTION {
                    gallery_section_header(FL_S("Button"));
                    ui_button(FL_S("Click me"));
                    const int clen = snprintf(clicked_buf, sizeof(clicked_buf), "Clicked %d times", clicks);
                    ui_text((FlString) { .data = clicked_buf, .length = (u32)clen });
                }
                GALLERY_SECTION {
                    gallery_section_header(FL_S("Checkbox"));
                    ui_checkbox(FL_S("Enable feature"), &checkbox);
                }
                GALLERY_SECTION {
                    gallery_section_header(FL_S("Switch"));
                    ui_switch(FL_S("Enable notifications"), &toggle);
                }
                GALLERY_SECTION {
                    gallery_section_header(FL_S("Radio group"));
                    FlString options[] = { FL_S("Low"), FL_S("Medium"), FL_S("High") };
                    ui_radio_group(options, (int)(sizeof(options) / sizeof(options[0])), &radio_choice);
                }
                GALLERY_SECTION {
                    gallery_section_header(FL_S("Slider"));
                    ui_slider_float(FL_S("Volume"), &volume, 0.0f, 1.0f);
                    ui_slider_int(FL_S("Count"), &count, 0, 10);
                }
                GALLERY_SECTION {
                    gallery_section_header(FL_S("Progress bar"));
                    ui_progress_bar(volume);
                }
                GALLERY_SECTION {
                    gallery_section_header(FL_S("Dropdown"));
                    FlString options[] = { FL_S("Low"), FL_S("Medium"), FL_S("High") };
                    ui_dropdown(FL_S("Quality"), &dropdown_open, options, (int)(sizeof(options) / sizeof(options[0])),
                                &dropdown_choice);
                }
                GALLERY_SECTION {
                    gallery_section_header(FL_S("Tab bar"));
                    FlString tabs[] = { FL_S("General"), FL_S("Audio"), FL_S("Video") };
                    ui_tab_bar(tabs, (int)(sizeof(tabs) / sizeof(tabs[0])), &tab);
                }
                GALLERY_SECTION {
                    gallery_section_header(FL_S("Tooltip"));
                    ui_button(FL_S("Hover for a tip"));
                    ui_tooltip(ID("Hover for a tip"), FL_S("Tooltips reveal after a short hover-dwell."));
                }
                GALLERY_SECTION {
                    gallery_section_header(FL_S("Message box"));
                    ui_button(FL_S("Show alert"));
                    ui_button(FL_S("Show confirm"));
                }
                GALLERY_SECTION {
                    gallery_section_header(FL_S("Context menu"));
                    ui_text(FL_S("Right-click inside the panel below."));
                    ui_menu_region(ID("ctx_region"), ID("ctx_menu")) {
                        ui_layout_area(.sizing = { .width = sizing_grow() }, .padding = padding_all(24),
                                       .color = ui_color(44, 46, 56, 255), .radius = corner_radius(6),
                                       .direction = LayoutDirection_TopToBottom) {
                            ui_text_styled(FL_S("Right-click here"), .color = ui_color(150, 200, 255, 255));
                        }
                    }
                }
                GALLERY_SECTION {
                    gallery_section_header(FL_S("List (virtualized)"));
                    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_fixed(180) },
                                   .direction = LayoutDirection_TopToBottom) {
                        FlListRange r
                            = ui_list_begin(ID("gallery_list"), list_ids, (u32)n, list_sel,
                                            (FlListConfig) { .row_height = 26.0f, .gap = 4.0f, .labels = list_labels });
                        for (u32 i = r.start; i < r.end; i++) {
                            ui_list_item_begin(list_ids[i]);
                            ui_layout_area(.sizing = { .width = sizing_grow() }, .padding = padding_all(6),
                                           .radius = corner_radius(4)) {
                                ui_text(list_labels[i]);
                            }
                            ui_list_item_end();
                        }
                        ui_list_end();
                    }
                }
                GALLERY_SECTION {
                    gallery_section_header(FL_S("List (declarative)"));
                    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_fixed(150) },
                                   .direction = LayoutDirection_TopToBottom) {
                        ui_list(ID("gallery_decl_list"), decl_items, GALLERY_DECL_ROWS, decl_sel,
                                (FlListOpts) {
                                    .row_height = 26.0f, .gap = 4.0f, .multi_select = true, .reorderable = true });
                    }
                }
            }
        }
    }

    ui_menu(ID("ctx_menu"), .min_width = 220) {
        ui_menu_item(FL_S("Cut"), .shortcut = FL_S("Ctrl+X"));
        ui_menu_item(FL_S("Copy"), .shortcut = FL_S("Ctrl+C"));
        ui_menu_item(FL_S("Paste"), .shortcut = FL_S("Ctrl+V"), .disabled = true);
        ui_menu_separator();
        ui_menu_item(FL_S("Keep me open"), .close_policy = FlMenuClosePolicy_KeepOpen);
        ui_submenu(FL_S("More")) {
            ui_menu_item(FL_S("Duplicate"));
            ui_menu_item(FL_S("Delete"));
        }
    }

    ui_update_end(ctx, 1.0f / 60.0f);

    selection_set_destroy(list_sel);
    selection_set_destroy(decl_sel);
}

#undef GALLERY_SECTION

#undef GALLERY_DECL_ROWS

UTEST(perf_fence, scene_widget_gallery) {
    render_fixture_teardown(); // the command count baseline is for a context that has drawn nothing yet
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const int n = 500; // matches the example's list row count; the list is virtualized to a 180px viewport
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });

    const u64 t0 = get_monotonic_time_ns_os();
    scene_widget_gallery(ctx, n);
    const u64 t1 = get_monotonic_time_ns_os();

    const FlFrameStats stats = perf_fence_frame_stats(ctx);
    printf(
        "[perf_fence] scene_widget_gallery(N=%d): %llu ns, render_commands=%u bin_alloc_bytes=%u clip_broadcasts=%u\n",
        n, (unsigned long long)(t1 - t0), stats.render_commands, stats.bin_alloc_bytes, stats.clip_broadcasts);

    ASSERT_EQ_MSG(stats.render_commands, 67u, "render_commands baseline drift");
    ASSERT_EQ_MSG(stats.bin_alloc_bytes, 820080u, "bin_alloc_bytes baseline drift");
    ASSERT_EQ_MSG(stats.clip_broadcasts, 1020u, "clip_broadcasts baseline drift");
}

UTEST(render, theme_scope) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(12, 12, 14);

    static const FlThemeColors base = {
        .interactive.base = { 200, 30, 40, 255 },
        .interactive.hover = { 220, 60, 70, 255 },
        .interactive.press = { 170, 20, 30, 255 },
        .interactive.focus = { 255, 255, 255, 255 },
        .interactive.disabled = { 200, 30, 40, 128 },
        .surface.base = { 18, 20, 24, 255 },
        .surface.elevated = { 36, 38, 44, 255 },
        .surface.overlay = { 8, 9, 11, 255 },
        .surface.selected = { 200, 30, 40, 255 },
        .border.default_color = { 50, 52, 60, 255 },
        .border.focus = { 230, 80, 92, 255 },
        .border.separator = { 44, 46, 52, 255 },
        .text.primary = { 240, 240, 240, 255 },
        .text.secondary = { 150, 150, 150, 255 },
        .text.accent = { 200, 30, 40, 255 },
        .feedback.success = { 76, 175, 80, 255 },
        .feedback.warning = { 255, 152, 0, 255 },
        .feedback.error = { 244, 67, 54, 255 },
        .feedback.info = { 33, 150, 243, 255 },
    };
    ui_set_theme(ctx, &base);

    const FlColor override_surface = { 60, 20, 20, 255 };     // override A: surface.base
    const FlColor override_interactive = { 20, 60, 20, 255 }; // override A: interactive.base
    const FlColor override_text = { 220, 220, 40, 255 };      // override B (nested): text.primary
    const FlColor per_object = { 10, 200, 200, 255 };         // explicit per-object color, never themed

    ASSERT_EQ_MSG(ctx->theme_override_depth, 0, "override stack must start empty");

    FlColor captured[8];
    captured[0] = ui_get_theme_colors(ctx)->surface.base;

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = base.surface.base,
                   .direction = FlLayoutDirection_LeftToRight, .padding = fl_padding_md(), .child_gap = FlSpacing_LG) {
        ui_layout_area(.sizing = { sizing_fixed(140), sizing_fixed(140) }, .color = captured[0]) {}

        ui_theme_push(ctx, (FlThemeOverride) {
                               .slots = FlThemeSlot_SurfaceBase | FlThemeSlot_InteractiveBase,
                               .colors = { .surface.base = override_surface, .interactive.base = override_interactive },
                           });
        const FlThemeColors* scope_a = ui_get_theme_colors(ctx);
        ASSERT_TRUE_MSG(color_eq(scope_a->surface.base, override_surface), "A: surface.base must resolve to override");
        ASSERT_TRUE_MSG(color_eq(scope_a->interactive.base, override_interactive),
                        "A: interactive.base must resolve to override");
        ASSERT_TRUE_MSG(color_eq(scope_a->text.primary, base.text.primary),
                        "A: unflagged text.primary must inherit the base theme");
        captured[1] = scope_a->surface.base;
        captured[2] = scope_a->interactive.base;
        captured[3] = per_object;
        ui_layout_area(.sizing = { sizing_fixed(140), sizing_fixed(140) }, .color = captured[1]) {}
        ui_layout_area(.sizing = { sizing_fixed(140), sizing_fixed(140) }, .color = captured[2]) {}
        ui_layout_area(.sizing = { sizing_fixed(140), sizing_fixed(140) }, .color = captured[3]) {}

        ui_theme_push(ctx, (FlThemeOverride) {
                               .slots = FlThemeSlot_TextPrimary,
                               .colors = { .text.primary = override_text },
                           });
        const FlThemeColors* scope_b = ui_get_theme_colors(ctx);
        ASSERT_TRUE_MSG(color_eq(scope_b->text.primary, override_text), "B: text.primary must resolve to override");
        ASSERT_TRUE_MSG(color_eq(scope_b->surface.base, override_surface),
                        "B: surface.base must inherit override A (nested stacking)");
        ASSERT_TRUE_MSG(color_eq(scope_b->interactive.base, override_interactive),
                        "B: interactive.base must inherit override A (nested stacking)");
        captured[4] = scope_b->text.primary;
        captured[5] = scope_b->surface.base;
        ui_layout_area(.sizing = { sizing_fixed(140), sizing_fixed(140) }, .color = captured[4]) {}
        ui_layout_area(.sizing = { sizing_fixed(140), sizing_fixed(140) }, .color = captured[5]) {}

        ui_theme_pop(ctx);
        const FlThemeColors* scope_a2 = ui_get_theme_colors(ctx);
        ASSERT_TRUE_MSG(color_eq(scope_a2->text.primary, base.text.primary),
                        "after popping B, text.primary must revert to the base theme");
        ASSERT_TRUE_MSG(color_eq(scope_a2->surface.base, override_surface),
                        "after popping B, surface.base must still be override A");
        captured[6] = scope_a2->text.primary;
        ui_layout_area(.sizing = { sizing_fixed(140), sizing_fixed(140) }, .color = captured[6]) {}

        ui_theme_pop(ctx);
        const FlThemeColors* outer = ui_get_theme_colors(ctx);
        ASSERT_TRUE_MSG(color_eq(outer->surface.base, base.surface.base),
                        "after popping A, surface.base must revert to the base theme");
        ASSERT_TRUE_MSG(color_eq(outer->interactive.base, base.interactive.base),
                        "after popping A, interactive.base must revert to the base theme");
        captured[7] = outer->surface.base;
        ui_layout_area(.sizing = { sizing_fixed(140), sizing_fixed(140) }, .color = captured[7]) {}
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_EQ_MSG(ctx->theme_override_depth, 0, "override stack must be balanced after equal push/pop");

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(ctx->shared->renderer.read_pixels != nullptr, "backend has no pixel-readback capability");
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "render buffer data is null");
    ASSERT_TRUE_MSG(buf.width == 1920 && buf.height == 1080, "unexpected render buffer dimensions");
    dump_frame("theme_scope", &buf);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.theme_scope_frame_hygiene - a scoped override is frame-local. If a caller pushes but forgets to pop, the
// override must not leak into the next frame: ui_update_begin resets the override depth to 0.

UTEST(render, theme_scope_frame_hygiene) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });

    static const FlThemeColors base = {
        .surface.base = { 18, 20, 24, 255 },
        .text.primary = { 240, 240, 240, 255 },
    };
    ui_set_theme(ctx, &base);

    // Push an override and deliberately never pop it within the frame.
    ui_update_begin(ctx);
    ui_theme_push(ctx, (FlThemeOverride) {
                           .slots = FlThemeSlot_SurfaceBase,
                           .colors = { .surface.base = { 90, 10, 10, 255 } },
                       });
    ASSERT_EQ_MSG(ctx->theme_override_depth, 1, "push must raise the override depth");
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .color = ui_get_theme_colors(ctx)->surface.base) {}
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_EQ_MSG(ctx->theme_override_depth, 1, "forgotten pop leaves the override on the stack this frame");

    ui_update_begin(ctx);
    ASSERT_EQ_MSG(ctx->theme_override_depth, 0, "ui_update_begin must reset the override depth for the new frame");
    ASSERT_TRUE_MSG(color_eq(ui_get_theme_colors(ctx)->surface.base, base.surface.base),
                    "after frame reset, surface.base must resolve to the base theme");
    ui_update_end(ctx, 1.0f / 60.0f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.theme_stack_fl_api - drives the theme override stack through the generated direct API: fl_theme_update
// injects the base palette, fl_theme_push layers a sparse override, fl_theme_pop unwinds it. The fl_theme_*
// forwarders thread the file-local s_ctx (set by ui_create in the fixture). base is static because
// fl_theme_update stores the pointer (caller-owned storage).

UTEST(render, theme_stack_fl_api) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ASSERT_TRUE_MSG(ui_get_ctx() == ctx, "fl_theme_* forward to s_ctx; fixture ctx must be the current ctx");

    static const FlThemeColors base = {
        .interactive.base = { 200, 30, 40, 255 },
        .surface.base = { 18, 20, 24, 255 },
        .surface.elevated = { 36, 38, 44, 255 },
        .text.primary = { 240, 240, 240, 255 },
    };
    const FlColor override_surface = { 60, 20, 20, 255 };

    fl_theme_update(&base);
    ASSERT_EQ_MSG(ctx->theme_override_depth, 0, "override stack must start empty");
    ASSERT_TRUE_MSG(color_eq(fl_theme_colors()->surface.base, base.surface.base),
                    "with no override pushed, surface.base must resolve to the injected base");

    fl_theme_push((FlThemeOverride) {
        .slots = FlThemeSlot_SurfaceBase,
        .colors = { .surface.base = override_surface },
    });
    ASSERT_EQ_MSG(ctx->theme_override_depth, 1, "push must raise the override depth");
    const FlThemeColors* scoped = fl_theme_colors();
    ASSERT_TRUE_MSG(color_eq(scoped->surface.base, override_surface),
                    "flagged surface.base must resolve to the override");
    ASSERT_TRUE_MSG(color_eq(scoped->surface.elevated, base.surface.elevated),
                    "unflagged surface.elevated must inherit the base theme");
    ASSERT_TRUE_MSG(color_eq(scoped->text.primary, base.text.primary),
                    "unflagged text.primary must inherit the base theme");

    fl_theme_pop();
    ASSERT_EQ_MSG(ctx->theme_override_depth, 0, "pop must restore the override depth");
    ASSERT_TRUE_MSG(color_eq(fl_theme_colors()->surface.base, base.surface.base),
                    "after pop, surface.base must revert to the injected base");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.public_read_getters - the narrow read getters: fl_theme_colors (live palette), fl_focused_id (focused
// widget id, no FlUiCtx exposed), and fl_list_clipper_create (visible-row window).

UTEST(render, public_read_getters) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ASSERT_TRUE_MSG(ui_get_ctx() == ctx, "getters read the current ctx; fixture ctx must be current");

    static const FlThemeColors base = { .surface.base = { 21, 22, 23, 255 } };
    fl_theme_update(&base);
    const FlThemeColors* colors = fl_theme_colors();
    ASSERT_TRUE_MSG(colors != nullptr, "fl_theme_colors must return a live palette");
    ASSERT_TRUE_MSG(color_eq(colors->surface.base, base.surface.base),
                    "fl_theme_colors must resolve to the injected base palette");

    ASSERT_EQ_MSG(fl_focused_id(), 0u, "fl_focused_id must report 0 when nothing is focused");
    fl_set_focus(ID("getters_focus_target"));
    ASSERT_EQ_MSG(fl_focused_id(), ui_layout_id_to_clay_id(ID("getters_focus_target")),
                  "fl_focused_id must return the widget id that took focus");

    // 100 items of 20px, scrolled 200px into a 100px viewport → rows [10, 16): first = 200/20 = 10;
    // last = (200+100)/20 + 1 = 16.
    FlListClipper clip = fl_list_clipper_create(100, 20.0f, 200.0f, 100.0f);
    ASSERT_EQ_MSG(clip.display_start, 10, "clipper display_start must be 10 for the known viewport");
    ASSERT_EQ_MSG(clip.display_end, 16, "clipper display_end must be 16 for the known viewport");
    ASSERT_TRUE_MSG(clip.items_height == 2000.0f, "clipper items_height must be total_items * item_height");

    // A zero item height (an unmeasured row on the first frame) would divide to inf and convert to int as
    // undefined behaviour; the window must come back empty instead.
    FlListClipper degenerate = fl_list_clipper_create(100, 0.0f, 200.0f, 100.0f);
    ASSERT_EQ_MSG(degenerate.display_start, 0, "zero item height must yield an empty window");
    ASSERT_EQ_MSG(degenerate.display_end, 0, "zero item height must yield an empty window");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.public_nav_query - the public nav/action query surface: fl_action_pressed reports the action a key press
// bound to it raised this frame, and fl_set_focus + fl_focused_id round-trip a layout id.

UTEST(render, public_nav_query) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ASSERT_TRUE_MSG(ui_get_ctx() == ctx, "fl_* nav queries read the current ctx; fixture ctx must be current");

    ctx->input_state.keyboard.keys_pressed[FlKey_Enter] = true;
    ui_actions_update(ctx);
    ASSERT_TRUE_MSG(fl_action_pressed(FlAction_Confirm), "fl_action_pressed must report a pressed action");
    ASSERT_FALSE_MSG(fl_action_pressed(FlAction_Cancel), "fl_action_pressed must report an unpressed action false");
    ctx->input_state.keyboard.keys_pressed[FlKey_Enter] = false;
    ui_actions_update(ctx);
    ASSERT_FALSE_MSG(fl_action_pressed(FlAction_Confirm), "fl_action_pressed must clear once the key is released");

    fl_set_focus(ID("nav_query_focus_target"));
    ASSERT_EQ_MSG(fl_focused_id(), ui_layout_id_to_clay_id(ID("nav_query_focus_target")),
                  "fl_set_focus + fl_focused_id must round-trip the resolved widget id");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.public_widget_api_smoke - every widget on the public fl_* surface is called once inside one real frame
// through its generated bare-primary name + convenience macro; a frame that emits render commands proves the
// widgets laid out. No interaction is injected, so every returned Signal is quiescent (.clicked false).

UTEST(render, public_widget_api_smoke) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ASSERT_TRUE_MSG(ui_get_ctx() == ctx, "fl_* widgets forward to the current ctx; fixture ctx must be current");

    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });

    bool checked = false, switched = true, pilled = false, chipped = true, dropdown_open = false;
    int radio_sel = 0, dropdown_sel = 1, tab_sel = 0;
    float scroll = 0.25f;
    FlString radio_opts[] = { FL_S("One"), FL_S("Two"), FL_S("Three") };
    FlString dropdown_opts[] = { FL_S("Low"), FL_S("High") };
    FlString tabs[] = { FL_S("General"), FL_S("Audio") };

    // A small valid RGBA buffer so the dynamic-image element references real memory.
    static u8 dyn_pixels[4 * 4 * 4] = { 0 };
    FlDynamicImage dyn = {
        .user_buffer = dyn_pixels,
        .width = 4,
        .height = 4,
        .user_stride = 4 * 4,
        .is_alpha_only = false,
    };

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        FlSignal cb = fl_checkbox(FL_S("Check"), &checked);
        FlSignal sw = fl_switch(FL_S("Switch"), &switched);
        FlSignal rg = fl_radio_group(radio_opts, 3, &radio_sel);
        FlSignal pl = fl_pill(FL_S("Pill"), &pilled);
        FlSignal ch = fl_chip(FL_S("Chip"), &chipped);
        fl_progress_bar(0.5f);
        FlSignal sb = fl_scrollbar(&scroll);
        fl_dropdown(FL_S("Quality"), &dropdown_open, dropdown_opts, 2, &dropdown_sel);
        bool tab_changed = fl_tab_bar(tabs, 2, &tab_sel);
        FlSignal lk = fl_link(FL_S("Click here"));
        fl_image(0, (FlImageOpts) { .color = ui_color(255, 255, 255,
                                                      255) }); // invalid handle: ui_image_impl no-ops, must not crash
        fl_dynamic_image(&dyn, (FlImageOpts) { .color = ui_color(255, 255, 255, 255) });
        fl_spacer(8.0f);
        fl_spacer_h(8.0f);

        ASSERT_FALSE_MSG(cb.clicked, "checkbox must not report a click with no input");
        ASSERT_FALSE_MSG(sw.clicked, "switch must not report a click with no input");
        ASSERT_FALSE_MSG(rg.clicked, "radio group must not report a selection change with no input");
        ASSERT_FALSE_MSG(pl.clicked, "pill must not report a click with no input");
        ASSERT_FALSE_MSG(ch.clicked, "chip must not report a click with no input");
        ASSERT_FALSE_MSG(sb.clicked, "scrollbar must not report a click with no input");
        ASSERT_FALSE_MSG(lk.clicked, "link must not report a click with no input");
        ASSERT_FALSE_MSG(tab_changed, "tab bar must not report a change with no input");
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_TRUE_MSG(ui_get_render_command_count() > 0, "public widget frame must capture render commands");

    ASSERT_EQ_MSG(radio_sel, 0, "radio selection unchanged with no input");
    ASSERT_EQ_MSG(tab_sel, 0, "tab selection unchanged with no input");
    ASSERT_EQ_MSG(dropdown_sel, 1, "dropdown selection unchanged with no input");
    ASSERT_FALSE_MSG(dropdown_open, "dropdown stays closed with no input");
    // (scrollbar recomputes *value from thumb geometry every frame, so it is intentionally not asserted unchanged)
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.state_derivation - interactive-state color derivation. Resolves fl_resolve_widget_colors for synthetic
// ItemState at stepped hot_t / active_t / focused_t factors: the OKLCH lighten/darken hover/press derivation and the
// OKLab focus-border lerp. The assertions are the endpoints the theme fixes and the direction each channel moves.

enum { STATE_DERIVATION_STEPS = 5 };

typedef enum StateChannel {
    StateChannel_Hover,
    StateChannel_Press,
    StateChannel_Focus,
} StateChannel;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The background (hover, press) or border (focus) the variant resolves to with one animation factor set to t.
static FlColor state_derivation_resolve(FlWidgetVariant variant, StateChannel channel, float t,
                                        const FlThemeColors* theme) {
    ItemState item = { 0 };
    if (channel == StateChannel_Hover) {
        item.state.hot_t = t;
    } else if (channel == StateChannel_Press) {
        item.state.active_t = t;
    } else {
        item.state.focused_t = t;
    }
    const FlWidgetColors colors = fl_resolve_widget_colors(variant, (FlColor) { 0 }, (FlColor) { 0 }, &item, theme,
                                                           /*animate_text=*/true);
    return channel == StateChannel_Focus ? colors.border : colors.bg;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int channel_sum(FlColor c) {
    return (int)c.r + (int)c.g + (int)c.b;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(render, state_derivation) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // A deterministic theme so every derivation input is fixed.
    static const FlThemeColors known = {
        .interactive.base = { 200, 30, 40, 255 },
        .interactive.hover = { 220, 60, 70, 255 },
        .interactive.press = { 170, 20, 30, 255 },
        .interactive.focus = { 255, 255, 255, 255 },
        .interactive.disabled = { 200, 30, 40, 128 },
        .surface.base = { 18, 20, 24, 255 },
        .surface.elevated = { 36, 38, 44, 255 },
        .surface.overlay = { 8, 9, 11, 255 },
        .surface.selected = { 200, 30, 40, 255 },
        .border.default_color = { 50, 52, 60, 255 },
        .border.focus = { 230, 80, 92, 255 },
        .border.separator = { 44, 46, 52, 255 },
        .text.primary = { 240, 240, 240, 255 },
        .text.secondary = { 150, 150, 150, 255 },
        .text.accent = { 200, 30, 40, 255 },
        .feedback.success = { 76, 175, 80, 255 },
        .feedback.warning = { 255, 152, 0, 255 },
        .feedback.error = { 244, 67, 54, 255 },
        .feedback.info = { 33, 150, 243, 255 },
    };
    ui_set_theme(ctx, &known);
    const FlThemeColors* theme = ui_get_theme_colors(ctx);
    ASSERT_TRUE_MSG(theme == &known, "seam must resolve to the injected theme");

    const float steps[STATE_DERIVATION_STEPS] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    const FlWidgetVariant variants[4]
        = { FlWidgetVariant_Solid, FlWidgetVariant_Outline, FlWidgetVariant_Ghost, FlWidgetVariant_Soft };
    const FlColor rest_bg[4] = {
        known.interactive.base,
        known.surface.base,
        known.surface.base,
        { known.interactive.base.r, known.interactive.base.g, known.interactive.base.b, 38 },
    };

    for (int v = 0; v < 4; v++) {
        FlColor hover[STATE_DERIVATION_STEPS];
        FlColor press[STATE_DERIVATION_STEPS];
        FlColor focus[STATE_DERIVATION_STEPS];
        for (int s = 0; s < STATE_DERIVATION_STEPS; s++) {
            hover[s] = state_derivation_resolve(variants[v], StateChannel_Hover, steps[s], theme);
            press[s] = state_derivation_resolve(variants[v], StateChannel_Press, steps[s], theme);
            focus[s] = state_derivation_resolve(variants[v], StateChannel_Focus, steps[s], theme);
        }

        ASSERT_TRUE_MSG(color_eq(hover[0], rest_bg[v]), "no hover resolves to the resting background");
        ASSERT_TRUE_MSG(color_eq(press[0], rest_bg[v]), "no press resolves to the resting background");
        ASSERT_EQ_MSG(focus[STATE_DERIVATION_STEPS - 1].r, known.interactive.focus.r, "full focus border red");
        ASSERT_EQ_MSG(focus[STATE_DERIVATION_STEPS - 1].g, known.interactive.focus.g, "full focus border green");
        ASSERT_EQ_MSG(focus[STATE_DERIVATION_STEPS - 1].b, known.interactive.focus.b, "full focus border blue");
        ASSERT_EQ_MSG(focus[STATE_DERIVATION_STEPS - 1].a, 255, "full focus border is opaque");

        for (int s = 1; s < STATE_DERIVATION_STEPS; s++) {
            ASSERT_GT_MSG(channel_sum(hover[s]), channel_sum(hover[s - 1]), "hover lightens at every step");
            ASSERT_LE_MSG(channel_sum(press[s]), channel_sum(press[s - 1]), "press never lightens");
            ASSERT_GT_MSG(channel_sum(focus[s]) + focus[s].a, channel_sum(focus[s - 1]) + focus[s - 1].a,
                          "the focus border moves toward the focus color at every step");
        }
        ASSERT_LT_MSG(channel_sum(press[STATE_DERIVATION_STEPS - 1]), channel_sum(press[0]),
                      "full press is darker than rest");
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.variant_derivation - perceptual variant derivation from an arbitrary base ("Object Color") plus a
// luminance-chosen label color. Lighten raises and darken lowers the brightness, desaturate narrows the channel
// spread, and the readable foreground flips with the background.

static int variant_derivation_spread(FlColor c) {
    return (int)max(max(c.r, c.g), c.b) - (int)min(min(c.r, c.g), c.b);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(render, variant_derivation) {
    const FlColor label_light = FL_THEME_DEFAULT_TEXT_PRIMARY;
    const FlColor label_dark = { 16, 16, 18, 255 };

    // App-chosen derivation amounts (illustrative - the framework does not mandate a set).
    const float lighten_amt = 0.12f, darken_amt = 0.12f, desat_amt = 0.5f;

    const struct {
        FlColor base;
        bool saturated;
        bool dark_label; // the label the base and its lighter and darker siblings take
    } rows[] = {
        { { 230, 230, 236, 255 }, false, true }, { { 20, 20, 24, 255 }, false, false },
        { { 236, 202, 42, 255 }, true, true },   { { 40, 84, 224, 255 }, true, false },
        { { 214, 40, 48, 255 }, true, false },
    };

    for (size_t r = 0; r < sizeof(rows) / sizeof(rows[0]); r++) {
        const FlColor base = rows[r].base;
        const FlColor lighter = color_lighten_perceptual(base, lighten_amt);
        const FlColor darker = color_darken_perceptual(base, darken_amt);
        const FlColor muted = color_desaturate_perceptual(base, desat_amt);

        ASSERT_GE_MSG(channel_sum(lighter), channel_sum(base), "lighten does not darken");
        ASSERT_LE_MSG(channel_sum(darker), channel_sum(base), "darken does not lighten");
        ASSERT_GT_MSG(channel_sum(lighter), channel_sum(darker), "the lighter sibling is brighter than the darker one");
        if (rows[r].saturated) {
            ASSERT_LT_MSG(variant_derivation_spread(muted), variant_derivation_spread(base),
                          "desaturate narrows the channel spread");
        }
    }

    const FlColor pale = rows[0].base;
    const FlColor near_black = rows[1].base;
    const FlColor yellow = rows[2].base;
    ASSERT_TRUE_MSG(color_eq(color_readable_foreground(pale, label_light, label_dark), label_dark),
                    "a pale background takes the dark label");
    ASSERT_TRUE_MSG(color_eq(color_readable_foreground(yellow, label_light, label_dark), label_dark),
                    "a saturated yellow background takes the dark label");
    ASSERT_TRUE_MSG(color_eq(color_readable_foreground(near_black, label_light, label_dark), label_light),
                    "a near-black background takes the light label");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.widget_slot_override - per-widget state-slot overrides (FlWidgetConfig::theme_override). A widget
// config carries a sparse FlThemeOverride layered over the resolved theme for that one widget, composing with any
// active scoped subtree override (widget-level wins for the slots it flags, inherits the rest). Driven through
// real Outline buttons - resting background surface.base, border interactive.base - read back from the debug
// render-command snapshot.

// Collect the opaque background (rectangle) or border colors painted in the last frame, in command order. Each
// Outline button emits exactly one fully opaque rectangle (its bg) and one opaque border, in declaration order;
// the container draws neither, and the link drawn at the end paints only near-transparent helper rects (excluded by
// the alpha==255 filter). So the k-th opaque rectangle and k-th opaque border belong to the k-th button.
static int slot_override_collect(UiRenderCommandType type, UiColor4* out, int max) {
    int count = 0;
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n && count < max; i++) {
        UiRenderCommand cmd;
        if (!ui_get_render_command(i, &cmd) || cmd.type != type) {
            continue;
        }
        const UiColor4 c = (type == UiRenderCmd_Border) ? cmd.border_color : cmd.bg_color;
        if (c.a < 255.0f) {
            continue; // skip the link's near-transparent container / underline helpers
        }
        out[count++] = c;
    }
    return count;
}

// Find a text command whose color matches want (the link's text color carries its per-widget interactive.base
// override).
static bool slot_override_has_text_color(FlColor want) {
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (!ui_get_render_command(i, &cmd) || cmd.type != UiRenderCmd_Text) {
            continue;
        }
        if (fabsf(cmd.text_color.r - (float)want.r) < 0.5f && fabsf(cmd.text_color.g - (float)want.g) < 0.5f
            && fabsf(cmd.text_color.b - (float)want.b) < 0.5f && fabsf(cmd.text_color.a - (float)want.a) < 0.5f) {
            return true;
        }
    }
    return false;
}

UTEST(render, widget_slot_override) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(12, 12, 14);

    // Built-in 8x8 font (dependency-free); buttons paint text.
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 3));

    static const FlThemeColors base = {
        .interactive.base = { 200, 30, 40, 255 }, // I0 - Outline border at rest
        .interactive.focus = { 255, 255, 255, 255 },
        .surface.base = { 18, 20, 24, 255 }, // S0 - Outline background at rest
        .surface.elevated = { 36, 38, 44, 255 },
        .border.default_color = { 50, 52, 60, 255 },
        .text.primary = { 240, 240, 240, 255 },
        .text.secondary = { 150, 150, 150, 255 },
        .text.accent = { 200, 30, 40, 255 },
    };
    ui_set_theme(ctx, &base);

    const FlColor S1 = { 60, 20, 20, 255 };  // subtree surface.base
    const FlColor I1 = { 20, 60, 20, 255 };  // subtree interactive.base
    const FlColor I2 = { 40, 40, 200, 255 }; // widget override: interactive.base (border)
    const FlColor S2 = { 70, 50, 10, 255 };  // widget override: surface.base (background)

    // Per-widget overrides as named locals - a compound literal inline in the ui_button() macro would split on its
    // internal commas (the preprocessor only protects parentheses, not braces).
    const FlThemeOverride ov_interactive
        = { .slots = FlThemeSlot_InteractiveBase, .colors = { .interactive.base = I2 } };
    const FlThemeOverride ov_surface = { .slots = FlThemeSlot_SurfaceBase, .colors = { .surface.base = S2 } };

    // A link exercises the nested cfg.widget.theme_override path (vs. button's bare cfg.theme_override). At rest its
    // text renders in interactive.base, so a unique override color is findable among the text commands.
    const FlColor LINK_OV = { 123, 60, 200, 255 };
    const FlThemeOverride ov_link = { .slots = FlThemeSlot_InteractiveBase, .colors = { .interactive.base = LINK_OV } };

    ASSERT_EQ_MSG(ctx->theme_override_depth, 0, "override stack must start empty");

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom, .padding = fl_padding_md(), .child_gap = FlSpacing_MD) {
        ui_button(FL_S("plain"), .variant = FlWidgetVariant_Outline);

        ui_theme_push(ctx, (FlThemeOverride) {
                               .slots = FlThemeSlot_SurfaceBase | FlThemeSlot_InteractiveBase,
                               .colors = { .surface.base = S1, .interactive.base = I1 },
                           });
        ui_button(FL_S("sib"), .variant = FlWidgetVariant_Outline);
        ui_button(FL_S("wbase"), .variant = FlWidgetVariant_Outline, .theme_override = &ov_interactive);
        ui_button(FL_S("wsurf"), .variant = FlWidgetVariant_Outline, .theme_override = &ov_surface);
        ui_theme_pop(ctx);

        ui_button(FL_S("plain2"), .variant = FlWidgetVariant_Outline);

        ui_link(FL_S("lnk"), .widget.theme_override = &ov_link);
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_EQ_MSG(ctx->theme_override_depth, 0, "override stack must be balanced after equal push/pop");

    // Five buttons, declaration order: plain, sib, wbase, wsurf, plain2.
    UiColor4 bg[5], border[5];
    ASSERT_EQ_MSG(slot_override_collect(UiRenderCmd_Rectangle, bg, 5), 5, "expected one background per button");
    ASSERT_EQ_MSG(slot_override_collect(UiRenderCmd_Border, border, 5), 5, "expected one border per button");

    ASSERT_TRUE_MSG(slot_override_color_eq(bg[0], base.surface.base), "plain: bg resolves to base surface");
    ASSERT_TRUE_MSG(slot_override_color_eq(border[0], base.interactive.base),
                    "plain: border resolves to base interactive");
    ASSERT_TRUE_MSG(slot_override_color_eq(bg[1], S1), "sib: bg resolves to the subtree override");
    ASSERT_TRUE_MSG(slot_override_color_eq(border[1], I1), "sib: border resolves to the subtree override");
    ASSERT_TRUE_MSG(slot_override_color_eq(bg[2], S1), "wbase: bg inherits the subtree surface override");
    ASSERT_TRUE_MSG(slot_override_color_eq(border[2], I2), "wbase: border wins with the per-widget override");
    ASSERT_TRUE_MSG(slot_override_color_eq(bg[3], S2), "wsurf: bg wins with the per-widget override");
    ASSERT_TRUE_MSG(slot_override_color_eq(border[3], I1), "wsurf: border inherits the subtree interactive override");
    ASSERT_TRUE_MSG(slot_override_color_eq(bg[4], base.surface.base), "plain2: bg reverts to base after pop");
    ASSERT_TRUE_MSG(slot_override_color_eq(border[4], base.interactive.base),
                    "plain2: border reverts to base after pop");

    ASSERT_TRUE_MSG(slot_override_has_text_color(LINK_OV), "link: cfg.widget.theme_override retints the link text");

    // Focus ring (FlThemeSlot_InteractiveFocus): driven with a synthetic focused ItemState because the render
    // path's focus animation cannot reach focused_t=1 in a single frame. A Solid variant has a transparent base
    // border, so at focused_t=1 the focus border lands exactly on interactive.focus (alpha-ramp, no OKLab
    // rounding).
    const FlColor focus_ov_color = { 12, 240, 200, 255 };
    const FlThemeOverride ov_focus
        = { .slots = FlThemeSlot_InteractiveFocus, .colors = { .interactive.focus = focus_ov_color } };
    const FlThemeColors focus_merged = fl_theme_merge_override(&base, &ov_focus);
    ASSERT_TRUE_MSG(color_eq(focus_merged.interactive.focus, focus_ov_color), "focus slot merges per-widget");
    ItemState focused = { 0 };
    focused.state.focused_t = 1.0f;
    const FlWidgetColors focus_with_ov = fl_resolve_widget_colors(FlWidgetVariant_Solid, (FlColor) { 0 },
                                                                  (FlColor) { 0 }, &focused, &focus_merged, false);
    const FlWidgetColors focus_no_ov
        = fl_resolve_widget_colors(FlWidgetVariant_Solid, (FlColor) { 0 }, (FlColor) { 0 }, &focused, &base, false);
    ASSERT_TRUE_MSG(color_eq(focus_with_ov.border, focus_ov_color),
                    "focus-ring override drives the derived focus ring");
    ASSERT_TRUE_MSG(color_eq(focus_no_ov.border, base.interactive.focus),
                    "without override the focus ring is the base focus");
    ASSERT_TRUE_MSG(!color_eq(focus_with_ov.border, focus_no_ov.border), "focus-ring override changes the focus ring");

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(ctx->shared->renderer.read_pixels != nullptr, "backend has no pixel-readback capability");
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "render buffer data is null");
    ASSERT_TRUE_MSG(buf.width == 1920 && buf.height == 1080, "unexpected render buffer dimensions");
    dump_frame("widget_slot_override", &buf);
}

// render.switch_skin - a skin-overridden switch rendered alongside a default one.
//
// The asserts are computed from the geometry, not read off the render: at the skinned row's mid-height the frame
// must be back to the clear color past x=26 (the skinned track ends there) while the default row is still painted
// at the same x (its track runs to 48).
UTEST(render, switch_skin) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(12, 12, 14);

    // Pin the theme: the track/knob colors come straight out of these slots.
    static const FlThemeColors base = {
        .interactive.base = { 200, 30, 40, 255 }, // on-track fill
        .interactive.focus = { 255, 255, 255, 255 },
        .surface.base = { 18, 20, 24, 255 },
        .surface.elevated = { 36, 38, 44, 255 },     // off-track fill
        .border.default_color = { 50, 52, 60, 255 }, // track border at rest
        .text.primary = { 240, 240, 240, 255 },      // knob
        .text.secondary = { 150, 150, 150, 255 },
        .text.accent = { 200, 30, 40, 255 },
    };
    ui_set_theme(ctx, &base);

    // Built-in 8x8 font at scale 1 (dependency-free). Scale 1
    // keeps each label inside its own row - the skinned row is only 13px tall.
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 1));

    // Two rows, top-left, no gap: the default 48x24 pill then the compact 26x13 skin. Each switch's own container
    // is {grow, fixed(track_height)}, so row 0 spans y 0..24 and row 1 y 24..37, both tracks flush at x = 0.
    bool off_default = false;
    bool off_skin = false;
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .direction = LayoutDirection_TopToBottom) {
        ui_switch(FL_S("default"), &off_default, .track_width = 0.0f);
        ui_switch(FL_S("skin"), &off_skin, .track_width = 26.0f, .track_height = 13.0f);
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(ctx->shared->renderer.read_pixels != nullptr, "backend has no pixel-readback capability");
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "render buffer data is null");
    ASSERT_TRUE_MSG(buf.width == 1920 && buf.height == 1080, "unexpected render buffer dimensions");

    // x = 36 is inside the default track (0..48) and well past the skinned one's right edge (26); the two rows'
    // mid-heights are y = 12 and y = 30. Each row is compared against bare background WITHIN ITS OWN ROW
    // (x = 200, past both tracks) rather than against the clear color: every switch lays its hit-test container
    // across the full row width at alpha 2, so "unpainted" is the container tint, not the clear color.
#define SWITCH_PX_EQ(ax, ay, bx, by) \
    (memcmp(readback_rgb_at(&buf, (ax), (ay)), readback_rgb_at(&buf, (bx), (by)), 3) == 0)
    ASSERT_FALSE_MSG(SWITCH_PX_EQ(36, 12, 200, 12), "the default track still covers x = 36");
    ASSERT_TRUE_MSG(SWITCH_PX_EQ(36, 30, 200, 30), "the skinned track stops short of x = 36");
    // Both tracks do cover x = 10, so the row above is a width difference, not a missing skinned switch.
    ASSERT_FALSE_MSG(SWITCH_PX_EQ(10, 30, 200, 30), "the skinned track is painted at x = 10");
#undef SWITCH_PX_EQ

    dump_frame("switch_skin", &buf);
}

// render.slider_skin - a skin-overridden slider rendered above a default one.
//
// The assert is computed from the geometry, not read off the render: a column drawn through both tracks, clear of the
// thumbs and of either label, must cross exactly two runs of the fill color - 8px for the default bar, then 3px for the
// skinned one. Sliders are at rest here, so the fill is theme->text.primary unmodulated (ui_apply_state_color is
// identity at hot_t/active_t 0).
UTEST(render, slider_skin) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(12, 12, 14);

    // Pin the theme: the fill / thumb / unfilled colors come straight out of these slots.
    static const FlThemeColors base = {
        .interactive.base = { 200, 30, 40, 255 },
        .interactive.focus = { 255, 255, 255, 255 },
        .surface.base = { 18, 20, 24, 255 },
        .surface.elevated = { 36, 38, 44, 255 }, // unfilled track
        .border.default_color = { 50, 52, 60, 255 },
        .text.primary = { 240, 240, 240, 255 }, // filled track, thumb, and label/value text
        .text.secondary = { 150, 150, 150, 255 },
        .text.accent = { 200, 30, 40, 255 },
    };
    ui_set_theme(ctx, &base);

    // Built-in 8x8 font at scale 1 (dependency-free).
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 1));

    // Two sliders, top-left: the default 24px thumb over an 8px bar, then the compact 13px thumb over a 3px bar,
    // both at half value. The labels are also the widget ids. Two frames: the segment widths derive from the track
    // row's width as carried over from the previous frame, so only the second is worth asserting.
    float value_default = 0.5f;
    float value_skin = 0.5f;
    for (int pass = 0; pass < 2; pass++) {
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .direction = LayoutDirection_TopToBottom) {
            ui_slider_float(FL_S("slider_skin_default"), &value_default, 0.0f, 1.0f, .thumb_size = 0.0f);
            ui_slider_float(FL_S("slider_skin_thin"), &value_skin, 0.0f, 1.0f, .track_height = 3.0f,
                            .thumb_size = 13.0f);
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(ctx->shared->renderer.read_pixels != nullptr, "backend has no pixel-readback capability");
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "render buffer data is null");
    ASSERT_TRUE_MSG(buf.width == 1920 && buf.height == 1080, "unexpected render buffer dimensions");

    // Walk x = 200 top to bottom and measure each run of fill-colored pixels. The tracks grow to 1000px wide, so
    // 200 is inside both filled segments; it clears the thumbs (mid-travel, around x = 490) and both labels (a
    // few 8px glyphs at the left edge), which are drawn in this same color. The two run lengths are therefore
    // the two bar thicknesses, in layout order.
    const u8 fill_rgb[3] = { 240, 240, 240 };
    int runs[8];
    int run_count = 0;
    int current_run = 0;
    for (int y = 0; y < 1080; y++) {
        if (memcmp(readback_rgb_at(&buf, 200, y), fill_rgb, 3) == 0) {
            current_run++;
        } else if (current_run > 0) {
            if (run_count < 8) {
                runs[run_count] = current_run;
            }
            run_count++;
            current_run = 0;
        }
    }
    ASSERT_EQ_MSG(run_count, 2, "the column crosses exactly the two sliders' filled bars");
    ASSERT_EQ_MSG(runs[0], 8, "the default bar is 8px thick");
    ASSERT_EQ_MSG(runs[1], 3, "the skinned bar is 3px thick");

    dump_frame("slider_skin", &buf);
}

// render.tab_bar_skin - a segmented-control-skinned tab bar rendered under a default one. The default bar is the
// zeroed config the dock leaf header also passes.
//
// The gate is a scanline through the middle of the pill, positioned from the snapshot rather than
// hand-computed: it must cross exactly one run of the accent fill (the selected tab), and the strip must show
// the pill's own surface color between its edge and that tab.
UTEST(render, tab_bar_skin) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(12, 12, 14);

    // Pin the theme: every color below comes straight out of these slots.
    static const FlThemeColors base = {
        .interactive.base = { 200, 30, 40, 255 }, // selected tab fill
        .interactive.focus = { 255, 255, 255, 255 },
        .surface.base = { 18, 20, 24, 255 },     // unselected tab fill
        .surface.elevated = { 36, 38, 44, 255 }, // the pill behind the tabs
        .border.default_color = { 50, 52, 60, 255 },
        .text.primary = { 240, 240, 240, 255 },
        .text.secondary = { 150, 150, 150, 255 },
        .text.accent = { 200, 30, 40, 255 },
    };
    ui_set_theme(ctx, &base);

    // Built-in 8x8 font at scale 1 (dependency-free).
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 1));

    // Hover lightens the fills perceptually, which would pull the scanline's colors off their theme values.
    ctx->input_state.mouse.x = -1000.0f;
    ctx->input_state.mouse.y = -1000.0f;

    // Distinct label sets because a label is also its tab's id and the fixture's ItemState map outlives a case.
    FlString tabs_default[] = { FL_S("Mix"), FL_S("Fx"), FL_S("Out") };
    FlString tabs_skin[] = { FL_S("Osc"), FL_S("Env"), FL_S("Mod") };
    int sel_default = 0;
    int sel_skin = 0;
    for (int pass = 0; pass < 2; pass++) {
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .direction = LayoutDirection_TopToBottom) {
            ui_tab_bar(tabs_default, 3, &sel_default);
            ui_tab_bar(tabs_skin, 3, &sel_skin, .item_padding_h = 18.0f, .item_padding_v = 6.0f, .item_radius = 6.0f,
                       .container_padding = 3.0f, .container_radius = 8.0f, .font_size = 10);
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }

    // Rectangles in render order: the default bar's three tabs, then the skinned strip's pill and its tabs.
    UiRenderCommand rects[8];
    ASSERT_EQ_MSG(render_cmd_collect(UiRenderCmd_Rectangle, rects, 8), 7,
                  "three default tabs, then the pill and three more");
    const UiRenderCommand pill = rects[3];
    const UiRenderCommand first_tab = rects[4];

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(ctx->shared->renderer.read_pixels != nullptr, "backend has no pixel-readback capability");
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "render buffer data is null");
    ASSERT_TRUE_MSG(buf.width == 1920 && buf.height == 1080, "unexpected render buffer dimensions");

    // Scan the pill's middle row across its full width: the selected tab is the only accent-filled thing on it.
    const u8 accent_rgb[3] = { 200, 30, 40 };
    const u8 pill_rgb[3] = { 36, 38, 44 };
    const int scan_y = (int)(pill.y + pill.height / 2.0f);
    int accent_runs = 0;
    bool in_run = false;
    for (int x = (int)pill.x; x < (int)(pill.x + pill.width); x++) {
        const bool accent = memcmp(readback_rgb_at(&buf, x, scan_y), accent_rgb, 3) == 0;
        if (accent && !in_run) {
            accent_runs++;
        }
        in_run = accent;
    }
    ASSERT_EQ_MSG(accent_runs, 1, "one accent run across the strip: the selected tab");
    ASSERT_EQ_MSG(memcmp(readback_rgb_at(&buf, (int)first_tab.x - 2, scan_y), pill_rgb, 3), 0,
                  "the pill paints behind the tabs, showing through its own padding");

    dump_frame("tab_bar_skin", &buf);
}

// render.dropdown_skin - a pill-skinned dropdown trigger rendered under a default one.
//
// The gate below is computed from the snapshot's own rects rather than
// hand-placed: a column through both triggers, clear of their labels and chevrons, crosses exactly two runs of
// the surface fill, and the skinned run falls short of the default one by exactly the height the config took
// off. Stating it as a difference cancels the border out of both ends.
UTEST(render, dropdown_skin) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(12, 12, 14);

    // Pin the theme: the fill, border and label colors come straight out of these slots.
    static const FlThemeColors base = {
        .interactive.base = { 200, 30, 40, 255 }, // the Outline trigger's border
        .interactive.focus = { 255, 255, 255, 255 },
        .surface.base = { 30, 34, 42, 255 }, // the trigger's fill, and the run the scan below counts
        .surface.elevated = { 36, 38, 44, 255 },
        .border.default_color = { 50, 52, 60, 255 },
        .text.primary = { 240, 240, 240, 255 },
        .text.secondary = { 150, 150, 150, 255 },
        .text.accent = { 200, 30, 40, 255 },
    };
    ui_set_theme(ctx, &base);

    // Built-in 8x8 font at scale 1 (dependency-free).
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 1));

    // Hover lightens the fill perceptually, which would pull the scanned run off its theme color.
    ctx->input_state.mouse.x = -1000.0f;
    ctx->input_state.mouse.y = -1000.0f;

    // Distinct labels because a label is also its trigger's id and the fixture's ItemState map outlives a case.
    FlString opts[2] = { FL_S("Sine"), FL_S("Saw") };
    bool open_default = false;
    bool open_skin = false;
    int sel_default = 0;
    int sel_skin = 0;
    for (int pass = 0; pass < 2; pass++) {
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .direction = LayoutDirection_TopToBottom) {
            ui_dropdown(FL_S("Wave"), &open_default, opts, 2, &sel_default);
            ui_dropdown(FL_S("Shape"), &open_skin, opts, 2, &sel_skin, .padding_h = 12.0f, .padding_v = 7.0f,
                        .corner_radius = 8.0f, .min_width = 180.0f);
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }

    // One rectangle per closed trigger, in layout order.
    UiRenderCommand rects[4];
    ASSERT_EQ_MSG(render_cmd_collect(UiRenderCmd_Rectangle, rects, 4), 2, "one rectangle per closed trigger");
    const float height_taken_off = rects[0].height - rects[1].height;
    ASSERT_GT_MSG((double)height_taken_off, 0.0, "the pill is the shorter of the two");

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(ctx->shared->renderer.read_pixels != nullptr, "backend has no pixel-readback capability");
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "render buffer data is null");
    ASSERT_TRUE_MSG(buf.width == 1920 && buf.height == 1080, "unexpected render buffer dimensions");

    // Walk a column down through both triggers and measure each run of the fill color. The triggers grow to the
    // full window width, so a column at three quarters across sits well past the labels at their left edge and
    // well before the chevrons at their right, leaving nothing but fill between the top and bottom borders.
    const u8 fill_rgb[3] = { 30, 34, 42 };
    const int scan_x = (int)(rects[0].x + rects[0].width * 0.75f);
    int runs[4];
    int run_count = 0;
    int current_run = 0;
    for (int y = 0; y < 1080; y++) {
        if (memcmp(readback_rgb_at(&buf, scan_x, y), fill_rgb, 3) == 0) {
            current_run++;
        } else if (current_run > 0) {
            if (run_count < 4) {
                runs[run_count] = current_run;
            }
            run_count++;
            current_run = 0;
        }
    }
    ASSERT_EQ_MSG(run_count, 2, "the column crosses exactly the two triggers' fills");
    ASSERT_EQ_MSG(runs[0] - runs[1], (int)height_taken_off,
                  "the painted pill falls short of the default trigger by exactly the height padding_v took off");

    dump_frame("dropdown_skin", &buf);
}

// render.dock_tabs - the dock leaf header's tab strip, rendered through the real dock_layout.c call path
// ui_tab_strip is shared between the public tab bar and docking, and docking passes a
// zeroed FlTabBarConfig, so every geometry knob must be inert there.
UTEST(render, dock_tabs) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(12, 12, 14);

    static const FlThemeColors base = {
        .interactive.base = { 200, 30, 40, 255 },
        .interactive.focus = { 255, 255, 255, 255 },
        .surface.base = { 18, 20, 24, 255 },
        .surface.elevated = { 36, 38, 44, 255 },
        .border.default_color = { 50, 52, 60, 255 },
        .text.primary = { 240, 240, 240, 255 },
        .text.secondary = { 150, 150, 150, 255 },
        .text.accent = { 200, 30, 40, 255 },
    };
    ui_set_theme(ctx, &base);
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 1));
    ctx->input_state.mouse.x = -1000.0f; // hover would pull the fills off their exact theme colors
    ctx->input_state.mouse.y = -1000.0f;

    // One root leaf holding two panels, so the leaf renders a tab header.
    FlDockWorkspace* ws = dock_workspace_create();
    ASSERT_TRUE_MSG(ws != nullptr, "dock_workspace_create failed");
    dock_workspace_set_tree(ws, dock_tree_build(dock_leaf(100, 101)));

    for (int pass = 0; pass < 2; pass++) {
        ui_update_begin(ctx);
        dock_workspace_begin(ws);
        // Titles first so both passes render the same labelled strip.
        dock_workspace_declare(ws, 100, FL_S("Console"));
        dock_workspace_declare(ws, 101, FL_S("Log"));
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
            dock_workspace_emit(ws);
        }
        dock_workspace_end(ws);
        ui_update_end(ctx, 1.0f / 60.0f);
    }

    // The leaf paints no background of its own, so the frame holds the header bar and the two tabs on it - and
    // no pill, which is the zeroed container_padding/container_radius showing. Declaration order puts the bar
    // first, then the selected tab, then the unselected one.
    UiRenderCommand rects[8];
    ASSERT_EQ_MSG(render_cmd_collect(UiRenderCmd_Rectangle, rects, 8), 3,
                  "the header bar and its two tabs, with no pill among them");
    ASSERT_NEAR_MSG((double)rects[0].width, (double)ctx->layout_width, 1e-4, "the header bar spans the tile");
    ASSERT_TRUE_MSG(tab_color_eq(rects[0].bg_color, base.surface.elevated), "and reads as a raised surface");
    ASSERT_NEAR_MSG((double)rects[1].height, (double)DOCK_TAB_HEIGHT, 1e-4,
                    "a dock tab is a title bar's height, not a button's");
    ASSERT_NEAR_MSG((double)rects[1].corner_radius.tl, (double)FlRadius_SM, 1e-4, "at the dock's tab radius");
    // Two panels share this bar, so neither tab stretches: the selected one is Soft - an accent tint the full
    // width of a one-panel header could carry, where the accent FILL of a plain tab bar could not.
    const FlColor soft = { base.interactive.base.r, base.interactive.base.g, base.interactive.base.b, 38 };
    ASSERT_TRUE_MSG(tab_color_eq(rects[1].bg_color, soft), "the selected dock tab tints with the accent");
    ASSERT_TRUE_MSG(tab_color_eq(rects[2].bg_color, base.surface.base), "an unselected dock tab fills from the theme");

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(ctx->shared->renderer.read_pixels != nullptr, "backend has no pixel-readback capability");
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "render buffer data is null");

    dump_frame("dock_tabs", &buf);

    dock_workspace_destroy(ws);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.public_button_variant_config - the public fl_button takes a trailing FlWidgetConfig, so the
// designated-initializer macro can pass .variant (and id/color/focus). Read off the emitted background fills:
// at rest (no hover/press) the variant base color is the fill, so Solid (interactive.base) and Outline
// (surface.base) differ exactly.

UTEST(render, public_button_variant_config) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 3));

    static const FlThemeColors base = {
        .interactive.base = { 200, 30, 40, 255 }, // Solid bg
        .interactive.focus = { 255, 255, 255, 255 },
        .surface.base = { 18, 20, 24, 255 }, // Outline bg
        .border.default_color = { 50, 52, 60, 255 },
        .text.primary = { 240, 240, 240, 255 },
        .text.secondary = { 150, 150, 150, 255 },
    };
    ui_set_theme(ctx, &base);

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom, .padding = fl_padding_md(), .child_gap = FlSpacing_MD) {
        fl_button(FL_S("solid"), .variant = FlWidgetVariant_Solid);
        fl_button(FL_S("outline"), .variant = FlWidgetVariant_Outline);
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    // Two buttons, declaration order: solid, outline. One opaque background fill each.
    UiColor4 bg[2];
    ASSERT_EQ_MSG(slot_override_collect(UiRenderCmd_Rectangle, bg, 2), 2, "expected one background per button");
    ASSERT_TRUE_MSG(slot_override_color_eq(bg[0], base.interactive.base),
                    "Solid variant via fl_button config resolves bg to interactive.base");
    ASSERT_TRUE_MSG(slot_override_color_eq(bg[1], base.surface.base),
                    "Outline variant via fl_button config resolves bg to surface.base");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.item_behavior_auto_focus - FlItemFlags_auto_focus is the flag-only counterpart of
// FlWidgetConfig.auto_focus. Two focusable elements in order: A (plain focusable) then B (focusable +
// auto_focus). Auto-restore picks the first focusable when nothing is focused, so B ending up focused - not
// A - is only explicable by auto_focus.

UTEST(render, item_behavior_auto_focus) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // Clear any focus carried from a prior fixture user so auto-restore starts from a clean slate.
    ctx->focused_id = 0;
    ctx->last_focused_id = 0;

    const FlLayoutId id_a = ID("af_plain");
    const FlLayoutId id_b = ID("af_auto");

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        // A fill makes each area emit a render command carrying its id, so per-element interaction state
        // (found / focused) is updated in ui_update_end.
        ui_layout_area_id(id_a, .sizing = { sizing_fixed(40), sizing_fixed(20) }, .color = ui_color(40, 40, 60, 255)) {
            fl_item_behavior(id_a, FlItemFlags_focusable);
        }
        ui_layout_area_id(id_b, .sizing = { sizing_fixed(40), sizing_fixed(20) }, .color = ui_color(60, 40, 40, 255)) {
            fl_item_behavior(id_b, FlItemFlags_focusable | FlItemFlags_auto_focus);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlElementState sa = ui_get_element_state(id_a);
    FlElementState sb = ui_get_element_state(id_b);
    ASSERT_TRUE_MSG(sa.found && sb.found, "both focusable elements are laid out");
    ASSERT_TRUE_MSG(sb.focused, "auto_focus moves focus to the requesting element");
    ASSERT_FALSE_MSG(sa.focused, "the first focusable element is not focused — auto_focus, not auto-restore, won");
}

// A spacer's extent is authored logical px like every other length, so it must follow ui_scale. The check is a
// fit-width row holding one horizontal spacer: fit takes the spacer's own width, so the row's laid-out rect IS
// the spacer's resolved extent. Driven at scale 2 because an unscaled spacer is indistinguishable from a scaled
// one at scale 1.
UTEST(render, a_spacer_follows_the_ui_scale) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(2.0f, 2.0f);

    const FlLayoutId row = ID("spacer_scale_row");
    const FlLayoutId col = ID("spacer_scale_col");

    // Two frames: element state is published by ui_update_end, so the second frame is the first one that can
    // read the first's rects back.
    for (int i = 0; i < 2; i++) {
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
            // A fill makes each box emit a render command carrying its id, which is what gives
            // ui_get_element_state a rect to report.
            ui_layout_area_id(row, .sizing = { sizing_fit(), sizing_fixed(10) }, .color = ui_color(40, 40, 60, 255)) {
                fl_item_behavior(row, FlItemFlags_clickable);
                fl_spacer_h(10.0f);
            }
            ui_layout_area_id(col, .sizing = { sizing_fixed(10), sizing_fit() },
                              .direction = FlLayoutDirection_TopToBottom, .color = ui_color(40, 40, 60, 255)) {
                fl_item_behavior(col, FlItemFlags_clickable);
                fl_spacer(10.0f);
            }
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }

    FlElementState h = ui_get_element_state(row);
    FlElementState v = ui_get_element_state(col);

    ASSERT_TRUE_MSG(h.found && v.found, "both spacer holders are laid out");
    ASSERT_EQ_MSG((int)h.size.x, 20, "a 10px horizontal spacer is 20 physical px at scale 2");
    ASSERT_EQ_MSG((int)v.size.y, 20, "a 10px vertical spacer is 20 physical px at scale 2");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.text_overflow_clip_vs_elide - the single-line text-overflow mode (FlTextConfig.overflow). A ten-glyph
// run is placed twice inside the same narrow fixed-width box, once per mode, and read off the debug
// render-command snapshot. The built-in 8x8 font is used so every ASCII glyph advances exactly 16px (monospace,
// FreeType-free → exact widths). That font has no glyph for U+2026, so the ellipsis measures 0px here; the test
// checks the ellipsis BYTES are present, not that it reserves width.

UTEST(render, text_overflow_clip_vs_elide) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(20, 20, 24);

    const u16 glyph_px = 16; // built-in 8x8 font at scale 2
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 2));

    // A ten-glyph single word (no spaces → a genuine single-line run): 10 * 16 = 160px unwrapped.
    const FlString name = { .data = "AAAAAAAAAA", .length = 10, .is_static = 1 };
    const float box_w = 64.0f; // 4 glyphs (64px) fit, the 5th (80px) does not.
    const FlColor white = { 240, 240, 240, 255 };

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        ui_layout_area(.sizing = { sizing_fixed(box_w), sizing_fixed(40) }) {
            ui_text_styled(name, .color = white, .no_wrap = true, .overflow = FlOverflow_Clip);
        }
        // The caller adds NO sizing declaration on the text itself - elide implies the box-width constraint.
        ui_layout_area(.sizing = { sizing_fixed(box_w), sizing_fixed(40) }) {
            ui_text_styled(name, .color = white, .no_wrap = true, .overflow = FlOverflow_Elide);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    static const char ellipsis[3] = { (char)0xE2, (char)0x80, (char)0xA6 }; // U+2026
    bool found_clip = false, found_elide = false;
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (!ui_get_render_command(i, &cmd) || cmd.type != UiRenderCmd_Text || cmd.text.data == nullptr) {
            continue;
        }
        const char* s = (const char*)cmd.text.data;
        const u64 len = cmd.text.length;
        const bool has_ellipsis = len >= 3 && memcmp(s + (len - 3), ellipsis, 3) == 0;
        if (has_ellipsis) {
            found_elide = true;
            const u64 prefix_len = len - 3;
            ASSERT_EQ_MSG((int)prefix_len, 4, "elide keeps the 4 glyphs (64px) that fit the 64px box");
            ASSERT_TRUE_MSG(cmd.width <= box_w + 0.5f, "elided run fits within the box width");
        } else {
            found_clip = true;
            ASSERT_EQ_MSG((int)len, (int)name.length, "clip keeps the full run (no truncation)");
            ASSERT_TRUE_MSG(cmd.width > box_w + 0.5f, "clip run keeps its full width and overflows the box");
            ASSERT_TRUE_MSG(fabsf(cmd.width - (float)(name.length * glyph_px)) < 0.5f,
                            "clip run width is the full unwrapped width");
        }
    }
    ASSERT_TRUE_MSG(found_clip, "expected a clip text command");
    ASSERT_TRUE_MSG(found_elide, "expected an elide text command");
}

static int canvas_custom_command_count(void) {
    int count = 0;
    int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (ui_get_render_command(i, &cmd) && cmd.type == UiRenderCmd_Custom) {
            count++;
        }
    }
    return count;
}

// Paint participates in z-order (under children) and is bracketed by the live scissor stack, and many primitives
// in one scope produce exactly one CUSTOM span (one marker per scope, never per primitive).
UTEST(render, canvas_paint_z_clip_and_one_span) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    const FlColor canvas_col = ui_color(40, 200, 120, 255); // opaque paint fill
    const FlColor child_col = ui_color(230, 90, 60, 255);   // opaque child box over the paint

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        // Clip container (120x120 at origin) clips its oversized canvas child - an ancestor scissor that
        // brackets the paint. The canvas also self-clips to its own 200x200 rect, but the ancestor clip is
        // tighter, so it is the dominant one here.
        ui_layout_area_id(ID("clipbox"), .sizing = { sizing_fixed(120), sizing_fixed(120) },
                          .clip = { .horizontal = true, .vertical = true }) {
            ui_layout_area_id(ID("canvas"), .sizing = { sizing_fixed(200), sizing_fixed(200) }) {
                Painter* p = ui_get_painter();
                ASSERT_TRUE_MSG(p != nullptr, "ui_get_painter is valid inside an open painting scope");
                // Many primitives in one scope - these must collapse to a single CUSTOM span.
                painter_rect(p, 0.0f, 0.0f, 200.0f, 200.0f, canvas_col);
                painter_rect(p, 4.0f, 4.0f, 8.0f, 8.0f, canvas_col);
                painter_circle(p, 100.0f, 100.0f, 6.0f, canvas_col, ui_color(0, 0, 0, 0), 0.0f);
                painter_rect(p, 20.0f, 20.0f, 6.0f, 6.0f, canvas_col);

                // Child element, laid out at the canvas origin - must draw OVER the paint.
                ui_layout_area(.sizing = { sizing_fixed(60), sizing_fixed(60) }, .color = child_col) {}
            }
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");

    // (30,30) sits under the child box (0..60) → the child's color, proving paint is UNDER its child element.
    ASSERT_TRUE_MSG(px_near(&buf, 30, 30, child_col, 2), "child element draws over the paint (paint at element z)");
    // (90,90) is inside the clip (0..120) and the canvas paint but not under the child → the painted fill.
    ASSERT_TRUE_MSG(px_near(&buf, 90, 90, canvas_col, 2), "paint is visible at the element's z, inside the scissor");
    // (150,150) is within the canvas paint region (0..200) but OUTSIDE the clip (>120) → scissor cropped it.
    ASSERT_FALSE_MSG(px_near(&buf, 150, 150, canvas_col, 2), "paint is bracketed by the live layout scissor stack");

    ASSERT_EQ_MSG(canvas_custom_command_count(), 1, "one painting scope emits exactly one CUSTOM span");
}

// render.placed_child_scroll_clip - the placed-child primitive: a layout element positioned at an
// explicit (x, y) in its parent's content coordinate space, scrolling and clipping with the container but NOT
// advancing the flex cursor or growing the content extent. The scene is a 400x300 manual_scroll clip box at the
// origin holding one flowed sibling D plus three placed children A/B/C (A and B overlap; C straddles the right
// clip edge). Frame 1 (scroll 0) checks placement, overlap, flow-independence, and clipping; frame 2 re-emits
// with a non-zero scroll and checks the placed children move by exactly the scroll delta.
UTEST(render, placed_child_scroll_clip) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    const FlColor clear_col = ui_color(12, 12, 14, 255);   // canvas_fixture_reset clear color (outside the clip box)
    const FlColor box_col = ui_color(50, 50, 60, 255);     // clip-box fill (inside the box, no child over it)
    const FlColor flow_col = ui_color(240, 200, 40, 255);  // D: a flowed sibling, emitted BEFORE the placed children
    const FlColor flow2_col = ui_color(200, 80, 220, 255); // E: a flowed sibling, emitted AFTER the placed children
    const FlColor a_col = ui_color(40, 200, 120, 255);     // A: placed at (100, 100)
    const FlColor b_col = ui_color(220, 60, 60, 255);      // B: placed at (120, 110), overlapping A
    const FlColor c_col = ui_color(80, 140, 240, 255);     // C: placed at (380, 100), straddling the right clip edge

    // --- Frame 1: scroll_pos = (0, 0) ----------------------------------------------------------------------------
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        // 400x300 clip box at the origin, owning its scroll (manual_scroll) - the content viewport. Default
        // LeftToRight: the two flowed children D and E lay out as a row, the three placed children do not.
        ui_layout_area_id(
            ID("placed_clip"), .sizing = { sizing_fixed(400), sizing_fixed(300) }, .color = box_col,
            .clip = { .horizontal = true, .vertical = true, .manual_scroll = true, .scroll_pos = { 0.0f, 0.0f } }) {
            // Flowed sibling D: first non-placed child, at the flex-cursor origin (0,0).
            ui_layout_area(.sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = flow_col) {}
            ui_layout_area(.sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = a_col,
                           .placed = { .enabled = true, .offset = { 100.0f, 100.0f } }) {}
            ui_layout_area(.sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = b_col,
                           .placed = { .enabled = true, .offset = { 120.0f, 110.0f } }) {}
            ui_layout_area(.sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = c_col,
                           .placed = { .enabled = true, .offset = { 380.0f, 100.0f } }) {}
            // Flowed sibling E: emitted AFTER the placed children. With the cursor-advance skip, E lands right
            // after D at x=40..80; a bug that let placed children advance the cursor would push E far to the right.
            ui_layout_area(.sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = flow2_col) {}
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");

    // Placement: A renders at its explicit offset. (105,105) is inside A (100..140) but left of B (x<120) - A only.
    ASSERT_TRUE_MSG(px_near(&buf, 105, 105, a_col, 2), "placed child A renders at its content-space offset");
    // The clip box fills where no child covers it, proving the placed children are its descendants.
    ASSERT_TRUE_MSG(px_near(&buf, 300, 250, box_col, 2), "clip box renders its own area under the placed children");
    // Flow-independence: D sits at the cursor origin (x=0..40), and E - emitted AFTER the three placed children -
    // sits immediately after D at x=40..80, NOT pushed right (a cursor-advancing bug would put E near x=160).
    ASSERT_TRUE_MSG(px_near(&buf, 20, 20, flow_col, 2), "first flowed sibling D is at the cursor origin");
    ASSERT_TRUE_MSG(px_near(&buf, 60, 20, flow2_col, 2), "flowed sibling after placed children is not displaced");
    ASSERT_FALSE_MSG(px_near(&buf, 160, 20, flow2_col, 2), "placed children did not advance the flex cursor");
    // Overlap is expressible: A-only pixel is A, B-only pixel is B, and the overlap shows B (the later sibling).
    ASSERT_TRUE_MSG(px_near(&buf, 150, 145, b_col, 2), "placed child B renders where it does not overlap A");
    ASSERT_TRUE_MSG(px_near(&buf, 130, 130, b_col, 2), "overlapping placed children both render (later draws on top)");
    // Clipping: C straddles the right edge - its inside half draws, its outside half is cropped to the clip area.
    ASSERT_TRUE_MSG(px_near(&buf, 390, 120, c_col, 2), "placed child C renders inside the clip area");
    ASSERT_FALSE_MSG(px_near(&buf, 410, 120, c_col, 2), "placed child is clipped to the container's active area");
    ASSERT_TRUE_MSG(px_near(&buf, 410, 120, clear_col, 2),
                    "outside the clip box is the clear color, not leaked content");

    // --- Frame 2: scroll_pos = (-40, -30) - content scrolls up/left ----------------------------------------------
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        ui_layout_area_id(
            ID("placed_clip"), .sizing = { sizing_fixed(400), sizing_fixed(300) }, .color = box_col,
            .clip = { .horizontal = true, .vertical = true, .manual_scroll = true, .scroll_pos = { -40.0f, -30.0f } }) {
            ui_layout_area(.sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = flow_col) {}
            ui_layout_area(.sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = a_col,
                           .placed = { .enabled = true, .offset = { 100.0f, 100.0f } }) {}
            ui_layout_area(.sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = b_col,
                           .placed = { .enabled = true, .offset = { 120.0f, 110.0f } }) {}
            ui_layout_area(.sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = c_col,
                           .placed = { .enabled = true, .offset = { 380.0f, 100.0f } }) {}
            ui_layout_area(.sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = flow2_col) {}
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf2;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf2), "read_pixels failed");

    // The placed child scrolls with the container: A's A-only pixel moved by exactly the scroll delta (-40,-30),
    // from (105,105) to (65,75), and is gone from its old position.
    ASSERT_TRUE_MSG(px_near(&buf2, 65, 75, a_col, 2), "placed child scrolls with the container by the scroll delta");
    ASSERT_FALSE_MSG(px_near(&buf2, 105, 105, a_col, 2), "placed child no longer sits at its un-scrolled position");

    // Content extent: the clip container's content size reflects only the flowed children (D + E, 40px wide each,
    // laid out as a row = 80px), never the placed children. Queried after the second frame: Clay populates
    // contentSize once the scroll container exists; on the very first frame it is still zero.
    Clay_ScrollContainerData scd
        = Clay_GetScrollContainerData((Clay_ElementId) { .id = ui_layout_id_to_clay_id(ID("placed_clip")) });
    ASSERT_TRUE_MSG(scd.found, "clip container scroll data is queryable");
    ASSERT_TRUE_MSG(scd.contentDimensions.width <= 80.5f,
                    "placed children do not grow the container's content extent (flowed D+E only)");
}

// placed_child_interaction - a placed child is a first-class interactive element: hit-testing, hover/press/
// click, the element-local mouse_pos, and topmost-wins arbitration all land at the placed (offset) rect, not the
// flex position it skipped. The scene is a 400x300 clip box at root origin (40, 30); two interactive placed
// children A (offset 100,100) and B (offset 120,110) overlap.

typedef struct PlacedHit {
    FlSignal a, b;
    FlElementState sa, sb;
} PlacedHit;

// Emit one frame of the two-overlapping-placed-children scene with the cursor at (mx, my); click injects a
// same-frame press+release fast tap. A is emitted before B, so B is the topmost (later-drawn) sibling. Both are
// clickable and carry a color so they emit a RECTANGLE that resolves their rect for the hit-test.
static PlacedHit placed_interaction_frame(FlUiCtx* ctx, f32 mx, f32 my, bool click) {
    ctx->input_state.mouse.x = mx;
    ctx->input_state.mouse.y = my;
    ctx->mouse_pressed_this_frame = click;
    ctx->mouse_released_this_frame = click;

    PlacedHit r = { 0 };
    ui_update_begin(ctx);
    // Root padding places the clip box at origin (40, 30) so the placed rect's origin is non-trivial.
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .padding = { .left = 40, .top = 30 }) {
        ui_layout_area_id(
            ID("pi_clip"), .sizing = { sizing_fixed(400), sizing_fixed(300) }, .color = ui_color(50, 50, 60, 255),
            .clip = { .horizontal = true, .vertical = true, .manual_scroll = true, .scroll_pos = { 0.0f, 0.0f } }) {
            ui_layout_area_id(ID("pi_a"), .sizing = { sizing_fixed(40), sizing_fixed(40) },
                              .color = ui_color(40, 200, 120, 255),
                              .placed = { .enabled = true, .offset = { 100.0f, 100.0f } }) {
                r.a = ui_item_behavior(ID("pi_a"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
            ui_layout_area_id(ID("pi_b"), .sizing = { sizing_fixed(40), sizing_fixed(40) },
                              .color = ui_color(220, 60, 60, 255),
                              .placed = { .enabled = true, .offset = { 120.0f, 110.0f } }) {
                r.b = ui_item_behavior(ID("pi_b"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    r.sa = ui_get_element_state(ID("pi_a"));
    r.sb = ui_get_element_state(ID("pi_b"));
    return r;
}

UTEST(render, placed_child_interaction) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx); // 1920x1080, identity scale → logical == physical

    // Geometry (root padding origin (40,30) + placed offset, all scale 1.0):
    //   A: content [100..140]x[100..140] → screen [140..180]x[130..170]
    //   B: content [120..160]x[110..150] → screen [160..200]x[140..180]
    // A-only point (150,135): inside A, left/above of B. Overlap point (170,155): inside both.
    const f32 a_only_x = 150.0f, a_only_y = 135.0f;
    const f32 overlap_x = 170.0f, overlap_y = 155.0f;

    // Phase 1 - hover A alone. Hot propagates over two frames (next_hot_id → hot_id → state.hot → the Signal the
    // behavior reads the following frame), so dwell a few frames before asserting hovered.
    PlacedHit r = { 0 };
    for (int f = 0; f < 4; f++) {
        r = placed_interaction_frame(ctx, a_only_x, a_only_y, false);
    }
    ASSERT_TRUE_MSG(r.a.hovered, "item_behavior on a placed child reports hovered at its placed rect");
    ASSERT_FALSE_MSG(r.b.hovered, "the non-hovered overlapping placed child does not report hovered");

    // element_state returns the placed rect (origin = root origin + placed offset), not the skipped flex position.
    ASSERT_NEAR(r.sa.pos.x, 140.0f, 0.5f);
    ASSERT_NEAR(r.sa.pos.y, 130.0f, 0.5f);
    ASSERT_NEAR(r.sa.size.x, 40.0f, 0.5f);
    ASSERT_NEAR(r.sa.size.y, 40.0f, 0.5f);
    ASSERT_TRUE_MSG(r.sa.found, "the placed child is hit-tested as an ordinary laid-out element");

    // Signal.mouse_pos is element-local relative to the PLACED rect: (cursor - placed origin) = (150-140, 135-130).
    ASSERT_NEAR(r.a.mouse_pos.x, 10.0f, 0.5f);
    ASSERT_NEAR(r.a.mouse_pos.y, 5.0f, 0.5f);

    // Phase 2 - a fast tap (press+release same frame) on the hovered placed child clicks it.
    r = placed_interaction_frame(ctx, a_only_x, a_only_y, true);
    ASSERT_TRUE_MSG(r.a.pressed, "a press over the placed rect reports pressed");
    ASSERT_TRUE_MSG(r.a.clicked, "a tap over the placed rect reports clicked");

    // Phase 3 - topmost-wins. With the cursor over the A∩B overlap, B (emitted last → drawn on top) takes the hover
    // and A does not, matching paint order. Dwell to let the hot arbitration settle on B.
    for (int f = 0; f < 4; f++) {
        r = placed_interaction_frame(ctx, overlap_x, overlap_y, false);
    }
    ASSERT_TRUE_MSG(r.b.hovered, "the topmost overlapping placed child (later sibling) wins the hover");
    ASSERT_FALSE_MSG(r.a.hovered, "the shadowed overlapping placed child does not also report hovered");
    ASSERT_TRUE_MSG(r.sb.hot, "element_state hot reflects the topmost placed child");
    ASSERT_FALSE_MSG(r.sa.hot, "element_state hot is not set on the shadowed placed child");
}

// placed_child_element_state - a placed child's element_state origin tracks the container scroll (it is a
// flowed child for scroll/scissor purposes), and the element-local mouse_position stays in logical authoring
// space under a non-identity UI scale. Single placed child A at content offset (100,100) in a manual_scroll clip
// box at root origin (40,30).
UTEST(render, placed_child_element_state) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    // --- Scroll tracking (identity scale) ---
    // Frame 1, scroll (0,0): the placed rect resolves at origin + offset = (140, 130).
    ctx->input_state.mouse.x = 0.0f;
    ctx->input_state.mouse.y = 0.0f;
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .padding = { .left = 40, .top = 30 }) {
        ui_layout_area_id(
            ID("pes_clip"), .sizing = { sizing_fixed(400), sizing_fixed(300) }, .color = ui_color(50, 50, 60, 255),
            .clip = { .horizontal = true, .vertical = true, .manual_scroll = true, .scroll_pos = { 0.0f, 0.0f } }) {
            ui_layout_area_id(ID("pes_a"), .sizing = { sizing_fixed(40), sizing_fixed(40) },
                              .color = ui_color(40, 200, 120, 255),
                              .placed = { .enabled = true, .offset = { 100.0f, 100.0f } }) {
                ui_item_behavior(ID("pes_a"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlElementState s0 = ui_get_element_state(ID("pes_a"));
    ASSERT_TRUE_MSG(s0.found, "placed child element_state is found");
    ASSERT_NEAR(s0.pos.x, 140.0f, 0.5f);
    ASSERT_NEAR(s0.pos.y, 130.0f, 0.5f);

    // Frame 2, scroll (-40,-30): the placed rect scrolls with the container by exactly the scroll delta → (100,100).
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .padding = { .left = 40, .top = 30 }) {
        ui_layout_area_id(
            ID("pes_clip"), .sizing = { sizing_fixed(400), sizing_fixed(300) }, .color = ui_color(50, 50, 60, 255),
            .clip = { .horizontal = true, .vertical = true, .manual_scroll = true, .scroll_pos = { -40.0f, -30.0f } }) {
            ui_layout_area_id(ID("pes_a"), .sizing = { sizing_fixed(40), sizing_fixed(40) },
                              .color = ui_color(40, 200, 120, 255),
                              .placed = { .enabled = true, .offset = { 100.0f, 100.0f } }) {
                ui_item_behavior(ID("pes_a"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlElementState s1 = ui_get_element_state(ID("pes_a"));
    ASSERT_NEAR(s1.pos.x, 100.0f, 0.5f); // 140 + (-40)
    ASSERT_NEAR(s1.pos.y, 100.0f, 0.5f); // 130 + (-30)

    // --- Logical-space pointer under a non-identity UI scale ---
    // At ui_scale 0.5 the placed offset (100,100 logical) scales to (50,50) physical and the root padding (40,30
    // logical) to (20,15) physical, so A's physical origin is (70, 65). A cursor 10 physical px inside resolves to
    // 20 logical px in - proving the placed rect flows through the logical-space mouse query (no reprojection).
    ctx->layout_width = 960.0f;
    ctx->layout_height = 540.0f;
    UiScalePair fit_540 = ui_reference_fit(540);
    ui_set_scale(fit_540.ui_scale, fit_540.text_scale);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 960.0f, .height = 540.0f });

    ctx->input_state.mouse.x = 0.0f;
    ctx->input_state.mouse.y = 0.0f;
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .padding = { .left = 40, .top = 30 }) {
        ui_layout_area_id(
            ID("pes_clip"), .sizing = { sizing_fixed(400), sizing_fixed(300) }, .color = ui_color(50, 50, 60, 255),
            .clip = { .horizontal = true, .vertical = true, .manual_scroll = true, .scroll_pos = { 0.0f, 0.0f } }) {
            ui_layout_area_id(ID("pes_a"), .sizing = { sizing_fixed(40), sizing_fixed(40) },
                              .color = ui_color(40, 200, 120, 255),
                              .placed = { .enabled = true, .offset = { 100.0f, 100.0f } }) {
                ui_item_behavior(ID("pes_a"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlElementState sh = ui_get_element_state(ID("pes_a"));
    ASSERT_TRUE_MSG(sh.found, "placed child is found at 0.5 scale");
    ASSERT_NEAR(sh.pos.x, 70.0f, 0.5f); // (40+100) logical * 0.5 = 70 physical
    ASSERT_NEAR(sh.pos.y, 65.0f, 0.5f); // (30+100) logical * 0.5 = 65 physical

    ctx->input_state.mouse.x = 80.0f; // 10 physical px inside on x (origin 70)
    ctx->input_state.mouse.y = 75.0f; // 10 physical px inside on y (origin 65)
    FlVec2 local = ui_mouse_position(ID("pes_a"));
    ASSERT_NEAR(local.x, 20.0f, 0.5f); // 10 physical / 0.5 = 20 logical
    ASSERT_NEAR(local.y, 20.0f, 0.5f);
}

// scrollbar_auto_id_no_collision - two ui_scrollbar widgets in one frame WITHOUT an id_override must not
// collide. The auto id is seeded from the bound value pointer (a stable per-instance key), so each scrollbar
// gets a distinct element id. Two side-by-side vertical scrollbars bound to different float*, one hovered and
// tapped, the other asserted untouched.

typedef struct TwoScrollbars {
    FlSignal a, b;
} TwoScrollbars;

// One frame: scrollbar A (bound va) in the left box, scrollbar B (bound vb) in the right box, cursor at (mx, my)
// with an optional same-frame press+release tap. Neither passes id_override, so both take the auto-id path.
static TwoScrollbars two_scrollbars_frame(FlUiCtx* ctx, float* va, float* vb, f32 mx, f32 my, bool click) {
    ctx->input_state.mouse.x = mx;
    ctx->input_state.mouse.y = my;
    ctx->mouse_pressed_this_frame = click;
    ctx->mouse_released_this_frame = click;

    TwoScrollbars r = { 0 };
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        // Left box [0..40]x[0..300] holds scrollbar A; a 160px spacer separates it from the right box so hovering
        // A never touches B.
        ui_layout_area_id(ID("sb_box_a"), .sizing = { sizing_fixed(40), sizing_fixed(300) }) {
            r.a = ui_scrollbar(va, .min = 0.0f, .max = 100.0f, .width = 40.0f);
        }
        ui_layout_area(.sizing = { sizing_fixed(160), sizing_fixed(300) }) {}
        ui_layout_area_id(ID("sb_box_b"), .sizing = { sizing_fixed(40), sizing_fixed(300) }) {
            r.b = ui_scrollbar(vb, .min = 0.0f, .max = 100.0f, .width = 40.0f);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    return r;
}

UTEST(render, scrollbar_auto_id_no_collision) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    float va = 0.0f;
    float vb = 25.0f;

    // Dwell hovering over scrollbar A's track (x=20 is inside the left box's [0..40] scrollbar) so A's hot state
    // propagates before the tap.
    TwoScrollbars r = { 0 };
    for (int f = 0; f < 5; f++) {
        r = two_scrollbars_frame(ctx, &va, &vb, 20.0f, 150.0f, false);
    }
    ASSERT_TRUE_MSG(r.a.hovered, "scrollbar A reports hovered while the cursor is over its track");
    ASSERT_FALSE_MSG(r.b.hovered, "scrollbar B (distinct id) does not report hovered — no shared hot state");

    // Tap the middle of A's track. A's value jumps toward mid-range; B's value must be untouched (its own id, no
    // shared drag state).
    r = two_scrollbars_frame(ctx, &va, &vb, 20.0f, 150.0f, true);
    ASSERT_TRUE_MSG(va > 1.0f, "tapping scrollbar A moves its bound value");
    ASSERT_EQ_MSG(vb, 25.0f, "scrollbar B's value is unchanged — the two scrollbars do not share drag state");
}

// scrollbar_full_viewport_ratio_no_nan - with viewport_ratio 1.0 the thumb fills the track, so the drag path's
// usable_track is exactly 0. The 0/0 that produced was a NaN, and clamp_f32_impl's two comparisons both fail on
// NaN, so it passed straight through into the bound value.
UTEST(render, scrollbar_full_viewport_ratio_no_nan) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    float value = 25.0f;

    for (int f = 0; f < 3; f++) {
        ctx->input_state.mouse.x = 20.0f;
        ctx->input_state.mouse.y = 150.0f;
        ctx->mouse_pressed_this_frame = (f == 2);
        ctx->mouse_released_this_frame = (f == 2);

        ui_update_begin(ctx);
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
            ui_layout_area_id(ID("sb_full_box"), .sizing = { sizing_fixed(40), sizing_fixed(300) }) {
                ui_scrollbar(&value, .min = 0.0f, .max = 100.0f, .width = 40.0f, .viewport_ratio = 1.0f,
                             .widget.id_override = FL_S("sb_full"));
            }
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }

    ASSERT_TRUE_MSG(value == value, "tapping a full-viewport scrollbar must not write NaN into the bound value");
    ASSERT_TRUE_MSG(value >= 0.0f && value <= 100.0f, "the bound value must stay inside [min, max]");
}

// placed_child_culling - an offscreen placed child participates in the engine's offscreen culling like any
// flowed child: it is not drawn (emits no render command) and never becomes interactive, so the app's own
// visible-range virtualization has a correctness backstop. The scene holds one on-screen placed child (the
// control) and one placed far past the screen bounds, both registered interactive.
UTEST(render, placed_child_culling) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx); // 1920x1080

    const FlColor onscreen_col = ui_color(40, 200, 120, 255); // control: placed inside the viewport
    const FlColor offscreen_col = ui_color(230, 90, 30, 255); // placed far past the 1920x1080 bounds

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_layout_area_id(
            ID("cull_clip"), .sizing = { sizing_fixed(400), sizing_fixed(300) }, .color = ui_color(50, 50, 60, 255),
            .clip = { .horizontal = true, .vertical = true, .manual_scroll = true, .scroll_pos = { 0.0f, 0.0f } }) {
            ui_layout_area_id(ID("cull_on"), .sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = onscreen_col,
                              .placed = { .enabled = true, .offset = { 100.0f, 100.0f } }) {
                ui_item_behavior(ID("cull_on"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
            // Placed at (3000, 3000): its rect lands far past the 1920x1080 screen → engine culls it.
            ui_layout_area_id(ID("cull_off"), .sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = offscreen_col,
                              .placed = { .enabled = true, .offset = { 3000.0f, 3000.0f } }) {
                ui_item_behavior(ID("cull_off"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    bool saw_onscreen = false, saw_offscreen = false;
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (!ui_get_render_command(i, &cmd) || cmd.type != UiRenderCmd_Rectangle) {
            continue;
        }
        if ((u8)(cmd.bg_color.r + 0.5f) == 40 && (u8)(cmd.bg_color.g + 0.5f) == 200
            && (u8)(cmd.bg_color.b + 0.5f) == 120) {
            saw_onscreen = true;
        }
        if ((u8)(cmd.bg_color.r + 0.5f) == 230 && (u8)(cmd.bg_color.g + 0.5f) == 90
            && (u8)(cmd.bg_color.b + 0.5f) == 30) {
            saw_offscreen = true;
        }
    }
    ASSERT_TRUE_MSG(saw_onscreen, "the on-screen placed child is drawn (control)");
    ASSERT_FALSE_MSG(saw_offscreen, "the offscreen placed child is culled — not drawn downstream");

    // The culled child is registered interactive (item_behavior ran) yet never resolves a rect → not found, not
    // laid out for interaction. The on-screen control is found, proving the scene is otherwise live.
    FlElementState on = ui_get_element_state(ID("cull_on"));
    FlElementState off = ui_get_element_state(ID("cull_off"));
    ASSERT_TRUE_MSG(on.found, "the on-screen placed child is laid out and interactive");
    ASSERT_FALSE_MSG(off.found, "the culled placed child is not laid out (no interaction downstream)");
    ASSERT_FALSE_MSG(off.hot, "the culled placed child never becomes hot");
}

// An element that sets BOTH a color: background and a paint span renders the paint OVER the fill (per-element
// z-stack background -> own scissor -> paint span -> children -> border), and the paint span is clipped to the
// element's OWN rect - overshoot is cropped even with no ancestor clip in play.
UTEST(render, canvas_paint_over_fill_and_self_clip) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    const FlColor fill_col = ui_color(30, 30, 120, 255);    // opaque color: background
    const FlColor paint_col = ui_color(40, 200, 120, 255);  // opaque paint, drawn over the fill
    const FlColor child_col = ui_color(230, 90, 60, 255);   // opaque child, drawn over the paint
    const FlColor border_col = ui_color(250, 230, 40, 255); // opaque border, drawn over the paint
    const FlColor clear_col = ui_color(12, 12, 14, 255);    // canvas_fixture_reset clear color

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        // 100x100 canvas at origin with a solid color: fill and a 4px border. Neither it nor any ancestor clips
        // (the root grow area is unclipped), so the only thing that can crop the paint is the canvas's own scissor.
        ui_layout_area_id(ID("fillcanvas"), .sizing = { sizing_fixed(100), sizing_fixed(100) }, .color = fill_col,
                          .border = fl_border(border_col, 4)) {
            Painter* p = ui_get_painter();
            ASSERT_TRUE_MSG(p != nullptr, "ui_get_painter is valid inside an open painting scope");
            // A paint band that overshoots the element to the right (x 0..200; the element is only 100 wide).
            painter_rect(p, 0.0f, 0.0f, 200.0f, 50.0f, paint_col);
            // Child box at the canvas origin - must draw OVER the paint.
            ui_layout_area(.sizing = { sizing_fixed(30), sizing_fixed(30) }, .color = child_col) {}
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");

    // (50,25): inside the canvas, under the paint band, not under the child → paint shows OVER the color: fill.
    ASSERT_TRUE_MSG(px_near(&buf, 50, 25, paint_col, 2), "paint composes over the element's own color: background");
    // (80,80): inside the canvas but below the paint band (y>50), not under the child → the bare fill shows.
    ASSERT_TRUE_MSG(px_near(&buf, 80, 80, fill_col, 2),
                    "the color: fill is a valid backdrop where paint does not cover");
    // (15,15): under the child box (0..30) → the child draws over the paint.
    ASSERT_TRUE_MSG(px_near(&buf, 15, 15, child_col, 2), "children compose over the paint span");
    // (60,2): on the canvas's own top border (y 0..4), over the paint band (y<50), clear of the child (x>30) →
    // the border draws over the paint (border is the upward-pass top of the per-element stack).
    ASSERT_TRUE_MSG(px_near(&buf, 60, 2, border_col, 2), "the element's border composes over the paint span");
    // (150,25): within the paint band's x-extent (0..200) but OUTSIDE the canvas's own 100px rect, with no
    // ancestor clip - the canvas's own scissor crops the overshoot, so the paint must NOT appear here.
    ASSERT_FALSE_MSG(px_near(&buf, 150, 25, paint_col, 2),
                     "paint is clipped to the element's own rect, not bled past it");
    ASSERT_TRUE_MSG(px_near(&buf, 150, 25, clear_col, 2), "the overshoot region shows the clear color, not bled paint");

    ASSERT_EQ_MSG(canvas_custom_command_count(), 1, "fill + paint + child still emits exactly one CUSTOM span");
}

// A floating element declared after the canvas composes over the paint.
UTEST(render, canvas_under_popup) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    const FlColor canvas_col = ui_color(40, 200, 120, 255);
    const FlColor popup_col = ui_color(90, 120, 230, 255);

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        ui_layout_area_id(ID("canvas3"), .sizing = { sizing_fixed(200), sizing_fixed(200) }) {
            painter_rect(ui_get_painter(), 0.0f, 0.0f, 200.0f, 200.0f, canvas_col);
        }
        // Popup: a floating element attached to the root at the origin, declared AFTER the canvas, so it composes
        // over the paint.
        ui_floating_area_id(ID("popup"), .attach_to = FlFloatingAttachTo_Root, .z_index = 1,
                            .sizing = { sizing_fixed(100), sizing_fixed(100) }) {
            ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = popup_col) {}
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");

    // (50,50) is under the popup (0..100) → the popup composes over the canvas paint.
    ASSERT_TRUE_MSG(px_near(&buf, 50, 50, popup_col, 2), "a later floating element draws over the canvas paint");
    // (150,150) is canvas paint outside the popup → still the painted fill.
    ASSERT_TRUE_MSG(px_near(&buf, 150, 150, canvas_col, 2), "the canvas paint shows where the popup does not cover");
}

// A custom control is an ordinary layout element (interaction by id via ui_widget_behavior / ui_get_element_state)
// whose fill is painted inside its scope. Also pins ui_get_painter() scope validity.
UTEST(render, canvas_custom_control) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    // Outside any frame/scope, the painter accessor is null - paint has nowhere to attach.
    ASSERT_TRUE_MSG(ui_get_painter() == nullptr, "ui_get_painter is null outside any painting scope");

    const FlColor ctrl_col = ui_color(200, 160, 40, 255);
    const FlLayoutId ctrl_id = ID("custom_control");
    bool painter_valid_in_scope = false;

    ui_update_begin(ctx);
    // A frame is open but no layout element is - still no scope.
    ASSERT_TRUE_MSG(ui_get_painter() == nullptr, "ui_get_painter is null before any layout element opens");

    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_layout_area_id(ctrl_id, .sizing = { sizing_fixed(80), sizing_fixed(80) }) {
            FlSignal sig = ui_widget_behavior(ctrl_id, (FlWidgetConfig) { 0 });
            (void)sig;
            Painter* p = ui_get_painter();
            painter_valid_in_scope = (p != nullptr);
            painter_rect(p, 0.0f, 0.0f, 80.0f, 80.0f, ctrl_col);
        }
    }
    ASSERT_TRUE_MSG(ui_get_painter() == nullptr, "ui_get_painter is null again once the scope closes");
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_TRUE_MSG(painter_valid_in_scope, "ui_get_painter was valid inside the control's scope");

    FlElementState st = ui_get_element_state(ctrl_id);
    ASSERT_TRUE_MSG(st.found, "the custom control is hit-tested as an ordinary layout element");
    ASSERT_EQ_MSG(canvas_custom_command_count(), 1, "painting inside the control emits one CUSTOM span");

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(px_near(&buf, 40, 40, ctrl_col, 2), "the control's painted fill rendered inside its element");
}

// Command/element accounting: a non-painting scope emits zero CUSTOM, and painting many primitives into a scope
// adds exactly one render command (the CUSTOM) and no extra layout element. The two frames render the identical
// layout so the only difference in command count is the paint.
UTEST(render, canvas_command_accounting) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    const FlColor box_col = ui_color(70, 70, 90, 255);
    const FlColor paint_col = ui_color(40, 200, 120, 255);

    // Frame A: the element opens a painting scope but paints nothing → no CUSTOM command, baseline count.
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        ui_layout_area_id(ID("acct"), .sizing = { sizing_fixed(100), sizing_fixed(100) }, .color = box_col) {}
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    const int base_count = ui_get_render_command_count();
    ASSERT_EQ_MSG(canvas_custom_command_count(), 0, "a scope with no painter calls emits no CUSTOM command");

    // Frame B: the identical layout, but the element paints several primitives.
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        ui_layout_area_id(ID("acct"), .sizing = { sizing_fixed(100), sizing_fixed(100) }, .color = box_col) {
            Painter* p = ui_get_painter();
            painter_rect(p, 0.0f, 0.0f, 100.0f, 100.0f, paint_col);
            painter_rect(p, 2.0f, 2.0f, 4.0f, 4.0f, paint_col);
            painter_circle(p, 50.0f, 50.0f, 6.0f, paint_col, ui_color(0, 0, 0, 0), 0.0f);
            painter_rect(p, 8.0f, 8.0f, 4.0f, 4.0f, paint_col);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    ASSERT_EQ_MSG(canvas_custom_command_count(), 1, "many primitives in one scope emit exactly one CUSTOM command");
    // Painting added exactly three commands: the CUSTOM span plus the SCISSOR_START/SCISSOR_END that bracket it
    // to the element's own rect, and zero extra layout elements.
    ASSERT_EQ_MSG(ui_get_render_command_count(), base_count + 3,
                  "painting adds the CUSTOM span plus its own-rect scissor bracket, and zero extra layout elements");
}

// The splice translates element-local paint by the element's resolved rect origin. Root padding places the
// canvas at a non-zero origin, so element-local (0,0) must land at that origin.
UTEST(render, canvas_paint_translated_by_origin) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    const FlColor canvas_col = ui_color(40, 200, 120, 255);

    ui_update_begin(ctx);
    // Root padding pushes its first child to origin (40, 30).
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom, .padding = { .left = 40, .top = 30 }) {
        ui_layout_area_id(ID("offset_canvas"), .sizing = { sizing_fixed(80), sizing_fixed(80) }) {
            painter_rect(ui_get_painter(), 0.0f, 0.0f, 80.0f, 80.0f, canvas_col); // element-local (0,0)
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");

    // (50,40) is element-local (10,10) once the origin (40,30) is added → the painted fill.
    ASSERT_TRUE_MSG(px_near(&buf, 50, 40, canvas_col, 2), "paint is translated by the element's rect origin");
    // (5,5) is in the root padding, above-left of the canvas → element-local (0,0) did NOT land at window (0,0).
    ASSERT_FALSE_MSG(px_near(&buf, 5, 5, canvas_col, 2), "the splice adds the origin, not a global (0,0) paste");
}

// Element-local pointer position: ui_mouse_position returns (mouse - origin) / ui_scale in logical
// (authoring) coordinates - the space the painter draws in. Verified at identity scale and at 0.5 scale to
// prove the query stays in logical authoring space at any UI scale (a physical-pixel offset would differ).
UTEST(render, canvas_element_local_pointer_pos) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx); // identity scale → ui_scale == 1.0

    // One frame resolves the element's rect; immediate mode means the query reads the previous frame's rect.
    // Root padding places the 80x80 canvas at origin (40, 30).
    ctx->input_state.mouse.x = 0.0f;
    ctx->input_state.mouse.y = 0.0f;
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .padding = { .left = 40, .top = 30 }) {
        ui_layout_area_id(ID("ptr_canvas"), .sizing = { sizing_fixed(80), sizing_fixed(80) }) {
            // A canvas registers as an ordinary interactive element by id; this records its resolved rect. The
            // paint emits a command carrying the element id, which is what marks the rect resolved (found).
            ui_widget_behavior(ID("ptr_canvas"), (FlWidgetConfig) { 0 });
            painter_rect(ui_get_painter(), 0.0f, 0.0f, 80.0f, 80.0f, ui_color(40, 200, 120, 255));
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlElementState st = ui_get_element_state(ID("ptr_canvas"));
    ASSERT_TRUE_MSG(st.found, "the canvas element is in the layout after one frame");
    ASSERT_NEAR(st.pos.x, 40.0f, 0.5f);
    ASSERT_NEAR(st.pos.y, 30.0f, 0.5f);

    // Pointer at window (50, 40) is element-local (10, 10); at scale 1.0 logical == physical.
    ctx->input_state.mouse.x = 50.0f;
    ctx->input_state.mouse.y = 40.0f;
    FlVec2 local = ui_mouse_position(ID("ptr_canvas"));
    ASSERT_NEAR(local.x, 10.0f, 0.5f);
    ASSERT_NEAR(local.y, 10.0f, 0.5f);

    // An element absent from the layout has no rect → {0, 0}.
    FlVec2 missing = ui_mouse_position(ID("ptr_canvas_absent"));
    ASSERT_EQ_MSG((int)missing.x, 0, "no rect → zero element-local x");
    ASSERT_EQ_MSG((int)missing.y, 0, "no rect → zero element-local y");

    // The same authoring layout at half scale (540p → ui_scale 0.5). Padding (40,30) logical snaps to (20,15)
    // physical; the 80-logical element is 40 physical. A pointer 10 physical px inside the element resolves to
    // 20 logical px in - the query divides by ui_scale, so the element-local position is resolution-independent.
    ctx->layout_width = 960.0f;
    ctx->layout_height = 540.0f;
    UiScalePair fit_540 = ui_reference_fit(540); // 1080p-reference policy: ui_scale 0.5, PAL text boost folded in
    ui_set_scale(fit_540.ui_scale, fit_540.text_scale);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 960.0f, .height = 540.0f });

    ctx->input_state.mouse.x = 0.0f;
    ctx->input_state.mouse.y = 0.0f;
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .padding = { .left = 40, .top = 30 }) {
        ui_layout_area_id(ID("ptr_canvas"), .sizing = { sizing_fixed(80), sizing_fixed(80) }) {
            ui_widget_behavior(ID("ptr_canvas"), (FlWidgetConfig) { 0 });
            painter_rect(ui_get_painter(), 0.0f, 0.0f, 80.0f, 80.0f, ui_color(40, 200, 120, 255));
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlElementState st_half = ui_get_element_state(ID("ptr_canvas"));
    ASSERT_TRUE_MSG(st_half.found, "the canvas element is in the layout at 0.5 scale");
    ASSERT_NEAR(st_half.pos.x, 20.0f, 0.5f); // physical origin
    ASSERT_NEAR(st_half.pos.y, 15.0f, 0.5f);

    ctx->input_state.mouse.x = 30.0f; // 10 physical px inside the element on x
    ctx->input_state.mouse.y = 25.0f; // 10 physical px inside the element on y
    FlVec2 local_half = ui_mouse_position(ID("ptr_canvas"));
    ASSERT_NEAR(local_half.x, 20.0f, 0.5f); // 10 physical / 0.5 == 20 logical
    ASSERT_NEAR(local_half.y, 20.0f, 0.5f);
}

// A painted canvas hover-highlights the half under the cursor, and the highlight tracks the
// pointer as it moves. Synthetic mouse input is fed straight into the context (the bare render fixture has no
// input pump). The 80x80 canvas sits at origin (40, 30); its halves split at element-local x == 40, i.e.
// window x == 80.
UTEST(render, canvas_hover_highlight_tracks_pointer) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx); // identity scale → ui_scale == 1.0, element-local logical == window pixels

    const FlColor base = ui_color(40, 200, 120, 255); // un-highlighted half
    const FlColor hl = ui_color(220, 80, 60, 255);    // highlighted (under-cursor) half

    // Frame 1: resolve the canvas rect so the next frame's query sees it (immediate mode). The pointer is far
    // away, so nothing is highlighted yet.
    ctx->input_state.mouse.x = 0.0f;
    ctx->input_state.mouse.y = 0.0f;
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .padding = { .left = 40, .top = 30 }) {
        ui_layout_area_id(ID("hover_canvas"), .sizing = { sizing_fixed(80), sizing_fixed(80) }) {
            ui_widget_behavior(ID("hover_canvas"), (FlWidgetConfig) { 0 });
            Painter* p = ui_get_painter();
            painter_rect(p, 0.0f, 0.0f, 40.0f, 80.0f, base);
            painter_rect(p, 40.0f, 0.0f, 40.0f, 80.0f, base);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    // Frame 2: pointer over the RIGHT half (window x 100 → element-local x 60 ≥ 40). The right half lights up.
    ctx->input_state.mouse.x = 100.0f;
    ctx->input_state.mouse.y = 70.0f;
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .padding = { .left = 40, .top = 30 }) {
        ui_layout_area_id(ID("hover_canvas"), .sizing = { sizing_fixed(80), sizing_fixed(80) }) {
            ui_widget_behavior(ID("hover_canvas"), (FlWidgetConfig) { 0 });
            FlVec2 local = ui_mouse_position(ID("hover_canvas"));
            bool right = local.x >= 40.0f;
            Painter* p = ui_get_painter();
            painter_rect(p, 0.0f, 0.0f, 40.0f, 80.0f, right ? base : hl);
            painter_rect(p, 40.0f, 0.0f, 40.0f, 80.0f, right ? hl : base);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(px_near(&buf, 100, 70, hl, 2), "right half (under cursor) is highlighted");
    ASSERT_TRUE_MSG(px_near(&buf, 60, 70, base, 2), "left half (away from cursor) stays base color");

    // Frame 3: move the pointer to the LEFT half (window x 60 → element-local x 20 < 40). The highlight follows.
    ctx->input_state.mouse.x = 60.0f;
    ctx->input_state.mouse.y = 70.0f;
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .padding = { .left = 40, .top = 30 }) {
        ui_layout_area_id(ID("hover_canvas"), .sizing = { sizing_fixed(80), sizing_fixed(80) }) {
            ui_widget_behavior(ID("hover_canvas"), (FlWidgetConfig) { 0 });
            FlVec2 local = ui_mouse_position(ID("hover_canvas"));
            bool right = local.x >= 40.0f;
            Painter* p = ui_get_painter();
            painter_rect(p, 0.0f, 0.0f, 40.0f, 80.0f, right ? base : hl);
            painter_rect(p, 40.0f, 0.0f, 40.0f, 80.0f, right ? hl : base);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf),
                    "read_pixels failed (frame 3)");
    ASSERT_TRUE_MSG(px_near(&buf, 60, 70, hl, 2), "highlight tracked the pointer to the left half");
    ASSERT_TRUE_MSG(px_near(&buf, 100, 70, base, 2), "right half reverts to base once the cursor leaves it");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Hover-dwell tooltip: the floating label is revealed only after the pointer has dwelled over the anchor -
// driven by the real stationary timer, not the eased hover value - and hides as soon as the anchor is no longer
// hovered. Asserted on the tooltip's floating element via Clay_GetElementData rather than a pixel comparison.

// One tooltip frame: an 80x80 anchor at window (40,30), with ui_tooltip emitted right after it. The caller sets
// the mouse position and the dwell timer before calling.
static void tooltip_emit_frame(FlUiCtx* ctx, FlLayoutId anchor_id, FlString text) {
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .padding = { .left = 40, .top = 30 }) {
        // A background colour makes the anchor emit a render command, so item-state hit-testing runs on it (an
        // empty layout area produces none and would never register as hot).
        ui_layout_area_id(anchor_id, .sizing = { sizing_fixed(80), sizing_fixed(80) },
                          .color = ui_color(40, 200, 120, 255)) {
            ui_widget_behavior(anchor_id, (FlWidgetConfig) { 0 });
        }
        ui_tooltip(anchor_id, text);
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

// Is the tooltip's floating element present in the layout just produced? Mirrors ui_tooltip_impl's tip-id
// derivation (anchor name + anchor hash as sub-id) and asks Clay whether that element was laid out this frame.
static bool tooltip_present(FlLayoutId anchor_id) {
    u32 anchor_clay_id = ui_layout_id_to_clay_id(anchor_id);
    FlLayoutId tip_id = { .name = anchor_id.name, .id = (int)anchor_clay_id };
    Clay_ElementData data = Clay_GetElementData((Clay_ElementId) { .id = ui_layout_id_to_clay_id(tip_id) });
    return data.found;
}

UTEST(render, tooltip_reveals_after_dwell_hides_on_unhover) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    FlLayoutId anchor = ID("tooltip_anchor");
    FlString text = FL_S("Tooltip text");

    // Settle hover with the dwell timer held at 0: the pointer rests on the anchor (rect 40..120 x 30..110), but
    // no dwell has elapsed, so the tooltip must not show. Hot propagates next_hot_id -> hot_id -> state.hot, so a
    // few frames are needed before hot reads 1 during emit.
    ctx->input_state.mouse.x = 80.0f;
    ctx->input_state.mouse.y = 70.0f;
    for (int i = 0; i < 3; i++) {
        ctx->input_state.mouse.stationary_timer = 0.0f;
        tooltip_emit_frame(ctx, anchor, text);
    }
    ASSERT_TRUE_MSG(ui_get_element_state(anchor).hot, "anchor is hovered while the pointer rests on it");
    ASSERT_FALSE_MSG(tooltip_present(anchor), "no tooltip before the dwell threshold is reached");

    // Dwell satisfied: same hover, timer past the 0.5s default -> the tooltip is revealed.
    ctx->input_state.mouse.stationary_timer = 0.6f;
    tooltip_emit_frame(ctx, anchor, text);
    ASSERT_TRUE_MSG(tooltip_present(anchor), "tooltip appears once the pointer has dwelled past the threshold");

    // Un-hover: move the pointer off the anchor. hot trails the pointer by two frames (next_hot_id -> hot_id ->
    // state.hot), so emit a few frames to let it settle; the dwell timer is kept high to prove it is the loss of
    // hover (not the timer) that hides the tooltip.
    ctx->input_state.mouse.x = 600.0f;
    ctx->input_state.mouse.y = 500.0f;
    ctx->input_state.mouse.stationary_timer = 5.0f;
    for (int i = 0; i < 3; i++) {
        tooltip_emit_frame(ctx, anchor, text);
    }
    ASSERT_FALSE_MSG(ui_get_element_state(anchor).hot, "anchor is no longer hot after the pointer leaves");
    ASSERT_FALSE_MSG(tooltip_present(anchor), "tooltip hides once the anchor is no longer hovered");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fl_app headless runner - drives the full tick path (pump -> begin -> frame -> end -> present(no-op) ->
// input_update) and reads back a known clear color through fl_app_read_pixels, then verifies the quit flag.
// fl_app's create calls for the process-global singletons (image system, VFS) are idempotent no-ops and its
// destroy is owner-gated, so the long-lived render fixture's globals are never stranded.

typedef struct AppSmokeState {
    FlColor color;
    bool frame_ran;
    bool frame_str_ok;
    bool frame_str_array_ok;
} AppSmokeState;

static void app_smoke_frame(void* user_data) {
    AppSmokeState* st = (AppSmokeState*)user_data;

    FlString s = fl_frame_str("hi", 2);
    st->frame_str_ok = (s.length == 2 && s.data != nullptr && s.data[0] == 'h' && s.data[1] == 'i');

    // fl_frame_str_array reserves count uninitialized FlString slots in the same frame arena; a zero count
    // yields null rather than a one-element allocation.
    FlString* slots = fl_frame_str_array(3);
    bool array_ok = (slots != nullptr);
    if (array_ok) {
        for (int i = 0; i < 3; i++) {
            slots[i] = fl_frame_str("abc", (size_t)(i + 1));
        }
        // Distinct slots: each kept its own length rather than every write landing on the same address.
        for (int i = 0; i < 3; i++) {
            const FlString slot = slots[i];
            array_ok = array_ok && slot.length == (u64)(i + 1) && slot.data != nullptr && slot.data[0] == 'a';
        }
    }
    st->frame_str_array_ok = array_ok && (fl_frame_str_array(0) == nullptr);

    st->frame_ran = true;

    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = st->color) {}
}

// render.snapped_line_crisp_at_fractional_dpi - a painter 1px line renders crisp through the software backend at
// a fractional UI scale: the snap lands it on a whole device row, so that row is solid and its neighbours are
// pure background. A smeared (unsnapped) line would straddle two rows at partial coverage and fail both
// neighbour checks.
UTEST(render, snapped_line_crisp_at_fractional_dpi) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);
    ui_set_scale(1.25f, 1.25f);
    ASSERT_TRUE_MSG(ctx->ui_scale == 1.25f, "composed scale is 1.25 for the crispness render");
    ASSERT_TRUE_MSG(ctx->text_scale == 1.25f, "ui_set_scale stores the text scale verbatim too");

    const FlColor line_col = ui_color(230, 90, 60, 255);

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        // Canvas at the device origin (0,0). A hairline at local y=9 lands at device y = 9 * 1.25 = 11.25, which
        // snaps to row 11; local x 0..100 covers device x 0..125, so x=50 is mid-line.
        ui_layout_area_id(ID("canvas"), .sizing = { sizing_fixed(200), sizing_fixed(200) }) {
            painter_hline(ui_get_painter(), 0.0f, 9.0f, 100.0f, line_col);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(px_near(&buf, 50, 11, line_col, 2), "the snapped line row (11) is solid — no blur");
    ASSERT_FALSE_MSG(px_near(&buf, 50, 10, line_col, 2), "the row above the line (10) is pure background");
    ASSERT_FALSE_MSG(px_near(&buf, 50, 12, line_col, 2), "the row below the line (12) is pure background");
}

// render.snapped_line_crisp_at_fractional_scroll - a painter 1px line inside an element whose origin carries a
// fractional offset renders crisp through the software backend. A floating element's offset is
// scaled-but-unsnapped (layout.c) - the same way ui_scroll applies a scroll offset as an unsnapped element
// translation - so it is a faithful scale-1.0 stand-in for a fractional scroll. The splice snaps the absolute
// device position, landing the line on a whole row.
UTEST(render, snapped_line_crisp_at_fractional_scroll) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx); // identity scale 1.0 - isolate the fractional origin from any DPI scaling

    const FlColor line_col = ui_color(90, 200, 230, 255);

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        // Floating canvas at root with a fractional y offset (10.4) - the unsnapped element origin a fractional
        // scroll produces. The hairline at local y=0 lands at device y=10.4, which snaps to row 10; local x
        // 0..100 with the 200 x offset covers device x 200..300, so x=250 is mid-line.
        ui_floating_area_id(ID("scrolled_canvas"), .attach_to = FlFloatingAttachTo_Root, .offset = { 200.0f, 10.4f },
                            .sizing = { sizing_fixed(200), sizing_fixed(200) }) {
            painter_hline(ui_get_painter(), 0.0f, 0.0f, 100.0f, line_col);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(px_near(&buf, 250, 10, line_col, 2), "the snapped line row (10) is solid — no blur");
    ASSERT_FALSE_MSG(px_near(&buf, 250, 9, line_col, 2), "the row above the line (9) is pure background");
    ASSERT_FALSE_MSG(px_near(&buf, 250, 11, line_col, 2), "the row below the line (11) is pure background");

    canvas_fixture_reset(ctx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.list_virtualization - the List foundation (ui_list_begin/_item/_end on list_uniform's core). Renders a
// 100-row list inside a fixed 240px viewport and asserts the virtualization only emits the visible window. A
// first frame establishes the list container height (list_uniform queries it from the previous frame's Clay
// layout); the second frame, with the viewport known, is the one asserted. Each visible row emits one
// solid rect of a known color, so counting rects of that color is the count of rows actually rendered.

// Count UiRenderCmd_Rectangle commands whose background matches c (UiColor4 carries float channels).
static int list_count_row_rects(FlColor c) {
    int count = 0;
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (!ui_get_render_command(i, &cmd) || cmd.type != UiRenderCmd_Rectangle) {
            continue;
        }
        if ((u8)(cmd.bg_color.r + 0.5f) == c.r && (u8)(cmd.bg_color.g + 0.5f) == c.g
            && (u8)(cmd.bg_color.b + 0.5f) == c.b && (u8)(cmd.bg_color.a + 0.5f) == c.a) {
            count++;
        }
    }
    return count;
}

// Emit one frame of the test list with the cursor parked off the list (so idle frames report no hover/click).
// Returns the visible range; the per-frame result and whether any row reported a click come back via out-params.
static FlListRange list_render_frame(FlUiCtx* ctx, u32 total, FlColor row_color, FlListResult* out_result,
                                     bool* out_any_clicked) {
    FlListRange range = { 0, 0, 0 };
    FlListResult result = { 0, 0 };
    bool any_clicked = false;
    // Park the cursor well off the list so no row hovers/clicks - keeps the result deterministic.
    ctx->input_state.mouse.x = -100.0f;
    ctx->input_state.mouse.y = -100.0f;
    // Pin the clear color.
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(12, 12, 14);
    ui_update_begin(ctx);
    // Fixed-size viewport so the list's grow container measures deterministically after frame 1.
    ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(240) }, .direction = FlLayoutDirection_TopToBottom) {
        // NULL set → no selection (this case exercises virtualization only); ordered_ids is unused on that path.
        range
            = ui_list_begin(ID("vlist"), nullptr, total, nullptr, (FlListConfig) { .row_height = 24.0f, .gap = 4.0f });
        for (u32 i = range.start; i < range.end; i++) {
            // Flat static list: pass the index as the stable id (CONTEXT Item Id - allowed, loses nothing).
            FlSignal sig = ui_list_item_begin((u64)(i + 1));
            any_clicked |= sig.clicked;
            ui_layout_area(.sizing = { sizing_grow(), sizing_fixed(24) }, .color = row_color) {}
            ui_list_item_end();
        }
        result = ui_list_end();
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    if (out_result != nullptr) {
        *out_result = result;
    }
    if (out_any_clicked != nullptr) {
        *out_any_clicked = any_clicked;
    }
    return range;
}

UTEST(render, list_virtualization) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const int layout_w = 320;
    const int layout_h = 256;
    ctx->layout_width = (float)layout_w;
    ctx->layout_height = (float)layout_h;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)layout_w, .height = (float)layout_h });

    const u32 total = 100;
    const FlColor row_color = ui_color(60, 140, 220, 255);

    // Frame 1: establishes the list container height for the virtualization math.
    list_render_frame(ctx, total, row_color, nullptr, nullptr);
    // Frame 2: viewport is now known - assert this one.
    FlListResult result;
    bool any_clicked = true;
    FlListRange range = list_render_frame(ctx, total, row_color, &result, &any_clicked);

    ASSERT_EQ_MSG(0u, range.start, "list should be scrolled to the top");
    ASSERT_EQ_MSG(total, range.total, "range.total should report the full item count");
    ASSERT_TRUE_MSG(range.end > range.start, "expected a non-empty visible range");
    ASSERT_TRUE_MSG(range.end < total, "virtualization should not emit all rows for a 100-row list");

    const int rendered = list_count_row_rects(row_color);
    ASSERT_EQ_MSG((int)(range.end - range.start), rendered,
                  "rendered row count must equal the visible range (no off-screen rows drawn)");

    ASSERT_TRUE_MSG(!any_clicked, "no input injected — no row should report a click");
    ASSERT_EQ_MSG((u64)0, result.activated_id, "idle frame: no row activated");
    ASSERT_EQ_MSG((u64)0, result.focused_id, "cursor off-list: no row focused");

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(ctx->shared->renderer.read_pixels != nullptr, "backend has no pixel-readback capability");
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "render buffer data is null");

    dump_frame("list", &buf);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.list_empty - a zero-item list renders cleanly.

UTEST(render, list_empty) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 320.0f;
    ctx->layout_height = 256.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 320.0f, .height = 256.0f });

    const FlColor row_color = ui_color(60, 140, 220, 255);
    FlListResult result;
    FlListRange range = list_render_frame(ctx, 0, row_color, &result, nullptr);

    ASSERT_EQ_MSG(0u, range.start, "empty list start should be 0");
    ASSERT_EQ_MSG(0u, range.end, "empty list should have no visible rows");
    ASSERT_EQ_MSG(0u, range.total, "empty list total should be 0");
    ASSERT_EQ_MSG(0, list_count_row_rects(row_color), "empty list should emit no row rects");
    ASSERT_EQ_MSG((u64)0, result.activated_id, "empty list: no activation");
    ASSERT_EQ_MSG((u64)0, result.focused_id, "empty list: no focus");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.list_stable_id_split - the Item Id split (CONTEXT Item Id): the result is keyed on the app's opaque
// u64 stable id, never on the dense emission position. The cursor is aimed at the row at position 2, and
// focused_id must come back as that row's stable id.

UTEST(render, list_stable_id_split) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 320.0f;
    ctx->layout_height = 256.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 320.0f, .height = 256.0f });
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(12, 12, 14);

    const u32 total = 50;
    const u32 target_pos = 2;
    // Stable id mapping id(pos) = pos*10 + 1000: never equal to the position, so reporting the position instead
    // of the id (or vice versa) is caught unambiguously.
    const u64 target_id = (u64)target_pos * 10ull + 1000ull;

    FlListResult result = { 0, 0 };
    // Frame 0 parks the cursor off-list and locates the target row; the remaining frames hold the cursor on it.
    // Hover propagates over two frames (item_state_update sets next_hot_id -> hot_id at one frame's end, and
    // item->state.hot tracks hot_id at the next end, which item_behavior then reads the frame after), so the
    // cursor must dwell a few frames before the row reports hovered/focused.
    f32 hover_x = -100.0f, hover_y = -100.0f;

    for (int f = 0; f < 4; f++) {
        ctx->input_state.mouse.x = hover_x;
        ctx->input_state.mouse.y = hover_y;
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(240) }, .direction = FlLayoutDirection_TopToBottom) {
            FlListRange r = ui_list_begin(ID("split_list"), nullptr, total, nullptr,
                                          (FlListConfig) { .row_height = 24.0f, .gap = 4.0f });
            for (u32 i = r.start; i < r.end; i++) {
                ui_list_item_begin((u64)i * 10ull + 1000ull);
                // Unique red channel per row so the target row's rect is locatable from the command stream.
                ui_layout_area(.sizing = { sizing_grow(), sizing_fixed(24) },
                               .color = ui_color((u8)(20 + i), 120, 200, 255)) {}
                ui_list_item_end();
            }
            result = ui_list_end();
        }
        ui_update_end(ctx, 1.0f / 60.0f);

        if (f == 0) {
            // Locate the target row's rect (its unique red channel) and aim the cursor at its center for frame 2.
            const u8 want_r = (u8)(20 + target_pos);
            const int n = ui_get_render_command_count();
            for (int i = 0; i < n; i++) {
                UiRenderCommand cmd;
                if (!ui_get_render_command(i, &cmd) || cmd.type != UiRenderCmd_Rectangle) {
                    continue;
                }
                if ((u8)(cmd.bg_color.r + 0.5f) == want_r && (u8)(cmd.bg_color.g + 0.5f) == 120
                    && (u8)(cmd.bg_color.b + 0.5f) == 200) {
                    hover_x = cmd.x + cmd.width * 0.5f;
                    hover_y = cmd.y + cmd.height * 0.5f;
                    break;
                }
            }
            ASSERT_TRUE_MSG(hover_y > 0.0f, "could not locate the target row's rect after frame 1");
        }
    }

    ASSERT_EQ_MSG(target_id, result.focused_id, "focused_id must report the stable id of the hovered row");
    ASSERT_TRUE_MSG(result.focused_id != (u64)target_pos, "focused_id must not be the dense position");
}

// The end-to-end wheel case: the wheel is driven through fl_app_inject_wheel over a flagged full-window
// Scroll Target, the scoped delta accumulates into an app-owned offset, and a column of bars is painted shifted by
// it. The frames before the wheel (offset 0) and after must differ.

typedef struct WheelScrollState {
    FlUiCtx* ctx;
    f32 offset_y; // app-owned cumulative scroll offset; the framework holds no absolute wheel value
} WheelScrollState;

#define WHEEL_SCROLL_W 200

#define WHEEL_SCROLL_H 160

#define WHEEL_SCROLL_BARS 12

#define WHEEL_SCROLL_PITCH 28.0f

#define WHEEL_SCROLL_BAR_H 20.0f

// Content is taller than the viewport (12 * 28 = 336 > 160), so there is real range to scroll over.
#define WHEEL_SCROLL_MAX_OFFSET ((float)(WHEEL_SCROLL_BARS) * WHEEL_SCROLL_PITCH - (float)(WHEEL_SCROLL_H))

static const FlLayoutId k_wheel_scroll_id
    = { .name = { .data = "wheel_scroll", .length = 12, .is_static = 1 }, .id = 0 };

// The wheel over the target accumulates into offset_y, clamped to the content range. Solid painter rects only - no
// text - so the frame is byte-deterministic on the software renderer.
static void wheel_scroll_frame(void* user_data) {
    WheelScrollState* st = (WheelScrollState*)user_data;
    st->ctx = ui_get_ctx();

    ui_layout_area_id(k_wheel_scroll_id, .sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_scroll_target(k_wheel_scroll_id);
        FlVec2 wheel = ui_mouse_wheel_delta(k_wheel_scroll_id);
        st->offset_y -= wheel.y; // positive dy scrolls content up (offset grows)
        if (st->offset_y < 0.0f) {
            st->offset_y = 0.0f;
        }
        if (st->offset_y > WHEEL_SCROLL_MAX_OFFSET) {
            st->offset_y = WHEEL_SCROLL_MAX_OFFSET;
        }

        Painter* p = st->ctx->painter;
        painter_rect(p, 0.0f, 0.0f, (float)WHEEL_SCROLL_W, (float)WHEEL_SCROLL_H, ui_color(18, 20, 26, 255));
        for (int i = 0; i < WHEEL_SCROLL_BARS; i++) {
            float y = (float)i * WHEEL_SCROLL_PITCH - st->offset_y;
            FlColor color = ui_color((u8)(30 + i * 18), (u8)(210 - i * 14), (u8)(120 + i * 8), 255);
            painter_rect(p, 0.0f, y, (float)WHEEL_SCROLL_W, WHEEL_SCROLL_BAR_H, color);
        }
    }
}

UTEST(render, wheel_scroll) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings
        = { .window_title = FL_S("wheel_scroll"), .width = WHEEL_SCROLL_W, .height = WHEEL_SCROLL_H, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    WheelScrollState st = { 0 };

    // Settle the pointer over the Scroll Target so it becomes the hot (wheel-eligible) target.
    fl_app_inject_mouse_move(app, WHEEL_SCROLL_W / 2, WHEEL_SCROLL_H / 2);
    for (int i = 0; i < 4; i++) {
        fl_app_tick(app, &st, wheel_scroll_frame);
    }
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    ASSERT_EQ_MSG(0.0, (double)st.offset_y, "no wheel injected yet: offset is at the top");

    FlPixelReadback before;
    ASSERT_TRUE_MSG(st.ctx->shared->renderer.read_pixels != nullptr, "backend has no pixel-readback capability");
    ASSERT_TRUE_MSG(render_seam_read_pixels(&st.ctx->shared->renderer, st.ctx->surface, &before), "read_pixels failed");
    ASSERT_TRUE_MSG(before.width == WHEEL_SCROLL_W && before.height == WHEEL_SCROLL_H, "unexpected before-frame size");
    uint64_t before_hash = fnv1a_64(before.pixels, (size_t)before.width * (size_t)before.height * 3u);
    dump_frame("wheel_before", &before);

    // Cached wheel_y is unscaled (input caches the accumulated delta verbatim), so one inject of
    // dy = -WHEEL_SCROLL_MAX_OFFSET drives the offset to exactly the clamped bottom.
    fl_app_inject_wheel(app, 0.0f, -WHEEL_SCROLL_MAX_OFFSET);
    fl_app_tick(app, &st, wheel_scroll_frame);
    ASSERT_NEAR_MSG((double)WHEEL_SCROLL_MAX_OFFSET, (double)st.offset_y, 0.001,
                    "wheel over the hot Scroll Target routed the scoped delta into the app offset");

    FlPixelReadback after;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&st.ctx->shared->renderer, st.ctx->surface, &after), "read_pixels failed");
    uint64_t after_hash = fnv1a_64(after.pixels, (size_t)after.width * (size_t)after.height * 3u);
    dump_frame("wheel_after", &after);

    ASSERT_NE_MSG((long long)before_hash, (long long)after_hash, "wheel scroll must change the rendered frame");

    fl_app_destroy(app);
}

UTEST(render, app_headless_smoke) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    const int w = 64;
    const int h = 64;
    FlAppSettings settings = { .window_title = FL_S("headless"), .width = w, .height = h, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    // The injection surface is live in headless mode: these must not trip the headless assert or quit.
    fl_app_inject_mouse_move(app, 10, 10);
    fl_app_inject_key(app, FlKey_Space, true);

    AppSmokeState st = { .color = ui_color(40, 160, 90, 255) }; // distinctive green vs. the clear color

    // Two ticks: the first sources dt=0 (no previous timestamp), the second exercises the real-dt branch.
    ASSERT_TRUE_MSG(fl_app_tick(app, &st, app_smoke_frame), "first tick should keep running");
    ASSERT_TRUE_MSG(st.frame_ran, "frame callback did not run");
    ASSERT_TRUE_MSG(st.frame_str_ok, "fl_frame_str did not return the expected per-frame copy");
    ASSERT_TRUE_MSG(st.frame_str_array_ok,
                    "fl_frame_str_array did not reserve distinct writable slots (or count 0 was not null)");
    ASSERT_TRUE_MSG(fl_app_tick(app, &st, app_smoke_frame), "second tick should keep running");

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(fl_app_read_pixels(app, &buf), "fl_app_read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "readback buffer is null");
    ASSERT_EQ(buf.width, (u32)w);
    ASSERT_EQ(buf.height, (u32)h);

    // Sample the center: it sits well inside the full-window rect.
    const u8* pixels = (const u8*)buf.pixels;
    const size_t offset = (size_t)(h / 2) * (size_t)buf.stride + (size_t)(w / 2) * 3u;
    const u8 r = pixels[offset + 0];
    const u8 g = pixels[offset + 1];
    const u8 b = pixels[offset + 2];
    ASSERT_TRUE_MSG(g > 100, "expected a strong green channel in the presented frame");
    ASSERT_TRUE_MSG(g > r && g > b, "expected green to dominate red and blue");

    fl_app_inject_window_close(app);
    ASSERT_FALSE_MSG(fl_app_tick(app, &st, app_smoke_frame), "window-close should stop the loop");

    fl_app_destroy(app);

    // With a close already queued, run()'s first tick sees the quit flag and returns false instead of blocking.
    FlApp* app2 = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app2 != nullptr, "second fl_app_create failed");
    fl_app_inject_window_close(app2);
    fl_app_run(app2, &st, app_smoke_frame); // returns; would hang here if the quit flag were ignored
    fl_app_destroy(app2);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.adjacent_radio_groups_no_id_collision - end-to-end guard that adjacent multi-item widgets lay out in
// one frame without an id collision. A collision makes the debug duplicate-id tracker (ui_open_element_with_id)
// fatally assert, so reaching frame end proves the ids stayed distinct.

UTEST(render, adjacent_radio_groups_no_id_collision) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ASSERT_TRUE_MSG(ui_get_ctx() == ctx, "widgets forward to the current ctx; fixture ctx must be current");

    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });
    ui_set_scale(1.0f, 1.0f);
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 3));

    // Adjacent int fields → &sel.b == &sel.a + 4. Group A has 5 options so its top item index (4) lands exactly
    // on group B's key under a plain linear seed.
    struct {
        int a;
        int b;
    } sel = { 0, 0 };
    FlString opts_a[] = { FL_S("A0"), FL_S("A1"), FL_S("A2"), FL_S("A3"), FL_S("A4") };
    FlString opts_b[] = { FL_S("B0"), FL_S("B1"), FL_S("B2") };

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        fl_radio_group(opts_a, 5, &sel.a);
        fl_radio_group(opts_b, 3, &sel.b);
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    // Reaching here without the duplicate-id assert firing is the regression guard; selections stay put with no
    // input injected.
    ASSERT_EQ_MSG(sel.a, 0, "group A selection unchanged with no input");
    ASSERT_EQ_MSG(sel.b, 0, "group B selection unchanged with no input");
    ASSERT_TRUE_MSG(ui_get_render_command_count() > 0, "adjacent radio groups must emit render commands");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.adjacent_tab_bars_no_id_collision - the tab-strip analogue of the radio guard above. Two tab bars keyed
// on adjacent int fields (a 4-byte gap), the first with 5 tabs so its top tab index (4) aliases the second bar's
// tab 0 under a plain linear seed - reaching frame end without the duplicate-id assert proves the ids stayed
// distinct.

UTEST(render, adjacent_tab_bars_no_id_collision) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ASSERT_TRUE_MSG(ui_get_ctx() == ctx, "widgets forward to the current ctx; fixture ctx must be current");

    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });
    ui_set_scale(1.0f, 1.0f);
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 3));

    struct {
        int a;
        int b;
    } sel = { 0, 0 };
    FlString tabs_a[] = { FL_S("A0"), FL_S("A1"), FL_S("A2"), FL_S("A3"), FL_S("A4") };
    FlString tabs_b[] = { FL_S("B0"), FL_S("B1"), FL_S("B2") };

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        ui_tab_bar(tabs_a, 5, &sel.a);
        ui_tab_bar(tabs_b, 3, &sel.b);
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_EQ_MSG(sel.a, 0, "bar A selection unchanged with no input");
    ASSERT_EQ_MSG(sel.b, 0, "bar B selection unchanged with no input");
    ASSERT_TRUE_MSG(ui_get_render_command_count() > 0, "adjacent tab bars must emit render commands");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.tab_bar_keyboard_nav_uses_seeded_ids - guards the two keyboard-nav seed sites (prev/next tab in
// ui_tab_bar_impl). A mismatch there would NOT trip the duplicate-id assert (only the render pass opens elements);
// it would silently point fl_set_focus at the wrong id.

UTEST(render, tab_bar_keyboard_nav_uses_seeded_ids) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });
    ui_set_scale(1.0f, 1.0f);
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 3));

    int sel = 1;
    FlString tabs[] = { FL_S("T0"), FL_S("T1"), FL_S("T2") };
    const u32 key = (u32)(uintptr_t)&sel;
    const u32 tab0 = ui_item_id_seed(key, 0u);
    const u32 tab1 = ui_item_id_seed(key, 1u);
    const u32 tab2 = ui_item_id_seed(key, 2u);
    const u32 tab0_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("tab"), .id = tab0 });
    const u32 tab1_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("tab"), .id = tab1 });
    const u32 tab2_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("tab"), .id = tab2 });

    // Frame 1: lay out a focusable tab bar so the per-tab ItemStates exist.
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_tab_bar(tabs, 3, &sel, .widget.focusable = FlFocusable_Yes);
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ItemState* item1 = hashmap_get(&ctx->shared->item_state, tab1_id);
    ASSERT_TRUE_MSG(item1 != nullptr && item1->state.size.x > 0.0f, "middle tab was not laid out");

    ctx->focused_id = tab1_id;
    item1->state.focused_t = 1.0f;
    ui_update_begin(ctx);
    ctx->nav_frame.nav_right = true; // set after begin; the tab bar consumes it before core spatial nav (update_end)
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_tab_bar(tabs, 3, &sel, .widget.focusable = FlFocusable_Yes);
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    ASSERT_EQ_MSG(fl_focused_id(), tab2_id, "nav-right from tab 1 must focus tab 2 (next-tab seed site)");

    item1 = hashmap_get(&ctx->shared->item_state, tab1_id);
    ASSERT_TRUE_MSG(item1 != nullptr, "middle tab state persists");
    ctx->focused_id = tab1_id;
    item1->state.focused_t = 1.0f;
    ui_update_begin(ctx);
    ctx->nav_frame.nav_left = true;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_tab_bar(tabs, 3, &sel, .widget.focusable = FlFocusable_Yes);
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    ASSERT_EQ_MSG(fl_focused_id(), tab0_id, "nav-left from tab 1 must focus tab 0 (prev-tab seed site)");

    ctx->focused_id = 0;
    ctx->nav_frame.nav_left = false;
    ctx->nav_frame.nav_right = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Auto-restore repairs a vanished focused widget; it never seeds one. The ring is unclaimed until a Tab, a click
// or an explicit fl_set_focus moves it.

static void focus_idle_frame(FlUiCtx* ctx) {
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_button(FL_S("idle_first"));
        ui_button(FL_S("idle_second"));
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(render, idle_frame_leaves_the_keyboard_ring_unclaimed) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });
    ui_set_scale(1.0f, 1.0f);
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 3));

    const u32 first_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("idle_first"), .id = 0 });

    ctx->focused_id = 0;
    ctx->last_focused_id = 0;

    // The ring is an animation, so a single frame cannot show it not ramping: run several.
    for (int frame = 0; frame < 3; frame++) {
        focus_idle_frame(ctx);
        ASSERT_EQ_MSG(fl_focused_id(), 0u, "an idle frame must leave the keyboard ring unclaimed");
    }
    ASSERT_FALSE_MSG(focus_ring_drawn(), "no widget strokes a focus border before the user aims at one");

    ui_update_begin(ctx);
    ctx->nav_next_pressed = true; // set after begin, like the directional flags the tab bar consumes
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_button(FL_S("idle_first"));
        ui_button(FL_S("idle_second"));
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    ctx->nav_next_pressed = false;
    ASSERT_EQ_MSG(fl_focused_id(), first_id, "the first Tab must land on the first focusable widget");

    // A focused widget that leaves the frame is still repaired - to last_focused_id while that one is around.
    ctx->focused_id = 0xBADF0CU;
    focus_idle_frame(ctx);
    ASSERT_EQ_MSG(fl_focused_id(), first_id, "a vanished focused widget restores the last focused one");

    // And to the first focusable widget when the last focused one is gone too.
    ctx->focused_id = 0xBADF0CU;
    ctx->last_focused_id = 0xBADF0DU;
    focus_idle_frame(ctx);
    ASSERT_EQ_MSG(fl_focused_id(), first_id, "with no last focused widget the repair falls back to the first");

    ctx->focused_id = 0;
    ctx->last_focused_id = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.focusable_no_stays_out_of_focus_order - the focus ring is an animation, so a single frame cannot see
// it: run several and pin focused_t at 0 throughout.
//
// FlFocusable_Default is what a zeroed config carries, so if Default subtracted the flag as well, every widget
// that never named the field would silently lose keyboard access - a slider's whole arrow-key adjustment path
// is gated on signal.focused.

UTEST(render, focusable_no_stays_out_of_focus_order) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });
    ui_set_scale(1.0f, 1.0f);
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 3));

    ctx->focused_id = 0;
    ctx->last_focused_id = 0;

    // A strip's per-tab ids key off the selection pointer alone - the labels are not part of them - so a stack
    // local here would reuse the slot (and the ids, and their leftover focused_t) of the tab-bar case above.
    // A static owns an address no other case can hold.
    static int sel;
    sel = 0;
    FlString tabs[] = { FL_S("N0"), FL_S("N1"), FL_S("N2") };
    const u32 key = (u32)(uintptr_t)&sel;
    const u32 tab0_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("tab"), .id = ui_item_id_seed(key, 0u) });

    for (int frame = 0; frame < 4; frame++) {
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
            ui_tab_bar(tabs, 3, &sel, .widget.focusable = FlFocusable_No);
        }
        ui_update_end(ctx, 1.0f / 60.0f);

        ASSERT_EQ_MSG(fl_focused_id(), 0u, "an FlFocusable_No widget must never take auto-restored focus");
        ItemState* tab0 = hashmap_get(&ctx->shared->item_state, tab0_id);
        ASSERT_TRUE_MSG(tab0 != nullptr, "first tab was laid out");
        ASSERT_LT_MSG(tab0->state.focused_t, 0.001f, "its focus ring must never ramp");
    }

    // Same strip, field never named: Default must leave the family's focus behaviour alone. Auto-restore only
    // repairs a vanished widget, so the stale id is what makes it run and land on the strip's first entry.
    ctx->focused_id = 0xBADF0CU;
    ctx->last_focused_id = 0;
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_tab_bar(tabs, 3, &sel);
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    ASSERT_EQ_MSG(fl_focused_id(), tab0_id, "a zeroed config (FlFocusable_Default) keeps the tab order it had");

    // And the same for the widgets whose keyboard interaction dies with focus: a zero-config button and slider
    // must still register, or FlFocusable_Default has quietly become an opt-out.
    ctx->focused_id = 0xBADF0CU;
    ctx->last_focused_id = 0;
    static float slider_value;
    slider_value = 0.5f;
    const u32 button_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("def_button"), .id = 0 });

    // Read the slider's resolved flags inside the frame: ui_update_end prunes every ItemState the frame did not
    // touch (remove_deleted_items), so a lookup after it races the sweep.
    FlItemFlags slider_flags = 0;
    bool slider_found = false;

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_button(FL_S("def_button"));
        ui_slider_float(FL_S("def_slider"), &slider_value, 0.0f, 1.0f);
        ItemState* s = hashmap_get(&ctx->shared->item_state,
                                   ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("def_slider"), .id = 0 }));
        if (s != nullptr) {
            slider_found = true;
            slider_flags = s->flags;
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    ASSERT_EQ_MSG(fl_focused_id(), button_id, "a zero-config button is still the first entry in the focus order");
    ASSERT_TRUE_MSG(slider_found, "zero-config slider registered an item state");
    ASSERT_TRUE_MSG((slider_flags & FlItemFlags_focusable) != 0,
                    "a zero-config slider keeps FlItemFlags_focusable — its arrow-key path needs signal.focused");

    // The scrollbar is the one family that reads FlFocusable_Yes, and it is the only place Yes has any effect
    // at all (everywhere else STANDARD_WIDGET_FLAGS already carries the bit).
    static float bar_value;
    bar_value = 0.0f;
    FlItemFlags bar_default_flags = 0;
    FlItemFlags bar_yes_flags = 0;
    FlItemFlags bar_no_flags = 0;
    bool bar_default_found = false;
    bool bar_yes_found = false;
    bool bar_no_found = false;

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_scrollbar(&bar_value, .max = 100.0f, .widget.id_override = FL_S("sbar_default"));
        ui_scrollbar(&bar_value, .max = 100.0f, .widget.id_override = FL_S("sbar_yes"),
                     .widget.focusable = FlFocusable_Yes);
        // The No bar is what separates == FlFocusable_Yes from a plain truthiness test: No is 2, so a
        // truthiness test would opt this one IN.
        ui_scrollbar(&bar_value, .max = 100.0f, .widget.id_override = FL_S("sbar_no"),
                     .widget.focusable = FlFocusable_No);
        ItemState* d = hashmap_get(&ctx->shared->item_state,
                                   ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("sbar_default"), .id = 0 }));
        ItemState* y = hashmap_get(&ctx->shared->item_state,
                                   ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("sbar_yes"), .id = 0 }));
        ItemState* n = hashmap_get(&ctx->shared->item_state,
                                   ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("sbar_no"), .id = 0 }));
        if (d != nullptr) {
            bar_default_found = true;
            bar_default_flags = d->flags;
        }
        if (y != nullptr) {
            bar_yes_found = true;
            bar_yes_flags = y->flags;
        }
        if (n != nullptr) {
            bar_no_found = true;
            bar_no_flags = n->flags;
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_TRUE_MSG(bar_default_found && bar_yes_found && bar_no_found,
                    "all three scrollbars registered an item state");
    ASSERT_TRUE_MSG((bar_default_flags & FlItemFlags_focusable) == 0,
                    "a zero-config scrollbar stays out of the tab order — it is pointer-driven by default");
    ASSERT_TRUE_MSG((bar_yes_flags & FlItemFlags_focusable) != 0,
                    "FlFocusable_Yes opts the scrollbar in, the one place Yes changes anything");
    ASSERT_TRUE_MSG((bar_no_flags & FlItemFlags_focusable) == 0,
                    "FlFocusable_No leaves the scrollbar out — a truthiness test would opt it in, No being 2");

    ctx->focused_id = 0;
    ctx->last_focused_id = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render.focusable_default_keeps_tab_bar_arrow_nav - the strip gates its OWN arrow-key handling on
// cfg.widget.focusable (ui_tab_bar.c), separately from the focus_order gate in ui_item_behavior, so a zeroed
// config has to reach it as "navigable" too.

UTEST(render, focusable_default_keeps_tab_bar_arrow_nav) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });
    ui_set_scale(1.0f, 1.0f);
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 3));

    // Static for the same reason as the case above: per-tab ids key off this pointer alone.
    static int sel;
    sel = 1;
    FlString tabs[] = { FL_S("D0"), FL_S("D1"), FL_S("D2") };
    const u32 key = (u32)(uintptr_t)&sel;
    const u32 tab1_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("tab"), .id = ui_item_id_seed(key, 1u) });
    const u32 tab2_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("tab"), .id = ui_item_id_seed(key, 2u) });

    // Frame 1 lays the strip out so the per-tab ItemStates exist - with the field never named.
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_tab_bar(tabs, 3, &sel);
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ItemState* item1 = hashmap_get(&ctx->shared->item_state, tab1_id);
    ASSERT_TRUE_MSG(item1 != nullptr && item1->state.size.x > 0.0f, "middle tab was laid out");

    // Observe the CONSUME, not just the resulting focus. Core spatial navigation would also walk tab1 -> tab2
    // on nav-right - they are adjacent - so asserting the focus landed there passes whether or not the strip
    // handled it, and cannot see this gate at all. Only the strip zeroes nav_frame.nav_right (to stop spatial
    // nav double-acting), and it does so before ui_update_end reads it, so sampling the flag inside the frame
    // is what distinguishes the two.
    bool consumed_by_strip = false;
    ctx->focused_id = tab1_id;
    item1->state.focused_t = 1.0f;
    ui_update_begin(ctx);
    ctx->nav_frame.nav_right = true; // set after begin; the strip consumes it before core spatial nav
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_tab_bar(tabs, 3, &sel);
        consumed_by_strip = !ctx->nav_frame.nav_right;
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    ASSERT_TRUE_MSG(consumed_by_strip, "a zero-config strip handles nav-right itself and consumes it");
    ASSERT_EQ_MSG(fl_focused_id(), tab2_id, "a zero-config strip still moves focus on nav-right");

    ctx->focused_id = 0;
    ctx->last_focused_id = 0;
    ctx->nav_frame.nav_right = false;
    ctx->nav_frame.nav_left = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Raster premultiply: an opaque pixel keeps its channels in both the SIMD (16-pixel blocks) and scalar tails, and
// both paths round c * a / 255 to nearest.

UTEST(image_decode, premultiply_keeps_opaque_and_rounds_in_simd_and_scalar_paths) {
    enum { WIDTH = 20 }; // 16 pixels through the SIMD block, 4 through the scalar tail
    u8 pixels[WIDTH * 4];
    for (int i = 0; i < WIDTH; i++) {
        const bool opaque = (i % 2) == 0;
        pixels[i * 4 + 0] = 255;
        pixels[i * 4 + 1] = (u8)(100 + i);
        pixels[i * 4 + 2] = 1;
        pixels[i * 4 + 3] = opaque ? 255 : 128;
    }

    FlImageDecodeResult result = {
        .pixels = pixels,
        .width = WIDTH,
        .height = 1,
        .stride = WIDTH * 4,
        .channels = 4,
        .needs_thumbnail_compose = true,
    };
    image_thumbnail_compose(s_arena, &result, nullptr, nullptr);
    ASSERT_TRUE(result.premultiplied);

    for (int i = 0; i < WIDTH; i++) {
        const int a = pixels[i * 4 + 3];
        const int src_g = 100 + i;
        const int want_r = (255 * a + 127) / 255;
        const int want_g = (src_g * a + 127) / 255;
        const int want_b = (1 * a + 127) / 255;
        ASSERT_EQ_MSG((int)pixels[i * 4 + 0], want_r, "R is round(c * a / 255)");
        ASSERT_EQ_MSG((int)pixels[i * 4 + 1], want_g, "G is round(c * a / 255)");
        ASSERT_EQ_MSG((int)pixels[i * 4 + 2], want_b, "B is round(c * a / 255)");
        if (a == 255) {
            ASSERT_EQ_MSG((int)pixels[i * 4 + 0], 255, "an opaque channel stays 255");
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The dedup cache maps an image to its request id. Freeing the image drops that entry, so loading the same path
// again starts a new request instead of returning the freed handle.

UTEST(image_load, free_then_reload_returns_a_working_handle) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    vfs_test_register_driver(&s_stub_vfs_plugin);
    s_stub_vfs_override_data = k_png_red;
    s_stub_vfs_override_size = sizeof(k_png_red);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub-image-reload"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();

    const FlImageLoadOptions options = { 0 };
    const FlImageHandle first = fl_image_load_mount(mount.mount, FL_S(STUB_VFS_FILE), options);
    ASSERT_NE(first, FL_IMAGE_HANDLE_INVALID);
    ASSERT_EQ_MSG(fl_image_load_mount(mount.mount, FL_S(STUB_VFS_FILE), options), first,
                  "an identical load is deduplicated while the image lives");
    ASSERT_EQ(wait_public_image_terminal(first), FlImageStatus_Ready);

    image_free((ImageHandle)first);
    const FlImageHandle second = fl_image_load_mount(mount.mount, FL_S(STUB_VFS_FILE), options);
    ASSERT_NE(second, FL_IMAGE_HANDLE_INVALID);
    ASSERT_NE_MSG(second, first, "a freed image must not be served from the dedup cache");
    ASSERT_EQ_MSG(wait_public_image_terminal(second), FlImageStatus_Ready, "the reloaded image must realize");

    image_free((ImageHandle)second);
    fl_vfs_mount_close(mount.mount);
    s_stub_vfs_override_data = NULL;
    s_stub_vfs_override_size = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The dedup key names the mount instance, not its source path: two live mounts of one source keep separate cache
// entries, and a remount of a closed source starts clean.

UTEST(image_load, mounts_of_one_source_do_not_share_dedup_entries) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    vfs_test_register_driver(&s_stub_vfs_plugin);
    s_stub_vfs_override_data = k_png_red;
    s_stub_vfs_override_size = sizeof(k_png_red);

    FlVfsMountResult a = vfs_mount(FL_S("/stub-image-twin"));
    FlVfsMountResult b = vfs_mount(FL_S("/stub-image-twin"));
    ASSERT_EQ(a.status, FlVfsMountErrorStatus_Success);
    ASSERT_EQ(b.status, FlVfsMountErrorStatus_Success);
    vfs_wait_all();

    const FlImageLoadOptions options = { 0 };
    const FlImageHandle from_a = fl_image_load_mount(a.mount, FL_S(STUB_VFS_FILE), options);
    const FlImageHandle from_b = fl_image_load_mount(b.mount, FL_S(STUB_VFS_FILE), options);
    ASSERT_NE(from_a, FL_IMAGE_HANDLE_INVALID);
    ASSERT_NE(from_b, FL_IMAGE_HANDLE_INVALID);
    ASSERT_NE_MSG(from_a, from_b, "separate mounts of one source must not share a handle");
    ASSERT_EQ(wait_public_image_terminal(from_a), FlImageStatus_Ready);
    ASSERT_EQ(wait_public_image_terminal(from_b), FlImageStatus_Ready);

    image_free((ImageHandle)from_a);
    ASSERT_EQ_MSG(fl_image_load_mount(b.mount, FL_S(STUB_VFS_FILE), options), from_b,
                  "freeing one mount's image leaves the other mount's cache entry alone");
    ASSERT_EQ(fl_image_status(from_b), FlImageStatus_Ready);

    image_free((ImageHandle)from_b);
    fl_vfs_mount_close(a.mount);
    fl_vfs_mount_close(b.mount);
    s_stub_vfs_override_data = NULL;
    s_stub_vfs_override_size = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(image_load, remount_of_a_closed_source_does_not_return_a_stale_failed_entry) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    vfs_test_register_driver(&s_stub_vfs_plugin);
    s_stub_vfs_override_data = NULL; // the stub's text payload: not a decodable image

    FlVfsMountResult first = vfs_mount(FL_S("/stub-image-remount"));
    ASSERT_EQ(first.status, FlVfsMountErrorStatus_Success);
    vfs_wait_all();

    const FlImageLoadOptions options = { 0 };
    const FlImageHandle failed = fl_image_load_mount(first.mount, FL_S(STUB_VFS_FILE), options);
    ASSERT_NE(failed, FL_IMAGE_HANDLE_INVALID);
    ASSERT_EQ(wait_public_image_terminal(failed), FlImageStatus_Error);
    fl_vfs_mount_close(first.mount);

    s_stub_vfs_override_data = k_png_red;
    s_stub_vfs_override_size = sizeof(k_png_red);
    FlVfsMountResult second = vfs_mount(FL_S("/stub-image-remount"));
    ASSERT_EQ(second.status, FlVfsMountErrorStatus_Success);
    vfs_wait_all();

    const FlImageHandle loaded = fl_image_load_mount(second.mount, FL_S(STUB_VFS_FILE), options);
    ASSERT_NE(loaded, FL_IMAGE_HANDLE_INVALID);
    ASSERT_NE_MSG(loaded, failed, "a remounted source must not be served the old mount's failed entry");
    ASSERT_EQ(wait_public_image_terminal(loaded), FlImageStatus_Ready);

    image_free((ImageHandle)failed);
    image_free((ImageHandle)loaded);
    fl_vfs_mount_close(second.mount);
    s_stub_vfs_override_data = NULL;
    s_stub_vfs_override_size = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Load/forget churn on one path leaves no live entries, so the key storage must not keep growing with the cycles.

#define DEDUP_CHURN_CYCLES 2000

UTEST(image_load, dedup_key_storage_stays_bounded_under_load_free_churn) {
    fl_vfs_init(s_arena); // idempotent
    vfs_test_register_driver(&s_stub_vfs_plugin);

    FlVfsMountResult mount = vfs_mount(FL_S("/stub"));
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "stub mount should succeed");
    vfs_wait_all();

    FlRenderBackend backend = { 0 };
    ImageSystem* sys = image_system_create(s_arena, &backend);
    ASSERT_TRUE(sys != nullptr);

    const ImageDecodeOptions decode_options = { 0 };
    FlImageLoadRequestId* ids = arena_alloc_array(s_arena, FlImageLoadRequestId, DEDUP_CHURN_CYCLES);
    for (int i = 0; i < DEDUP_CHURN_CYCLES; i++) {
        bool was_cached = true;
        ids[i] = image_system_load_dedup(sys, mount.mount, FL_S(STUB_VFS_FILE), &decode_options, nullptr, nullptr,
                                         &was_cached);
        ASSERT_NE(ids[i], (FlImageLoadRequestId)FL_IMAGE_LOAD_REQUEST_ID_INVALID);
        ASSERT_FALSE_MSG(was_cached, "every cycle starts from an empty cache");
        image_system_forget_dedup(sys, ids[i]);
    }
    ASSERT_LT_MSG(image_system_dedup_key_bytes(sys), 32768u, "removed keys must be reclaimed, not retained");

    vfs_wait_all();
    for (int i = 0; i < DEDUP_CHURN_CYCLES; i++) {
        fl_vfs_close(ids[i]);
    }
    image_system_destroy(sys);
    fl_vfs_mount_close(mount.mount);
}
