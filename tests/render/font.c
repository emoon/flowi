// font_cache, sw_glyph_cache and fixed-pitch tests

#include "render/common.h"

// Quantize bucket boundaries.

UTEST(font_cache, quantize_buckets) {
    const IntCase cases[] = {
        { (int)font_cache_quantize_size(1), 2, "quantize(1) -> 2px (2px bucket)" },
        { (int)font_cache_quantize_size(29), 30, "quantize(29) -> 30px (2px bucket)" },
        { (int)font_cache_quantize_size(30), 32, "quantize(30) -> 32px (4px bucket)" },
        { (int)font_cache_quantize_size(59), 60, "quantize(59) -> 60px (4px bucket)" },
        { (int)font_cache_quantize_size(60), 64, "quantize(60) -> 64px (8px bucket)" },
    };
    check_int_cases(utest_result, cases, sizeof_array(cases));
}

// Quarter-pixel size keys for the fractional-size text path, round(size * 4).

UTEST(font_cache, quantize_q4_quarter_steps) {
    const IntCase cases[] = {
        { (int)font_cache_quantize_size_q4(7.5f), 30, "7.5px resolves exactly to key 30" },
        { (int)font_cache_quantize_size_q4(9.5f), 38, "9.5px resolves exactly to key 38" },
        { (int)font_cache_quantize_size_q4(10.5f), 42, "10.5px resolves exactly to key 42" },
        { (int)font_cache_quantize_size_q4(16.0f), 64, "whole px maps to size × 4" },
        { (int)font_cache_quantize_size_q4(16.1f), 64, "within a quarter step rounds to the same key" },
        { (int)font_cache_quantize_size_q4(16.25f), 65, "next quarter step gets its own key" },
        { (int)font_cache_quantize_size_q4(0.0f), 0, "zero stays zero" },
        { (int)font_cache_quantize_size_q4(-4.0f), 0, "negative clamps to zero" },
        { (int)font_cache_quantize_size_q4(1.0e9f), 65535, "huge sizes clamp to the u16 key range" },
    };
    check_int_cases(utest_result, cases, sizeof_array(cases));
}

// quantize is not idempotent at the bucket band boundaries, so a key formed by font_cache_whole_px_to_q4 is
// bucketed exactly once (29 -> 30, and not 30 -> 32 after it).
UTEST(font_cache, q4_to_whole_px_round_trip) {
    const IntCase cases[] = {
        { (int)font_cache_q4_to_whole_px(30), 8, "a 7.5px key rasterizes at the nearest whole px (8)" },
        { (int)font_cache_q4_to_whole_px(29), 7, "a 7.25px key rounds down to 7" },
        { (int)font_cache_q4_to_whole_px(0), 1, "a degenerate zero key clamps to 1px" },
        { (int)font_cache_whole_px_to_q4(29), 120, "29px buckets to 30px when the key is formed" },
        { (int)font_cache_q4_to_whole_px(font_cache_whole_px_to_q4(29)), 30,
          "the 29px round trip lands on 30 — bucketed once, not re-bucketed to 32" },
        { (int)font_cache_q4_to_whole_px(font_cache_whole_px_to_q4(59)), 60,
          "the 59px round trip lands on 60 — the 4px-band boundary is not re-bucketed to 64" },
        { (int)font_cache_q4_to_whole_px(font_cache_whole_px_to_q4(16)), 16,
          "an in-band whole px round trips unchanged" },
    };
    check_int_cases(utest_result, cases, sizeof_array(cases));
}

// Every load path allocates its face slot through one allocator. The default font needs no font data, so it can
// fill the table on its own.

UTEST(font_cache, face_slots_are_unique_and_bounded) {
    FlFontCache* cache = font_cache_new(s_arena);

    FlFontId first = font_cache_load_default_font(cache, 1);
    FlFontId second = font_cache_load_default_font(cache, 2);
    ASSERT_TRUE_MSG(first != FONT_ID_INVALID && second != FONT_ID_INVALID, "the first loads must succeed");
    ASSERT_TRUE_MSG(first != second, "each claimed slot gets its own id");
    const IntCase loaded[] = {
        { (int)font_cache_get_font_status(cache, first), (int)FontStatus_Ready,
          "the built-in font is ready on return" },
        { font_cache_get_size(cache, second), 16, "scale 2 loads at 8*scale px" },
    };
    check_int_cases(utest_result, loaded, sizeof_array(loaded));

    // 128 slots in total, of which two are taken. Fill the rest.
    for (int i = 2; i < 128; i++) {
        ASSERT_TRUE_MSG(font_cache_load_default_font(cache, 1) != FONT_ID_INVALID, "slots below the cap must load");
    }
    ASSERT_EQ_MSG((int)font_cache_load_default_font(cache, 1), (int)FONT_ID_INVALID, "the cap refuses a 129th face");

    ASSERT_EQ_MSG((int)font_cache_get_font_status(cache, first), (int)FontStatus_Ready,
                  "a refused load leaves earlier faces intact");

    font_cache_destroy(cache);
}

// Single-line measurement at fractional sizes (font_cache_measure_text, font_cache_get_font_metrics). The embedded
// glyph test font has known glyphs: 'A' is a solid square, 'o' a ring, and both advance 0.8 em.

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
    // 1600 is 25.0px in 26.6 fixed point: the font's 0.8 em design advance (25.6px at 32px) after hinting.
    ASSERT_EQ_MSG(adv, 1600, "whole-px 'A' advance is pinned to its historical value");
    FlString aa = { .data = "AA", .length = 2, .is_static = 1, .is_ascii = 1 };
    FlTextMetrics m = measure(cache, aa, font, 32.0f, 0.0f);
    const int laid_out = laid_out_advance_q4(cache, font, font_cache_whole_px_to_q4(32), 'A');
    ASSERT_NEAR_MSG(m.width, (float)(2 * laid_out) / 64.0f, 0.02f, "width = summed advances of the layout path");
    ASSERT_TRUE_MSG(m.height > 0.0f && m.ascent > 0.0f, "line metrics are filled");
    ASSERT_NEAR_MSG(m.height, m.ascent + m.descent, 2.0f, "line height tracks ascent + descent");

    FlString aa_utf8 = { .data = "AA", .length = 2, .is_static = 1 };
    FlString a = { .data = "A", .length = 1, .is_static = 1, .is_ascii = 1 };
    FlString aaa = { .data = "AAA", .length = 3, .is_static = 1, .is_ascii = 1 };
    const NearCase spacing[] = {
        { measure(cache, aa_utf8, font, 32.0f, 0.0f).width, m.width, 0.0001f,
          "utf8 iteration agrees with the ascii fast path" },
        { measure(cache, aa, font, 32.0f, 1.5f).width, m.width + 1.5f, 0.0001f,
          "two glyphs get exactly one spacing gap" },
        { measure(cache, a, font, 32.0f, 1.5f).width, measure(cache, a, font, 32.0f, 0.0f).width, 0.0001f,
          "a single glyph gets no spacing gap" },
        { measure(cache, aaa, font, 32.0f, 1.5f).width, 3.0f * ((float)laid_out / 64.0f) + 3.0f, 0.05f,
          "three glyphs get exactly two spacing gaps (n−1, no trailing gap)" },
    };
    check_near_cases(utest_result, spacing, sizeof_array(spacing));

    FlTextMetrics empty = measure(cache, string_empty(), font, 32.0f, 4.0f);
    ASSERT_NEAR_MSG(empty.width, 0.0f, 0.0001f, "empty text has zero width");
    ASSERT_NEAR_MSG(empty.height, m.height, 0.0001f, "empty text keeps the line metrics");

    FlTextMetrics q_lo = measure(cache, aa, font, 7.5f, 0.0f);
    FlTextMetrics q_hi = measure(cache, aa, font, 7.6f, 0.0f);
    ASSERT_TRUE_MSG(q_lo.width > 0.0f && q_lo.width < m.width, "7.5px measures smaller than 32px");
    ASSERT_NEAR_MSG(q_lo.width, q_hi.width, 0.0001f, "7.5px and 7.6px share one quarter-px key");
    ASSERT_NEAR_MSG(measure(cache, aa, font, 0.0f, 0.0f).width, m.width, 0.0001f, "size 0 falls back to the load size");

    FlFontMetrics fm = font_cache_get_font_metrics(cache, font, 32.0f);
    const NearCase metrics[] = {
        { fm.ascent, m.ascent, 0.0001f, "font metrics ascent matches measurement" },
        { fm.descent, m.descent, 0.0001f, "font metrics descent matches measurement" },
        { fm.line_height, m.height, 0.0001f, "font metrics line height matches measurement" },
    };
    check_near_cases(utest_result, metrics, sizeof_array(metrics));

    FlTextMetrics unknown = measure(cache, aa, 9999, 32.0f, 0.0f);
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
    FlString abc = { .data = "ABC", .length = 3, .is_static = 1, .is_ascii = 1 };
    FlTextMetrics m = measure(cache, ab, font, 0.0f, 0.0f);
    ASSERT_TRUE_MSG(m.ascent == 16.0f && m.descent == 0.0f,
                    "the built-in font's glyphs sit above the baseline (ascent is the cell, no descent)");
    const NearCase cases[] = {
        { m.width, 32.0f, 0.0001f, "two cells at 8px × scale 2" },
        { m.height, 16.0f, 0.0001f, "cell height at scale 2" },
        { measure(cache, ab, font, 0.0f, 2.0f).width, 34.0f, 0.0001f,
          "letter spacing applies to the built-in font too" },
        { measure(cache, ab, font, 12.34f, 0.0f).width, m.width, 0.0001f,
          "the built-in font ignores the requested size" },
        { measure(cache, abc, font, 0.0f, 2.0f).width, 52.0f, 0.0001f,
          "three cells plus exactly two spacing gaps (n−1)" },
        { font_cache_get_font_metrics(cache, font, 0.0f).line_height, 16.0f, 0.0001f,
          "font metrics report the cell height" },
    };
    check_near_cases(utest_result, cases, sizeof_array(cases));

    font_cache_destroy(cache);
}

// The glyph-outline service over the embedded test fonts (tests/fixtures/fonts/glyph_test_fonts.h). 'A' is a solid
// square and 'o' a ring whose counter winds the opposite way. There is one font per outline flavor (TrueType
// quadratics, CFF cubics). Light-hinted loading may move coordinates slightly, so the geometry checks are ranges.

// forbidden is the segment kind the flavor never produces and expected is the curve kind it does.
typedef struct OutlineFlavor {
    const unsigned char* data;
    long data_size;
    FlGlyphSegmentKind expected;
    FlGlyphSegmentKind forbidden;
} OutlineFlavor;

static void check_square_is_lines_only(int* utest_result, const FlGlyphOutline* outline) {
    ASSERT_TRUE_MSG(outline->segment_count >= 4, "the square has at least its 4 edges");
    for (u32 i = 0; i < outline->segment_count; i++) {
        ASSERT_EQ_MSG((int)outline->segments[i].kind, (int)FlGlyphSegment_Line, "the square is lines only");
    }
}

static void check_ring_curve_kinds(int* utest_result, const FlGlyphOutline* outline, const OutlineFlavor* flavor) {
    bool saw_expected = false;
    for (u32 i = 0; i < outline->segment_count; i++) {
        ASSERT_TRUE_MSG((int)outline->segments[i].kind != (int)flavor->forbidden,
                        "outline carries the wrong curve kind for its font flavor");
        saw_expected |= outline->segments[i].kind == flavor->expected;
    }
    ASSERT_TRUE_MSG(saw_expected, "the ring must decompose to curve segments");
}

// The edge vectors of each contour sum to zero, so the sum over all segments is zero too.
static bool outline_contours_close(const FlGlyphOutline* outline) {
    f32 sum_x = 0.0f;
    f32 sum_y = 0.0f;
    for (u32 i = 0; i < outline->segment_count; i++) {
        const FlGlyphSegment* seg = &outline->segments[i];
        int last = seg->kind == FlGlyphSegment_Line ? 1 : (seg->kind == FlGlyphSegment_Quadratic ? 2 : 3);
        sum_x += seg->pts[last].x - seg->pts[0].x;
        sum_y += seg->pts[last].y - seg->pts[0].y;
    }
    return sum_x > -0.001f && sum_x < 0.001f && sum_y > -0.001f && sum_y < 0.001f;
}

static bool point_in_control_box(const FlGlyphOutline* outline, FlVec2 pt) {
    return pt.x >= outline->min_x - 0.01f && pt.x <= outline->max_x + 0.01f && pt.y >= outline->min_y - 0.01f
           && pt.y <= outline->max_y + 0.01f;
}

static void check_points_inside_control_box(int* utest_result, const FlGlyphOutline* outline) {
    for (u32 i = 0; i < outline->segment_count; i++) {
        for (int p = 1; p < 4; p++) {
            const FlVec2 pt = outline->segments[i].pts[p];
            const bool unused_trailing_point = pt.x == 0.0f && pt.y == 0.0f;
            ASSERT_TRUE_MSG(unused_trailing_point || point_in_control_box(outline, pt),
                            "every control point sits inside the control box");
        }
        ASSERT_TRUE_MSG(point_in_control_box(outline, outline->segments[i].pts[0]),
                        "every control point sits inside the control box");
    }
}

static void check_outline_font(int* utest_result, const OutlineFlavor* flavor) {
    FlFontCache* cache = font_cache_new(s_arena);
    const u16 size = 32;
    const u16 size_q4 = font_cache_whole_px_to_q4(size); // the outline service is keyed in quarter px

    FlFontId font = font_cache_load_font_memory(cache, flavor->data, flavor->data_size, size);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "memory-loaded font must be ready synchronously");
    ASSERT_EQ_MSG((int)font_cache_get_font_status(cache, font), (int)FontStatus_Ready, "status must be Ready");

    u32 glyph_a = font_cache_get_glyph_index(cache, font, 'A');
    u32 glyph_o = font_cache_get_glyph_index(cache, font, 'o');
    ASSERT_TRUE_MSG(glyph_a != 0 && glyph_o != 0, "'A' and 'o' must map to glyphs");
    ASSERT_EQ_MSG((int)font_cache_get_glyph_index(cache, font, 'Q'), 0, "unmapped codepoints return index 0");

    FlGlyphOutline outline;
    ASSERT_TRUE_MSG(font_cache_get_glyph_outline(cache, font, size_q4, glyph_a, s_arena, &outline),
                    "'A' outline must load");
    check_square_is_lines_only(utest_result, &outline);

    ASSERT_TRUE_MSG(font_cache_get_glyph_outline(cache, font, size_q4, glyph_o, s_arena, &outline),
                    "'o' outline must load");
    check_ring_curve_kinds(utest_result, &outline, flavor);
    ASSERT_TRUE_MSG(outline_contours_close(&outline), "every contour must be closed (edge vectors telescope to zero)");

    // The design advance is 0.8 em, 25.6 px at 32 px, and the box spans 0.6 x 0.7 em.
    ASSERT_TRUE_MSG(outline.advance > 20.0f && outline.advance < 30.0f, "'o' advance lands near 0.8 em");
    ASSERT_TRUE_MSG(outline.max_x > outline.min_x && outline.max_y > outline.min_y, "'o' has a real bbox");
    check_points_inside_control_box(utest_result, &outline);

    font_cache_destroy(cache);
}

UTEST(font_cache, glyph_outline_truetype_quadratic) {
    const OutlineFlavor flavor
        = { k_glyph_test_ttf, k_glyph_test_ttf_size, FlGlyphSegment_Quadratic, FlGlyphSegment_Cubic };
    check_outline_font(utest_result, &flavor);
}

UTEST(font_cache, glyph_outline_cff_cubic) {
    const OutlineFlavor flavor
        = { k_glyph_test_cff, k_glyph_test_cff_size, FlGlyphSegment_Cubic, FlGlyphSegment_Quadratic };
    check_outline_font(utest_result, &flavor);
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
    const BoolCase smaller[] = {
        { small.advance > 0.0f && small.advance < big.advance, true, "the 7.5px advance is smaller than the 32px one" },
        { small.max_x - small.min_x < big.max_x - big.min_x, true, "the 7.5px geometry is smaller" },
    };
    check_bool_cases(utest_result, smaller, sizeof_array(smaller));
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

// The built-in 8x8 default font is generated at an integer scale. bearing_y is in pixels, like width and height,
// and positions the baseline as glyph_y = baseline_y - bearing_y. The 8x8 font fills its cell, so the whole glyph
// is above the baseline and its ascent equals the cell height (8 * scale).
UTEST(sw_glyph_cache, default_font_bearing_y_scales) {
    FlFontCache* shared = font_cache_new(s_arena);
    SwGlyphCache* sw = sw_glyph_cache_new(s_arena, shared, FontRenderMode_Grayscale);

    for (u16 scale = 1; scale <= 4; scale++) {
        FlFontId font = font_cache_load_default_font(shared, scale);
        ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "default font loads at every scale");

        // The default font is a single atlas of fixed size, and the size argument is ignored for it.
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

static FlFontId load_roboto(FlFontCache* cache, FlVfsMount** out_mount, u16 size) {
    fl_vfs_init(s_arena);
    FlVfsMountResult mount = vfs_mount(FL_S(FLOWI_TEST_DATA_DIR));
    if (mount.status != FlVfsMountErrorStatus_Success) {
        return FONT_ID_INVALID;
    }
    vfs_wait_all();
    *out_mount = mount.mount;
    FlFontId font = font_cache_load_font(cache, mount.mount, FL_S("Roboto-Regular.ttf"), size);
    font_cache_wait_loaded(cache, font);
    return font;
}

static const Glyph* wait_for_glyph(SwGlyphCache* sw, FlFontId font, u16 size, u32 glyph_index) {
    u64 deadline_ns = get_monotonic_time_ns_os() + 5000000000ull;
    const Glyph* glyph = sw_glyph_cache_get(sw, font, size, glyph_index);
    while (glyph == nullptr && get_monotonic_time_ns_os() < deadline_ns) {
        sleep_us(200);
        glyph = sw_glyph_cache_get(sw, font, size, glyph_index);
    }
    return glyph;
}

typedef struct AtlasPoll {
    SwGlyphCache* sw;
    FlFontId font;
    u32 index_a;
    u32 index_eacute;
    u16 sizes[4];
    const Glyph* glyphs[4];
} AtlasPoll;

// One poll pass: realizes the not yet ready sizes' 'A' and, as each turns ready, rasterizes a glyph outside ASCII on
// the main thread. Reports whether every size is ready.
static void poll_atlas_sizes(int* utest_result, AtlasPoll* poll, bool* out_all_ready) {
    bool all_ready = true;
    for (int i = 0; i < 4; i++) {
        if (poll->glyphs[i] != nullptr) {
            continue;
        }
        poll->glyphs[i] = sw_glyph_cache_get(poll->sw, poll->font, poll->sizes[i], poll->index_a);
        all_ready = all_ready && poll->glyphs[i] != nullptr;
        if (poll->glyphs[i] != nullptr) {
            ASSERT_TRUE_MSG(sw_glyph_cache_get(poll->sw, poll->font, poll->sizes[i], poll->index_eacute) != nullptr,
                            "a glyph outside ASCII rasterizes on demand once the atlas is ready");
        }
    }
    *out_all_ready = all_ready;
}

// Each atlas job owns a temporary FT_Library, so the cache's shared library is used only on the main thread. The
// case runs concurrent atlas jobs against the main-thread on-demand path. It is deterministic as it stands, and
// it detects a data race only under the manual TSAN build, which CI does not run.
UTEST(sw_glyph_cache, concurrent_atlas_jobs_and_on_demand) {
    FlFontCache* shared = font_cache_new(s_arena);
    SwGlyphCache* sw = sw_glyph_cache_new(s_arena, shared, FontRenderMode_Grayscale);

    AtlasPoll poll = { .sw = sw, .sizes = { 12, 16, 24, 32 } };

    FlVfsMount* mount = nullptr;
    poll.font = load_roboto(shared, &mount, poll.sizes[0]);
    ASSERT_EQ_MSG((int)font_cache_get_font_status(shared, poll.font), (int)FontStatus_Ready, "Roboto must load");
    poll.index_a = font_cache_get_glyph_index(shared, poll.font, 'A');
    poll.index_eacute = font_cache_get_glyph_index(shared, poll.font, 0x00E9);
    ASSERT_TRUE_MSG(poll.index_a != 0 && poll.index_eacute != 0, "Roboto maps 'A' and e-acute");

    // The first get for each size enqueues that size's atlas rasterization job.
    for (int i = 0; i < 4; i++) {
        sw_glyph_cache_get(sw, poll.font, poll.sizes[i], poll.index_a);
    }

    // Poll until every size's 'A' is realized.
    u64 deadline_ns = get_monotonic_time_ns_os() + 5000000000ull;
    bool all_ready = false;
    while (!all_ready) {
        poll_atlas_sizes(utest_result, &poll, &all_ready);
        ASSERT_TRUE_MSG(get_monotonic_time_ns_os() < deadline_ns,
                        "atlas rasterization jobs did not all complete within the deadline");
        if (!all_ready) {
            sleep_us(200);
        }
    }

    for (int i = 0; i < 4; i++) {
        ASSERT_TRUE_MSG(poll.glyphs[i] != nullptr, "'A' must realize at every requested size");
        ASSERT_TRUE_MSG(poll.glyphs[i]->advance_x != 0, "realized 'A' carries a nonzero advance at every size");
    }

    sw_glyph_cache_destroy(sw);
    font_cache_destroy(shared);
    fl_vfs_mount_close(mount);
}

UTEST(sw_glyph_cache, glyphs_are_keyed_by_glyph_index) {
    FlFontCache* shared = font_cache_new(s_arena);
    SwGlyphCache* sw = sw_glyph_cache_new(s_arena, shared, FontRenderMode_Grayscale);
    const u16 size = 24;

    FlVfsMount* mount = nullptr;
    FlFontId font = load_roboto(shared, &mount, size);
    ASSERT_EQ_MSG((int)font_cache_get_font_status(shared, font), (int)FontStatus_Ready, "Roboto must load");
    const u32 index_a = font_cache_get_glyph_index(shared, font, 'A');
    const u32 index_i = font_cache_get_glyph_index(shared, font, 'i');
    const u32 index_eacute = font_cache_get_glyph_index(shared, font, 0x00E9);
    ASSERT_TRUE_MSG(index_a != 'A', "the glyph index of 'A' differs from its codepoint in Roboto");

    const Glyph* a = wait_for_glyph(sw, font, size, index_a);
    ASSERT_TRUE_MSG(a != nullptr, "'A' is realized by its glyph index");
    const Glyph* i = sw_glyph_cache_get(sw, font, size, index_i);
    ASSERT_TRUE_MSG(i != nullptr, "'i' is pre-rasterized with the rest of ASCII");
    ASSERT_TRUE_MSG(a->advance_x > i->advance_x, "each index returns its own glyph: 'A' is wider than 'i'");

    const Glyph* eacute = sw_glyph_cache_get(sw, font, size, index_eacute);
    ASSERT_TRUE_MSG(eacute != nullptr, "an index outside ASCII rasterizes on demand");
    const BoolCase cached[] = {
        { eacute->advance_x > 0 && eacute->bitmap_data_r != nullptr, true, "and carries coverage" },
        { sw_glyph_cache_get(sw, font, size, index_eacute) == eacute, true, "the on-demand glyph is cached" },
        { sw_glyph_cache_get(sw, font, size, 0) == nullptr, true, "index 0 (no face maps it) draws nothing" },
        { sw_glyph_cache_get(sw, font, size, 1000000) == nullptr, true, "an index past the face is refused" },
    };
    check_bool_cases(utest_result, cached, sizeof_array(cached));

    sw_glyph_cache_destroy(sw);
    font_cache_destroy(shared);
    fl_vfs_mount_close(mount);
}

UTEST(sw_glyph_cache, fallback_face_glyph_is_ready_on_first_get) {
    FlFontCache* shared = font_cache_new(s_arena);
    SwGlyphCache* sw = sw_glyph_cache_new(s_arena, shared, FontRenderMode_Grayscale);
    const u16 size = 32;

    FlFontId primary = font_cache_load_font_memory(shared, k_glyph_test_ttf, k_glyph_test_ttf_size, size);
    FlFontId fallback = font_cache_load_font_memory(shared, k_fallback_test_ttf, k_fallback_test_ttf_size, size);
    ASSERT_TRUE_MSG(primary != FONT_ID_INVALID && fallback != FONT_ID_INVALID, "both test fonts must load");
    font_cache_set_fallback_font(shared, fallback);

    FontGlyphRef b = font_cache_resolve_glyph(shared, primary, 'B');
    ASSERT_EQ_MSG((int)b.font, (int)fallback, "'B' resolves to the fallback face");

    const Glyph* glyph = sw_glyph_cache_get(sw, b.font, size, b.glyph_index);
    ASSERT_TRUE_MSG(glyph != nullptr, "the fallback face realizes 'B' by its own index without waiting for a job");
    ASSERT_TRUE_MSG(glyph->advance_x > 0 && glyph->bitmap_data_r != nullptr, "and it carries coverage");

    sw_glyph_cache_destroy(sw);
    font_cache_destroy(shared);
}

// The primary font (glyph_test_fonts.h) maps only 'A' and 'o'. The fallback font (fallback_test_font.h) maps 'B'.
UTEST(font_cache, text_size_includes_fallback_advance) {
    FlFontCache* cache = font_cache_new(s_arena);
    const u16 size = 32;

    FlFontId primary = font_cache_load_font_memory(cache, k_glyph_test_ttf, k_glyph_test_ttf_size, size);
    ASSERT_TRUE_MSG(primary != FONT_ID_INVALID, "primary font must load");
    ASSERT_EQ_MSG((int)font_cache_get_glyph_index(cache, primary, 'B'), 0, "'B' is absent from the primary face");

    const FlString b = FL_S("B");

    ASSERT_EQ_MSG(font_cache_get_text_size(cache, b, primary, size).x, 0.0f,
                  "no fallback -> missing codepoint measures zero width");
    ASSERT_EQ_MSG(measure(cache, b, primary, 32.0f, 0.0f).width, 0.0f,
                  "no fallback -> the fractional path measures zero width too");

    FlFontId fallback = font_cache_load_font_memory(cache, k_fallback_test_ttf, k_fallback_test_ttf_size, size);
    ASSERT_TRUE_MSG(fallback != FONT_ID_INVALID, "fallback font must load");
    font_cache_set_fallback_font(cache, fallback);

    int advance = laid_out_advance_q4(cache, primary, font_cache_whole_px_to_q4(size), 'B');
    ASSERT_TRUE_MSG(advance > 0, "fallback supplies a nonzero advance for 'B'");
    ASSERT_EQ_MSG(font_cache_get_text_size(cache, b, primary, size).x, (float)advance / 64.0f,
                  "measured width equals the per-codepoint laid-out advance (fallback included)");
    ASSERT_NEAR_MSG(measure(cache, b, primary, 32.0f, 0.0f).width, (float)advance / 64.0f, 0.0001f,
                    "the fractional path picks up the fallback advance too");

    font_cache_destroy(cache);
}

UTEST(font_cache, text_layout_splits_runs_per_face) {
    FlFontCache* cache = font_cache_new(s_arena);
    const u16 size = 32;

    FlFontId primary = font_cache_load_font_memory(cache, k_glyph_test_ttf, k_glyph_test_ttf_size, size);
    FlFontId fallback = font_cache_load_font_memory(cache, k_fallback_test_ttf, k_fallback_test_ttf_size, size);
    ASSERT_TRUE_MSG(primary != FONT_ID_INVALID && fallback != FONT_ID_INVALID, "both test fonts must load");
    font_cache_set_fallback_font(cache, fallback);

    FlArena* ir = arena_new();
    void* start = fl_arena_current_pos(ir);
    FlString text = { .data = "AAB", .length = 3, .is_static = 1, .is_ascii = 1 };
    const u16 q4 = font_cache_whole_px_to_q4(size);
    ir_text_layout(ir, cache, primary, q4, 1.0f, (FlColor) { 255, 255, 255, 255 }, text, 0.0f, 0.0f, 0.0f);

    const FlCmdHeader* h0 = (const FlCmdHeader*)start;
    const FlCmdTextRun* run0 = (const FlCmdTextRun*)(h0 + 1);
    const FlGlyph* g0 = (const FlGlyph*)(run0 + 1);
    const IntCase first_run[] = {
        { (int)run0->font, (int)primary, "the first run is drawn by the font asked for" },
        { (int)run0->glyph_count, 2, "it holds both glyphs the font maps" },
        { (int)g0[0].glyph_index, (int)font_cache_get_glyph_index(cache, primary, 'A'),
          "'A' carries the primary face's index" },
    };
    check_int_cases(utest_result, first_run, sizeof_array(first_run));

    const FlCmdHeader* h1 = (const FlCmdHeader*)((const u8*)h0 + h0->byte_size);
    const FlCmdTextRun* run1 = (const FlCmdTextRun*)(h1 + 1);
    const FlGlyph* g1 = (const FlGlyph*)(run1 + 1);
    const IntCase second_run[] = {
        { (int)h1->kind, (int)FlCmd_TextRun, "the glyph the font lacks gets a run of its own" },
        { (int)run1->font, (int)fallback, "that run is drawn by the fallback face" },
        { (int)run1->size, (int)q4, "at the same size" },
        { (int)run1->glyph_count, 1, "and holds only the fallback glyph" },
        { (int)g1[0].glyph_index, (int)font_cache_get_glyph_index(cache, fallback, 'B'),
          "'B' carries the fallback face's index" },
    };
    check_int_cases(utest_result, second_run, sizeof_array(second_run));

    const float adv = (float)laid_out_advance_q4(cache, primary, q4, 'A') / 64.0f;
    ASSERT_NEAR_MSG(g1[0].x, 2.0f * adv, 0.02f, "the fallback glyph sits where the pen left off");

    fl_arena_destroy(ir);
    font_cache_destroy(cache);
}

UTEST(font_cache, default_font_shapes_to_codepoints) {
    FlFontCache* cache = font_cache_new(s_arena);
    FlFontId builtin = font_cache_load_default_font(cache, 2);
    FlFontId fallback = font_cache_load_font_memory(cache, k_fallback_test_ttf, k_fallback_test_ttf_size, 32);
    font_cache_set_fallback_font(cache, fallback);

    FlString text = { .data = "B", .length = 1, .is_static = 1, .is_ascii = 1 };
    FlShapedRun run = font_cache_shape_run(cache, builtin, 0, text);
    ASSERT_EQ_MSG((int)run.count, 1, "one glyph");
    const IntCase glyph[] = {
        { (int)run.glyphs[0].font, (int)builtin, "the built-in font draws its own ASCII" },
        { (int)run.glyphs[0].glyph_index, (int)'B', "its glyph index is the codepoint" },
        { run.glyphs[0].x, 0, "the first glyph starts the run" },
        { run.width == 16 * 64, true, "one 8 px cell at scale 2" },
    };
    check_int_cases(utest_result, glyph, sizeof_array(glyph));

    font_cache_destroy(cache);
}

static i64 summed_advances_q4(FlFontCache* cache, FlFontId font, u16 q4, FlString bytes) {
    i64 sum = 0;
    for_count(i, bytes.length) {
        sum += font_cache_get_glyph_advance_q4(cache, font, q4, (unsigned char)bytes.data[i]);
    }
    return sum;
}

static float spaced_width(i64 advances, size_t count, float spacing) {
    float width = (float)advances / 64.0f;
    if (count > 1) {
        width += spacing * (float)(count - 1);
    }
    return width;
}

UTEST(font_cache, fixed_pitch_is_detected_from_advances) {
    FlFontCache* cache = font_cache_new(s_arena);

    FlFontId mono = font_cache_load_font_memory(cache, k_fixed_pitch_test_ttf, k_fixed_pitch_test_ttf_size, 16);
    FlFontId uneven = font_cache_load_font_memory(cache, k_uneven_pitch_test_ttf, k_uneven_pitch_test_ttf_size, 16);
    FlFontId partial = font_cache_load_font_memory(cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 16);
    FlFontId builtin = font_cache_load_default_font(cache, 1);

    const BoolCase detected[] = {
        { font_cache_is_fixed_pitch(cache, mono), true, "every printable ASCII glyph shares one advance" },
        { font_cache_is_fixed_pitch(cache, uneven), false, "one narrower glyph makes the face proportional" },
        { font_cache_is_fixed_pitch(cache, partial), false, "a face that lacks printable ASCII is not fixed pitch" },
        { font_cache_is_fixed_pitch(cache, builtin), true, "the built-in font is fixed pitch" },
        { font_cache_is_fixed_pitch(cache, 9999), false, "an unknown font is not fixed pitch" },
    };
    check_bool_cases(utest_result, detected, sizeof_array(detected));

    font_cache_destroy(cache);
}

UTEST(font_cache, fixed_pitch_measure_matches_summed_advances) {
    const FlString line = FL_S("static int measure(const char* text, int length) { return length * advance; } ~~~");
    const float sizes[] = { 7.5f, 9.5f, 12.0f, 13.0f, 16.0f, 22.25f, 31.0f };
    const float spacings[] = { 0.0f, 1.25f };

    FlFontCache* cache = font_cache_new(s_arena);
    FlFontId font = font_cache_load_font_memory(cache, k_fixed_pitch_test_ttf, k_fixed_pitch_test_ttf_size, 16);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "the test font must load");

    for_count(i, 7) {
        const u16 q4 = font_cache_quantize_size_q4(sizes[i]);
        const float cold = measure(cache, line, font, sizes[i], 0.0f).width;
        const i64 sum = summed_advances_q4(cache, font, q4, line);
        ASSERT_TRUE_MSG(sum > 0, "the line has a width");
        ASSERT_TRUE_MSG(cold == spaced_width(sum, line.length, 0.0f), "a first measure equals the summed advances");
        for_count(s, 2) {
            ASSERT_TRUE_MSG(measure(cache, line, font, sizes[i], spacings[s]).width
                                == spaced_width(sum, line.length, spacings[s]),
                            "measuring equals the summed advances plus n-1 gaps");
        }
        ASSERT_TRUE_MSG(font_cache_shape_run(cache, font, q4, line).width == sum, "shaping equals the summed advances");
    }

    for (u16 px = 8; px <= 64; px += 7) {
        const float measured = font_cache_get_text_size(cache, line, font, px).x;
        i64 sum = 0;
        for_count(i, line.length) {
            sum += font_cache_get_glyph_advance(cache, font, px, (unsigned char)line.data[i]);
        }
        ASSERT_TRUE_MSG(measured == (float)sum / 64.0f, "the whole-px measure equals the summed advances");
    }

    font_cache_destroy(cache);
}

UTEST(font_cache, fixed_pitch_measure_keeps_control_and_fallback_advances) {
    FlFontCache* cache = font_cache_new(s_arena);
    const float size = 16.0f;
    const u16 q4 = font_cache_quantize_size_q4(size);

    FlFontId mono = font_cache_load_font_memory(cache, k_fixed_pitch_test_ttf, k_fixed_pitch_test_ttf_size, 16);
    FlFontId uneven = font_cache_load_font_memory(cache, k_uneven_pitch_test_ttf, k_uneven_pitch_test_ttf_size, 16);
    font_cache_set_fallback_font(cache, uneven);

    const int cell = font_cache_get_glyph_advance_q4(cache, mono, q4, 'a');
    const int tab = font_cache_get_glyph_advance_q4(cache, mono, q4, '\t');
    ASSERT_TRUE_MSG(cell > 0 && tab > cell, "the tab comes from the wider fallback glyph");
    ASSERT_EQ_MSG(font_cache_get_glyph_advance_q4(cache, mono, q4, '\n'), 0, "no face maps the newline");

    const FlString tabbed = FL_S("a\tb");
    ASSERT_TRUE_MSG(measure(cache, tabbed, mono, size, 0.0f).width == (float)(2 * cell + tab) / 64.0f,
                    "a control character the fallback maps keeps the fallback advance");
    ASSERT_TRUE_MSG(font_cache_get_text_size(cache, tabbed, mono, 16).x == (float)(2 * cell + tab) / 64.0f,
                    "on the whole-px path too");

    const FlString controls = FL_S("a\nb\x7f");
    ASSERT_TRUE_MSG(measure(cache, controls, mono, size, 2.0f).width == spaced_width(2 * cell, 4, 2.0f),
                    "unmapped control characters advance zero and still count for spacing");

    const FlString accented = { .data = "A\xC3\xA9", .length = 3, .is_static = 1 };
    const int e_acute = font_cache_get_glyph_advance_q4(cache, mono, q4, 0xE9);
    ASSERT_TRUE_MSG(e_acute > cell, "the accented glyph is wider than the cell");
    ASSERT_TRUE_MSG(measure(cache, accented, mono, size, 2.0f).width == spaced_width(cell + e_acute, 2, 2.0f),
                    "a non-ASCII string is measured per codepoint");

    const FlString mislabelled = { .data = "A\xC3\xA9", .length = 3, .is_static = 1, .is_ascii = 1 };
    ASSERT_TRUE_MSG(measure(cache, mislabelled, mono, size, 0.0f).width
                        == (float)summed_advances_q4(cache, mono, q4, mislabelled) / 64.0f,
                    "bytes above ASCII in a string flagged ASCII measure as before");

    ASSERT_EQ_MSG(measure(cache, (FlString) { .is_ascii = 1 }, mono, size, 2.0f).width, 0.0f,
                  "an empty string has no width");
    ASSERT_TRUE_MSG(measure(cache, FL_S("a"), mono, size, 2.0f).width == (float)cell / 64.0f,
                    "one glyph has no spacing gap");

    font_cache_destroy(cache);
}

UTEST(font_cache, fixed_pitch_measure_on_partial_and_builtin_faces) {
    FlFontCache* cache = font_cache_new(s_arena);
    const float size = 12.0f;
    const u16 q4 = font_cache_quantize_size_q4(size);

    FlFontId partial = font_cache_load_font_memory(cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    const int cell = laid_out_advance_q4(cache, partial, q4, 'A');
    ASSERT_EQ_MSG(laid_out_advance_q4(cache, partial, q4, 'o'), cell, "both glyphs share the advance here");
    ASSERT_NEAR_MSG(measure(cache, FL_S("AoAo"), partial, size, 0.0f).width, (float)(4 * cell) / 64.0f, 0.05f,
                    "mapped glyphs measure as cells");
    ASSERT_NEAR_MSG(measure(cache, FL_S("AoB"), partial, size, 0.0f).width, (float)(2 * cell) / 64.0f, 0.05f,
                    "a glyph the face lacks advances zero");

    FlFontId fallback = font_cache_load_font_memory(cache, k_fallback_test_ttf, k_fallback_test_ttf_size, 32);
    font_cache_set_fallback_font(cache, fallback);
    const int b = laid_out_advance_q4(cache, partial, q4, 'B');
    ASSERT_TRUE_MSG(b > 0, "the fallback maps 'B'");
    ASSERT_NEAR_MSG(measure(cache, FL_S("AoB"), partial, size, 0.0f).width, (float)(2 * cell + b) / 64.0f, 0.05f,
                    "a fallback glyph keeps its own advance");

    FlFontId builtin = font_cache_load_default_font(cache, 2);
    ASSERT_EQ_MSG(measure(cache, FL_S("a\tb"), builtin, 0.0f, 0.0f).width, 48.0f,
                  "every ASCII code is a cell in the built-in font");
    const FlString mislabelled = { .data = "A\xC3\xA9", .length = 3, .is_static = 1, .is_ascii = 1 };
    ASSERT_EQ_MSG(measure(cache, mislabelled, builtin, 0.0f, 0.0f).width, 16.0f,
                  "bytes above ASCII advance zero in the built-in font");
    ASSERT_EQ_MSG(font_cache_get_text_size(cache, FL_S("abc"), builtin, 16).x, 48.0f, "on the whole-px path too");

    font_cache_destroy(cache);
}

// Every backend resolves glyphs through font_cache_resolve_glyph before preparing outlines. A resolved glyph index
// is valid only for the face it resolved to.
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

// The opt-in 1080p scale-to-fit policy (ui/reference_fit.h). ui_reference_fit(height) gives the numbers the core
// computes in ui_apply_scale_preset: identity at the 1080p reference, and half scale with the PAL text boost at
// 540p.

UTEST(reference_fit, reproduces_reference_and_pal_numbers) {
    UiScalePair ref = ui_reference_fit(1080);
    EXPECT_EQ(ref.ui_scale, 1.0f);
    EXPECT_EQ(ref.text_scale, 1.0f);

    // At 540p: half UI scale, and text at 0.5 * 1.10 = 0.55 with the SD/PAL text boost (1.10).
    UiScalePair pal = ui_reference_fit(540);
    EXPECT_EQ(pal.ui_scale, 0.5f);
    EXPECT_EQ(pal.text_scale, 0.55f);

    // The helper returns the core's preset computation as a pair.
    float ui_scale, text_factor, text_scale;
    ui_apply_scale_preset(&ui_scale, &text_factor, &text_scale, 540);
    EXPECT_EQ(pal.ui_scale, ui_scale);
    EXPECT_EQ(pal.text_scale, text_scale);
}

// font_cache_resolve_size_px is the one place that implements "size <= 0 means the face's load size" and applies
// the caller's type scale. It also refuses a scale of 0, which on a draw path would quantize to a zero key and
// drop the run without a trace.
UTEST(font_cache, resolve_size_px_applies_the_scale_over_the_zero_fallback) {
    FlFontCache* cache = font_cache_new(s_arena);
    FlFontId font = font_cache_load_font_memory(cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "memory-loaded font must be ready synchronously");

    const NearCase resolved[] = {
        { font_cache_resolve_size_px(cache, font, 12.0f, 1.0f), 12.0f, 0.0001f,
          "an explicit size at identity scale is itself" },
        { font_cache_resolve_size_px(cache, font, 12.0f, 2.5f), 30.0f, 0.0001f,
          "an explicit size is multiplied by the scale" },
        { font_cache_resolve_size_px(cache, font, 0.0f, 1.0f), 32.0f, 0.0001f,
          "size 0 falls back to the face's authored load size" },
        { font_cache_resolve_size_px(cache, font, 0.0f, 2.0f), 64.0f, 0.0001f,
          "the load-size fallback is scaled too, which is what stops a font freezing at load scale" },
        { font_cache_resolve_size_px(cache, font, -1.0f, 2.0f), 64.0f, 0.0001f,
          "a negative size takes the same fallback as 0" },
    };
    check_near_cases(utest_result, resolved, sizeof_array(resolved));

    const NearCase refused[] = {
        { font_cache_resolve_size_px(cache, font, 12.0f, 0.0f), 0.0, 0.0, "a zero scale resolves to nothing" },
        { font_cache_resolve_size_px(cache, font, 12.0f, -2.0f), 0.0, 0.0, "so does a negative one" },
        { font_cache_resolve_size_px(cache, 9999, 12.0f, 2.0f), 0.0, 0.0, "an unknown font resolves to nothing" },
    };
    check_near_cases(utest_result, refused, sizeof_array(refused));

    font_cache_destroy(cache);
}
