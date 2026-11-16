#include "kb_route_fixture.h"
#include <string.h>
#include <ui/painter.h>
#include <ui/render_ir.h>
#include <ui/text_spans.h>

#include "fixtures/fonts/fixed_pitch_test_fonts.h"

#define Q4 (13 * 4)

static const FlColor k_red = { .r = 255, .a = 255 };
static const FlColor k_blue = { .b = 255, .a = 255 };

static bool same_color(FlColor a, FlColor b) {
    return memcmp(&a, &b, sizeof(a)) == 0;
}

static FlString text_of(const char* chars) {
    return (FlString) { .data = chars, .length = strlen(chars), .is_static = 1 };
}

static FlFontId load_mono(const KbRoute* route) {
    return font_cache_load_font_memory(route->cache, k_fixed_pitch_test_ttf, k_fixed_pitch_test_ttf_size, 16);
}

static TextSpanSlices slice(const KbRoute* route, FlFontId font, SpannedText spanned) {
    const FlShapedRun shaped = font_cache_shape_run(route->cache, font, Q4, spanned.text);
    return text_span_slices(route->arena, (SpannedLine) { spanned.text, shaped }, spanned.spans, spanned.span_count);
}

static bool slice_covers(const TextSpanSlice* s, u32 first, u32 count) {
    return s->first_glyph == first && s->glyph_count == count;
}

UTEST(text_spans, spans_split_the_glyphs_in_order) {
    KbRoute route = kb_route_open();
    const FlTextSpan spans[] = { { .len = 2, .color = k_red }, { .len = 3, .color = k_blue, .bg = k_red } };

    const TextSpanSlices out = slice(&route, load_mono(&route), (SpannedText) { text_of("abcdefg"), spans, 2 });
    ASSERT_EQ((int)out.count, 2);
    ASSERT_TRUE(slice_covers(&out.slices[0], 0, 2));
    ASSERT_TRUE(slice_covers(&out.slices[1], 2, 3));

    kb_route_close(&route);
}

UTEST(text_spans, each_slice_carries_its_colors_and_ends_where_the_next_starts) {
    KbRoute route = kb_route_open();
    const FlTextSpan spans[] = { { .len = 2, .color = k_red }, { .len = 3, .color = k_blue, .bg = k_red } };

    const TextSpanSlices out = slice(&route, load_mono(&route), (SpannedText) { text_of("abcdefg"), spans, 2 });
    ASSERT_TRUE(same_color(out.slices[0].color, k_red));
    ASSERT_TRUE(same_color(out.slices[1].color, k_blue));
    ASSERT_TRUE(same_color(out.slices[1].bg, k_red));
    ASSERT_TRUE(out.slices[0].x_end == out.slices[1].x_start);

    kb_route_close(&route);
}

UTEST(text_spans, text_after_the_last_span_is_in_no_slice) {
    KbRoute route = kb_route_open();
    const FlTextSpan spans[] = { { .len = 2, .color = k_red } };

    const TextSpanSlices out = slice(&route, load_mono(&route), (SpannedText) { text_of("abcdefg"), spans, 1 });
    ASSERT_EQ((int)out.count, 1);

    kb_route_close(&route);
}

UTEST(text_spans, a_span_past_the_end_is_cut_and_empty_spans_give_no_slice) {
    KbRoute route = kb_route_open();
    const FlTextSpan spans[] = { { .len = 0, .color = k_blue }, { .len = 99, .color = k_red }, { .len = 4 } };

    const TextSpanSlices out = slice(&route, load_mono(&route), (SpannedText) { text_of("abc"), spans, 3 });
    ASSERT_EQ((int)out.count, 1);
    ASSERT_TRUE(slice_covers(&out.slices[0], 0, 3));
    ASSERT_TRUE(same_color(out.slices[0].color, k_red));

    kb_route_close(&route);
}

UTEST(text_spans, a_span_edge_inside_a_utf8_sequence_moves_to_its_end) {
    KbRoute route = kb_route_open();
    const FlTextSpan spans[] = { { .len = 2, .color = k_red }, { .len = 1, .color = k_blue } };

    const TextSpanSlices out = slice(&route, load_mono(&route), (SpannedText) { text_of("a\xC3\xAFz"), spans, 2 });
    ASSERT_EQ((int)out.count, 2);
    ASSERT_TRUE(slice_covers(&out.slices[0], 0, 2));
    ASSERT_TRUE(slice_covers(&out.slices[1], 2, 1));

    kb_route_close(&route);
}

UTEST(text_spans, proportional_spans_share_one_shaped_run) {
    KbRoute route = kb_route_open();
    const FlTextSpan spans[] = { { .len = 1, .color = k_red }, { .len = 3, .color = k_blue } };

    const FlShapedRun whole = font_cache_shape_run(route.cache, route.roboto, Q4, text_of("AVAV"));
    const TextSpanSlices out = text_span_slices(route.arena, (SpannedLine) { text_of("AVAV"), whole }, spans, 2);
    ASSERT_EQ((int)out.count, 2);
    ASSERT_TRUE(out.slices[1].x_end == whole.width);
    ASSERT_TRUE(out.slices[0].x_end == out.slices[1].x_start);

    kb_route_close(&route);
}

typedef struct PainterRig {
    FlArena* arena;
    FlFontCache* cache;
    FlFontId mono;
    Painter* painter;
} PainterRig;

static PainterRig rig_open(void) {
    PainterRig rig = { .arena = arena_new() };
    rig.cache = font_cache_new(rig.arena);
    rig.mono = font_cache_load_font_memory(rig.cache, k_fixed_pitch_test_ttf, k_fixed_pitch_test_ttf_size, 16);
    rig.painter = painter_create(rig.arena);
    painter_begin_frame(rig.painter, (PainterFrame) { .font_cache = rig.cache,
                                                      .ui_scale = 1.0f,
                                                      .text_scale = 1.0f,
                                                      .state_arena = rig.arena });
    return rig;
}

static void rig_close(PainterRig* rig) {
    font_cache_destroy(rig->cache);
    fl_arena_destroy(rig->arena);
}

typedef struct Recorded {
    int fills;
    int runs;
    bool fills_first;
    FlColor run_colors[8];
    float run_x[8];
    FlRect fill_rect;
    FlColor fill_color;
} Recorded;

static void record_record(Recorded* rec, const FlCmdHeader* h) {
    if (h->kind == FlCmd_FillRect) {
        const FlCmdFillRect* fill = (const FlCmdFillRect*)(h + 1);
        rec->fills_first &= rec->runs == 0;
        rec->fill_rect = fill->rect;
        rec->fill_color = fill->corner[0];
        rec->fills++;
    } else if (h->kind == FlCmd_TextRun) {
        const FlCmdTextRun* run = (const FlCmdTextRun*)(h + 1);
        rec->run_colors[rec->runs] = run->color;
        rec->run_x[rec->runs++] = ((const FlGlyph*)(run + 1))[0].x;
    }
}

static Recorded record_span(const FlPaintSpan* span) {
    Recorded rec = { .fills_first = true };
    const u8* at = (const u8*)span->first->ir;
    for (const u8* end = at + span->first->size; at < end; at += ((const FlCmdHeader*)at)->byte_size) {
        record_record(&rec, (const FlCmdHeader*)at);
    }
    return rec;
}

static Recorded record_spans(PainterRig* rig, FlTextStyle style, SpannedText spanned) {
    void* slot = nullptr;
    painter_scope_push(rig->painter, &slot);
    painter_text_spans(rig->painter, 10.0f, 20.0f, spanned, style);
    painter_scope_pop(rig->painter);
    return record_span((const FlPaintSpan*)slot);
}

UTEST(text_spans, the_painter_draws_every_fill_before_the_text) {
    PainterRig rig = rig_open();
    const FlTextSpan spans[] = { { .len = 2, .color = k_red, .bg = k_blue }, { .len = 3, .color = k_blue } };

    const Recorded rec = record_spans(&rig, (FlTextStyle) { .font_id = rig.mono, .font_size = 13.0f },
                                      (SpannedText) { text_of("abcde"), spans, 2 });
    ASSERT_EQ(rec.fills, 1);
    ASSERT_EQ(rec.runs, 2);
    ASSERT_TRUE(rec.fills_first);
    ASSERT_TRUE(same_color(rec.fill_color, k_blue));

    rig_close(&rig);
}

UTEST(text_spans, the_painter_colors_each_run_and_places_the_fill_under_its_span) {
    PainterRig rig = rig_open();
    const FlTextSpan spans[] = { { .len = 2, .color = k_red, .bg = k_blue }, { .len = 1, .color = k_blue } };

    const Recorded rec = record_spans(&rig, (FlTextStyle) { .font_id = rig.mono, .font_size = 13.0f },
                                      (SpannedText) { text_of("abcde"), spans, 2 });
    ASSERT_TRUE(same_color(rec.run_colors[0], k_red));
    ASSERT_TRUE(same_color(rec.run_colors[1], k_blue));
    ASSERT_NEAR_MSG(rec.run_x[0], 10.0f, 0.0001f, "the first run starts at x");
    ASSERT_NEAR_MSG(rec.fill_rect.x, 10.0f, 0.0001f, "the fill starts with its span");
    ASSERT_NEAR_MSG(rec.fill_rect.w, rec.run_x[1] - rec.run_x[0], 0.0001f, "the fill ends where the next span starts");

    rig_close(&rig);
}

UTEST(text_spans, the_same_positions_as_text_styled) {
    PainterRig rig = rig_open();
    const FlTextStyle style = { .font_id = rig.mono, .font_size = 13.0f, .letter_spacing = 1.5f };
    const FlTextSpan spans[] = { { .len = 2, .color = k_red }, { .len = 3, .color = k_blue } };
    const Recorded rec = record_spans(&rig, style, (SpannedText) { text_of("abcde"), spans, 2 });

    void* slot = nullptr;
    painter_scope_push(rig.painter, &slot);
    painter_text_styled(rig.painter, 10.0f, 20.0f, text_of("abcde"), style, k_red);
    painter_scope_pop(rig.painter);
    const FlPaintSpan* span = (const FlPaintSpan*)slot;
    const FlGlyph* glyphs = (const FlGlyph*)((const FlCmdTextRun*)((const FlCmdHeader*)span->first->ir + 1) + 1);

    ASSERT_NEAR_MSG(rec.run_x[0], glyphs[0].x, 0.0001f, "first span starts where the line starts");
    ASSERT_NEAR_MSG(rec.run_x[1], glyphs[2].x, 0.0001f, "second span starts on the third glyph, spacing included");

    rig_close(&rig);
}

static u64 shape_misses(const KbRoute* route) {
    return stats_get_value_by_name(font_cache_stats(route->cache), FL_S("misses"), FL_S("shaped_run_cache"));
}

UTEST(text_spans, drawing_the_same_line_with_other_spans_shapes_it_once) {
    KbRoute route = kb_route_open();
    Painter* p = painter_create(route.arena);
    painter_begin_frame(p, (PainterFrame) { .font_cache = route.cache,
                                            .ui_scale = 1.0f,
                                            .text_scale = 1.0f,
                                            .state_arena = route.arena });
    const FlTextStyle style = { .font_id = route.roboto, .font_size = 13.0f };
    const FlTextSpan first[] = { { .len = 2, .color = k_red } };
    const FlTextSpan second[] = { { .len = 4, .color = k_blue } };

    void* slot = nullptr;
    painter_scope_push(p, &slot);
    painter_text_spans(p, 0.0f, 0.0f, (SpannedText) { text_of("AVAVAV"), first, 1 }, style);
    const u64 after_first = shape_misses(&route);
    painter_text_spans(p, 0.0f, 20.0f, (SpannedText) { text_of("AVAVAV"), second, 1 }, style);
    painter_scope_pop(p);

    ASSERT_EQ(after_first, 1ULL);
    ASSERT_EQ(shape_misses(&route), 1ULL);

    kb_route_close(&route);
}
