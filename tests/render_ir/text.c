// render-IR text run tests

#include "render_ir/common.h"

// The styled text-run layout (ir_text_layout at a fractional quarter-px key) agrees with
// font_cache_measure_text: the same quantizer, the same glyph positions and the same n-1 letter-spacing rule.

UTEST(render_ir, styled_text_layout_matches_measurement) {
    FlFontCache* cache = font_cache_new(s_arena);
    FlFontId font = load_glyph_test_font(cache);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "memory-loaded font must be ready synchronously");

    const float font_size = 7.5f;
    const float spacing = 1.25f;
    const u16 q4 = font_cache_quantize_size_q4(font_size);
    ASSERT_EQ_MSG((int)q4, 30, "7.5px resolves exactly in quarter-px steps");

    FlArena* ir = arena_new();
    void* start = fl_arena_current_pos(ir);
    FlString aaa = { .data = "AAA", .length = 3, .is_static = 1, .is_ascii = 1 };
    ir_text_layout(ir, cache, font, q4, 1.0f, (FlColor) { 255, 255, 255, 255 }, aaa, 10.0f, 20.0f, spacing);

    const FlCmdHeader* h = (const FlCmdHeader*)start;
    ASSERT_EQ_MSG((int)h->kind, (int)FlCmd_TextRun, "the layout emits one text run");
    const FlCmdTextRun* run = (const FlCmdTextRun*)(h + 1);
    ASSERT_EQ_MSG((int)run->size, (int)q4, "the run carries the quarter-px size key");
    ASSERT_EQ_MSG((int)run->glyph_count, 3, "one glyph per codepoint");
    const FlGlyph* glyphs = (const FlGlyph*)(run + 1);

    float adv = (float)laid_out_advance_q4(cache, font, q4, 'A') / 64.0f;
    ASSERT_TRUE_MSG(adv > 0.0f, "the fractional advance is non-zero");
    ASSERT_EQ_MSG(font_cache_get_glyph_advance_q4(cache, 9999, q4, 'A'), 0, "an unknown font advances zero");
    const float first_step = glyphs[1].x - glyphs[0].x;
    const float second_step = glyphs[2].x - glyphs[1].x;
    ASSERT_NEAR_MSG(first_step, adv + spacing, 0.0001f, "each pen step adds advance + spacing");
    ASSERT_NEAR_MSG(second_step, adv + spacing, 0.0001f, "each pen step adds advance + spacing");

    // Without the n-1 rule the first pen would move while the delta and extent checks above still pass.
    FlTextMetrics m = font_cache_measure_text(
        cache, aaa, (FlTextStyle) { .font_id = font, .font_size = font_size, .letter_spacing = spacing });
    ASSERT_NEAR_MSG(glyphs[0].x, 10.0f, 0.0001f, "the first glyph has no leading spacing gap");
    ASSERT_NEAR_MSG((glyphs[2].x + adv) - glyphs[0].x, m.width, 0.0001f, "draw and measure agree on the width");

    fl_arena_destroy(ir);
    font_cache_destroy(cache);
}

// The face, size and text a shaped run was made from.
typedef struct ShapeCase {
    FlFontCache* cache;
    FlFontId font;
    u16 q4;
    FlString text;
} ShapeCase;

// Asserts every glyph of run sits after the advances before it, on the baseline, on the face's glyph index for
// the codepoint. Stores the sum of the advances in out_sum.
static void expect_unkerned_run(int* utest_result, const FlShapedRun* run, const ShapeCase* c, i64* out_sum) {
    i64 sum = 0;
    for_count(i, run->count) {
        const u32 codepoint = (u32)c->text.data[i];
        ASSERT_EQ_MSG((int)run->glyphs[i].glyph_index, (int)font_cache_get_glyph_index(c->cache, c->font, codepoint),
                      "the glyph index is the face's index for the codepoint");
        ASSERT_EQ_MSG((int)run->glyphs[i].font, (int)c->font, "a codepoint the font maps stays on it");
        ASSERT_TRUE_MSG(run->glyphs[i].x == sum, "each glyph sits after the advances before it");
        sum += laid_out_advance_q4(c->cache, c->font, c->q4, codepoint);
        ASSERT_EQ_MSG(run->glyphs[i].y, 0, "unkerned glyphs sit on the baseline");
    }
    *out_sum = sum;
}

UTEST(render_ir, shaped_run_matches_measurement) {
    FlFontCache* cache = font_cache_new(s_arena);
    FlFontId font = load_glyph_test_font(cache);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "memory-loaded font must be ready synchronously");

    const float font_size = 7.5f;
    const u16 q4 = font_cache_quantize_size_q4(font_size);

    FlString ascii = { .data = "AAB", .length = 3, .is_static = 1, .is_ascii = 1 };
    FlShapedRun run = font_cache_shape_run(cache, font, q4, ascii);
    ASSERT_EQ_MSG((int)run.count, 3, "one glyph per codepoint");
    i64 sum = 0;
    IR_EXPECT(expect_unkerned_run, &run, &(ShapeCase) { cache, font, q4, ascii }, &sum);
    ASSERT_TRUE_MSG(run.width == sum, "the run width is the sum of its advances");

    FlTextMetrics m = font_cache_measure_text(cache, ascii, (FlTextStyle) { .font_id = font, .font_size = font_size });
    ASSERT_NEAR_MSG((float)run.width / 64.0f, m.width, 0.0001f, "shaping and measuring agree on the width");

    FlString accented = { .data = "A\xC3\xA9", .length = 3, .is_static = 1 };
    FlShapedRun wide = font_cache_shape_run(cache, font, q4, accented);
    ASSERT_EQ_MSG((int)wide.count, 2, "glyphs are counted per codepoint, not per byte");
    FlFontId mono = font_cache_load_font_memory(cache, k_fixed_pitch_test_ttf, k_fixed_pitch_test_ttf_size, 16);
    const u32 eacute = font_cache_get_glyph_index(cache, mono, 0xE9);
    ASSERT_TRUE_MSG(eacute != 0, "the fixed-pitch test font maps e-acute");
    ASSERT_EQ_MSG((int)font_cache_shape_run(cache, mono, q4, accented).glyphs[1].glyph_index, (int)eacute,
                  "a multi-byte sequence decodes to one glyph");

    FlString mislabelled = { .data = "A\xC3\xA9", .length = 3, .is_static = 1, .is_ascii = 1 };
    ASSERT_EQ_MSG((int)font_cache_shape_run(cache, font, q4, mislabelled).count, 2, "drawing ignores is_ascii");

    FlShapedRun unknown = font_cache_shape_run(cache, 9999, q4, ascii);
    ASSERT_EQ_MSG((int)unknown.count, 3, "an unknown font still yields its glyphs");
    ASSERT_TRUE_MSG(unknown.width == 0, "an unknown font advances zero");
    ASSERT_EQ_MSG((int)unknown.glyphs[0].font, 9999, "an unknown font keeps the id it was asked for");

    FlShapedRun empty = font_cache_shape_run(cache, font, q4, (FlString) { 0 });
    ASSERT_EQ_MSG((int)empty.count, 0, "an empty string shapes to nothing");

    font_cache_destroy(cache);
}

// Paints text_styled in its own scope and returns the slot the scope's span attached to.
static void* paint_styled_text(Painter* p, FlVec2 at, FlString text, FlTextStyle style) {
    void* slot = nullptr;
    painter_scope_push(p, &slot);
    painter_text_styled(p, at.x, at.y, text, style, ui_color(255, 255, 255, 255));
    painter_scope_pop(p);
    return slot;
}

// Positions scale with ui_scale. The type size and spacing follow text_scale, which this frame holds at 1.
UTEST(render_ir, painter_text_styled_emits_quarter_px_run) {
    FlArena* arena = arena_new();
    FlFontCache* cache = font_cache_new(s_arena);
    FlFontId font = load_glyph_test_font(cache);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "memory-loaded font must be ready synchronously");

    Painter* p = test_painter_begin(arena, cache, 2.0f, 1.0f);

    const FlTextStyle style = { .font_id = font, .font_size = 7.5f, .letter_spacing = 1.25f };
    void* slot = paint_styled_text(p, (FlVec2) { .x = 10.0f, .y = 20.0f }, FL_S("AA"), style);

    const FlCmdHeader* h = span_first_record(slot);
    ASSERT_TRUE_MSG(h != nullptr, "styled text attached a paint span");
    ASSERT_EQ_MSG((int)h->kind, (int)FlCmd_TextRun, "styled text emits one text run");
    const FlCmdTextRun* run = (const FlCmdTextRun*)(h + 1);
    ASSERT_EQ_MSG((int)run->size, 30, "the run carries the 7.5px quarter-px key");
    ASSERT_EQ_MSG((int)run->font, (int)font, "the run carries the styled font");
    ASSERT_EQ_MSG((int)run->glyph_count, 2, "one glyph per codepoint");

    const FlGlyph* glyphs = (const FlGlyph*)(run + 1);
    float ascent = font_cache_get_font_metrics(cache, font, 7.5f).ascent;
    ASSERT_NEAR_MSG(glyphs[0].x, 20.0f, 0.0001f, "the pen x is the ui-scaled origin");
    ASSERT_NEAR_MSG(glyphs[0].y, 40.0f + ascent, 0.0001f, "the baseline is the ui-scaled y plus the ascent");
    float adv = (float)laid_out_advance_q4(cache, font, 30, 'A') / 64.0f;
    ASSERT_NEAR_MSG(glyphs[1].x - glyphs[0].x, adv + 1.25f, 0.0001f,
                    "advance and spacing stay unscaled, matching measurement");

    // font_size 0 falls back to the font's default size (loaded at 32, so key 128).
    void* slot_default = paint_styled_text(p, (FlVec2) { 0 }, FL_S("A"), (FlTextStyle) { .font_id = font });
    const FlCmdHeader* hd = span_first_record(slot_default);
    ASSERT_TRUE_MSG(hd != nullptr, "size 0 still draws");
    const FlCmdTextRun* run_default = (const FlCmdTextRun*)(hd + 1);
    ASSERT_EQ_MSG((int)run_default->size, 128, "size 0 resolves to the font's default size key");

    void* slot_unknown = paint_styled_text(p, (FlVec2) { 0 }, FL_S("A"), (FlTextStyle) { .font_id = 9999 });
    ASSERT_TRUE_MSG(slot_unknown == nullptr, "an unknown font attaches no paint span");

    font_cache_destroy(cache);
    fl_arena_destroy(arena);
}

// What clay_text_run needs to turn one Clay text command into a run. Whole-px font size, so the size key needs no
// quantizer.
typedef struct ClayTextCase {
    FlFontCache* cache;
    FlFontId font;
    u16 font_size;
    u16 spacing;
} ClayTextCase;

// Returns nullptr if the hop emitted something other than a text run.
static const FlCmdTextRun* clay_text_run(FlArena* out, const ClayTextCase* c) {
    static const char k_text[] = "AAA";
    fl_arena_align(out, 8);
    const FlCmdHeader* h = (const FlCmdHeader*)fl_arena_current_pos(out);
    Clay_RenderCommand cmd = {
        .commandType = CLAY_RENDER_COMMAND_TYPE_TEXT,
        .boundingBox = { 10.0f, 20.0f, 400.0f, 40.0f },
        .renderData = { .text = { .stringContents = { .length = 3, .chars = k_text, .baseChars = k_text },
                                  .textColor = { 255, 255, 255, 255 },
                                  .fontId = (uint16_t)c->font,
                                  .fontSize = c->font_size,
                                  .letterSpacing = c->spacing,
                                  .verticalAlignment = CLAY_TEXT_VALIGN_TOP } },
    };
    fl_clay_to_render_ir(out, c->cache, &cmd, 1);
    return h->kind == FlCmd_TextRun ? (const FlCmdTextRun*)(h + 1) : nullptr;
}

// Letter spacing survives the Clay -> IR hop. Clay widens the text element whether or not the value is forwarded,
// so only the emitted pen positions show whether it was.
UTEST(render_ir, clay_text_forwards_letter_spacing_to_the_pen) {
    FlFontCache* cache = font_cache_new(s_arena);
    FlFontId font = load_glyph_test_font(cache);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "memory-loaded font must be ready synchronously");

    const u16 font_size = 32;
    const float adv = (float)laid_out_advance_q4(cache, font, font_cache_whole_px_to_q4(font_size), 'A') / 64.0f;
    ASSERT_TRUE_MSG(adv > 0.0f, "the advance is non-zero");

    FlArena* out_packed = arena_new();
    FlArena* out_spaced = arena_new();

    const FlCmdTextRun* packed = clay_text_run(out_packed, &(ClayTextCase) { cache, font, font_size, 0 });
    const FlCmdTextRun* spaced = clay_text_run(out_spaced, &(ClayTextCase) { cache, font, font_size, 5 });
    ASSERT_TRUE_MSG(packed != nullptr && spaced != nullptr, "each command emits one text run");
    ASSERT_EQ_MSG((int)packed->glyph_count, 3, "one glyph per codepoint");
    ASSERT_EQ_MSG((int)spaced->glyph_count, 3, "one glyph per codepoint");

    const FlGlyph* pg = (const FlGlyph*)(packed + 1);
    const FlGlyph* sg = (const FlGlyph*)(spaced + 1);

    // Compared against the unspaced run so no font metric is baked in: each interior pen step grows by exactly
    // the spacing, and the run's drawn extent by n-1 of them.
    ASSERT_NEAR_MSG(pg[1].x - pg[0].x, adv, 0.02f, "an unspaced run steps by the bare advance");
    ASSERT_NEAR_MSG(sg[1].x - sg[0].x, adv + 5.0f, 0.02f, "the forwarded spacing lands on the first pen step");
    ASSERT_NEAR_MSG(sg[2].x - sg[1].x, adv + 5.0f, 0.02f, "and on every later one");
    const float packed_extent = pg[2].x - pg[0].x;
    ASSERT_NEAR_MSG(sg[2].x - sg[0].x, packed_extent + 10.0f, 0.0001f, "n−1 gaps across a three-glyph run");

    // Both runs start at the element's left edge, so spacing does not shift the whole string.
    ASSERT_NEAR_MSG(pg[0].x, 10.0f, 0.0001f, "an unspaced run starts at the bounding box");
    ASSERT_NEAR_MSG(sg[0].x, 10.0f, 0.0001f, "and a spaced one starts in the same place");

    fl_arena_destroy(out_spaced);
    fl_arena_destroy(out_packed);
    font_cache_destroy(cache);
}

// The painter's own text draws also convert authored sizes to physical ones, and they do not go through Clay, so
// the label path above does not cover them. ui_scale is held at 1 so that a size taken from the coordinate scale
// and not the type scale shows up as wrong.
UTEST(render_ir, painter_text_size_and_spacing_follow_text_scale) {
    FlArena* arena = arena_new();
    FlFontCache* cache = font_cache_new(s_arena);
    FlFontId font = load_glyph_test_font(cache);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "memory-loaded font must be ready synchronously");

    Painter* p = test_painter_begin(arena, cache, 1.0f, 2.0f);

    // painter_text: the authored 32px handle rasterizes at 64px.
    void* slot_plain = nullptr;
    painter_scope_push(p, &slot_plain);
    painter_text(p, 0.0f, 0.0f, FL_S("A"), font, ui_color(255, 255, 255, 255));
    painter_scope_pop(p);
    const FlCmdHeader* hp = span_first_record(slot_plain);
    ASSERT_TRUE_MSG(hp != nullptr, "painter_text attached a paint span");
    ASSERT_EQ_MSG((int)hp->kind, (int)FlCmd_TextRun, "painter_text emits one text run");
    ASSERT_EQ_MSG((int)((const FlCmdTextRun*)(hp + 1))->size, (int)font_cache_whole_px_to_q4(64),
                  "the authored load size is multiplied by text_scale, not drawn raw");

    // painter_text_styled: an explicit 7.5px authored size resolves to a 15px key, and the 1.25px authored
    // spacing to 2.5px between the pens.
    const FlTextStyle style = { .font_id = font, .font_size = 7.5f, .letter_spacing = 1.25f };
    void* slot = paint_styled_text(p, (FlVec2) { .x = 10.0f, .y = 20.0f }, FL_S("AA"), style);

    const FlCmdHeader* h = span_first_record(slot);
    ASSERT_TRUE_MSG(h != nullptr, "styled text attached a paint span");
    const FlCmdTextRun* run = (const FlCmdTextRun*)(h + 1);
    ASSERT_EQ_MSG((int)run->size, 60, "7.5px authored at text_scale 2 carries the 15px quarter-px key");
    ASSERT_EQ_MSG((int)run->glyph_count, 2, "one glyph per codepoint");

    const FlGlyph* glyphs = (const FlGlyph*)(run + 1);
    float adv = (float)font_cache_get_glyph_advance_q4(cache, font, 60, 'A') / 64.0f;
    ASSERT_NEAR_MSG(glyphs[1].x - glyphs[0].x, adv + 2.5f, 0.0001f,
                    "the advance is taken at the scaled size and the authored spacing scales with it");

    // ui_scale is 1 here, so the pen still starts at the authored origin. The type scale does not apply to
    // coordinates.
    ASSERT_NEAR_MSG(glyphs[0].x, 10.0f, 0.0001f, "text_scale does not move the pen origin");

    // font_size 0 falls back to the font's authored load size, which scales the same way (32 to 64).
    void* slot_default = paint_styled_text(p, (FlVec2) { 0 }, FL_S("A"), (FlTextStyle) { .font_id = font });
    const FlCmdHeader* hd = span_first_record(slot_default);
    ASSERT_TRUE_MSG(hd != nullptr, "size 0 still draws");
    ASSERT_EQ_MSG((int)((const FlCmdTextRun*)(hd + 1))->size, (int)font_cache_quantize_size_q4(64.0f),
                  "the load-size fallback is scaled too");

    fl_arena_destroy(arena);
    font_cache_destroy(cache);
}
