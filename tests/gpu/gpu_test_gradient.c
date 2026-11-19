// GPU smoke test: cross-backend gradient, clip, painter rects and image pixel tests.

#include "gpu_test_common.h"

// Shared gradient, clip and image pixel cases: the remaining core-tier primitives running on both
// backends through the neutral seam: per-corner gradients (bilinear corner blend), clip cuts (the clip-rect SDF
// cut), and the atlas sampler path (RGBA color, alpha-coverage, linear/nearest filtering, src sub-rects, and a
// clipped image). Every expectation is a property both backends satisfy within the same channels_near
// tolerance, with no per-backend golden fork. The slot / oversize image tier stays GL-only (run_image_tests / the
// slot suites); it is not part of the atlas sampler path.

typedef struct GfxCtx {
    FlRenderBackend* backend;
    FlSurface* surface;
    FlArena* ir_arena;
    FlRenderFrame frame;
    FlPixelReadback buf;
} GfxCtx;

static GfxCtx gfx_create(FlRenderBackend* backend, FlArena* ir_arena) {
    return (GfxCtx) {
        .backend = backend,
        .surface = backend->create_surface(backend->user_data, 256, 256),
        .ir_arena = ir_arena,
        .frame = test_frame(),
    };
}

static bool gfx_render_read(GfxCtx* c, const void* start) {
    ir_frame_render(c->backend, c->surface, &c->frame, c->ir_arena, start);
    return render_seam_read_pixels(c->backend, c->surface, &c->buf);
}

static void fill_corners(FlArena* arena, FlRect rect, const FlColor* corners) {
    ir_fill_rect(arena, rect, (FlCornerRadius) { 0 }, corners, nullptr);
}

static void fill_solid(FlArena* arena, FlRect rect, FlCornerRadius radius, FlColor c) {
    ir_fill_rect(arena, rect, radius, (FlColor[4]) { c, c, c, c }, nullptr);
}

static void emit_gradient_scene(FlArena* ir_arena) {
    const FlColor pure_red = { 255, 0, 0, 255 };
    const FlColor pure_green = { 0, 255, 0, 255 };
    const FlColor pure_blue = { 0, 0, 255, 255 };
    const FlColor black = { 0, 0, 0, 255 };
    const FlColor half_red = { 200, 40, 50, 128 };
    const FlColor half_blue = { 20, 40, 200, 128 };

    // G1: horizontal gradient black -> red (TL/BL black, TR/BR red).
    fill_corners(ir_arena, (FlRect) { 0, 0, 128, 16 }, (FlColor[4]) { black, pure_red, black, pure_red });
    // G2: vertical gradient green -> blue (TL/TR green, BL/BR blue).
    fill_corners(ir_arena, (FlRect) { 160, 0, 16, 128 }, (FlColor[4]) { pure_green, pure_green, pure_blue, pure_blue });
    // G3: four distinct corners (TL red, TR green, BL blue, BR white), the corner-order check.
    fill_corners(ir_arena, (FlRect) { 0, 160, 64, 64 }, (FlColor[4]) { pure_red, pure_green, pure_blue, white });
    // G4: half-alpha gradient over the transparent target (premultiplied-output check).
    fill_corners(ir_arena, (FlRect) { 80, 160, 64, 32 }, (FlColor[4]) { half_red, half_blue, half_red, half_blue });
}

static int check_gradient_horizontal(const FlPixelReadback* buf) {
    // G1: the horizontal axis.
    u8 prev_r = 0;
    for (int x = 0; x < 128; x++) {
        const u8* gpx = pixel_at(buf, x, 8);
        CHECK(gpx[0] >= prev_r, "gradient: red must be monotonic along the horizontal axis");
        CHECK(gpx[1] == 0 && gpx[2] == 0, "gradient: black->red must not invent green or blue");
        CHECK(gpx[3] == 255, "gradient: an opaque gradient stays fully opaque");
        prev_r = gpx[0];
    }
    const u8 left_end = pixel_at(buf, 0, 8)[0];
    const u8 right_end = pixel_at(buf, 127, 8)[0];
    const u8 midpoint = pixel_at(buf, 64, 8)[0];
    CHECK(left_end < 8, "gradient: the horizontal left end is near black");
    CHECK(right_end > 247, "gradient: the horizontal right end is near pure red");
    CHECK(midpoint > 120 && midpoint < 136, "gradient: the horizontal midpoint sits near half intensity");
    return 0;
}

static int check_gradient_vertical(const FlPixelReadback* buf) {
    // G2: the same property on the vertical axis (catches a u/v swap).
    u8 prev_b = 0;
    for (int y = 0; y < 128; y++) {
        const u8* gpx = pixel_at(buf, 168, y);
        CHECK(gpx[2] >= prev_b, "gradient: blue must be monotonic along the vertical axis");
        CHECK((int)gpx[1] + (int)gpx[2] > 240, "gradient: green->blue must conserve intensity");
        prev_b = gpx[2];
    }
    const u8 top_green = pixel_at(buf, 168, 0)[1];
    const u8 bottom_blue = pixel_at(buf, 168, 127)[2];
    CHECK(top_green > 247, "gradient: the vertical top end is near pure green");
    CHECK(bottom_blue > 247, "gradient: the vertical bottom end is near pure blue");
    return 0;
}

static int check_gradient_corners(const FlPixelReadback* buf) {
    // G3: each corner region is dominated by its own corner color (catches TL/TR/BL/BR mixups in either the
    // translator packing or the shader's quadrant mapping).
    const u8* tl = pixel_at(buf, 2, 162);
    CHECK(tl[0] > 200 && tl[1] < 40 && tl[2] < 40, "gradient corners: TL is red-dominated");
    const u8* tr = pixel_at(buf, 61, 162);
    CHECK(tr[1] > 200 && tr[0] < 40 && tr[2] < 40, "gradient corners: TR is green-dominated");
    return 0;
}

static int check_gradient_bottom_corners(const FlPixelReadback* buf) {
    const u8* bl = pixel_at(buf, 2, 221);
    CHECK(bl[2] > 200 && bl[0] < 40 && bl[1] < 40, "gradient corners: BL is blue-dominated");
    const u8* br = pixel_at(buf, 61, 221);
    CHECK(br[0] > 200 && br[1] > 200 && br[2] > 200, "gradient corners: BR is white-dominated");
    return 0;
}

static int check_gradient_alpha(const FlPixelReadback* buf) {
    // G4: at pixel center x = 112.5, u = 32.5/64, the interpolated color is (200 - 180u, 40, 50 + 150u) at
    // alpha 128.
    const f32 u = 32.5f / 64.0f;
    const f32 a = 128.0f / 255.0f;
    CHECK(channels_near(pixel_at(buf, 112, 176),
                        (ChannelExpect) { (200.0f - 180.0f * u) * a, 40.0f * a, (50.0f + 150.0f * u) * a, 128.0f },
                        3.0f),
          "gradient: a half-alpha gradient lands premultiplied with the interpolated alpha");
    return 0;
}

int run_gradient_pixel_test(FlRenderBackend* backend, FlArena* ir_arena) {
    GfxCtx c = gfx_create(backend, ir_arena);

    void* start = ir_rewind_begin(ir_arena);
    emit_gradient_scene(ir_arena);
    CHECK(gfx_render_read(&c, start), "gradient: read_pixels failed");

    if (check_gradient_horizontal(&c.buf) != 0) {
        return 1;
    }
    if (check_gradient_vertical(&c.buf) != 0) {
        return 1;
    }
    if (check_gradient_corners(&c.buf) != 0) {
        return 1;
    }
    if (check_gradient_bottom_corners(&c.buf) != 0) {
        return 1;
    }
    if (check_gradient_alpha(&c.buf) != 0) {
        return 1;
    }

    backend->destroy_surface(backend->user_data, c.surface);
    printf("gpu_smoke_test: gradient (per-corner) PASS\n");
    return 0;
}

static void emit_clip_scene(FlArena* ir_arena, FlColor clip_red, FlColor clip_green) {
    // K1: a 64x64 red rect clipped to {16,16,32,32}.
    ir_clip_push(ir_arena, (FlRect) { 16, 16, 32, 32 });
    fill_solid(ir_arena, (FlRect) { 0, 0, 64, 64 }, (FlCornerRadius) { 0 }, clip_red);
    ir_clip_pop(ir_arena);
    // K2: a green rect whose clip starts at the fractional x = 100.5, so pixel column 100 is half covered.
    ir_clip_push(ir_arena, (FlRect) { 100.5f, 0.0f, 32.0f, 32.0f });
    fill_solid(ir_arena, (FlRect) { 96, 0, 64, 32 }, (FlCornerRadius) { 0 }, clip_green);
    ir_clip_pop(ir_arena);
    // K3: a rounded rect (TL radius 16, arc circle centered at (208,48)) clipped to its left 8 pixels
    // ({192..200} x {32..96}); the clip edge at x = 200 slices through the corner arc.
    ir_clip_push(ir_arena, (FlRect) { 192, 32, 8, 64 });
    fill_solid(ir_arena, (FlRect) { 192, 32, 64, 64 }, (FlCornerRadius) { .tl = 16.0f }, clip_red);
    ir_clip_pop(ir_arena);
}

static int check_integer_clip(const FlPixelReadback* buf, FlColor clip_red) {
    const u8* first = pixel_at(buf, 16, 16);
    const u8* last = pixel_at(buf, 47, 47);
    CHECK(pixel_color(first, clip_red, 2.0f) && pixel_color(last, clip_red, 2.0f),
          "clip: pixels inside an integer clip keep full coverage");
    const u8* left = pixel_at(buf, 15, 30);
    const u8* right = pixel_at(buf, 48, 30);
    const u8* above = pixel_at(buf, 30, 15);
    const u8* below = pixel_at(buf, 30, 48);
    CHECK(pixel_transparent(left) && pixel_transparent(right) && pixel_transparent(above) && pixel_transparent(below),
          "clip: the rect must not bleed past any integer clip edge");
    return 0;
}

static int check_fractional_clip(const FlPixelReadback* buf, FlColor clip_green) {
    // K2: the fractional clip edge at x = 100.5 half-covers pixel 100 (center at 100.5), like a fractional rect edge.
    CHECK(pixel_transparent(pixel_at(buf, 99, 10)), "clip: pixels left of a fractional clip edge stay transparent");
    CHECK(channels_near(pixel_at(buf, 100, 10), (ChannelExpect) { 40.0f * 0.5f, 200.0f * 0.5f, 60.0f * 0.5f, 127.5f },
                        3.0f),
          "clip: a fractionally clipped pixel carries analytic half coverage");
    CHECK(pixel_color(pixel_at(buf, 101, 10), clip_green, 2.0f),
          "clip: pixels right of a fractional clip edge keep full coverage");
    return 0;
}

static int check_rounded_clip(const FlPixelReadback* buf, FlColor clip_red) {
    CHECK(pixel_color(pixel_at(buf, 199, 44), clip_red, 2.0f),
          "clip on a rounded corner: inside both the arc and the clip is the solid fill");
    const u8* at_edge = pixel_at(buf, 200, 44);
    const u8* past_edge = pixel_at(buf, 201, 80);
    CHECK(pixel_transparent(at_edge) && pixel_transparent(past_edge),
          "clip on a rounded corner: nothing lands past the clip edge, arc or not");
    CHECK(pixel_transparent(pixel_at(buf, 193, 33)),
          "clip on a rounded corner: outside the arc stays transparent inside the clip");
    CHECK(pixel_color(pixel_at(buf, 193, 90), clip_red, 2.0f),
          "clip on a rounded corner: the straight edge below the corner still fills inside the clip");
    return 0;
}

int run_clip_pixel_test(FlRenderBackend* backend, FlArena* ir_arena) {
    GfxCtx c = gfx_create(backend, ir_arena);
    const FlColor clip_red = { 200, 40, 50, 255 };
    const FlColor clip_green = { 40, 200, 60, 255 };

    void* start = ir_rewind_begin(ir_arena);
    emit_clip_scene(ir_arena, clip_red, clip_green);
    CHECK(gfx_render_read(&c, start), "clip: read_pixels failed");

    if (check_integer_clip(&c.buf, clip_red) != 0) {
        return 1;
    }
    if (check_fractional_clip(&c.buf, clip_green) != 0) {
        return 1;
    }
    if (check_rounded_clip(&c.buf, clip_red) != 0) {
        return 1;
    }

    backend->destroy_surface(backend->user_data, c.surface);
    printf("gpu_smoke_test: clip (integer/fractional/rounded) PASS\n");
    return 0;
}

// Painter::rects renders exactly what the Painter::rect loop renders on the GPU backend: overlapping translucent
// columns (src-over order), a fractional UI scale, a scissor that cuts one canvas, and a later canvas that must
// land over the first (Canvas z-order). The frame goes through the real splice (fl_clay_to_render_ir) once per
// recording, and the two readbacks are compared byte for byte.

#define RECTS_TEST_COUNT 48

typedef struct RectColumns {
    FlColoredRect a[RECTS_TEST_COUNT];
    FlColoredRect b[RECTS_TEST_COUNT];
} RectColumns;

typedef struct RectsCapture {
    u8* readback[2];
    size_t bytes;
} RectsCapture;

typedef struct RectList {
    FlColoredRect* rects;
    u32 count;
} RectList;

static void rects_test_record(Painter* p, bool batch, RectList list, void** slot) {
    FlColoredRect* rects = list.rects;
    painter_scope_push(p, slot);
    if (batch) {
        painter_rects(p, rects, list.count);
    } else {
        for (u32 i = 0; i < list.count; i++) {
            painter_rect(p, (FlRect) { .x = rects[i].x, .y = rects[i].y, .w = rects[i].w, .h = rects[i].h },
                         rects[i].color);
        }
    }
    painter_scope_pop(p);
}

static void fill_rect_columns(RectColumns* cols) {
    for (u32 i = 0; i < RECTS_TEST_COUNT; i++) {
        f32 f = (f32)i;
        cols->a[i] = (FlColoredRect) { 2.0f + f * 0.9f,
                                       4.0f + (f32)(i % 5),
                                       2.5f,
                                       30.0f - (f32)(i % 7) * 2.0f,
                                       { (u8)(30 + i * 4), (u8)(220 - i * 3), 80, (u8)(90 + (i % 6) * 30) } };
        cols->b[i] = (FlColoredRect) { 6.0f + f * 0.9f,
                                       10.0f + (f32)(i % 4),
                                       2.5f,
                                       28.0f,
                                       { 220, (u8)(40 + i * 2), (u8)(60 + i), (u8)(100 + (i % 5) * 35) } };
    }
}

static void emit_rects_frame(GfxCtx* c, Painter* p, bool batch, RectColumns* cols) {
    void* slot_a = nullptr;
    void* slot_b = nullptr;
    rects_test_record(p, batch, (RectList) { cols->a, RECTS_TEST_COUNT }, &slot_a);
    rects_test_record(p, batch, (RectList) { cols->b, RECTS_TEST_COUNT }, &slot_b);

    // Canvas A under a scissor of device pixels x 20..60, y 10..40, which cuts its columns (they run to about
    // x = 90, y = 67); canvas B declared later at a fractional origin, so it composes over A where they
    // overlap.
    Clay_RenderCommand cmds[4] = {
        { .commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_START, .boundingBox = { 20.0f, 10.0f, 40.0f, 30.0f } },
        { .commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM,
          .boundingBox = { 20.0f, 10.0f, 150.0f, 100.0f },
          .renderData = { .custom = { .customData = slot_a } } },
        { .commandType = CLAY_RENDER_COMMAND_TYPE_SCISSOR_END },
        { .commandType = CLAY_RENDER_COMMAND_TYPE_CUSTOM,
          .boundingBox = { 60.25f, 30.5f, 150.0f, 100.0f },
          .renderData = { .custom = { .customData = slot_b } } },
    };

    void* start = ir_rewind_begin(c->ir_arena);
    fl_clay_to_render_ir(c->ir_arena, nullptr, cmds, 4);
    ir_frame_render(c->backend, c->surface, &c->frame, c->ir_arena, start);
}

static int capture_rects_frames(GfxCtx* c, Painter* p, RectColumns* cols, RectsCapture* capture) {
    for (int batch = 0; batch < 2; batch++) {
        emit_rects_frame(c, p, batch != 0, cols);

        CHECK(render_seam_read_pixels(c->backend, c->surface, &c->buf), "painter rects: read_pixels failed");
        capture->bytes = (size_t)c->buf.stride * (size_t)c->buf.height;
        capture->readback[batch] = (u8*)malloc(capture->bytes);
        CHECK(capture->readback[batch] != nullptr, "painter rects: readback copy allocation failed");
        memcpy(capture->readback[batch], c->buf.pixels, capture->bytes);
    }
    return 0;
}

static int check_rects_capture(const RectsCapture* capture, int height) {
    int drawn = 0;
    for (size_t i = 0; i + 3 < capture->bytes; i += 4) {
        drawn += capture->readback[0][i + 3] != 0;
    }
    CHECK(drawn > 500, "painter rects: the scalar reference frame must actually draw");
    // The scissor clips: (65, 20) is inside canvas A's unclipped columns but outside the scissor and clear of
    // canvas B (which starts at y = 45); (40, 20) is inside the scissor.
    const size_t row_stride = capture->bytes / (size_t)height;
    CHECK(capture->readback[0][20 * row_stride + 65 * 4 + 3] == 0,
          "painter rects: the scissor clips the scalar columns");
    CHECK(capture->readback[0][20 * row_stride + 40 * 4 + 3] != 0, "painter rects: columns draw inside the scissor");
    CHECK(memcmp(capture->readback[0], capture->readback[1], capture->bytes) == 0,
          "painter rects: the batched frame must match the scalar rect loop pixel for pixel");
    return 0;
}

int run_painter_rects_pixel_test(FlRenderBackend* backend, FlArena* ir_arena) {
    GfxCtx c = gfx_create(backend, ir_arena);

    RectColumns cols;
    fill_rect_columns(&cols);

    FlArena* scratch = arena_new();
    Painter* p = painter_create(scratch);
    painter_begin_frame(
        p, (PainterFrame) { .font_cache = nullptr, .ui_scale = 1.5f, .text_scale = 1.0f, .state_arena = scratch });

    RectsCapture capture = { 0 };
    if (capture_rects_frames(&c, p, &cols, &capture) != 0) {
        return 1;
    }
    if (check_rects_capture(&capture, 256) != 0) {
        return 1;
    }

    free(capture.readback[0]);
    free(capture.readback[1]);
    fl_arena_destroy(scratch);
    backend->destroy_surface(backend->user_data, c.surface);
    printf("gpu_smoke_test: painter rects batch vs scalar loop PASS\n");
    return 0;
}

enum { IMG_W = 8, IMG_H = 8 };

static const FlImageSource img_color = { .kind = FlImageKind_Static, .handle = 301 };
static const FlImageSource img_alpha = { .kind = FlImageKind_Static, .handle = 302 };
static const FlImageSource img_ramp = { .kind = FlImageKind_Static, .handle = 303 };
static const FlImageSource img_padded = { .kind = FlImageKind_Static, .handle = 304 };
static const FlImageSource img_tight = { .kind = FlImageKind_Static, .handle = 305 };

static void emit_native(FlArena* arena, FlImageSource src, FlRect dst, FlImageFilter filter) {
    ir_image(arena, src, FlImageMode_Color, dst, (FlRect) { 0 }, white, filter, nullptr);
}

static u8 rgba[IMG_W * IMG_H * 4];
static u8 alpha_px[4 * 4];

static void fill_color_texels(void) {
    // Color source: 8x8 opaque RGBA, each texel derived from its coordinates (opaque texels keep premultiplied ==
    // straight, so expectations stay exact). Distinct handles from the GL-only run_image_tests (101..) so both
    // suites can share the backend's atlas on the Linux build.
    for (int y = 0; y < IMG_H; y++) {
        for (int x = 0; x < IMG_W; x++) {
            u8* px = &rgba[(y * IMG_W + x) * 4];
            px[0] = (u8)(x * 30);
            px[1] = (u8)(y * 30);
            px[2] = 77;
            px[3] = 255;
        }
    }
}

static void fill_alpha_texels(void) {
    // Alpha-coverage source: 4x4, one alpha step per column (0, 85, 170, 255), the white x alpha atlas form.
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            alpha_px[y * 4 + x] = (u8)(x * 85);
        }
    }
}

static int prepare_color_sources(FlRenderBackend* backend, FlArena* scratch, const FlImageLoadRequest* req) {
    fill_color_texels();
    FlImageDecodeResult color_decode = {
        .pixels = rgba,
        .width = IMG_W,
        .height = IMG_H,
        .stride = IMG_W * 4,
        .channels = 4,
        .premultiplied = true,
    };
    CHECK(backend->prepare_image(backend->user_data, 301, &color_decode, scratch, req),
          "prepare_image: the backend reports the image realized");

    fill_alpha_texels();
    FlImageDecodeResult alpha_decode = { .pixels = alpha_px, .width = 4, .height = 4, .stride = 4, .channels = 1 };
    CHECK(backend->prepare_image(backend->user_data, 302, &alpha_decode, scratch, req),
          "prepare_image: the backend reports the image realized");

    // Filter probe: 2x1 black|white scaled 8x so linear and nearest sampling are distinguishable.
    static u8 ramp_px[2 * 4] = { 0, 0, 0, 255, 255, 255, 255, 255 };
    FlImageDecodeResult ramp_decode = { .pixels = ramp_px, .width = 2, .height = 1, .stride = 8, .channels = 4 };
    CHECK(backend->prepare_image(backend->user_data, 303, &ramp_decode, scratch, req),
          "prepare_image: the backend reports the image realized");
    return 0;
}

static int prepare_stride_sources(FlRenderBackend* backend, FlArena* scratch, const FlImageLoadRequest* req) {
    // The two row-pitch forms the atlas-cell upload must handle, checked on whichever backend runs this case:
    //  - a padded source (stride 12 over a tight 8, 0xAA sentinel padding); the pitch rides the upload, so none
    //    of the padding may leak into the cell;
    //  - a source declaring no stride at all (0 = tightly packed), where the upload derives w * bpp itself.
    // Same four texels either way, so both must land identically.
    static u8 padded_px[2 * 12] = {
        255, 0, 0,   255, 0,   255, 0, 255, 0xAA, 0xAA, 0xAA, 0xAA, // red, green, then row padding
        0,   0, 255, 255, 255, 255, 0, 255, 0xAA, 0xAA, 0xAA, 0xAA, // blue, yellow, then row padding
    };
    FlImageDecodeResult padded_decode
        = { .pixels = padded_px, .width = 2, .height = 2, .stride = 12, .channels = 4, .premultiplied = true };
    CHECK(backend->prepare_image(backend->user_data, 304, &padded_decode, scratch, req),
          "prepare_image: the backend reports the image realized");

    static u8 tight_px[2 * 2 * 4] = {
        255, 0, 0,   255, 0,   255, 0, 255, //
        0,   0, 255, 255, 255, 255, 0, 255,
    };
    FlImageDecodeResult tight_decode
        = { .pixels = tight_px, .width = 2, .height = 2, .stride = 0, .channels = 4, .premultiplied = true };
    CHECK(backend->prepare_image(backend->user_data, 305, &tight_decode, scratch, req),
          "prepare_image: the backend reports the image realized");
    return 0;
}

static void emit_image_scene(FlArena* ir_arena) {
    emit_native(ir_arena, img_color, (FlRect) { 16, 16, 8, 8 }, FlImageFilter_Nearest);
    emit_native(ir_arena, img_color, (FlRect) { 48, 16, 8, 8 }, FlImageFilter_Linear);
    ir_image(ir_arena, img_color, FlImageMode_Color, (FlRect) { 80, 16, 8, 8 }, (FlRect) { 0 },
             (FlColor) { 128, 255, 255, 255 }, FlImageFilter_Nearest, nullptr);
    ir_image(ir_arena, img_color, FlImageMode_Color, (FlRect) { 112, 16, 4, 4 }, (FlRect) { 4, 4, 4, 4 }, white,
             FlImageFilter_Nearest, nullptr); // src sub-rect (bottom-right quadrant)
    ir_image(ir_arena, img_alpha, FlImageMode_AlphaCoverage, (FlRect) { 16, 48, 4, 4 }, (FlRect) { 0 },
             (FlColor) { 200, 40, 50, 255 }, FlImageFilter_Nearest, nullptr);
    emit_native(ir_arena, img_ramp, (FlRect) { 48, 48, 16, 4 }, FlImageFilter_Linear);
    emit_native(ir_arena, img_ramp, (FlRect) { 48, 64, 16, 4 }, FlImageFilter_Nearest);
    ir_clip_push(ir_arena, (FlRect) { 96, 48, 4, 8 });
    emit_native(ir_arena, img_color, (FlRect) { 96, 48, 8, 8 }, FlImageFilter_Nearest);
    ir_clip_pop(ir_arena);
    // The two row-pitch forms, at native size so every texel is a checkable passthrough.
    emit_native(ir_arena, img_padded, (FlRect) { 16, 80, 2, 2 }, FlImageFilter_Nearest);
    emit_native(ir_arena, img_tight, (FlRect) { 32, 80, 2, 2 }, FlImageFilter_Nearest);
}

static int check_color_passthrough(const FlPixelReadback* buf) {
    // Color mode at native size is texel passthrough for nearest and linear alike (pixel centers map to texel
    // centers, where the linear weights collapse to one texel).
    for (int y = 0; y < IMG_H; y++) {
        for (int x = 0; x < IMG_W; x++) {
            const u8* nearest = pixel_at(buf, 16 + x, 16 + y);
            const u8* linear = pixel_at(buf, 48 + x, 16 + y);
            CHECK(channels_near(nearest, (ChannelExpect) { (f32)(x * 30), (f32)(y * 30), 77, 255 }, 1.0f),
                  "image color: nearest at native size passes every texel through exactly");
            CHECK(channels_near(linear, (ChannelExpect) { (f32)(x * 30), (f32)(y * 30), 77, 255 }, 1.0f),
                  "image color: linear at native size samples texel centers — also exact passthrough");
        }
    }
    const u8* before = pixel_at(buf, 15, 20);
    const u8* after = pixel_at(buf, 24, 20);
    CHECK(pixel_transparent(before) && pixel_transparent(after), "image color: nothing renders outside the dst rect");
    return 0;
}

static int check_tint_and_sub_rect(const FlPixelReadback* buf) {
    // Tint modulation: the red channel halves (128/255), the others pass through.
    CHECK(channels_near(pixel_at(buf, 84, 18), (ChannelExpect) { 120.0f * 128.0f / 255.0f, 60, 77, 255 }, 2.0f),
          "image color: the tint modulates channels independently");

    const u8* sub_first = pixel_at(buf, 112, 16);
    const u8* sub_last = pixel_at(buf, 115, 19);
    CHECK(channels_near(sub_first, (ChannelExpect) { 4 * 30, 4 * 30, 77, 255 }, 1.0f)
              && channels_near(sub_last, (ChannelExpect) { 7 * 30, 7 * 30, 77, 255 }, 1.0f),
          "image color: a src sub-rect samples only its region");

    // Alpha-coverage is stored white x alpha, colorized by the tint: column x carries alpha x*85, premultiplied.
    for (int x = 0; x < 4; x++) {
        f32 a = (f32)(x * 85);
        const u8* px = pixel_at(buf, 16 + x, 49);
        CHECK(
            channels_near(px, (ChannelExpect) { a * 200.0f / 255.0f, a * 40.0f / 255.0f, a * 50.0f / 255.0f, a }, 3.0f),
            "image alpha-coverage: the tint colorizes stored white x alpha, premultiplied");
    }
    return 0;
}

static int check_filter_ramps(const FlPixelReadback* buf) {
    u8 prev = 0;
    bool saw_mid = false;
    for (int x = 0; x < 16; x++) {
        const u8* lp = pixel_at(buf, 48 + x, 50);
        CHECK(lp[0] >= prev, "image linear: the scaled ramp rises monotonically");
        prev = lp[0];
        saw_mid |= lp[0] > 32 && lp[0] < 224;
        const u8* np = pixel_at(buf, 48 + x, 66);
        CHECK(np[0] <= 1 || np[0] >= 254, "image nearest: the scaled ramp holds only the two texel values");
    }
    CHECK(saw_mid, "image linear: the scaled ramp passes through intermediate values");
    const u8 step_start = pixel_at(buf, 48, 66)[0];
    const u8 step_end = pixel_at(buf, 63, 66)[0];
    CHECK(step_start <= 1 && step_end >= 254, "image nearest: the step runs black to white across the dst");
    return 0;
}

static int check_clipped_image(const FlPixelReadback* buf) {
    // Clipped image: the left half {96..99} shows the image, the right half {100..103} is cut away.
    CHECK(channels_near(pixel_at(buf, 98, 50), (ChannelExpect) { 2 * 30, 2 * 30, 77, 255 }, 1.0f),
          "clipped image: inside the clip the image renders");
    CHECK(pixel_transparent(pixel_at(buf, 101, 50)),
          "clipped image: the clip cuts the image past its edge, like it cuts a fill");
    return 0;
}

static int check_stride_forms(const FlPixelReadback* buf) {
    for (int i = 0; i < 2; i++) {
        int ox = 16 + i * 16; // the padded source, then the tight one
        const char* what = i == 0 ? "image padded stride: row padding rides the upload without leaking"
                                  : "image zero stride: a stride-less source uploads as tightly packed";
        const u8* red_px = pixel_at(buf, ox + 0, 80);
        const u8* green_px = pixel_at(buf, ox + 1, 80);
        const u8* blue_px = pixel_at(buf, ox + 0, 81);
        const u8* yellow_px = pixel_at(buf, ox + 1, 81);
        CHECK(channels_near(red_px, (ChannelExpect) { 255, 0, 0, 255 }, 1.0f), what);
        CHECK(channels_near(green_px, (ChannelExpect) { 0, 255, 0, 255 }, 1.0f), what);
        CHECK(channels_near(blue_px, (ChannelExpect) { 0, 0, 255, 255 }, 1.0f), what);
        CHECK(channels_near(yellow_px, (ChannelExpect) { 255, 255, 0, 255 }, 1.0f), what);
    }
    return 0;
}

static int check_image_release(GfxCtx* c) {
    FlRenderBackend* backend = c->backend;
    backend->release_image(backend->user_data, 301);
    void* start = ir_rewind_begin(c->ir_arena);
    emit_native(c->ir_arena, img_color, (FlRect) { 16, 16, 8, 8 }, FlImageFilter_Nearest);
    emit_native(c->ir_arena, img_ramp, (FlRect) { 48, 48, 16, 4 }, FlImageFilter_Linear);
    CHECK(gfx_render_read(c, start), "image: read_pixels after release failed");
    CHECK(pixel_transparent(pixel_at(&c->buf, 20, 20)), "image release: the released handle stops rendering");
    const u8 ramp_start = pixel_at(&c->buf, 48, 50)[0];
    const u8 ramp_end = pixel_at(&c->buf, 63, 50)[0];
    CHECK(ramp_start < 8 && ramp_end > 247,
          "image release: an untouched handle keeps rendering after another is released");

    // Release the rest so this case leaves no residue in the backend-wide atlas (the Linux build shares it with the
    // GL-only run_image_tests, which uses disjoint handles but a common atlas).
    backend->release_image(backend->user_data, 302);
    backend->release_image(backend->user_data, 303);
    backend->release_image(backend->user_data, 304);
    backend->release_image(backend->user_data, 305);
    return 0;
}

static int check_image_frame(GfxCtx* c) {
    void* start = ir_rewind_begin(c->ir_arena);
    emit_image_scene(c->ir_arena);
    CHECK(gfx_render_read(c, start), "image: read_pixels failed");

    if (check_color_passthrough(&c->buf) != 0) {
        return 1;
    }
    if (check_tint_and_sub_rect(&c->buf) != 0) {
        return 1;
    }
    if (check_filter_ramps(&c->buf) != 0) {
        return 1;
    }
    if (check_clipped_image(&c->buf) != 0) {
        return 1;
    }
    return check_stride_forms(&c->buf);
}

int run_image_pixel_test(FlRenderBackend* backend, FlArena* ir_arena) {
    GfxCtx c = gfx_create(backend, ir_arena);
    CHECK(!backend->prepare_image_on_job,
          "image: the GPU backend defers prepare_image to the render thread (thread-confined uploads)");
    CHECK(backend->prepare_image != nullptr && backend->release_image != nullptr,
          "image: the backend exposes the prepare and release seams");

    FlArena* scratch = arena_new();
    FlImageLoadRequest req = { .relative_path = FL_S("synthetic") };
    if (prepare_color_sources(backend, scratch, &req) != 0) {
        return 1;
    }
    if (prepare_stride_sources(backend, scratch, &req) != 0) {
        return 1;
    }
    if (check_image_frame(&c) != 0) {
        return 1;
    }
    if (check_image_release(&c) != 0) {
        return 1;
    }

    fl_arena_destroy(scratch);
    backend->destroy_surface(backend->user_data, c.surface);
    printf("gpu_smoke_test: image (atlas sampler + clip) PASS\n");
    return 0;
}
