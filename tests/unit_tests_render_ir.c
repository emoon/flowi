// render-IR build and lowering tests

#include "unit_tests_common.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render_ir: the styled text-run layout (fl_ir_text_layout at a fractional quarter-px key) agrees with
// font_cache_measure_text - same quantizer, same per-glyph advances, same n−1 letter-spacing rule.

UTEST(render_ir, styled_text_layout_matches_measurement) {
    FlFontCache* cache = font_cache_new(s_arena);
    FlFontId font = font_cache_load_font_memory(cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "memory-loaded font must be ready synchronously");

    const float font_size = 7.5f;
    const float spacing = 1.25f;
    const u16 q4 = font_cache_quantize_size_q4(font_size);
    ASSERT_EQ_MSG((int)q4, 30, "7.5px resolves exactly in quarter-px steps");

    FlArena* ir = arena_new();
    FlArena* scratch = arena_new();
    void* start = fl_arena_current_pos(ir);
    FlString aaa = { .data = "AAA", .length = 3, .is_static = 1, .is_ascii = 1 };
    fl_ir_text_layout(ir, scratch, cache, font, q4, 1.0f, (FlColor) { 255, 255, 255, 255 }, aaa, 10.0f, 20.0f, spacing);

    const FlCmdHeader* h = (const FlCmdHeader*)start;
    ASSERT_EQ_MSG((int)h->kind, (int)FlCmd_TextRun, "the layout emits one text run");
    const FlCmdTextRun* run = (const FlCmdTextRun*)(h + 1);
    ASSERT_EQ_MSG((int)run->size, (int)q4, "the run carries the quarter-px size key");
    ASSERT_EQ_MSG((int)run->glyph_count, 3, "one glyph per codepoint");
    const FlGlyph* glyphs = (const FlGlyph*)(run + 1);

    float adv = (float)font_cache_get_glyph_advance_q4(cache, font, q4, 'A') / 64.0f;
    ASSERT_TRUE_MSG(adv > 0.0f, "the fractional advance is non-zero");
    ASSERT_EQ_MSG(font_cache_get_glyph_advance_q4(cache, 9999, q4, 'A'), 0, "an unknown font advances zero");
    ASSERT_NEAR_MSG(glyphs[1].x - glyphs[0].x, adv + spacing, 0.0001f, "each pen step adds advance + spacing");
    ASSERT_NEAR_MSG(glyphs[2].x - glyphs[1].x, adv + spacing, 0.0001f, "each pen step adds advance + spacing");

    // A dropped n−1 guard would offset the first pen while leaving the deltas/extent checks unchanged.
    ASSERT_NEAR_MSG(glyphs[0].x, 10.0f, 0.0001f, "the first glyph has no leading spacing gap");

    FlTextMetrics m = font_cache_measure_text(cache, aaa, font, font_size, spacing);
    ASSERT_NEAR_MSG((glyphs[2].x + adv) - glyphs[0].x, m.width, 0.0001f, "draw and measure agree on the width");

    fl_arena_destroy(scratch);
    fl_arena_destroy(ir);
    font_cache_destroy(cache);
}

// The painter styled draw: positions scale with ui_scale while the type size and spacing do not - they
// answer to text_scale instead, which this frame holds at 1.
UTEST(render_ir, painter_text_styled_emits_quarter_px_run) {
    FlArena* arena = arena_new();
    FlFontCache* cache = font_cache_new(s_arena);
    FlFontId font = font_cache_load_font_memory(cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "memory-loaded font must be ready synchronously");

    Painter* p = painter_create(arena);
    painter_begin_frame(p, cache, 2.0f, 1.0f, arena);

    const FlTextStyle style = { .font_id = font, .font_size = 7.5f, .letter_spacing = 1.25f };
    void* slot = nullptr;
    painter_scope_push(p, &slot);
    painter_text_styled(p, 10.0f, 20.0f, FL_S("AA"), style, ui_color(255, 255, 255, 255));
    painter_scope_pop(p);

    const FlPaintSpan* span = (const FlPaintSpan*)slot;
    ASSERT_TRUE_MSG(span != nullptr && span->first != nullptr, "styled text attached a paint span");
    const FlCmdHeader* h = (const FlCmdHeader*)span->first->ir;
    ASSERT_EQ_MSG((int)h->kind, (int)FlCmd_TextRun, "styled text emits one text run");
    const FlCmdTextRun* run = (const FlCmdTextRun*)(h + 1);
    ASSERT_EQ_MSG((int)run->size, 30, "the run carries the 7.5px quarter-px key");
    ASSERT_EQ_MSG((int)run->font, (int)font, "the run carries the styled font");
    ASSERT_EQ_MSG((int)run->glyph_count, 2, "one glyph per codepoint");

    const FlGlyph* glyphs = (const FlGlyph*)(run + 1);
    float ascent = font_cache_get_font_metrics(cache, font, 7.5f).ascent;
    ASSERT_NEAR_MSG(glyphs[0].x, 20.0f, 0.0001f, "the pen x is the ui-scaled origin");
    ASSERT_NEAR_MSG(glyphs[0].y, 40.0f + ascent, 0.0001f, "the baseline is the ui-scaled y plus the ascent");
    float adv = (float)font_cache_get_glyph_advance_q4(cache, font, 30, 'A') / 64.0f;
    ASSERT_NEAR_MSG(glyphs[1].x - glyphs[0].x, adv + 1.25f, 0.0001f,
                    "advance and spacing stay unscaled, matching measurement");

    // font_size 0 falls back to the font's default size (loaded at 32 -> key 128).
    void* slot_default = nullptr;
    painter_scope_push(p, &slot_default);
    painter_text_styled(p, 0.0f, 0.0f, FL_S("A"), (FlTextStyle) { .font_id = font }, ui_color(255, 255, 255, 255));
    painter_scope_pop(p);
    const FlPaintSpan* span_default = (const FlPaintSpan*)slot_default;
    ASSERT_TRUE_MSG(span_default != nullptr && span_default->first != nullptr, "size 0 still draws");
    const FlCmdTextRun* run_default = (const FlCmdTextRun*)((const FlCmdHeader*)span_default->first->ir + 1);
    ASSERT_EQ_MSG((int)run_default->size, 128, "size 0 resolves to the font's default size key");

    void* slot_unknown = nullptr;
    painter_scope_push(p, &slot_unknown);
    painter_text_styled(p, 0.0f, 0.0f, FL_S("A"), (FlTextStyle) { .font_id = 9999 }, ui_color(255, 255, 255, 255));
    painter_scope_pop(p);
    ASSERT_TRUE_MSG(slot_unknown == nullptr, "an unknown font attaches no paint span");

    font_cache_destroy(cache);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// render_ir: optional-tier emission and SW-backend skip (ui/render_ir_build.h, sr_ir_to_render_command.c).
// The builders are verified through a full walk of the emitted byte stream. The painter case proves the
// reference-space-to-pixel scaling happens at emit (unitless angles/intensity pass through). The SW case proves
// the translator steps over every optional-tier record via byte_size.

// Advance to the next record per the frozen walk contract.
static const FlCmdHeader* ir_next(const FlCmdHeader* h) {
    return (const FlCmdHeader*)((const u8*)h + h->byte_size);
}

UTEST(render_ir, optional_tier_builders_round_trip) {
    FlArena* arena = arena_new();
    fl_arena_align(arena, 8);
    const u8* start = (const u8*)fl_arena_current_pos(arena);

    FlColor stroke = { 10, 20, 30, 255 };
    FlColor fill = { 40, 50, 60, 128 };
    FlEffect glow = { .kind = FlEffectKind_Glow, .color = { 1, 2, 3, 4 }, .off_x = 0.0f, .off_y = 0.0f, .blur = 6.0f };

    FlPolyPoint* pts
        = fl_ir_polyline(arena, stroke, fill, 2.0f, 100.0f, FlPolylineFlags_filled | FlPolylineFlags_closed, 3, &glow);
    pts[0] = (FlPolyPoint) { 0.0f, 8.0f, 1.0f };
    pts[1] = (FlPolyPoint) { 10.0f, 5.0f, 0.5f };
    pts[2] = (FlPolyPoint) { 20.0f, 8.0f, 1.0f };
    fl_ir_arc(arena, 50.0f, 60.0f, 24.0f, 4.0f, -2.0f, 4.0f, stroke, FlArcFlags_round_caps, nullptr);
    fl_ir_circle(arena, 1.0f, 2.0f, 3.0f, stroke, fill, 1.5f, nullptr);
    fl_ir_diamond(arena, 4.0f, 5.0f, 6.0f, 7.0f, stroke, fill, 0.0f, nullptr);
    FlBar* bars = fl_ir_bars(arena, 200.0f, stroke, 2, &glow);
    bars[0] = (FlBar) { 0.0f, 4.0f, 50.0f };
    bars[1] = (FlBar) { 6.0f, 4.0f, -10.0f };

    const u8* end = (const u8*)fl_arena_current_pos(arena);

    const FlCmdHeader* h = (const FlCmdHeader*)start;
    ASSERT_TRUE_MSG(h->kind == FlCmd_Polyline, "first record is the polyline");
    ASSERT_TRUE_MSG((h->flags & FL_HAS_EFFECT) != 0, "polyline carries its effect flag");
    ASSERT_TRUE_MSG((h->byte_size & 7u) == 0, "byte_size keeps the 8-byte record stride");
    const FlCmdPolyline* pl = (const FlCmdPolyline*)(h + 1);
    ASSERT_TRUE_MSG(pl->flags == (FlPolylineFlags_filled | FlPolylineFlags_closed), "polyline flags round-trip");
    ASSERT_TRUE_MSG(pl->width == 2.0f && pl->baseline_y == 100.0f, "polyline scalars round-trip");
    ASSERT_TRUE_MSG(pl->color.r == 10 && pl->fill_color.a == 128, "polyline colors round-trip");
    ASSERT_TRUE_MSG(pl->point_count == 3, "polyline point count round-trips");
    const FlPolyPoint* rpts = (const FlPolyPoint*)(pl + 1);
    ASSERT_TRUE_MSG(rpts[1].x == 10.0f && rpts[1].y == 5.0f, "inline points round-trip");
    ASSERT_TRUE_MSG(rpts[1].intensity == 0.5f, "per-point intensity round-trips");
    const FlEffect* fx = (const FlEffect*)(rpts + 3);
    ASSERT_TRUE_MSG(fx->kind == FlEffectKind_Glow && fx->blur == 6.0f, "effect follows the trailing point array");
    ASSERT_TRUE_MSG((const u8*)(fx + 1) <= (const u8*)h + h->byte_size, "effect lands inside the record");

    h = ir_next(h);
    ASSERT_TRUE_MSG(h->kind == FlCmd_Arc, "second record is the arc");
    ASSERT_TRUE_MSG((h->flags & FL_HAS_EFFECT) == 0, "no effect flag without an effect");
    const FlCmdArc* arc = (const FlCmdArc*)(h + 1);
    ASSERT_TRUE_MSG(arc->cx == 50.0f && arc->cy == 60.0f, "arc center round-trips");
    ASSERT_TRUE_MSG(arc->radius == 24.0f && arc->thickness == 4.0f, "arc radius/thickness round-trip");
    ASSERT_TRUE_MSG(arc->start_angle == -2.0f && arc->sweep == 4.0f, "arc angles round-trip");
    ASSERT_TRUE_MSG(arc->flags == FlArcFlags_round_caps, "arc flags round-trip");

    h = ir_next(h);
    ASSERT_TRUE_MSG(h->kind == FlCmd_Circle, "third record is the circle");
    const FlCmdCircle* circle = (const FlCmdCircle*)(h + 1);
    ASSERT_TRUE_MSG(circle->radius == 3.0f && circle->border_width == 1.5f, "circle fields round-trip");

    h = ir_next(h);
    ASSERT_TRUE_MSG(h->kind == FlCmd_Diamond, "fourth record is the diamond");
    const FlCmdDiamond* diamond = (const FlCmdDiamond*)(h + 1);
    ASSERT_TRUE_MSG(diamond->rx == 6.0f && diamond->ry == 7.0f, "diamond half-extents round-trip");
    ASSERT_TRUE_MSG(diamond->border_width == 0.0f, "borderless diamond round-trips");

    h = ir_next(h);
    ASSERT_TRUE_MSG(h->kind == FlCmd_Bar, "fifth record is the bar set");
    ASSERT_TRUE_MSG((h->flags & FL_HAS_EFFECT) != 0, "bar set carries its effect flag");
    const FlCmdBarSet* bar_set = (const FlCmdBarSet*)(h + 1);
    ASSERT_TRUE_MSG(bar_set->baseline_y == 200.0f && bar_set->bar_count == 2, "bar-set payload round-trips");
    const FlBar* rbars = (const FlBar*)(bar_set + 1);
    ASSERT_TRUE_MSG(rbars[0].h == 50.0f && rbars[1].h == -10.0f, "inline bars round-trip (negative h draws down)");
    fx = (const FlEffect*)(rbars + 2);
    ASSERT_TRUE_MSG(fx->kind == FlEffectKind_Glow, "effect follows the trailing bar array");

    // The walk terminates a `p < end` loop: it reaches the stream end, overshooting only by the final
    // record's 8-byte-stride rounding (the arena is not padded after the last record).
    h = ir_next(h);
    ASSERT_TRUE_MSG((const u8*)h >= end && (const u8*)h - end < 8, "walk lands on the stream end");

    fl_arena_destroy(arena);
}

UTEST(render_ir, painter_shapes_scale_at_emit) {
    FlArena* arena = arena_new();
    Painter* p = painter_create(arena);
    // ui_scale 2.0 makes scaled vs unscaled fields unambiguous. No font cache needed for shapes.
    painter_begin_frame(p, nullptr, 2.0f, 1.0f, arena);

    // Open one painting scope (no Clay needed); the painter attaches the span to this local customData slot.
    void* slot = nullptr;
    painter_scope_push(p, &slot);

    FlColor color = { 200, 100, 50, 255 };
    FlColor border = { 1, 2, 3, 4 };
    FlPolyPoint pts[2] = { { 1.0f, 2.0f, 1.0f }, { 3.0f, 4.0f, 0.25f } };
    painter_polyline(p, pts, 2, 1.5f, color, FlPolylineFlags_filled, border, 50.0f);
    painter_arc(p, 10.0f, 20.0f, 30.0f, 5.0f, 1.0f, 2.5f, color, 0);
    painter_circle_stroked(p, 1.0f, 2.0f, 3.0f, color, border, 0.5f, (FlStrokeStyle) { 1.5f, 2.5f });
    painter_diamond(p, 4.0f, 5.0f, 6.0f, 7.0f, color, border, 0.0f);
    FlBar in_bars[2] = { { 0.0f, 2.0f, 10.0f }, { 3.0f, 2.0f, -4.0f } };
    painter_bars(p, 100.0f, in_bars, 2, color);

    painter_scope_pop(p);

    // The scope's span is one coalesced segment (consecutive primitives, no nested child paint between them).
    const FlPaintSpan* span = (const FlPaintSpan*)slot;
    ASSERT_TRUE_MSG(span != nullptr, "first paint attached a span to the scope's slot");
    ASSERT_TRUE_MSG(span->first == span->last && span->first != nullptr && span->first->next == nullptr,
                    "consecutive painter calls coalesce into one span segment");
    const FlCmdHeader* h = (const FlCmdHeader*)span->first->ir;
    const u8* end = (const u8*)span->first->ir + span->first->size;

    ASSERT_TRUE_MSG(h->kind == FlCmd_Polyline, "painter emits the polyline");
    const FlCmdPolyline* pl = (const FlCmdPolyline*)(h + 1);
    ASSERT_TRUE_MSG(pl->width == 3.0f && pl->baseline_y == 100.0f, "polyline width/baseline scale");
    const FlPolyPoint* rpts = (const FlPolyPoint*)(pl + 1);
    ASSERT_TRUE_MSG(rpts[1].x == 6.0f && rpts[1].y == 8.0f, "points scale while copied into the record");
    ASSERT_TRUE_MSG(rpts[1].intensity == 0.25f, "intensity is unitless and does not scale");

    h = ir_next(h);
    ASSERT_TRUE_MSG(h->kind == FlCmd_Arc, "painter emits the arc");
    const FlCmdArc* arc = (const FlCmdArc*)(h + 1);
    ASSERT_TRUE_MSG(arc->cx == 20.0f && arc->radius == 60.0f && arc->thickness == 10.0f, "arc lengths scale");
    ASSERT_TRUE_MSG(arc->start_angle == 1.0f && arc->sweep == 2.5f, "arc angles are unitless and do not scale");

    h = ir_next(h);
    ASSERT_TRUE_MSG(h->kind == FlCmd_Circle, "painter emits the circle");
    const FlCmdCircle* circle = (const FlCmdCircle*)(h + 1);
    ASSERT_TRUE_MSG(circle->radius == 6.0f && circle->border_width == 1.0f, "circle radius/border scale");
    ASSERT_TRUE_MSG(circle->stroke.dash_len == 3.0f && circle->stroke.gap_len == 5.0f,
                    "dash lengths are lengths along the ring and scale with it");

    h = ir_next(h);
    ASSERT_TRUE_MSG(h->kind == FlCmd_Diamond, "painter emits the diamond");
    const FlCmdDiamond* diamond = (const FlCmdDiamond*)(h + 1);
    ASSERT_TRUE_MSG(diamond->rx == 12.0f && diamond->ry == 14.0f, "diamond half-extents scale");

    h = ir_next(h);
    ASSERT_TRUE_MSG(h->kind == FlCmd_Bar, "painter emits the bar set");
    const FlCmdBarSet* bar_set = (const FlCmdBarSet*)(h + 1);
    ASSERT_TRUE_MSG(bar_set->baseline_y == 200.0f, "bar baseline scales");
    const FlBar* rbars = (const FlBar*)(bar_set + 1);
    ASSERT_TRUE_MSG(rbars[1].x == 6.0f && rbars[1].w == 4.0f && rbars[1].h == -8.0f, "bars scale while copied");

    // Same termination contract as above: overshoot only by the final record's stride rounding.
    h = ir_next(h);
    ASSERT_TRUE_MSG((const u8*)h >= end && (const u8*)h - end < 8, "painter stream walk lands on its end");
}

// The painter's effect is scoped state: every later primitive in the element it was set in carries it, a
// zeroed effect clears it, and it reaches neither a nested child, nor a sibling once the scope pops, nor the
// next frame.
UTEST(render_ir, painter_effect_is_scoped_state) {
    FlArena* arena = arena_new();
    Painter* p = painter_create(arena);
    painter_begin_frame(p, nullptr, 1.0f, 1.0f, arena);

    FlColor color = { 10, 20, 30, 255 };
    // The plugin frame's box-shadow: 0 24px 70px #000.
    FlEffect shadow = { .kind = FlEffectKind_DropShadow, .color = { 0, 0, 0, 255 }, .off_y = 24.0f, .blur = 70.0f };

    void* outer_slot = nullptr;
    void* child_slot = nullptr;
    void* sibling_slot = nullptr;

    painter_scope_push(p, &outer_slot);
    painter_set_effect(p, shadow);
    painter_rect(p, 0.0f, 0.0f, 10.0f, 10.0f, color);
    painter_hline(p, 0.0f, 12.0f, 10.0f, color);

    painter_scope_push(p, &child_slot);
    painter_rect(p, 0.0f, 0.0f, 4.0f, 4.0f, color);
    painter_scope_pop(p);

    painter_circle(p, 1.0f, 1.0f, 2.0f, color, color, 0.0f);
    painter_set_effect(p, (FlEffect) { 0 });
    painter_rect(p, 0.0f, 0.0f, 2.0f, 2.0f, color);
    painter_scope_pop(p);

    painter_scope_push(p, &sibling_slot);
    painter_rect(p, 0.0f, 0.0f, 3.0f, 3.0f, color);
    painter_scope_pop(p);

    const FlPaintSpan* outer = (const FlPaintSpan*)outer_slot;
    ASSERT_TRUE_MSG(outer != nullptr && outer->first != nullptr && outer->first->next != nullptr,
                    "the child's paint splits the outer span into two segments");

    const FlCmdHeader* h = (const FlCmdHeader*)outer->first->ir;
    ASSERT_TRUE_MSG(h->kind == FlCmd_FillRect && (h->flags & FL_HAS_EFFECT) != 0,
                    "a rect drawn after set_effect carries the effect flag");
    const FlEffect* fx = (const FlEffect*)((const FlCmdFillRect*)(h + 1) + 1);
    ASSERT_TRUE_MSG(fx->kind == FlEffectKind_DropShadow && fx->off_y == 24.0f && fx->blur == 70.0f,
                    "the effect follows the fill-rect payload");

    h = ir_next(h);
    ASSERT_TRUE_MSG(h->kind == FlCmd_FillRect && (h->flags & FL_SNAP_DEVICE_PX) != 0 && (h->flags & FL_HAS_EFFECT) != 0,
                    "a device-snapped hairline carries the effect too");

    h = (const FlCmdHeader*)outer->first->next->ir;
    ASSERT_TRUE_MSG(h->kind == FlCmd_Circle && (h->flags & FL_HAS_EFFECT) != 0,
                    "the effect outlives a nested child scope");
    h = ir_next(h);
    ASSERT_TRUE_MSG(h->kind == FlCmd_FillRect && (h->flags & FL_HAS_EFFECT) == 0, "a zeroed effect clears the state");

    const FlPaintSpan* child = (const FlPaintSpan*)child_slot;
    ASSERT_TRUE_MSG(child != nullptr && child->first != nullptr, "the child scope recorded its own span");
    ASSERT_TRUE_MSG((((const FlCmdHeader*)child->first->ir)->flags & FL_HAS_EFFECT) == 0,
                    "a nested child does not inherit the effect");

    const FlPaintSpan* sibling = (const FlPaintSpan*)sibling_slot;
    ASSERT_TRUE_MSG(sibling != nullptr && sibling->first != nullptr, "the sibling scope recorded its own span");
    ASSERT_TRUE_MSG((((const FlCmdHeader*)sibling->first->ir)->flags & FL_HAS_EFFECT) == 0,
                    "the effect is gone once the scope it was set in pops");

    // Frame boundary: begin_frame drops every open scope, so an effect left set cannot reach the next frame.
    // It also rewinds the IR arena, which invalidates every record read above.
    void* leaked_slot = nullptr;
    painter_scope_push(p, &leaked_slot);
    painter_set_effect(p, shadow);
    painter_begin_frame(p, nullptr, 1.0f, 1.0f, arena);

    void* next_frame_slot = nullptr;
    painter_scope_push(p, &next_frame_slot);
    painter_rect(p, 0.0f, 0.0f, 5.0f, 5.0f, color);
    painter_scope_pop(p);

    const FlPaintSpan* next_frame = (const FlPaintSpan*)next_frame_slot;
    ASSERT_TRUE_MSG(next_frame != nullptr && next_frame->first != nullptr, "the next frame recorded its own span");
    ASSERT_TRUE_MSG((((const FlCmdHeader*)next_frame->first->ir)->flags & FL_HAS_EFFECT) == 0,
                    "begin_frame clears the effect");

    fl_arena_destroy(arena);
}

// painter_rect_styled reaches the whole fill-rect command at once: per-corner radii (scaled like every other
// length) and four differing corner colors in TL, TR, BL, BR order. The narrower calls funnel through it.
UTEST(render_ir, painter_rect_styled_emits_per_corner_radius_and_gradient) {
    FlArena* arena = arena_new();
    Painter* p = painter_create(arena);
    painter_begin_frame(p, nullptr, 2.0f, 1.0f, arena);

    FlColor tl = { 1, 0, 0, 255 };
    FlColor tr = { 0, 2, 0, 255 };
    FlColor bl = { 0, 0, 3, 255 };
    FlColor br = { 4, 4, 4, 255 };

    void* slot = nullptr;
    painter_scope_push(p, &slot);
    // The limiter meter: rounded top, square bottom, vertical gradient.
    painter_set_effect(p, (FlEffect) { .kind = FlEffectKind_Glow, .color = { 9, 9, 9, 255 }, .blur = 3.0f });
    painter_rect_styled(p, 10.0f, 20.0f, 30.0f, 40.0f,
                        (FlRectConfig) { .radius = { 5.0f, 6.0f, 0.0f, 0.0f }, .corner = { tl, tr, bl, br } });
    painter_set_effect(p, (FlEffect) { 0 });
    painter_rect_rounded(p, 0.0f, 0.0f, 8.0f, 8.0f, 3.0f, tl);
    painter_scope_pop(p);

    const FlPaintSpan* span = (const FlPaintSpan*)slot;
    ASSERT_TRUE_MSG(span != nullptr && span->first != nullptr, "rect_styled attached a paint span");
    const FlCmdHeader* h = (const FlCmdHeader*)span->first->ir;
    ASSERT_TRUE_MSG(h->kind == FlCmd_FillRect, "a styled rect is the core fill rect, not a new command kind");
    const FlCmdFillRect* fr = (const FlCmdFillRect*)(h + 1);
    ASSERT_TRUE_MSG(fr->rect.x == 20.0f && fr->rect.y == 40.0f && fr->rect.w == 60.0f && fr->rect.h == 80.0f,
                    "geometry scales at emit");
    ASSERT_TRUE_MSG(fr->radius.tl == 10.0f && fr->radius.tr == 12.0f && fr->radius.bl == 0.0f && fr->radius.br == 0.0f,
                    "each corner radius scales independently and a zero stays square");
    ASSERT_TRUE_MSG(fr->corner[0].r == 1 && fr->corner[1].g == 2 && fr->corner[2].b == 3 && fr->corner[3].r == 4,
                    "corner colors keep TL, TR, BL, BR order");
    ASSERT_TRUE_MSG((h->flags & FL_HAS_EFFECT) != 0, "a styled rect carries the scope's effect");

    h = ir_next(h);
    ASSERT_TRUE_MSG(h->kind == FlCmd_FillRect && (h->flags & FL_HAS_EFFECT) == 0,
                    "rect_rounded still emits a plain fill rect");
    const FlCmdFillRect* rr = (const FlCmdFillRect*)(h + 1);
    ASSERT_TRUE_MSG(rr->radius.tl == 6.0f && rr->radius.tr == 6.0f && rr->radius.bl == 6.0f && rr->radius.br == 6.0f,
                    "rect_rounded keeps its uniform scaled radius through the shared emitter");
    ASSERT_TRUE_MSG(rr->corner[0].r == 1 && rr->corner[3].r == 1, "rect_rounded is four equal corner colors");

    fl_arena_destroy(arena);
}

UTEST(render_ir, painter_image_sub_emits_source_rect_and_scaled_dst) {
    FlArena* arena = arena_new();
    ImageRegistry* registry = image_registry_new(arena);
    image_registry_mark_realized((ImageHandle)1234, 16, 8);

    Painter* p = painter_create(arena);
    painter_begin_frame(p, nullptr, 2.0f, 1.0f, arena);

    void* slot = nullptr;
    painter_scope_push(p, &slot);
    painter_set_effect(p, (FlEffect) { .kind = FlEffectKind_Glow, .color = { 1, 2, 3, 4 }, .blur = 6.0f });
    ASSERT_TRUE_MSG(painter_image_sub(p, 3.0f, 4.0f, 12.0f, 6.0f, 2.0f, 1.0f, 4.0f, 3.0f, (FlImageHandle)1234,
                                      ui_color(10, 20, 30, 40)),
                    "painter_image_sub records for a ready image");
    painter_scope_pop(p);

    const FlPaintSpan* span = (const FlPaintSpan*)slot;
    ASSERT_TRUE_MSG(span != nullptr && span->first != nullptr, "image_sub attached a paint span");
    const FlCmdHeader* h = (const FlCmdHeader*)span->first->ir;
    ASSERT_TRUE_MSG(h->kind == FlCmd_Image, "image_sub emits image IR");
    const FlCmdImage* im = (const FlCmdImage*)(h + 1);
    ASSERT_TRUE_MSG(im->dst.x == 6.0f && im->dst.y == 8.0f && im->dst.w == 24.0f && im->dst.h == 12.0f,
                    "destination rect scales at emit");
    ASSERT_TRUE_MSG(im->src.x == 2.0f && im->src.y == 1.0f && im->src.w == 4.0f && im->src.h == 3.0f,
                    "source rect stays in image pixels");
    ASSERT_TRUE_MSG((h->flags & FL_HAS_EFFECT) != 0, "the image record carries the scope's effect flag");
    const FlEffect* fx = (const FlEffect*)(im + 1);
    ASSERT_TRUE_MSG(fx->kind == FlEffectKind_Glow && fx->blur == 6.0f, "the effect follows the image payload");

    image_registry_destroy(registry);
    fl_arena_destroy(arena);
}

// painter_hline/painter_vline emit a CORE fill rect (FlCmd_FillRect - software-backend visible, not the
// optional-tier polyline) tagged FL_SNAP_DEVICE_PX, with the long axis scaled and the thin axis a whole device
// pixel (>= 1). The position snap itself happens later at the splice; here we pin the emit contract.
UTEST(render_ir, painter_lines_emit_snapped_core_fill_rect) {
    FlArena* arena = arena_new();
    Painter* p = painter_create(arena);
    // ui_scale 1.25: the thin axis snaps to a single whole device pixel while the long axis scales fractionally.
    painter_begin_frame(p, nullptr, 1.25f, 1.0f, arena);

    void* slot = nullptr;
    painter_scope_push(p, &slot);
    FlColor col = { 200, 100, 50, 255 };
    painter_hline(p, 4.0f, 10.0f, 100.0f, col);
    painter_vline(p, 20.0f, 5.0f, 80.0f, col);
    painter_scope_pop(p);

    const FlPaintSpan* span = (const FlPaintSpan*)slot;
    ASSERT_TRUE_MSG(span != nullptr && span->first != nullptr, "lines attached a span to the scope's slot");
    const FlCmdHeader* h = (const FlCmdHeader*)span->first->ir;

    ASSERT_TRUE_MSG(h->kind == FlCmd_FillRect, "hline is a core fill rect, not the optional-tier polyline");
    ASSERT_TRUE_MSG((h->flags & FL_SNAP_DEVICE_PX) != 0, "hline is tagged for device-pixel snapping");
    const FlCmdFillRect* hl = (const FlCmdFillRect*)(h + 1);
    ASSERT_TRUE_MSG(hl->rect.x == 5.0f && hl->rect.y == 12.5f, "hline position scales (4,10 * 1.25)");
    ASSERT_TRUE_MSG(hl->rect.w == 125.0f, "hline length scales along x (100 * 1.25)");
    ASSERT_TRUE_MSG(hl->rect.h == 1.0f, "hline thickness is one whole device pixel at 1.25 scale");
    ASSERT_TRUE_MSG(hl->radius.tl == 0.0f && hl->radius.br == 0.0f, "a hairline has no corner radius");
    ASSERT_TRUE_MSG(hl->corner[0].r == 200 && hl->corner[3].r == 200, "solid color in all four corners");

    h = ir_next(h);
    ASSERT_TRUE_MSG(h->kind == FlCmd_FillRect && (h->flags & FL_SNAP_DEVICE_PX) != 0,
                    "vline is a snapped core fill rect too");
    const FlCmdFillRect* vl = (const FlCmdFillRect*)(h + 1);
    ASSERT_TRUE_MSG(vl->rect.w == 1.0f, "vline thickness is one whole device pixel along x");
    ASSERT_TRUE_MSG(vl->rect.h == 100.0f, "vline length scales along y (80 * 1.25)");
    ASSERT_TRUE_MSG(vl->rect.x == 25.0f && vl->rect.y == 6.25f, "vline position scales (20,5 * 1.25)");

    fl_arena_destroy(arena);
}

// The splice rounds a snap-tagged fill rect to the device-pixel grid AFTER folding in the element origin, so a
// 1px line stays crisp when that origin carries a fractional scroll offset (scroll_offset is applied unsnapped as
// the element's translation). A plain, untagged fill rect at the same fractional origin is left fractional - the
// flag, not the splice, gates the snap.
UTEST(render_ir, snapped_line_splice_rounds_at_fractional_origin) {
    FlArena* arena = arena_new();
    Painter* p = painter_create(arena);
    // ui_scale 1.0 so element-local == device space, isolating the origin snap from any emit-time scaling.
    painter_begin_frame(p, nullptr, 1.0f, 1.0f, arena);

    FlColor col = { 40, 200, 120, 255 };

    void* snapped_slot = nullptr;
    painter_scope_push(p, &snapped_slot);
    painter_hline(p, 0.0f, 0.0f, 100.0f, col); // element-local rect {0,0,100,1}, snap-tagged
    painter_scope_pop(p);

    void* plain_slot = nullptr;
    painter_scope_push(p, &plain_slot);
    painter_rect(p, 0.0f, 0.0f, 100.0f, 1.0f, col); // element-local rect {0,0,100,1}, NOT tagged
    painter_scope_pop(p);

    // A fractional element origin - exactly what a fractional scroll offset produces.
    const Clay_BoundingBox bb = { 200.4f, 10.4f, 100.0f, 1.0f };

    // Snapped line: origin folded in, then rounded to whole device pixels.
    FlArena* out_snapped = arena_new();
    fl_arena_align(out_snapped, 8);
    const FlCmdHeader* hs = (const FlCmdHeader*)fl_arena_current_pos(out_snapped);
    Clay_RenderCommand snapped_cmd = { .commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM,
                                       .boundingBox = bb,
                                       .renderData = { .custom = { .customData = snapped_slot } } };
    fl_clay_to_render_ir(out_snapped, arena, nullptr, &snapped_cmd, 1);
    ASSERT_TRUE_MSG(hs->kind == FlCmd_FillRect, "snapped line splices to a fill rect");
    const FlCmdFillRect* fs = (const FlCmdFillRect*)(hs + 1);
    ASSERT_TRUE_MSG(fs->rect.x == 200.0f && fs->rect.y == 10.0f, "snapped to the device grid (round 200.4, 10.4)");
    ASSERT_TRUE_MSG(fs->rect.w == 100.0f && fs->rect.h == 1.0f, "snapped extent stays a 1px line");

    // Plain rect: same fractional origin, left fractional (the flag decides, not the splice).
    FlArena* out_plain = arena_new();
    fl_arena_align(out_plain, 8);
    const FlCmdHeader* hp = (const FlCmdHeader*)fl_arena_current_pos(out_plain);
    Clay_RenderCommand plain_cmd = { .commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM,
                                     .boundingBox = bb,
                                     .renderData = { .custom = { .customData = plain_slot } } };
    fl_clay_to_render_ir(out_plain, arena, nullptr, &plain_cmd, 1);
    const FlCmdFillRect* fp = (const FlCmdFillRect*)(hp + 1);
    ASSERT_TRUE_MSG(fp->rect.x == 200.4f && fp->rect.y == 10.4f, "an untagged rect keeps its fractional origin");

    fl_arena_destroy(out_plain);
    fl_arena_destroy(out_snapped);
    fl_arena_destroy(arena);
}

// Returns nullptr if the hop emitted something other than a text run. Whole-px font size, so the size key
// needs no quantizer.
static const FlCmdTextRun* clay_text_run_at_spacing(FlArena* out, FlArena* scratch, FlFontCache* cache, FlFontId font,
                                                    u16 font_size, u16 spacing) {
    static const char k_text[] = "AAA";
    fl_arena_align(out, 8);
    const FlCmdHeader* h = (const FlCmdHeader*)fl_arena_current_pos(out);
    Clay_RenderCommand cmd = {
        .commandType = CLAY_RENDER_COMMAND_TYPE_TEXT,
        .boundingBox = { 10.0f, 20.0f, 400.0f, 40.0f },
        .renderData = { .text = { .stringContents = { .length = 3, .chars = k_text, .baseChars = k_text },
                                  .textColor = { 255, 255, 255, 255 },
                                  .fontId = (uint16_t)font,
                                  .fontSize = font_size,
                                  .letterSpacing = spacing,
                                  .verticalAlignment = CLAY_TEXT_VALIGN_TOP } },
    };
    fl_clay_to_render_ir(out, scratch, cache, &cmd, 1);
    return h->kind == FlCmd_TextRun ? (const FlCmdTextRun*)(h + 1) : nullptr;
}

// Letter spacing must survive the Clay -> IR hop. Clay widens the text element whether or not the value is
// forwarded, so only the emitted pen positions can tell a forwarded value from a dropped one.
UTEST(render_ir, clay_text_forwards_letter_spacing_to_the_pen) {
    FlFontCache* cache = font_cache_new(s_arena);
    FlFontId font = font_cache_load_font_memory(cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "memory-loaded font must be ready synchronously");

    const u16 font_size = 32;
    const float adv
        = (float)font_cache_get_glyph_advance_q4(cache, font, font_cache_whole_px_to_q4(font_size), 'A') / 64.0f;
    ASSERT_TRUE_MSG(adv > 0.0f, "the advance is non-zero");

    FlArena* scratch = arena_new();
    FlArena* out_packed = arena_new();
    FlArena* out_spaced = arena_new();

    const FlCmdTextRun* packed = clay_text_run_at_spacing(out_packed, scratch, cache, font, font_size, 0);
    const FlCmdTextRun* spaced = clay_text_run_at_spacing(out_spaced, scratch, cache, font, font_size, 5);
    ASSERT_TRUE_MSG(packed != nullptr && spaced != nullptr, "each command emits one text run");
    ASSERT_EQ_MSG((int)packed->glyph_count, 3, "one glyph per codepoint");
    ASSERT_EQ_MSG((int)spaced->glyph_count, 3, "one glyph per codepoint");

    const FlGlyph* pg = (const FlGlyph*)(packed + 1);
    const FlGlyph* sg = (const FlGlyph*)(spaced + 1);

    // Stated against the unspaced run so no font metric is baked in: each interior pen step grows by exactly
    // the spacing, and the run's drawn extent by n−1 of them.
    ASSERT_NEAR_MSG(pg[1].x - pg[0].x, adv, 0.0001f, "an unspaced run steps by the bare advance");
    ASSERT_NEAR_MSG(sg[1].x - sg[0].x, adv + 5.0f, 0.0001f, "the forwarded spacing lands on the first pen step");
    ASSERT_NEAR_MSG(sg[2].x - sg[1].x, adv + 5.0f, 0.0001f, "and on every later one");
    ASSERT_NEAR_MSG(sg[2].x - sg[0].x, (pg[2].x - pg[0].x) + 10.0f, 0.0001f, "n−1 gaps across a three-glyph run");

    // No leading gap: both runs start at the element's left edge, so spacing never shifts the whole string.
    ASSERT_NEAR_MSG(pg[0].x, 10.0f, 0.0001f, "an unspaced run starts at the bounding box");
    ASSERT_NEAR_MSG(sg[0].x, 10.0f, 0.0001f, "and a spaced one starts in the same place");

    fl_arena_destroy(out_spaced);
    fl_arena_destroy(out_packed);
    fl_arena_destroy(scratch);
    font_cache_destroy(cache);
}

// A parent scope that paints, opens a child scope that paints, then paints again has a non-contiguous span: the
// child's record sits between the parent's two records in the shared arena, so the parent's span is two
// segments and the child's is one. Each scope gets its own CUSTOM span (one marker per scope).
UTEST(render_ir, painter_span_nesting_splits_segments) {
    FlArena* arena = arena_new();
    Painter* p = painter_create(arena);
    painter_begin_frame(p, nullptr, 1.0f, 1.0f, arena);

    const FlColor col = { 10, 20, 30, 255 };

    void* slot_parent = nullptr;
    void* slot_child = nullptr;

    painter_scope_push(p, &slot_parent);
    painter_rect(p, 1.0f, 0.0f, 1.0f, 1.0f, col); // parent record A (x == 1)
    painter_scope_push(p, &slot_child);
    painter_rect(p, 2.0f, 0.0f, 1.0f, 1.0f, col); // child record  (x == 2), interrupts the parent's run
    painter_scope_pop(p);
    painter_rect(p, 3.0f, 0.0f, 1.0f, 1.0f, col); // parent record B (x == 3)
    painter_scope_pop(p);

    const FlPaintSpan* parent = (const FlPaintSpan*)slot_parent;
    const FlPaintSpan* child = (const FlPaintSpan*)slot_child;
    ASSERT_TRUE_MSG(parent != nullptr && child != nullptr, "both scopes attached their own span");

    ASSERT_TRUE_MSG(parent->first != nullptr && parent->first->next == parent->last && parent->last->next == nullptr,
                    "the interrupted parent span is exactly two segments");
    const FlCmdFillRect* pa = (const FlCmdFillRect*)((const FlCmdHeader*)parent->first->ir + 1);
    const FlCmdFillRect* pb = (const FlCmdFillRect*)((const FlCmdHeader*)parent->last->ir + 1);
    ASSERT_TRUE_MSG(pa->rect.x == 1.0f && pb->rect.x == 3.0f, "parent segments hold its own records, in order");

    ASSERT_TRUE_MSG(child->first == child->last && child->first != nullptr && child->first->next == nullptr,
                    "the child span is one segment");
    const FlCmdFillRect* cc = (const FlCmdFillRect*)((const FlCmdHeader*)child->first->ir + 1);
    ASSERT_TRUE_MSG(cc->rect.x == 2.0f, "child segment holds only the child's record");

    fl_arena_destroy(arena);
}

UTEST(render_ir, sw_backend_skips_optional_tier) {
    FlArena* ir_arena = arena_new();
    FlArena* commands_arena = arena_new();
    FlArena* state_arena = arena_new();

    fl_arena_align(ir_arena, 8);
    const u8* start = (const u8*)fl_arena_current_pos(ir_arena);

    // Two core-tier fill rects sandwiching all five optional-tier kinds (one with a trailing array + effect,
    // the worst case for the byte_size step).
    FlColor c = { 255, 0, 0, 255 };
    FlColor corners[4] = { c, c, c, c };
    FlEffect glow = { .kind = FlEffectKind_Glow, .color = c, .blur = 4.0f };

    fl_ir_fill_rect(ir_arena, (FlRect) { 0.0f, 0.0f, 10.0f, 10.0f }, (FlCornerRadius) { 0 }, corners, nullptr);
    FlPolyPoint* pts = fl_ir_polyline(ir_arena, c, c, 1.0f, 0.0f, 0, 2, &glow);
    pts[0] = (FlPolyPoint) { 0.0f, 0.0f, 1.0f };
    pts[1] = (FlPolyPoint) { 1.0f, 1.0f, 1.0f };
    fl_ir_arc(ir_arena, 0.0f, 0.0f, 5.0f, 1.0f, 0.0f, 3.0f, c, 0, nullptr);
    fl_ir_circle(ir_arena, 0.0f, 0.0f, 2.0f, c, c, 0.0f, nullptr);
    fl_ir_diamond(ir_arena, 0.0f, 0.0f, 2.0f, 2.0f, c, c, 0.0f, nullptr);
    FlBar* bars = fl_ir_bars(ir_arena, 0.0f, c, 1, nullptr);
    bars[0] = (FlBar) { 0.0f, 1.0f, 1.0f };
    fl_ir_fill_rect(ir_arena, (FlRect) { 20.0f, 0.0f, 10.0f, 10.0f }, (FlCornerRadius) { 0 }, corners, nullptr);

    size_t bytes = (size_t)((const u8*)fl_arena_current_pos(ir_arena) - start);
    UiRenderCommandsArray out = sr_ir_to_render_commands(commands_arena, state_arena, nullptr, start, bytes, 255);

    ASSERT_TRUE_MSG(out.count == 2, "only the two core-tier fill rects translate");

    fl_arena_destroy(ir_arena);
    fl_arena_destroy(commands_arena);
    fl_arena_destroy(state_arena);
}

static int sw_translate_range(FlArena* ir_arena, FlArena* commands_arena, FlArena* state_arena, const u8* start) {
    size_t bytes = (size_t)((const u8*)fl_arena_current_pos(ir_arena) - start);
    return sr_ir_to_render_commands(commands_arena, state_arena, nullptr, start, bytes, 255).count;
}

// Per-primitive companion to sw_backend_skips_optional_tier: each kind is fed ALONE, so a primitive that gains
// SW support flips exactly its own assertion instead of moving an aggregate count.
UTEST(render_ir, sw_backend_drops_each_optional_tier) {
    FlArena* ir_arena = arena_new();
    FlArena* commands_arena = arena_new();
    FlArena* state_arena = arena_new();

    FlColor c = { 255, 0, 0, 255 };

    fl_arena_align(ir_arena, 8);
    const u8* start = (const u8*)fl_arena_current_pos(ir_arena);
    FlPolyPoint* pts = fl_ir_polyline(ir_arena, c, c, 1.0f, 0.0f, 0, 2, nullptr);
    pts[0] = (FlPolyPoint) { 0.0f, 0.0f, 1.0f };
    pts[1] = (FlPolyPoint) { 1.0f, 1.0f, 1.0f };
    ASSERT_TRUE_MSG(sw_translate_range(ir_arena, commands_arena, state_arena, start) == 0,
                    "Polyline is optional-tier: the SW backend drops it");

    fl_arena_align(ir_arena, 8);
    start = (const u8*)fl_arena_current_pos(ir_arena);
    fl_ir_arc(ir_arena, 0.0f, 0.0f, 5.0f, 1.0f, 0.0f, 3.0f, c, 0, nullptr);
    ASSERT_TRUE_MSG(sw_translate_range(ir_arena, commands_arena, state_arena, start) == 0,
                    "Arc is optional-tier: the SW backend drops it");

    fl_arena_align(ir_arena, 8);
    start = (const u8*)fl_arena_current_pos(ir_arena);
    fl_ir_circle(ir_arena, 0.0f, 0.0f, 2.0f, c, c, 0.0f, nullptr);
    ASSERT_TRUE_MSG(sw_translate_range(ir_arena, commands_arena, state_arena, start) == 0,
                    "Circle is optional-tier: the SW backend drops it");

    fl_arena_align(ir_arena, 8);
    start = (const u8*)fl_arena_current_pos(ir_arena);
    fl_ir_diamond(ir_arena, 0.0f, 0.0f, 2.0f, 2.0f, c, c, 0.0f, nullptr);
    ASSERT_TRUE_MSG(sw_translate_range(ir_arena, commands_arena, state_arena, start) == 0,
                    "Diamond is optional-tier: the SW backend drops it");

    fl_arena_align(ir_arena, 8);
    start = (const u8*)fl_arena_current_pos(ir_arena);
    FlBar* bars = fl_ir_bars(ir_arena, 0.0f, c, 1, nullptr);
    bars[0] = (FlBar) { 0.0f, 1.0f, 1.0f };
    ASSERT_TRUE_MSG(sw_translate_range(ir_arena, commands_arena, state_arena, start) == 0,
                    "Bar is optional-tier: the SW backend drops it");

    fl_arena_destroy(ir_arena);
    fl_arena_destroy(commands_arena);
    fl_arena_destroy(state_arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SW backend behavior is unchanged by an effect annotation: it never reads the effect, so a core fill rect
// carrying a drop shadow translates to the same render command as the same rect without one.

UTEST(render_ir, sw_backend_ignores_effect_on_core_command) {
    FlArena* ir_arena = arena_new();
    FlArena* commands_arena = arena_new();
    FlArena* state_arena = arena_new();

    FlColor c = { 200, 40, 50, 255 };
    FlColor corners[4] = { c, c, c, c };
    FlRect rect = { 5.0f, 6.0f, 30.0f, 20.0f };
    FlEffect shadow
        = { .kind = FlEffectKind_DropShadow, .color = { 0, 0, 0, 255 }, .off_x = 12.0f, .off_y = 8.0f, .blur = 5.0f };

    fl_arena_align(ir_arena, 8);
    const u8* plain_start = (const u8*)fl_arena_current_pos(ir_arena);
    fl_ir_fill_rect(ir_arena, rect, (FlCornerRadius) { 0 }, corners, nullptr);
    size_t plain_bytes = (size_t)((const u8*)fl_arena_current_pos(ir_arena) - plain_start);

    // The same rect, but with a drop shadow appended at the record tail (the core builder has no effect
    // parameter, so the record is assembled directly, as the painter's annotation path would).
    fl_arena_align(ir_arena, 8);
    const u8* fx_start = (const u8*)fl_arena_current_pos(ir_arena);
    FlCmdHeader* header = fl_ir_header(
        ir_arena, FlCmd_FillRect, (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdFillRect)) + fl_ir_effect_bytes(&shadow));
    FlCmdFillRect* fr = arena_alloc_zero(ir_arena, FlCmdFillRect);
    fr->rect = rect;
    fr->corner[0] = c;
    fr->corner[1] = c;
    fr->corner[2] = c;
    fr->corner[3] = c;
    fl_ir_append_effect(ir_arena, header, &shadow);
    size_t fx_bytes = (size_t)((const u8*)fl_arena_current_pos(ir_arena) - fx_start);

    UiRenderCommandsArray plain
        = sr_ir_to_render_commands(commands_arena, state_arena, nullptr, plain_start, plain_bytes, 255);
    UiRenderCommandsArray withfx
        = sr_ir_to_render_commands(commands_arena, state_arena, nullptr, fx_start, fx_bytes, 255);

    ASSERT_EQ(plain.count, 1);
    ASSERT_EQ(withfx.count, plain.count);
    const RenderCommand* a = &plain.commands[0];
    const RenderCommand* b = &withfx.commands[0];
    ASSERT_EQ((int)a->mask, (int)b->mask);
    ASSERT_TRUE_MSG(memcmp(&a->aabb, &b->aabb, sizeof a->aabb) == 0, "the effect must not move or resize the SW rect");
    ASSERT_TRUE_MSG(memcmp(&a->corner_colors, &b->corner_colors, sizeof a->corner_colors) == 0,
                    "the effect must not change the SW rect's colors");
    ASSERT_TRUE_MSG(a->radius == b->radius, "the effect must not change the SW rect's corner radius");

    fl_arena_destroy(ir_arena);
    fl_arena_destroy(commands_arena);
    fl_arena_destroy(state_arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SW and GPU IR decoders must agree on the two bits of intent-decode math they both do: the global_alpha
// fold (both round: (a*ga+127)/255) and the corner-radius clamp (both floor negatives to 0). A rect that trips
// either divergence would render differently on the two backends.

UTEST(render_ir, sw_gpu_decode_agrees) {
    FlArena* ir_arena = arena_new();
    FlArena* commands_arena = arena_new();
    FlArena* state_arena = arena_new();

    // --- global_alpha fold: a=200, ga=200 is a value where truncation (156) and rounding (157) differ. ---
    FlColor c = { 10, 20, 30, 200 };
    FlColor solid[4] = { c, c, c, c };
    FlRect rect = { 0.0f, 0.0f, 100.0f, 100.0f };

    fl_arena_align(ir_arena, 8);
    const u8* fill_start = (const u8*)fl_arena_current_pos(ir_arena);
    fl_ir_fill_rect(ir_arena, rect, (FlCornerRadius) { 0 }, solid, nullptr);
    size_t fill_bytes = (size_t)((const u8*)fl_arena_current_pos(ir_arena) - fill_start);

    UiRenderCommandsArray sw
        = sr_ir_to_render_commands(commands_arena, state_arena, nullptr, fill_start, fill_bytes, 200);
    ASSERT_EQ(sw.count, 1);
    u8 sw_rgba[16];
    u8x16_store_unaligned(sw.commands[0].corner_colors, sw_rgba, 0);
    int sw_alpha = sw_rgba[3]; // premultiply leaves the alpha channel as the folded alpha

    GpuDrawList gpu = gpu_ir_to_draw_cmds(commands_arena, fill_start, fill_bytes, 200, 200, 200, nullptr, nullptr,
                                          nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(gpu.count >= 1);
    int gpu_alpha = (int)((gpu.cmds[0].color[0] >> 24) & 0xFFu); // R low byte, A high byte

    ASSERT_EQ_MSG(sw_alpha, 157, "SW must round the global_alpha fold, not truncate");
    ASSERT_EQ_MSG(gpu_alpha, sw_alpha, "SW and GPU must fold global_alpha identically");

    // --- corner-radius clamp: a negative corner must decode as zero on both backends. (12,12,-3,0) trips the
    // SW edge-path predicates (has_only_top_corners requires bl==0), so an unfloored negative would take a
    // different split path than the same rect with the corner at 0. ---
    FlColor opaque = { 255, 255, 255, 255 };
    FlColor op4[4] = { opaque, opaque, opaque, opaque };
    FlCornerRadius neg_r = { .tl = 12.0f, .tr = 12.0f, .bl = -3.0f, .br = 0.0f };
    FlCornerRadius zero_r = { .tl = 12.0f, .tr = 12.0f, .bl = 0.0f, .br = 0.0f };

    fl_arena_align(ir_arena, 8);
    const u8* neg_start = (const u8*)fl_arena_current_pos(ir_arena);
    fl_ir_fill_rect(ir_arena, rect, neg_r, op4, nullptr);
    size_t neg_bytes = (size_t)((const u8*)fl_arena_current_pos(ir_arena) - neg_start);

    fl_arena_align(ir_arena, 8);
    const u8* zero_start = (const u8*)fl_arena_current_pos(ir_arena);
    fl_ir_fill_rect(ir_arena, rect, zero_r, op4, nullptr);
    size_t zero_bytes = (size_t)((const u8*)fl_arena_current_pos(ir_arena) - zero_start);

    UiRenderCommandsArray sw_neg
        = sr_ir_to_render_commands(commands_arena, state_arena, nullptr, neg_start, neg_bytes, 255);
    UiRenderCommandsArray sw_zero
        = sr_ir_to_render_commands(commands_arena, state_arena, nullptr, zero_start, zero_bytes, 255);
    ASSERT_EQ_MSG(sw_neg.count, sw_zero.count, "SW must clamp a negative corner radius to zero (same split as 0)");
    ASSERT_TRUE_MSG(memcmp(sw_neg.commands, sw_zero.commands, sizeof(RenderCommand) * (size_t)sw_neg.count) == 0,
                    "SW negative-radius decode must be byte-identical to the zero-radius decode");

    GpuDrawList gpu_neg = gpu_ir_to_draw_cmds(commands_arena, neg_start, neg_bytes, 255, 200, 200, nullptr, nullptr,
                                              nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(gpu_neg.count >= 1);
    ASSERT_TRUE_MSG(gpu_neg.cmds[0].radius[2] == 0.0f, "GPU must floor a negative corner radius to zero");

    fl_arena_destroy(ir_arena);
    fl_arena_destroy(commands_arena);
    fl_arena_destroy(state_arena);
}

// Dashing is GPU-tier: the software backend draws every border solid. FlCmdBorder carries 8 extra bytes for
// the pattern, so a stale offset would have the SW decode read the stroke into a width or a color.
UTEST(render_ir, sw_backend_ignores_a_border_dash_pattern) {
    FlArena* ir_arena = arena_new();
    FlArena* commands_arena = arena_new();
    FlArena* state_arena = arena_new();
    const FlColor color = { 200, 40, 50, 255 };
    const FlRect rect = { 10.0f, 10.0f, 40.0f, 40.0f };
    const FlCornerRadius radius = { .tl = 6.0f, .tr = 6.0f, .bl = 6.0f, .br = 6.0f };

    fl_arena_align(ir_arena, 8);
    const u8* solid = (const u8*)fl_arena_current_pos(ir_arena);
    fl_ir_border(ir_arena, rect, 4.0f, 3.0f, 2.0f, 1.0f, radius, color);
    size_t solid_bytes = (size_t)((const u8*)fl_arena_current_pos(ir_arena) - solid);

    fl_arena_align(ir_arena, 8);
    const u8* dashed = (const u8*)fl_arena_current_pos(ir_arena);
    fl_ir_border_stroked(ir_arena, rect, 4.0f, 3.0f, 2.0f, 1.0f, radius, color, (FlStrokeStyle) { 3.0f, 2.0f });
    size_t dashed_bytes = (size_t)((const u8*)fl_arena_current_pos(ir_arena) - dashed);

    UiRenderCommandsArray sw_solid
        = sr_ir_to_render_commands(commands_arena, state_arena, nullptr, solid, solid_bytes, 255);
    UiRenderCommandsArray sw_dashed
        = sr_ir_to_render_commands(commands_arena, state_arena, nullptr, dashed, dashed_bytes, 255);

    ASSERT_TRUE_MSG(sw_solid.count > 0, "the solid border must decode to at least one SW command");
    ASSERT_EQ_MSG(sw_dashed.count, sw_solid.count, "a dash pattern must not change how many commands SW emits");
    ASSERT_TRUE_MSG(memcmp(sw_dashed.commands, sw_solid.commands, sizeof(RenderCommand) * (size_t)sw_solid.count) == 0,
                    "the SW decode of a dashed border must be byte-identical to the solid one");

    fl_arena_destroy(ir_arena);
    fl_arena_destroy(commands_arena);
    fl_arena_destroy(state_arena);
}

// The dash pattern has no word of its own in the 80-byte draw command - it rides the free high bits of the
// flags word (GPU_DRAW_DASH_* in gpu_ir_to_draw_cmd.h), pinned here at literal bit positions rather than
// through the same helper that writes it.
UTEST(render_ir, gpu_packs_the_dash_pattern_into_the_flags_word) {
    FlArena* ir_arena = arena_new();
    FlArena* commands_arena = arena_new();
    const FlColor color = { 200, 40, 50, 255 };
    const FlRect rect = { 10.0f, 10.0f, 40.0f, 40.0f };

    // 3px on, 2px off at the 1/16px step: 48 and 32 raw, dash in bits 10..20 and gap in 21..31.
    const u32 expected = (48u << 10) | (32u << 21);

    struct {
        const char* what;
        FlStrokeStyle stroke;
        u32 flags;
    } border_cases[] = {
        { "a dash/gap pair packs at its documented bit positions", { 3.0f, 2.0f }, expected },
        { "a zeroed stroke leaves the flags word untouched", { 0.0f, 0.0f }, 0u },
        { "a zero gap is a solid stroke, not a zero-length hole", { 3.0f, 0.0f }, 0u },
        { "a zero dash is solid too, rather than an invisible border", { 0.0f, 2.0f }, 0u },
        { "a negative length is solid, not a wrapped huge pattern", { 3.0f, -2.0f }, 0u },
        // 1/32px of dash has no on/off a pixel could show, and rounds away at the packed step.
        { "a sub-step length rounds away to solid", { 0.03f, 2.0f }, 0u },
        // 2047 raw is the 11-bit ceiling: 127.9375px, past any stroke a pattern is visible on.
        { "an over-long length clamps to the ceiling instead of wrapping",
          { 900.0f, 900.0f },
          (2047u << 10) | (2047u << 21) },
    };

    for (size_t i = 0; i < sizeof(border_cases) / sizeof(border_cases[0]); i++) {
        fl_arena_align(ir_arena, 8);
        const u8* start = (const u8*)fl_arena_current_pos(ir_arena);
        fl_ir_border_stroked(ir_arena, rect, 4.0f, 4.0f, 4.0f, 4.0f, (FlCornerRadius) { 0 }, color,
                             border_cases[i].stroke);
        const size_t bytes = (size_t)((const u8*)fl_arena_current_pos(ir_arena) - start);
        GpuDrawList g = gpu_ir_to_draw_cmds(commands_arena, start, bytes, 255, 128, 128, nullptr, nullptr, nullptr,
                                            nullptr, nullptr, nullptr);
        ASSERT_EQ(g.count, 1u);
        ASSERT_EQ_MSG(g.cmds[0].flags, border_cases[i].flags, border_cases[i].what);
    }

    // A border whose widths consume the rect degenerates to a plain fill, which has no ring to dash.
    fl_arena_align(ir_arena, 8);
    const u8* consumed = (const u8*)fl_arena_current_pos(ir_arena);
    fl_ir_border_stroked(ir_arena, rect, 40.0f, 40.0f, 40.0f, 40.0f, (FlCornerRadius) { 0 }, color,
                         (FlStrokeStyle) { 3.0f, 2.0f });
    size_t consumed_bytes = (size_t)((const u8*)fl_arena_current_pos(ir_arena) - consumed);
    GpuDrawList gc = gpu_ir_to_draw_cmds(commands_arena, consumed, consumed_bytes, 255, 128, 128, nullptr, nullptr,
                                         nullptr, nullptr, nullptr, nullptr);
    ASSERT_EQ(gc.count, 1u);
    ASSERT_EQ_MSG(gc.cmds[0].kind, (u32)GpuDraw_Rect, "a border consuming its rect still becomes a plain fill");
    ASSERT_EQ_MSG(gc.cmds[0].flags, 0u, "a degenerate border carries no dash into the fill it becomes");

    // The same encoding on a circle's ring - and no pattern at all when the normalization leaves no ring.
    fl_arena_align(ir_arena, 8);
    const u8* ring = (const u8*)fl_arena_current_pos(ir_arena);
    fl_ir_circle_stroked(ir_arena, 20.0f, 20.0f, 10.0f, color, color, 2.0f, (FlStrokeStyle) { 3.0f, 2.0f }, nullptr);
    fl_ir_circle_stroked(ir_arena, 20.0f, 20.0f, 10.0f, color, color, 0.0f, (FlStrokeStyle) { 3.0f, 2.0f }, nullptr);
    fl_ir_circle_stroked(ir_arena, 20.0f, 20.0f, 10.0f, color, color, 10.0f, (FlStrokeStyle) { 3.0f, 2.0f }, nullptr);
    size_t ring_bytes = (size_t)((const u8*)fl_arena_current_pos(ir_arena) - ring);
    GpuDrawList gr = gpu_ir_to_draw_cmds(commands_arena, ring, ring_bytes, 255, 128, 128, nullptr, nullptr, nullptr,
                                         nullptr, nullptr, nullptr);
    ASSERT_EQ(gr.count, 3u);
    ASSERT_EQ_MSG(gr.cmds[0].flags, expected, "a circle's ring packs the pattern the same way a border does");
    ASSERT_EQ_MSG(gr.cmds[1].flags, 0u, "a borderless circle has no ring to dash");
    ASSERT_EQ_MSG(gr.cmds[2].flags, 0u, "a border consuming the disc leaves no ring to dash either");

    fl_arena_destroy(ir_arena);
    fl_arena_destroy(commands_arena);
}

// The painter's own text draws are the second crossing from authored to physical, and they do not go through
// Clay at all, so the label path above cannot cover them. ui_scale is held at 1 so a size that wrongly picked
// up the coordinate scale instead of the type scale is still wrong, not accidentally right.
UTEST(render_ir, painter_text_size_and_spacing_follow_text_scale) {
    FlArena* arena = arena_new();
    FlFontCache* cache = font_cache_new(s_arena);
    FlFontId font = font_cache_load_font_memory(cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "memory-loaded font must be ready synchronously");

    Painter* p = painter_create(arena);
    painter_begin_frame(p, cache, 1.0f, 2.0f, arena);

    // painter_text: the authored 32px handle must rasterize at 64px.
    void* slot_plain = nullptr;
    painter_scope_push(p, &slot_plain);
    painter_text(p, 0.0f, 0.0f, FL_S("A"), font, ui_color(255, 255, 255, 255));
    painter_scope_pop(p);
    const FlPaintSpan* span_plain = (const FlPaintSpan*)slot_plain;
    ASSERT_TRUE_MSG(span_plain != nullptr && span_plain->first != nullptr, "painter_text attached a paint span");
    const FlCmdHeader* hp = (const FlCmdHeader*)span_plain->first->ir;
    ASSERT_EQ_MSG((int)hp->kind, (int)FlCmd_TextRun, "painter_text emits one text run");
    ASSERT_EQ_MSG((int)((const FlCmdTextRun*)(hp + 1))->size, (int)font_cache_whole_px_to_q4(64),
                  "the authored load size is multiplied by text_scale, not drawn raw");

    // painter_text_styled: an explicit 7.5px authored size resolves to a 15px key, and the 1.25px authored
    // spacing to 2.5px between the pens.
    const FlTextStyle style = { .font_id = font, .font_size = 7.5f, .letter_spacing = 1.25f };
    void* slot = nullptr;
    painter_scope_push(p, &slot);
    painter_text_styled(p, 10.0f, 20.0f, FL_S("AA"), style, ui_color(255, 255, 255, 255));
    painter_scope_pop(p);

    const FlPaintSpan* span = (const FlPaintSpan*)slot;
    ASSERT_TRUE_MSG(span != nullptr && span->first != nullptr, "styled text attached a paint span");
    const FlCmdHeader* h = (const FlCmdHeader*)span->first->ir;
    const FlCmdTextRun* run = (const FlCmdTextRun*)(h + 1);
    ASSERT_EQ_MSG((int)run->size, 60, "7.5px authored at text_scale 2 carries the 15px quarter-px key");
    ASSERT_EQ_MSG((int)run->glyph_count, 2, "one glyph per codepoint");

    const FlGlyph* glyphs = (const FlGlyph*)(run + 1);
    float adv = (float)font_cache_get_glyph_advance_q4(cache, font, 60, 'A') / 64.0f;
    ASSERT_NEAR_MSG(glyphs[1].x - glyphs[0].x, adv + 2.5f, 0.0001f,
                    "the advance is taken at the scaled size and the authored spacing scales with it");

    // ui_scale is 1 here, so the pen still starts at the authored origin: the type scale must not leak into
    // coordinates.
    ASSERT_NEAR_MSG(glyphs[0].x, 10.0f, 0.0001f, "text_scale does not move the pen origin");

    // font_size 0 falls back to the font's authored load size, which scales the same way (32 -> 64).
    void* slot_default = nullptr;
    painter_scope_push(p, &slot_default);
    painter_text_styled(p, 0.0f, 0.0f, FL_S("A"), (FlTextStyle) { .font_id = font }, ui_color(255, 255, 255, 255));
    painter_scope_pop(p);
    const FlPaintSpan* span_default = (const FlPaintSpan*)slot_default;
    ASSERT_TRUE_MSG(span_default != nullptr && span_default->first != nullptr, "size 0 still draws");
    const FlCmdHeader* hd = (const FlCmdHeader*)span_default->first->ir;
    ASSERT_EQ_MSG((int)((const FlCmdTextRun*)(hd + 1))->size, (int)font_cache_quantize_size_q4(64.0f),
                  "the load-size fallback is scaled too");

    fl_arena_destroy(arena);
    font_cache_destroy(cache);
}
