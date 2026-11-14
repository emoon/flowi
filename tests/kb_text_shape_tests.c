#include "utest/utest.h"
#include <core/core.h>
#include <core/os/os.h>
#include <core/string.h>

#include "fixtures/fonts/glyph_test_fonts.h"
#include "kb/kb_text_shape.h"

typedef struct KbShaped {
    int glyph_count;
    int advance;
} KbShaped;

static KbShaped kb_shape(kbts_shape_context* context, FlString text) {
    kbts_ShapeBegin(context, KBTS_DIRECTION_LTR, KBTS_LANGUAGE_DONT_KNOW);
    kbts_ShapeUtf8(context, text.data, (int)text.length, KBTS_USER_ID_GENERATION_MODE_CODEPOINT_INDEX);
    kbts_ShapeEnd(context);

    KbShaped shaped = { 0 };
    kbts_run run;
    while (kbts_ShapeRun(context, &run)) {
        kbts_glyph* glyph;
        while (kbts_GlyphIteratorNext(&run.Glyphs, &glyph)) {
            shaped.glyph_count++;
            shaped.advance += glyph->AdvanceX;
        }
    }
    return shaped;
}

UTEST(kb_text_shape, shapes_latin_string_from_memory_font) {
    kbts_shape_context* context = kbts_CreateShapeContext(nullptr, nullptr);
    ASSERT_TRUE(context != nullptr);
    kbts_font* font = kbts_ShapePushFontFromMemory(context, (void*)k_glyph_test_ttf, (int)k_glyph_test_ttf_size, 0);
    const KbShaped shaped = kb_shape(context, FL_S("AoA"));
    const kbts_shape_error error = kbts_ShapeError(context);
    kbts_DestroyShapeContext(context);

    ASSERT_TRUE(font != nullptr);
    ASSERT_EQ(error, (kbts_shape_error)KBTS_SHAPE_ERROR_NONE);
    ASSERT_EQ(shaped.glyph_count, 3);
    ASSERT_EQ(shaped.advance, 2400);
}

UTEST(kb_text_shape, kerned_pair_is_narrower_than_its_glyphs_alone) {
    FileMmap file = file_mmap_open_os(FL_S(FLOWI_TEST_DATA_DIR "/Roboto-Regular.ttf"));
    ASSERT_TRUE(file.data != nullptr);
    kbts_shape_context* context = kbts_CreateShapeContext(nullptr, nullptr);
    kbts_font* font = kbts_ShapePushFontFromMemory(context, (void*)file.data, (int)file.size, 0);
    const KbShaped pair = kb_shape(context, FL_S("AV"));
    const KbShaped first = kb_shape(context, FL_S("A"));
    const KbShaped second = kb_shape(context, FL_S("V"));
    const kbts_shape_error error = kbts_ShapeError(context);
    kbts_DestroyShapeContext(context);
    file_mmap_close_os(&file);

    ASSERT_TRUE(font != nullptr);
    ASSERT_EQ(error, (kbts_shape_error)KBTS_SHAPE_ERROR_NONE);
    ASSERT_EQ(pair.glyph_count, 2);
    ASSERT_GT(pair.advance, 0);
    ASSERT_LT(pair.advance, first.advance + second.advance);
}

UTEST(kb_text_shape, loads_a_font_with_a_null_allocator) {
    kbts_font font = kbts_FontFromMemory((void*)k_glyph_test_ttf, (int)k_glyph_test_ttf_size, 0, nullptr, nullptr);
    const bool valid = kbts_FontIsValid(&font);
    const bool has_blob = font.Blob != nullptr;
    kbts_FreeFont(&font);

    ASSERT_TRUE(valid);
    ASSERT_TRUE(has_blob);
}
