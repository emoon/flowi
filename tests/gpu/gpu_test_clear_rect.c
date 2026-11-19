// GPU smoke test: cross-backend clear-screen, rect and border pixel tests.

#include "gpu_test_common.h"

// Shared clear-screen case: the one pixel case that runs on both backends against one golden. It is
// the whole story on Metal (the backend clears the offscreen target to the frame envelope's background colour, no
// IR consumed) and a trivial slice of the GL pipeline (the background translator emits a fullscreen clear the
// raster pass fills). An opaque clear premultiplies to itself, so both backends must land byte-identical RGBA8,
// pinned against a shared solid-colour PNG golden decoded here.

// 64x64 solid #224466 (34,68,102) opaque RGBA PNG (color type 6).
static const unsigned char k_clear_screen_golden_png[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
    0x00, 0x40, 0x00, 0x00, 0x00, 0x40, 0x08, 0x06, 0x00, 0x00, 0x00, 0xaa, 0x69, 0x71, 0xde, 0x00, 0x00, 0x00,
    0x66, 0x49, 0x44, 0x41, 0x54, 0x78, 0xda, 0xed, 0xd0, 0x41, 0x11, 0x00, 0x00, 0x04, 0x00, 0x30, 0x59, 0x64,
    0x91, 0x45, 0xff, 0x06, 0xe4, 0x70, 0xf6, 0x58, 0x81, 0x45, 0x56, 0xcf, 0x67, 0x21, 0x40, 0x80, 0x00, 0x01,
    0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00, 0x01,
    0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00, 0x01,
    0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00, 0x01,
    0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x00, 0x01, 0x02, 0x04, 0xdc, 0xb7, 0x79, 0xea, 0xb1, 0xa5, 0x9e,
    0x77, 0xbc, 0xeb, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

// The clear colour the case renders and the golden encodes, kept in step with k_clear_screen_golden_png.

int run_clear_screen_test(FlRenderBackend* backend, FlArena* ir_arena) {
    const int w = 64;
    const int h = 64;

    FlSurface* surface = backend->create_surface(backend->user_data, w, h);

    // The backend exposes its offscreen target through the single-name platform-conditional accessor:
    // a GL texture-name u32 or a retained id<MTLTexture> void*, both non-null once a surface exists.
    GpuSurfaceTexture tex = gpu_renderer_surface_texture(surface);
    CHECK(tex.handle != 0, "clear-screen: the surface has a backend-owned offscreen target");
    CHECK(tex.width == w && tex.height == h, "clear-screen: the surface texture carries the surface dims");

    FlRenderFrame frame = test_frame();
    frame.commands = nullptr;
    frame.bytes = 0;
    frame.background.clear = (FlColor) { CLEAR_SCREEN_R, CLEAR_SCREEN_G, CLEAR_SCREEN_B, 255 };
    render_seam_render(backend, surface, &frame);

    FlPixelReadback rb;
    CHECK(backend->read_pixels != nullptr, "clear-screen: backend has a pixel-readback capability");
    CHECK(render_seam_read_pixels(backend, surface, &rb), "clear-screen: read_pixels failed");
    CHECK(rb.width == (u32)w && rb.height == (u32)h, "clear-screen: readback dims match the surface");
    CHECK(rb.stride == (u32)w * 4u, "clear-screen: readback is tight RGBA8 (stride == width*4)");

    FlImageDecodeResult* golden = image_decode(ir_arena, k_clear_screen_golden_png, sizeof(k_clear_screen_golden_png),
                                               &(ImageDecodeOptions) { 0 });
    CHECK(golden != nullptr, "clear-screen: the golden PNG decodes");
    CHECK(golden->width == w && golden->height == h && golden->channels == 4,
          "clear-screen: the golden is a 64x64 RGBA image");
    CHECK((u32)golden->stride == (u32)w * 4u, "clear-screen: the golden decodes to tight RGBA8 rows");
    CHECK(memcmp(rb.pixels, golden->pixels, (size_t)w * (size_t)h * 4u) == 0,
          "clear-screen: the readback matches the shared solid-colour golden byte-for-byte");

    backend->destroy_surface(backend->user_data, surface);
    printf("gpu_smoke_test: clear-screen golden PASS\n");
    return 0;
}

// Shared rect and border pixel cases: the core-tier primitives running on both backends through the
// neutral seam. Every expectation is a property of the rounded-rect SDF coverage model (coverage = clamp(0.5 - d))
// both backends compute identically, checked with the same pixel_color / channels_near tolerance: one
// comparison, with no per-backend golden fork. Both cases render over a transparent background (no clear command), so
// an untouched pixel reads exactly zero.

// The workhorse fill colour (opaque, so it stores premultiplied to itself).
static const FlColor rect_fill = { 200, 40, 50, 255 };

// Open an 8-aligned IR run on ir_arena (rewinding it first), exactly as the core does, and return the run's
// start pointer; the caller fills frame.commands/.bytes from it before rendering.
static const void* shared_ir_begin(FlArena* ir_arena) {
    fl_arena_rewind(ir_arena);
    fl_arena_align(ir_arena, 8);
    return fl_arena_current_pos(ir_arena);
}

int run_rect_pixel_test(FlRenderBackend* backend, FlArena* ir_arena) {
    const int w = 128;
    const int h = 128;
    FlSurface* surface = backend->create_surface(backend->user_data, w, h);

    const void* start = shared_ir_begin(ir_arena);
    ir_fill_rect(ir_arena, (FlRect) { 32, 32, 64, 64 }, (FlCornerRadius) { .tl = 16.0f },
                 (FlColor[4]) { rect_fill, rect_fill, rect_fill, rect_fill }, nullptr);
    FlRenderFrame frame = test_frame();
    frame.commands = start;
    frame.bytes = (size_t)((const u8*)fl_arena_current_pos(ir_arena) - (const u8*)start);
    render_seam_render(backend, surface, &frame);

    FlPixelReadback buf;
    CHECK(render_seam_read_pixels(backend, surface, &buf), "rect: read_pixels failed");

    // The TL arc circle is centred at (48,48), radius 16.
    CHECK(pixel_color(pixel_at(&buf, 60, 60), rect_fill, 2.0f), "rect: the interior is the exact solid fill");
    CHECK(pixel_transparent(pixel_at(&buf, 33, 33)), "rect: a pixel outside the rounded corner arc stays transparent");
    CHECK(pixel_color(pixel_at(&buf, 95, 32), rect_fill, 2.0f), "rect: a radius-0 corner stays sharp (covered)");
    CHECK(pixel_transparent(pixel_at(&buf, 96, 32)), "rect: nothing bleeds past a radius-0 corner edge");
    CHECK(pixel_color(pixel_at(&buf, 64, 32), rect_fill, 2.0f), "rect: a straight top-edge midpoint is fully covered");

    u8 prev_a = 0;
    bool saw_partial = false;
    for (int d = 0; d <= 15; d++) {
        const u8* px = pixel_at(&buf, 33 + d, 33 + d);
        CHECK(px[3] >= prev_a, "rect: corner AA alpha must rise monotonically along the diagonal");
        prev_a = px[3];
        if (px[3] > 0 && px[3] < 255) {
            saw_partial = true;
            f32 a = (f32)px[3] / 255.0f;
            CHECK(channels_near(
                      px,
                      (ChannelExpect) { (f32)rect_fill.r * a, (f32)rect_fill.g * a, (f32)rect_fill.b * a, (f32)px[3] },
                      3.0f),
                  "rect: a corner AA pixel is the fill premultiplied by its coverage");
        }
    }
    CHECK(saw_partial, "rect: the rounded corner produces at least one genuine partial-coverage pixel");
    CHECK(prev_a == 255, "rect: the diagonal ends fully opaque inside the fill");

    backend->destroy_surface(backend->user_data, surface);
    printf("gpu_smoke_test: rect (rounded corner) PASS\n");
    return 0;
}

int run_border_pixel_test(FlRenderBackend* backend, FlArena* ir_arena) {
    const int w = 128;
    const int h = 128;
    FlSurface* surface = backend->create_surface(backend->user_data, w, h);

    // Outer edges at 32/96, inner edges at 36/92 (outer + width), so the band is {32..36} and {92..96} per axis.
    const void* start = shared_ir_begin(ir_arena);
    ir_border(ir_arena, (FlRect) { 32, 32, 64, 64 }, 4, 4, 4, 4, (FlCornerRadius) { 0 }, rect_fill);
    FlRenderFrame frame = test_frame();
    frame.commands = start;
    frame.bytes = (size_t)((const u8*)fl_arena_current_pos(ir_arena) - (const u8*)start);
    render_seam_render(backend, surface, &frame);

    FlPixelReadback buf;
    CHECK(render_seam_read_pixels(backend, surface, &buf), "border: read_pixels failed");

    CHECK(pixel_color(pixel_at(&buf, 33, 64), rect_fill, 2.0f) && pixel_color(pixel_at(&buf, 94, 64), rect_fill, 2.0f)
              && pixel_color(pixel_at(&buf, 64, 33), rect_fill, 2.0f)
              && pixel_color(pixel_at(&buf, 64, 94), rect_fill, 2.0f),
          "border: all four side bands are hit");
    CHECK(pixel_color(pixel_at(&buf, 33, 33), rect_fill, 2.0f),
          "border: a square corner sits in both bands and is hit");
    CHECK(pixel_transparent(pixel_at(&buf, 64, 64)), "border: the interior hole stays untouched");
    CHECK(pixel_transparent(pixel_at(&buf, 31, 64)) && pixel_transparent(pixel_at(&buf, 64, 31)),
          "border: nothing lands outside the outer edge");
    CHECK(pixel_color(pixel_at(&buf, 35, 64), rect_fill, 2.0f) && pixel_transparent(pixel_at(&buf, 36, 64)),
          "border: an integer inner edge cuts exactly between band and hole");

    backend->destroy_surface(backend->user_data, surface);
    printf("gpu_smoke_test: border (band) PASS\n");
    return 0;
}
