#include "utest/utest.h"
#include <core/arena.h>
#include <core/core.h>
#include <core/os/os.h>
#include <core/string.h>
#include <ui/font_shaper.h>

#include "fixtures/fonts/fallback_test_font.h"
#include "fixtures/fonts/glyph_test_fonts.h"

typedef struct ShaperRig {
    FlArena* arena;
    FileMmap roboto_file;
    FontShaper* shaper;
    FontShaperRun roboto;
} ShaperRig;

static ShaperRig shaper_rig_open(u16 size_q4) {
    ShaperRig rig = { .arena = arena_new() };
    rig.roboto_file = file_mmap_open_os(FL_S(FLOWI_TEST_DATA_DIR "/Roboto-Regular.ttf"));
    rig.shaper = font_shaper_new(rig.arena);
    FontShaperFont* font = font_shaper_font_new(rig.arena, rig.roboto_file.data, (i64)rig.roboto_file.size);
    rig.roboto = (FontShaperRun) {
        .primary = { .font = font, .id = 1 },
        .size_q4 = size_q4,
        .glyph_arena = rig.arena,
    };
    return rig;
}

static bool shaper_rig_ready(const ShaperRig* rig) {
    return rig->roboto_file.data != nullptr && rig->roboto.primary.font != nullptr;
}

static void shaper_rig_close(ShaperRig* rig) {
    font_shaper_destroy(rig->shaper);
    fl_arena_destroy(rig->arena);
    file_mmap_close_os(&rig->roboto_file);
}

static i32 summed_advances(const FontUnitRun* units) {
    i32 sum = 0;
    for_count(i, units->count) {
        sum += units->glyphs[i].advance;
    }
    return sum;
}

UTEST(font_shaper_run, latin_text_is_one_face_without_offsets) {
    ShaperRig rig = shaper_rig_open(56);
    ASSERT_TRUE(shaper_rig_ready(&rig));
    FontUnitRun units;

    ASSERT_TRUE(font_shaper_shape(rig.shaper, &rig.roboto, FL_S("AVATAR Wave"), &units));
    ASSERT_EQ((int)units.count, 11);
    ASSERT_FALSE(units.has_offsets || units.has_faces);
    ASSERT_EQ(units.width_units, summed_advances(&units));

    shaper_rig_close(&rig);
}

UTEST(font_shaper_run, empty_text_is_an_empty_run) {
    ShaperRig rig = shaper_rig_open(56);
    ASSERT_TRUE(shaper_rig_ready(&rig));
    FontUnitRun units;
    FlShapedRun placed;

    ASSERT_TRUE(font_shaper_shape(rig.shaper, &rig.roboto, FL_S(""), &units));
    font_shaper_place(&units, &rig.roboto, &placed);
    ASSERT_EQ((int)units.count + (int)placed.count, 0);
    ASSERT_EQ(placed.width, (i64)0);

    shaper_rig_close(&rig);
}

UTEST(font_shaper_run, a_positioned_mark_carries_an_offset) {
    ShaperRig rig = shaper_rig_open(56);
    ASSERT_TRUE(shaper_rig_ready(&rig));
    FontUnitRun units;

    ASSERT_TRUE(font_shaper_shape(rig.shaper, &rig.roboto, FL_S("x\xCC\x81"), &units));
    ASSERT_TRUE(units.has_offsets);
    ASSERT_NE((int)units.glyphs[1].offset_x, 0);

    shaper_rig_close(&rig);
}

UTEST(font_shaper_run, a_fallback_glyph_is_marked_with_its_face) {
    ShaperRig rig = shaper_rig_open(128);
    ASSERT_TRUE(shaper_rig_ready(&rig));
    const FontShaperRun run = {
        .primary = { .font = font_shaper_font_new(rig.arena, k_glyph_test_ttf, k_glyph_test_ttf_size), .id = 2 },
        .fallback = { .font = font_shaper_font_new(rig.arena, k_fallback_test_ttf, k_fallback_test_ttf_size), .id = 3 },
        .size_q4 = 128,
    };
    FontUnitRun units;

    ASSERT_TRUE(font_shaper_shape(rig.shaper, &run, FL_S("AoB"), &units));
    ASSERT_EQ((int)units.count, 3);
    ASSERT_TRUE(units.has_faces);
    ASSERT_EQ((int)units.glyphs[0].face + (int)units.glyphs[1].face, 0);
    ASSERT_EQ((int)units.glyphs[2].face, 1);

    shaper_rig_close(&rig);
}

static FlShapedRun placed_after_one_em(const ShaperRig* rig, FontUnitGlyph second) {
    const FontUnitGlyph glyphs[] = { { .glyph_index = 5, .advance = 2048 }, second };
    const FontUnitRun units = { .glyphs = glyphs, .count = 2, .width_units = 2048 + second.advance };
    FlShapedRun placed;
    font_shaper_place(&units, &rig->roboto, &placed);
    return placed;
}

UTEST(font_shaper_run, placing_scales_units_and_offsets_do_not_move_the_pen) {
    ShaperRig rig = shaper_rig_open(56);
    ASSERT_TRUE(shaper_rig_ready(&rig));

    const FlShapedRun scaled = placed_after_one_em(&rig, (FontUnitGlyph) { .glyph_index = 6, .advance = 1024 });
    ASSERT_EQ(scaled.glyphs[0].x, 0);
    ASSERT_EQ(scaled.glyphs[1].x, 14 * 64);
    ASSERT_EQ((int)scaled.glyphs[1].glyph_index, 6);
    ASSERT_EQ(scaled.width, (i64)21 * 64);

    const FlShapedRun offset = placed_after_one_em(&rig, (FontUnitGlyph) { .offset_x = -1024, .offset_y = 512 });
    ASSERT_EQ(offset.glyphs[1].x, 7 * 64);
    ASSERT_EQ(offset.glyphs[1].y, 224);
    ASSERT_EQ(offset.width, (i64)14 * 64);

    shaper_rig_close(&rig);
}

UTEST(font_shaper_run, the_pen_rounds_once_per_face_change) {
    ShaperRig rig = shaper_rig_open(52);
    ASSERT_TRUE(shaper_rig_ready(&rig));
    FontShaperRun run = rig.roboto;
    run.fallback = (FontShaperFace) { .font = run.primary.font, .id = 9 };
    const FontUnitGlyph glyphs[] = { { 5, 4 }, { 6, 4, .face = 1 }, { 7, 4 } };
    const FontUnitRun units = { .glyphs = glyphs, .count = 3, .width_units = 12, .has_faces = true };
    FlShapedRun placed;

    font_shaper_place(&units, &run, &placed);
    ASSERT_EQ(placed.glyphs[1].x, 2);
    ASSERT_EQ(placed.glyphs[2].x, 4);
    ASSERT_EQ(placed.width, (i64)6);

    shaper_rig_close(&rig);
}

UTEST(font_shaper_run, measuring_gives_the_width_of_the_placed_run) {
    ShaperRig rig = shaper_rig_open(89);
    ASSERT_TRUE(shaper_rig_ready(&rig));
    FontShaperRun measure = rig.roboto;
    measure.glyph_arena = nullptr;
    FontUnitRun units;
    FlShapedRun placed;
    FlShapedRun measured;

    ASSERT_TRUE(font_shaper_shape(rig.shaper, &rig.roboto, FL_S("Toy Tracker, Today."), &units));
    font_shaper_place(&units, &rig.roboto, &placed);
    font_shaper_place(&units, &measure, &measured);
    ASSERT_TRUE(measured.glyphs == nullptr);
    ASSERT_EQ(measured.count, placed.count);
    ASSERT_EQ(measured.width, placed.width);

    shaper_rig_close(&rig);
}
