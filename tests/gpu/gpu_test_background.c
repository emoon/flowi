// GPU smoke test: background and effect pixel tests.

#include "gpu_test_common.h"

#if !defined(__APPLE__)

// Background pixel tests: the frame envelope's background composited end-to-end. Two solid
// fullscreen-tier sources (300 wide, past the atlas cap, so they ride slot textures like real backgrounds)
// make every expectation a closed-form composite the readback can check.

enum { BG_W = 300, BG_H = 8 };

typedef struct BackgroundCtx {
    FlRenderBackend* backend;
    FlSurface* surface;
    FlArena* ir_arena;
    FlRenderFrame frame;
} BackgroundCtx;

static const FlColor k_background_clear = { 20, 30, 40, 255 };

static FlImageDecodeResult solid_decode(u8* px, FlColor c) {
    for (int i = 0; i < BG_W * BG_H; i++) {
        px[i * 4 + 0] = c.r;
        px[i * 4 + 1] = c.g;
        px[i * 4 + 2] = c.b;
        px[i * 4 + 3] = c.a;
    }
    return (FlImageDecodeResult) {
        .pixels = px,
        .width = BG_W,
        .height = BG_H,
        .stride = BG_W * 4,
        .channels = 4,
        .premultiplied = true,
    };
}

static bool render_background(BackgroundCtx* c, const void* start, FlBackground background, FlPixelReadback* buf) {
    c->frame.background = background;
    ir_frame_render(c->backend, c->surface, &c->frame, c->ir_arena, start);
    return render_seam_read_pixels(c->backend, c->surface, buf);
}

static int prepare_background_images(FlRenderBackend* backend, FlArena* scratch) {
    FlImageLoadRequest req = { .relative_path = FL_S("synthetic-background") };
    static u8 prev_px[BG_W * BG_H * 4];
    static u8 curr_px[BG_W * BG_H * 4];
    FlImageDecodeResult prev_decode = solid_decode(prev_px, (FlColor) { 200, 40, 50, 255 });
    FlImageDecodeResult curr_decode = solid_decode(curr_px, (FlColor) { 40, 50, 200, 255 });
    // prev = solid red, current = solid blue
    CHECK(backend->prepare_image(backend->user_data, 301, &prev_decode, scratch, &req),
          "prepare_image: the backend reports the image realized");
    CHECK(backend->prepare_image(backend->user_data, 302, &curr_decode, scratch, &req),
          "prepare_image: the backend reports the image realized");
    return 0;
}

static int check_background_clear_and_single(BackgroundCtx* c) {
    FlPixelReadback buf;
    const FlBackground clear_only
        = { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID, .clear = k_background_clear };

    void* start = ir_rewind_begin(c->ir_arena);
    CHECK(render_background(c, start, clear_only, &buf), "read_pixels failed on the clear-only frame");
    CHECK(pixel_color(pixel_at(&buf, 960, 540), (FlColor) { 20, 30, 40, 255 }, 2.0f),
          "background: the clear color fills the target");
    CHECK(pixel_color(pixel_at(&buf, 8, 8), (FlColor) { 20, 30, 40, 255 }, 2.0f),
          "background: the clear reaches the corners");

    // A single background image composites with its alpha over the clear: lerp(clear, image, alpha/255).
    start = ir_rewind_begin(c->ir_arena);
    const FlBackground single
        = { .current = 302, .prev = FL_IMAGE_HANDLE_INVALID, .alpha = 128, .clear = k_background_clear };
    CHECK(render_background(c, start, single, &buf), "read_pixels failed on the single-image frame");
    f32 a = 128.0f / 255.0f;
    CHECK(channels_near(pixel_at(&buf, 960, 540),
                        (ChannelExpect) { 20 * (1 - a) + 40 * a, 30 * (1 - a) + 50 * a, 40 * (1 - a) + 200 * a, 255 },
                        3.0f),
          "background: a single image composites with its alpha over the clear");
    return 0;
}

static int check_background_crossfade(BackgroundCtx* c) {
    FlPixelReadback buf;
    // Crossfade interpolates with crossfade_t: prev (red) to current (blue), checked at t = 0, 0.5, 1.
    const struct {
        f32 t;
        f32 r, g, b;
    } cases[] = {
        { 0.0f, 200, 40, 50 },  // full prev
        { 0.5f, 120, 45, 125 }, // lerp(red, blue, ~0.5)
        { 1.0f, 40, 50, 200 },  // full current
    };
    for (int i = 0; i < 3; i++) {
        void* start = ir_rewind_begin(c->ir_arena);
        FlBackground fade = { .current = 302, .prev = 301, .crossfade_t = cases[i].t, .clear = k_background_clear };
        CHECK(render_background(c, start, fade, &buf), "read_pixels failed on a crossfade frame");
        CHECK(
            channels_near(pixel_at(&buf, 960, 540), (ChannelExpect) { cases[i].r, cases[i].g, cases[i].b, 255 }, 3.0f),
            "crossfade: the blend matches lerp(prev, current, t) at t in {0, 0.5, 1}");
    }
    return 0;
}

static int check_background_under_ui(BackgroundCtx* c) {
    FlPixelReadback buf;
    void* start = ir_rewind_begin(c->ir_arena);
    const FlColor green = { 40, 200, 60, 255 };
    emit_solid_rect(c->ir_arena, (FlRect) { 100, 100, 200, 200 }, green);
    const FlBackground clear_only
        = { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID, .clear = k_background_clear };
    CHECK(render_background(c, start, clear_only, &buf), "read_pixels failed on the UI-over-background frame");
    CHECK(pixel_color(pixel_at(&buf, 200, 200), green, 2.0f), "background: the UI rect draws over the background");
    CHECK(pixel_color(pixel_at(&buf, 500, 500), (FlColor) { 20, 30, 40, 255 }, 2.0f),
          "background: the clear shows where no UI covers");
    return 0;
}

int run_background_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    BackgroundCtx ctx = {
        .backend = backend,
        .surface = backend->create_surface(backend->user_data, 1920, 1080),
        .ir_arena = ir_arena,
        .frame = test_frame(),
    };
    FlArena* scratch = arena_new();

    if (prepare_background_images(backend, scratch) != 0) {
        return 1;
    }
    if (check_background_clear_and_single(&ctx) != 0) {
        return 1;
    }
    if (check_background_crossfade(&ctx) != 0) {
        return 1;
    }
    if (check_background_under_ui(&ctx) != 0) {
        return 1;
    }

    fl_arena_destroy(scratch);
    printf("gpu_smoke_test: background pixel tests PASS\n");
    backend->destroy_surface(backend->user_data, ctx.surface);
    return 0;
}

// Effect rendering: a drop shadow casts the shape's silhouette offset + blurred and under the
// primitive; a glow casts it centered + blurred and additive. The shape's own coverage rides on top untouched.
// The readback is premultiplied coverage over a transparent target, so every expectation below is computable
// from the shape's SDF.

int run_effect_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    FlRenderFrame frame = test_frame();
    FlPixelReadback buf;

    // A red square [100,100]..[140,140] with an opaque green drop shadow offset (30, 30), blur 4. The shadow's
    // silhouette is the square shifted by the offset: [130,130]..[170,170], blurred at its edges.
    const FlColor shadow_color = { 30, 220, 40, 255 };
    FlEffect shadow
        = { .kind = FlEffectKind_DropShadow, .color = shadow_color, .off_x = 30.0f, .off_y = 30.0f, .blur = 4.0f };

    void* start = ir_rewind_begin(ir_arena);
    emit_rect_effect(ir_arena, (FlRect) { 100.0f, 100.0f, 40.0f, 40.0f }, red, &shadow);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the drop-shadow frame");

    CHECK(pixel_color(pixel_at(&buf, 160, 160), (FlColor) { 30, 220, 40, 255 }, 3.0f),
          "drop shadow: present at full coverage in the offset silhouette, clear of the primitive");
    CHECK(pixel_transparent(pixel_at(&buf, 300, 300)), "drop shadow: no shadow far outside the silhouette's reach");
    CHECK(pixel_color(pixel_at(&buf, 135, 135), red, 2.0f),
          "drop shadow: a covered interior pixel is the primitive alone, the shadow hidden beneath it");

    // Walk inward across the shadow's right edge (at x = 170, along y = 160, clear of the square at x <= 140).
    CHECK(check_aa_ramp_line(&buf, (AaRampRun) { 178, 160, -1, 0, 16 },
                             (AaRampSpec) { shadow_color, true, "drop shadow falloff" })
              == 0,
          "drop shadow: a blurred edge falls off monotonically, premultiplied by coverage");

    // Glow over a transparent target: centered (the offset is ignored), so it bleeds symmetrically on both
    // sides of the square. A blur-6 green glow on the same red square.
    const FlColor glow_color = { 0, 200, 0, 255 };
    FlEffect glow = { .kind = FlEffectKind_Glow, .color = glow_color, .off_x = 30.0f, .off_y = 30.0f, .blur = 6.0f };
    start = ir_rewind_begin(ir_arena);
    emit_rect_effect(ir_arena, (FlRect) { 100.0f, 100.0f, 40.0f, 40.0f }, red, &glow);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the glow frame");
    // Pixels mirrored about the square's center column (x = 120): x = 143 and x = 96 are the same distance
    // outside the right and left edges.
    const u8* right = pixel_at(&buf, 143, 120);
    const u8* left = pixel_at(&buf, 96, 120);
    CHECK(right[1] > 20, "glow: the centered glow bleeds past the primitive's edge");
    CHECK(channels_near(left, (ChannelExpect) { (f32)right[0], (f32)right[1], (f32)right[2], (f32)right[3] }, 3.0f),
          "glow: centered — equal bleed on both edges, the offset ignored");

    // Additivity: over an opaque grey backdrop a glow pixel is brighter than the backdrop alone: green lifted
    // by the glow, where src-over could only darken.
    const FlColor grey = { 80, 80, 80, 255 };
    start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, (FlRect) { 80.0f, 80.0f, 120.0f, 120.0f }, grey); // backdrop under the glow's bleed
    emit_rect_effect(ir_arena, (FlRect) { 100.0f, 100.0f, 40.0f, 40.0f }, red, &glow);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the additive-glow frame");
    const u8* lit = pixel_at(&buf, 143, 120); // over the backdrop, in the glow bleed, clear of the red square
    CHECK(lit[1] > 100 && lit[1] > lit[0] && lit[1] > lit[2],
          "glow: additive — the green glow lifts the backdrop above its own grey");
    CHECK(lit[0] >= 80 - 2 && lit[2] >= 80 - 2,
          "glow: additive — the backdrop's other channels are preserved, only added to");

    printf("gpu_smoke_test: effect pixel tests PASS\n");
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

#endif // !defined(__APPLE__)
