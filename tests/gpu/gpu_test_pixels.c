// GPU smoke test: hand-built IR pixel tests, border pixel tests and image tests.

#include "gpu_test_common.h"

#if !defined(__APPLE__)

// Part B: hand-built IR in pixel coordinates, driving the backend seam directly. The readback is
// premultiplied coverage alpha over a transparent target, so every expectation below is computable.

typedef struct PixCtx {
    FlRenderBackend* backend;
    FlSurface* surface;
    FlArena* ir_arena;
    FlArena* scratch;
    FlRenderFrame frame;
    FlPixelReadback buf;
} PixCtx;

typedef int (*PixCheckFn)(PixCtx*);

typedef struct PixSuite {
    const PixCheckFn* checks;
    size_t count;
    const char* pass_message; // nullptr: the suite prints no PASS line
} PixSuite;

static int run_pix_suite(FlRenderBackend* backend, FlArena* ir_arena, const PixSuite* suite) {
    PixCtx c = {
        .backend = backend,
        .surface = backend->create_surface(backend->user_data, 1920, 1080),
        .ir_arena = ir_arena,
        .frame = test_frame(),
    };
    for (size_t i = 0; i < suite->count; i++) {
        if (suite->checks[i](&c) != 0) {
            return 1;
        }
    }
    if (suite->pass_message != nullptr) {
        printf("gpu_smoke_test: %s PASS\n", suite->pass_message);
    }
    backend->destroy_surface(backend->user_data, c.surface);
    return 0;
}

#define RUN_PIX_SUITE(backend, ir_arena, message, ...)                         \
    do {                                                                       \
        static const PixCheckFn checks_[] = { __VA_ARGS__ };                   \
        const PixSuite suite_ = { checks_, sizeof_array(checks_), (message) }; \
        return run_pix_suite((backend), (ir_arena), &suite_);                  \
    } while (0)

static bool pix_render_read(PixCtx* c, const void* start) {
    ir_frame_render(c->backend, c->surface, &c->frame, c->ir_arena, start);
    return render_seam_read_pixels(c->backend, c->surface, &c->buf);
}

static void fill_four_corners(FlArena* arena, FlRect rect, FlCornerRadius radius, const FlColor* corners) {
    ir_fill_rect(arena, rect, radius, corners, nullptr);
}

static void fill_uniform(FlArena* arena, FlRect rect, FlCornerRadius radius, FlColor c) {
    ir_fill_rect(arena, rect, radius, (FlColor[4]) { c, c, c, c }, nullptr);
}

// Part B: hand-built IR in pixel coordinates, driving the backend seam directly. The readback is
// premultiplied coverage alpha over a transparent target, so every expectation below is computable.

static int check_direct_ir_blend(PixCtx* c) {
    // Src-over blend ordering and coverage alpha over the transparent target.
    FlArena* a = c->ir_arena;
    void* start = ir_rewind_begin(a);
    emit_solid_rect(a, (FlRect) { 0.0f, 0.0f, 256.0f, 256.0f }, red); // A: opaque base
    emit_solid_rect(a, (FlRect) { 192.0f, 64.0f, 128.0f, 128.0f },
                    blue);                                              // B: half-transparent, overlaps A's right edge
    emit_solid_rect(a, (FlRect) { 300.5f, 8.0f, 20.0f, 16.0f }, green); // C: fractional left edge
    CHECK(pix_render_read(c, start), "read_pixels failed after direct IR frame");

    const f32 ab = 128.0f / 255.0f; // B's alpha
    CHECK(pixel_color(pixel_at(&c->buf, 16, 16), red, 2.0f), "direct IR: pixel inside A only");
    const u8* blended = pixel_at(&c->buf, 200, 100);
    CHECK(channels_near(blended,
                        (ChannelExpect) { 200.0f * (1 - ab) + 20.0f * ab, 40.0f, 50.0f * (1 - ab) + 200.0f * ab, 255 },
                        3.0f),
          "direct IR: B should src-over blend onto A");
    const u8* b_alone = pixel_at(&c->buf, 280, 100);
    CHECK(channels_near(b_alone, (ChannelExpect) { 20.0f * ab, 40.0f * ab, 200.0f * ab, 128 }, 3.0f),
          "direct IR: B alone should land premultiplied with its own alpha");
    return 0;
}

static int check_direct_ir_edges(PixCtx* c) {
    // Pixel (300, 10) spans x 300..301; C starts at 300.5 -> exactly half coverage of an opaque color.
    const u8* edge = pixel_at(&c->buf, 300, 10);
    CHECK(channels_near(edge, (ChannelExpect) { 40.0f * 0.5f, 200.0f * 0.5f, 60.0f * 0.5f, 127.5f }, 3.0f),
          "direct IR: a half-covered edge pixel should carry half coverage alpha");
    CHECK(pixel_transparent(pixel_at(&c->buf, 400, 300)), "direct IR: untouched pixels should stay fully transparent");
    return 0;
}

static int check_direct_ir_global_alpha(PixCtx* c) {
    const f32 ab = 128.0f / 255.0f;
    void* start = ir_rewind_begin(c->ir_arena);
    emit_solid_rect(c->ir_arena, (FlRect) { 0.0f, 0.0f, 64.0f, 64.0f }, red);
    c->frame.global_alpha = 128;
    CHECK(pix_render_read(c, start), "read_pixels failed after global_alpha frame");
    CHECK(channels_near(pixel_at(&c->buf, 16, 16), (ChannelExpect) { 200.0f * ab, 40.0f * ab, 50.0f * ab, 128 }, 3.0f),
          "direct IR: global_alpha must halve an opaque rect");
    CHECK(pixel_transparent(pixel_at(&c->buf, 200, 100)), "direct IR: the second frame must fully replace the first");
    return 0;
}

static void emit_rounded_gradient_scene(FlArena* ir_arena) {
    const FlColor pure_red = { 255, 0, 0, 255 };
    const FlColor pure_green = { 0, 255, 0, 255 };
    const FlColor pure_blue = { 0, 0, 255, 255 };
    const FlColor black = { 0, 0, 0, 255 };
    const FlColor half_red = { 200, 40, 50, 128 };
    const FlColor half_blue = { 20, 40, 200, 128 };

    // R1: one rounded corner (TL, radius 16) mixed with three square ones.
    fill_uniform(ir_arena, (FlRect) { 32, 32, 64, 64 }, (FlCornerRadius) { .tl = 16.0f }, red);
    // R2: all four corners rounded, radius 8.
    fill_uniform(ir_arena, (FlRect) { 128, 32, 64, 64 }, (FlCornerRadius) { 8, 8, 8, 8 }, red);
    // G1: horizontal gradient black -> red.
    fill_four_corners(ir_arena, (FlRect) { 0, 128, 128, 32 }, (FlCornerRadius) { 0 },
                      (FlColor[4]) { black, pure_red, black, pure_red });
    // G2: vertical gradient green -> blue.
    fill_four_corners(ir_arena, (FlRect) { 160, 128, 32, 128 }, (FlCornerRadius) { 0 },
                      (FlColor[4]) { pure_green, pure_green, pure_blue, pure_blue });
    // G3: four distinct corners (corner-order check: TL red, TR green, BL blue, BR white).
    fill_four_corners(ir_arena, (FlRect) { 224, 128, 64, 64 }, (FlCornerRadius) { 0 },
                      (FlColor[4]) { pure_red, pure_green, pure_blue, white });
    // G4: half-alpha gradient over the transparent target (premultiplied-output check).
    fill_four_corners(ir_arena, (FlRect) { 320, 128, 64, 32 }, (FlCornerRadius) { 0 },
                      (FlColor[4]) { half_red, half_blue, half_red, half_blue });
}

static int check_rounded_r1(PixCtx* c) {
    // Rounded corners and per-corner gradients
    // One frame of non-overlapping rects; every expectation below is a property of the SDF coverage model
    // (clamp on distance) and bilinear corner interpolation, not a golden image.
    void* start = ir_rewind_begin(c->ir_arena);
    emit_rounded_gradient_scene(c->ir_arena);
    c->frame.global_alpha = 255;
    CHECK(pix_render_read(c, start), "read_pixels failed on the rounded/gradient frame");

    // R1 inside/outside sampling: the TL corner circle is centered at (48, 48) with radius 16.
    CHECK(pixel_transparent(pixel_at(&c->buf, 33, 33)),
          "rounded: a pixel outside the corner arc must stay fully transparent");
    CHECK(pixel_color(pixel_at(&c->buf, 40, 40), red, 2.0f),
          "rounded: a pixel inside the corner arc must be the exact solid fill");
    int ramp = check_aa_ramp_line(&c->buf, (AaRampRun) { 33, 33, 1, 1, 6 },
                                  (AaRampSpec) { red, true, "rounded corner diagonal" });
    if (ramp != 0) {
        return 1;
    }
    return CHECK_PROBES(&c->buf,
                        PROBE_COLOR(95, 32, red, "rounded: a radius-0 corner mixed with a rounded one must stay sharp"),
                        PROBE_CLEAR(96, 32, "rounded: nothing may bleed past a radius-0 corner"));
}

static int check_rounded_r2(PixCtx* c) {
    // R2: all four corner-tip pixels are outside their arcs (tip-to-center distance ~10.6 > 8.5); all four
    // edge midpoints are on straight edges and fully covered.
    static const char* const tips = "rounded: every corner-tip pixel of a uniformly rounded rect must be transparent";
    static const char* const mids = "rounded: straight-edge midpoints of a rounded rect must be fully covered";
    return CHECK_PROBES(&c->buf, PROBE_CLEAR(128, 32, tips), PROBE_CLEAR(191, 32, tips), PROBE_CLEAR(128, 95, tips),
                        PROBE_CLEAR(191, 95, tips), PROBE_COLOR(160, 32, red, mids), PROBE_COLOR(160, 95, red, mids),
                        PROBE_COLOR(128, 63, red, mids), PROBE_COLOR(191, 63, red, mids));
}

static int check_g1_horizontal(PixCtx* c) {
    u8 prev_r = 0;
    for (int x = 0; x < 128; x++) {
        const u8* gpx = pixel_at(&c->buf, x, 144);
        CHECK(gpx[0] >= prev_r, "gradient: red must be monotonic along the horizontal axis");
        CHECK(gpx[1] == 0 && gpx[2] == 0, "gradient: black->red must not invent green or blue");
        CHECK(gpx[3] == 255, "gradient: an opaque gradient stays fully opaque");
        prev_r = gpx[0];
    }
    const u8 left_end = pixel_at(&c->buf, 0, 144)[0];
    CHECK(left_end < 8, "gradient: the left end must be near black");
    const u8 right_end = pixel_at(&c->buf, 127, 144)[0];
    CHECK(right_end > 247, "gradient: the right end must be near pure red");
    const u8* mid = pixel_at(&c->buf, 64, 144);
    CHECK(mid[0] > 120 && mid[0] < 136, "gradient: the midpoint must sit near half intensity");
    return 0;
}

static int check_g2_vertical(PixCtx* c) {
    // G2: the same property on the vertical axis (catches a u/v swap).
    u8 prev_b = 0;
    for (int y = 128; y < 256; y++) {
        const u8* gpx = pixel_at(&c->buf, 176, y);
        CHECK(gpx[2] >= prev_b, "gradient: blue must be monotonic along the vertical axis");
        CHECK((int)gpx[1] + (int)gpx[2] > 240, "gradient: green->blue must conserve intensity");
        prev_b = gpx[2];
    }
    const u8 top_end = pixel_at(&c->buf, 176, 128)[1];
    CHECK(top_end > 247, "gradient: the top end must be near pure green");
    const u8 bottom_end = pixel_at(&c->buf, 176, 255)[2];
    CHECK(bottom_end > 247, "gradient: the bottom end must be near pure blue");
    return 0;
}

static int check_g3_corners(PixCtx* c) {
    // G3: catches TL/TR/BL/BR order mixups in either the translator packing or the shader's quadrant mapping.
    const u8* tl = pixel_at(&c->buf, 226, 130);
    CHECK(tl[0] > 200 && tl[1] < 40 && tl[2] < 40, "gradient corners: TL must be red-dominated");
    const u8* tr = pixel_at(&c->buf, 285, 130);
    CHECK(tr[1] > 200 && tr[0] < 40 && tr[2] < 40, "gradient corners: TR must be green-dominated");
    return 0;
}

static int check_g3_bottom_and_g4(PixCtx* c) {
    const u8* bl = pixel_at(&c->buf, 226, 190);
    CHECK(bl[2] > 200 && bl[0] < 40 && bl[1] < 40, "gradient corners: BL must be blue-dominated");
    const u8* br = pixel_at(&c->buf, 285, 190);
    CHECK(br[0] > 200 && br[1] > 200 && br[2] > 200, "gradient corners: BR must be white-dominated");

    // G4: an interior pixel of a half-alpha gradient over the transparent target: the stored alpha is the
    // interpolated alpha (no coverage loss in the interior) and RGB is premultiplied by it. At pixel center
    // x = 352.5, u = 32.5/64, the interpolated color is (200 - 180u, 40, 50 + 150u) at alpha 128.
    const f32 u = 32.5f / 64.0f;
    const f32 a = 128.0f / 255.0f;
    CHECK(channels_near(pixel_at(&c->buf, 352, 144),
                        (ChannelExpect) { (200.0f - 180.0f * u) * a, 40.0f * a, (50.0f + 150.0f * u) * a, 128.0f },
                        3.0f),
          "gradient: a half-alpha gradient must land premultiplied with the interpolated alpha");
    return 0;
}

static void emit_pixel_clip_scene(FlArena* ir_arena) {
    // K1: a 64x64 red rect clipped to {16,16,32,32}: every visible pixel comes from the clip, not the rect.
    ir_clip_push(ir_arena, (FlRect) { 16, 16, 32, 32 });
    emit_solid_rect(ir_arena, (FlRect) { 0.0f, 0.0f, 64.0f, 64.0f }, red);
    ir_clip_pop(ir_arena);
    // K2: a green rect whose clip starts at the fractional x = 100.5, so pixel column 100 is half covered.
    ir_clip_push(ir_arena, (FlRect) { 100.5f, 0.0f, 32.0f, 32.0f });
    emit_solid_rect(ir_arena, (FlRect) { 96.0f, 0.0f, 64.0f, 32.0f }, green);
    ir_clip_pop(ir_arena);
    // K3: a rounded rect (TL radius 16, arc circle centered at (208,48)) clipped to its left 8 pixels
    // ({192..200} x {32..96}); the clip edge at x = 200 slices through the corner arc.
    ir_clip_push(ir_arena, (FlRect) { 192, 32, 8, 64 });
    fill_uniform(ir_arena, (FlRect) { 192, 32, 64, 64 }, (FlCornerRadius) { .tl = 16.0f }, red);
    ir_clip_pop(ir_arena);
}

static int check_clip_integer(PixCtx* c) {
    // Clip cut: pixels land exactly at clip boundaries
    void* start = ir_rewind_begin(c->ir_arena);
    emit_pixel_clip_scene(c->ir_arena);
    CHECK(pix_render_read(c, start), "read_pixels failed on the clip frame");

    static const char* const inside = "clip: pixels inside an integer clip keep full coverage";
    static const char* const edges = "clip: the rect must not bleed past any integer clip edge";
    return CHECK_PROBES(&c->buf, PROBE_COLOR(16, 16, red, inside), PROBE_COLOR(47, 47, red, inside),
                        PROBE_CLEAR(15, 30, edges), PROBE_CLEAR(48, 30, edges), PROBE_CLEAR(30, 15, edges),
                        PROBE_CLEAR(30, 48, edges));
}

static int check_clip_fractional(PixCtx* c) {
    // K2: the fractional clip edge at x = 100.5 half-covers pixel 100 (centers at 100.5), exactly like a
    // fractional rect edge would.
    CHECK(pixel_transparent(pixel_at(&c->buf, 99, 10)), "clip: pixels left of a fractional clip edge stay transparent");
    const u8* half = pixel_at(&c->buf, 100, 10);
    CHECK(channels_near(half, (ChannelExpect) { 40.0f * 0.5f, 200.0f * 0.5f, 60.0f * 0.5f, 127.5f }, 3.0f),
          "clip: a fractionally clipped pixel carries analytic half coverage");
    CHECK(pixel_color(pixel_at(&c->buf, 101, 10), green, 2.0f),
          "clip: pixels right of a fractional clip edge keep full coverage");
    return 0;
}

static int check_clip_rounded(PixCtx* c) {
    static const char* const past_edge = "clip on a rounded corner: nothing lands past the clip edge, arc or not";
    return CHECK_PROBES(
        &c->buf,
        PROBE_COLOR(199, 44, red, "clip on a rounded corner: inside both the arc and the clip is the solid fill"),
        PROBE_CLEAR(200, 44, past_edge), PROBE_CLEAR(201, 80, past_edge),
        PROBE_CLEAR(193, 33, "clip on a rounded corner: outside the arc stays transparent inside the clip"),
        PROBE_COLOR(193, 90, red,
                    "clip on a rounded corner: the straight edge below the corner still fills inside the clip"));
}

static int check_tile_overflow(PixCtx* c) {
    // Tile-list capacity: the per-tile index list holds 64 entries; commands beyond that are counted as
    // overflow and dropped. 64 red rects then 6 blue ones on a single 8x8 tile (pixels 32..40): the blues
    // land past capacity, so the pixel must stay red and uncorrupted.
    void* start = ir_rewind_begin(c->ir_arena);
    for (int i = 0; i < 64; i++) {
        emit_solid_rect(c->ir_arena, (FlRect) { 32.0f, 32.0f, 8.0f, 8.0f }, red);
    }
    for (int i = 0; i < 6; i++) {
        emit_solid_rect(c->ir_arena, (FlRect) { 32.0f, 32.0f, 8.0f, 8.0f }, (FlColor) { 20, 40, 200, 255 });
    }
    CHECK(pix_render_read(c, start), "read_pixels failed after overflow frame");
    return CHECK_PROBES(
        &c->buf,
        PROBE_COLOR(36, 36, red,
                    "overflow: only the first 64 commands on a tile may render, without corrupting the pixel"),
        PROBE_CLEAR(48, 36, "overflow: the stack must stay confined to its own tile"));
}

int run_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    RUN_PIX_SUITE(backend, ir_arena, nullptr, check_direct_ir_blend, check_direct_ir_edges,
                  check_direct_ir_global_alpha, check_rounded_r1, check_rounded_r2, check_g1_horizontal,
                  check_g2_vertical, check_g3_corners, check_g3_bottom_and_g4, check_clip_integer,
                  check_clip_fractional, check_clip_rounded, check_tile_overflow);
}

// Border pixel tests: every expectation is a property of the outer-minus-inner SDF coverage model.

static void emit_border_scene(FlArena* ir_arena) {
    // B1: square border, uniform width 4; bands at {32..36} and {92..96} on each axis.
    ir_border(ir_arena, (FlRect) { 32, 32, 64, 64 }, 4, 4, 4, 4, (FlCornerRadius) { 0 }, red);
    // B2: per-side widths l=8 r=2 t=4 b=0; each side's band is its own width; the bottom has none.
    ir_border(ir_arena, (FlRect) { 128, 32, 64, 64 }, 8, 2, 4, 0, (FlCornerRadius) { 0 }, green);
    // B3: rounded, radius 16, width 4; the TL outer arc is centered at (240, 48), outer 16, inner 12.
    ir_border(ir_arena, (FlRect) { 224, 32, 64, 64 }, 4, 4, 4, 4, (FlCornerRadius) { 16, 16, 16, 16 }, red);
    // B4: width 10 past the radius 6; the inner corner at (330, 42) is sharp.
    ir_border(ir_arena, (FlRect) { 320, 32, 64, 64 }, 10, 10, 10, 10, (FlCornerRadius) { 6, 6, 6, 6 }, red);
    // B5: widths consuming the rect, which renders as a filled rounded rect in the border color.
    ir_border(ir_arena, (FlRect) { 416, 32, 64, 64 }, 40, 40, 40, 40, (FlCornerRadius) { 8, 8, 8, 8 }, red);
    // B6: half-alpha border; band pixels land premultiplied like every other kind.
    ir_border(ir_arena, (FlRect) { 512, 32, 64, 64 }, 4, 4, 4, 4, (FlCornerRadius) { 0 }, blue);
    // B7: rounded with per-side widths; each corner's inner radius derives from its own two adjacent
    // sides (TL pairs left+top, TR right+top, BL left+bottom, BR right+bottom): radius 12 with widths
    // l=8 r=2 t=6 b=1 gives inner radii TL 4, TR 6, BL 4, BR 10, with the inner rect at {616, 38, 54, 57}.
    ir_border(ir_arena, (FlRect) { 608, 32, 64, 64 }, 8, 2, 6, 1, (FlCornerRadius) { 12, 12, 12, 12 }, red);
    // B8: fractional uniform width 4.5; the inner edge sits on a half-pixel boundary at x = 708.5.
    ir_border(ir_arena, (FlRect) { 704, 32, 64, 64 }, 4.5f, 4.5f, 4.5f, 4.5f, (FlCornerRadius) { 0 }, red);
    // B9: dashed, 8 on 8 off. Both runs are 64 long, an exact 4 periods, so the rescale is the identity and
    // the pattern lands on known coordinates: dashes at 0..8, 16..24, 32..40, 48..56 along each edge,
    // measured from the outer rect's origin down whichever axis the nearest edge runs.
    ir_border_stroked(ir_arena, (FlRect) { 800, 32, 64, 64 }, 4, 4, 4, 4, (FlCornerRadius) { 0 }, red,
                      (FlStrokeStyle) { 8.0f, 8.0f });
}

static int check_border_uniform(PixCtx* c) {
    // One frame of non-overlapping borders.
    void* start = ir_rewind_begin(c->ir_arena);
    emit_border_scene(c->ir_arena);
    CHECK(pix_render_read(c, start), "read_pixels failed on the border frame");

    static const char* const bands = "border: all four side bands must be hit";
    static const char* const outside = "border: nothing may land outside the outer edge";
    static const char* const cut = "border: an integer inner edge cuts exactly between band and hole";
    return CHECK_PROBES(&c->buf, PROBE_COLOR(33, 64, red, bands), PROBE_COLOR(94, 64, red, bands),
                        PROBE_COLOR(64, 33, red, bands), PROBE_COLOR(64, 94, red, bands),
                        PROBE_COLOR(33, 33, red, "border: a square corner pixel sits in both bands and must be hit"),
                        PROBE_CLEAR(64, 64, "border: the interior hole must stay untouched"),
                        PROBE_CLEAR(31, 64, outside), PROBE_CLEAR(64, 31, outside), PROBE_COLOR(35, 64, red, cut),
                        PROBE_CLEAR(36, 64, cut));
}

static int check_border_per_side(PixCtx* c) {
    static const char* const left = "border per-side: the left band is exactly w_l wide";
    static const char* const right = "border per-side: the right band is exactly w_r wide";
    static const char* const top = "border per-side: the top band is exactly w_t wide";
    return CHECK_PROBES(&c->buf, PROBE_COLOR(135, 64, green, left), PROBE_CLEAR(136, 64, left),
                        PROBE_COLOR(190, 64, green, right), PROBE_CLEAR(189, 64, right),
                        PROBE_COLOR(160, 35, green, top), PROBE_CLEAR(160, 36, top),
                        PROBE_CLEAR(160, 94, "border per-side: a zero-width side draws nothing"));
}

static int check_border_rounded(PixCtx* c) {
    // B3: the rounded corner band, walked diagonally inward from outside the outer arc into the hole.
    int ramp = check_aa_ramp_line(&c->buf, (AaRampRun) { 225, 33, 1, 1, 6 },
                                  (AaRampSpec) { red, true, "border rounded-corner diagonal" });
    if (ramp != 0) {
        return 1;
    }
    static const char* const mids = "border rounded: straight-edge band midpoints keep full coverage";
    return CHECK_PROBES(
        &c->buf, PROBE_CLEAR(232, 40, "border rounded: the hole must reach across the rounded corner's inner arc"),
        PROBE_CLEAR(224, 32, "border rounded: the corner tip outside the outer arc stays transparent"),
        PROBE_COLOR(226, 64, red, mids), PROBE_COLOR(256, 34, red, mids),
        PROBE_CLEAR(256, 64, "border rounded: the interior hole stays untouched"));
}

static int check_border_width_past_radius(PixCtx* c) {
    // B4: a width past the radius makes the inner corner sharp. A rounded inner corner would leave the
    // pixel diagonally outside it partial rather than fully covered.
    static const char* const straight = "border width>radius: band and hole still split along the straight edges";
    static const char* const consumed
        = "border consuming the rect: the center and edge midpoints fill in the border color";
    return CHECK_PROBES(
        &c->buf,
        PROBE_COLOR(329, 41, red,
                    "border width>radius: the pixel diagonally outside the sharp inner corner is fully covered"),
        PROBE_CLEAR(330, 42, "border width>radius: the pixel diagonally inside the sharp inner corner is hole"),
        PROBE_COLOR(324, 64, red, straight), PROBE_CLEAR(352, 64, straight), PROBE_COLOR(448, 64, red, consumed),
        PROBE_COLOR(448, 33, red, consumed),
        PROBE_CLEAR(416, 32, "border consuming the rect: the rounded corner tip still stays transparent"));
}

static int check_border_half_alpha(PixCtx* c) {
    const f32 a = 128.0f / 255.0f;
    const u8* band = pixel_at(&c->buf, 514, 64);
    CHECK(channels_near(band, (ChannelExpect) { 20.0f * a, 40.0f * a, 200.0f * a, 128.0f }, 3.0f),
          "border: a half-alpha band must land premultiplied");
    CHECK(pixel_transparent(pixel_at(&c->buf, 544, 64)),
          "border: a half-alpha border's hole must stay fully transparent");
    return 0;
}

static int check_border_per_corner(PixCtx* c) {
    // B7: the per-corner derivation, probed where the corners disagree. Each probe sits ~4.95px from its
    // corner's inner arc center along the corner diagonal: past TL's inner radius 4 (band) but within
    // TR's inner radius 6 (hole), so swapping which sides feed which corner cannot pass both. BR's inner
    // radius 10 is probed at 9.19px (hole) and 10.61px (band) from its arc center at (660, 85).
    static const char* const edges = "border per-corner: the straight-edge bands still match their own side widths";
    return CHECK_PROBES(
        &c->buf,
        PROBE_COLOR(616, 38, red,
                    "border per-corner: TL (l=8, t=6 -> inner radius 4) keeps the band 4.95px from its arc center"),
        PROBE_CLEAR(667, 40, "border per-corner: TR (r=2, t=6 -> inner radius 6) is hole 4.95px from its arc center"),
        PROBE_CLEAR(666, 91, "border per-corner: BR (r=2, b=1 -> inner radius 10) hollows far deeper into the corner"),
        PROBE_COLOR(667, 92, red, "border per-corner: the band resumes past BR's inner radius 10"),
        PROBE_COLOR(612, 64, red, edges), PROBE_CLEAR(620, 64, edges), PROBE_COLOR(640, 95, red, edges),
        PROBE_CLEAR(640, 94, edges));
}

static int check_border_fractional(PixCtx* c) {
    // B8: width 4.5 puts the inner edge through pixel-center column 708.5: opaque band, exact half
    // coverage, then the untouched hole.
    CHECK(pixel_color(pixel_at(&c->buf, 707, 64), red, 2.0f),
          "border fractional: the band pixel before the inner edge is fully covered");
    const u8* edge = pixel_at(&c->buf, 708, 64);
    CHECK(channels_near(edge, (ChannelExpect) { 200.0f * 0.5f, 40.0f * 0.5f, 50.0f * 0.5f, 127.5f }, 3.0f),
          "border fractional: the inner-edge pixel carries analytic half coverage, premultiplied");
    CHECK(pixel_transparent(pixel_at(&c->buf, 709, 64)),
          "border fractional: the hole begins right past the fractional inner edge");
    return 0;
}

static int check_border_dashed(PixCtx* c) {
    // B9: each side measures its own run: the top band reads the pattern across x while the left band reads
    // it down y, so a probe that is a dash on one axis and a gap on the other pins which run the pixel was
    // assigned to. Every probe sits >= 3px of arc length clear of a dash boundary, past the 0.5px AA ramp.
    return CHECK_PROBES(
        &c->buf, PROBE_COLOR(804, 34, red, "border dashed: the top band is drawn inside a dash"),
        PROBE_CLEAR(812, 34, "border dashed: the top band is cut away inside a gap"),
        PROBE_COLOR(820, 34, red, "border dashed: the pattern repeats along the run"),
        PROBE_COLOR(802, 36, red, "border dashed: the left band runs the pattern down its own edge"),
        PROBE_CLEAR(802, 44, "border dashed: the left band's gaps fall by distance down the edge, not across it"),
        PROBE_CLEAR(832, 64, "border dashed: the interior hole is still untouched"));
}

int run_border_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    RUN_PIX_SUITE(backend, ir_arena, "border pixel tests", check_border_uniform, check_border_per_side,
                  check_border_rounded, check_border_width_past_radius, check_border_half_alpha,
                  check_border_per_corner, check_border_fractional, check_border_dashed);
}

// Image pixel tests: synthetic decodes go through the backend's prepare_image seam (render
// thread; the GPU backend leaves prepare_image_on_job unset), then FlCmd_Image frames property-check both
// modes off the readback. Atlas placement is backend-private, so every expectation reads only rendered pixels.

enum { PIX_IMG_W = 8, PIX_IMG_H = 8 };

static u8 rgba_texels[PIX_IMG_W * PIX_IMG_H * 4];
static u8 alpha_texels[4 * 4];

static const FlImageSource pix_img_color = { .kind = FlImageKind_Static, .handle = 101 };
static const FlImageSource pix_img_alpha = { .kind = FlImageKind_Static, .handle = 102 };
static const FlImageSource pix_img_ramp = { .kind = FlImageKind_Static, .handle = 103 };

static void fill_pix_color_texels(void) {
    // Color-mode source: 8x8 opaque RGBA, every texel derived from its coordinates. Opaque texels keep
    // premultiplied == straight, so expectations stay exact.
    for (int y = 0; y < PIX_IMG_H; y++) {
        for (int x = 0; x < PIX_IMG_W; x++) {
            u8* px = &rgba_texels[(y * PIX_IMG_W + x) * 4];
            px[0] = (u8)(x * 30);
            px[1] = (u8)(y * 30);
            px[2] = 77;
            px[3] = 255;
        }
    }
}

static void fill_pix_alpha_texels(void) {
    // Alpha-coverage source: 4x4, one alpha step per column (0, 85, 170, 255).
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            alpha_texels[y * 4 + x] = (u8)(x * 85);
        }
    }
}

static int prepare_basic_images(PixCtx* c, const FlImageLoadRequest* req) {
    FlRenderBackend* backend = c->backend;
    fill_pix_color_texels();
    FlImageDecodeResult color_decode = {
        .pixels = rgba_texels,
        .width = PIX_IMG_W,
        .height = PIX_IMG_H,
        .stride = PIX_IMG_W * 4,
        .channels = 4,
        .premultiplied = true,
    };
    CHECK(backend->prepare_image(backend->user_data, 101, &color_decode, c->scratch, req),
          "prepare_image: the backend reports the image realized");

    fill_pix_alpha_texels();
    FlImageDecodeResult alpha_decode = { .pixels = alpha_texels, .width = 4, .height = 4, .stride = 4, .channels = 1 };
    CHECK(backend->prepare_image(backend->user_data, 102, &alpha_decode, c->scratch, req),
          "prepare_image: the backend reports the image realized");

    // Filter probe: 2x1 black|white, scaled 8x so linear and nearest sampling are distinguishable.
    static u8 ramp_px[2 * 4] = { 0, 0, 0, 255, 255, 255, 255, 255 };
    FlImageDecodeResult ramp_decode = { .pixels = ramp_px, .width = 2, .height = 1, .stride = 8, .channels = 4 };
    CHECK(backend->prepare_image(backend->user_data, 103, &ramp_decode, c->scratch, req),
          "prepare_image: the backend reports the image realized");
    return 0;
}

static int prepare_special_images(PixCtx* c, const FlImageLoadRequest* req) {
    FlRenderBackend* backend = c->backend;
    // Strided source: 2x2 RGBA whose rows carry 4 bytes of padding (stride 12 over a tight 8). The upload
    // path must repack rows and never read the padding.
    static u8 strided_px[2 * 12] = {
        10, 20, 30, 255, 40,  50,  60,  255, 0xAA, 0xAA, 0xAA, 0xAA, // row 0 + padding
        70, 80, 90, 255, 110, 120, 130, 255, 0xAA, 0xAA, 0xAA, 0xAA, // row 1 + padding
    };
    FlImageDecodeResult strided_decode = {
        .pixels = strided_px,
        .width = 2,
        .height = 2,
        .stride = 12,
        .channels = 4,
        .premultiplied = true,
    };
    CHECK(backend->prepare_image(backend->user_data, 104, &strided_decode, c->scratch, req),
          "prepare_image: the backend reports the image realized");

    // Oversized source: past the per-image cap the seam routes to a dedicated slot texture instead of the
    // atlas, and the handle keeps rendering. The slot-image section owns the thorough coverage.
    static u8 big_dummy[(GPU_IMAGE_ATLAS_MAX_DIM + 1) * 4 * 4];
    memset(big_dummy, 0x40, sizeof(big_dummy));
    FlImageDecodeResult big_decode = {
        .pixels = big_dummy,
        .width = GPU_IMAGE_ATLAS_MAX_DIM + 1,
        .height = 4,
        .stride = (GPU_IMAGE_ATLAS_MAX_DIM + 1) * 4,
        .channels = 4,
        .premultiplied = true,
    };
    CHECK(backend->prepare_image(backend->user_data, 105, &big_decode, c->scratch, req),
          "prepare_image: the backend reports the image realized");
    return 0;
}

static void emit_pix_image(FlArena* arena, FlImageSource src, FlRect dst, FlImageFilter filter) {
    ir_image(arena, src, FlImageMode_Color, dst, (FlRect) { 0 }, white, filter, nullptr);
}

static void emit_pix_image_scene(FlArena* a) {
    emit_pix_image(a, pix_img_color, (FlRect) { 16, 16, 8, 8 }, FlImageFilter_Nearest); // nearest, native
    emit_pix_image(a, pix_img_color, (FlRect) { 48, 16, 8, 8 }, FlImageFilter_Linear);  // linear, native
    ir_image(a, pix_img_color, FlImageMode_Color, (FlRect) { 80, 16, 8, 8 }, (FlRect) { 0 },
             (FlColor) { 128, 255, 255, 255 }, FlImageFilter_Nearest, nullptr); // tinted
    ir_image(a, pix_img_color, FlImageMode_Color, (FlRect) { 112, 16, 4, 4 }, (FlRect) { 4, 4, 4, 4 }, white,
             FlImageFilter_Nearest, nullptr); // sub-rect
    ir_image(a, pix_img_alpha, FlImageMode_AlphaCoverage, (FlRect) { 16, 48, 4, 4 }, (FlRect) { 0 },
             (FlColor) { 200, 40, 50, 255 }, FlImageFilter_Nearest, nullptr);           // alpha-coverage
    emit_pix_image(a, pix_img_ramp, (FlRect) { 48, 48, 16, 4 }, FlImageFilter_Linear);  // ramp, linear
    emit_pix_image(a, pix_img_ramp, (FlRect) { 48, 64, 16, 4 }, FlImageFilter_Nearest); // ramp, nearest
    emit_pix_image(a, (FlImageSource) { .kind = FlImageKind_Static, .handle = 104 }, (FlRect) { 80, 48, 2, 2 },
                   FlImageFilter_Nearest); // strided source
    emit_pix_image(a, (FlImageSource) { .kind = FlImageKind_Static, .handle = 105 }, (FlRect) { 96, 48, 8, 8 },
                   FlImageFilter_Nearest); // large: slot tier
}

static int prepare_pix_images(PixCtx* c) {
    CHECK(!c->backend->prepare_image_on_job,
          "image: the GPU backend must defer prepare_image to the render thread (thread-confined GL uploads)");
    CHECK(c->backend->prepare_image != nullptr && c->backend->release_image != nullptr,
          "image: the backend must expose the prepare and release seams");

    c->scratch = arena_new();
    FlImageLoadRequest req = { .relative_path = FL_S("synthetic") };
    if (prepare_basic_images(c, &req) != 0) {
        return 1;
    }
    return prepare_special_images(c, &req);
}

static int check_pix_color_passthrough(PixCtx* c) {
    void* start = ir_rewind_begin(c->ir_arena);
    emit_pix_image_scene(c->ir_arena);
    CHECK(pix_render_read(c, start), "read_pixels failed on the image frame");

    // Color mode at native size is texel passthrough for nearest and linear alike (pixel centers map to
    // texel centers, where linear weights collapse to one texel).
    for (int y = 0; y < PIX_IMG_H; y++) {
        for (int x = 0; x < PIX_IMG_W; x++) {
            const u8* nearest = pixel_at(&c->buf, 16 + x, 16 + y);
            const u8* linear = pixel_at(&c->buf, 48 + x, 16 + y);
            CHECK(channels_near(nearest, (ChannelExpect) { (f32)(x * 30), (f32)(y * 30), 77, 255 }, 1.0f),
                  "image color: nearest at native size must pass every texel through exactly");
            CHECK(channels_near(linear, (ChannelExpect) { (f32)(x * 30), (f32)(y * 30), 77, 255 }, 1.0f),
                  "image color: linear at native size samples texel centers — also exact passthrough");
        }
    }
    const u8* before = pixel_at(&c->buf, 15, 20);
    const u8* after = pixel_at(&c->buf, 24, 20);
    CHECK(pixel_transparent(before) && pixel_transparent(after), "image color: nothing renders outside the dst rect");
    return 0;
}

static int check_pix_tint_and_sub_rect(PixCtx* c) {
    // Tint modulation: the red channel halves (128/255), the others pass through.
    CHECK(channels_near(pixel_at(&c->buf, 84, 18), (ChannelExpect) { 120.0f * 128.0f / 255.0f, 60, 77, 255 }, 2.0f),
          "image color: the tint must modulate channels independently");

    // The src sub-rect shows exactly the bottom-right source quadrant.
    const u8* sub_first = pixel_at(&c->buf, 112, 16);
    const u8* sub_last = pixel_at(&c->buf, 115, 19);
    CHECK(channels_near(sub_first, (ChannelExpect) { 4 * 30, 4 * 30, 77, 255 }, 1.0f)
              && channels_near(sub_last, (ChannelExpect) { 7 * 30, 7 * 30, 77, 255 }, 1.0f),
          "image color: a src sub-rect must sample only its region");

    // Alpha-coverage is stored white x alpha, colorized by the tint: column x carries alpha x * 85,
    // premultiplied against the tint.
    for (int x = 0; x < 4; x++) {
        f32 a = (f32)(x * 85);
        const u8* px = pixel_at(&c->buf, 16 + x, 49);
        CHECK(
            channels_near(px, (ChannelExpect) { a * 200.0f / 255.0f, a * 40.0f / 255.0f, a * 50.0f / 255.0f, a }, 3.0f),
            "image alpha-coverage: the tint colorizes stored white x alpha, premultiplied");
    }
    return 0;
}

static int check_pix_filter_ramps(PixCtx* c) {
    u8 prev = 0;
    bool saw_mid = false;
    for (int x = 0; x < 16; x++) {
        const u8* lp = pixel_at(&c->buf, 48 + x, 50);
        CHECK(lp[0] >= prev, "image linear: the scaled ramp must rise monotonically");
        prev = lp[0];
        saw_mid |= lp[0] > 32 && lp[0] < 224;
        const u8* np = pixel_at(&c->buf, 48 + x, 66);
        CHECK(np[0] <= 1 || np[0] >= 254, "image nearest: the scaled ramp must hold only the two texel values");
    }
    CHECK(saw_mid, "image linear: the scaled ramp must pass through intermediate values");
    const u8 step_start = pixel_at(&c->buf, 48, 66)[0];
    const u8 step_end = pixel_at(&c->buf, 63, 66)[0];
    CHECK(step_start <= 1 && step_end >= 254, "image nearest: the step must run black to white across the dst");
    return 0;
}

static int check_pix_strided_and_oversize(PixCtx* c) {
    const u8* t00 = pixel_at(&c->buf, 80, 48);
    const u8* t10 = pixel_at(&c->buf, 81, 48);
    const u8* t01 = pixel_at(&c->buf, 80, 49);
    const u8* t11 = pixel_at(&c->buf, 81, 49);
    CHECK(pixel_color(t00, (FlColor) { 10, 20, 30, 255 }, 1.0f) && pixel_color(t10, (FlColor) { 40, 50, 60, 255 }, 1.0f)
              && pixel_color(t01, (FlColor) { 70, 80, 90, 255 }, 1.0f)
              && pixel_color(t11, (FlColor) { 110, 120, 130, 255 }, 1.0f),
          "image strided: row padding must be repacked away, every texel exact");

    CHECK(pixel_color(pixel_at(&c->buf, 100, 52), (FlColor) { 64, 64, 64, 64 }, 1.0f),
          "image oversize: an image past the per-image cap renders from its slot texture");
    return 0;
}

static int check_pix_release(PixCtx* c) {
    c->backend->release_image(c->backend->user_data, 101);
    void* start = ir_rewind_begin(c->ir_arena);
    emit_pix_image(c->ir_arena, pix_img_color, (FlRect) { 16, 16, 8, 8 }, FlImageFilter_Nearest);
    ir_image(c->ir_arena, pix_img_alpha, FlImageMode_AlphaCoverage, (FlRect) { 16, 48, 4, 4 }, (FlRect) { 0 },
             (FlColor) { 200, 40, 50, 255 }, FlImageFilter_Nearest, nullptr);
    CHECK(pix_render_read(c, start), "read_pixels failed after release_image");
    CHECK(pixel_transparent(pixel_at(&c->buf, 20, 20)), "image release: a released handle must stop rendering");
    const u8* kept = pixel_at(&c->buf, 19, 49);
    CHECK(channels_near(
              kept, (ChannelExpect) { 255.0f * 200.0f / 255.0f, 255.0f * 40.0f / 255.0f, 255.0f * 50.0f / 255.0f, 255 },
              3.0f),
          "image release: other images keep rendering after an unrelated release");
    fl_arena_destroy(c->scratch);
    return 0;
}

int run_image_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    RUN_PIX_SUITE(backend, ir_arena, "image tests", prepare_pix_images, check_pix_color_passthrough,
                  check_pix_tint_and_sub_rect, check_pix_filter_ramps, check_pix_strided_and_oversize,
                  check_pix_release);
}

#endif // !defined(__APPLE__)
