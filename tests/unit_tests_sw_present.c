// Surface Binding presentation: create_surface -> ui_set_surface -> render -> read_pixels.
//
// This is the sequence a host runs to put a frame on screen, checked headless against the SW
// backend so it needs no display hardware.

#include "unit_tests_common.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FNV-1a-64 over an RGB24 readback, stride-aware.
//
// Two readbacks alias the surface's own output buffer, so the first frame is digested before the second is
// rendered and the digests are compared with each other, never with a stored value.

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Resize every part of the output geometry the host owns: the surface itself, the context's layout
// area (what ui_set_display_resolution sets), and the dimensions Clay measures against.

static void set_resolution(FlUiCtx* ctx, int width, int height) {
    render_seam_change_resolution(&s_render.backend, ctx->surface, width, height);
    ui_set_display_resolution(width, height);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)width, .height = (float)height });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// One frame filling the window with a known colour, then the surface read back.
static void render_fill_and_read(FlUiCtx* ctx, FlColor fill, FlPixelReadback* out) {
    ui_update_begin(ctx);
    fl_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .color = fill) {}
    ui_update_end(ctx, 1.0f / 60.0f);
    render_seam_read_pixels(&s_render.backend, ctx->surface, out);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A surface presents at 1080p, at 720p and at a PAL/SD height: the readback is correctly sized and
// unpadded, carries real ink, and reproduces byte for byte when the same content is drawn again.

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

        // Surface Binding produced a correctly-sized, unpadded RGB24 buffer.
        ASSERT_TRUE(rb.pixels != nullptr);
        ASSERT_EQ((u32)w, rb.width);
        ASSERT_EQ((u32)h, rb.height);
        ASSERT_EQ((u32)(w * 3), rb.stride);

        // Output is non-blank: a centre pixel carries real colour, not a zeroed buffer.
        const u8* centre = (const u8*)rb.pixels + (size_t)(h / 2) * rb.stride + (size_t)(w / 2) * 3;
        ASSERT_TRUE((centre[0] | centre[1] | centre[2]) != 0);

        // Presenting is deterministic: identical content yields an identical buffer.
        const u64 hash_first = hash_readback(&rb);
        FlPixelReadback rb2 = { 0 };
        render_fill_and_read(ctx, fill, &rb2);
        ASSERT_EQ(hash_first, hash_readback(&rb2));
    }
}
