// render-IR builder and backend decode tests

#include "render_ir/common.h"

// Optional-tier emission and the SW backend's skip (ui/render_ir_build.h, sr_ir_to_render_command.c). The builder
// case walks the whole emitted byte stream. The SW case checks that the translator steps over every optional-tier
// record by byte_size.

static void expect_polyline_record(int* utest_result, const FlCmdHeader* h) {
    ASSERT_TRUE_MSG(h->kind == FlCmd_Polyline, "first record is the polyline");
    ASSERT_TRUE_MSG(ir_has_flag(h, FL_HAS_EFFECT), "polyline carries its effect flag");
    ASSERT_TRUE_MSG((h->byte_size & 7u) == 0, "byte_size keeps the 8-byte record stride");
    const FlCmdPolyline* pl = (const FlCmdPolyline*)(h + 1);
    ASSERT_TRUE_MSG(pl->flags == (FlPolylineFlags_filled | FlPolylineFlags_closed), "polyline flags round-trip");
    ASSERT_TRUE_MSG(pl->width == 2.0f && pl->baseline_y == 100.0f, "polyline scalars round-trip");
    const FlPolyPoint* rpts = (const FlPolyPoint*)(pl + 1);
    ASSERT_TRUE_MSG(pl->color.r == 10 && pl->fill_color.a == 128, "polyline colors round-trip");
    ASSERT_TRUE_MSG(pl->point_count == 3, "polyline point count round-trips");
    const FlEffect* fx = (const FlEffect*)(rpts + 3);
    ASSERT_TRUE_MSG(rpts[1].x == 10.0f && rpts[1].y == 5.0f, "inline points round-trip");
    ASSERT_TRUE_MSG(rpts[1].intensity == 0.5f, "per-point intensity round-trips");
    const u8* effect_end = (const u8*)(fx + 1);
    ASSERT_TRUE_MSG(fx->kind == FlEffectKind_Glow && fx->blur == 6.0f, "effect follows the trailing point array");
    ASSERT_TRUE_MSG(effect_end <= (const u8*)h + h->byte_size, "effect lands inside the record");
}

static void expect_arc_record(int* utest_result, const FlCmdHeader* h) {
    ASSERT_TRUE_MSG(h->kind == FlCmd_Arc, "second record is the arc");
    ASSERT_TRUE_MSG(!ir_has_flag(h, FL_HAS_EFFECT), "no effect flag without an effect");
    const FlCmdArc* arc = (const FlCmdArc*)(h + 1);
    ASSERT_TRUE_MSG(arc->cx == 50.0f && arc->cy == 60.0f, "arc center round-trips");
    ASSERT_TRUE_MSG(arc->radius == 24.0f && arc->thickness == 4.0f, "arc radius/thickness round-trip");
    const u32 arc_flags = arc->flags;
    ASSERT_TRUE_MSG(arc->start_angle == -2.0f && arc->sweep == 4.0f, "arc angles round-trip");
    ASSERT_TRUE_MSG(arc_flags == FL_IR_ARC_ROUND_CAPS, "arc flags round-trip");
}

static void expect_circle_and_diamond_records(int* utest_result, const FlCmdHeader* circle_h) {
    ASSERT_TRUE_MSG(circle_h->kind == FlCmd_Circle, "third record is the circle");
    const FlCmdCircle* circle = (const FlCmdCircle*)(circle_h + 1);
    ASSERT_TRUE_MSG(circle->radius == 3.0f && circle->border_width == 1.5f, "circle fields round-trip");

    const FlCmdHeader* h = ir_next(circle_h);
    ASSERT_TRUE_MSG(h->kind == FlCmd_Diamond, "fourth record is the diamond");
    const FlCmdDiamond* diamond = (const FlCmdDiamond*)(h + 1);
    ASSERT_TRUE_MSG(diamond->rx == 6.0f && diamond->ry == 7.0f, "diamond half-extents round-trip");
    ASSERT_TRUE_MSG(diamond->border_width == 0.0f, "borderless diamond round-trips");
}

static void expect_bar_set_record(int* utest_result, const FlCmdHeader* h) {
    ASSERT_TRUE_MSG(h->kind == FlCmd_Bar, "fifth record is the bar set");
    ASSERT_TRUE_MSG(ir_has_flag(h, FL_HAS_EFFECT), "bar set carries its effect flag");
    const FlCmdBarSet* bar_set = (const FlCmdBarSet*)(h + 1);
    ASSERT_TRUE_MSG(bar_set->baseline_y == 200.0f && bar_set->bar_count == 2, "bar-set payload round-trips");
    const FlBar* rbars = (const FlBar*)(bar_set + 1);
    ASSERT_TRUE_MSG(rbars[0].h == 50.0f && rbars[1].h == -10.0f, "inline bars round-trip (negative h draws down)");
    const FlEffect* fx = (const FlEffect*)(rbars + 2);
    ASSERT_TRUE_MSG(fx->kind == FlEffectKind_Glow, "effect follows the trailing bar array");
}

// Emits a polyline, arc, circle, diamond and bar set, the polyline and the bar set with a glow.
static void emit_optional_tier_stream(FlArena* arena) {
    FlColor stroke = { 10, 20, 30, 255 };
    FlColor fill = { 40, 50, 60, 128 };
    FlEffect glow = { .kind = FlEffectKind_Glow, .color = { 1, 2, 3, 4 }, .off_x = 0.0f, .off_y = 0.0f, .blur = 6.0f };

    FlPolyPoint* pts
        = ir_polyline(arena, stroke, fill, 2.0f, 100.0f, FlPolylineFlags_filled | FlPolylineFlags_closed, 3, &glow);
    pts[0] = (FlPolyPoint) { 0.0f, 8.0f, 1.0f };
    pts[1] = (FlPolyPoint) { 10.0f, 5.0f, 0.5f };
    pts[2] = (FlPolyPoint) { 20.0f, 8.0f, 1.0f };
    ir_arc(arena, 50.0f, 60.0f, 24.0f, 4.0f, -2.0f, 4.0f, stroke, FL_IR_ARC_ROUND_CAPS, nullptr);
    ir_circle(arena, 1.0f, 2.0f, 3.0f, stroke, fill, 1.5f, nullptr);
    ir_diamond(arena, 4.0f, 5.0f, 6.0f, 7.0f, stroke, fill, 0.0f, nullptr);
    FlBar* bars = ir_bars(arena, 200.0f, stroke, 2, &glow);
    bars[0] = (FlBar) { 0.0f, 4.0f, 50.0f };
    bars[1] = (FlBar) { 6.0f, 4.0f, -10.0f };
}

UTEST(render_ir, optional_tier_builders_round_trip) {
    FlArena* arena = arena_new();
    const u8* start = ir_mark(arena);
    emit_optional_tier_stream(arena);
    const u8* end = (const u8*)fl_arena_current_pos(arena);

    const FlCmdHeader* h = (const FlCmdHeader*)start;
    IR_EXPECT(expect_polyline_record, h);
    h = ir_next(h);
    IR_EXPECT(expect_arc_record, h);
    h = ir_next(h);
    IR_EXPECT(expect_circle_and_diamond_records, h);
    h = ir_next(ir_next(h));
    IR_EXPECT(expect_bar_set_record, h);

    // The walk reaches the stream end and overshoots it only by the last record's rounding to the 8-byte stride,
    // because the arena is not padded after the last record.
    h = ir_next(h);
    IR_EXPECT(expect_walk_ends_at, h, end, "walk lands on the stream end");

    fl_arena_destroy(arena);
}

UTEST(render_ir, sw_backend_skips_optional_tier) {
    SwRig rig = sw_rig_new();
    const u8* start = ir_mark(rig.ir);

    // Two core-tier fill rects around all five optional-tier kinds. One of those has a trailing array and an
    // effect, the worst case for the byte_size step.
    FlColor c = { 255, 0, 0, 255 };
    FlColor corners[4] = { c, c, c, c };
    FlEffect glow = { .kind = FlEffectKind_Glow, .color = c, .blur = 4.0f };

    ir_fill_rect(rig.ir, (FlRect) { 0.0f, 0.0f, 10.0f, 10.0f }, (FlCornerRadius) { 0 }, corners, nullptr);
    FlPolyPoint* pts = ir_polyline(rig.ir, c, c, 1.0f, 0.0f, 0, 2, &glow);
    pts[0] = (FlPolyPoint) { 0.0f, 0.0f, 1.0f };
    pts[1] = (FlPolyPoint) { 1.0f, 1.0f, 1.0f };
    ir_arc(rig.ir, 0.0f, 0.0f, 5.0f, 1.0f, 0.0f, 3.0f, c, 0, nullptr);
    ir_circle(rig.ir, 0.0f, 0.0f, 2.0f, c, c, 0.0f, nullptr);
    ir_diamond(rig.ir, 0.0f, 0.0f, 2.0f, 2.0f, c, c, 0.0f, nullptr);
    FlBar* bars = ir_bars(rig.ir, 0.0f, c, 1, nullptr);
    bars[0] = (FlBar) { 0.0f, 1.0f, 1.0f };
    ir_fill_rect(rig.ir, (FlRect) { 20.0f, 0.0f, 10.0f, 10.0f }, (FlCornerRadius) { 0 }, corners, nullptr);

    UiRenderCommandsArray out = sw_translate_ir(&rig, ir_range_since(rig.ir, start), 255);

    ASSERT_TRUE_MSG(out.count == 2, "only the two core-tier fill rects translate");

    sw_rig_destroy(&rig);
}

static void emit_polyline_alone(FlArena* ir, FlColor c) {
    FlPolyPoint* pts = ir_polyline(ir, c, c, 1.0f, 0.0f, 0, 2, nullptr);
    pts[0] = (FlPolyPoint) { 0.0f, 0.0f, 1.0f };
    pts[1] = (FlPolyPoint) { 1.0f, 1.0f, 1.0f };
}

static void emit_arc_alone(FlArena* ir, FlColor c) {
    ir_arc(ir, 0.0f, 0.0f, 5.0f, 1.0f, 0.0f, 3.0f, c, 0, nullptr);
}

static void emit_circle_alone(FlArena* ir, FlColor c) {
    ir_circle(ir, 0.0f, 0.0f, 2.0f, c, c, 0.0f, nullptr);
}

static void emit_diamond_alone(FlArena* ir, FlColor c) {
    ir_diamond(ir, 0.0f, 0.0f, 2.0f, 2.0f, c, c, 0.0f, nullptr);
}

static void emit_bar_alone(FlArena* ir, FlColor c) {
    FlBar* bars = ir_bars(ir, 0.0f, c, 1, nullptr);
    bars[0] = (FlBar) { 0.0f, 1.0f, 1.0f };
}

// Like sw_backend_skips_optional_tier, but each kind is fed alone, so a primitive that gains SW support changes
// only its own assertion.
UTEST(render_ir, sw_backend_drops_each_optional_tier) {
    SwRig rig = sw_rig_new();
    FlColor c = { 255, 0, 0, 255 };

    const struct {
        const char* what;
        void (*emit)(FlArena* ir, FlColor c);
    } cases[] = {
        { "Polyline is optional-tier: the SW backend drops it", emit_polyline_alone },
        { "Arc is optional-tier: the SW backend drops it", emit_arc_alone },
        { "Circle is optional-tier: the SW backend drops it", emit_circle_alone },
        { "Diamond is optional-tier: the SW backend drops it", emit_diamond_alone },
        { "Bar is optional-tier: the SW backend drops it", emit_bar_alone },
    };

    for_count(i, sizeof_array(cases)) {
        const u8* start = ir_mark(rig.ir);
        cases[i].emit(rig.ir, c);
        ASSERT_TRUE_MSG(sw_translate_ir(&rig, ir_range_since(rig.ir, start), 255).count == 0, cases[i].what);
    }

    sw_rig_destroy(&rig);
}

// Appends a core fill rect with a drop shadow at the record tail. The core builder has no effect parameter, so the
// record is assembled directly, as the painter's annotation path does.
static IrRange emit_fill_rect_with_effect(FlArena* ir, FlRect rect, FlColor c, const FlEffect* effect) {
    const u8* start = ir_mark(ir);
    FlCmdHeader* header = ir_header(
        ir, FlCmd_FillRect, (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdFillRect)) + ir_effect_bytes(effect));
    FlCmdFillRect* fr = arena_alloc_zero(ir, FlCmdFillRect);
    fr->rect = rect;
    fr->corner[0] = c;
    fr->corner[1] = c;
    fr->corner[2] = c;
    fr->corner[3] = c;
    ir_append_effect(ir, header, effect);
    return ir_range_since(ir, start);
}

static void expect_same_sw_rect(int* utest_result, const RenderCommand* a, const RenderCommand* b) {
    ASSERT_EQ((int)a->mask, (int)b->mask);
    const bool same_aabb = memcmp(&a->aabb, &b->aabb, sizeof a->aabb) == 0;
    ASSERT_TRUE_MSG(same_aabb, "the effect must not move or resize the SW rect");
    const bool same_colors = memcmp(&a->corner_colors, &b->corner_colors, sizeof a->corner_colors) == 0;
    ASSERT_TRUE_MSG(same_colors, "the effect must not change the SW rect's colors");
    ASSERT_TRUE_MSG(a->radius == b->radius, "the effect must not change the SW rect's corner radius");
}

// The SW backend never reads the effect, so a core fill rect with a drop shadow translates to the same render
// command as the same rect without one.

UTEST(render_ir, sw_backend_ignores_effect_on_core_command) {
    SwRig rig = sw_rig_new();

    FlColor c = { 200, 40, 50, 255 };
    FlColor corners[4] = { c, c, c, c };
    FlRect rect = { 5.0f, 6.0f, 30.0f, 20.0f };
    FlEffect shadow
        = { .kind = FlEffectKind_DropShadow, .color = { 0, 0, 0, 255 }, .off_x = 12.0f, .off_y = 8.0f, .blur = 5.0f };

    const u8* plain_start = ir_mark(rig.ir);
    ir_fill_rect(rig.ir, rect, (FlCornerRadius) { 0 }, corners, nullptr);
    IrRange plain_range = ir_range_since(rig.ir, plain_start);
    IrRange fx_range = emit_fill_rect_with_effect(rig.ir, rect, c, &shadow);

    UiRenderCommandsArray plain = sw_translate_ir(&rig, plain_range, 255);
    UiRenderCommandsArray withfx = sw_translate_ir(&rig, fx_range, 255);

    ASSERT_EQ(plain.count, 1);
    ASSERT_EQ(withfx.count, plain.count);
    IR_EXPECT(expect_same_sw_rect, &plain.commands[0], &withfx.commands[0]);

    sw_rig_destroy(&rig);
}

// global_alpha fold: a=200, ga=200 is a value where truncation (156) and rounding (157) differ.
static void expect_global_alpha_fold_agrees(int* utest_result, const SwRig* rig) {
    FlColor c = { 10, 20, 30, 200 };
    FlColor solid[4] = { c, c, c, c };

    const u8* start = ir_mark(rig->ir);
    ir_fill_rect(rig->ir, (FlRect) { 0.0f, 0.0f, 100.0f, 100.0f }, (FlCornerRadius) { 0 }, solid, nullptr);
    IrRange fill = ir_range_since(rig->ir, start);

    UiRenderCommandsArray sw = sw_translate_ir(rig, fill, 200);
    ASSERT_EQ(sw.count, 1);
    u8 sw_rgba[16];
    u8x16_store_unaligned(sw.commands[0].corner_colors, sw_rgba, 0);
    int sw_alpha = sw_rgba[3]; // premultiply leaves the alpha channel as the folded alpha

    GpuDrawList gpu = gpu_decode_ir(
        rig->commands, fill, (GpuIrInput) { .global_alpha = 200, .target_width = 200, .target_height = 200 });
    ASSERT_TRUE(gpu.count >= 1);
    int gpu_alpha = (int)((gpu.cmds[0].color[0] >> 24) & 0xFFu); // R low byte, A high byte

    ASSERT_EQ_MSG(sw_alpha, 157, "SW must round the global_alpha fold, not truncate");
    ASSERT_EQ_MSG(gpu_alpha, sw_alpha, "SW and GPU must fold global_alpha identically");
}

// Corner-radius clamp: a negative corner decodes as zero on both backends. (12,12,-3,0) reaches the SW
// edge-path predicates (has_only_top_corners requires bl==0), so a negative that was not floored would take a
// different split path than the same rect with the corner at 0.
static void expect_negative_radius_clamps(int* utest_result, const SwRig* rig) {
    FlColor opaque = { 255, 255, 255, 255 };
    FlColor op4[4] = { opaque, opaque, opaque, opaque };
    FlRect rect = { 0.0f, 0.0f, 100.0f, 100.0f };
    FlCornerRadius neg_r = { .tl = 12.0f, .tr = 12.0f, .bl = -3.0f, .br = 0.0f };
    FlCornerRadius zero_r = { .tl = 12.0f, .tr = 12.0f, .bl = 0.0f, .br = 0.0f };

    const u8* neg_start = ir_mark(rig->ir);
    ir_fill_rect(rig->ir, rect, neg_r, op4, nullptr);
    IrRange neg = ir_range_since(rig->ir, neg_start);

    const u8* zero_start = ir_mark(rig->ir);
    ir_fill_rect(rig->ir, rect, zero_r, op4, nullptr);
    IrRange zero = ir_range_since(rig->ir, zero_start);

    UiRenderCommandsArray sw_neg = sw_translate_ir(rig, neg, 255);
    UiRenderCommandsArray sw_zero = sw_translate_ir(rig, zero, 255);
    ASSERT_EQ_MSG(sw_neg.count, sw_zero.count, "SW must clamp a negative corner radius to zero (same split as 0)");
    ASSERT_TRUE_MSG(memcmp(sw_neg.commands, sw_zero.commands, sizeof(RenderCommand) * (size_t)sw_neg.count) == 0,
                    "SW negative-radius decode must be byte-identical to the zero-radius decode");

    GpuDrawList gpu_neg = gpu_decode_ir(
        rig->commands, neg, (GpuIrInput) { .global_alpha = 255, .target_width = 200, .target_height = 200 });
    ASSERT_TRUE(gpu_neg.count >= 1);
    ASSERT_TRUE_MSG(gpu_neg.cmds[0].radius[2] == 0.0f, "GPU must floor a negative corner radius to zero");
}

// The SW and GPU IR decoders share two pieces of decode math: the global_alpha fold, which both round as
// (a*ga+127)/255, and the corner-radius clamp, which floors negatives to 0. A rect that hit a difference in either
// would render differently on the two backends.

UTEST(render_ir, sw_gpu_decode_agrees) {
    SwRig rig = sw_rig_new();

    IR_EXPECT(expect_global_alpha_fold_agrees, &rig);
    IR_EXPECT(expect_negative_radius_clamps, &rig);

    sw_rig_destroy(&rig);
}

// Dashing is GPU-tier, and the software backend draws every border solid. FlCmdBorder carries 8 extra bytes for
// the pattern, so a stale offset would make the SW decode read the stroke as a width or a color.
UTEST(render_ir, sw_backend_ignores_a_border_dash_pattern) {
    SwRig rig = sw_rig_new();
    const FlColor color = { 200, 40, 50, 255 };
    const FlRect rect = { 10.0f, 10.0f, 40.0f, 40.0f };
    const FlCornerRadius radius = { .tl = 6.0f, .tr = 6.0f, .bl = 6.0f, .br = 6.0f };

    const u8* solid_start = ir_mark(rig.ir);
    ir_border(rig.ir, rect, 4.0f, 3.0f, 2.0f, 1.0f, radius, color);
    IrRange solid = ir_range_since(rig.ir, solid_start);

    const u8* dashed_start = ir_mark(rig.ir);
    ir_border_stroked(rig.ir, rect, 4.0f, 3.0f, 2.0f, 1.0f, radius, color, (FlStrokeStyle) { 3.0f, 2.0f });
    IrRange dashed = ir_range_since(rig.ir, dashed_start);

    UiRenderCommandsArray sw_solid = sw_translate_ir(&rig, solid, 255);
    UiRenderCommandsArray sw_dashed = sw_translate_ir(&rig, dashed, 255);

    ASSERT_TRUE_MSG(sw_solid.count > 0, "the solid border must decode to at least one SW command");
    ASSERT_EQ_MSG(sw_dashed.count, sw_solid.count, "a dash pattern must not change how many commands SW emits");
    ASSERT_TRUE_MSG(memcmp(sw_dashed.commands, sw_solid.commands, sizeof(RenderCommand) * (size_t)sw_solid.count) == 0,
                    "the SW decode of a dashed border must be byte-identical to the solid one");

    sw_rig_destroy(&rig);
}

// The 80-byte draw command has no word for the dash pattern, so it goes in the free high bits of the flags word
// (GPU_DRAW_DASH_* in gpu_ir_to_draw_cmd.h). The checks use literal bit positions and not the helper that writes
// them.

// 3px on, 2px off at the 1/16px step: 48 and 32 raw, dash in bits 10..20 and gap in 21..31.
#define DASH_3_ON_2_OFF ((48u << 10) | (32u << 21))

static const GpuIrInput k_dash_target = { .global_alpha = 255, .target_width = 128, .target_height = 128 };

static void expect_border_dash_flags(int* utest_result, FlArena* ir_arena, FlArena* commands_arena) {
    const FlColor color = { 200, 40, 50, 255 };
    const FlRect rect = { 10.0f, 10.0f, 40.0f, 40.0f };

    struct {
        const char* what;
        FlStrokeStyle stroke;
        u32 flags;
    } border_cases[] = {
        { "a dash/gap pair packs at its documented bit positions", { 3.0f, 2.0f }, DASH_3_ON_2_OFF },
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

    for_count(i, sizeof_array(border_cases)) {
        const u8* start = ir_mark(ir_arena);
        ir_border_stroked(ir_arena, rect, 4.0f, 4.0f, 4.0f, 4.0f, (FlCornerRadius) { 0 }, color,
                          border_cases[i].stroke);
        GpuDrawList g = gpu_decode_ir(commands_arena, ir_range_since(ir_arena, start), k_dash_target);
        ASSERT_EQ(g.count, 1u);
        ASSERT_EQ_MSG(g.cmds[0].flags, border_cases[i].flags, border_cases[i].what);
    }
}

// A border whose widths consume the rect degenerates to a plain fill, which has no ring to dash.
static void expect_consumed_border_is_plain_fill(int* utest_result, FlArena* ir_arena, FlArena* commands_arena) {
    const FlColor color = { 200, 40, 50, 255 };
    const FlRect rect = { 10.0f, 10.0f, 40.0f, 40.0f };

    const u8* consumed = ir_mark(ir_arena);
    ir_border_stroked(ir_arena, rect, 40.0f, 40.0f, 40.0f, 40.0f, (FlCornerRadius) { 0 }, color,
                      (FlStrokeStyle) { 3.0f, 2.0f });
    GpuDrawList gc = gpu_decode_ir(commands_arena, ir_range_since(ir_arena, consumed), k_dash_target);
    ASSERT_EQ(gc.count, 1u);
    ASSERT_EQ_MSG(gc.cmds[0].kind, (u32)GpuDraw_Rect, "a border consuming its rect still becomes a plain fill");
    ASSERT_EQ_MSG(gc.cmds[0].flags, 0u, "a degenerate border carries no dash into the fill it becomes");
}

// The same encoding on a circle's ring, and no pattern when the normalization leaves no ring.
static void expect_circle_ring_dash(int* utest_result, FlArena* ir_arena, FlArena* commands_arena) {
    const FlColor color = { 200, 40, 50, 255 };

    const u8* ring = ir_mark(ir_arena);
    ir_circle_stroked(ir_arena, 20.0f, 20.0f, 10.0f, color, color, 2.0f, (FlStrokeStyle) { 3.0f, 2.0f }, nullptr);
    ir_circle_stroked(ir_arena, 20.0f, 20.0f, 10.0f, color, color, 0.0f, (FlStrokeStyle) { 3.0f, 2.0f }, nullptr);
    ir_circle_stroked(ir_arena, 20.0f, 20.0f, 10.0f, color, color, 10.0f, (FlStrokeStyle) { 3.0f, 2.0f }, nullptr);
    GpuDrawList gr = gpu_decode_ir(commands_arena, ir_range_since(ir_arena, ring), k_dash_target);
    ASSERT_EQ(gr.count, 3u);
    const GpuDrawCmd* ring_cmds = gr.cmds;
    ASSERT_EQ_MSG(ring_cmds[0].flags, DASH_3_ON_2_OFF, "a circle's ring packs the pattern the same way a border does");
    ASSERT_EQ_MSG(ring_cmds[1].flags, 0u, "a borderless circle has no ring to dash");
    ASSERT_EQ_MSG(ring_cmds[2].flags, 0u, "a border consuming the disc leaves no ring to dash either");
}

UTEST(render_ir, gpu_packs_the_dash_pattern_into_the_flags_word) {
    SwRig rig = sw_rig_new();

    IR_EXPECT(expect_border_dash_flags, rig.ir, rig.commands);
    IR_EXPECT(expect_consumed_border_is_plain_fill, rig.ir, rig.commands);
    IR_EXPECT(expect_circle_ring_dash, rig.ir, rig.commands);

    sw_rig_destroy(&rig);
}

UTEST(render_ir, software_alpha_coverage_stays_inside_its_source) {
    SwRig rig = sw_rig_new();
    u8 mask[4 * 4] = { 0 };

    const u8* start = ir_mark(rig.ir);
    FlImageSource source = { .kind = FlImageKind_Dynamic, .dynamic = { .pixels = mask, .w = 4, .h = 4 } };
    ir_image(rig.ir, source, FlImageMode_AlphaCoverage, (FlRect) { 10.0f, 20.0f, 12.0f, 9.0f }, (FlRect) { 0 },
             (FlColor) { 255, 255, 255, 255 }, FlImageFilter_Linear, nullptr);

    const UiRenderCommandsArray result = sw_translate_ir(&rig, ir_range_since(rig.ir, start), 255);
    ASSERT_EQ_MSG(result.count, 1, "the coverage image translates to one command");
    const f32x4 aabb = result.commands[0].render_text.aabb;
    EXPECT_EQ(f32x4_extract(aabb, 2), 14.0f);
    EXPECT_EQ(f32x4_extract(aabb, 3), 24.0f);

    sw_rig_destroy(&rig);
}

UTEST(render_ir, software_translation_stops_at_a_zero_size_record) {
    SwRig rig = sw_rig_new();
    const FlCmdHeader unsized[2] = { { .kind = 0xFFFF, .byte_size = 0 }, { .kind = FlCmd_ClipPop, .byte_size = 8 } };

    const UiRenderCommandsArray result = sw_translate(rig.commands, rig.state, unsized, sizeof(unsized), 255);
    ASSERT_EQ_MSG((int)result.count, 0, "a record without a size ends the walk, as it does for the GPU backend");

    sw_rig_destroy(&rig);
}
