// GPU smoke test: UI core frame and image widget through the GPU backend.

#include "gpu_test_common.h"

#if !defined(__APPLE__)

// Part A: end to end through the UI core, a full-screen red container plus a text run. The text leg guards
// the IR-driven glyph path: ui_set_font_cache binds the cache to the GPU backend, and render() must realize
// the run's glyphs into its atlas itself (no host-side prepare pass).

static bool is_green_text_pixel(const u8* p) {
    return p[3] != 0 && p[1] > 150 && p[1] > p[0] && p[1] > p[2];
}

static bool is_opaque_red_pixel(const u8* p) {
    return p[0] > 150 && p[0] > p[1] && p[0] > p[2] && p[3] == 255;
}

static bool readback_has_pixel(const FlPixelReadback* buf, bool (*pred)(const u8*)) {
    for (u32 y = 0; y < buf->height; ++y) {
        for (u32 x = 0; x < buf->width; ++x) {
            if (pred(pixel_at(buf, (int)x, (int)y))) {
                return true;
            }
        }
    }
    return false;
}

static void set_layout_size(FlUiCtx* ctx, int w, int h) {
    ctx->layout_width = (float)w;
    ctx->layout_height = (float)h;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)w, .height = (float)h });
}

static void emit_ui_frame_text(FlUiCtx* ctx) {
    const FlColor rect_color = ui_color(200, 40, 50, 255);

    ui_update_begin(ctx);
    // Padding keeps the text off the (16,16) probe so the red-background asserts below stay valid; the text sits
    // inside the padded box. 'A' in the embedded test font is a solid square, so it paints a solid green block.
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = rect_color,
                   .direction = LayoutDirection_TopToBottom, .padding = padding_all(40)) {
        ui_text_styled(FL_S("AAA"), .color = ui_color(40, 220, 90, 255));
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

static int read_ui_surface(FlUiCtx* ctx, FlPixelReadback* buf) {
    CHECK(ctx->shared->renderer.read_pixels != nullptr, "backend has no pixel-readback capability");
    CHECK(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, buf), "read_pixels failed");
    return 0;
}

static int check_ui_frame_readback_shape(const FlPixelReadback* buf) {
    CHECK(buf->pixels != nullptr, "readback buffer is null");
    CHECK(buf->width == 1920 && buf->height == 1080, "readback should be the offscreen target size");
    CHECK(buf->stride == buf->width * 4u, "readback stride should be interleaved RGBA8");
    return 0;
}

static int check_ui_frame_pixels(FlUiCtx* ctx) {
    FlPixelReadback buf;
    if (read_ui_surface(ctx, &buf) != 0) {
        return 1;
    }
    if (check_ui_frame_readback_shape(&buf) != 0) {
        return 1;
    }

    const u8* px = pixel_at(&buf, 16, 16);
    printf("gpu_smoke_test: UI frame pixel(16,16) = (%u, %u, %u, %u)\n", px[0], px[1], px[2], px[3]);
    CHECK(px[0] > 100, "UI frame: expected a strong red channel inside the rectangle");
    CHECK(px[0] > px[1] && px[0] > px[2], "UI frame: expected red to dominate green and blue");
    CHECK(px[3] == 255, "UI frame: an opaque rect must carry full coverage alpha");

    // The text run drew iff some pixel is the run's green tint (not the red rect, not transparent).
    CHECK(readback_has_pixel(&buf, is_green_text_pixel),
          "UI frame: text drawn through the ui core renders on GPU (glyphs prepared into the atlas)");
    return 0;
}

int run_ui_frame_test(FlArena* arena, FlRenderBackend* backend) {
    UiShared* shared = ui_shared_create(arena, backend);
    FlUiCtx* ctx = ui_create(arena, shared);
    CHECK(ctx != nullptr, "ui_create returned null");
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    ui_set_surface(ctx, surface);

    fl_vfs_init(arena);

    FlFontCache* font_cache = font_cache_new(arena);
    ui_set_font_cache(ctx, font_cache);

    FlFontId font = font_cache_load_font_memory(font_cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    ui_font_set(font);

    set_layout_size(ctx, 256, 256);
    emit_ui_frame_text(ctx);
    if (check_ui_frame_pixels(ctx) != 0) {
        return 1;
    }

    ui_destroy(ctx);
    backend->destroy_surface(backend->user_data, surface);
    ui_shared_destroy(shared); // shared tier outlives the per-window ctx; exercises image_registry_destroy
    return 0;
}

// ui_image() widget path under the GPU backend, end-to-end: image_load_from_memory -> begin-frame
// pump (the GPU realizes on this render thread and acks the core registry) -> ui_image() inside a real layout
// -> render -> read back, proving the per-backend realization ack opens the emit gate and the image draws.

// 4x4 solid-red RGBA PNG (color type 6); RGBA so the decode yields the 4 channels the GPU atlas takes.
static const unsigned char k_ui_image_png_rgba_red[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x04, 0x08, 0x06, 0x00, 0x00, 0x00, 0xa9, 0xf1, 0x9e, 0x7e, 0x00, 0x00, 0x00, 0x12, 0x49,
    0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x19, 0x33, 0x90, 0x2e, 0x00, 0x00, 0x3c, 0x40,
    0x1f, 0xe1, 0x1a, 0xf3, 0xa5, 0x48, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

#define UI_IMAGE_READY_TIMEOUT_MS 5000

enum { UI_IMAGE_LAYOUT_W = 256, UI_IMAGE_LAYOUT_H = 256 };

typedef struct UiImageProbe {
    FlUiCtx* ctx;
    ImageHandle handle;
    FlPixelReadback buf;
    bool ready;
    bool drew_red;
    int frames;
} UiImageProbe;

static const u8* ui_image_center_pixel(const FlPixelReadback* buf) {
    return pixel_at(buf, UI_IMAGE_LAYOUT_W / 2, UI_IMAGE_LAYOUT_H / 2);
}

static void emit_static_image_frame(FlUiCtx* ctx, ImageHandle handle) {
    set_layout_size(ctx, UI_IMAGE_LAYOUT_W, UI_IMAGE_LAYOUT_H);

    ui_update_begin(ctx); // pumps image_begin_frame: a landed decode realizes into the atlas + acks here
    // White tint passes the color image through unmodified (the tint multiplies the texels; the default
    // {0,0,0,0} would render it black). fill_container grows the image to cover the probe.
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_image(handle, .color = ui_color(255, 255, 255, 255), .fill_container = true);
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

static int probe_static_image_frame(UiImageProbe* probe) {
    emit_static_image_frame(probe->ctx, probe->handle);

    probe->ready = fl_image_is_ready(probe->handle);
    if (!probe->ready) {
        return 0;
    }
    CHECK(probe->ctx->shared->renderer.read_pixels != nullptr,
          "ui_image widget: backend has no pixel-readback capability");
    CHECK(render_seam_read_pixels(&probe->ctx->shared->renderer, probe->ctx->surface, &probe->buf),
          "ui_image widget: read_pixels failed");
    // Content renders into the top-left layout region at scale 1.0, and the image fills that region,
    // so its center samples the image's red texels.
    probe->drew_red = is_opaque_red_pixel(ui_image_center_pixel(&probe->buf));
    return 0;
}

static int poll_static_image(UiImageProbe* probe) {
    for (; probe->frames < UI_IMAGE_READY_TIMEOUT_MS && !probe->drew_red; probe->frames++) {
        if (probe_static_image_frame(probe) != 0) {
            return 1;
        }
        if (!probe->drew_red) {
            sleep_ms(1);
        }
    }
    return 0;
}

static int check_static_image(FlUiCtx* ctx, ImageHandle handle) {
    UiImageProbe probe = { .ctx = ctx, .handle = handle };
    // The decode runs on a job worker; the GPU backend defers realization to the begin-frame pump, which
    // ui_update_begin runs on this (GL) thread. So the frame where the decode lands realizes the image and
    // renders it in one pass.
    if (poll_static_image(&probe) != 0) {
        return 1;
    }

    CHECK(probe.ready, "ui_image widget: image never reported ready under the GPU backend (registry realization ack)");

    ImageResult meta = image_get_data(handle);
    CHECK(meta.width == 4 && meta.height == 4, "ui_image widget: registry reports the decoded 4x4 logical size");

    const u8* px = ui_image_center_pixel(&probe.buf);
    printf("gpu_smoke_test: ui_image widget pixel(%d,%d) = (%u, %u, %u, %u) after %d frame(s)\n", UI_IMAGE_LAYOUT_W / 2,
           UI_IMAGE_LAYOUT_H / 2, px[0], px[1], px[2], px[3], probe.frames);
    CHECK(probe.drew_red, "ui_image widget: the static image drew red through ui_image() under the GPU backend");

    printf("gpu_smoke_test: ui_image() widget path (static image ready + draws on GPU) PASS\n");
    return 0;
}

static void fill_dynamic_image_red(FlDynamicImage* dyn) {
    for (int i = 0; i < dyn->width * dyn->height; i++) {
        u8* px_rgba = dyn->user_buffer + (size_t)i * 4u; // premultiplied opaque red (matches atlas/slot upload)
        px_rgba[0] = 255;
        px_rgba[1] = 0;
        px_rgba[2] = 0;
        px_rgba[3] = 255;
    }
}

static void emit_dynamic_image_frames(FlUiCtx* ctx, FlDynamicImage* dyn) {
    for (int i = 0; i < 3; i++) { // a few ticks let the slot upload + draw settle; no readiness poll needed
        set_layout_size(ctx, UI_IMAGE_LAYOUT_W, UI_IMAGE_LAYOUT_H);
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
            ui_dynamic_image(dyn, .color = ui_color(255, 255, 255, 255), .fill_container = true);
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }
}

static int check_dynamic_image(FlArena* arena, FlUiCtx* ctx, ImageHandle handle) {
    // Dynamic images stay outside the registry: they reference a live caller buffer, carry their
    // own dimensions, and never call image_registry_mark_realized. There is no ImageHandle to key on and no
    // readiness gate, hence no ready-poll loop below.
    FlDynamicImage* dyn = dynamic_image_create(4, 4, arena);
    CHECK(dyn != nullptr, "ui_image widget: dynamic_image_create returned null");
    fill_dynamic_image_red(dyn);
    emit_dynamic_image_frames(ctx, dyn);

    FlPixelReadback dyn_buf = { 0 };
    CHECK(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &dyn_buf),
          "ui_image widget: dynamic read_pixels failed");
    bool dyn_red = is_opaque_red_pixel(ui_image_center_pixel(&dyn_buf));
    CHECK(dyn_red, "ui_image widget: dynamic image draws on GPU with no registry/readiness gate");

    CHECK(fl_image_is_ready(handle), "ui_image widget: the static handle stays realized after dynamic rendering");

    printf("gpu_smoke_test: ui_dynamic_image() widget path (no readiness gate, draws on GPU) PASS\n");
    return 0;
}

int run_ui_image_widget_test(FlArena* arena, FlRenderBackend* backend) {
    UiShared* shared = ui_shared_create(arena, backend);
    FlUiCtx* ctx = ui_create(arena, shared);
    CHECK(ctx != nullptr, "ui_image widget: ui_create returned null");
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    ui_set_surface(ctx, surface);

    fl_vfs_init(arena);

    // ui_update_begin pumps the font cache, so it must be non-null even though this frame draws no text.
    ui_set_font_cache(ctx, font_cache_new(arena));

    // The load/decode system is separate from the registry ui_create already made; it carries the GPU
    // backend's deferred prepare_image, whose realization ack fires in the pump below.
    image_create_system(arena, backend);

    ImageHandle handle
        = image_load_from_memory(k_ui_image_png_rgba_red, sizeof(k_ui_image_png_rgba_red), (FlImageLoadOptions) { 0 });
    CHECK(handle != IMAGE_HANDLE_INVALID, "ui_image widget: image_load_from_memory returned an invalid handle");

    // Readiness is gated on the backend's realization ack, never on decode/CPU-pixel presence.
    CHECK(!fl_image_is_ready(handle), "ui_image widget: handle must not be ready before the backend realizes it");

    if (check_static_image(ctx, handle) != 0) {
        return 1;
    }
    if (check_dynamic_image(arena, ctx, handle) != 0) {
        return 1;
    }

    // Reset the process-global image system so the later fl_app GPU-path test rebinds it to its own backend.
    image_destroy_system();
    ui_destroy(ctx);
    backend->destroy_surface(backend->user_data, surface);
    ui_shared_destroy(shared); // shared tier outlives the per-window ctx; exercises image_registry_destroy
    return 0;
}

#endif // !defined(__APPLE__)
