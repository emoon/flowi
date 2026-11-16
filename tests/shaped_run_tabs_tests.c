#include "kb_route_fixture.h"
#include <ui/painter.h>
#include <ui/color.h>
#include <ui/render_ir.h>

#include "fixtures/fonts/fixed_pitch_test_fonts.h"

#define Q4 (13 * 4)

static i64 advance(FlFontCache* cache, FlFontId font, u32 codepoint) {
    return font_cache_get_glyph_advance_q4(cache, font, Q4, codepoint);
}

static FlString text_of(const char* chars) {
    return (FlString) { .data = chars, .length = strlen(chars), .is_static = 1 };
}

UTEST(shaped_run_tabs, a_tab_moves_the_pen_to_the_next_stop) {
    KbRoute route = kb_route_open();
    FlFontId mono = font_cache_load_font_memory(route.cache, k_fixed_pitch_test_ttf, k_fixed_pitch_test_ttf_size, 16);
    const i64 cell = advance(route.cache, mono, 'a');

    const FlShapedRun one = font_cache_shape_line(route.cache, mono, Q4, (TabbedLine) { text_of("a\tb"), 4.0f });
    ASSERT_EQ((int)one.count, 3);
    ASSERT_TRUE(one.glyphs[2].x == 4 * cell);
    ASSERT_TRUE(one.width == 5 * cell);

    const FlShapedRun on_stop = font_cache_shape_line(route.cache, mono, Q4, (TabbedLine) { text_of("abcd\te"), 4.0f });
    ASSERT_TRUE(on_stop.glyphs[5].x == 8 * cell);

    const FlShapedRun two = font_cache_shape_line(route.cache, mono, Q4, (TabbedLine) { text_of("\t\tb"), 2.0f });
    ASSERT_TRUE(two.glyphs[2].x == 4 * cell);

    kb_route_close(&route);
}

UTEST(shaped_run_tabs, a_tab_glyph_draws_as_a_space_and_keeps_its_byte_offset) {
    KbRoute route = kb_route_open();
    FlFontId mono = font_cache_load_font_memory(route.cache, k_fixed_pitch_test_ttf, k_fixed_pitch_test_ttf_size, 16);
    const FontGlyphRef space = font_cache_resolve_glyph(route.cache, mono, ' ');

    const FlShapedRun run = font_cache_shape_line(route.cache, mono, Q4, (TabbedLine) { text_of("a\tb"), 4.0f });
    ASSERT_EQ((int)run.glyphs[1].glyph_index, (int)space.glyph_index);
    ASSERT_EQ((int)run.glyphs[1].byte_offset, 1);
    ASSERT_EQ((int)run.glyphs[2].byte_offset, 2);

    kb_route_close(&route);
}

UTEST(shaped_run_tabs, no_tab_width_leaves_the_shaped_run_alone) {
    KbRoute route = kb_route_open();
    FlFontId mono = font_cache_load_font_memory(route.cache, k_fixed_pitch_test_ttf, k_fixed_pitch_test_ttf_size, 16);
    const FlString text = text_of("a\tb");

    const FlShapedRun plain = font_cache_shape_run(route.cache, mono, Q4, text);
    const i64 plain_width = plain.width;
    const FlShapedRun line = font_cache_shape_line(route.cache, mono, Q4, (TabbedLine) { text, 0.0f });
    ASSERT_TRUE(line.width == plain_width);

    kb_route_close(&route);
}

UTEST(shaped_run_tabs, measure_follows_the_tab_stops) {
    KbRoute route = kb_route_open();
    FlFontId mono = font_cache_load_font_memory(route.cache, k_fixed_pitch_test_ttf, k_fixed_pitch_test_ttf_size, 16);
    const float cell = (float)advance(route.cache, mono, 'a') / 64.0f;
    const FlTextStyle style = { .font_id = mono, .font_size = 13.0f, .tab_width = 4.0f };

    ASSERT_EQ(font_cache_measure_text(route.cache, text_of("a\tb"), style).width, 5.0f * cell);

    const FlTextStyle spaced = { .font_id = mono, .font_size = 13.0f, .tab_width = 4.0f, .letter_spacing = 1.0f };
    ASSERT_EQ(font_cache_measure_text(route.cache, text_of("a\tb"), spaced).width, 5.0f * cell + 2.0f);

    kb_route_close(&route);
}

UTEST(shaped_run_tabs, proportional_text_lands_glyphs_after_a_tab_on_a_stop) {
    KbRoute route = kb_route_open();
    const i64 stop = 3 * advance(route.cache, route.roboto, ' ');

    const FlShapedRun run = font_cache_shape_line(route.cache, route.roboto, Q4, (TabbedLine) { text_of("ii\tj"), 3.0f });
    ASSERT_EQ((int)run.count, 4);
    ASSERT_TRUE(run.glyphs[3].x % stop == 0);
    ASSERT_TRUE(run.glyphs[3].x > run.glyphs[1].x);

    kb_route_close(&route);
}

UTEST(shaped_run_tabs, painter_draws_tabs_on_the_stops_that_measure_reports) {
    FlArena* arena = arena_new();
    FlFontCache* cache = font_cache_new(arena);
    FlFontId mono = font_cache_load_font_memory(cache, k_fixed_pitch_test_ttf, k_fixed_pitch_test_ttf_size, 16);
    ASSERT_TRUE(mono != FONT_ID_INVALID);

    Painter* p = painter_create(arena);
    painter_begin_frame(
        p, (PainterFrame) { .font_cache = cache, .ui_scale = 1.0f, .text_scale = 1.0f, .state_arena = arena });

    const FlTextStyle style = { .font_id = mono, .font_size = 16.0f, .tab_width = 4.0f };
    void* slot = nullptr;
    painter_scope_push(p, &slot);
    painter_text_styled(p, 0.0f, 0.0f, FL_S("a\tb"), style, ui_color(255, 255, 255, 255));
    painter_scope_pop(p);

    const FlPaintSpan* span = (const FlPaintSpan*)slot;
    ASSERT_TRUE(span != nullptr && span->first != nullptr);
    const FlCmdHeader* h = (const FlCmdHeader*)span->first->ir;
    const FlCmdTextRun* run = (const FlCmdTextRun*)(h + 1);
    ASSERT_EQ((int)run->glyph_count, 3);
    const FlGlyph* glyphs = (const FlGlyph*)(run + 1);
    const float cell = glyphs[1].x - glyphs[0].x;
    ASSERT_NEAR_MSG(glyphs[2].x - glyphs[0].x, 4.0f * cell, 0.0001f, "b sits on the fourth tab stop");
    ASSERT_NEAR_MSG(font_cache_measure_text(cache, FL_S("a\tb"), style).width, 5.0f * cell, 0.0001f,
                    "measure matches the drawn layout");

    font_cache_destroy(cache);
    fl_arena_destroy(arena);
}
