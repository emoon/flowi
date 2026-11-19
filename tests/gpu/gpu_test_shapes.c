// GPU smoke test: cross-backend polyline, arc, circle, diamond and bar pixel tests.

#include "gpu_test_common.h"

// Shared polyline pixel cases: the optional-tier Polyline primitive running on both backends
// through the neutral seam. A polyline lowers to one draw command per edge: a stroke capsule (round caps/joins
// by policy) or, when filled, a fill-to-baseline trapezoid, with per-point intensity modulating stroke alpha.
// This suite pins both backends to the same coverage model within the shared pixel_color / channels_near
// tolerance, with no per-backend golden fork. Rendered over a transparent background (no clear command), so an

// The stroke / fill colours (opaque, so they store premultiplied to themselves).
static const FlColor poly_stroke = { 200, 40, 50, 255 };
static const FlColor poly_fill = { 40, 200, 60, 255 };

typedef struct ShapeCtx {
    FlRenderBackend* backend;
    FlSurface* surface;
    FlArena* ir_arena;
    FlRenderFrame frame;
    FlPixelReadback buf;
} ShapeCtx;

static bool render_and_read(ShapeCtx* c, const void* start) {
    ir_frame_render(c->backend, c->surface, &c->frame, c->ir_arena, start);
    return render_seam_read_pixels(c->backend, c->surface, &c->buf);
}

static PolylineStyle stroke_style(f32 width) {
    return (PolylineStyle) { .color = poly_stroke, .fill_color = poly_stroke, .width = width };
}

static int check_polyline_stroke(ShapeCtx* c) {
    // A thick horizontal stroke (width 7, half-width 3.5) centered on y = 100, with a clean AA ramp across the
    // top edge at y = 96.5 (a fractional edge lands on a pixel-row center, so it produces a partial pixel;
    // an integer edge would be exact-coverage).
    void* start = ir_rewind_begin(c->ir_arena);
    emit_polyline(c->ir_arena, (FlPolyPoint[]) { { 100.0f, 100.0f, 1.0f }, { 200.0f, 100.0f, 1.0f } }, 2,
                  stroke_style(7.0f));
    CHECK(render_and_read(c, start), "read_pixels failed on the horizontal stroke");
    CHECK(pixel_color(pixel_at(&c->buf, 150, 100), poly_stroke, 2.0f),
          "polyline: the stroke centerline is the opaque stroke color (on-line hit)");
    CHECK(pixel_transparent(pixel_at(&c->buf, 150, 90)),
          "polyline: a pixel well off the stroke stays fully transparent (off-line miss)");
    return check_aa_ramp_line(&c->buf, (AaRampRun) { 150, 94, 0, 1, 6 },
                              (AaRampSpec) { poly_stroke, true, "polyline horizontal stroke edge" });
}

static int check_polyline_diagonal_and_join(ShapeCtx* c) {
    void* start = ir_rewind_begin(c->ir_arena);
    emit_polyline(c->ir_arena, (FlPolyPoint[]) { { 100.0f, 300.0f, 1.0f }, { 300.0f, 500.0f, 1.0f } }, 2,
                  stroke_style(4.0f));
    CHECK(render_and_read(c, start), "read_pixels failed on the diagonal stroke");
    CHECK(pixel_at(&c->buf, 200, 400)[3] == 255, "polyline diagonal: the centerline midpoint is opaque (on-line hit)");
    CHECK(pixel_transparent(pixel_at(&c->buf, 240, 400)),
          "polyline diagonal: a pixel inside the bbox but off the line stays transparent (off-line miss)");

    // Round joins fall out of the overlapping end caps, so the vertex pixel where two capsules meet must be
    // fully covered, with no gap or seam at the join.
    start = ir_rewind_begin(c->ir_arena);
    emit_polyline(c->ir_arena,
                  (FlPolyPoint[]) { { 100.0f, 600.0f, 1.0f }, { 200.0f, 500.0f, 1.0f }, { 300.0f, 600.0f, 1.0f } }, 3,
                  stroke_style(6.0f));
    CHECK(render_and_read(c, start), "read_pixels failed on the bent stroke");
    CHECK(pixel_at(&c->buf, 200, 500)[3] == 255,
          "polyline join: the elbow where two capsules meet is fully covered (round join, no gap)");
    return 0;
}

static int check_polyline_closed(ShapeCtx* c) {
    // A closed polyline: a triangle A(100,100) B(300,100) C(200,250). FlPolylineFlags_closed emits the last->first
    // edge (C->A) too.
    void* start = ir_rewind_begin(c->ir_arena);
    PolylineStyle closed = stroke_style(6.0f);
    closed.flags = FlPolylineFlags_closed;
    emit_polyline(c->ir_arena,
                  (FlPolyPoint[]) { { 100.0f, 100.0f, 1.0f }, { 300.0f, 100.0f, 1.0f }, { 200.0f, 250.0f, 1.0f } }, 3,
                  closed);
    CHECK(render_and_read(c, start), "read_pixels failed on the closed polyline");
    const u8* top_edge = pixel_at(&c->buf, 200, 100);
    CHECK(pixel_color(top_edge, poly_stroke, 2.0f), "polyline closed: the top edge (A->B) is the opaque stroke color");
    const u8* closing_edge = pixel_at(&c->buf, 150, 175);
    CHECK(pixel_color(closing_edge, poly_stroke, 2.0f),
          "polyline closed: the closing edge (last->first, C->A) draws — its midpoint is the opaque stroke color");
    const u8* interior = pixel_at(&c->buf, 200, 150);
    CHECK(pixel_transparent(interior),
          "polyline closed: the interior stays untouched (a closed stroke is an outline, not a fill)");
    return 0;
}

static int check_polyline_fill(ShapeCtx* c) {
    // Fill-to-baseline: a horizontal curve at y = 700 filled down to baseline y = 800.
    void* start = ir_rewind_begin(c->ir_arena);
    PolylineStyle filled = { .color = poly_stroke,
                             .fill_color = poly_fill,
                             .width = 2.0f,
                             .baseline_y = 800.0f,
                             .flags = FlPolylineFlags_filled };
    emit_polyline(c->ir_arena, (FlPolyPoint[]) { { 100.0f, 700.0f, 1.0f }, { 300.0f, 700.0f, 1.0f } }, 2, filled);
    CHECK(render_and_read(c, start), "read_pixels failed on the filled polyline");
    const u8* between = pixel_at(&c->buf, 200, 750);
    CHECK(pixel_color(between, poly_fill, 2.0f),
          "polyline fill: a pixel between the curve and the baseline is the opaque fill color");
    const u8* above = pixel_at(&c->buf, 200, 650);
    CHECK(pixel_transparent(above), "polyline fill: a pixel above the curve stays transparent");
    const u8* below = pixel_at(&c->buf, 200, 803);
    CHECK(pixel_transparent(below), "polyline fill: a pixel below the baseline stays transparent");
    return 0;
}

static int check_polyline_intensity(ShapeCtx* c) {
    // Per-point intensity modulates the stroke alpha (and the premultiplied rgb with it).
    void* start = ir_rewind_begin(c->ir_arena);
    emit_polyline(c->ir_arena, (FlPolyPoint[]) { { 100.0f, 900.0f, 0.5f }, { 300.0f, 900.0f, 0.5f } }, 2,
                  stroke_style(8.0f));
    CHECK(render_and_read(c, start), "read_pixels failed on the intensity stroke");
    CHECK(pixel_color(pixel_at(&c->buf, 200, 900), (FlColor) { 100, 20, 25, 128 }, 3.0f),
          "polyline intensity: a uniform intensity of 0.5 halves the premultiplied stroke color and alpha");

    start = ir_rewind_begin(c->ir_arena);
    emit_polyline(c->ir_arena, (FlPolyPoint[]) { { 100.0f, 950.0f, 1.0f }, { 300.0f, 950.0f, 0.0f } }, 2,
                  stroke_style(8.0f));
    CHECK(render_and_read(c, start), "read_pixels failed on the intensity-gradient stroke");
    const u8* near_end = pixel_at(&c->buf, 140, 950);
    const u8* far_end = pixel_at(&c->buf, 260, 950);
    CHECK(near_end[3] > far_end[3],
          "polyline intensity: a 1.0 -> 0.0 gradient dims the stroke alpha along the segment");
    CHECK(near_end[3] > 180, "polyline intensity: near the 1.0 end the stroke stays near-opaque");
    CHECK(pixel_at(&c->buf, 285, 950)[3] < 30,
          "polyline intensity: near the 0.0 end the stroke fades to near-transparent (a real ramp, not a constant)");
    return 0;
}

typedef int (*ShapeCheckFn)(ShapeCtx*);

typedef struct ShapeSuite {
    const ShapeCheckFn* checks;
    size_t count;
    const char* name;
} ShapeSuite;

static int run_shape_checks(FlRenderBackend* backend, FlArena* ir_arena, const ShapeSuite* suite) {
    ShapeCtx ctx = {
        .backend = backend,
        .surface = backend->create_surface(backend->user_data, 1920, 1080),
        .ir_arena = ir_arena,
        .frame = test_frame(),
    };
    for (size_t i = 0; i < suite->count; i++) {
        if (suite->checks[i](&ctx) != 0) {
            return 1;
        }
    }

    printf("gpu_smoke_test: %s PASS\n", suite->name);
    backend->destroy_surface(backend->user_data, ctx.surface);
    return 0;
}

int run_polyline_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    static const ShapeCheckFn checks[] = {
        check_polyline_stroke, check_polyline_diagonal_and_join, check_polyline_closed,
        check_polyline_fill,   check_polyline_intensity,
    };
    const ShapeSuite suite = { checks, sizeof_array(checks), "polyline pixel tests" };
    return run_shape_checks(backend, ir_arena, &suite);
}

// Arc / ring / circle-sector pixel cases: the optional-tier arc
// SDF rendered end-to-end: a knob-indicator ring with butt caps, the same ring with round caps, a filled
// sector (thickness reaching the centre), and a full-sweep seamless ring. Both backends are pinned to the same
// coverage model and the same knob angle convention within the shared pixel_color / channels_near
// tolerance, with no per-backend golden fork.

// Sample the pixel whose center a knob-convention polar point lands on (angle 0 at 12 o'clock,
// positive clockwise), y-down pixel space. Callers put centers on the half-pixel grid so cardinal points land
// exactly on pixel centers; elsewhere the containing pixel's center is within ~0.71px of the analytic point,
// so checks keep at least that much margin from any shape edge.
static const u8* pixel_at_knob_angle(const FlPixelReadback* buf, FlVec2 center, f32 angle, f32 r) {
    int x = (int)(center.x + r * sinf(angle));
    int y = (int)(center.y - r * cosf(angle));
    return pixel_at(buf, x, y);
}

static const FlVec2 k_knob_center = { 100.5f, 100.5f };
static const f32 k_knob_end = 0.375f * TAU;
static const f32 k_knob_delta = 4.0f / 54.0f; // ~4px of arc length on the centerline

static int check_knob_arc(ShapeCtx* c) {
    // Knob indicator arc, butt caps
    // Centered (100.5, 100.5), outer radius 60, thickness 12 (ring band 48..60, centerline 54), sweeping three
    // quarter-turns from 7:30 through 12 to 4:30. The bottom quarter is the gap.
    const FlVec2 k = k_knob_center;
    void* start = ir_rewind_begin(c->ir_arena);
    ir_arc(c->ir_arena, k.x, k.y, 60.0f, 12.0f, -k_knob_end, 0.75f * TAU, green, 0, nullptr);
    CHECK(render_and_read(c, start), "read_pixels failed on the knob arc frame");

    const FlPixelReadback* buf = &c->buf;
    const u8* twelve = pixel_at_knob_angle(buf, k, 0.0f, 54.0f);
    CHECK(pixel_color(twelve, green, 2.0f), "knob arc: the centerline at 12 o'clock is solid");
    const u8* three = pixel_at_knob_angle(buf, k, 0.25f * TAU, 54.0f);
    const u8* nine = pixel_at_knob_angle(buf, k, -0.25f * TAU, 54.0f);
    CHECK(pixel_color(three, green, 2.0f) && pixel_color(nine, green, 2.0f),
          "knob arc: the centerline at 3 and 9 o'clock is solid");
    const u8* six = pixel_at_knob_angle(buf, k, 0.5f * TAU, 54.0f);
    CHECK(pixel_transparent(six), "knob arc: the gap at 6 o'clock stays transparent");
    const u8* face = pixel_at(buf, 100, 100);
    CHECK(pixel_transparent(face), "knob arc: the ring interior (the knob face) stays transparent");
    const u8* inside = pixel_at_knob_angle(buf, k, 0.0f, 40.0f);
    CHECK(pixel_transparent(inside), "knob arc: inside the inner radius stays transparent");
    const u8* outside = pixel_at_knob_angle(buf, k, 0.0f, 70.0f);
    CHECK(pixel_transparent(outside), "knob arc: outside the outer radius stays transparent");

    // Radial AA walking inward across the outer edge at 3 o'clock (edge at x = 160.5).
    if (check_aa_ramp_line(&c->buf, (AaRampRun) { 165, 100, -1, 0, 11 },
                           (AaRampSpec) { green, true, "knob arc outer edge" })
        != 0) {
        return 1;
    }

    // Butt caps cut sharp at both end angles (~4px of centerline arc past the edge is fully clear).
    const u8* before_end = pixel_at_knob_angle(buf, k, k_knob_end - k_knob_delta, 54.0f);
    CHECK(pixel_color(before_end, green, 2.0f), "knob arc: just inside the end angle the centerline is solid");
    const u8* past_end = pixel_at_knob_angle(buf, k, k_knob_end + k_knob_delta, 54.0f);
    CHECK(pixel_transparent(past_end), "knob arc: a butt cap cuts sharp past the end angle");
    const u8* past_start = pixel_at_knob_angle(buf, k, -k_knob_end - k_knob_delta, 54.0f);
    CHECK(pixel_transparent(past_start), "knob arc: the start cap cuts sharp too");
    return 0;
}

static int check_knob_round_caps(ShapeCtx* c) {
    // The same knob with round caps
    // The cap disc (radius 6) covers ~4px past the end angle; ~10px past stays clear.
    const FlVec2 k = k_knob_center;
    void* start = ir_rewind_begin(c->ir_arena);
    ir_arc(c->ir_arena, k.x, k.y, 60.0f, 12.0f, -k_knob_end, 0.75f * TAU, green, FL_IR_ARC_ROUND_CAPS, nullptr);
    CHECK(render_and_read(c, start), "read_pixels failed on the round-cap frame");
    CHECK(pixel_color(pixel_at_knob_angle(&c->buf, k, k_knob_end + k_knob_delta, 54.0f), green, 2.0f),
          "knob arc round caps: the cap disc covers past the end angle");
    CHECK(pixel_transparent(pixel_at_knob_angle(&c->buf, k, k_knob_end + 10.0f / 54.0f, 54.0f)),
          "knob arc round caps: the cap disc ends at its radius");
    return 0;
}

static int check_sector_and_ring(ShapeCtx* c) {
    // Filled sector and full ring
    // Thickness reaching the center makes a pie wedge; a full-turn sweep is a seamless ring.
    const FlVec2 sector = { 300.5f, 100.5f };
    const FlVec2 ring = { 500.5f, 100.5f };
    void* start = ir_rewind_begin(c->ir_arena);
    ir_arc(c->ir_arena, sector.x, sector.y, 40.0f, 40.0f, 0.0f, 0.25f * TAU, red, 0, nullptr);
    ir_arc(c->ir_arena, ring.x, ring.y, 40.0f, 8.0f, 0.0f, TAU, blue, 0, nullptr);
    CHECK(render_and_read(c, start), "read_pixels failed on the sector/ring frame");

    const FlPixelReadback* buf = &c->buf;
    const u8* wedge = pixel_at_knob_angle(buf, sector, 0.125f * TAU, 20.0f);
    CHECK(pixel_color(wedge, red, 2.0f), "filled sector: the wedge interior is solid");
    const u8* apex = pixel_at_knob_angle(buf, sector, 0.125f * TAU, 4.0f);
    CHECK(pixel_color(apex, red, 2.0f), "filled sector: solid right up near the apex");
    const u8* opposite = pixel_at_knob_angle(buf, sector, 0.625f * TAU, 20.0f);
    CHECK(pixel_transparent(opposite), "filled sector: the opposite quadrant stays transparent");
    const u8* before_edge = pixel_at_knob_angle(buf, sector, -0.02f * TAU, 20.0f);
    const u8* after_edge = pixel_at_knob_angle(buf, sector, 0.02f * TAU, 20.0f);
    CHECK(pixel_transparent(before_edge) && pixel_color(after_edge, red, 2.0f),
          "filled sector: the butt edge at 12 o'clock cuts sharp");

    for (int i = 0; i < 4; i++) {
        const f32 a = 128.0f / 255.0f;
        CHECK(channels_near(pixel_at_knob_angle(&c->buf, ring, 0.25f * TAU * (f32)i, 36.0f),
                            (ChannelExpect) { 20.0f * a, 40.0f * a, 200.0f * a, 128.0f }, 3.0f),
              "full ring: every cardinal renders, half-alpha premultiplied, with no seam");
    }
    return 0;
}

int run_arc_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    static const ShapeCheckFn checks[] = { check_knob_arc, check_knob_round_caps, check_sector_and_ring };
    const ShapeSuite suite = { checks, sizeof_array(checks), "arc pixel tests" };
    return run_shape_checks(backend, ir_arena, &suite);
}

// Circle / diamond pixel cases: the optional-tier filled-SDF
// shapes rendered end-to-end: a bordered circle (inward border ring), a borderless circle, a bordered diamond,
// and a clip-cut circle. Both backends are pinned to the same coverage model and the same border-ring split
// (outer-minus-inner coverage) within the shared pixel_color tolerance, with no per-backend golden fork.
static void emit_circle_diamond_scene(FlArena* ir_arena) {
    // Circles and diamond (modulation points / automation nodes), plus a clip cut
    ir_circle(ir_arena, 200.5f, 300.5f, 30.0f, red, green, 6.0f, nullptr); // bordered: fill inside r = 24
    ir_circle(ir_arena, 300.5f, 300.5f, 20.0f, red, green, 0.0f, nullptr); // borderless: green never shows
    // Dashed ring, 6 on 6 off around the ring centerline at radius 18 (circumference 113.10). Nine periods
    // fit closest, so the rescale lands the pattern at 12.57 with a 6.28 dash, putting the top of the ring
    // a quarter period into a dash and the bottom a quarter period into a gap.
    ir_circle_stroked(ir_arena, 700.5f, 300.5f, 20.0f, red, green, 4.0f, (FlStrokeStyle) { 6.0f, 6.0f }, nullptr);
    ir_diamond(ir_arena, 400.5f, 300.5f, 30.0f, 20.0f, red, green, 4.0f, nullptr);
    ir_clip_push(ir_arena, (FlRect) { 600.0f, 280.0f, 21.0f, 41.0f }); // cuts at x = 621
    ir_circle(ir_arena, 620.5f, 300.5f, 15.0f, red, red, 0.0f, nullptr);
    ir_clip_pop(ir_arena);
}

static int check_circle_pixels(const FlPixelReadback* buf) {
    int status = CHECK_PROBES(buf, PROBE_COLOR(200, 300, red, "circle: the center is the fill color"),
                              PROBE_COLOR(215, 300, red, "circle: inside the border ring is still fill"),
                              PROBE_COLOR(227, 300, green, "circle: the rim band is the border color"),
                              PROBE_CLEAR(235, 300, "circle: outside the radius is transparent"));
    if (status != 0) {
        return 1;
    }
    // AA at the outer rim (edge at x = 230.5): the ramp lands in the border color.
    status = check_aa_ramp_line(buf, (AaRampRun) { 233, 300, -1, 0, 6 }, (AaRampSpec) { green, true, "circle rim" });
    if (status != 0) {
        return 1;
    }
    return CHECK_PROBES(buf, PROBE_COLOR(300, 300, red, "circle: a zero border width renders pure fill out to the rim"),
                        PROBE_COLOR(318, 300, red, "circle: a zero border width renders pure fill out to the rim"),
                        PROBE_CLEAR(322, 300, "circle: a zero border width still ends at the radius"));
}

static int check_diamond_pixels(const FlPixelReadback* buf) {
    // Diamond: fill inside the 4px-inset rhombus, the border band along the edge and at both vertices. (The
    // edge normal scale is rx*ry/len = 16.64, so the sampled offsets sit well clear of both band edges.)
    int status = CHECK_PROBES(buf, PROBE_COLOR(400, 300, red, "diamond: the center is the fill color"),
                              PROBE_COLOR(409, 306, red, "diamond: inside the inset fill region"),
                              PROBE_COLOR(413, 309, green, "diamond: the border band along the slant edge"),
                              PROBE_CLEAR(418, 312, "diamond: outside the rhombus is transparent"));
    if (status != 0) {
        return 1;
    }
    status
        = CHECK_PROBES(buf, PROBE_COLOR(427, 300, green, "diamond: just inside the x and y vertices is border color"),
                       PROBE_COLOR(400, 282, green, "diamond: just inside the x and y vertices is border color"),
                       PROBE_CLEAR(433, 300, "diamond: just outside the x vertex is transparent"));
    if (status != 0) {
        return 1;
    }
    // AA across the slant edge at pixel row 309 (centers y = 309.5, edge at x = 417.0, ~1.8px ramp
    // width): the partials land in the border color.
    return check_aa_ramp_line(buf, (AaRampRun) { 421, 309, -1, 0, 7 },
                              (AaRampSpec) { green, true, "diamond slant edge" });
}

static int check_dashed_and_clipped_circles(const FlPixelReadback* buf) {
    int status = CHECK_PROBES(
        buf, PROBE_COLOR(700, 282, green, "dashed circle: the ring is drawn inside a dash"),
        PROBE_CLEAR(700, 318, "dashed circle: the ring is cut away inside a gap"),
        PROBE_COLOR(700, 300, red, "dashed circle: the fill disc is untouched by the mask"),
        PROBE_COLOR(700, 314, red, "dashed circle: the fill still covers the disc under a gap, out towards the ring"));
    if (status != 0) {
        return 1;
    }
    return CHECK_PROBES(buf, PROBE_COLOR(612, 300, red, "clipped circle: inside both the circle and the clip renders"),
                        PROBE_CLEAR(625, 300, "clipped circle: the clip cuts the circle even inside its radius"));
}

int run_circle_diamond_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    FlRenderFrame frame = test_frame();
    FlPixelReadback buf;

    void* start = ir_rewind_begin(ir_arena);
    emit_circle_diamond_scene(ir_arena);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the circle/diamond frame");

    if (check_circle_pixels(&buf) != 0) {
        return 1;
    }
    if (check_diamond_pixels(&buf) != 0) {
        return 1;
    }
    if (check_dashed_and_clipped_circles(&buf) != 0) {
        return 1;
    }

    printf("gpu_smoke_test: circle/diamond pixel tests PASS\n");
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

// Meter-bar pixel cases: the batched optional-tier primitive
// rendered end-to-end: a single bar-set record off a shared baseline carrying a full row of bars. Bar sets are
// batched at the IR record level (one FlCmd_Bar carries the whole row) and lower CPU-side to one GpuDraw_Rect
// per bar off the baseline. Rendering the whole row from one ir_bars record is the readback that proves the
// record stays batched (not degraded to one fill-rect call per bar).
int run_bar_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    FlRenderFrame frame = test_frame();
    FlPixelReadback buf;

    const FlColor green = { 40, 200, 60, 255 };

    // Meter bars
    // One bar-set record off a baseline at y = 400: two upward bars, a gap, and a negative bar hanging below.
    void* start = ir_rewind_begin(ir_arena);
    FlBar* bars = ir_bars(ir_arena, 400.0f, green, 3, nullptr);
    bars[0] = (FlBar) { 50.0f, 6.0f, 30.0f };
    bars[1] = (FlBar) { 60.0f, 6.0f, 60.0f };
    bars[2] = (FlBar) { 70.0f, 6.0f, -20.0f };
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the meter bar frame");

    int status
        = CHECK_PROBES(&buf, PROBE_COLOR(52, 380, green, "meter bars: pixels inside each bar are solid"),
                       PROBE_COLOR(62, 350, green, "meter bars: pixels inside each bar are solid"),
                       PROBE_CLEAR(52, 360, "meter bars: above a bar's height stays transparent"),
                       PROBE_CLEAR(57, 380, "meter bars: the gap between bars stays transparent"),
                       PROBE_COLOR(72, 410, green, "meter bars: a negative height hangs below the baseline"),
                       PROBE_CLEAR(72, 395, "meter bars: bars extend from the baseline in one direction only"),
                       PROBE_CLEAR(52, 405, "meter bars: bars extend from the baseline in one direction only"),
                       PROBE_COLOR(52, 370, green, "meter bars: an integer bar top cuts exactly on the pixel boundary"),
                       PROBE_CLEAR(52, 369, "meter bars: an integer bar top cuts exactly on the pixel boundary"));
    if (status != 0) {
        return 1;
    }

    printf("gpu_smoke_test: bar pixel tests PASS\n");
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}
