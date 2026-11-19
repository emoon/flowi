// GPU smoke test: IOSurface surface-texture variant (Metal only).

#include "gpu_test_common.h"

#if defined(__APPLE__)

// IOSurface Surface-texture variant, Metal only. The backend's second, additive accessor hands back the
// offscreen target's backing IOSurface so an out-of-process host can sample the UI cross-process. This case stands in
// for that host in-process: it renders the known clear colour, opens the returned IOSurface into a second MTLTexture
// on a freshly created MTLDevice (gpu_test_iosurface_readback, the Objective-C probe arm), and confirms the pixels
// read back through that independent device match both the golden colour and the primary accessor's own readback.

// Defined in gpu_iosurface_probe_metal.m (the Objective-C test arm); opens the IOSurface on a fresh device and blits
// its RGBA8 pixels into out_rgba (tight, width*height*4, row 0 first). Returns true on success.
bool gpu_test_iosurface_readback(void* iosurface, int width, int height, u8* out_rgba);

int run_iosurface_surface_test(FlRenderBackend* backend, FlArena* ir_arena) {
    const int w = 64;
    const int h = 64;
    FlSurface* surface = backend->create_surface(backend->user_data, w, h);

    // Latch the surface into IOSurface mode up front (the host's setup step), then render. A first accessor call
    // rebuilds the target, so anything drawn before it would be discarded.
    GpuSurfaceIOSurface io = gpu_renderer_surface_iosurface(surface);
    CHECK(io.handle != nullptr, "iosurface: the surface exposes a backend-owned IOSurface backing");
    CHECK(io.width == w && io.height == h, "iosurface: the handle carries the surface dims");

    GpuSurfaceTexture tex = gpu_renderer_surface_texture(surface);
    CHECK(tex.handle != 0,
          "iosurface: the primary MTLTexture accessor still returns the (now IOSurface-backed) target");
    CHECK(tex.width == w && tex.height == h, "iosurface: the primary accessor reports the same dims");

    // The primary accessor's two invalidation events (gpu_renderer.h) are the resolution change exercised further
    // down and this surface's first IOSurface call, which rebuilds the target in place. Only the second half is
    // assertable by handle: a repeat IOSurface call re-latches nothing, so both handles must survive it unchanged.
    // (The first call's invalidation is not observable here: Metal readily hands the rebuilt target the address the
    // released one just vacated, so a stale handle can compare equal to a live one.)
    CHECK(gpu_renderer_surface_iosurface(surface).handle == io.handle,
          "iosurface: a repeat accessor call returns the same backing (no re-upgrade)");
    CHECK(gpu_renderer_surface_texture(surface).handle == tex.handle,
          "iosurface: a repeat IOSurface call does not invalidate the primary handle");

    FlRenderFrame frame = test_frame();
    frame.commands = nullptr;
    frame.bytes = 0;
    frame.background.clear = (FlColor) { CLEAR_SCREEN_R, CLEAR_SCREEN_G, CLEAR_SCREEN_B, 255 };
    render_seam_render(backend, surface, &frame);

    FlPixelReadback prim;
    CHECK(render_seam_read_pixels(backend, surface, &prim), "iosurface: primary read_pixels failed");
    CHECK(prim.width == (u32)w && prim.height == (u32)h && prim.stride == (u32)w * 4u,
          "iosurface: primary readback is tight 64x64 RGBA8");

    u8* probe = arena_alloc_array(ir_arena, u8, (size_t)w * (size_t)h * 4u);
    CHECK(gpu_test_iosurface_readback(io.handle, w, h, probe),
          "iosurface: opening the IOSurface on a fresh device and reading it back succeeds");

    const FlColor clear = { CLEAR_SCREEN_R, CLEAR_SCREEN_G, CLEAR_SCREEN_B, 255 };
    CHECK(pixel_color(&probe[0], clear, 0.0f), "iosurface: the cross-context readback is the exact clear colour");
    CHECK(pixel_color(&probe[((size_t)(h - 1) * w + (w - 1)) * 4u], clear, 0.0f),
          "iosurface: the bottom-right cross-context pixel is the exact clear colour too");
    CHECK(memcmp(probe, prim.pixels, (size_t)w * (size_t)h * 4u) == 0,
          "iosurface: the cross-context readback matches the primary accessor byte-for-byte");

    // change_resolution keeps the surface IOSurface-backed: a re-fetch reports the new dims and stays a valid handle.
    // 96x48 is deliberate: its tight row (384 B) is not 256-aligned, so unlike the 64x64 case (256 B, already
    // aligned) the readback's row-padding-strip arithmetic does work here.
    const int w2 = 96;
    const int h2 = 48;
    render_seam_change_resolution(backend, surface, w2, h2);
    GpuSurfaceIOSurface io2 = gpu_renderer_surface_iosurface(surface);
    CHECK(io2.handle != nullptr, "iosurface: the handle survives change_resolution (still IOSurface-backed)");
    CHECK(io2.width == w2 && io2.height == h2, "iosurface: the re-fetched handle reports the resized dims");

    render_seam_render(backend, surface, &frame);
    FlPixelReadback prim2;
    CHECK(render_seam_read_pixels(backend, surface, &prim2), "iosurface: resized primary read_pixels failed");
    CHECK(prim2.width == (u32)w2 && prim2.height == (u32)h2 && prim2.stride == (u32)w2 * 4u,
          "iosurface: resized primary readback is tight 96x48 RGBA8");
    u8* probe2 = arena_alloc_array(ir_arena, u8, (size_t)w2 * (size_t)h2 * 4u);
    CHECK(gpu_test_iosurface_readback(io2.handle, w2, h2, probe2),
          "iosurface: the resized IOSurface opens and reads back on a fresh device");
    CHECK(pixel_color(&probe2[0], clear, 0.0f), "iosurface: the resized cross-context readback is the clear colour");
    CHECK(pixel_color(&probe2[((size_t)(h2 - 1) * w2 + (w2 - 1)) * 4u], clear, 0.0f),
          "iosurface: the resized bottom-right cross-context pixel is the clear colour (exercises the row-padding "
          "strip)");
    CHECK(memcmp(probe2, prim2.pixels, (size_t)w2 * (size_t)h2 * 4u) == 0,
          "iosurface: the resized cross-context readback matches the primary accessor byte-for-byte");

    backend->destroy_surface(backend->user_data, surface);
    printf("gpu_smoke_test: IOSurface surface-texture variant PASS\n");
    return 0;
}

// IOSurface ownership / leak churn. The IOSurface path adds manual CFRetain/CFRelease ownership
// (IOSurfaceCreate +1 in metal_make_iosurface_texture, released in metal_setup_resolution's resize branch and in
// metal_destroy_surface) that a one-shot case cannot catch a forgotten- or double-release in. This drives both
// release paths hard: many create/latch/destroy cycles (the destroy path), then many resizes of one latched surface
// (the resize path), each verified by a cross-context readback so a use-after-free would corrupt the pixels. A final
// retain-count assertion holds an independent reference across a destroy and confirms the backend dropped exactly its
// own references.

// Defined in gpu_iosurface_probe_metal.m: CoreFoundation retain-count probes over the opaque IOSurfaceRef, so this
// plain-C case can assert the backend's CFRelease balance without naming a CF/Metal type.
void gpu_test_cf_retain(void* obj);
long gpu_test_cf_retain_count(void* obj);
void gpu_test_cf_release(void* obj);

int run_iosurface_churn_leak_test(FlRenderBackend* backend, FlArena* ir_arena) {
    const FlColor clear = { CLEAR_SCREEN_R, CLEAR_SCREEN_G, CLEAR_SCREEN_B, 255 };
    FlRenderFrame frame = test_frame();
    frame.commands = nullptr;
    frame.bytes = 0;
    frame.background.clear = clear;

    // Create/latch/render/read/destroy 32 times: exercises the metal_destroy_surface CFRelease path repeatedly.
    for (int i = 0; i < 32; ++i) {
        FlSurface* s = backend->create_surface(backend->user_data, 64, 64);
        GpuSurfaceIOSurface io = gpu_renderer_surface_iosurface(s);
        CHECK(io.handle != nullptr, "iosurface churn: each fresh latched surface exposes an IOSurface backing");
        render_seam_render(backend, s, &frame);
        u8 probe[64 * 64 * 4];
        CHECK(gpu_test_iosurface_readback(io.handle, 64, 64, probe),
              "iosurface churn: cross-context readback succeeds");
        CHECK(pixel_color(&probe[0], clear, 0.0f), "iosurface churn: the cross-context readback stays correct");
        backend->destroy_surface(backend->user_data, s);
    }

    // Resize one latched surface 16 times: exercises the metal_setup_resolution CFRelease-old + recreate path. Each
    // size alternates so change_resolution never no-ops.
    FlSurface* rs = backend->create_surface(backend->user_data, 64, 64);
    (void)gpu_renderer_surface_iosurface(rs); // latch
    for (int i = 0; i < 16; ++i) {
        const int rw = 64 + (i % 4) * 16; // 64/80/96/112, none 256-aligned except 64
        const int rh = 48 + (i % 3) * 16;
        render_seam_change_resolution(backend, rs, rw, rh);
        GpuSurfaceIOSurface io = gpu_renderer_surface_iosurface(rs);
        CHECK(io.handle != nullptr && io.width == rw && io.height == rh,
              "iosurface churn: a resized latched surface re-exposes a valid handle at the new dims");
        render_seam_render(backend, rs, &frame);
        u8* probe = arena_alloc_array(ir_arena, u8, (size_t)rw * (size_t)rh * 4u);
        CHECK(gpu_test_iosurface_readback(io.handle, rw, rh, probe), "iosurface churn: resized cross-context readback");
        CHECK(pixel_color(&probe[0], clear, 0.0f), "iosurface churn: resized readback stays correct across the churn");
    }
    backend->destroy_surface(backend->user_data, rs);

    // Quantitative ownership check: hold an independent reference across a destroy. IOSurfaceCreate gave the backend
    // +1 and the backend's MTLTexture holds its own internal reference; after destroy_surface both must be gone, so
    // only this test's retain remains.
    FlSurface* os = backend->create_surface(backend->user_data, 64, 64);
    GpuSurfaceIOSurface oio = gpu_renderer_surface_iosurface(os);
    CHECK(oio.handle != nullptr, "iosurface churn: ownership-check surface exposes a handle");
    gpu_test_cf_retain(oio.handle); // the test's independent +1
    backend->destroy_surface(backend->user_data, os);
    CHECK(gpu_test_cf_retain_count(oio.handle) == 1,
          "iosurface churn: destroy drops exactly the backend's IOSurface references (only the test's retain remains)");
    gpu_test_cf_release(oio.handle);

    printf("gpu_smoke_test: IOSurface ownership/leak churn PASS\n");
    return 0;
}

#endif // defined(__APPLE__)
