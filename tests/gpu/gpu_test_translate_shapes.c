// GPU smoke test: shape, polyline and border translator unit tests.

#include "gpu_test_translate.h"

#if !defined(__APPLE__)

// Optional-tier shape translation: arcs, circles, diamonds, and bar sets become their SDF draw kinds (each bar
// of a set reuses GpuDraw_Rect). Still Section 1: pure CPU, no GL.

#define PACKED_RED (200u | (40u << 8) | (50u << 16) | (255u << 24))
#define PACKED_GREEN (40u | (200u << 8) | (60u << 16) | (255u << 24))

static bool near_f32(f32 a, f32 b, f32 tolerance) {
    return fabsf(a - b) <= tolerance;
}

static bool aabb_near(const GpuAabb* a, GpuAabb want, f32 tolerance) {
    return near_f32(a->x0, want.x0, tolerance) && near_f32(a->y0, want.y0, tolerance)
           && near_f32(a->x1, want.x1, tolerance) && near_f32(a->y1, want.y1, tolerance);
}

static void tr_begin_rewind_out(TrCtx* c) {
    fl_arena_rewind(c->out_arena);
    tr_begin(c);
}

static int check_arc_quarter_basic(TrCtx* c) {
    // A quarter ring from 12 to 3 o'clock. The encoding carries center + outer/inner radius and the wedge
    // frame: bisector cos/sin and half-aperture sin/cos, all four 1/sqrt(2) for this quarter.
    c->start = ir_begin(c->ir_arena);
    ir_arc(c->ir_arena, 100.0f, 100.0f, 50.0f, 20.0f, 0.0f, 0.25f * TAU, red, 0, nullptr);
    tr_run(c, 255);
    const GpuDrawCmd* cmd = &c->list.cmds[0];
    CHECK(c->list.count == 1, "arc: one record translates to one command");
    CHECK(cmd->kind == GpuDraw_Arc, "arc: kind is Arc");
    CHECK(cmd->flags == 0, "arc: butt caps and a partial sweep carry no flags");
    return 0;
}

static int check_arc_quarter_encoding(TrCtx* c) {
    const GpuDrawCmd* cmd = &c->list.cmds[0];
    CHECK(rect_words_are(cmd, (FlRect) { 100.0f, 100.0f, 50.0f, 30.0f }),
          "arc: rect packs cx, cy, outer radius, inner radius (outer minus thickness)");
    for (int i = 0; i < 4; i++) {
        CHECK(near_f32(cmd->radius[i], 0.70710678f, 1e-4f),
              "arc: a 12-to-3 quarter has bisector and half-aperture both at an eighth turn");
    }
    CHECK(cmd->color[0] == PACKED_RED, "arc: the color packs like every other kind");
    return 0;
}

static int check_arc_quarter_bounds(TrCtx* c) {
    const GpuAabb* box = &c->list.aabbs[0];
    CHECK(aabb_near(box, (GpuAabb) { 100.0f, 50.0f, 150.0f, 100.0f }, 1e-3f),
          "arc: the AABB bounds the swept quadrant, not the full circle");

    tr_run(c, 128);
    const u32 color = c->list.cmds[0].color[0];
    CHECK((color >> 24) == 128u, "arc: global_alpha folds into the packed color");
    return 0;
}

static int check_arc_directions(TrCtx* c) {
    tr_begin(c);
    ir_arc(c->ir_arena, 100.0f, 100.0f, 50.0f, 20.0f, 0.0f, 0.25f * TAU, red, 0, nullptr);
    ir_arc(c->ir_arena, 100.0f, 100.0f, 50.0f, 20.0f, 0.25f * TAU, -0.25f * TAU, red, 0, nullptr);
    tr_run(c, 255);
    CHECK(c->list.count == 2, "arc: both sweep directions translate");
    CHECK(memcmp(&c->list.cmds[0], &c->list.cmds[1], sizeof(GpuDrawCmd)) == 0,
          "arc: a negative sweep encodes exactly like its positive-clockwise equivalent");
    CHECK(memcmp(&c->list.aabbs[0], &c->list.aabbs[1], sizeof(GpuAabb)) == 0,
          "arc: a negative sweep bounds exactly like its positive-clockwise equivalent");
    return 0;
}

static int check_arc_round_caps(TrCtx* c) {
    // Round caps widen the AABB by the cap discs (centerline radius 40, cap radius 10).
    tr_begin(c);
    ir_arc(c->ir_arena, 100.0f, 100.0f, 50.0f, 20.0f, 0.0f, 0.25f * TAU, red, FL_IR_ARC_ROUND_CAPS, nullptr);
    tr_run(c, 255);
    const GpuAabb* box = &c->list.aabbs[0];
    CHECK(c->list.cmds[0].flags == GPU_DRAW_ARC_ROUND_CAPS, "arc: round caps map to the flag");
    CHECK(near_f32(box->x0, 90.0f, 1e-3f) && near_f32(box->y1, 110.0f, 1e-3f),
          "arc: round caps widen the AABB by the cap discs at both ends");
    CHECK(near_f32(box->y0, 50.0f, 1e-3f) && near_f32(box->x1, 150.0f, 1e-3f),
          "arc: the outer cardinal extremes still bound the capped sector");
    return 0;
}

static int check_arc_full_ring(TrCtx* c) {
    tr_begin(c);
    ir_arc(c->ir_arena, 100.0f, 100.0f, 50.0f, 8.0f, 1.0f, TAU + 1.0f, red, 0, nullptr);
    tr_run(c, 255);
    CHECK(c->list.cmds[0].flags == GPU_DRAW_ARC_FULL, "arc: a sweep past a full turn sets the FULL flag");
    CHECK(aabb_is(&c->list.aabbs[0], (GpuAabb) { 50.0f, 50.0f, 150.0f, 150.0f }),
          "arc: a full ring bounds the whole circle");
    return 0;
}

static int check_arc_degenerate(TrCtx* c) {
    tr_begin(c);
    ir_arc(c->ir_arena, 100.0f, 100.0f, 50.0f, 80.0f, 0.0f, 0.25f * TAU, red, 0, nullptr);
    tr_run(c, 255);
    CHECK(c->list.cmds[0].rect[3] == 0.0f, "arc: thickness past the radius clamps the inner radius to 0");

    tr_begin(c);
    ir_arc(c->ir_arena, 100.0f, 100.0f, 0.0f, 8.0f, 0.0f, 1.0f, red, 0, nullptr);
    ir_arc(c->ir_arena, 100.0f, 100.0f, 50.0f, 0.0f, 0.0f, 1.0f, red, 0, nullptr);
    ir_arc(c->ir_arena, 100.0f, 100.0f, 50.0f, 8.0f, 0.0f, 0.0f, red, 0, nullptr);
    tr_run(c, 255);
    CHECK(c->list.count == 0, "arc: zero radius, thickness, or sweep drops the record");
    return 0;
}

static int check_arc_clip(TrCtx* c) {
    tr_begin(c);
    ir_clip_push(c->ir_arena, (FlRect) { 0, 0, 60, 60 });
    ir_arc(c->ir_arena, 100.0f, 100.0f, 50.0f, 20.0f, 0.0f, 0.25f * TAU, red, 0, nullptr);        // top-right quadrant
    ir_arc(c->ir_arena, 100.0f, 100.0f, 50.0f, 20.0f, 0.75f * TAU, 0.25f * TAU, red, 0, nullptr); // 9-to-12
    ir_clip_pop(c->ir_arena);
    tr_run(c, 255);
    CHECK(c->list.count == 1, "arc: a sector wholly outside the clip drops even though its circle overlaps");
    CHECK(aabb_near(&c->list.aabbs[0], (GpuAabb) { 50.0f, 50.0f, 60.0f, 60.0f }, 1e-3f),
          "arc: the surviving sector's AABB clamps to the clip");
    return 0;
}

static void emit_circle_cases(FlArena* arena) {
    ir_circle(arena, 40.0f, 60.0f, 10.0f, red, green, 3.0f, nullptr);
    ir_circle(arena, 40.0f, 60.0f, 10.0f, red, green, -5.0f, nullptr); // negative width clamps to 0
    ir_circle(arena, 40.0f, 60.0f, 10.0f, red, green, 10.0f, nullptr); // border consumes the disc
    ir_circle(arena, 40.0f, 60.0f, 0.0f, red, green, 0.0f, nullptr);   // degenerate: dropped
}

static int check_circle_first(const GpuDrawList* list) {
    const GpuDrawCmd* first = &list->cmds[0];
    CHECK(list->count == 3, "circle: a zero radius drops the record");
    CHECK(first->kind == GpuDraw_Circle, "circle: kind is Circle");
    CHECK(rect_words_are(first, (FlRect) { 40.0f, 60.0f, 10.0f, 3.0f }),
          "circle: rect packs cx, cy, radius, border width");
    return 0;
}

static int check_circle_colors_and_clamps(const GpuDrawList* list) {
    const GpuDrawCmd* first = &list->cmds[0];
    CHECK(first->color[0] == PACKED_RED && first->color[1] == PACKED_GREEN, "circle: fill then border color");
    CHECK(aabb_is(&list->aabbs[0], (GpuAabb) { 30.0f, 50.0f, 50.0f, 70.0f }), "circle: the AABB is center +- radius");
    CHECK(list->cmds[1].rect[3] == 0.0f, "circle: a negative border width clamps to 0");
    const GpuDrawCmd* consumed = &list->cmds[2];
    CHECK(consumed->rect[3] == 0.0f && consumed->color[0] == PACKED_GREEN,
          "circle: a border consuming the disc normalizes to a borderless border-color disc");
    return 0;
}

static int check_circle_translation(TrCtx* c) {
    tr_begin(c);
    emit_circle_cases(c->ir_arena);
    tr_run(c, 255);
    if (check_circle_first(&c->list) != 0) {
        return 1;
    }
    return check_circle_colors_and_clamps(&c->list);
}

static int check_diamond_first(const GpuDrawList* list) {
    const GpuDrawCmd* first = &list->cmds[0];
    CHECK(list->count == 2, "diamond: a zero half-extent drops the record");
    CHECK(first->kind == GpuDraw_Diamond, "diamond: kind is Diamond");
    CHECK(rect_words_are(first, (FlRect) { 40.0f, 60.0f, 8.0f, 6.0f }), "diamond: rect packs cx, cy, rx, ry");
    return 0;
}

static int check_diamond_borders(const GpuDrawList* list) {
    const GpuDrawCmd* first = &list->cmds[0];
    CHECK(first->radius[0] == 2.0f, "diamond: the border width rides in radius[0]");
    CHECK(first->color[0] == PACKED_RED && first->color[1] == PACKED_GREEN, "diamond: fill then border color");
    CHECK(aabb_is(&list->aabbs[0], (GpuAabb) { 32.0f, 54.0f, 48.0f, 66.0f }),
          "diamond: the AABB is center +- half-extents");
    const GpuDrawCmd* past = &list->cmds[1];
    CHECK(past->radius[0] == 0.0f && past->color[0] == PACKED_GREEN,
          "diamond: a border past the inradius normalizes to a borderless border-color rhombus");
    return 0;
}

static int check_diamond_translation(TrCtx* c) {
    // Diamond border normalization is against the inradius (rx*ry/len = 4.8 here).
    tr_begin(c);
    ir_diamond(c->ir_arena, 40.0f, 60.0f, 8.0f, 6.0f, red, green, 2.0f, nullptr);
    ir_diamond(c->ir_arena, 40.0f, 60.0f, 8.0f, 6.0f, red, green, 5.0f, nullptr); // past the inradius
    ir_diamond(c->ir_arena, 40.0f, 60.0f, 0.0f, 6.0f, red, green, 0.0f, nullptr); // degenerate: dropped
    tr_run(c, 255);
    if (check_diamond_first(&c->list) != 0) {
        return 1;
    }
    return check_diamond_borders(&c->list);
}

static int check_bar_commands(const GpuDrawList* list) {
    const f32 expected_bars[3][4] = { { 10, 80, 5, 20 }, { 20, 100, 5, 15 }, { 50, 70, 5, 30 } };
    for (int b = 0; b < 3; b++) {
        const f32* e = expected_bars[b];
        const GpuDrawCmd* cmd = &list->cmds[b];
        CHECK(cmd->kind == GpuDraw_Rect, "bars: every bar reuses the rect raster path");
        CHECK(rect_words_are(cmd, (FlRect) { e[0], e[1], e[2], e[3] }),
              "bars: heights extend up from the baseline, negative ones down");
        CHECK(aabb_is(&list->aabbs[b], (GpuAabb) { e[0], e[1], e[0] + e[2], e[1] + e[3] }),
              "bars: each bar carries its own AABB");
        for (int i = 0; i < 4; i++) {
            CHECK(cmd->color[i] == PACKED_RED, "bars: the set color packs into all four corners");
            CHECK(cmd->radius[i] == 0.0f, "bars: bars are square-cornered rects");
        }
    }
    return 0;
}

static int check_bar_expansion(TrCtx* c) {
    // Each bar expands to one solid rect command off the shared baseline at y = 100.
    tr_begin(c);
    FlBar* bars = ir_bars(c->ir_arena, 100.0f, red, 5, nullptr);
    bars[0] = (FlBar) { 10.0f, 5.0f, 20.0f };  // up: y 80..100
    bars[1] = (FlBar) { 20.0f, 5.0f, -15.0f }; // down: y 100..115
    bars[2] = (FlBar) { 30.0f, 0.0f, 10.0f };  // zero width: dropped
    bars[3] = (FlBar) { 40.0f, 5.0f, 0.0f };   // zero height: dropped
    bars[4] = (FlBar) { 50.0f, 5.0f, 30.0f };  // up: y 70..100
    tr_run(c, 255);
    CHECK(c->list.count == 3, "bars: degenerate bars drop bar-by-bar, the rest expand");
    return check_bar_commands(&c->list);
}

static int check_bar_clip(TrCtx* c) {
    tr_begin(c);
    ir_clip_push(c->ir_arena, (FlRect) { 0, 0, 18, 200 });
    FlBar* bars = ir_bars(c->ir_arena, 100.0f, red, 3, nullptr);
    bars[0] = (FlBar) { 10.0f, 5.0f, 20.0f };  // wholly inside
    bars[1] = (FlBar) { 15.0f, 10.0f, 20.0f }; // straddles the clip edge at x = 18
    bars[2] = (FlBar) { 30.0f, 5.0f, 20.0f };  // wholly outside: dropped
    ir_clip_pop(c->ir_arena);
    tr_run(c, 255);
    CHECK(c->list.count == 2, "bars: a bar wholly outside the clip drops; the rest survive");
    const GpuAabb* straddler = &c->list.aabbs[1];
    CHECK(straddler->x1 == 18.0f, "bars: a straddling bar's AABB clamps to the clip");
    const GpuDrawCmd* cmd = &c->list.cmds[1];
    CHECK(cmd->clip == 1, "bars: every bar carries the set's clip index");
    return 0;
}

int run_shape_translator_tests(void) {
    RUN_TR_SUITE("shape translator unit tests", check_arc_quarter_basic, check_arc_quarter_encoding,
                 check_arc_quarter_bounds, check_arc_directions, check_arc_round_caps, check_arc_full_ring,
                 check_arc_degenerate, check_arc_clip, check_circle_translation, check_diamond_translation,
                 check_bar_expansion, check_bar_clip);
}

// Polyline translation: a polyline expands to one GpuDraw_Polyline command per segment: a stroke capsule
// (line-aware classification) or, when FILLED, a fill-to-baseline trapezoid emitted before the strokes.
// Still Section 1: pure CPU, no GL.

typedef struct PolyShape {
    f32 width;
    f32 baseline;
    u32 flags;
    f32 intensity;
} PolyShape;

static void emit_three_point_polyline(FlArena* arena, PolyShape shape) {
    FlPolyPoint* pts = ir_polyline(arena, red, green, shape.width, shape.baseline, shape.flags, 3, nullptr);
    pts[0] = (FlPolyPoint) { 10.0f, 10.0f, shape.intensity };
    pts[1] = (FlPolyPoint) { 30.0f, 20.0f, shape.intensity };
    pts[2] = (FlPolyPoint) { 50.0f, 10.0f, shape.intensity };
}

static int check_polyline_open_segments(TrCtx* c) {
    c->start = ir_begin(c->ir_arena);
    FlPolyPoint* pts = ir_polyline(c->ir_arena, red, green, 2.0f, 0.0f, 0, 3, nullptr);
    pts[0] = (FlPolyPoint) { 10.0f, 10.0f, 1.0f };
    pts[1] = (FlPolyPoint) { 30.0f, 20.0f, 0.5f };
    pts[2] = (FlPolyPoint) { 50.0f, 10.0f, 0.25f };
    tr_run(c, 255);
    const GpuDrawCmd* first = &c->list.cmds[0];
    CHECK(c->list.count == 2, "polyline: a 3-point open stroke expands to two segments");
    CHECK(first->kind == GpuDraw_Polyline && (first->flags & GPU_DRAW_POLY_STROKE) != 0,
          "polyline: a stroke segment is a GpuDraw_Polyline with the stroke flag set");
    CHECK(rect_words_are(first, (FlRect) { 10.0f, 10.0f, 30.0f, 20.0f }),
          "polyline: a stroke segment's rect is its two endpoints");
    return 0;
}

static int check_polyline_open_fields(TrCtx* c) {
    const GpuDrawCmd* first = &c->list.cmds[0];
    CHECK(first->radius[0] == 1.0f, "polyline: radius.x is the stroke half-width");
    CHECK(first->radius[1] == 1.0f && first->radius[2] == 0.5f, "polyline: radius.yz are the per-endpoint intensities");
    CHECK(first->color[0] == PACKED_RED, "polyline: color.x is the packed stroke color");
    const GpuDrawCmd* second = &c->list.cmds[1];
    CHECK(second->rect[0] == 30.0f && second->rect[2] == 50.0f && second->radius[2] == 0.25f,
          "polyline: the second segment continues from the shared vertex");
    // The capsule AABB is the segment bbox dilated by the half-width plus the AA ramp (here 1 + 0.5).
    CHECK(aabb_is(&c->list.aabbs[0], (GpuAabb) { 10.0f - 1.5f, 10.0f - 1.5f, 30.0f + 1.5f, 20.0f + 1.5f }),
          "polyline: a stroke segment's AABB is the capsule bound, not the whole polyline");
    return 0;
}

static int check_polyline_filled_order(TrCtx* c) {
    // FILLED: 3 points -> 2 fill trapezoids (first, behind the stroke) + 2 stroke capsules.
    tr_begin_rewind_out(c);
    emit_three_point_polyline(c->ir_arena, (PolyShape) { 2.0f, 100.0f, FlPolylineFlags_filled, 1.0f });
    tr_run(c, 255);
    const GpuDrawList* list = &c->list;
    CHECK(list->count == 4, "polyline filled: a 3-point fill expands to two trapezoids and two strokes");
    CHECK((list->cmds[0].flags & GPU_DRAW_POLY_STROKE) == 0 && (list->cmds[1].flags & GPU_DRAW_POLY_STROKE) == 0,
          "polyline filled: the fill trapezoids come first, behind the stroke");
    CHECK(list->cmds[0].kind == GpuDraw_Polyline && list->cmds[0].color[0] == PACKED_GREEN
              && list->cmds[0].radius[3] == 100.0f,
          "polyline filled: a fill segment carries the fill color and the baseline_y");
    return 0;
}

static int check_polyline_filled_strokes(TrCtx* c) {
    const GpuDrawList* list = &c->list;
    CHECK((list->cmds[2].flags & GPU_DRAW_POLY_STROKE) != 0 && (list->cmds[3].flags & GPU_DRAW_POLY_STROKE) != 0,
          "polyline filled: the stroke capsules come after the fill");
    // A fill trapezoid's AABB spans the segment's x and reaches the baseline in y (here y 10..100, +0.5 AA).
    CHECK(aabb_is(&list->aabbs[0], (GpuAabb) { 10.0f, 10.0f - 0.5f, 30.0f, 100.0f + 0.5f }),
          "polyline filled: a fill trapezoid's AABB reaches the baseline");
    return 0;
}

static int check_polyline_closed_edge(TrCtx* c) {
    tr_begin_rewind_out(c);
    emit_three_point_polyline(c->ir_arena, (PolyShape) { 2.0f, 0.0f, FlPolylineFlags_closed, 1.0f });
    tr_run(c, 255);
    CHECK(c->list.count == 3, "polyline closed: the closing edge adds a third segment");
    CHECK(rect_words_are(&c->list.cmds[2], (FlRect) { 50.0f, 10.0f, 10.0f, 10.0f }),
          "polyline closed: the closing segment runs last -> first");
    return 0;
}

static int check_polyline_degenerate(TrCtx* c) {
    tr_begin_rewind_out(c);
    FlPolyPoint* pts = ir_polyline(c->ir_arena, red, green, 2.0f, 0.0f, 0, 1, nullptr); // one point: no segment
    pts[0] = (FlPolyPoint) { 10.0f, 10.0f, 1.0f };
    emit_three_point_polyline(c->ir_arena, (PolyShape) { 0.0f, 0.0f, 0, 1.0f }); // width 0, not filled: nothing
    pts = ir_polyline(c->ir_arena, red, green, 2.0f, 0.0f, 0, 3, nullptr);       // a zero-length middle segment drops
    pts[0] = (FlPolyPoint) { 10.0f, 10.0f, 1.0f };
    pts[1] = (FlPolyPoint) { 10.0f, 10.0f, 1.0f }; // coincident with pts[0]
    pts[2] = (FlPolyPoint) { 50.0f, 10.0f, 1.0f };
    tr_run(c, 255);
    CHECK(c->list.count == 1,
          "polyline degenerate: only the one non-zero-length segment of the third polyline survives");
    CHECK(c->list.cmds[0].rect[0] == 10.0f && c->list.cmds[0].rect[2] == 50.0f,
          "polyline degenerate: the surviving segment skips the coincident pair");
    return 0;
}

static int check_polyline_fill_edge_cases(TrCtx* c) {
    tr_begin_rewind_out(c);
    FlPolyPoint* pts = ir_polyline(c->ir_arena, red, green, 2.0f, 100.0f, FlPolylineFlags_filled, 2, nullptr);
    pts[0] = (FlPolyPoint) { 20.0f, 10.0f, 1.0f };
    pts[1] = (FlPolyPoint) { 20.0f, 40.0f, 1.0f }; // vertical
    tr_run(c, 255);
    CHECK(c->list.count == 1 && (c->list.cmds[0].flags & GPU_DRAW_POLY_STROKE) != 0,
          "polyline filled: a vertical edge has a stroke but no fill trapezoid");

    tr_begin_rewind_out(c);
    pts = ir_polyline(c->ir_arena, red, green, 0.0f, 100.0f, FlPolylineFlags_filled, 2, nullptr);
    pts[0] = (FlPolyPoint) { 10.0f, 10.0f, 1.0f };
    pts[1] = (FlPolyPoint) { 50.0f, 20.0f, 1.0f };
    tr_run(c, 255);
    const GpuDrawCmd* cmd = &c->list.cmds[0];
    CHECK(c->list.count == 1 && (cmd->flags & GPU_DRAW_POLY_STROKE) == 0 && cmd->color[0] == PACKED_GREEN,
          "polyline filled: a zero-width polyline keeps its fill trapezoid and emits no stroke");
    return 0;
}

int run_polyline_translator_tests(void) {
    RUN_TR_SUITE("polyline translator unit tests", check_polyline_open_segments, check_polyline_open_fields,
                 check_polyline_filled_order, check_polyline_filled_strokes, check_polyline_closed_edge,
                 check_polyline_degenerate, check_polyline_fill_edge_cases);
}

// Border translation: the outer rect, the clamped outer radii, and the four side widths cross to the shader
// verbatim, with no inner radius anywhere in the encoding (the IR contract: the backend derives the inner
// geometry). Still Section 1: pure CPU, no GL.

static int check_border_encoding(TrCtx* c) {
    c->start = ir_begin(c->ir_arena);
    ir_border(c->ir_arena, (FlRect) { 10, 20, 60, 40 }, 1.5f, 2.0f, 3.0f, 4.25f, (FlCornerRadius) { 5, 6, 7, 8 }, red);
    tr_run(c, 255);
    const GpuDrawCmd* cmd = &c->list.cmds[0];
    CHECK(c->list.count == 1, "border: one record translates to one command");
    CHECK(cmd->kind == GpuDraw_Border, "border: kind is Border");
    CHECK(rect_words_are(cmd, (FlRect) { 10.0f, 20.0f, 60.0f, 40.0f }), "border: rect packs the outer rect unchanged");
    return 0;
}

static int check_border_fields(TrCtx* c) {
    const GpuDrawCmd* cmd = &c->list.cmds[0];
    CHECK(radii_are(cmd, (FlRect) { 5.0f, 6.0f, 7.0f, 8.0f }),
          "border: in-range outer radii pass through in TL TR BL BR order");
    CHECK(cmd->color[0] == PACKED_RED, "border: the color packs like every other kind");
    const bool widths_verbatim = bits_f32(cmd->color[1]) == 1.5f && bits_f32(cmd->color[2]) == 2.0f
                                 && bits_f32(cmd->color[3]) == 3.0f && bits_f32(cmd->aux) == 4.25f;
    CHECK(widths_verbatim,
          "border: side widths cross bit-verbatim in left/right/top/bottom order — no inner radius is encoded");
    const GpuAabb* box = &c->list.aabbs[0];
    CHECK(aabb_is(box, (GpuAabb) { 10.0f, 20.0f, 70.0f, 60.0f }), "border: the AABB is the outer rect");

    tr_run(c, 128);
    CHECK((c->list.cmds[0].color[0] >> 24) == 128u, "border: global_alpha folds into the packed color");
    return 0;
}

static int check_border_clamping(TrCtx* c) {
    tr_begin(c);
    ir_border(c->ir_arena, (FlRect) { 0, 0, 40, 20 }, 2, 2, 2, 2, (FlCornerRadius) { 30.0f, 5.0f, -3.0f, 10.0f }, red);
    tr_run(c, 255);
    CHECK(radii_are(&c->list.cmds[0], (FlRect) { 10.0f, 5.0f, 0.0f, 10.0f }),
          "border: outer radii clamp like the fill path (oversized to half the smaller dimension, negative to 0)");

    tr_begin(c);
    ir_border(c->ir_arena, (FlRect) { 0, 0, 50, 50 }, -3.0f, 2.0f, 0.0f, 0.0f, (FlCornerRadius) { 0 }, red);
    ir_border(c->ir_arena, (FlRect) { 0, 0, 50, 50 }, 0, 0, 0, 0, (FlCornerRadius) { 4, 4, 4, 4 }, red);
    ir_border(c->ir_arena, (FlRect) { 0, 0, 50, 50 }, -1.0f, -1.0f, -1.0f, -1.0f, (FlCornerRadius) { 0 }, red);
    ir_border(c->ir_arena, (FlRect) { 0, 0, 0, 50 }, 2, 2, 2, 2, (FlCornerRadius) { 0 }, red);
    tr_run(c, 255);
    CHECK(c->list.count == 1, "border: all-zero/negative widths and empty rects drop");
    CHECK(bits_f32(c->list.cmds[0].color[1]) == 0.0f && bits_f32(c->list.cmds[0].color[2]) == 2.0f,
          "border: a negative width clamps to 0 without disturbing the others");
    return 0;
}

static int check_border_consumed(TrCtx* c) {
    // Either axis alone is enough to consume the rect.
    tr_begin(c);
    ir_border(c->ir_arena, (FlRect) { 0, 0, 40, 40 }, 20.0f, 20.0f, 1.0f, 1.0f, (FlCornerRadius) { 4, 4, 4, 4 }, green);
    ir_border(c->ir_arena, (FlRect) { 0, 0, 40, 40 }, 1.0f, 1.0f, 0.0f, 50.0f, (FlCornerRadius) { 0 }, green);
    tr_run(c, 255);
    CHECK(c->list.count == 2, "border: borders consuming the rect still translate");
    for (int i = 0; i < 2; i++) {
        CHECK(c->list.cmds[i].kind == GpuDraw_Rect, "border: a border consuming the rect normalizes to a plain fill");
        for (int corner = 0; corner < 4; corner++) {
            CHECK(c->list.cmds[i].color[corner] == PACKED_GREEN,
                  "border: the normalized fill carries the border color in all four corners");
        }
    }
    CHECK(c->list.cmds[0].radius[0] == 4.0f, "border: the normalized fill keeps the outer radii");
    return 0;
}

static int check_border_clip(TrCtx* c) {
    tr_begin(c);
    ir_clip_push(c->ir_arena, (FlRect) { 0, 0, 30, 200 });
    ir_border(c->ir_arena, (FlRect) { 10, 10, 40, 40 }, 2, 2, 2, 2, (FlCornerRadius) { 0 }, red); // straddles
    ir_border(c->ir_arena, (FlRect) { 60, 10, 40, 40 }, 2, 2, 2, 2, (FlCornerRadius) { 0 }, red); // outside
    ir_clip_pop(c->ir_arena);
    tr_run(c, 255);
    CHECK(c->list.count == 1, "border: a border wholly outside the clip drops");
    CHECK(c->list.aabbs[0].x1 == 30.0f, "border: a straddling border's AABB clamps to the clip");
    CHECK(c->list.cmds[0].clip == 1, "border: the border carries its clip index");
    return 0;
}

int run_border_translator_tests(void) {
    RUN_TR_SUITE("border translator unit tests", check_border_encoding, check_border_fields, check_border_clamping,
                 check_border_consumed, check_border_clip);
}

#endif // !defined(__APPLE__)
