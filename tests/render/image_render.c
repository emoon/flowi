// rendered image blending and scaling tests

#include "render/common.h"

// A dynamic image supplied with stride == 0 means tightly packed, as the GPU backend treats it
// (tex_sub_image_strided: `if (stride == 0) stride = w * bpp;`). The SW backend also renders consecutive source
// rows for it and does not repeat row 0. Both emit_image sites are covered: the alpha-coverage path (1 byte per
// pixel) and the dynamic color path (4 bytes per pixel).

// Draws a tightly packed (stride 0) 16x16 dynamic image 1:1 into (8,8)-(24,24) over a white 32x32 surface.
typedef struct TightImage {
    FlImageMode mode;
    const u8* pixels;
    FlColor tint;
} TightImage;

static bool render_tight_dynamic_image(SwTarget* target, const TightImage* image, FlPixelReadback* out) {
    const FlColor white = { 255, 255, 255, 255 };
    IrRecording rec = ir_recording_begin();
    FlImageSource src = { .kind = FlImageKind_Dynamic };
    src.dynamic.pixels = (u8*)image->pixels;
    src.dynamic.w = 16;
    src.dynamic.h = 16;
    src.dynamic.stride = 0;
    ir_image(rec.ir, src, image->mode, (FlRect) { 8.0f, 8.0f, 16.0f, 16.0f }, (FlRect) { 0.0f, 0.0f, 0.0f, 0.0f },
             image->tint, FlImageFilter_Nearest, nullptr);
    const SwRenderStyle style = { .clear = white, .global_alpha = 255 };
    const bool ok = sw_target_render(target, &rec, &style, out);
    ir_recording_end(&rec);
    return ok;
}

// A 16x16 coverage plane whose top half is uncovered and bottom half fully covered.
static void fill_halves_coverage(u8* coverage) {
    for (int y = 0; y < 16; ++y) {
        memset(coverage + (size_t)y * 16, y < 8 ? 0x00 : 0xFF, 16);
    }
}

// A 16x16 RGBA image whose top half is blue and bottom half red.
static void fill_halves_rgba(u8* rgba) {
    for (int i = 0; i < 16 * 16; ++i) {
        const bool top = i / 16 < 8;
        u8* p = rgba + (size_t)i * 4;
        p[0] = top ? 0 : 255;
        p[1] = 0;
        p[2] = top ? 255 : 0;
        p[3] = 255;
    }
}

UTEST(render, dynamic_image_stride_zero_tight) {
    SwTarget target;
    sw_target_create(&target, 32, 32);
    const FlColor white = { 255, 255, 255, 255 };

    // dst maps the 16x16 source 1:1 into (8,8)-(24,24). A top sample falls in source rows 0..7 and a bottom
    // sample in rows 8..15. The two halves have different content, so a read that repeats row 0 shows.
    const int sample_x = 16, top_y = 11, bot_y = 20;

    // Alpha-coverage path (1 byte per pixel): the top half is uncovered (0x00) and the bottom half fully covered
    // (0xFF). With an opaque black tint over white, uncovered stays white and covered darkens toward black.
    {
        u8 coverage[16 * 16];
        fill_halves_coverage(coverage);
        FlPixelReadback buf;
        ASSERT_TRUE_MSG(render_tight_dynamic_image(
                            &target, &(TightImage) { FlImageMode_AlphaCoverage, coverage, { 0, 0, 0, 255 } }, &buf),
                        "read_pixels failed");
        const u8* top = readback_rgb_at(&buf, sample_x, top_y);
        const u8* bot = readback_rgb_at(&buf, sample_x, bot_y);

        // A read with a literal stride of 0 would take source row 0 (uncovered) for every row and leave the
        // bottom white too.
        ASSERT_TRUE_MSG(top[0] > 200, "alpha-coverage top half (source rows 0..7) must stay ~white (uncovered)");
        ASSERT_TRUE_MSG(bot[0] < 80, "alpha-coverage bottom half must darken — stride 0 wrongly read as literal 0");
    }

    // Dynamic color path (4 bytes per pixel, RGBA): top half blue, bottom half red. A white tint changes
    // nothing.
    {
        u8 rgba[16 * 16 * 4];
        fill_halves_rgba(rgba);
        FlPixelReadback buf;
        ASSERT_TRUE_MSG(render_tight_dynamic_image(&target, &(TightImage) { FlImageMode_Color, rgba, white }, &buf),
                        "read_pixels failed");
        const u8* top = readback_rgb_at(&buf, sample_x, top_y);
        const u8* bot = readback_rgb_at(&buf, sample_x, bot_y);

        // A read with a literal stride of 0 would take source row 0 (blue) for every destination row.
        ASSERT_TRUE_MSG(top[2] > 200 && top[0] < 80, "color top half (source rows 0..7) must be blue");
        ASSERT_TRUE_MSG(bot[0] > 200 && bot[2] < 80,
                        "color bottom half must be red — stride 0 wrongly read as literal 0");
    }

    sw_target_destroy(&target);
}

UTEST(render, widget_dynamic_image_keeps_alpha_only_mode) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(1.0f, 1.0f);

    FlDynamicImage* alpha = fl_dynamic_image_create_alpha(4, 4, (FlArena*)s_arena);
    ASSERT_TRUE_MSG(alpha != nullptr, "alpha image creation failed");

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        fl_dynamic_image(alpha, (FlImageOpts) { .color = ui_color(255, 255, 255, 255) });
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_TRUE_MSG(s_render.image_found, "the dynamic image must reach the frame");
    ASSERT_EQ_MSG((int)s_render.submitted_image.mode, (int)FlImageMode_AlphaCoverage,
                  "an alpha-only image must be drawn as alpha coverage");
}

// Error paths of the public mounted loader: a missing path, the invalid handle, an absent handle and a null mount.
static void check_mounted_load_errors(int* utest_result, FlVfsMount* mount) {
    FlImageHandle failed = fl_image_load_mount(mount, FL_S("missing.png"), (FlImageLoadOptions) { 0 });
    ASSERT_NE_MSG(failed, FL_IMAGE_HANDLE_INVALID, "a valid asynchronous request returns a pollable handle");
    ASSERT_EQ_MSG(wait_public_image_terminal(failed), FlImageStatus_Error, "missing path should resolve to error");
    ASSERT_EQ_MSG(fl_image_status(failed), FlImageStatus_Error, "error status should remain terminal");

    const struct {
        FlImageHandle handle;
        const char* msg;
    } absent[] = {
        { FL_IMAGE_HANDLE_INVALID, "invalid handle should report error" },
        { UINT32_MAX - 1, "an arbitrary absent handle should report error, not pending" },
    };
    for (size_t i = 0; i < sizeof_array(absent); i++) {
        ASSERT_EQ_MSG(fl_image_status(absent[i].handle), FlImageStatus_Error, absent[i].msg);
    }
    ASSERT_EQ_MSG(fl_image_load_mount(nullptr, FL_S(STUB_VFS_FILE), (FlImageLoadOptions) { 0 }),
                  FL_IMAGE_HANDLE_INVALID, "null mount should fail synchronously");
    image_free((ImageHandle)failed);
}

UTEST(render, public_mounted_image_load_and_status) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    vfs_test_register_driver(&s_stub_vfs_plugin);
    s_stub_vfs_override_data = k_png_red;
    s_stub_vfs_override_size = sizeof(k_png_red);

    FlVfsMountResult first_mount = vfs_mount(FL_S("/stub-image-a"));
    FlVfsMountResult second_mount = vfs_mount(FL_S("/stub-image-b"));
    ASSERT_EQ(first_mount.status, FlVfsMountErrorStatus_Success);
    ASSERT_EQ(second_mount.status, FlVfsMountErrorStatus_Success);
    vfs_wait_all();

    FlImageLoadOptions resize_options = { .target_width = 2, .target_height = 2, .preserve_aspect_ratio = true };
    FlImageHandle direct = fl_image_load_mount(first_mount.mount, FL_S(STUB_VFS_FILE), resize_options);
    ASSERT_NE_MSG(direct, FL_IMAGE_HANDLE_INVALID, "direct mounted load should start");
    // Whether the load has already completed is a race with the job system, so the new handle is only required
    // to be pollable and not already failed.
    FlImageStatus started = fl_image_status(direct);
    ASSERT_TRUE_MSG(started == FlImageStatus_Pending || started == FlImageStatus_Ready,
                    "new asynchronous load should be pending or already ready");

    FlImageHandle dedup = fl_image_load_mount(first_mount.mount, FL_S(STUB_VFS_FILE), resize_options);
    ASSERT_EQ_MSG(dedup, direct, "same mount/path/options should deduplicate");

    FlImageHandle composed = fl_image_load_mount(second_mount.mount, FL_S(STUB_VFS_FILE), resize_options);
    ASSERT_NE_MSG(composed, FL_IMAGE_HANDLE_INVALID, "composed mounted load should start");
    ASSERT_NE_MSG(composed, direct, "the same relative path on different mounts must not deduplicate");

    ASSERT_EQ_MSG(wait_public_image_terminal(direct), FlImageStatus_Ready, "direct mounted load should realize");
    ASSERT_EQ_MSG(wait_public_image_terminal(composed), FlImageStatus_Ready, "composed mounted load should realize");
    ASSERT_EQ_MSG(fl_image_status(direct), FlImageStatus_Ready, "ready status should remain terminal");
    ImageResult resized = image_get_data((ImageHandle)direct);
    ASSERT_EQ_MSG(resized.width, 2, "target_width option should reach the decoder");
    ASSERT_EQ_MSG(resized.height, 2, "target_height option should reach the decoder");

    check_mounted_load_errors(utest_result, first_mount.mount);

    image_free((ImageHandle)direct);
    image_free((ImageHandle)composed);
    fl_vfs_mount_close(first_mount.mount);
    fl_vfs_mount_close(second_mount.mount);
    s_stub_vfs_override_data = nullptr;
    s_stub_vfs_override_size = 0;
}

// The SW realized-pixel pool is owned by the software backend, and the core readiness registry does not see what
// is resident in it. A realized image can drop out of the pool and be realized again later without the core
// ever seeing a state that is ready but not resident.
//
// The headless harness has no VFS mount, so it cannot run the real async reload. The test hooks change only the
// pool residency.

UTEST(render, image_pool_evict_self_heals) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ImageHandle handle = image_load_from_memory(k_png_red, sizeof(k_png_red), (FlImageLoadOptions) { 0 });
    ASSERT_TRUE_MSG(handle != IMAGE_HANDLE_INVALID, "image_load_from_memory returned invalid handle");
    ASSERT_TRUE_MSG(wait_image_ready(handle), "image never realized into the SW pool");

    ASSERT_TRUE_MSG(image_cache_get_data(handle) != nullptr, "pixels should be resident after realize");
    ASSERT_TRUE_MSG(fl_image_is_ready(handle), "core registry should report ready after realize");
    const ImageStatus status = image_get_status(image_get_data(handle));
    ASSERT_EQ_MSG((int)status, (int)ImageStatus_Ready,
                  "an acknowledged memory load must report ready, not pending or failed");

    // Evict the pixels from the pool but keep the entry, so the test can make it resident again.
    void* entry = image_cache_test_evict_keep(handle);
    ASSERT_TRUE_MSG(entry != nullptr, "expected a resident entry to evict");

    ASSERT_TRUE_MSG(image_cache_get_data(handle) == nullptr, "pixels should not be resident after eviction");
    ASSERT_TRUE_MSG(fl_image_is_ready(handle), "core readiness must survive a SW pool eviction");

    // The miss is repaired: the image becomes resident again, as it would when the reload completes.
    ASSERT_TRUE_MSG(image_cache_test_restore(handle, entry), "restore failed");
    ASSERT_TRUE_MSG(image_cache_get_data(handle) != nullptr, "pixels should be resident again after reload");
    ASSERT_TRUE_MSG(fl_image_is_ready(handle), "core readiness unchanged across the whole cycle");

    image_free(handle);
}

// A memory load that is realized and out of the active set when its load state is resolved reads as ready and
// never as an error.

UTEST(render, image_status_realized_after_unready_poll) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const ImageHandle handle = (ImageHandle)0x80ffe001u;
    image_registry_mark_realized(handle, 4, 4);

    ImageResult result = image_resolve_unrealized(handle);
    ASSERT_EQ_MSG((int)image_get_status(result), (int)ImageStatus_Ready,
                  "a handle realized after the first readiness check must not report error");
    ASSERT_EQ(result.width, 4);

    image_registry_remove(handle);
}

// A failed in-memory decode has no VFS handle to carry an error status, so the failure is recorded against the
// request id (memory_load_job) and the poll reports an error. Without the record it would stay pending forever.

UTEST(render, image_load_from_memory_bad_data_reports_error) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // Not a valid header for any supported format, so the decode job fails at once.
    static const u8 k_garbage[]
        = { 0xde, 0xad, 0xbe, 0xef, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b };
    ImageHandle handle = image_load_from_memory(k_garbage, sizeof(k_garbage), (FlImageLoadOptions) { 0 });
    ASSERT_TRUE_MSG(handle != IMAGE_HANDLE_INVALID, "image_load_from_memory returned invalid handle");

    // A bounded poll: the load leaves pending within the window, and only to the error state.
    ImageStatus status = ImageStatus_Pending;
    for (int i = 0; i < 5000 && status == ImageStatus_Pending; ++i) {
        status = image_get_status(image_get_data(handle));
        if (status == ImageStatus_Pending) {
            sleep_ms(1);
        }
    }
    ASSERT_EQ_MSG((int)status, (int)ImageStatus_Error,
                  "corrupt in-memory load must resolve to error, not pend forever");
    ASSERT_FALSE_MSG(fl_image_is_ready(handle), "corrupt in-memory load must never report ready");

    // Later polls stay in the error state. The record lasts until the handle is freed.
    ASSERT_EQ_MSG((int)image_get_status(image_get_data(handle)), (int)ImageStatus_Error,
                  "error status must be stable across polls");

    image_free(handle);
}

// The image cache's eviction and free paths run on a job worker thread, and the render thread may still be
// reading pixels through an ImageData* it got from image_cache_get_data earlier in the frame. So frees go on a
// pending list, which image_cache_update drains. That runs at the top of render() on the render thread, before
// any of the frame's get_data calls.

UTEST(render, image_cache_defer_free_drains_at_frame_boundary) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ImageCache* cache = image_cache_get_sys();
    ASSERT_TRUE_MSG(cache != nullptr, "image cache singleton not initialized");

    // Drain what earlier render cases left pending, so the count deltas below are exact.
    image_cache_update(cache, 1);
    ASSERT_EQ_MSG((int)image_cache_test_pending_free_count(), 0, "pending free list should start empty");

    ImageHandle handle = image_load_from_memory(k_png_red, sizeof(k_png_red), (FlImageLoadOptions) { 0 });
    ASSERT_TRUE_MSG(handle != IMAGE_HANDLE_INVALID, "image_load_from_memory returned invalid handle");
    ASSERT_TRUE_MSG(wait_image_ready(handle), "image never realized into the SW pool");

    ImageData* data = image_cache_get_data(handle);
    ASSERT_TRUE_MSG(data != nullptr, "pixels should be resident after realize");
    ASSERT_TRUE_MSG(data->data != nullptr, "resident entry carries pixel data");

    // A byte the render thread would read. The free below does not return this block to TLSF yet.
    u8* pixels = (u8*)data->data;
    u8 first_byte = pixels[0];

    // free_handle is one of the two free sites on the job thread. It queues the block.
    image_cache_free_handle(handle);
    ASSERT_TRUE_MSG(image_cache_test_pending_free_count() > 0, "free_handle should defer, not free immediately");
    ASSERT_EQ_MSG((int)pixels[0], (int)first_byte, "deferred block must stay intact until the frame-boundary drain");

    // Frame boundary on the render thread: image_cache_update drains the queue and returns the block to TLSF.
    image_cache_update(cache, 2);
    ASSERT_EQ_MSG((int)image_cache_test_pending_free_count(), 0, "image_cache_update must drain the pending list");

    // The reclaimed space is usable again: a new load of the same size is realized into the pool.
    ImageHandle reload = image_load_from_memory(k_png_red, sizeof(k_png_red), (FlImageLoadOptions) { 0 });
    ASSERT_TRUE_MSG(reload != IMAGE_HANDLE_INVALID, "reload after drain returned invalid handle");
    ASSERT_TRUE_MSG(wait_image_ready(reload), "same-size alloc should succeed after the drain reclaims TLSF space");
    ASSERT_TRUE_MSG(image_cache_get_data(reload) != nullptr, "reloaded pixels should be resident");

    image_free(reload);
    image_free(handle);
}

// painter_image uses its w and h parameters as the size to scale the image to. A 4x4 image drawn at a 200x200
// logical size is checked by pixels inside and outside the requested rect. A non-positive w or h falls back to
// native size, which render.primitives_and_background_blend covers.

UTEST(render, image_scaled) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // ui_scale 1 so painter coordinates map 1:1 to the offscreen buffer, with a fixed clear color and no
    // background.
    render_set_layout(ctx, 1920.0f, 1080.0f);
    ui_background_set(IMAGE_HANDLE_INVALID);
    const u8 clear_r = 12, clear_g = 12, clear_b = 14;
    ui_background_set_clear_color(clear_r, clear_g, clear_b);

    // k_png_red is a 4x4 RGB PNG, drawn here at a 200x200 logical size.
    ImageHandle fg_image = image_load_from_memory(k_png_red, sizeof(k_png_red), (FlImageLoadOptions) { 0 });
    ASSERT_TRUE_MSG(fg_image != IMAGE_HANDLE_INVALID, "image load returned invalid handle");
    ASSERT_TRUE_MSG(wait_image_ready(fg_image), "foreground image did not decode in time");

    const float draw_x = 100.0f, draw_y = 100.0f, draw_w = 200.0f, draw_h = 200.0f;

    ui_update_begin(ctx);
    // A full-window container at origin (0,0), so element-local painter coords equal absolute pixels.
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        Painter* p = ctx->painter;
        ASSERT_TRUE_MSG(painter_image(p, (FlRect) { .x = draw_x, .y = draw_y, .w = draw_w, .h = draw_h },
                                      (FlImageHandle)fg_image, (FlColor) { 255, 255, 255, 255 }),
                        "painter_image: foreground not ready");
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    read_frame_1080p(utest_result, ctx, &buf);
    RETURN_IF_FAILED();

    // The center of the requested 200x200 rect would have the clear color if painter_image had drawn at the 4x4
    // native size.
    const u8* center
        = buf.pixels + (size_t)(draw_y + draw_h * 0.5f) * buf.stride + (size_t)(draw_x + draw_w * 0.5f) * 3u;
    ASSERT_TRUE_MSG(!(center[0] == clear_r && center[1] == clear_g && center[2] == clear_b),
                    "center of requested rect equals clear color — painter_image ignored w/h");
    // A pixel well outside the requested rect keeps the clear color.
    const u8* outside
        = buf.pixels + (size_t)(draw_y + draw_h * 0.5f) * buf.stride + (size_t)(draw_x + draw_w + 20) * 3u;
    ASSERT_TRUE_MSG(outside[0] == clear_r && outside[1] == clear_g && outside[2] == clear_b,
                    "pixel outside requested rect is not clear color");

    dump_frame("image_scaled", &buf);
}

// Scaled-image sampling stays correct past a downscale of about 4.4x.
//
// The SW scaled sampler precomputes an 8-pixel shuffle over a fixed 32-byte source window. Once the downscale
// ratio puts the source columns of the 8 destination pixels more than 31 bytes apart (roughly above 4.4x), the
// window cannot cover them, and those commands go through a scalar gather that takes the exact nearest neighbor
// per pixel.
//
// The source is an opaque XRGB horizontal ramp (column c has value c*3, every row identical). XRGB drops the X
// byte, so it converts to a 3-plane RGB layout with blue last, and identical rows keep the vertical scale from
// affecting the horizontal check. It is drawn at 8x downscale (scalar gather) and 2x (the fast windowed path),
// and each destination pixel is checked against the reference sample floor(dst_x * x_step).

// Walks the interior of the middle destination row, skipping the boundary pixels the fractional AABB may cover only
// partly. Destination pixel i (0-based from the draw's left edge) maps to source column floor(i * x_step) under
// nearest-neighbor sampling.
static void check_ramp_row(int* utest_result, const FlPixelReadback* buf, const FlRect* draw, int src_w) {
    const int tol = 2; // slack for compositing and the Q7 round trip
    const int row = (int)(draw->y + draw->h * 0.5f);
    const int dw = (int)draw->w;
    const float x_step = (float)src_w / (float)dw;
    int first = -1, last = -1;
    for (int i = 1; i < dw - 1; i++) {
        const int r = readback_rgb_at(buf, (int)draw->x + i, row)[0];
        const int ref = min((int)floorf((float)i * x_step), src_w - 1) * 3;
        ASSERT_TRUE_MSG(r >= ref - tol && r <= ref + tol,
                        "sampled pixel does not match the reference nearest-neighbor source column");
        first = (i == 1) ? r : first;
        last = r;
    }
    ASSERT_TRUE_MSG(last - first > 100, "downscaled ramp did not span the expected range");
}

// An opaque XRGB horizontal ramp: column c has value c*3 in every row, and the byte order is R,G,B,X.
static void fill_xrgb_ramp(u8* pixels, int w, int h) {
    for (int y = 0; y < h; y++) {
        for (int c = 0; c < w; c++) {
            u8* px = pixels + ((size_t)y * (size_t)w + (size_t)c) * 4;
            px[0] = px[1] = px[2] = (u8)(c * 3);
            px[3] = 0; // X byte, dropped by the conversion from XRGB to planar
        }
    }
}

UTEST(render, image_downscale_gather) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout(ctx, 1920.0f, 1080.0f);
    ui_background_set(IMAGE_HANDLE_INVALID);
    const u8 clear_r = 3, clear_g = 3, clear_b = 3;
    ui_background_set_clear_color(clear_r, clear_g, clear_b);

    // XRGB is opaque (no alpha plane, so 3-plane RGB), and the scaled draw composites the exact source color.
    // The byte order is R,G,B,X.
    enum { SRC_W = 80, SRC_H = 8 };
    u8 ramp[SRC_W * SRC_H * 4];
    fill_xrgb_ramp(ramp, SRC_W, SRC_H);
    FlImageHandle ramp_img = fl_image_create_from_pixels(ramp, SRC_W, SRC_H, FlImageFormat_XRGB);
    ASSERT_NE_MSG(ramp_img, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "ramp image create returned invalid handle");
    ASSERT_TRUE_MSG(fl_image_is_ready(ramp_img), "ramp image must be ready immediately");

    // The 8x downscale (80 -> 10) takes the scalar gather path and the 2x downscale (80 -> 40) the fast path.
    const float x8 = 100.0f, y8 = 100.0f, w8 = 10.0f, h8 = 8.0f;
    const float x2 = 100.0f, y2 = 300.0f, w2 = 40.0f, h2 = 8.0f;

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        Painter* p = ctx->painter;
        ASSERT_TRUE_MSG(
            painter_image(p, (FlRect) { .x = x8, .y = y8, .w = w8, .h = h8 }, ramp_img, ui_color(255, 255, 255, 255)),
            "painter_image: 8x ramp not ready");
        ASSERT_TRUE_MSG(
            painter_image(p, (FlRect) { .x = x2, .y = y2, .w = w2, .h = h2 }, ramp_img, ui_color(255, 255, 255, 255)),
            "painter_image: 2x ramp not ready");
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    read_frame(utest_result, ctx, &buf);
    RETURN_IF_FAILED();

    check_ramp_row(utest_result, &buf, &(FlRect) { x8, y8, w8, h8 }, SRC_W);
    check_ramp_row(utest_result, &buf, &(FlRect) { x2, y2, w2, h2 }, SRC_W);
}

// The scaled sampler's fixed 32-byte window stays in bounds.
//
// The scaled sampler (sample_plane_8 in sr_render_image_scale.c) does two unaligned 16-byte loads per plane row,
// at load_offset and load_offset + 16, which make a 32-byte window. Near the bottom-right of the last (blue)
// plane that window reaches up to about 30 bytes past the logical 3-plane layout, so sr_convert_to_planar pads
// the allocation by SR_SCALE_PLANE_LOAD_GUARD bytes.
//
// The plane geometry and the guard come from the real sr_convert_to_planar and the shared constant, so the test
// follows production. The worst-case window is replayed twice: on the real converted buffer, and on a replica
// that malloc sizes exactly. Only the replica lets ASan catch an over-read, because the production planar buffer
// is arena-allocated and arena sub-allocations have no redzone of their own.

UTEST(render, image_scale_plane_over_read_guard) {
    // width and height give a padded 3-plane layout that is not trivial. keep_alpha=false drops the X byte, which
    // leaves blue as the last plane.
    enum { SRC_W = 39, SRC_H = 7 };
    u8 src_pixels[SRC_W * SRC_H * 4];
    memset(src_pixels, 0x5a, sizeof src_pixels);
    ImageData src = {
        .data = src_pixels,
        .width = SRC_W,
        .height = SRC_H,
        .padded_width = SRC_W,
        .padded_height = SRC_H,
        .stride = SRC_W * 4,
        .channels = 4,
        .format = FlImageFormat_XRGB,
    };

    FlArena* arena = arena_new();
    ASSERT_TRUE_MSG(arena != nullptr, "arena_new failed");
    ImageData* planar = sr_convert_to_planar(&src, false, arena);
    ASSERT_TRUE_MSG(planar != nullptr, "sr_convert_to_planar returned null");
    ASSERT_EQ_MSG(planar->channels, 3, "XRGB (keep_alpha=false) must convert to a 3-plane blue-last layout");

    const int plane_size = planar->padded_width * planar->padded_height;
    const int total_size = plane_size * planar->channels; // logical 3-plane size, before the guard

    // The worst case is the last plane (blue), last row, rightmost load offset. load_offset is the clamped source
    // column, whose maximum is width - 1 == padded_width - 2 (padded_width = width + 1).
    const size_t load_offset = (size_t)(planar->padded_width - 2);
    const size_t row_offset = (size_t)(planar->channels - 1) * (size_t)plane_size
                              + (size_t)(planar->padded_height - 1) * (size_t)planar->padded_width;
    const size_t highest = row_offset + load_offset + 31; // highest byte offset the 32-byte window addresses

    ASSERT_TRUE_MSG(highest >= (size_t)total_size,
                    "guard is load-bearing: the window reaches past the unguarded 3-plane size");
    ASSERT_TRUE_MSG(highest < (size_t)total_size + (size_t)SR_SCALE_PLANE_LOAD_GUARD,
                    "guard is sufficient: the window stays within the guarded allocation");

    u8 sink[32];

    // Replay the sampler's two loads on the real converted buffer, which has the guard.
    const u8* real_row = planar->data + row_offset;
    u8x16_store_unaligned(u8x16_load_unaligned(real_row, load_offset), sink, 0);
    u8x16_store_unaligned(u8x16_load_unaligned(real_row, load_offset + 16), sink, 16);

    // Replay the same window on the exactly sized malloc replica, where a malloc redzone follows the guarded
    // allocation and ASan can check it.
    u8* replica = (u8*)malloc((size_t)total_size + (size_t)SR_SCALE_PLANE_LOAD_GUARD);
    ASSERT_TRUE_MSG(replica != nullptr, "planar replica allocation failed");
    memset(replica, 0, (size_t)total_size + (size_t)SR_SCALE_PLANE_LOAD_GUARD);
    const u8* rep_row = replica + row_offset;
    u8x16_store_unaligned(u8x16_load_unaligned(rep_row, load_offset), sink, 0);
    u8x16_store_unaligned(u8x16_load_unaligned(rep_row, load_offset + 16), sink, 16);
    ASSERT_TRUE_MSG(sink[0] == 0 && sink[31] == 0, "guarded window read cleared bytes");

    free(replica);
    fl_arena_destroy(arena);
}

// An image destination of zero width is dropped and not rasterized. The scaled path precomputes a shuffle table
// sized to the integer destination width. At dst_width == 0 the padding-fill loop in calculate_shuffle_tables
// would index result[dst_width - 1], and as a size_t subscript -1 becomes a huge out-of-bounds index. The painter
// clamps degenerate sizes, so the case uses the raw IR path (Clay images, direct ir_image), which can carry a
// zero-width dst.

UTEST(render, zero_width_image_dst_is_dropped) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // A ready 2x2 static image, so the color path resolves. The two draws below differ only in the destination
    // width.
    u8 rgb_pixels[12] = { 220, 20, 30, 20, 220, 30, 20, 30, 220, 240, 240, 40 };
    FlImageHandle img = fl_image_create_from_pixels(rgb_pixels, 2, 2, FlImageFormat_RGB);
    ASSERT_NE_MSG(img, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "raw upload returned invalid handle");
    ASSERT_TRUE_MSG(image_cache_get_data((ImageHandle)img) != nullptr, "image pixels should be resident");

    FlImageSource source = { .kind = FlImageKind_Static };
    source.handle = img;
    const FlColor white = { 255, 255, 255, 255 };

    FlArena* ir_arena = arena_new();
    FlArena* commands_arena = arena_new();
    FlArena* state_arena = arena_new();

    // Control: an 8x8 destination for the 2x2 source takes the scaled path and emits one command.
    fl_arena_align(ir_arena, 8);
    const u8* pos_start = (const u8*)fl_arena_current_pos(ir_arena);
    ir_image(ir_arena, source, FlImageMode_Color, (FlRect) { 4.0f, 4.0f, 8.0f, 8.0f }, (FlRect) { 0 }, white,
             FlImageFilter_Linear, nullptr);
    size_t pos_bytes = (size_t)((const u8*)fl_arena_current_pos(ir_arena) - pos_start);
    UiRenderCommandsArray pos = sw_translate(commands_arena, state_arena, pos_start, pos_bytes, 255);
    ASSERT_EQ_MSG(pos.count, 1u, "a positive-width scaled image emits one render command");
    ASSERT_TRUE_MSG((pos.commands[0].mask & RenderMask_TextureScaled) != 0, "8x8 dst for 2x2 src is the scaled path");

    // The same draw with dst.w == 0 is dropped before command setup, so calculate_shuffle_tables never runs for
    // it.
    fl_arena_align(ir_arena, 8);
    const u8* zero_start = (const u8*)fl_arena_current_pos(ir_arena);
    ir_image(ir_arena, source, FlImageMode_Color, (FlRect) { 4.0f, 4.0f, 0.0f, 8.0f }, (FlRect) { 0 }, white,
             FlImageFilter_Linear, nullptr);
    size_t zero_bytes = (size_t)((const u8*)fl_arena_current_pos(ir_arena) - zero_start);
    UiRenderCommandsArray zero = sw_translate(commands_arena, state_arena, zero_start, zero_bytes, 255);
    ASSERT_EQ_MSG(zero.count, 0u, "a zero-width image destination is dropped, not rasterized");

    // A destination narrower than one whole pixel truncates to zero columns and is dropped the same way.
    fl_arena_align(ir_arena, 8);
    const u8* thin_start = (const u8*)fl_arena_current_pos(ir_arena);
    ir_image(ir_arena, source, FlImageMode_Color, (FlRect) { 4.0f, 4.0f, 0.5f, 8.0f }, (FlRect) { 0 }, white,
             FlImageFilter_Linear, nullptr);
    size_t thin_bytes = (size_t)((const u8*)fl_arena_current_pos(ir_arena) - thin_start);
    UiRenderCommandsArray thin = sw_translate(commands_arena, state_arena, thin_start, thin_bytes, 255);
    ASSERT_EQ_MSG(thin.count, 0u, "a sub-pixel-wide image destination is dropped, not rasterized");

    fl_arena_destroy(ir_arena);
    fl_arena_destroy(commands_arena);
    fl_arena_destroy(state_arena);
    image_free((ImageHandle)img);
}

// Draws an image through the SW backend onto a 32x32 surface of the clear color and reads it back. The 8x8 dst
// for the 2x2 sources below always takes the scaled path.

typedef struct ScaledDraw {
    FlImageSource source;
    FlColor tint;
    u8 global_alpha;
    FlColor clear;
} ScaledDraw;

static bool render_image_scaled(SwTarget* target, const ScaledDraw* draw, FlPixelReadback* out) {
    IrRecording rec = ir_recording_begin();
    ir_image(rec.ir, draw->source, FlImageMode_Color, (FlRect) { 8.0f, 8.0f, 8.0f, 8.0f }, (FlRect) { 0 }, draw->tint,
             FlImageFilter_Linear, nullptr);
    const SwRenderStyle style = { .clear = draw->clear, .global_alpha = draw->global_alpha };
    const bool ok = sw_target_render(target, &rec, &style, out);
    ir_recording_end(&rec);
    return ok;
}

static FlImageSource static_source(FlImageHandle img) {
    FlImageSource source = { .kind = FlImageKind_Static };
    source.handle = img;
    return source;
}

static const u8* scaled_probe_px(const FlPixelReadback* buf) {
    return (const u8*)buf->pixels + (size_t)12 * buf->stride + (size_t)12 * 3u; // inside the 8x8 dst at (8,8)
}

// A scaled RGBA image with a translucent alpha plane composites over the background (premultiplied over). It does
// not paint its color opaque.

UTEST(render, scaled_image_blends_source_alpha) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // Straight (0, 0, 255, a=128), which the upload premultiplies to (0, 0, 128, 128).
    u8 rgba[16];
    for (int i = 0; i < 4; ++i) {
        rgba[i * 4 + 0] = 0;
        rgba[i * 4 + 1] = 0;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = 128;
    }
    FlImageHandle img = fl_image_create_from_pixels(rgba, 2, 2, FlImageFormat_RGBA);
    ASSERT_NE_MSG(img, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "raw upload returned invalid handle");

    SwTarget target;
    sw_target_create(&target, 32, 32);
    const ScaledDraw draw = { static_source(img), { 255, 255, 255, 255 }, 255, { 200, 200, 200, 255 } };
    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_image_scaled(&target, &draw, &buf), "read_pixels failed");
    const u8* px = scaled_probe_px(&buf);
    // r = 200 * (1 - 128/255), about 100, and b = 128 + 100, about 228. A draw that ignored alpha would give
    // r = 0, b = 128.
    ASSERT_TRUE_MSG(px[0] > 90 && px[0] < 110, "scaled RGBA must blend its alpha plane over the background");
    ASSERT_TRUE_MSG(px[2] > 218 && px[2] < 238, "scaled RGBA blue must be the premultiplied-over result");

    sw_target_destroy(&target);
    image_free((ImageHandle)img);
}

// A scaled opaque image under a frame fade blends toward the background, for both the 4-plane static upload and
// the 3-plane dynamic buffer.

UTEST(render, scaled_image_honours_global_alpha) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    u8 rgb[12];
    u8 rgba[16];
    for (int i = 0; i < 4; ++i) {
        rgb[i * 3 + 0] = 0;
        rgb[i * 3 + 1] = 0;
        rgb[i * 3 + 2] = 255;
        rgba[i * 4 + 0] = 0;
        rgba[i * 4 + 1] = 0;
        rgba[i * 4 + 2] = 255;
        rgba[i * 4 + 3] = 255;
    }
    FlImageHandle img = fl_image_create_from_pixels(rgb, 2, 2, FlImageFormat_RGB);
    ASSERT_NE_MSG(img, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "raw upload returned invalid handle");
    FlImageSource dynamic = { .kind = FlImageKind_Dynamic };
    dynamic.dynamic.pixels = rgba;
    dynamic.dynamic.w = 2;
    dynamic.dynamic.h = 2;

    const FlImageSource sources[2] = { static_source(img), dynamic };
    for (int i = 0; i < 2; ++i) {
        SwTarget target;
        sw_target_create(&target, 32, 32);
        const ScaledDraw draw = { sources[i], { 255, 255, 255, 255 }, 128, { 200, 200, 200, 255 } };
        FlPixelReadback buf;
        ASSERT_TRUE_MSG(render_image_scaled(&target, &draw, &buf), "read_pixels failed");
        const u8* px = scaled_probe_px(&buf);
        // At a 50% fade r is about 100 and b about 228. A draw that ignored the fade would paint r = 0, b = 255.
        ASSERT_TRUE_MSG(px[0] > 90 && px[0] < 110, "faded scaled image must blend toward the background");
        ASSERT_TRUE_MSG(px[2] > 218 && px[2] < 238, "faded scaled image blue must be the lerp toward the background");
        sw_target_destroy(&target);
    }

    image_free((ImageHandle)img);
}

// A scaled 1-channel image is coverage for the tint: full coverage paints the tint and zero coverage leaves the
// background. Only the one plane is sampled.

UTEST(render, scaled_alpha_image_is_tinted_coverage) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    u8 full[4] = { 255, 255, 255, 255 };
    u8 none[4] = { 0, 0, 0, 0 };
    FlImageHandle img_full = fl_image_create_from_pixels(full, 2, 2, FlImageFormat_Alpha);
    FlImageHandle img_none = fl_image_create_from_pixels(none, 2, 2, FlImageFormat_Alpha);
    ASSERT_NE_MSG(img_full, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "raw upload returned invalid handle");
    ASSERT_NE_MSG(img_none, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "raw upload returned invalid handle");

    const FlColor tint = { 10, 20, 30, 255 };
    const FlColor clear = { 200, 200, 200, 255 };

    SwTarget target;
    sw_target_create(&target, 32, 32);
    FlPixelReadback buf;
    ScaledDraw draw = { static_source(img_full), tint, 255, clear };
    ASSERT_TRUE_MSG(render_image_scaled(&target, &draw, &buf), "read_pixels failed");
    const u8* px = scaled_probe_px(&buf);
    ASSERT_TRUE_MSG(abs(px[0] - 10) <= 3 && abs(px[1] - 20) <= 3 && abs(px[2] - 30) <= 3,
                    "full coverage must paint the tint");
    sw_target_destroy(&target);

    sw_target_create(&target, 32, 32);
    draw.source = static_source(img_none);
    ASSERT_TRUE_MSG(render_image_scaled(&target, &draw, &buf), "read_pixels failed");
    px = scaled_probe_px(&buf);
    ASSERT_TRUE_MSG(abs(px[0] - 200) <= 3 && abs(px[1] - 200) <= 3 && abs(px[2] - 200) <= 3,
                    "zero coverage must leave the background");
    sw_target_destroy(&target);

    image_free((ImageHandle)img_full);
    image_free((ImageHandle)img_none);
}

// Every static image block is allocated with SR_SCALE_PLANE_LOAD_GUARD zeroed bytes past its padded planes. The
// sizes below are exact multiples of the allocator granularity, so an unguarded block would have no slack.

UTEST(render, static_image_blocks_carry_the_sampler_guard) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // 15x15 pads to 16x16 planes of 256 bytes: one plane for Alpha, four (RGB plus opaque alpha) for RGB.
    u8 alpha[15 * 15];
    u8 rgb[15 * 15 * 3];
    memset(alpha, 0x7f, sizeof alpha);
    memset(rgb, 0x7f, sizeof rgb);
    FlImageHandle h_alpha = fl_image_create_from_pixels(alpha, 15, 15, FlImageFormat_Alpha);
    FlImageHandle h_rgb = fl_image_create_from_pixels(rgb, 15, 15, FlImageFormat_RGB);
    ASSERT_NE_MSG(h_alpha, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "raw upload returned invalid handle");
    ASSERT_NE_MSG(h_rgb, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "raw upload returned invalid handle");

    const struct {
        FlImageHandle handle;
        size_t planes_size;
    } cases[2] = { { h_alpha, 256 }, { h_rgb, 1024 } };

    for (int i = 0; i < 2; ++i) {
        ImageData* data = image_cache_get_data((ImageHandle)cases[i].handle);
        ASSERT_TRUE_MSG(data != nullptr, "image pixels should be resident");
        ASSERT_TRUE_MSG(tlsf_block_size(data->data) >= cases[i].planes_size + SR_SCALE_PLANE_LOAD_GUARD,
                        "static image block must extend SR_SCALE_PLANE_LOAD_GUARD past its planes");
        for (int g = 0; g < SR_SCALE_PLANE_LOAD_GUARD; ++g) {
            ASSERT_EQ_MSG(data->data[cases[i].planes_size + (size_t)g], 0, "guard bytes must be zeroed");
        }
    }

    image_free((ImageHandle)h_alpha);
    image_free((ImageHandle)h_rgb);
}

// An opaque pixel premultiplies to itself, and a half-alpha channel rounds to nearest.

UTEST(render, premultiply_keeps_opaque_channels) {
    const u8 in[16] = { 255, 200, 1, 255, 255, 200, 1, 255, 255, 200, 1, 255, 255, 200, 1, 255 };
    u8 out[16];
    u8x16_store_unaligned(sr_premultiply_rgba(u8x16_new(in)), out, 0);
    const char* const opaque_msgs[4] = { "opaque 255 must stay 255", "opaque channel must be unchanged",
                                         "opaque channel must be unchanged", "alpha lane must be preserved" };
    for (int i = 0; i < 4; i++) {
        ASSERT_EQ_MSG(out[i], in[i], opaque_msgs[i]);
    }

    const u8 half[16] = { 255, 100, 0, 128, 255, 100, 0, 128, 255, 100, 0, 128, 255, 100, 0, 128 };
    u8x16_store_unaligned(sr_premultiply_rgba(u8x16_new(half)), out, 0);
    ASSERT_EQ_MSG(out[0], 128, "255 at alpha 128 premultiplies to 128");
    ASSERT_EQ_MSG(out[1], 50, "100 at alpha 128 premultiplies to 50");
}

// The raw upload is synchronous and copies caller memory into a static image the backend owns, so the case
// changes the source buffers before drawing. It covers RGBA, Alpha, RGB and XRGB handles.

enum { FP_CLEAR_R = 5, FP_CLEAR_G = 7, FP_CLEAR_B = 11 };

static void check_from_pixels_probes(int* utest_result, const FlPixelReadback* buf) {
    const PxRange ranges[] = {
        { 100, 100, { 181, 0, 0 }, { 255, 69, 79 }, "RGBA copied pixels did not render the original red texel" },
        { 108,
          100,
          { FP_CLEAR_R, FP_CLEAR_G, FP_CLEAR_B },
          { FP_CLEAR_R, FP_CLEAR_G, FP_CLEAR_B },
          "Alpha zero texel should leave the clear color unchanged" },
        { 109, 100, { 26, 151, 191 }, { 255, 255, 255 }, "Alpha full texel should tint through painter_image color" },
        { 116, 100, { 21, 141, 61 }, { 255, 255, 255 }, "RGB copied pixels did not render the original texel" },
        { 120, 100, { 151, 46, 171 }, { 255, 255, 255 }, "XRGB copied pixels did not render the original texel" },
    };
    check_px_ranges(utest_result, buf, ranges, sizeof_array(ranges));
}

UTEST(render, image_from_pixels) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout(ctx, 1920.0f, 1080.0f);
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(FP_CLEAR_R, FP_CLEAR_G, FP_CLEAR_B);

    u8 rgba_pixels[16] = {
        220, 20, 30, 255, 20, 220, 30, 255, 20, 30, 220, 255, 240, 240, 40, 255,
    };
    u8 alpha_pixels[4] = { 0, 255, 128, 255 };
    u8 rgb_pixels[3] = { 35, 180, 90 };
    u8 xrgb_pixels[4] = { 190, 70, 210, 0 };

    FlImageHandle rgba = fl_image_create_from_pixels(rgba_pixels, 2, 2, FlImageFormat_RGBA);
    FlImageHandle alpha = fl_image_create_from_pixels(alpha_pixels, 2, 2, FlImageFormat_Alpha);
    FlImageHandle rgb = fl_image_create_from_pixels(rgb_pixels, 1, 1, FlImageFormat_RGB);
    FlImageHandle xrgb = fl_image_create_from_pixels(xrgb_pixels, 1, 1, FlImageFormat_XRGB);

    const RawUpload uploads[4] = {
        { rgba, "RGBA raw upload returned invalid handle", "direct RGBA handle must be ready immediately",
          "RGBA pixels should be resident" },
        { alpha, "Alpha raw upload returned invalid handle", "Alpha handle must be ready immediately",
          "Alpha pixels should be resident" },
        { rgb, "RGB raw upload returned invalid handle", "RGB handle must be ready immediately",
          "RGB pixels should be resident" },
        { xrgb, "XRGB raw upload returned invalid handle", "direct XRGB handle must be ready immediately",
          "XRGB pixels should be resident" },
    };
    check_raw_uploads(utest_result, uploads, sizeof_array(uploads));

    memset(rgba_pixels, 0, sizeof rgba_pixels);
    memset(alpha_pixels, 0, sizeof alpha_pixels);
    memset(rgb_pixels, 0, sizeof rgb_pixels);
    memset(xrgb_pixels, 0, sizeof xrgb_pixels);

    const struct {
        float x;
        FlImageHandle handle;
        FlColor tint;
        const char* msg;
    } draws[4] = {
        { 100.0f, rgba, ui_color(255, 255, 255, 255), "painter_image: RGBA raw handle not ready" },
        { 108.0f, alpha, ui_color(40, 200, 250, 255), "painter_image: Alpha raw handle not ready" },
        { 116.0f, rgb, ui_color(255, 255, 255, 255), "painter_image: RGB raw handle not ready" },
        { 120.0f, xrgb, ui_color(255, 255, 255, 255), "painter_image: XRGB raw handle not ready" },
    };

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        for (int i = 0; i < 4; i++) {
            ASSERT_TRUE_MSG(painter_image(ctx->painter, (FlRect) { .x = draws[i].x, .y = 100.0f, .w = 0.0f, .h = 0.0f },
                                          draws[i].handle, draws[i].tint),
                            draws[i].msg);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    read_frame_1080p(utest_result, ctx, &buf);
    RETURN_IF_FAILED();

    check_from_pixels_probes(utest_result, &buf);

    dump_frame("image_from_pixels", &buf);

    image_free((ImageHandle)rgba);
    image_free((ImageHandle)alpha);
    image_free((ImageHandle)rgb);
    image_free((ImageHandle)xrgb);
}

// Uploading again into a live handle keeps the handle's identity, dimensions and format, and treats the caller's
// buffer as create_from_pixels does.

UTEST(render, image_update_pixels) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout(ctx, 1920.0f, 1080.0f);
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(5, 7, 11);

    // Dedicated changes nothing under the software backend. The GPU backend uses it to keep the image off its
    // atlas.
    u8 pixels[4] = { 220, 30, 40, 255 };
    FlImageHandle handle = fl_image_create_dedicated(pixels, 1, 1, FlImageFormat_RGBA);
    ASSERT_NE_MSG(handle, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "dedicated raw upload returned invalid handle");
    ASSERT_TRUE_MSG(fl_image_is_ready(handle), "dedicated handle must be ready immediately");

    u8 rebaked[4] = { 30, 210, 60, 255 };
    ASSERT_TRUE_MSG(fl_image_update_pixels(handle, rebaked), "update_pixels should re-upload into a live handle");
    memset(rebaked, 0, sizeof rebaked); // the caller may free its buffer as soon as the call returns
    ASSERT_TRUE_MSG(fl_image_is_ready(handle), "the handle stays ready across a re-upload");

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ASSERT_TRUE_MSG(painter_image(ctx->painter, (FlRect) { .x = 200.0f, .y = 200.0f, .w = 0.0f, .h = 0.0f }, handle,
                                      ui_color(255, 255, 255, 255)),
                        "painter_image: re-uploaded handle not ready");
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    const u8* texel = readback_rgb_at(&buf, 200, 200);
    ASSERT_TRUE_MSG(texel[1] > 150 && texel[0] < 90 && texel[2] < 110,
                    "the re-uploaded (green) pixels must render, not the original red ones");

    ASSERT_FALSE_MSG(fl_image_update_pixels((FlImageHandle)0x7fffffff, pixels),
                     "update_pixels on a handle never created from pixels must fail");

    image_free((ImageHandle)handle);
}

// The RGBA (4-channel) image blend applies the source alpha exactly once (premultiplied over), as the GPU backend
// does. create_from_pixels premultiplies the RGBA source with the same round-to-nearest /255 rule as the GPU's
// pack_tint_premultiplied, and the SW sampler passes that premultiplied color and its alpha through unchanged.
// The expected value below is that premultiply followed by premultiplied over, which is the GPU backend's
// compositing.
//
// The case uses the bilinear same-size path (process_8_pixels). The nearest path composites 4-channel texels
// through the same premul_over_8 helper, so it is covered as well. The corner-rounding branch of premul_over_8 is
// not covered, because emit_image never sets RenderMask_Corners.

UTEST(render, translucent_image_over) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout(ctx, 1920.0f, 1080.0f);
    ui_background_set(IMAGE_HANDLE_INVALID);
    const u8 clear_r = 60, clear_g = 60, clear_b = 60;
    ui_background_set_clear_color(clear_r, clear_g, clear_b);

    // A uniform 8x8 RGBA image at 50% alpha. create_from_pixels premultiplies it and marks it premultiplied.
    u8 rgba_pixels[8 * 8 * 4];
    for (int i = 0; i < 8 * 8; ++i) {
        rgba_pixels[i * 4 + 0] = 200;
        rgba_pixels[i * 4 + 1] = 80;
        rgba_pixels[i * 4 + 2] = 20;
        rgba_pixels[i * 4 + 3] = 128;
    }
    FlImageHandle img = fl_image_create_from_pixels(rgba_pixels, 8, 8, FlImageFormat_RGBA);
    ASSERT_NE_MSG(img, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "RGBA raw upload returned invalid handle");
    ASSERT_TRUE_MSG(fl_image_is_ready(img), "direct RGBA handle must be ready immediately");

    // Premultiplied over of the premultiplied source on the clear color (global_alpha 255). The premultiply uses
    // the round-to-nearest /255 both backends share, so out = premul(c,a) + bg*(255-a)/255.
    const int a = 128;
    const int premul_r = (200 * a + 127) / 255, premul_g = (80 * a + 127) / 255, premul_b = (20 * a + 127) / 255;
    const int exp_r = premul_r + (clear_r * (255 - a) + 127) / 255; // 130
    const int exp_g = premul_g + (clear_g * (255 - a) + 127) / 255; // 70
    const int exp_b = premul_b + (clear_b * (255 - a) + 127) / 255; // 40

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        Painter* p = ctx->painter;
        ASSERT_TRUE_MSG(painter_image(p, (FlRect) { .x = 200.0f, .y = 200.0f, .w = 0.0f, .h = 0.0f }, img,
                                      ui_color(255, 255, 255, 255)),
                        "painter_image: RGBA handle not ready");
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "render buffer data is null");

    const u8* px = readback_rgb_at(&buf, 204, 204); // interior of the 8x8 image
    ASSERT_TRUE_MSG(abs((int)px[0] - exp_r) <= 2 && abs((int)px[1] - exp_g) <= 2 && abs((int)px[2] - exp_b) <= 2,
                    "translucent RGBA image must composite premultiplied-over (once), not rgb·a²");

    image_free((ImageHandle)img);
}

UTEST(render, image_sub_atlas) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout(ctx, 1920.0f, 1080.0f);
    ui_background_set(IMAGE_HANDLE_INVALID);
    const int clear_r = 3, clear_g = 5, clear_b = 7;
    ui_background_set_clear_color(clear_r, clear_g, clear_b);

    u8 atlas_pixels[8] = {
        0, 255, 255, 0, 128, 255, 0, 128,
    };
    FlImageHandle atlas = fl_image_create_from_pixels(atlas_pixels, 4, 2, FlImageFormat_Alpha);
    ASSERT_NE_MSG(atlas, (FlImageHandle)FL_IMAGE_HANDLE_INVALID, "Alpha atlas upload returned invalid handle");
    ASSERT_TRUE_MSG(fl_image_is_ready(atlas), "alpha atlas handle must be ready immediately");

    memset(atlas_pixels, 0, sizeof atlas_pixels);

    const FlColor left_tint = ui_color(40, 220, 80, 255);
    const FlColor right_tint = ui_color(220, 70, 240, 255);

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ASSERT_TRUE_MSG(fl_painter_image_sub((FlImageSub) {
                            .dx = 100.0f, .dy = 100.0f, .sw = 2.0f, .sh = 2.0f, .image = atlas, .color = left_tint }),
                        "direct image_sub draw failed");
        ASSERT_TRUE_MSG(fl_painter_image_sub((FlImageSub) { .dx = 108.0f,
                                                            .dy = 100.0f,
                                                            .dw = 2.0f,
                                                            .dh = 2.0f,
                                                            .sx = 2.0f,
                                                            .sw = 2.0f,
                                                            .sh = 2.0f,
                                                            .image = atlas,
                                                            .color = right_tint }),
                        "second image_sub draw failed");
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    read_frame_1080p(utest_result, ctx, &buf);
    RETURN_IF_FAILED();

    const PxRange ranges[] = {
        { 100,
          100,
          { clear_r, clear_g, clear_b },
          { clear_r, clear_g, clear_b },
          "left cell alpha-zero texel should leave clear color unchanged" },
        { 101, 100, { 26, 171, 56 }, { 255, 255, 255 }, "left cell full-alpha texel should use the left tint" },
        { 100,
          101,
          { clear_r + 1, 81, clear_b + 1 },
          { 255, 255, 255 },
          "left cell half-alpha texel should blend the left tint" },
        { 108, 100, { 171, 46, 181 }, { 255, 255, 255 }, "right cell full-alpha texel should use the right tint" },
        { 109,
          100,
          { clear_r, clear_g, clear_b },
          { clear_r, clear_g, clear_b },
          "right cell alpha-zero texel should leave clear color unchanged" },
        { 112,
          100,
          { clear_r, clear_g, clear_b },
          { clear_r, clear_g, clear_b },
          "pixel outside atlas-cell draws should remain clear" },
    };
    check_px_ranges(utest_result, &buf, ranges, sizeof_array(ranges));

    dump_frame("image_sub", &buf);

    image_free((ImageHandle)atlas);
}
