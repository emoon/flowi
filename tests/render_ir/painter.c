// render-IR painter emission tests

#include "render_ir/common.h"

// The painter case checks that reference-space lengths are scaled to pixels at emit while unitless angles and
// intensity pass through.

// True when the span is exactly one segment.
static bool span_is_one_segment(const FlPaintSpan* span) {
    return span->first == span->last && span->first != nullptr && span->first->next == nullptr;
}

static void expect_scaled_polyline(int* utest_result, const FlCmdHeader* h) {
    ASSERT_TRUE_MSG(h->kind == FlCmd_Polyline, "painter emits the polyline");
    const FlCmdPolyline* pl = (const FlCmdPolyline*)(h + 1);
    ASSERT_TRUE_MSG(pl->width == 3.0f && pl->baseline_y == 100.0f, "polyline width/baseline scale");
    const FlPolyPoint* rpts = (const FlPolyPoint*)(pl + 1);
    ASSERT_TRUE_MSG(rpts[1].x == 6.0f && rpts[1].y == 8.0f, "points scale while copied into the record");
    ASSERT_TRUE_MSG(rpts[1].intensity == 0.25f, "intensity is unitless and does not scale");
}

static void expect_scaled_arc(int* utest_result, const FlCmdHeader* h) {
    ASSERT_TRUE_MSG(h->kind == FlCmd_Arc, "painter emits the arc");
    const FlCmdArc* arc = (const FlCmdArc*)(h + 1);
    ASSERT_TRUE_MSG(arc->cx == 20.0f && arc->radius == 60.0f && arc->thickness == 10.0f, "arc lengths scale");
    ASSERT_TRUE_MSG(arc->start_angle == 1.0f && arc->sweep == 2.5f, "arc angles are unitless and do not scale");
}

static void expect_scaled_circle(int* utest_result, const FlCmdHeader* h) {
    ASSERT_TRUE_MSG(h->kind == FlCmd_Circle, "painter emits the circle");
    const FlCmdCircle* circle = (const FlCmdCircle*)(h + 1);
    ASSERT_TRUE_MSG(circle->radius == 6.0f && circle->border_width == 1.0f, "circle radius/border scale");
    ASSERT_TRUE_MSG(circle->stroke.dash_len == 3.0f && circle->stroke.gap_len == 5.0f,
                    "dash lengths are lengths along the ring and scale with it");
}

static void expect_scaled_diamond(int* utest_result, const FlCmdHeader* h) {
    ASSERT_TRUE_MSG(h->kind == FlCmd_Diamond, "painter emits the diamond");
    const FlCmdDiamond* diamond = (const FlCmdDiamond*)(h + 1);
    ASSERT_TRUE_MSG(diamond->rx == 12.0f && diamond->ry == 14.0f, "diamond half-extents scale");
}

static void expect_scaled_bars(int* utest_result, const FlCmdHeader* h) {
    ASSERT_TRUE_MSG(h->kind == FlCmd_Bar, "painter emits the bar set");
    const FlCmdBarSet* bar_set = (const FlCmdBarSet*)(h + 1);
    ASSERT_TRUE_MSG(bar_set->baseline_y == 200.0f, "bar baseline scales");
    const FlBar* rbars = (const FlBar*)(bar_set + 1);
    ASSERT_TRUE_MSG(rbars[1].x == 6.0f && rbars[1].w == 4.0f && rbars[1].h == -8.0f, "bars scale while copied");
}

// Paints one of each optional-tier primitive inside the open scope.
static void paint_optional_tier_shapes(Painter* p) {
    FlColor color = { 200, 100, 50, 255 };
    FlColor border = { 1, 2, 3, 4 };
    FlPolyPoint pts[2] = { { 1.0f, 2.0f, 1.0f }, { 3.0f, 4.0f, 0.25f } };
    painter_polyline(p, pts, 2,
                     (FlPolylineStyle) { .stroke = { .width = 1.5f, .color = color },
                                         .flags = FlPolylineFlags_filled,
                                         .fill_color = border,
                                         .baseline_y = 50.0f });
    painter_arc(p, (FlVec2) { .x = 10.0f, .y = 20.0f }, 30.0f, 1.0f, 2.5f,
                (FlStroke) { .width = 5.0f, .color = color });
    painter_circle_bordered(
        p, (FlVec2) { .x = 1.0f, .y = 2.0f }, 3.0f, color,
        (FlStroke) { .width = 0.5f, .color = border, .dash = { .dash_len = 1.5f, .gap_len = 2.5f } });
    painter_diamond(p, (FlVec2) { .x = 4.0f, .y = 5.0f }, (FlVec2) { .x = 6.0f, .y = 7.0f }, color);
    FlBar in_bars[2] = { { 0.0f, 2.0f, 10.0f }, { 3.0f, 2.0f, -4.0f } };
    painter_bars(p, 100.0f, in_bars, 2, color);
}

UTEST(render_ir, painter_shapes_scale_at_emit) {
    FlArena* arena = arena_new();
    // ui_scale 2.0 separates scaled fields from unscaled ones. Shapes need no font cache.
    Painter* p = test_painter_begin(arena, nullptr, 2.0f, 1.0f);

    // One painting scope, without Clay. The painter attaches the span to this local customData slot.
    void* slot = nullptr;
    painter_scope_push(p, &slot);
    paint_optional_tier_shapes(p);
    painter_scope_pop(p);

    // The scope's span is one segment, because the primitives are consecutive with no child paint between them.
    const FlPaintSpan* span = (const FlPaintSpan*)slot;
    ASSERT_TRUE_MSG(span != nullptr, "first paint attached a span to the scope's slot");
    ASSERT_TRUE_MSG(span_is_one_segment(span), "consecutive painter calls coalesce into one span segment");
    const FlCmdHeader* h = (const FlCmdHeader*)span->first->ir;
    const u8* end = (const u8*)span->first->ir + span->first->size;

    IR_EXPECT(expect_scaled_polyline, h);
    h = ir_next(h);
    IR_EXPECT(expect_scaled_arc, h);
    h = ir_next(h);
    IR_EXPECT(expect_scaled_circle, h);
    h = ir_next(h);
    IR_EXPECT(expect_scaled_diamond, h);
    h = ir_next(h);
    IR_EXPECT(expect_scaled_bars, h);

    // As above, the overshoot is only the last record's stride rounding.
    h = ir_next(h);
    IR_EXPECT(expect_walk_ends_at, h, end, "painter stream walk lands on its end");
}

// The scene the effect test inspects: an outer scope that sets an effect, paints a nested child and clears the
// effect, then a sibling scope.
typedef struct EffectScene {
    void* outer;
    void* child;
    void* sibling;
} EffectScene;

static void paint_square(Painter* p, float size, FlColor color) {
    painter_rect(p, rect_xywh(0.0f, 0.0f, size, size), color);
}

static void record_effect_scene(Painter* p, EffectScene* scene, FlEffect shadow, FlColor color) {
    painter_scope_push(p, &scene->outer);
    painter_set_effect(p, shadow);
    paint_square(p, 10.0f, color);
    painter_hline(p, 0.0f, 12.0f, 10.0f, color);

    painter_scope_push(p, &scene->child);
    paint_square(p, 4.0f, color);
    painter_scope_pop(p);

    painter_circle(p, (FlVec2) { .x = 1.0f, .y = 1.0f }, 2.0f, color);
    painter_set_effect(p, (FlEffect) { 0 });
    paint_square(p, 2.0f, color);
    painter_scope_pop(p);

    painter_scope_push(p, &scene->sibling);
    paint_square(p, 3.0f, color);
    painter_scope_pop(p);
}

// The outer scope's first segment: the effect applies to everything painted before the child.
static void expect_effect_before_child(int* utest_result, const FlPaintSpan* outer) {
    const FlCmdHeader* h = (const FlCmdHeader*)outer->first->ir;
    ASSERT_TRUE_MSG(h->kind == FlCmd_FillRect && ir_has_flag(h, FL_HAS_EFFECT),
                    "a rect drawn after set_effect carries the effect flag");
    const FlEffect* fx = (const FlEffect*)((const FlCmdFillRect*)(h + 1) + 1);
    ASSERT_TRUE_MSG(fx->kind == FlEffectKind_DropShadow && fx->off_y == 24.0f && fx->blur == 70.0f,
                    "the effect follows the fill-rect payload");

    h = ir_next(h);
    ASSERT_TRUE_MSG(h->kind == FlCmd_FillRect && ir_has_flag(h, FL_SNAP_DEVICE_PX) && ir_has_flag(h, FL_HAS_EFFECT),
                    "a device-snapped hairline carries the effect too");
}

// The outer scope's second segment: the effect outlives the child until a zeroed effect clears it.
static void expect_effect_after_child(int* utest_result, const FlPaintSpan* outer) {
    const FlCmdHeader* h = (const FlCmdHeader*)outer->first->next->ir;
    ASSERT_TRUE_MSG(h->kind == FlCmd_Circle && ir_has_flag(h, FL_HAS_EFFECT),
                    "the effect outlives a nested child scope");
    h = ir_next(h);
    ASSERT_TRUE_MSG(h->kind == FlCmd_FillRect && !ir_has_flag(h, FL_HAS_EFFECT), "a zeroed effect clears the state");
}

static void expect_outer_span_effects(int* utest_result, const FlPaintSpan* outer) {
    ASSERT_TRUE_MSG(outer != nullptr && outer->first != nullptr && outer->first->next != nullptr,
                    "the child's paint splits the outer span into two segments");
    IR_EXPECT(expect_effect_before_child, outer);
    IR_EXPECT(expect_effect_after_child, outer);
}

// The scope in slot recorded a span whose first record has no effect flag.
static void expect_scope_without_effect(int* utest_result, const void* slot, const char* recorded_msg,
                                        const char* clean_msg) {
    const FlCmdHeader* h = span_first_record(slot);
    ASSERT_TRUE_MSG(h != nullptr, recorded_msg);
    ASSERT_TRUE_MSG(!ir_has_flag(h, FL_HAS_EFFECT), clean_msg);
}

// The painter's effect is scoped state. Every later primitive in the element it was set in carries it, and a
// zeroed effect clears it. It does not reach a nested child, a sibling after the scope pops, or the next frame.
UTEST(render_ir, painter_effect_is_scoped_state) {
    FlArena* arena = arena_new();
    Painter* p = test_painter_begin(arena, nullptr, 1.0f, 1.0f);

    FlColor color = { 10, 20, 30, 255 };
    // The plugin frame's box-shadow: 0 24px 70px #000.
    FlEffect shadow = { .kind = FlEffectKind_DropShadow, .color = { 0, 0, 0, 255 }, .off_y = 24.0f, .blur = 70.0f };

    EffectScene scene = { 0 };
    record_effect_scene(p, &scene, shadow, color);

    IR_EXPECT(expect_outer_span_effects, (const FlPaintSpan*)scene.outer);
    IR_EXPECT(expect_scope_without_effect, scene.child, "the child scope recorded its own span",
              "a nested child does not inherit the effect");
    IR_EXPECT(expect_scope_without_effect, scene.sibling, "the sibling scope recorded its own span",
              "the effect is gone once the scope it was set in pops");

    // begin_frame drops every open scope, so an effect left set cannot reach the next frame. It also rewinds the
    // IR arena, which invalidates every record read above.
    void* leaked_slot = nullptr;
    painter_scope_push(p, &leaked_slot);
    painter_set_effect(p, shadow);
    painter_begin_frame(p, test_frame(arena, nullptr, 1.0f, 1.0f));

    void* next_frame_slot = nullptr;
    painter_scope_push(p, &next_frame_slot);
    paint_square(p, 5.0f, color);
    painter_scope_pop(p);

    IR_EXPECT(expect_scope_without_effect, next_frame_slot, "the next frame recorded its own span",
              "begin_frame clears the effect");

    fl_arena_destroy(arena);
}

static void expect_styled_rect(int* utest_result, const FlCmdHeader* h) {
    ASSERT_TRUE_MSG(h->kind == FlCmd_FillRect, "a styled rect is the core fill rect, not a new command kind");
    ASSERT_TRUE_MSG(ir_has_flag(h, FL_HAS_EFFECT), "a styled rect carries the scope's effect");
    const FlCmdFillRect* fr = (const FlCmdFillRect*)(h + 1);
    ASSERT_TRUE_MSG(rect_is(fr->rect, rect_xywh(20.0f, 40.0f, 60.0f, 80.0f)), "geometry scales at emit");
    ASSERT_TRUE_MSG(radius_is(fr->radius, (FlCornerRadius) { 10.0f, 12.0f, 0.0f, 0.0f }),
                    "each corner radius scales independently and a zero stays square");
    ASSERT_TRUE_MSG(fr->corner[0].r == 1 && fr->corner[1].g == 2 && fr->corner[2].b == 3 && fr->corner[3].r == 4,
                    "corner colors keep TL, TR, BL, BR order");
}

static void expect_rounded_rect(int* utest_result, const FlCmdHeader* h) {
    ASSERT_TRUE_MSG(h->kind == FlCmd_FillRect && !ir_has_flag(h, FL_HAS_EFFECT),
                    "rect_rounded still emits a plain fill rect");
    const FlCmdFillRect* rr = (const FlCmdFillRect*)(h + 1);
    ASSERT_TRUE_MSG(radius_is(rr->radius, (FlCornerRadius) { 6.0f, 6.0f, 6.0f, 6.0f }),
                    "rect_rounded keeps its uniform scaled radius through the shared emitter");
    ASSERT_TRUE_MSG(rr->corner[0].r == 1 && rr->corner[3].r == 1, "rect_rounded is four equal corner colors");
}

// painter_rect_styled sets every field of the fill-rect command: per-corner radii (scaled like every other
// length) and four corner colors in TL, TR, BL, BR order. The narrower calls go through it.
UTEST(render_ir, painter_rect_styled_emits_per_corner_radius_and_gradient) {
    FlArena* arena = arena_new();
    Painter* p = test_painter_begin(arena, nullptr, 2.0f, 1.0f);

    FlColor tl = { 1, 0, 0, 255 };
    FlColor tr = { 0, 2, 0, 255 };
    FlColor bl = { 0, 0, 3, 255 };
    FlColor br = { 4, 4, 4, 255 };

    void* slot = nullptr;
    painter_scope_push(p, &slot);
    // The limiter meter: rounded top, square bottom, vertical gradient.
    painter_set_effect(p, (FlEffect) { .kind = FlEffectKind_Glow, .color = { 9, 9, 9, 255 }, .blur = 3.0f });
    painter_rect_styled(p, rect_xywh(10.0f, 20.0f, 30.0f, 40.0f),
                        (FlRectConfig) { .radius = { 5.0f, 6.0f, 0.0f, 0.0f }, .corner = { tl, tr, bl, br } });
    painter_set_effect(p, (FlEffect) { 0 });
    painter_rect_rounded(p, rect_xywh(0.0f, 0.0f, 8.0f, 8.0f), 3.0f, tl);
    painter_scope_pop(p);

    const FlCmdHeader* h = span_first_record(slot);
    ASSERT_TRUE_MSG(h != nullptr, "rect_styled attached a paint span");
    IR_EXPECT(expect_styled_rect, h);
    IR_EXPECT(expect_rounded_rect, ir_next(h));

    fl_arena_destroy(arena);
}

static void expect_sub_image(int* utest_result, const FlCmdHeader* h) {
    ASSERT_TRUE_MSG(h->kind == FlCmd_Image, "image_sub emits image IR");
    const FlCmdImage* im = (const FlCmdImage*)(h + 1);
    ASSERT_TRUE_MSG(im->dst.x == 6.0f && im->dst.y == 8.0f && im->dst.w == 24.0f && im->dst.h == 12.0f,
                    "destination rect scales at emit");
    ASSERT_TRUE_MSG(im->src.x == 2.0f && im->src.y == 1.0f && im->src.w == 4.0f && im->src.h == 3.0f,
                    "source rect stays in image pixels");
    ASSERT_TRUE_MSG(ir_has_flag(h, FL_HAS_EFFECT), "the image record carries the scope's effect flag");
    const FlEffect* fx = (const FlEffect*)(im + 1);
    ASSERT_TRUE_MSG(fx->kind == FlEffectKind_Glow && fx->blur == 6.0f, "the effect follows the image payload");
}

UTEST(render_ir, painter_image_sub_emits_source_rect_and_scaled_dst) {
    FlArena* arena = arena_new();
    ImageRegistry* registry = image_registry_new(arena);
    image_registry_mark_realized((ImageHandle)1234, 16, 8);

    Painter* p = test_painter_begin(arena, nullptr, 2.0f, 1.0f);

    void* slot = nullptr;
    painter_scope_push(p, &slot);
    painter_set_effect(p, (FlEffect) { .kind = FlEffectKind_Glow, .color = { 1, 2, 3, 4 }, .blur = 6.0f });
    const FlImageSub sub = { .dx = 3.0f,
                             .dy = 4.0f,
                             .dw = 12.0f,
                             .dh = 6.0f,
                             .sx = 2.0f,
                             .sy = 1.0f,
                             .sw = 4.0f,
                             .sh = 3.0f,
                             .image = (FlImageHandle)1234,
                             .color = ui_color(10, 20, 30, 40) };
    ASSERT_TRUE_MSG(painter_image_sub(p, sub), "painter_image_sub records for a ready image");
    painter_scope_pop(p);

    const FlCmdHeader* h = span_first_record(slot);
    ASSERT_TRUE_MSG(h != nullptr, "image_sub attached a paint span");
    IR_EXPECT(expect_sub_image, h);

    image_registry_destroy(registry);
    fl_arena_destroy(arena);
}

static void expect_hline_record(int* utest_result, const FlCmdHeader* h) {
    ASSERT_TRUE_MSG(h->kind == FlCmd_FillRect, "hline is a core fill rect, not the optional-tier polyline");
    ASSERT_TRUE_MSG(ir_has_flag(h, FL_SNAP_DEVICE_PX), "hline is tagged for device-pixel snapping");
    const FlCmdFillRect* hl = (const FlCmdFillRect*)(h + 1);
    ASSERT_TRUE_MSG(hl->rect.x == 5.0f && hl->rect.y == 12.5f, "hline position scales (4,10 * 1.25)");
    ASSERT_TRUE_MSG(hl->rect.w == 125.0f, "hline length scales along x (100 * 1.25)");
    ASSERT_TRUE_MSG(hl->rect.h == 1.0f, "hline thickness is one whole device pixel at 1.25 scale");
    const FlCornerRadius radius = hl->radius;
    ASSERT_TRUE_MSG(radius.tl == 0.0f && radius.br == 0.0f, "a hairline has no corner radius");
    ASSERT_TRUE_MSG(hl->corner[0].r == 200 && hl->corner[3].r == 200, "solid color in all four corners");
}

static void expect_vline_record(int* utest_result, const FlCmdHeader* h) {
    ASSERT_TRUE_MSG(h->kind == FlCmd_FillRect && ir_has_flag(h, FL_SNAP_DEVICE_PX),
                    "vline is a snapped core fill rect too");
    const FlCmdFillRect* vl = (const FlCmdFillRect*)(h + 1);
    ASSERT_TRUE_MSG(vl->rect.w == 1.0f, "vline thickness is one whole device pixel along x");
    ASSERT_TRUE_MSG(vl->rect.h == 100.0f, "vline length scales along y (80 * 1.25)");
    ASSERT_TRUE_MSG(vl->rect.x == 25.0f && vl->rect.y == 6.25f, "vline position scales (20,5 * 1.25)");
}

// painter_hline and painter_vline emit a core fill rect (FlCmd_FillRect, which the software backend draws, unlike
// the optional-tier polyline) tagged FL_SNAP_DEVICE_PX. The long axis is scaled and the thin axis is a whole
// device pixel, at least 1. The position is snapped later, at the splice.
UTEST(render_ir, painter_lines_emit_snapped_core_fill_rect) {
    FlArena* arena = arena_new();
    // At ui_scale 1.25 the thin axis snaps to one whole device pixel while the long axis scales fractionally.
    Painter* p = test_painter_begin(arena, nullptr, 1.25f, 1.0f);

    void* slot = nullptr;
    painter_scope_push(p, &slot);
    FlColor col = { 200, 100, 50, 255 };
    painter_hline(p, 4.0f, 10.0f, 100.0f, col);
    painter_vline(p, 20.0f, 5.0f, 80.0f, col);
    painter_scope_pop(p);

    const FlCmdHeader* h = span_first_record(slot);
    ASSERT_TRUE_MSG(h != nullptr, "lines attached a span to the scope's slot");
    IR_EXPECT(expect_hline_record, h);
    IR_EXPECT(expect_vline_record, ir_next(h));

    fl_arena_destroy(arena);
}

// Splices the custom span in slot at bb into out and returns the first record it wrote.
static const FlCmdHeader* splice_custom_span(FlArena* out, void* slot, Clay_BoundingBox bb) {
    const FlCmdHeader* first = (const FlCmdHeader*)ir_mark(out);
    Clay_RenderCommand cmd = { .commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM,
                               .boundingBox = bb,
                               .renderData = { .custom = { .customData = slot } } };
    fl_clay_to_render_ir(out, nullptr, &cmd, 1);
    return first;
}

// The splice rounds a snap-tagged fill rect to the device-pixel grid after adding the element origin, so a 1px
// line stays crisp when the origin carries a fractional scroll offset (scroll_offset is applied unsnapped as the
// element's translation). An untagged fill rect at the same origin stays fractional.
UTEST(render_ir, snapped_line_splice_rounds_at_fractional_origin) {
    FlArena* arena = arena_new();
    // At ui_scale 1.0 element-local space is device space, so only the origin snap is under test.
    Painter* p = test_painter_begin(arena, nullptr, 1.0f, 1.0f);

    FlColor col = { 40, 200, 120, 255 };

    void* snapped_slot = nullptr;
    painter_scope_push(p, &snapped_slot);
    painter_hline(p, 0.0f, 0.0f, 100.0f, col); // element-local rect {0,0,100,1}, snap-tagged
    painter_scope_pop(p);

    void* plain_slot = nullptr;
    painter_scope_push(p, &plain_slot);
    painter_rect(p, rect_xywh(0.0f, 0.0f, 100.0f, 1.0f), col); // element-local rect {0,0,100,1}, not tagged
    painter_scope_pop(p);

    // A fractional element origin, as a fractional scroll offset produces.
    const Clay_BoundingBox bb = { 200.4f, 10.4f, 100.0f, 1.0f };

    // Snapped line: the origin is added, then the rect is rounded to whole device pixels.
    FlArena* out_snapped = arena_new();
    const FlCmdHeader* hs = splice_custom_span(out_snapped, snapped_slot, bb);
    ASSERT_TRUE_MSG(hs->kind == FlCmd_FillRect, "snapped line splices to a fill rect");
    const FlCmdFillRect* fs = (const FlCmdFillRect*)(hs + 1);
    ASSERT_TRUE_MSG(fs->rect.x == 200.0f && fs->rect.y == 10.0f, "snapped to the device grid (round 200.4, 10.4)");
    ASSERT_TRUE_MSG(fs->rect.w == 100.0f && fs->rect.h == 1.0f, "snapped extent stays a 1px line");

    // Plain rect: same fractional origin, left fractional.
    FlArena* out_plain = arena_new();
    const FlCmdHeader* hp = splice_custom_span(out_plain, plain_slot, bb);
    const FlCmdFillRect* fp = (const FlCmdFillRect*)(hp + 1);
    ASSERT_TRUE_MSG(fp->rect.x == 200.4f && fp->rect.y == 10.4f, "an untagged rect keeps its fractional origin");

    fl_arena_destroy(out_plain);
    fl_arena_destroy(out_snapped);
    fl_arena_destroy(arena);
}

// A parent scope that paints, opens a child scope that paints, then paints again has a span in two segments,
// because the child's record sits between the parent's two records in the shared arena. The child's span is one
// segment. Each scope gets its own custom span.
UTEST(render_ir, painter_span_nesting_splits_segments) {
    FlArena* arena = arena_new();
    Painter* p = test_painter_begin(arena, nullptr, 1.0f, 1.0f);

    const FlColor col = { 10, 20, 30, 255 };

    void* slot_parent = nullptr;
    void* slot_child = nullptr;

    painter_scope_push(p, &slot_parent);
    painter_rect(p, rect_xywh(1.0f, 0.0f, 1.0f, 1.0f), col); // parent record A (x == 1)
    painter_scope_push(p, &slot_child);
    painter_rect(p, rect_xywh(2.0f, 0.0f, 1.0f, 1.0f), col); // child record  (x == 2), interrupts the parent's run
    painter_scope_pop(p);
    painter_rect(p, rect_xywh(3.0f, 0.0f, 1.0f, 1.0f), col); // parent record B (x == 3)
    painter_scope_pop(p);

    const FlPaintSpan* parent = (const FlPaintSpan*)slot_parent;
    const FlPaintSpan* child = (const FlPaintSpan*)slot_child;
    ASSERT_TRUE_MSG(parent != nullptr && child != nullptr, "both scopes attached their own span");

    ASSERT_TRUE_MSG(parent->first != nullptr && parent->first->next == parent->last && parent->last->next == nullptr,
                    "the interrupted parent span is exactly two segments");
    const FlCmdFillRect* pa = (const FlCmdFillRect*)((const FlCmdHeader*)parent->first->ir + 1);
    const FlCmdFillRect* pb = (const FlCmdFillRect*)((const FlCmdHeader*)parent->last->ir + 1);
    ASSERT_TRUE_MSG(pa->rect.x == 1.0f && pb->rect.x == 3.0f, "parent segments hold its own records, in order");

    ASSERT_TRUE_MSG(span_is_one_segment(child), "the child span is one segment");
    const FlCmdFillRect* cc = (const FlCmdFillRect*)((const FlCmdHeader*)child->first->ir + 1);
    ASSERT_TRUE_MSG(cc->rect.x == 2.0f, "child segment holds only the child's record");

    fl_arena_destroy(arena);
}
