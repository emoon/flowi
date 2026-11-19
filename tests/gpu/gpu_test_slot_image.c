// GPU smoke test: cross-backend slot-image pixel tests.

#include "gpu_test_common.h"

// Cross-backend slot-image pixel tests for the non-atlas image paths end to end: large statics on
// dedicated slot textures, dynamic buffers streamed on referencing frames, strided rows, full slot occupancy,
// and release_image on the slot tier. Runs on both backends through the neutral seam; slot assignment is
// backend-private, so every expectation reads only rendered pixels and no per-backend golden fork is needed.
enum { LW = 300, LH = 8, DW = 16, DH = 16 };

static u8 large_px[LW * LH * 4];
static u8 large_alpha[LW * 4];
static u8 dyn_px[DW * DH * 4];
static u8 dyn_alpha[8];
static u8 dyn_strided[24 * 2];
static u8 dyn_repack[18 * 2];

static const FlImageSource img_large = { .kind = FlImageKind_Static, .handle = 201 };
static const FlImageSource img_alpha = { .kind = FlImageKind_Static, .handle = 202 };
static const FlColor alpha_tint = { 200, 40, 50, 255 };
static const FlColor dyn_alpha_tint = { 40, 200, 60, 255 };
static const FlImageSource img_dyn
    = { .kind = FlImageKind_Dynamic, .dynamic = { .pixels = dyn_px, .w = DW, .h = DH, .stride = DW * 4 } };
// Stride 0: tightly packed, the per-frame dynamic stream deriving w * bpp for a single-channel source.
static const FlImageSource img_dyn_alpha
    = { .kind = FlImageKind_Dynamic, .dynamic = { .pixels = dyn_alpha, .w = 8, .h = 1, .stride = 0 } };
static const FlImageSource img_dyn_strided
    = { .kind = FlImageKind_Dynamic, .dynamic = { .pixels = dyn_strided, .w = 4, .h = 2, .stride = 24 } };
static const FlImageSource img_dyn_repack
    = { .kind = FlImageKind_Dynamic, .dynamic = { .pixels = dyn_repack, .w = 4, .h = 2, .stride = 18 } };

typedef struct SlotCtx {
    FlRenderBackend* backend;
    FlSurface* surface;
    FlArena* ir_arena;
    FlArena* scratch;
    FlRenderFrame frame;
    FlPixelReadback buf;
} SlotCtx;

static void emit_color(FlArena* arena, FlImageSource src, FlRect dst) {
    ir_image(arena, src, FlImageMode_Color, dst, (FlRect) { 0 }, white, FlImageFilter_Nearest, nullptr);
}

static void emit_color_sub(FlArena* arena, FlImageSource src, FlRect dst, FlRect src_rect) {
    ir_image(arena, src, FlImageMode_Color, dst, src_rect, white, FlImageFilter_Nearest, nullptr);
}

static void emit_coverage(FlArena* arena, FlImageSource src, FlRect dst, FlColor tint) {
    ir_image(arena, src, FlImageMode_AlphaCoverage, dst, (FlRect) { 0 }, tint, FlImageFilter_Nearest, nullptr);
}

static bool slot_render_read(SlotCtx* c, const void* start) {
    ir_frame_render(c->backend, c->surface, &c->frame, c->ir_arena, start);
    return render_seam_read_pixels(c->backend, c->surface, &c->buf);
}

static void fill_large_color(void) {
    // Large color source: 300 wide (past the atlas's 256 cap), every texel derived from its coordinates,
    // with a hard blue split at x == 150 so linear and nearest sampling are distinguishable when scaled.
    // Opaque texels keep premultiplied == straight, so expectations stay exact.
    for (int y = 0; y < LH; y++) {
        for (int x = 0; x < LW; x++) {
            u8* px = &large_px[(y * LW + x) * 4];
            px[0] = (u8)x;
            px[1] = (u8)(y * 30);
            px[2] = x >= 150 ? 255 : 0;
            px[3] = 255;
        }
    }
}

static void fill_large_alpha(void) {
    // Large alpha-coverage source: 300x4 single-channel, one alpha per column.
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < LW; x++) {
            large_alpha[y * LW + x] = (u8)x;
        }
    }
}

static int prepare_large_sources(SlotCtx* c) {
    FlRenderBackend* backend = c->backend;
    FlImageLoadRequest req = { .relative_path = FL_S("synthetic-large") };
    fill_large_color();
    FlImageDecodeResult large_decode = {
        .pixels = large_px,
        .width = LW,
        .height = LH,
        .stride = LW * 4,
        .channels = 4,
        .premultiplied = true,
    };
    CHECK(backend->prepare_image(backend->user_data, 201, &large_decode, c->scratch, &req),
          "prepare_image: the backend reports the image realized");

    fill_large_alpha();
    // Stride 0 = tightly packed, so the upload derives the row pitch as w * bpp itself. This is the single-channel
    // side of that fallback, which a source spelling its tight stride explicitly never reaches.
    FlImageDecodeResult alpha_decode = { .pixels = large_alpha, .width = LW, .height = 4, .stride = 0, .channels = 1 };
    CHECK(backend->prepare_image(backend->user_data, 202, &alpha_decode, c->scratch, &req),
          "prepare_image: the backend reports the image realized");
    return 0;
}

static void init_dynamic_sources(void) {
    for (int i = 0; i < DW * DH; i++) {
        dyn_px[i * 4 + 0] = 10;
        dyn_px[i * 4 + 1] = 200;
        dyn_px[i * 4 + 2] = 30;
        dyn_px[i * 4 + 3] = 255;
    }
    for (int x = 0; x < 8; x++) {
        dyn_alpha[x] = (u8)(x * 36);
    }
    // Strided dynamic buffers: 4x2, one with whole-pixel padding (stride 24, the GL_UNPACK_ROW_LENGTH
    // path), one with a stride no whole pixel count reaches (18, the repack path). Padding is sentinel.
    memset(dyn_strided, 0xAA, sizeof(dyn_strided));
    memset(dyn_repack, 0xAA, sizeof(dyn_repack));
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 4; x++) {
            u8* s = &dyn_strided[y * 24 + x * 4];
            s[0] = (u8)(40 * x + 5);
            s[1] = (u8)(80 * y + 10);
            s[2] = 99;
            s[3] = 255;
            u8* r = &dyn_repack[y * 18 + x * 4];
            r[0] = (u8)(50 * x + 3);
            r[1] = (u8)(90 * y + 7);
            r[2] = 7;
            r[3] = 255;
        }
    }
}

static void emit_matrix_frame(FlArena* ir_arena) {
    // Frame 1: the slot-image matrix: large static native and scaled (nearest), src sub-rect, large
    // alpha-coverage colorization, and all four dynamic buffers side by side.
    emit_color(ir_arena, img_large, (FlRect) { 8, 8, LW, LH });                                   // native
    emit_color(ir_arena, img_large, (FlRect) { 8, 24, 600, 16 });                                 // 2x up
    emit_color(ir_arena, img_large, (FlRect) { 700, 24, 100, 8 });                                // 3x down
    emit_color_sub(ir_arena, img_large, (FlRect) { 860, 24, 80, 8 }, (FlRect) { 260, 0, 40, 8 }); // sub-rect 2x
    emit_coverage(ir_arena, img_alpha, (FlRect) { 8, 48, LW, 4 }, alpha_tint);                    // colorize
    emit_color(ir_arena, img_dyn, (FlRect) { 400, 48, DW, DH });
    emit_coverage(ir_arena, img_dyn_alpha, (FlRect) { 448, 48, 8, 1 }, dyn_alpha_tint);
    emit_color(ir_arena, img_dyn_strided, (FlRect) { 480, 48, 4, 2 });
    emit_color(ir_arena, img_dyn_repack, (FlRect) { 500, 48, 4, 2 });
}

static int check_native_texels(const FlPixelReadback* buf) {
    // Native size is texel passthrough, including columns past the 256 atlas cap, which proves the
    // command samples a dedicated slot texture, not an atlas cell.
    const int xs[] = { 0, 1, 128, 149, 150, 255, 256, 270, 299 };
    for (size_t i = 0; i < sizeof_array(xs); i++) {
        int x = xs[i];
        f32 b = x >= 150 ? 255.0f : 0.0f;
        const u8* top = pixel_at(buf, 8 + x, 8);
        const u8* bottom = pixel_at(buf, 8 + x, 15);
        CHECK(channels_near(top, (ChannelExpect) { (f32)(x & 255), 0, b, 255 }, 1.0f)
                  && channels_near(bottom, (ChannelExpect) { (f32)(x & 255), 210, b, 255 }, 1.0f),
              "slot image: native size must pass every sampled texel through exactly");
    }
    CHECK(pixel_transparent(pixel_at(buf, 7, 10)) && pixel_transparent(pixel_at(buf, 309, 10)),
          "slot image: nothing renders outside the dst rect");
    return 0;
}

static int check_scaled_texels(const FlPixelReadback* buf) {
    // Scaled into a dst rect (nearest): a 2x upscale maps dst pixel pairs to one texel; a 3x downscale
    // samples texel 3x+1 (a mid-texel coordinate, so no fetch sits on a texel boundary); the src sub-rect
    // shows only its region, scaled.
    const int up_xs[] = { 0, 10, 149, 150, 200, 299 };
    for (size_t i = 0; i < sizeof_array(up_xs); i++) {
        int x = up_xs[i];
        f32 b = x >= 150 ? 255.0f : 0.0f;
        const u8* first = pixel_at(buf, 8 + 2 * x, 24 + 2);
        const u8* second = pixel_at(buf, 8 + 2 * x + 1, 24 + 2);
        CHECK(channels_near(first, (ChannelExpect) { (f32)(x & 255), 30, b, 255 }, 1.0f)
                  && channels_near(second, (ChannelExpect) { (f32)(x & 255), 30, b, 255 }, 1.0f),
              "slot image scaled: a 2x nearest upscale maps each dst pixel pair to its texel");
    }
    const int down_xs[] = { 0, 33, 49, 50, 99 };
    for (size_t i = 0; i < sizeof_array(down_xs); i++) {
        int x = down_xs[i];
        int texel = 3 * x + 1;
        f32 b = texel >= 150 ? 255.0f : 0.0f;
        const u8* px = pixel_at(buf, 700 + x, 24 + 1);
        CHECK(channels_near(px, (ChannelExpect) { (f32)(texel & 255), 30, b, 255 }, 1.0f),
              "slot image scaled: a 3x nearest downscale samples texel 3x+1");
    }
    for (int x = 0; x < 80; x += 13) {
        int texel = 260 + x / 2; // 2x upscale of the {260, 0, 40, 8} sub-rect: dst pixel pairs share a texel
        const u8* px = pixel_at(buf, 860 + x, 24 + 1);
        CHECK(channels_near(px, (ChannelExpect) { (f32)(texel & 255), 30, 255, 255 }, 1.0f),
              "slot image: a src sub-rect samples only its region, scaled");
    }
    return 0;
}

static int check_static_alpha_tint(const FlPixelReadback* buf) {
    const int xs[] = { 0, 100, 255, 280 };
    for (size_t i = 0; i < sizeof_array(xs); i++) {
        f32 a = (f32)(xs[i] & 255);
        const u8* px = pixel_at(buf, 8 + xs[i], 49);
        CHECK(
            channels_near(px, (ChannelExpect) { a * 200.0f / 255.0f, a * 40.0f / 255.0f, a * 50.0f / 255.0f, a }, 3.0f),
            "slot image alpha-coverage: the tint colorizes coverage, premultiplied");
    }
    return 0;
}

static int check_dynamic_alpha_row(const FlPixelReadback* buf, int base, int step, const char* msg) {
    for (int x = 0; x < 8; x++) {
        f32 a = (f32)(base + x * step);
        const u8* px = pixel_at(buf, 448 + x, 48);
        CHECK(
            channels_near(px, (ChannelExpect) { a * 40.0f / 255.0f, a * 200.0f / 255.0f, a * 60.0f / 255.0f, a }, 3.0f),
            msg);
    }
    return 0;
}

static int check_dynamic_strided(const FlPixelReadback* buf) {
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 4; x++) {
            const u8* whole = pixel_at(buf, 480 + x, 48 + y);
            const u8* repacked = pixel_at(buf, 500 + x, 48 + y);
            CHECK(channels_near(whole, (ChannelExpect) { (f32)(40 * x + 5), (f32)(80 * y + 10), 99, 255 }, 1.0f),
                  "dynamic strided: whole-pixel row padding rides the upload without leaking");
            CHECK(channels_near(repacked, (ChannelExpect) { (f32)(50 * x + 3), (f32)(90 * y + 7), 7, 255 }, 1.0f),
                  "dynamic strided: sub-pixel row padding repacks without leaking");
        }
    }
    return 0;
}

static int check_matrix_frame(SlotCtx* c) {
    void* start = ir_rewind_begin(c->ir_arena);
    emit_matrix_frame(c->ir_arena);
    CHECK(slot_render_read(c, start), "read_pixels failed on the slot-image frame");

    if (check_native_texels(&c->buf) != 0) {
        return 1;
    }
    if (check_scaled_texels(&c->buf) != 0) {
        return 1;
    }
    if (check_static_alpha_tint(&c->buf) != 0) {
        return 1;
    }

    CHECK(pixel_color(pixel_at(&c->buf, 408, 56), (FlColor) { 10, 200, 30, 255 }, 1.0f),
          "dynamic image: the first referencing frame renders the buffer content");
    if (check_dynamic_alpha_row(&c->buf, 0, 36,
                                "dynamic image alpha-coverage: the tint colorizes the live coverage buffer")
        != 0) {
        return 1;
    }
    return check_dynamic_strided(&c->buf);
}

static int check_dynamic_mutation(SlotCtx* c) {
    // Frame 2: mutate both dynamic buffers and re-render the same commands. The color buffer's new alpha of
    // 128 must not show: dynamic color renders opaque, matching the SW backend's planar (alpha-dropping)
    // realization.
    for (int i = 0; i < DW * DH; i++) {
        dyn_px[i * 4 + 0] = 200;
        dyn_px[i * 4 + 1] = 20;
        dyn_px[i * 4 + 2] = 160;
        dyn_px[i * 4 + 3] = 128;
    }
    for (int x = 0; x < 8; x++) {
        dyn_alpha[x] = (u8)(252 - x * 36);
    }
    void* start = ir_rewind_begin(c->ir_arena);
    emit_color(c->ir_arena, img_dyn, (FlRect) { 400, 48, DW, DH });
    emit_coverage(c->ir_arena, img_dyn_alpha, (FlRect) { 448, 48, 8, 1 }, dyn_alpha_tint);
    CHECK(slot_render_read(c, start), "read_pixels failed on the mutated dynamic frame");
    CHECK(pixel_color(pixel_at(&c->buf, 408, 56), (FlColor) { 200, 20, 160, 255 }, 1.0f),
          "dynamic image: a content change between frames re-uploads, and color mode renders opaque");
    return check_dynamic_alpha_row(&c->buf, 252, -36,
                                   "dynamic image alpha-coverage: a content change between frames re-uploads");
}

static int check_linear_slot(SlotCtx* c) {
    // Frame 3: linear filtering on a slot texture; the slot's sampler comes from the referencing command.
    // A 2x linear upscale across the blue split at texel 150 ramps through genuine intermediates where
    // nearest (frame 1's 2x upscale) stepped 0 -> 255 between adjacent pixel pairs.
    void* start = ir_rewind_begin(c->ir_arena);
    ir_image(c->ir_arena, img_large, FlImageMode_Color, (FlRect) { 8, 96, 600, 8 }, (FlRect) { 0 }, white,
             FlImageFilter_Linear, nullptr);
    CHECK(slot_render_read(c, start), "read_pixels failed on the linear slot frame");

    const u8 left_blue = pixel_at(&c->buf, 8 + 298, 97)[2];
    const u8 right_blue = pixel_at(&c->buf, 8 + 301, 97)[2];
    CHECK(left_blue <= 32 && right_blue >= 224, "slot image linear: the split stays sharp away from the boundary");
    const u8 ramp_a = pixel_at(&c->buf, 8 + 299, 97)[2];
    const u8 ramp_b = pixel_at(&c->buf, 8 + 300, 97)[2];
    CHECK(ramp_a > 32 && ramp_a < 224 && ramp_b > 32 && ramp_b < 224,
          "slot image linear: a 2x upscale ramps through genuine intermediates at the split");
    CHECK(ramp_a < ramp_b, "slot image linear: the ramp rises monotonically across the split");
    return 0;
}

static int check_slot_overflow(SlotCtx* c) {
    // Frame 4: full slot occupancy: GPU_IMAGE_SLOT_COUNT + 2 distinct dynamic images in one frame, so the
    // trailing two land past the sampler array's capacity.
    enum { OV_COUNT = GPU_IMAGE_SLOT_COUNT + 2 };
    static u8 ov_px[OV_COUNT][4];
    void* start = ir_rewind_begin(c->ir_arena);
    for (int i = 0; i < OV_COUNT; i++) {
        ov_px[i][0] = (u8)(10 * i + 5);
        ov_px[i][1] = 77;
        ov_px[i][2] = 200;
        ov_px[i][3] = 255;
        FlImageSource src
            = { .kind = FlImageKind_Dynamic, .dynamic = { .pixels = ov_px[i], .w = 1, .h = 1, .stride = 4 } };
        emit_color(c->ir_arena, src, (FlRect) { (f32)(8 + i * 8), 80, 4, 4 });
    }
    CHECK(slot_render_read(c, start), "read_pixels failed on the overflow frame");
    for (int i = 0; i < OV_COUNT; i++) {
        const u8* px = pixel_at(&c->buf, 9 + i * 8, 81);
        if (i < GPU_IMAGE_SLOT_COUNT) {
            CHECK(channels_near(px, (ChannelExpect) { (f32)(10 * i + 5), 77, 200, 255 }, 1.0f),
                  "slot overflow: every slotted image renders its own texture");
        } else {
            CHECK(pixel_transparent(px),
                  "slot overflow: commands past the slot capacity drop to transparent, predictably");
        }
    }
    return 0;
}

static u32 render_and_count_uploads(SlotCtx* c, FlImageSource src, FlRect dst) {
    void* start = ir_rewind_begin(c->ir_arena);
    emit_color(c->ir_arena, src, dst);
    ir_frame_render(c->backend, c->surface, &c->frame, c->ir_arena, start);
    return gpu_debug_dynamic_upload_count(c->backend->user_data);
}

static int check_upload_streaming(SlotCtx* c) {
    // Upload-streaming contract: a dynamic image uploads only on frames that reference it. The debug counter
    // ticks once per dynamic stream.
    static u8 stream_px[4 * 4 * 4];
    for (int i = 0; i < 4 * 4; i++) {
        stream_px[i * 4 + 0] = 60;
        stream_px[i * 4 + 1] = 120;
        stream_px[i * 4 + 2] = 180;
        stream_px[i * 4 + 3] = 255;
    }
    const FlImageSource img_stream
        = { .kind = FlImageKind_Dynamic, .dynamic = { .pixels = stream_px, .w = 4, .h = 4, .stride = 16 } };
    const FlRect stream_dst = { 8, 120, 4, 4 };

    u32 c0 = gpu_debug_dynamic_upload_count(c->backend->user_data);
    u32 c1 = render_and_count_uploads(c, img_stream, stream_dst);
    CHECK(c1 == c0 + 1, "dynamic upload counter: a referencing frame streams the buffer exactly once");

    // A plain static draw, no dynamic image: the entry stays live but uploads nothing.
    void* start = ir_rewind_begin(c->ir_arena);
    emit_coverage(c->ir_arena, img_alpha, (FlRect) { 8, 48, LW, 4 }, alpha_tint);
    ir_frame_render(c->backend, c->surface, &c->frame, c->ir_arena, start);
    u32 c2 = gpu_debug_dynamic_upload_count(c->backend->user_data);
    CHECK(c2 == c1, "dynamic upload counter: a frame that references no dynamic image uploads nothing");

    u32 c3 = render_and_count_uploads(c, img_stream, stream_dst);
    CHECK(c3 == c2 + 1, "dynamic upload counter: re-referencing the buffer streams it again");
    return 0;
}

static int check_slot_release(SlotCtx* c) {
    // Release: the released image's texture is deleted on the next render.
    c->backend->release_image(c->backend->user_data, 201);
    void* start = ir_rewind_begin(c->ir_arena);
    emit_color(c->ir_arena, img_large, (FlRect) { 8, 8, LW, LH });
    emit_coverage(c->ir_arena, img_alpha, (FlRect) { 8, 48, LW, 4 }, alpha_tint);
    CHECK(slot_render_read(c, start), "read_pixels failed after slot release");
    CHECK(pixel_transparent(pixel_at(&c->buf, 108, 12)), "slot release: a released large image stops rendering");
    const u8* kept = pixel_at(&c->buf, 8 + 100, 49);
    CHECK(channels_near(
              kept, (ChannelExpect) { 100.0f * 200.0f / 255.0f, 100.0f * 40.0f / 255.0f, 100.0f * 50.0f / 255.0f, 100 },
              3.0f),
          "slot release: other slot images keep rendering after an unrelated release");
    return 0;
}

static int run_slot_frames(SlotCtx* c) {
    if (check_matrix_frame(c) != 0) {
        return 1;
    }
    if (check_dynamic_mutation(c) != 0) {
        return 1;
    }
    return check_linear_slot(c);
}

static int run_slot_occupancy_and_release(SlotCtx* c) {
    if (check_slot_overflow(c) != 0) {
        return 1;
    }
    if (check_upload_streaming(c) != 0) {
        return 1;
    }
    return check_slot_release(c);
}

int run_slot_image_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    SlotCtx ctx = {
        .backend = backend,
        .surface = backend->create_surface(backend->user_data, 1920, 1080),
        .ir_arena = ir_arena,
        .scratch = arena_new(),
        .frame = test_frame(),
    };

    if (prepare_large_sources(&ctx) != 0) {
        return 1;
    }
    init_dynamic_sources();
    if (run_slot_frames(&ctx) != 0) {
        return 1;
    }
    if (run_slot_occupancy_and_release(&ctx) != 0) {
        return 1;
    }

    fl_arena_destroy(ctx.scratch);
    printf("gpu_smoke_test: slot-image pixel tests PASS\n");
    backend->destroy_surface(backend->user_data, ctx.surface);
    return 0;
}
