// text, border, surface and surface-lifecycle render tests

#include "render/common.h"

UTEST(render, load_default_font_takes_a_size_in_pixels) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const u16 sizes[] = { 0, 1, 4, 8, 12, 16, 20, 28, UINT16_MAX };
    const u16 scales[] = { 2, 1, 1, 1, 2, 2, 3, 4, 8191 };
    for_count(i, sizeof_array(sizes)) {
        EXPECT_EQ(font_cache_default_font_scale(sizes[i]), scales[i]);
    }
    FlFontId font = fl_font_load_default(20);
    ASSERT_NE((int)font, (int)FONT_ID_INVALID);
    EXPECT_EQ(font_cache_get_font_metrics(s_render.font_cache, font, 0.0f).line_height, 24.0f);
}

// Runs a frame through the layout primitives directly (fl_layout_start, fl_layout_start_auto,
// fl_layout_configure, fl_layout_end) and not through the ui_layout_area macro.

UTEST(render, layout_primitives) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const int layout_w = 256;
    const int layout_h = 256;
    ctx->layout_width = (float)layout_w;
    ctx->layout_height = (float)layout_h;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)layout_w, .height = (float)layout_h });

    const FlColor outer_color = ui_color(40, 40, 200, 255); // blue backdrop
    const FlColor child_color = ui_color(40, 200, 60, 255); // green nested child, anchored top-left

    ui_update_begin(ctx);
    fl_layout_start(ID("layout_primitives_outer"));
    fl_layout_configure(
        (FlLayoutConfig) { .sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = outer_color });
    fl_layout_start_auto();
    fl_layout_configure((FlLayoutConfig) { .sizing = { sizing_fixed(120), sizing_fixed(120) }, .color = child_color });
    fl_layout_end();
    fl_layout_end();
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "render buffer data is null");

    // Inside the nested 120x120 child at the top-left, green dominates, so the nested open and close worked.
    const int px = 16, py = 16;
    const u8* pixels = (const u8*)buf.pixels;
    const size_t offset = (size_t)py * (size_t)buf.stride + (size_t)px * 3u;
    const u8 r = pixels[offset + 0];
    const u8 g = pixels[offset + 1];
    const u8 b = pixels[offset + 2];
    ASSERT_TRUE_MSG(g > 100, "expected a strong green channel inside the nested child");
    ASSERT_TRUE_MSG(g > r && g > b, "expected green (the nested child) to dominate");
}

// fl_font_load_default, fl_font_style_register and fl_font_style_set on the built-in default font, which needs no
// system fonts and works headless.

UTEST(render, font_style_registry) {
    FlUiCtx* ctx = render_fixture_get(); // also makes the fixture context current (g_ui_ctx), which these fl_* read
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const FlFontId font = fl_font_load_default(0);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "fl_font_load_default must return a valid font id");

    fl_font_style_register(font, FlFontStyle_Bold);
    fl_font_style_set(FlFontStyle_Bold);
    ASSERT_EQ_MSG(ctx->active_font, font, "fl_font_style_set must make the registered style's font active");
}

// An RGB24 readback of the given size, tightly strided.
static void check_rgb24_size(int* utest_result, const FlPixelReadback* buf, u32 width, u32 height) {
    ASSERT_EQ(width, buf->width);
    ASSERT_EQ(height, buf->height);
    ASSERT_EQ((u32)(width * 3), buf->stride);
}

// One frame with a single styled "Ao" run; font_size 0 selects the load-size fallback.
static void emit_scaled_text_frame(FlUiCtx* ctx, FlLayoutId root, FlFontId font, u16 font_size) {
    ui_update_begin(ctx);
    fl_layout_start(root);
    fl_layout_configure((FlLayoutConfig) { .sizing = { .width = sizing_grow(), .height = sizing_grow() } });
    ui_text_styled(FL_S("Ao"), .font_id = font, .font_size = font_size, .color = ui_color(255, 255, 255, 255));
    fl_layout_end();
    ui_update_end(ctx, 1.0f / 60.0f);
}

// The software backend's change_resolution on a standalone backend, with no UI fixture and no global context. A
// pixel near the far corner of the grown buffer has the newly rendered clear color only if change_resolution
// reallocated the RGB24 target.

UTEST(render, resize) {
    FlArena* arena = arena_new();
    FlRenderBackend backend = create_software_renderer(arena, nullptr, FontRenderMode_Grayscale);
    ASSERT_TRUE_MSG(backend.change_resolution != nullptr, "backend has no change_resolution seam");
    ASSERT_TRUE_MSG(backend.read_pixels != nullptr, "backend has no pixel-readback capability");

    // The output buffer lives on the surface, and change_resolution below resizes it in place.
    FlSurface* surface = backend.create_surface(backend.user_data, 64, 48);

    // An empty command stream still clears the whole buffer to the background clear color, which shows that the
    // target was allocated and rendered at the current resolution.
    const FlColor clear = { .r = 200, .g = 40, .b = 50, .a = 255 };
    const FlRenderFrame frame = {
        .commands = nullptr,
        .bytes = 0,
        .global_alpha = 255,
        .background = { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID, .clear = clear },
    };

    render_seam_render(&backend, surface, &frame);
    FlPixelReadback before;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&backend, surface, &before), "read_pixels failed");
    check_rgb24_size(utest_result, &before, 64, 48);

    render_seam_change_resolution(&backend, surface, 128, 96);
    render_seam_render(&backend, surface, &frame);
    FlPixelReadback after;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&backend, surface, &after), "read_pixels failed");
    check_rgb24_size(utest_result, &after, 128, 96);

    // A pixel near the far corner exists only in the reallocated buffer.
    const u8* pixels = (const u8*)after.pixels;
    const size_t offset = (size_t)90 * (size_t)after.stride + (size_t)120 * 3u;
    const u32 want[3] = { clear.r, clear.g, clear.b };
    for (size_t c = 0; c < 3; c++) {
        ASSERT_EQ((u32)want[c], (u32)pixels[offset + c]);
    }

    backend.destroy_surface(backend.user_data, surface);
    if (backend.destroy != nullptr) {
        backend.destroy(backend.user_data);
    }
    fl_arena_destroy(arena);
}

// A SW surface owns its own arena and the SwSurface struct lives in that arena and not in the renderer's
// perm_arena, so a full create and destroy cycle leaves perm_arena's used bytes where they started.

UTEST(render, surface_cycle_no_perm_arena_growth) {
    FlArena* arena = arena_new();
    FlRenderBackend backend = create_software_renderer(arena, nullptr, FontRenderMode_Grayscale);
    ASSERT_TRUE_MSG(backend.create_surface != nullptr, "backend has no create_surface seam");
    ASSERT_TRUE_MSG(backend.destroy_surface != nullptr, "backend has no destroy_surface seam");

    // One warm-up cycle, so any one-time cost is paid before the baseline is taken.
    backend.destroy_surface(backend.user_data, backend.create_surface(backend.user_data, 64, 64));

    // perm_arena is the arena passed in, and its used bytes are its current write position.
    const char* used_before = (const char*)fl_arena_current_pos(arena);

    const int iterations = 1000;
    for (int i = 0; i < iterations; ++i) {
        FlSurface* surface = backend.create_surface(backend.user_data, 64, 64);
        backend.destroy_surface(backend.user_data, surface);
    }

    const char* used_after = (const char*)fl_arena_current_pos(arena);
    // Growth per iteration would show as about iterations * sizeof(SwSurface).
    ASSERT_EQ_MSG((ptrdiff_t)0, (ptrdiff_t)(used_after - used_before),
                  "perm_arena grew across surface create/destroy cycles — SwSurface struct is leaking");

    if (backend.destroy != nullptr) {
        backend.destroy(backend.user_data);
    }
    fl_arena_destroy(arena);
}

// The SW renderer's scissor stack stays paired with the IR clip nesting when the nesting exceeds
// MAX_SCISSOR_STACK_DEPTH (16). A push past the cap is dropped, and its matching pop decrements a counter of
// ignored pushes and leaves the real stack alone, as ignored_pushes does in the GPU backend's
// gpu_ir_to_draw_cmd.c.
//
// The scene is a 64x64 surface of one tile. 15 clips fit, the 15th restricting to the top-left 16x16. A 16th
// push is dropped and popped at once, and the 16x16 clip is still innermost when the fill is drawn.

UTEST(render, scissor_stack_deep_nesting_balanced) {
    FlArena* arena = arena_new();
    FlRenderBackend backend = create_software_renderer(arena, nullptr, FontRenderMode_Grayscale);
    FlSurface* surface = backend.create_surface(backend.user_data, 64, 64);

    FlArena* ir = arena_new();
    fl_arena_align(ir, 8);
    const u8* start = (const u8*)fl_arena_current_pos(ir);

    const FlColor red = { 220, 40, 50, 255 };
    const FlColor red4[4] = { red, red, red, red };
    const FlRect full = { 0.0f, 0.0f, 64.0f, 64.0f };
    const FlRect corner = { 0.0f, 0.0f, 16.0f, 16.0f };

    // 15 pushes fit: depth is capped at MAX_SCISSOR_STACK_DEPTH == 16 and slot 0 is the tile scissor. The last
    // one that fits restricts to the top-left 16x16, and the earlier 14 leave the region unrestricted.
    for (int i = 0; i < 14; ++i) {
        ir_clip_push(ir, full);
    }
    ir_clip_push(ir, corner); // 15th push: the innermost rect, which has to survive the over-cap pop below

    // The 16th push is past the cap, so the renderer drops it. Its matching pop cancels the dropped push and does
    // not pop the 16x16 clip, which is still active.
    ir_clip_push(ir, full);
    ir_clip_pop(ir);

    // A full-surface fill, drawn while the 16x16 clip is the innermost scissor.
    ir_fill_rect(ir, full, (FlCornerRadius) { 0 }, red4, nullptr);

    // Pop the 15 real pushes to balance the stream.
    for (int i = 0; i < 15; ++i) {
        ir_clip_pop(ir);
    }

    const size_t bytes = (size_t)((const u8*)fl_arena_current_pos(ir) - start);

    const FlColor clear = { 10, 10, 10, 255 };
    const FlRenderFrame frame = {
        .commands = start,
        .bytes = bytes,
        .global_alpha = 255,
        .background = { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID, .clear = clear },
    };
    render_seam_render(&backend, surface, &frame);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&backend, surface, &buf), "read_pixels failed");
    const u8* pixels = (const u8*)buf.pixels;

    // Control: inside the 16x16 clip the fill is drawn (red) in either case, which shows the fill ran.
    const size_t in = (size_t)8 * buf.stride + (size_t)8 * 3u;
    ASSERT_TRUE_MSG(pixels[in + 0] > 150 && pixels[in + 0] > pixels[in + 1] && pixels[in + 0] > pixels[in + 2],
                    "fill must be visible (red) inside the innermost 16x16 clip");

    // Outside the 16x16 clip but inside the 14th, full-surface clip, the fill is clipped away. If the over-cap
    // pop had removed the 16x16 clip, the fill would show here as red.
    const size_t out = (size_t)32 * buf.stride + (size_t)32 * 3u;
    ASSERT_EQ_MSG((u32)clear.r, (u32)pixels[out + 0], "fill leaked past the 16x16 clip — scissor pop/push imbalance");
    ASSERT_EQ_MSG((u32)clear.g, (u32)pixels[out + 1], "fill leaked past the 16x16 clip — scissor pop/push imbalance");
    ASSERT_EQ_MSG((u32)clear.b, (u32)pixels[out + 2], "fill leaked past the 16x16 clip — scissor pop/push imbalance");

    fl_arena_destroy(ir);
    backend.destroy_surface(backend.user_data, surface);
    if (backend.destroy != nullptr) {
        backend.destroy(backend.user_data);
    }
    fl_arena_destroy(arena);
}

// The SW text and coverage raster applies the frame's global_alpha to text exactly once, as the GPU backend does.
// global_alpha is applied when the command is emitted (sr_color_premul, pack_tint_premultiplied), so draw_text
// does not multiply by command->global_alpha again.
//
// The scene is a full-coverage (0xFF) alpha-coverage quad with an opaque black tint over a white background,
// swept across global_alpha values. A black tint premultiplies to zero RGB, so how far the covered pixel darkens
// from white depends only on the coverage and alpha term. The darkening is linear in global_alpha: pixel == 255 -
// fold(255, global_alpha), where fold is the round-to-nearest /255 both backends use. A fade applied twice is
// quadratic and far outside the tolerance at partial alpha (ga=128 gives about 192, not 128).

UTEST(render, text_alpha_linear_fade) {
    FlArena* arena = arena_new();
    FlRenderBackend backend = create_software_renderer(arena, nullptr, FontRenderMode_Grayscale);
    FlSurface* surface = backend.create_surface(backend.user_data, 32, 32);

    // A 16x16 single-channel coverage buffer, fully opaque. The SW backend blits coverage 1:1.
    u8 coverage[16 * 16];
    memset(coverage, 0xFF, sizeof(coverage));

    // An opaque black tint over a white clear color: the covered pixel darkens from 255 toward 0 by the alpha and
    // coverage term alone. The sample is well inside the dst rect (8,8)-(24,24).
    const FlColor black = { 0, 0, 0, 255 };
    const FlColor white = { 255, 255, 255, 255 };
    const int sample_x = 16, sample_y = 16;

    const u8 alphas[] = { 0, 64, 128, 192, 255 };
    for (size_t i = 0; i < sizeof_array(alphas); ++i) {
        const u8 ga = alphas[i];

        FlArena* ir = arena_new();
        fl_arena_align(ir, 8);
        const u8* start = (const u8*)fl_arena_current_pos(ir);
        FlImageSource src = { .kind = FlImageKind_Dynamic };
        src.dynamic.pixels = coverage;
        src.dynamic.w = 16;
        src.dynamic.h = 16;
        src.dynamic.stride = 16;
        ir_image(ir, src, FlImageMode_AlphaCoverage, (FlRect) { 8.0f, 8.0f, 16.0f, 16.0f },
                 (FlRect) { 0.0f, 0.0f, 0.0f, 0.0f }, black, FlImageFilter_Nearest, nullptr);
        const size_t bytes = (size_t)((const u8*)fl_arena_current_pos(ir) - start);

        const FlRenderFrame frame = {
            .commands = start,
            .bytes = bytes,
            .global_alpha = ga,
            .background = { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID, .clear = white },
        };
        render_seam_render(&backend, surface, &frame);

        FlPixelReadback buf;
        ASSERT_TRUE_MSG(render_seam_read_pixels(&backend, surface, &buf), "read_pixels failed");
        const u8* px = (const u8*)buf.pixels + (size_t)sample_y * buf.stride + (size_t)sample_x * 3u;

        // The one round-to-nearest /255 fold both backends apply. The linear output is white minus it.
        const int folded = (ga == 255) ? 255 : (255 * ga + 127) / 255;
        const int expected = 255 - folded;

        // The tolerance covers the tile's Q7/Q15 coverage quantization, about 1-2 LSB. A quadratic fade misses
        // by tens.
        for (int c = 0; c < 3; ++c) {
            const int got = px[c];
            ASSERT_TRUE_MSG(abs(got - expected) <= 4, "SW text global_alpha must fade linearly (matching GPU)");
        }
        fl_arena_destroy(ir);
    }

    backend.destroy_surface(backend.user_data, surface);
    if (backend.destroy != nullptr) {
        backend.destroy(backend.user_data);
    }
    fl_arena_destroy(arena);
}

// Composites a GPU-packed straight color (packed RGBA8 with R in the low byte and A in the high byte, the order of
// GLSL unpackUnorm4x8) over an opaque background at full coverage, as the GPU raster shader does: premultiply,
// then over, out = rgb*a + bg*(1-a), with round-to-nearest /255. Comparing an SW pixel with this compares it with
// the GPU result and not with a hand-typed constant.
static void gpu_straight_over_ref(u32 packed, const FlColor* bg, int out[3]) {
    const int a = (int)((packed >> 24) & 0xffu);
    const int c[3] = { (int)(packed & 0xffu), (int)((packed >> 8) & 0xffu), (int)((packed >> 16) & 0xffu) };
    const int b[3] = { bg->r, bg->g, bg->b };
    for (int i = 0; i < 3; ++i) {
        out[i] = (c[i] * a + b[i] * (255 - a) + 127) / 255;
    }
}

typedef struct OverFixture {
    FlRenderBackend* backend;
    FlSurface* surface;
    FlArena* commands_arena;
    FlColor clear;
} OverFixture;

typedef struct OverIr {
    FlArena* ir;
    const u8* start;
} OverIr;

static OverIr over_ir_begin(void) {
    FlArena* ir = arena_new();
    fl_arena_align(ir, 8);
    return (OverIr) { .ir = ir, .start = (const u8*)fl_arena_current_pos(ir) };
}

typedef struct OverProbe {
    int x;
    int y;
    int tol;
    const char* msg;
} OverProbe;

static bool render_ir_to_surface(const OverFixture* fx, const u8* start, size_t bytes, FlPixelReadback* out) {
    const FlRenderFrame frame = {
        .commands = start,
        .bytes = bytes,
        .global_alpha = 255,
        .background = { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID, .clear = fx->clear },
    };
    render_seam_render(fx->backend, fx->surface, &frame);
    return render_seam_read_pixels(fx->backend, fx->surface, out);
}

static bool px_matches_gpu_over(const FlPixelReadback* buf, const OverProbe* probe, u32 packed, const FlColor* bg) {
    int ref[3];
    gpu_straight_over_ref(packed, bg, ref);
    const u8* p = (const u8*)buf->pixels + (size_t)probe->y * buf->stride + (size_t)probe->x * 3u;
    return abs((int)p[0] - ref[0]) <= probe->tol && abs((int)p[1] - ref[1]) <= probe->tol
           && abs((int)p[2] - ref[2]) <= probe->tol;
}

// Translates the IR the case just recorded for the GPU, renders it in software, and asserts the probe pixel matches
// the GPU compositing of the first command's packed color. Destroys the IR arena.
static void check_over_ir(int* utest_result, const OverFixture* fx, const OverIr* rec, const OverProbe* probe) {
    FlArena* ir = rec->ir;
    const u8* start = rec->start;
    const size_t bytes = (size_t)((const u8*)fl_arena_current_pos(ir) - start);
    GpuDrawList g = gpu_ir_to_draw_cmds(
        fx->commands_arena,
        &(GpuIrInput) {
            .commands = start, .bytes = bytes, .global_alpha = 255, .target_width = 128, .target_height = 128 });
    ASSERT_TRUE(g.count >= 1);
    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_ir_to_surface(fx, start, bytes, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(px_matches_gpu_over(&buf, probe, g.cmds[0].color[0], &fx->clear), probe->msg);
    fl_arena_destroy(ir);
}

// The SW fill, border and gradient blend applies the source alpha exactly once (straight over: out = rgb*a +
// bg*(1-a)), as the GPU backend does. Colors reach the renderer with global_alpha already applied, so
// premultiplying the rgb there would apply alpha a second time in the lerp and give rgb*a*a. Each SW pixel is
// compared with the GPU backend's own compositing of the GPU-packed color for the same IR (gpu_ir_to_draw_cmds,
// then gpu_straight_over_ref).
//
// The case covers all four translator sites that use straight packing: a solid fill, a square (radius-0) border
// whose straight edges go through the fill path, a two-color gradient fill (RenderMask_ColorGradient), and a
// rounded border, whose corners render through process_8_pixels_border.

// A layout border's dash pattern passes through FlLayoutBorder.stroke -> Clay element config -> render data ->
// clay_to_render_ir, and nothing else reads it on that path. The case runs at ui_scale 2 because the lengths
// scale, and at scale 1 an unscaled pattern looks the same as a correct one.
UTEST(render, layout_border_dash_pattern_reaches_the_ir_scaled) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(2.0f, 2.0f);

    ui_update_begin(ctx);
    fl_layout_start(ID("dashed_border_root"));
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { .width = sizing_grow(), .height = sizing_grow() },
    });
    fl_layout_start(ID("dashed_border_child"));
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_fixed(100), sizing_fixed(100) },
        .border = { .color = ui_color(220, 200, 60, 255),
                    .width = { .left = 2, .right = 2, .top = 2, .bottom = 2 },
                    .stroke = { 3.0f, 2.5f } },
    });
    fl_layout_end();
    fl_layout_end();
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_TRUE_MSG(s_render.border_found, "a configured border must reach the frame as a border record");
    ASSERT_TRUE_MSG(s_render.submitted_border.stroke.dash_len == 6.0f
                        && s_render.submitted_border.stroke.gap_len == 5.0f,
                    "the dash pattern arrives scaled by the UI scale, like every other length");
}

UTEST(render, bitmap_font_baseline_sits_a_cell_below_the_box_top) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(1.0f, 1.0f);

    const u16 load_scale = 2;
    const float cell = 8.0f * (float)load_scale;
    FlFontId font = font_cache_load_default_font(s_render.font_cache, load_scale);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "the built-in font loads");

    ui_update_begin(ctx);
    fl_layout_start(ID("bitmap_top_root"));
    fl_layout_configure((FlLayoutConfig) { .sizing = { .width = sizing_grow(), .height = sizing_grow() } });
    ui_text_styled(FL_S("A"), .font_id = font, .valign = FlTextVAlign_Top, .color = ui_color(255, 255, 255, 255));
    fl_layout_end();
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_TRUE_MSG(s_render.text_run_found, "the text must reach the frame as a text run");
    ASSERT_NEAR_MSG(cell, s_render.text_run_glyphs[0].y, 0.5f, "a top-aligned line puts the baseline one cell down");

    const float row = 100.0f;
    ui_update_begin(ctx);
    fl_layout_start(ID("bitmap_center_root"));
    fl_layout_configure((FlLayoutConfig) { .sizing = { .width = sizing_grow(), .height = sizing_fixed(row) },
                                           .child_alignment = ui_align(Left, Center) });
    ui_text_styled(FL_S("A"), .font_id = font, .valign = FlTextVAlign_Center, .color = ui_color(255, 255, 255, 255));
    fl_layout_end();
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_TRUE_MSG(s_render.text_run_found, "the centred text must reach the frame as a text run");
    ASSERT_NEAR_MSG(row * 0.5f + cell * 0.5f, s_render.text_run_glyphs[0].y, 0.5f,
                    "a centred line spans one cell around the row centre");
}

// Text follows a scale change made after the font was loaded. A font handle names the size the caller authored,
// and text_scale is multiplied in when the size is used, so a face keeps nothing from the scale it was loaded
// under. Both paths are covered: an explicit config.font_size, and font_size 0 falling back to the load size.
// The case runs at scale 2 because at scale 1 an unmultiplied size looks correct.
UTEST(render, text_size_follows_a_scale_change_after_the_font_loads) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(1.0f, 1.0f);

    // Loaded while the scale is still 1. Everything below checks that this handle follows the later change.
    const u16 authored = 12;
    FlFontId font = font_cache_load_font_memory(s_render.font_cache, k_glyph_test_ttf, k_glyph_test_ttf_size, authored);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "the embedded test font loads");
    ASSERT_EQ_MSG(font_cache_get_size(s_render.font_cache, font), (int)authored,
                  "the handle keeps the authored size, unscaled");

    ui_set_scale(2.0f, 2.0f);

    // Explicit size: 20px authored reaches the IR as a 40px raster key.
    emit_scaled_text_frame(ctx, ID("scaled_text_root"), font, 20);
    ASSERT_TRUE_MSG(s_render.text_run_found, "the styled text must reach the frame as a text run");
    ASSERT_EQ_MSG((int)s_render.submitted_text_run.size, (int)font_cache_whole_px_to_q4(40),
                  "an authored 20px reaches the IR at 40px when text_scale is 2");

    // Size 0: the load-size fallback scales the same way, which holds only if the load size is authored.
    emit_scaled_text_frame(ctx, ID("scaled_text_default_root"), font, 0);
    ASSERT_TRUE_MSG(s_render.text_run_found, "the default-size text must reach the frame as a text run");
    ASSERT_EQ_MSG((int)s_render.submitted_text_run.size, (int)font_cache_whole_px_to_q4((u16)(authored * 2)),
                  "the load-size fallback scales too, so the load size must have stayed authored");

    // A font loaded after the change gets the same size as the one loaded before it.
    FlFontId late = font_cache_load_font_memory(s_render.font_cache, k_glyph_test_ttf, k_glyph_test_ttf_size, authored);
    ASSERT_EQ_MSG(font_cache_get_size(s_render.font_cache, late), (int)authored,
                  "a font loaded under scale 2 keeps the same authored size");
}

// Letter spacing and line height are authored px too, so they follow text_scale with the glyphs. Both are checked
// relatively, a scale-2 frame against a scale-1 frame authored at double, so no Clay line-box rule appears in
// the assertion. The third frame guards against a vacuous pass: it has the same authored values at scale 1, so
// its results must differ.
UTEST(render, text_letter_spacing_and_line_height_follow_the_scale) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlFontId font = font_cache_load_font_memory(s_render.font_cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 16);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "the embedded test font loads");

    // Each frame emits "AA" and reports the first pen step plus the laid-out text box height.
    struct SpacedFrame {
        float scale;
        u16 font_size;
        u16 spacing;
        u16 line_height;
        float pen_step;
        float box_height;
    };
    // Frames 1 and 2 resolve to the same physical size, spacing and line height by different routes. Frame 3
    // keeps frame 2's authored values but runs at scale 1, so everything it resolves is half.
    struct SpacedFrame frames[] = {
        { .scale = 1.0f, .font_size = 20, .spacing = 6, .line_height = 40 },
        { .scale = 2.0f, .font_size = 10, .spacing = 3, .line_height = 20 },
        { .scale = 1.0f, .font_size = 10, .spacing = 3, .line_height = 20 },
    };

    for (u32 i = 0; i < sizeof_array(frames); i++) {
        ui_set_scale(frames[i].scale, frames[i].scale);
        ui_update_begin(ctx);
        fl_layout_start(ID("spaced_text_root"));
        fl_layout_configure((FlLayoutConfig) { .sizing = { .width = sizing_grow(), .height = sizing_grow() } });
        ui_text_styled(FL_S("AA"), .font_id = font, .font_size = frames[i].font_size,
                       .letter_spacing = frames[i].spacing, .line_height = frames[i].line_height,
                       .color = ui_color(255, 255, 255, 255));
        fl_layout_end();
        ui_update_end(ctx, 1.0f / 60.0f);

        ASSERT_TRUE_MSG(s_render.text_run_found, "the styled text must reach the frame as a text run");
        ASSERT_EQ_MSG((int)s_render.text_run_glyph_count, 2, "one glyph per codepoint");
        frames[i].pen_step = s_render.text_run_glyphs[1].x - s_render.text_run_glyphs[0].x;

        // The laid-out text box can be read only from the debug render-command snapshot.
        UiRenderCommand text_cmd;
        ASSERT_TRUE_MSG(render_cmd_collect(UiRenderCmd_Text, &text_cmd, 1) == 1,
                        "the text element must appear in the render-command snapshot");
        frames[i].box_height = text_cmd.height;
    }

    const struct SpacedFrame* reference = &frames[0];
    const struct SpacedFrame* scaled = &frames[1];
    const struct SpacedFrame* control = &frames[2];
    ASSERT_TRUE_MSG(reference->pen_step > 0.0f, "the pen actually advances");
    ASSERT_NEAR_MSG(scaled->pen_step, reference->pen_step, 0.0001f,
                    "10px + 3px spacing at scale 2 steps like 20px + 6px at scale 1");
    ASSERT_NEAR_MSG(scaled->box_height, reference->box_height, 0.0001f,
                    "an authored 20px line height at scale 2 lays out like 40px at scale 1");
    ASSERT_TRUE_MSG(control->pen_step < scaled->pen_step - 0.0001f,
                    "the same authored values at scale 1 must step less, or the scale is not reaching the pen");
    ASSERT_TRUE_MSG(control->box_height < scaled->box_height - 0.0001f,
                    "and must lay out shorter, or the scale is not reaching the line box");
}

// The same path with no pattern configured: the zeroed default stays zeroed and does not pick up what the
// previous frame left in the Clay element config.
UTEST(render, layout_border_without_a_dash_pattern_stays_solid) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(1.0f, 1.0f);

    ui_update_begin(ctx);
    fl_layout_start(ID("solid_border_root"));
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { .width = sizing_grow(), .height = sizing_grow() },
    });
    fl_layout_start(ID("solid_border_child"));
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_fixed(100), sizing_fixed(100) },
        .border = { .color = ui_color(220, 200, 60, 255), .width = { .left = 2, .right = 2, .top = 2, .bottom = 2 } },
    });
    fl_layout_end();
    fl_layout_end();
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_TRUE_MSG(s_render.border_found, "a configured border must reach the frame as a border record");
    ASSERT_TRUE_MSG(s_render.submitted_border.stroke.dash_len == 0.0f
                        && s_render.submitted_border.stroke.gap_len == 0.0f,
                    "an unconfigured stroke stays solid all the way to the IR");
}

UTEST(render, translucent_fill_border_over) {
    FlArena* arena = arena_new();
    FlRenderBackend backend = create_software_renderer(arena, nullptr, FontRenderMode_Grayscale);
    FlSurface* surface = backend.create_surface(backend.user_data, 128, 128);
    FlArena* commands_arena = arena_new();

    const FlColor clear = { .r = 60, .g = 60, .b = 60, .a = 255 };
    const OverFixture fx
        = { .backend = &backend, .surface = surface, .commands_arena = commands_arena, .clear = clear };
    const FlColor tA = { .r = 200, .g = 80, .b = 20, .a = 128 }; // about 50% alpha
    const FlColor tB = { .r = 40, .g = 160, .b = 220, .a = 96 }; // a second translucent color for the gradient

    // Solid translucent fill, sampled deep inside, away from the antialiased edges.
    {
        const OverIr rec = over_ir_begin();
        const FlColor corners[4] = { tA, tA, tA, tA };
        ir_fill_rect(rec.ir, (FlRect) { 10.0f, 10.0f, 40.0f, 40.0f }, (FlCornerRadius) { 0 }, corners, nullptr);
        const OverProbe probe
            = { 30, 30, 2, "translucent fill must composite straight-over (rgb·a + bg·(1−a)), not rgb·a²" };
        check_over_ir(utest_result, &fx, &rec, &probe);
    }

    // Square (radius-0) border: straight edges render through the fill path. The sample is in the top band, which
    // is x=70..110, y=10..22. The GPU border packs the color in color[0].
    {
        const OverIr rec = over_ir_begin();
        ir_border(rec.ir, (FlRect) { 70.0f, 10.0f, 40.0f, 40.0f }, 12.0f, 12.0f, 12.0f, 12.0f, (FlCornerRadius) { 0 },
                  tA);
        const OverProbe probe = { 90, 16, 2, "translucent square border must composite straight-over" };
        check_over_ir(utest_result, &fx, &rec, &probe);
    }

    // Two-color gradient fill: differing corners take the RenderMask_ColorGradient path, which interpolates and
    // then composites per pixel. The left corners are tA and the right tB. The leftmost interior column is mostly
    // tA with half a texel of interpolation toward tB, which is why the tolerance is 3. color[0] is the TL corner
    // (tA).
    {
        const OverIr rec = over_ir_begin();
        const FlColor corners[4] = { tA, tB, tA, tB }; // TL, TR, BL, BR
        ir_fill_rect(rec.ir, (FlRect) { 10.0f, 70.0f, 48.0f, 48.0f }, (FlCornerRadius) { 0 }, corners, nullptr);
        const OverProbe probe = { 11, 90, 3, "translucent gradient corner must composite straight-over, not rgb·a²" };
        check_over_ir(utest_result, &fx, &rec, &probe);
    }

    // Rounded border: the corners render through process_8_pixels_border (dual radius). A corner 24px wide with
    // radius 24 is a filled quarter disk (inner radius 0), so a pixel well inside it is fully covered. (88,88) is
    // inside the top-left corner disk centered at (94,94). The tolerance of 3 covers any sub-pixel antialiasing at
    // the corner.
    {
        const OverIr rec = over_ir_begin();
        const FlCornerRadius r = { .tl = 24.0f, .tr = 24.0f, .bl = 24.0f, .br = 24.0f };
        ir_border(rec.ir, (FlRect) { 70.0f, 70.0f, 48.0f, 48.0f }, 24.0f, 24.0f, 24.0f, 24.0f, r, tA);
        const OverProbe probe
            = { 88, 88, 3, "translucent rounded border corner must composite straight-over, not rgb·a²" };
        check_over_ir(utest_result, &fx, &rec, &probe);
    }

    backend.destroy_surface(backend.user_data, surface);
    if (backend.destroy != nullptr) {
        backend.destroy(backend.user_data);
    }
    fl_arena_destroy(commands_arena);
    fl_arena_destroy(arena);
}

// utest's ASSERT_* macros return from the case, so a failure partway through would leave realized images in the
// fixed-size SW pixel pool. Clearing the background first keeps the compositor from holding a freed handle.
typedef struct RenderImages {
    ImageHandle fg;
    ImageHandle bg_curr;
    ImageHandle bg_prev;
} RenderImages;

static void render_images_cleanup(RenderImages* imgs) {
    ui_background_set(IMAGE_HANDLE_INVALID);
    const ImageHandle handles[] = { imgs->fg, imgs->bg_curr, imgs->bg_prev };
    for (size_t i = 0; i < sizeof_array(handles); ++i) {
        if (handles[i] != IMAGE_HANDLE_INVALID) {
            image_free(handles[i]);
        }
    }
}

#define render_images_auto(name)                                      \
    __attribute__((cleanup(render_images_cleanup))) RenderImages name \
        = { .fg = IMAGE_HANDLE_INVALID, .bg_curr = IMAGE_HANDLE_INVALID, .bg_prev = IMAGE_HANDLE_INVALID }

// Clay layout path: the primitives the widget and layout system express directly (rect, rounded, border, clip),
// then the painter path: gradient, static image, dynamic image (RGBA and alpha), text and translucent fill, painted
// inside this full-window container, whose origin is 0,0 so element-local coordinates equal absolute pixels. The
// paint draws at the container's z, under the child boxes above, but the painted region (y >= 400) does not
// overlap them. Images go through the painter because that texture path draws at the image's native size.
typedef struct PrimitiveImages {
    ImageHandle fg;
    FlDynamicImage* dyn_rgba;
    FlDynamicImage* dyn_alpha;
} PrimitiveImages;

static void emit_primitives_frame(int* utest_result, FlUiCtx* ctx, const PrimitiveImages* images) {
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_LeftToRight, .padding = fl_padding_md(), .child_gap = FlSpacing_LG) {
        ui_layout_area(.sizing = { sizing_fixed(120), sizing_fixed(120) }, .color = ui_color(220, 60, 60, 255)) {}

        ui_layout_area(.sizing = { sizing_fixed(120), sizing_fixed(120) }, .color = ui_color(60, 200, 120, 255),
                       .radius = corner_radius(28)) {}

        ui_layout_area(.sizing = { sizing_fixed(120), sizing_fixed(120) }, .color = ui_color(40, 40, 60, 255),
                       .border = fl_border(ui_color(240, 240, 80, 255), 6)) {}

        // Clip: a fixed box that clips an oversized rounded child.
        ui_layout_area(.sizing = { sizing_fixed(120), sizing_fixed(120) },
                       .clip = { .horizontal = true, .vertical = true }) {
            ui_layout_area(.sizing = { sizing_fixed(260), sizing_fixed(260) }, .color = ui_color(200, 120, 220, 255),
                           .radius = corner_radius(40)) {}
        }

        Painter* p = ctx->painter;

        // Gradient: the IR's gradient has no widget or Clay emitter, so this is the only path that reaches
        // RenderMask_ColorGradient.
        painter_gradient_rect(p, (FlRect) { .x = 100.0f, .y = 400.0f, .w = 320.0f, .h = 200.0f },
                              (FlCornerColors) { .top_left = ui_color(255, 0, 0, 255),
                                                 .top_right = ui_color(0, 255, 0, 255),
                                                 .bottom_left = ui_color(0, 0, 255, 255),
                                                 .bottom_right = ui_color(255, 255, 0, 255) });

        // Static image, drawn at native size.
        ASSERT_TRUE_MSG(painter_image(p, (FlRect) { .x = 470.0f, .y = 400.0f, .w = 0.0f, .h = 0.0f },
                                      (FlImageHandle)images->fg, (FlColor) { 255, 255, 255, 255 }),
                        "painter_image: foreground not ready");

        painter_dynamic_image(p, (FlVec2) { .x = 540.0f, .y = 400.0f }, images->dyn_rgba, ui_color(255, 255, 255, 255));

        // Alpha-only dynamic image, colored by the tint.
        painter_dynamic_image(p, (FlVec2) { .x = 620.0f, .y = 400.0f }, images->dyn_alpha, ui_color(80, 220, 255, 255));

        // Translucent painter rectangle, which blends with the background.
        painter_rect(p, (FlRect) { .x = 760.0f, .y = 400.0f, .w = 260.0f, .h = 200.0f }, ui_color(255, 120, 0, 120));
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

// The translucent fill is the 120/255 blend of its color over the pixels around it.
static void check_translucent_painter_fill(int* utest_result, const FlPixelReadback* buf) {
    const u8* around = readback_rgb_at(buf, 1100, 500);
    const u8* inside = readback_rgb_at(buf, 890, 500);
    const int a = 120;
    const int fill[3] = { 255, 120, 0 };
    const char* const msgs[3]
        = { "translucent fill red channel", "translucent fill green channel", "translucent fill blue channel" };
    for (int c = 0; c < 3; c++) {
        ASSERT_NEAR_MSG((double)inside[c], (double)((around[c] * (255 - a) + fill[c] * a) / 255), 3.0, msgs[c]);
    }
}

// Frame 2: a blend between the two background images. The blend compositor is a separate path that does not use
// alpha or the clear color. The half blend lies between the two endpoints.
static void check_background_blend(int* utest_result, FlUiCtx* ctx, ImageHandle bg_prev, ImageHandle bg_curr) {
    u8 blend_px[3][3];
    const u8 blend_t[3] = { 0, 255, 128 };
    for (int i = 0; i < 3; i++) {
        ui_background_set_blend(bg_prev, bg_curr, blend_t[i]);
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {} // background only
        ui_update_end(ctx, 1.0f / 60.0f);

        FlPixelReadback buf_xfade;
        ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf_xfade),
                        "read_pixels failed");
        memcpy(blend_px[i], readback_rgb_at(&buf_xfade, 960, 540), 3);
        if (i == 2) {
            dump_frame("crossfade", &buf_xfade);
        }
    }
    ASSERT_GT_MSG(abs((int)blend_px[0][0] - (int)blend_px[1][0]), 100, "the two blend endpoints differ");
    for (int c = 0; c < 3; c++) {
        ASSERT_NEAR_MSG((double)blend_px[2][c], ((double)blend_px[0][c] + (double)blend_px[1][c]) * 0.5, 3.0,
                        "a half blend lands midway between the endpoints");
    }
}

UTEST(render, primitives_and_background_blend) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // ui_set_scale 1.0 maps painter and Clay coordinates 1:1 to the offscreen buffer, which is already
    // 1920x1080.
    render_set_layout(ctx, 1920.0f, 1080.0f);
    ASSERT_TRUE_MSG(ctx->ui_scale == 1.0f, "expected identity ui_scale 1.0");

    // Foreground images load as Regular, and the SW backend deinterleaves them into the planar form the texture
    // sampler expects. Background images load with a background_mode, so the SW backend keeps them as
    // interleaved RGB, which the background compositor (copy and crossfade) reads.
    render_images_auto(imgs);
    const FlImageLoadOptions bg_options
        = { .background_mode = FlBackgroundMode_Center, .target_width = 1920, .target_height = 1080 };
    imgs.fg = image_load_from_memory(k_png_red, sizeof(k_png_red), (FlImageLoadOptions) { 0 });
    imgs.bg_curr = image_load_from_memory(k_png_red, sizeof(k_png_red), bg_options);
    imgs.bg_prev = image_load_from_memory(k_png_blue, sizeof(k_png_blue), bg_options);
    const ImageHandle fg_image = imgs.fg, bg_curr = imgs.bg_curr, bg_prev = imgs.bg_prev;
    ASSERT_TRUE_MSG(fg_image != IMAGE_HANDLE_INVALID && bg_curr != IMAGE_HANDLE_INVALID
                        && bg_prev != IMAGE_HANDLE_INVALID,
                    "image load returned invalid handle");
    ASSERT_TRUE_MSG(wait_image_ready(fg_image), "foreground image did not decode in time");
    ASSERT_TRUE_MSG(wait_image_ready(bg_curr), "background (current) image did not decode in time");
    ASSERT_TRUE_MSG(wait_image_ready(bg_prev), "background (previous) image did not decode in time");

    // Dynamic images, for both the RGBA and the alpha-only conversion paths.
    FlDynamicImage* dyn_rgba = dynamic_image_create(48, 48, s_arena);
    FlDynamicImage* dyn_alpha = dynamic_image_create_alpha(48, 48, s_arena);
    ASSERT_TRUE_MSG(dyn_rgba != nullptr && dyn_alpha != nullptr, "dynamic image creation failed");
    fill_dynamic_rgba(dyn_rgba);
    fill_dynamic_alpha(dyn_alpha);

    // Frame 1: the full primitive set drawn over a single background image composited with alpha over the clear
    // color. This is the only path that reads background.alpha and the clear color.
    ui_background_set_clear_color(10, 12, 16);
    ui_background_set(bg_curr);
    ui_background_set_alpha(200);
    emit_primitives_frame(utest_result, ctx, &(PrimitiveImages) { fg_image, dyn_rgba, dyn_alpha });
    RETURN_IF_FAILED();

    FlPixelReadback buf;
    read_frame_1080p(utest_result, ctx, &buf);
    RETURN_IF_FAILED();
    dump_frame("primitives", &buf);

    // Layout path: boxes at padding 16, gap 24, each 120 wide, probed at known points inside them.
    const int box_pitch = 120 + FlSpacing_LG;
    const int box_origin = FlSpacing_MD;
    const int box_mid = box_origin + 60;
    const PxProbe layout_probes[] = {
        { box_mid, box_mid, ui_color(220, 60, 60, 255), 2, true, "solid box center" },
        { box_mid + box_pitch, box_mid, ui_color(60, 200, 120, 255), 2, true, "rounded box center" },
        { box_mid + 2 * box_pitch, box_mid, ui_color(40, 40, 60, 255), 2, true, "bordered box interior" },
        { box_origin + 2 * box_pitch + 3, box_mid, ui_color(240, 240, 80, 255), 2, true,
          "bordered box edge carries the border color" },
        { box_mid + 3 * box_pitch, box_mid, ui_color(200, 120, 220, 255), 2, true, "clipped child fills the clip box" },
        { box_origin + 4 * box_pitch - FlSpacing_LG + 20, box_mid, ui_color(200, 120, 220, 255), 2, false,
          "the oversized child does not paint past the clip box" },
    };
    check_px_probes(utest_result, &buf, layout_probes, sizeof_array(layout_probes));

    // Gradient corners lean toward their corner colors.
    const PxProbe gradient_probes[] = {
        { 102, 402, ui_color(255, 0, 0, 255), 24, true, "gradient top-left corner" },
        { 417, 402, ui_color(0, 255, 0, 255), 24, true, "gradient top-right corner" },
        { 102, 597, ui_color(0, 0, 255, 255), 24, true, "gradient bottom-left corner" },
        { 417, 597, ui_color(255, 255, 0, 255), 24, true, "gradient bottom-right corner" },
    };
    check_px_probes(utest_result, &buf, gradient_probes, sizeof_array(gradient_probes));

    check_translucent_painter_fill(utest_result, &buf);
    check_background_blend(utest_result, ctx, bg_prev, bg_curr);
}

// A background image is scaled to the surface size when it is set, and the SW compositor addresses it by the
// surface's current tile grid on that assumption. change_resolution rebuilds the tile grid but does not refit a
// background image that is already set, so after a resize larger than the image the tile-copy paths would read
// rows and columns far past the image buffer. The renderer drops a background image the surface has outgrown
// and falls back to the clear color. Shrinking below the image still composites.
// A Center-mode background whose target equals a solid source fills the whole target, so the image is a uniform red
// RGB buffer of exactly w x h.
static void load_solid_background(int* utest_result, int w, int h, ImageHandle* out) {
    ImageHandle bg = image_load_from_memory(
        k_png_red, sizeof(k_png_red),
        (FlImageLoadOptions) { .background_mode = FlBackgroundMode_Center, .target_width = w, .target_height = h });
    *out = bg;
    ASSERT_TRUE_MSG(bg != IMAGE_HANDLE_INVALID, "background image load returned invalid handle");
    ASSERT_TRUE_MSG(wait_image_ready(bg), "background image did not decode in time");
    const ImageData* bg_data = image_cache_get_data(bg);
    ASSERT_TRUE_MSG(bg_data != nullptr, "background pixels not resident after realize");
    ASSERT_EQ_MSG(bg_data->channels, 3, "background must be interleaved RGB for the compositor");
    ASSERT_TRUE_MSG(bg_data->width == w && bg_data->height == h,
                    "background image dims must equal the load target for this test's coverage math");
}

UTEST(render, background_resize_larger_no_oob) {
    FlUiCtx* ctx = render_fixture_get(); // creates the image system and SW pixel pool the case uses
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlRenderBackend* backend = &ctx->shared->renderer;

    const int SMALL_W = 128, SMALL_H = 96;
    ImageHandle bg = IMAGE_HANDLE_INVALID;
    load_solid_background(utest_result, SMALL_W, SMALL_H, &bg);
    RETURN_IF_FAILED();

    // The case has its own surface, so the grow and shrink cycle does not touch the shared 1920x1080 fixture
    // surface.
    FlSurface* surface = backend->create_surface(backend->user_data, SMALL_W, SMALL_H);
    ASSERT_TRUE_MSG(surface != nullptr, "surface creation failed");

    const FlColor clear = { .r = 10, .g = 12, .b = 16, .a = 255 };
    const u8 clear_rgb[3] = { clear.r, clear.g, clear.b };
    const FlRenderFrame frame = {
        .commands = nullptr,
        .bytes = 0,
        .global_alpha = 255,
        .background = { .current = (FlImageHandle)bg,
                        .prev = FL_IMAGE_HANDLE_INVALID,
                        .crossfade_t = 0.0f,
                        .alpha = 255,
                        .clear = clear },
    };

    // At the matching size the opaque RGB background covers the surface and composites, which shows the image
    // path runs before the grow step drops it.
    FlPixelReadback base;
    render_seam_render(backend, surface, &frame);
    ASSERT_TRUE_MSG(render_seam_read_pixels(backend, surface, &base), "read_pixels failed");
    const IntCase base_dims[] = {
        { (int)base.width, SMALL_W, "matched-size readback dims width" },
        { (int)base.height, SMALL_H, "matched-size readback dims height" },
    };
    check_int_cases(utest_result, base_dims, sizeof_array(base_dims));
    RETURN_IF_FAILED();
    ASSERT_TRUE_MSG(memcmp(readback_rgb_at(&base, SMALL_W / 2, SMALL_H / 2), clear_rgb, 3) != 0,
                    "matched-size background must composite, not fall back to the clear color");

    // Grow past the image without refitting it. Every tile now addresses rows and columns beyond the SMALL_W x
    // SMALL_H buffer, so the coverage check drops the outgrown image and the direct-copy path reads nothing out
    // of bounds.
    const int BIG_W = 256, BIG_H = 192;
    render_seam_change_resolution(backend, surface, BIG_W, BIG_H);
    render_seam_render(backend, surface, &frame);
    FlPixelReadback grown;
    ASSERT_TRUE_MSG(render_seam_read_pixels(backend, surface, &grown), "read_pixels failed");
    const IntCase grown_dims[] = {
        { (int)grown.width, BIG_W, "grown readback dims width" },
        { (int)grown.height, BIG_H, "grown readback dims height" },
    };
    check_int_cases(utest_result, grown_dims, sizeof_array(grown_dims));
    RETURN_IF_FAILED();
    // A pixel the outgrown image never covered has the clear color.
    ASSERT_TRUE_MSG(memcmp(readback_rgb_at(&grown, BIG_W - 2, BIG_H - 2), clear_rgb, 3) == 0,
                    "outgrown background: far corner must fall back to the clear color, not read past the buffer");
    // A pixel inside the old image extent is clear too: the image is dropped as a whole.
    ASSERT_TRUE_MSG(memcmp(readback_rgb_at(&grown, 2, 2), clear_rgb, 3) == 0,
                    "outgrown background: the whole frame falls back to the clear color");

    // Shrink below the image. It still covers the whole surface, so it keeps compositing and reads only part of
    // its buffer.
    const int TINY_W = 64, TINY_H = 48;
    render_seam_change_resolution(backend, surface, TINY_W, TINY_H);
    render_seam_render(backend, surface, &frame);
    FlPixelReadback shrunk;
    ASSERT_TRUE_MSG(render_seam_read_pixels(backend, surface, &shrunk), "read_pixels failed");
    const IntCase shrunk_dims[] = {
        { (int)shrunk.width, TINY_W, "shrunk readback dims width" },
        { (int)shrunk.height, TINY_H, "shrunk readback dims height" },
    };
    check_int_cases(utest_result, shrunk_dims, sizeof_array(shrunk_dims));
    RETURN_IF_FAILED();
    ASSERT_TRUE_MSG(memcmp(readback_rgb_at(&shrunk, TINY_W / 2, TINY_H / 2), clear_rgb, 3) != 0,
                    "shrink below image: background still covers and must composite, not drop to the clear color");

    backend->destroy_surface(backend->user_data, surface);
}

// Both SW tile output paths process each tile row in 16-px SIMD chunks plus a scalar tail, so the rightmost
// (tile_width & 15) columns of a surface whose width is not a multiple of 16 take the less used path. The case
// renders a frame that is uniform horizontally and requires the whole sampled row to equal its midpoint pixel. A
// skipped tail, or a gap or overlap where the SIMD body meets the tail, leaves a column that differs. Two widths
// cover both forms of the rightmost tile column (TILE_WIDTH == 64):
//   * W=168: the rightmost tile is 40px, a 32px SIMD body followed by an 8px tail.
//   * W=200: the rightmost tile is 8px, a tail with no SIMD chunks.
// Both output paths are covered: copy_tile_to_output (a solid full-window fill, so every tile has a visual
// command) and crossfade_direct_to_output (a blend with only a background and no visual command, the fast path).
//
// The surface replaces the 1920x1080 fixture surface for the duration. utest ASSERTs return from the function,
// so results go into booleans while the replacement surface is alive, and the fixture surface is restored before
// any assertion.
typedef struct Non16Result {
    bool setup_ok; // surfaces created, frames read back, crossfade composited
    bool fill_ok;  // every column of the fill row equals the row midpoint
    bool xfade_ok; // every column of the crossfade row equals the row midpoint
    int bad_w;
    int bad_x;
    const char* bad_path;
} Non16Result;

typedef struct Non16Pass {
    FlUiCtx* ctx;
    FlSurface* saved;
    int w;
    int h;
    Non16Result* result;
} Non16Pass;

// The first column of row y whose pixel differs from the row midpoint, or -1 when the row is uniform.
static int first_nonuniform_column(const FlPixelReadback* buf, int w, int y) {
    const u8* mid = readback_rgb_at(buf, w / 2, y);
    for (int x = 0; x < w; ++x) {
        if (memcmp(readback_rgb_at(buf, x, y), mid, 3) != 0) {
            return x;
        }
    }
    return -1;
}

static void non16_record_bad(const Non16Pass* pass, bool* ok, const char* path, int x) {
    *ok = false;
    pass->result->bad_w = pass->w;
    pass->result->bad_x = x;
    pass->result->bad_path = path;
}

// Path 1: copy_tile_to_output, with a solid opaque full-window fill that is uniform across the row.
static void non16_fill_path(const Non16Pass* pass) {
    Non16Result* r = pass->result;
    const int W = pass->w;
    const int ry = pass->h / 2;
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(10, 12, 16);
    ui_update_begin(pass->ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = ui_color(200, 40, 50, 255)) {
    }
    ui_update_end(pass->ctx, 1.0f / 60.0f);

    FlPixelReadback fbuf;
    if (!render_seam_read_pixels(&pass->ctx->shared->renderer, pass->ctx->surface, &fbuf) || fbuf.width != (u32)W) {
        r->setup_ok = false;
        return;
    }
    const u8* mid = readback_rgb_at(&fbuf, W / 2, ry);
    const bool red_dominates = mid[0] > 100 && mid[0] > mid[1] && mid[0] > mid[2];
    r->setup_ok = r->setup_ok && red_dominates; // otherwise the fill did not render
    const int bad_x = r->fill_ok ? first_nonuniform_column(&fbuf, W, ry) : -1;
    if (bad_x >= 0) {
        non16_record_bad(pass, &r->fill_ok, "fill", bad_x);
    }
}

// Path 2: crossfade_direct_to_output, with a blend of two images and only a background. Center-mode backgrounds
// whose target equals the surface size stretch each solid source over the whole canvas, so the row is one uniform
// red/blue blend. A midpoint that differs from the clear color shows the crossfade fast path ran, and the scan of
// the full row then covers every tail and seam column.
static void non16_crossfade_path(const Non16Pass* pass, ImageHandle bg_prev, ImageHandle bg_curr) {
    Non16Result* r = pass->result;
    const int ry = pass->h / 2;
    ui_background_set_blend(bg_prev, bg_curr, 128);
    ui_update_begin(pass->ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {} // background only
    ui_update_end(pass->ctx, 1.0f / 60.0f);

    FlPixelReadback xbuf;
    if (!render_seam_read_pixels(&pass->ctx->shared->renderer, pass->ctx->surface, &xbuf)) {
        r->setup_ok = false;
        return;
    }
    const u8 clear_rgb[3] = { 10, 12, 16 };
    if (memcmp(readback_rgb_at(&xbuf, pass->w / 2, ry), clear_rgb, 3) == 0) {
        r->setup_ok = false; // the crossfade did not composite: the row is still the clear color
    }
    const int bad_x = r->xfade_ok ? first_nonuniform_column(&xbuf, pass->w, ry) : -1;
    if (bad_x >= 0) {
        non16_record_bad(pass, &r->xfade_ok, "crossfade", bad_x);
    }
}

static bool both_images_ready(ImageHandle a, ImageHandle b) {
    if (a == IMAGE_HANDLE_INVALID || b == IMAGE_HANDLE_INVALID) {
        return false;
    }
    return wait_image_ready(a) && wait_image_ready(b);
}

static void non16_width_pass(const Non16Pass* pass) {
    Non16Result* r = pass->result;
    FlSurface* surf = s_render.backend.create_surface(s_render.backend.user_data, pass->w, pass->h);
    if (surf == nullptr) {
        r->setup_ok = false;
        return;
    }
    ui_set_surface(pass->ctx, surf);
    render_set_layout(pass->ctx, (float)pass->w, (float)pass->h);

    non16_fill_path(pass);

    const FlImageLoadOptions bg_options
        = { .background_mode = FlBackgroundMode_Center, .target_width = pass->w, .target_height = pass->h };
    ImageHandle bg_prev = image_load_from_memory(k_png_blue, sizeof(k_png_blue), bg_options);
    ImageHandle bg_curr = image_load_from_memory(k_png_red, sizeof(k_png_red), bg_options);
    if (both_images_ready(bg_prev, bg_curr)) {
        non16_crossfade_path(pass, bg_prev, bg_curr);
    } else {
        r->setup_ok = false;
    }

    // On every iteration, pass or fail, restore the fixture surface, release the iteration's images and destroy
    // the temporary surface.
    ui_background_set(IMAGE_HANDLE_INVALID);
    if (bg_prev != IMAGE_HANDLE_INVALID) {
        image_free(bg_prev);
    }
    if (bg_curr != IMAGE_HANDLE_INVALID) {
        image_free(bg_curr);
    }
    ui_set_surface(pass->ctx, pass->saved);
    s_render.backend.destroy_surface(s_render.backend.user_data, surf);
}

UTEST(render, non16_width_right_edge) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlSurface* saved = ctx->surface;
    const int H = 120;
    const int widths[] = { 168, 200 }; // 168: body and tail in the rightmost tile. 200: only a tail

    Non16Result r = { .setup_ok = true, .fill_ok = true, .xfade_ok = true, .bad_x = -1, .bad_path = "" };
    for (size_t wi = 0; wi < sizeof_array(widths) && r.setup_ok; ++wi) {
        non16_width_pass(&(Non16Pass) { .ctx = ctx, .saved = saved, .w = widths[wi], .h = H, .result = &r });
    }

    if (!r.fill_ok || !r.xfade_ok) {
        fprintf(stderr, "non16_width_right_edge: %s path W=%d column %d differs from row midpoint\n", r.bad_path,
                r.bad_w, r.bad_x);
    }
    ASSERT_TRUE_MSG(r.setup_ok, "edge-width render setup failed (surface/readback/decode, or a path did not render)");
    ASSERT_TRUE_MSG(r.fill_ok, "copy_tile_to_output left a non-16 tail/seam column unwritten");
    ASSERT_TRUE_MSG(r.xfade_ok, "crossfade_direct_to_output left a non-16 tail/seam column unwritten");
}

// A Viewport3d under a backend with no 3D support.
//
// The software backend binds none of mesh_create, mesh_destroy and viewport3d_submit. The element still lays out
// and fills itself with the placeholder, mesh creation fails and returns no handle, and every 3D call logs and
// does nothing. The API's misuse checks are tested here too, since none of them need a GPU.

// Mesh creation has no backend to reach, so it reports failure and returns no handle, and the API's misuse checks
// are tested here too.
static void check_software_mesh_creation_fails(int* utest_result) {
    FlMeshVertex verts[3] = {
        { .x = 0.0f, .y = 0.0f, .z = 0.0f, .color = { 255, 255, 255, 255 } },
        { .x = 1.0f, .y = 0.0f, .z = 0.0f, .color = { 255, 255, 255, 255 } },
        { .x = 0.0f, .y = 1.0f, .z = 0.0f, .color = { 255, 255, 255, 255 } },
    };
    static const uint16_t tri[3] = { 0, 1, 2 };
    const struct {
        bool lines;
        u32 index_count;
        const char* msg;
    } cases[] = {
        { false, 3, "mesh_create must fail when the backend has no 3D seam" },
        { false, 4, "mesh_create must reject an index count that is not whole triangles" },
        { true, 2, "mesh_create_lines must fail when the backend has no 3D seam" },
        { true, 3, "mesh_create_lines must reject an index count that is not whole segments" },
    };
    for (size_t i = 0; i < sizeof_array(cases); i++) {
        const FlMeshHandle mesh = cases[i].lines ? fl_mesh_create_lines(verts, 3, tri, cases[i].index_count)
                                                 : fl_mesh_create(verts, 3, tri, cases[i].index_count);
        ASSERT_EQ_MSG(mesh, (FlMeshHandle)FL_MESH_HANDLE_INVALID, cases[i].msg);
    }
    fl_mesh_destroy(FL_MESH_HANDLE_INVALID); // does nothing
}

UTEST(render, viewport3d_software_placeholder) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout_1080p(ctx);
    ui_background_set_clear_color(5, 7, 11);

    ASSERT_TRUE_MSG(ctx->shared->renderer.viewport3d_submit == nullptr,
                    "the software backend must not bind the 3D seam");

    // Misuse outside any begin/end pair: each of these logs and returns without touching the open-viewport state.
    fl_viewport3d_end();
    fl_viewport3d_draw_mesh((FlMeshHandle)1, FL_IMAGE_HANDLE_INVALID, FlImageFilter_Linear);
    fl_viewport3d_draw_lines((FlMeshHandle)1, false);
    static const float identity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    };
    fl_viewport3d_set_view_projection(identity, 16);

    check_software_mesh_creation_fails(utest_result);

    // Two frames: the first lays the element out, so begin reports zero, and the second reads that rect back.
    FlVec2 size = { 0 };
    for (int frame = 0; frame < 2; frame++) {
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
            size = fl_viewport3d_begin((FlLayoutId) { .name = FL_S("sw_viewport") });
            fl_viewport3d_set_view_projection(identity, 3); // wrong count: logged, and the matrix is left untouched
            fl_viewport3d_draw_mesh(FL_MESH_HANDLE_INVALID, FL_IMAGE_HANDLE_INVALID, FlImageFilter_Linear);
            fl_viewport3d_end();
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }

    ASSERT_TRUE_MSG(size.x > 0.0f && size.y > 0.0f,
                    "a Viewport3d must lay out and report its rect even with no 3D backend");

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    const u8* px = readback_rgb_at(&buf, 400, 400);
    ASSERT_TRUE_MSG(px[0] == 24 && px[1] == 24 && px[2] == 28,
                    "a Viewport3d with no 3D backend must fill itself with the placeholder");
}

// More distinct viewport ids than the table holds, each shown for two frames and then dropped, all lay out,
// because slots not begun in the last frame are reused.

UTEST(render, viewport3d_retires_unused_slots) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout(ctx, 640.0f, 480.0f);

    static const char* const names[]
        = { "vp_a", "vp_b", "vp_c", "vp_d", "vp_e", "vp_f", "vp_g", "vp_h", "vp_i", "vp_j", "vp_k", "vp_l" };
    for (u32 n = 0; n < sizeof_array(names); n++) {
        FlVec2 size = { 0 };
        for (int frame = 0; frame < 2; frame++) {
            ui_update_begin(ctx);
            ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
                size = fl_viewport3d_begin((FlLayoutId) { .name = string_from_cstr(names[n]) });
                fl_viewport3d_end();
            }
            ui_update_end(ctx, 1.0f / 60.0f);
        }
        ASSERT_TRUE_MSG(size.x > 0.0f && size.y > 0.0f, "every viewport id must get a slot, however many came before");
    }
}

// Eight abandoned viewports are replaced by eight live ones. With the table full of viewports begun every frame, a
// ninth id is refused and none of the eight is displaced.

// Begins every id of the table each frame for four frames. The ninth is only ended while it has a slot.
static void begin_live_viewports(FlUiCtx* ctx, const char* const* names, FlVec2* sizes) {
    for (int frame = 0; frame < 4; frame++) {
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
            for (u32 n = 0; n < 9; n++) {
                sizes[n] = fl_viewport3d_begin((FlLayoutId) { .name = string_from_cstr(names[n]) });
                if (sizes[n].x > 0.0f || n < 8) {
                    fl_viewport3d_end();
                }
            }
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }
}

UTEST(render, viewport3d_live_slots_never_displaced) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout(ctx, 640.0f, 480.0f);

    static const char* const names[]
        = { "live_a", "live_b", "live_c", "live_d", "live_e", "live_f", "live_g", "live_h", "live_i" };
    // Fill the table with viewports that are then abandoned. The live set below takes their slots.
    static const char* const idle_names[]
        = { "idle_a", "idle_b", "idle_c", "idle_d", "idle_e", "idle_f", "idle_g", "idle_h" };
    for (u32 n = 0; n < 8; n++) {
        for (int frame = 0; frame < 2; frame++) {
            ui_update_begin(ctx);
            ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
                fl_viewport3d_begin((FlLayoutId) { .name = string_from_cstr(idle_names[n]) });
                fl_viewport3d_end();
            }
            ui_update_end(ctx, 1.0f / 60.0f);
        }
    }

    FlVec2 sizes[9] = { 0 };
    begin_live_viewports(ctx, names, sizes);

    for (u32 n = 0; n < 8; n++) {
        ASSERT_TRUE_MSG(sizes[n].x > 0.0f && sizes[n].y > 0.0f, "a viewport begun every frame keeps its slot");
    }
    ASSERT_TRUE_MSG(sizes[8].x == 0.0f && sizes[8].y == 0.0f, "a ninth live viewport has no slot");
}

// Each window context owns a painter arena that ui_destroy releases, so opening and closing windows leaves the live
// arena count where it started.

UTEST(render, window_teardown_releases_painter_arena) {
    FlUiCtx* fixture = render_fixture_get();
    ASSERT_TRUE_MSG(fixture != nullptr, "render fixture creation failed");

    FlMemoryStats before;
    fl_memory_tracker_get_stats(&before);

    // The arena count is process-wide, so threads outside this case can move it by a few. A context that leaked
    // an arena would add at least one per cycle.
    enum { CYCLES = 16, ARENA_COUNT_NOISE = 4 };
    for (int i = 0; i < CYCLES; i++) {
        FlUiCtx* ctx = ui_create(s_arena, fixture->shared);
        ASSERT_TRUE_MSG(ctx != nullptr, "ui_create failed");
        ui_destroy(ctx);
    }
    ui_make_current(fixture);

    FlMemoryStats after;
    fl_memory_tracker_get_stats(&after);
    ASSERT_LE_MSG(after.live_arena_count, before.live_arena_count + ARENA_COUNT_NOISE,
                  "ui_destroy must release every arena ui_create made");
}

static void check_app_smoke_frame_ran(int* utest_result, const AppSmokeState* st) {
    ASSERT_TRUE_MSG(st->frame_ran, "frame callback did not run");
    ASSERT_TRUE_MSG(st->frame_str_ok, "fl_frame_str did not return the expected per-frame copy");
    ASSERT_TRUE_MSG(st->frame_str_array_ok,
                    "fl_frame_str_array did not reserve distinct writable slots (or count 0 was not null)");
}

UTEST(render, app_headless_smoke) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    const int w = 64;
    const int h = 64;
    FlAppSettings settings = { .window_title = FL_S("headless"), .width = w, .height = h, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    // Injection works in headless mode: these calls neither trip the headless assert nor quit.
    fl_app_inject_mouse_move(app, 10, 10);
    fl_app_inject_key(app, FlKey_Space, true);

    AppSmokeState st = { .color = ui_color(40, 160, 90, 255) }; // a green that is easy to tell from the clear color

    // Two ticks: the first has dt=0, with no previous timestamp, and the second takes the branch with a real dt.
    ASSERT_TRUE_MSG(fl_app_tick(app, &st, app_smoke_frame), "first tick should keep running");
    check_app_smoke_frame_ran(utest_result, &st);
    ASSERT_TRUE_MSG(fl_app_tick(app, &st, app_smoke_frame), "second tick should keep running");

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(fl_app_read_pixels(app, &buf), "fl_app_read_pixels failed");
    ASSERT_TRUE_MSG(buf.pixels != nullptr, "readback buffer is null");
    ASSERT_EQ(buf.width, (u32)w);
    ASSERT_EQ(buf.height, (u32)h);

    // The center is well inside the full-window rect.
    const u8* pixels = (const u8*)buf.pixels;
    const size_t offset = (size_t)(h / 2) * (size_t)buf.stride + (size_t)(w / 2) * 3u;
    const u8 r = pixels[offset + 0];
    const u8 g = pixels[offset + 1];
    const u8 b = pixels[offset + 2];
    ASSERT_TRUE_MSG(g > 100, "expected a strong green channel in the presented frame");
    ASSERT_TRUE_MSG(g > r && g > b, "expected green to dominate red and blue");

    fl_app_inject_window_close(app);
    ASSERT_FALSE_MSG(fl_app_tick(app, &st, app_smoke_frame), "window-close should stop the loop");

    fl_app_destroy(app);

    // With a close already queued, run()'s first tick sees the quit flag and returns false without blocking.
    FlApp* app2 = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app2 != nullptr, "second fl_app_create failed");
    fl_app_inject_window_close(app2);
    fl_app_run(app2, &st, app_smoke_frame); // returns. It would hang here if the quit flag were ignored
    fl_app_destroy(app2);
}
