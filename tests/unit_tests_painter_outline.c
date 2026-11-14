// Painter outline tests
//
// The outline calls reuse the fill shapes' records: a rect outline is a border band, an outline-only circle
// or diamond is the bordered shape with a transparent fill, and a line is a two-point polyline. Each test
// records one call at ui_scale 2.0 and compares the record it produced with the expected one.

#include "unit_tests_common.h"

#define OUTLINE_COLOR { 200, 100, 50, 255 }

static const FlStroke k_outline
    = { .width = 1.5f, .color = OUTLINE_COLOR, .dash = { .dash_len = 2.0f, .gap_len = 3.0f } };

static void draw_rect_stroke(Painter* p) {
    painter_rect_stroke(p, (FlRect) { .x = 1.0f, .y = 2.0f, .w = 30.0f, .h = 40.0f }, 4.0f, k_outline);
}

static void draw_line(Painter* p) {
    painter_line(p, (FlVec2) { .x = 1.0f, .y = 2.0f }, (FlVec2) { .x = 3.0f, .y = 4.0f }, k_outline);
}

static void draw_circle_stroke(Painter* p) {
    painter_circle_stroke(p, (FlVec2) { .x = 5.0f, .y = 6.0f }, 7.0f, k_outline);
}

static void draw_diamond_stroke(Painter* p) {
    painter_diamond_stroke(p, (FlVec2) { .x = 5.0f, .y = 6.0f }, (FlVec2) { .x = 8.0f, .y = 9.0f }, k_outline);
}

static void draw_round_capped_arc(Painter* p) {
    painter_arc(p, (FlVec2) { .x = 5.0f, .y = 6.0f }, 7.0f, 0.5f, 1.0f,
                (FlStroke) { .width = 2.0f, .color = OUTLINE_COLOR, .cap = FlStrokeCap_Round });
}

// What one draw call is expected to record: the record kind and the leading bytes of its payload.
typedef struct OutlineExpect {
    FlCmdKind kind;
    const void* payload;
    size_t size;
} OutlineExpect;

// Runs draw inside one painting scope at ui_scale 2.0 and compares the first record with want.
static bool outline_records(void (*draw)(Painter*), OutlineExpect want) {
    FlArena* arena = arena_new();
    Painter* p = painter_create(arena);
    void* slot = nullptr; // the scope's customData slot; holds the FlPaintSpan after the first paint
    painter_begin_frame(p, (PainterFrame) { .ui_scale = 2.0f, .text_scale = 1.0f, .state_arena = arena });
    painter_scope_push(p, &slot);
    draw(p);
    painter_scope_pop(p);

    const FlPaintSpan* span = (const FlPaintSpan*)slot;
    bool matches = false;
    if (span != nullptr && span->first != nullptr) {
        const FlCmdHeader* h = (const FlCmdHeader*)span->first->ir;
        matches = h->kind == want.kind && memcmp(h + 1, want.payload, want.size) == 0;
    }
    fl_arena_destroy(arena);
    return matches;
}

UTEST(painter_outline, rect_stroke_is_a_border_band) {
    const FlCmdBorder band = { .rect = { .x = 2.0f, .y = 4.0f, .w = 60.0f, .h = 80.0f },
                               .w_l = 3.0f,
                               .w_r = 3.0f,
                               .w_t = 3.0f,
                               .w_b = 3.0f,
                               .radius = { .tl = 8.0f, .tr = 8.0f, .bl = 8.0f, .br = 8.0f },
                               .color = OUTLINE_COLOR,
                               .stroke = { .dash_len = 4.0f, .gap_len = 6.0f } };
    ASSERT_TRUE_MSG(outline_records(draw_rect_stroke, (OutlineExpect) { FlCmd_Border, &band, sizeof(band) }),
                    "the band takes the scaled rect, the stroke width on all four sides, radius, color and dash");
}

UTEST(painter_outline, line_is_a_two_point_polyline) {
    // The record is the polyline header followed inline by its points.
    const struct {
        FlCmdPolyline header;
        FlPolyPoint points[2];
    } line = { .header = { .color = OUTLINE_COLOR, .width = 3.0f, .point_count = 2 },
               .points = { { .x = 2.0f, .y = 4.0f, .intensity = 1.0f }, { .x = 6.0f, .y = 8.0f, .intensity = 1.0f } } };
    ASSERT_TRUE_MSG(outline_records(draw_line, (OutlineExpect) { FlCmd_Polyline, &line, sizeof(line) }),
                    "an open, unfilled stroke between the two scaled end points");
}

UTEST(painter_outline, circle_and_diamond_stroke_have_a_zero_alpha_fill) {
    const FlCmdCircle circle = { .cx = 10.0f,
                                 .cy = 12.0f,
                                 .radius = 14.0f,
                                 .border_color = OUTLINE_COLOR,
                                 .border_width = 3.0f,
                                 .stroke = { .dash_len = 4.0f, .gap_len = 6.0f } };
    ASSERT_TRUE_MSG(outline_records(draw_circle_stroke, (OutlineExpect) { FlCmd_Circle, &circle, sizeof(circle) }),
                    "the circle's ring is the stroke, dash included, over a fill that adds nothing");

    const FlCmdDiamond diamond
        = { .cx = 10.0f, .cy = 12.0f, .rx = 16.0f, .ry = 18.0f, .border_color = OUTLINE_COLOR, .border_width = 3.0f };
    ASSERT_TRUE_MSG(outline_records(draw_diamond_stroke, (OutlineExpect) { FlCmd_Diamond, &diamond, sizeof(diamond) }),
                    "the diamond's ring is the stroke over a fill that adds nothing");
}

UTEST(painter_outline, arc_takes_cap_and_width_from_the_stroke) {
    const FlCmdArc arc = { .cx = 10.0f,
                           .cy = 12.0f,
                           .radius = 14.0f,
                           .thickness = 4.0f,
                           .start_angle = 0.5f,
                           .sweep = 1.0f,
                           .color = OUTLINE_COLOR,
                           .flags = FL_IR_ARC_ROUND_CAPS };
    ASSERT_TRUE_MSG(outline_records(draw_round_capped_arc, (OutlineExpect) { FlCmd_Arc, &arc, sizeof(arc) }),
                    "lengths scale, angles do not, and a round cap sets the arc's round-caps bit");
}

static void draw_over_wide_rect_stroke(Painter* p) {
    painter_rect_stroke(p, (FlRect) { .w = 10.0f, .h = 6.0f }, 0.0f,
                        (FlStroke) { .width = 20.0f, .color = OUTLINE_COLOR });
}

static void draw_zero_length_line(Painter* p) {
    painter_line(p, (FlVec2) { .x = 1.0f, .y = 2.0f }, (FlVec2) { .x = 1.0f, .y = 2.0f }, k_outline);
}

UTEST(painter_outline, degenerate_outlines_stay_inside_their_shape) {
    // Half the smaller side (3 logical px) is the widest band that still fits; it scales to 6.
    const FlCmdBorder band = {
        .rect = { .w = 20.0f, .h = 12.0f }, .w_l = 6.0f, .w_r = 6.0f, .w_t = 6.0f, .w_b = 6.0f, .color = OUTLINE_COLOR
    };
    ASSERT_TRUE_MSG(outline_records(draw_over_wide_rect_stroke, (OutlineExpect) { FlCmd_Border, &band, sizeof(band) }),
                    "a stroke wider than the rect is clamped to half its smaller side");

    const FlCmdCircle dot = { .cx = 2.0f, .cy = 4.0f, .radius = 1.5f, .color = OUTLINE_COLOR };
    ASSERT_TRUE_MSG(outline_records(draw_zero_length_line, (OutlineExpect) { FlCmd_Circle, &dot, sizeof(dot) }),
                    "a zero-length line is a dot of the stroke width");
}
