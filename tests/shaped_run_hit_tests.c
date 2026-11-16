#include "kb_route_fixture.h"
#include <string.h>

#include "fixtures/fonts/fixed_pitch_test_fonts.h"

#define SIZE 13.0f
#define TOLERANCE 0.001f

static FlString text_of(const char* chars) {
    return (FlString) { .data = chars, .length = strlen(chars), .is_static = 1 };
}

static FlFontId load_mono(const KbRoute* route) {
    return font_cache_load_font_memory(route->cache, k_fixed_pitch_test_ttf, k_fixed_pitch_test_ttf_size, 16);
}

static float cell_of(const KbRoute* route, FlFontId font) {
    const FlTextStyle style = { .font_id = font, .font_size = SIZE };
    return font_cache_measure_text(route->cache, text_of("a"), style).width;
}

UTEST(shaped_run_hit, offsets_map_to_the_left_edge_of_their_character) {
    KbRoute route = kb_route_open();
    const FlFontId mono = load_mono(&route);
    const float cell = cell_of(&route, mono);
    const FlTextStyle style = { .font_id = mono, .font_size = SIZE };

    ASSERT_NEAR(font_cache_offset_to_x(route.cache, text_of("abc"), style, 0), 0.0f, TOLERANCE);
    ASSERT_NEAR(font_cache_offset_to_x(route.cache, text_of("abc"), style, 2), 2.0f * cell, TOLERANCE);
    ASSERT_NEAR(font_cache_offset_to_x(route.cache, text_of("abc"), style, 3), 3.0f * cell, TOLERANCE);
    ASSERT_NEAR(font_cache_offset_to_x(route.cache, text_of("abc"), style, 99), 3.0f * cell, TOLERANCE);

    kb_route_close(&route);
}

UTEST(shaped_run_hit, an_x_maps_to_the_character_under_it) {
    KbRoute route = kb_route_open();
    const FlFontId mono = load_mono(&route);
    const float cell = cell_of(&route, mono);
    const FlTextStyle style = { .font_id = mono, .font_size = SIZE };
    const FlString text = text_of("abc");

    ASSERT_EQ((int)font_cache_x_to_offset(route.cache, text, style, 0.1f * cell), 0);
    ASSERT_EQ((int)font_cache_x_to_offset(route.cache, text, style, 1.9f * cell), 1);
    ASSERT_EQ((int)font_cache_x_to_offset(route.cache, text, style, 2.1f * cell), 2);

    kb_route_close(&route);
}

UTEST(shaped_run_hit, an_x_outside_the_line_gives_its_first_glyph_or_its_length) {
    KbRoute route = kb_route_open();
    const FlFontId mono = load_mono(&route);
    const FlTextStyle style = { .font_id = mono, .font_size = SIZE };

    ASSERT_EQ((int)font_cache_x_to_offset(route.cache, text_of("abc"), style, -50.0f), 0);
    ASSERT_EQ((int)font_cache_x_to_offset(route.cache, text_of("abc"), style, 500.0f), 3);
    ASSERT_EQ((int)font_cache_x_to_offset(route.cache, text_of(""), style, 5.0f), 0);

    kb_route_close(&route);
}

UTEST(shaped_run_hit, a_tab_is_hit_across_its_whole_width) {
    KbRoute route = kb_route_open();
    const FlFontId mono = load_mono(&route);
    const float cell = cell_of(&route, mono);
    const FlTextStyle style = { .font_id = mono, .font_size = SIZE, .tab_width = 4.0f };
    const FlString text = text_of("ab\tcd");

    ASSERT_NEAR(font_cache_offset_to_x(route.cache, text, style, 2), 2.0f * cell, TOLERANCE);
    ASSERT_NEAR(font_cache_offset_to_x(route.cache, text, style, 3), 4.0f * cell, TOLERANCE);
    ASSERT_EQ((int)font_cache_x_to_offset(route.cache, text, style, 2.1f * cell), 2);
    ASSERT_EQ((int)font_cache_x_to_offset(route.cache, text, style, 3.9f * cell), 2);
    ASSERT_EQ((int)font_cache_x_to_offset(route.cache, text, style, 4.1f * cell), 3);

    kb_route_close(&route);
}

UTEST(shaped_run_hit, letter_spacing_adds_one_gap_per_glyph_before_the_character) {
    KbRoute route = kb_route_open();
    const FlFontId mono = load_mono(&route);
    const float pitch = cell_of(&route, mono) + 2.0f;
    const FlTextStyle style = { .font_id = mono, .font_size = SIZE, .letter_spacing = 2.0f };

    for_count(i, 3) {
        ASSERT_NEAR(font_cache_offset_to_x(route.cache, text_of("abc"), style, (u32)i), (float)i * pitch, TOLERANCE);
        ASSERT_EQ((int)font_cache_x_to_offset(route.cache, text_of("abc"), style, (float)i * pitch + 0.1f), (int)i);
    }

    kb_route_close(&route);
}

UTEST(shaped_run_hit, an_offset_inside_a_utf8_sequence_gives_the_next_character) {
    KbRoute route = kb_route_open();
    const FlTextStyle style = { .font_id = route.roboto, .font_size = SIZE };
    const FlString text = { .data = "a\xC3\xAFz", .length = 4, .is_static = 1 };

    const float next = font_cache_offset_to_x(route.cache, text, style, 3);
    ASSERT_NEAR(font_cache_offset_to_x(route.cache, text, style, 2), next, TOLERANCE);
    ASSERT_EQ((int)font_cache_x_to_offset(route.cache, text, style, next - 0.1f), 1);

    kb_route_close(&route);
}

static float measured(const KbRoute* route, FlString text, FlTextStyle style) {
    return font_cache_measure_text(route->cache, text, style).width;
}

UTEST(shaped_run_hit, the_end_of_the_line_is_the_measured_width) {
    KbRoute route = kb_route_open();
    const FlFontId mono = load_mono(&route);
    const FlString text = text_of("hello world");
    const FlTextStyle fixed = { .font_id = mono, .font_size = SIZE, .letter_spacing = 1.5f };
    const FlTextStyle proportional = { .font_id = route.roboto, .font_size = SIZE };

    ASSERT_NEAR(font_cache_offset_to_x(route.cache, text, fixed, 99), measured(&route, text, fixed), TOLERANCE);
    ASSERT_NEAR(font_cache_offset_to_x(route.cache, text, proportional, 99), measured(&route, text, proportional),
                TOLERANCE);

    kb_route_close(&route);
}

UTEST(shaped_run_hit, an_x_round_trips_through_every_character_boundary) {
    KbRoute route = kb_route_open();
    const FlTextStyle style = { .font_id = route.roboto, .font_size = SIZE, .letter_spacing = 0.5f };
    const FlString text = text_of("Hit test AVAV");

    for_count(offset, text.length) {
        const float x = font_cache_offset_to_x(route.cache, text, style, (u32)offset);
        ASSERT_EQ((int)font_cache_x_to_offset(route.cache, text, style, x + 0.01f), (int)offset);
    }

    kb_route_close(&route);
}

UTEST(shaped_run_hit, an_unknown_font_gives_zero) {
    KbRoute route = kb_route_open();
    const FlTextStyle style = { .font_id = 9999, .font_size = SIZE };

    ASSERT_EQ((int)font_cache_x_to_offset(route.cache, text_of("abc"), style, 10.0f), 0);
    ASSERT_NEAR(font_cache_offset_to_x(route.cache, text_of("abc"), style, 2), 0.0f, TOLERANCE);

    kb_route_close(&route);
}
