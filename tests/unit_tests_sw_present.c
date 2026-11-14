// Surface presentation: create_surface -> ui_set_surface -> render -> read_pixels.
//
// A host runs this sequence to put a frame on screen. The cases run it headless against the SW backend, so they
// need no display hardware.

#include "unit_tests_common.h"

// FNV-1a-64 over the pixel bytes of an RGB24 readback, skipping row padding.
//
// Every readback points at the surface's own output buffer, so hash one frame before rendering the next.
static u64 hash_readback(const FlPixelReadback* rb) {
    u64 hash = 0xcbf29ce484222325ULL;
    const u8* base = rb->pixels;
    for (u32 y = 0; y < rb->height; y++) {
        const u8* row = base + (size_t)y * rb->stride;
        for (u32 x = 0; x < rb->width * 3; x++) {
            hash = (hash ^ row[x]) * 0x100000001b3ULL;
        }
    }
    return hash;
}

// Resizes every part of the output geometry the host owns: the surface, the context's layout area and the
// dimensions Clay measures against.
static void set_resolution(FlUiCtx* ctx, int width, int height) {
    render_seam_change_resolution(&s_render.backend, ctx->surface, width, height);
    ui_set_display_resolution(width, height);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)width, .height = (float)height });
}

// Renders one frame that fills the window with fill, then reads the surface back.
static void render_fill_and_read(FlUiCtx* ctx, FlColor fill, FlPixelReadback* out) {
    ui_update_begin(ctx);
    fl_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .color = fill) {}
    ui_update_end(ctx, 1.0f / 60.0f);
    render_seam_read_pixels(&s_render.backend, ctx->surface, out);
}

UTEST(sw_present, present_at_resolutions) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE(ctx != nullptr);

    const struct {
        int width;
        int height;
    } resolutions[] = {
        { 1920, 1080 },
        { 1280, 720 },
        { 720, 576 }, // PAL/SD height
    };

    const FlColor fill = { 40, 80, 160, 255 };

    for_count(i, sizeof_array(resolutions)) {
        const int w = resolutions[i].width;
        const int h = resolutions[i].height;

        set_resolution(ctx, w, h);

        FlPixelReadback rb = { 0 };
        render_fill_and_read(ctx, fill, &rb);

        // The readback is an unpadded RGB24 buffer of the requested size.
        ASSERT_TRUE(rb.pixels != nullptr);
        ASSERT_EQ((u32)w, rb.width);
        ASSERT_EQ((u32)h, rb.height);
        ASSERT_EQ((u32)(w * 3), rb.stride);

        // A non-zero centre pixel shows the frame was drawn into the buffer.
        const u8* centre = (const u8*)rb.pixels + (size_t)(h / 2) * rb.stride + (size_t)(w / 2) * 3;
        ASSERT_TRUE((centre[0] | centre[1] | centre[2]) != 0);

        // The same content gives the same buffer.
        const u64 hash_first = hash_readback(&rb);
        FlPixelReadback rb2 = { 0 };
        render_fill_and_read(ctx, fill, &rb2);
        ASSERT_EQ(hash_first, hash_readback(&rb2));
    }
}
