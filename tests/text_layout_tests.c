#include "utest/utest.h"
#include <core/arena.h>
#include <core/core.h>
#include <core/os/os.h>
#include <core/string.h>
#include <core/utf8.h>
#include <ui/font_cache.h>
#include <ui/render_ir_build.h>

#include "fixtures/fonts/fallback_test_font.h"
#include "fixtures/fonts/fixed_pitch_test_fonts.h"
#include "fixtures/fonts/glyph_test_fonts.h"

typedef struct PenLayout {
    FlFontCache* cache;
    FlFontId font;
    u16 q4;
    float scale;
    float spacing;
    float origin_x;
} PenLayout;

static u32 collect_run_glyphs(const void* start, const void* end, FlGlyph* out) {
    u32 count = 0;
    const FlCmdHeader* h = start;
    while ((const void*)h < end) {
        const FlCmdTextRun* run = (const FlCmdTextRun*)(h + 1);
        const FlGlyph* glyphs = (const FlGlyph*)(run + 1);
        for_count(i, run->glyph_count) {
            out[count++] = glyphs[i];
        }
        h = (const FlCmdHeader*)((const u8*)h + h->byte_size);
    }
    return count;
}

static bool layout_follows_the_pen(const PenLayout* layout, FlString text) {
    FlArena* ir = arena_new();
    void* start = fl_arena_current_pos(ir);
    fl_ir_text_layout(ir, layout->cache, layout->font, layout->q4, layout->scale, (FlColor) { 255, 255, 255, 255 },
                      text, layout->origin_x, 20.25f, layout->spacing);
    FlGlyph glyphs[128];
    const u32 count = collect_run_glyphs(start, fl_arena_current_pos(ir), glyphs);

    bool same = true;
    i64 pen = 0;
    u32 index = 0;
    Utf8Iter it = utf8_iter(text);
    while (!utf8_iter_done(&it)) {
        const u32 codepoint = utf8_iter_next(&it);
        const float x = layout->origin_x + layout->scale * ((float)pen / 64.0f + (float)index * layout->spacing);
        same &= index < count && glyphs[index].x == x && glyphs[index].y == 20.25f;
        pen += font_cache_get_glyph_advance_q4(layout->cache, layout->font, layout->q4, codepoint);
        index++;
    }
    fl_arena_destroy(ir);
    return same && index == count;
}

static bool layouts_follow_the_pen(FlFontCache* cache, FlFontId font, FlString text) {
    const float sizes[] = { 7.5f, 13.0f, 14.0f, 22.25f, 28.0f };
    const float spacings[] = { 0.0f, 1.25f, 0.3f };
    const float scales[] = { 1.0f, 1.5f };
    const float origins[] = { 10.0f, 10.3f };

    bool same = true;
    for_count(i, sizeof_array(sizes) * sizeof_array(spacings) * sizeof_array(scales) * sizeof_array(origins)) {
        const size_t spacing = i / sizeof_array(sizes);
        const size_t scale = spacing / sizeof_array(spacings);
        const PenLayout layout = {
            .cache = cache,
            .font = font,
            .q4 = font_cache_quantize_size_q4(sizes[i % sizeof_array(sizes)]),
            .spacing = spacings[spacing % sizeof_array(spacings)],
            .scale = scales[scale % sizeof_array(scales)],
            .origin_x = origins[scale / sizeof_array(scales)],
        };
        same &= layout_follows_the_pen(&layout, text);
    }
    return same;
}

UTEST(text_layout, positions_follow_the_summed_advances) {
    FileMmap roboto_file = file_mmap_open_os(FL_S(FLOWI_TEST_DATA_DIR "/Roboto-Regular.ttf"));
    ASSERT_TRUE(roboto_file.data != nullptr);

    FlArena* arena = arena_new();
    FlFontCache* cache = font_cache_new(arena);
    FlFontId roboto = font_cache_load_font_memory(cache, roboto_file.data, (i64)roboto_file.size, 16);
    FlFontId uneven = font_cache_load_font_memory(cache, k_uneven_pitch_test_ttf, k_uneven_pitch_test_ttf_size, 16);
    FlFontId partial = font_cache_load_font_memory(cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    FlFontId fallback = font_cache_load_font_memory(cache, k_fallback_test_ttf, k_fallback_test_ttf_size, 32);
    FlFontId builtin = font_cache_load_default_font(cache, 2);
    font_cache_set_fallback_font(cache, fallback);

    const FlFontId fonts[] = { roboto, uneven, partial, builtin };
    const FlString texts[] = {
        FL_S("AVATAR Wave. To: fjord"),
        FL_S("static int measure(const char* text) { return 0; }"),
        FL_S("AoBBAoB"),
        { .data = "na\xC3\xAFve caf\xC3\xA9", .length = 12, .is_static = 1 },
    };
    for_count(f, sizeof_array(fonts)) {
        ASSERT_TRUE_MSG(fonts[f] != FONT_ID_INVALID, "the test font must load");
        for_count(t, sizeof_array(texts)) {
            ASSERT_TRUE_MSG(layouts_follow_the_pen(cache, fonts[f], texts[t]),
                            "each glyph sits at the pen the summed advances and spacing give");
        }
    }

    font_cache_destroy(cache);
    fl_arena_destroy(arena);
    file_mmap_close_os(&roboto_file);
}
