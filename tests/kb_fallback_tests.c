#include "kb_route_fixture.h"
#include <ui/render_ir_build.h>

#include "fixtures/fonts/fallback_test_font.h"
#include "fixtures/fonts/glyph_test_fonts.h"

typedef struct FallbackFonts {
    FlFontId primary;
    FlFontId fallback;
} FallbackFonts;

static FallbackFonts load_fallback_fonts(FlFontCache* cache) {
    return (FallbackFonts) {
        .primary = font_cache_load_font_memory(cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 32),
        .fallback = font_cache_load_font_memory(cache, k_fallback_test_ttf, k_fallback_test_ttf_size, 32),
    };
}

UTEST(kb_fallback, glyph_only_the_fallback_maps_comes_from_the_fallback) {
    KbRoute route = kb_route_open();
    const FallbackFonts fonts = load_fallback_fonts(route.cache);
    font_cache_set_fallback_font(route.cache, fonts.fallback);
    ASSERT_FALSE(font_cache_is_fixed_pitch(route.cache, fonts.primary));

    const FlShapedRun run = font_cache_shape_run(route.cache, fonts.primary, 32 * 4, FL_S("AoB"));
    ASSERT_EQ((int)run.count, 3);
    ASSERT_EQ((int)run.glyphs[0].font, (int)fonts.primary);
    ASSERT_EQ((int)run.glyphs[1].font, (int)fonts.primary);
    ASSERT_EQ((int)run.glyphs[2].font, (int)fonts.fallback);
    ASSERT_EQ((int)run.glyphs[2].glyph_index, (int)font_cache_get_glyph_index(route.cache, fonts.fallback, 'B'));
    ASSERT_LT((i64)run.glyphs[2].x, run.width);

    kb_route_close(&route);
}

UTEST(kb_fallback, fallback_glyph_gets_its_own_text_run) {
    KbRoute route = kb_route_open();
    const FallbackFonts fonts = load_fallback_fonts(route.cache);
    font_cache_set_fallback_font(route.cache, fonts.fallback);

    FlArena* ir = arena_new();
    const FlCmdHeader* first = fl_arena_current_pos(ir);
    ir_text_layout(ir, route.cache, fonts.primary, 32 * 4, 1.0f, (FlColor) { 255, 255, 255, 255 }, FL_S("AoB"), 0.0f,
                      0.0f, 0.0f);
    const FlCmdTextRun* primary_run = (const FlCmdTextRun*)(first + 1);
    const FlCmdHeader* second = (const FlCmdHeader*)((const u8*)first + first->byte_size);
    const FlCmdTextRun* fallback_run = (const FlCmdTextRun*)(second + 1);

    ASSERT_EQ((int)primary_run->font, (int)fonts.primary);
    ASSERT_EQ((int)primary_run->glyph_count, 2);
    ASSERT_EQ((int)fallback_run->font, (int)fonts.fallback);
    ASSERT_EQ((int)fallback_run->glyph_count, 1);

    fl_arena_destroy(ir);
    kb_route_close(&route);
}

UTEST(kb_fallback, unmapped_glyph_without_a_fallback_advances_zero) {
    KbRoute route = kb_route_open();
    const FallbackFonts fonts = load_fallback_fonts(route.cache);

    const FlShapedRun mapped = font_cache_shape_run(route.cache, fonts.primary, 32 * 4, FL_S("Ao"));
    const i64 mapped_width = mapped.width;
    const FlShapedRun run = font_cache_shape_run(route.cache, fonts.primary, 32 * 4, FL_S("AoB"));
    ASSERT_EQ((int)run.count, 3);
    ASSERT_EQ((int)run.glyphs[2].font, (int)fonts.primary);
    ASSERT_EQ((int)run.glyphs[2].glyph_index, 0);
    ASSERT_TRUE(run.width == mapped_width);

    kb_route_close(&route);
}
