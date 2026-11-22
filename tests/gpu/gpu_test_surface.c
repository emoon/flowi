// GPU smoke test: frame lifecycle, surfaces and the GPU perf fence.

#include "gpu_test_common.h"

#if !defined(__APPLE__)

// Frame lifecycle: an all-ineligible frame still clears the target, and change_resolution rebuilds the
// offscreen target, tile lists, and readback buffer. Also covers the surface-texture seam (gpu_renderer.h),
// whose dimensions track the resolution across the resize.

int run_frame_lifecycle_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    FlRenderFrame frame = test_frame();
    FlPixelReadback buf;

    // Surface-texture seam at the create resolution. The sections above never resized, so the dims must
    // still be the 1920x1080 the backend was created at.
    GpuSurfaceTexture surf = gpu_renderer_surface_texture(surface);
    CHECK(surf.handle != 0, "surface seam: texture name must be non-zero after create");
    CHECK(surf.width == 1920 && surf.height == 1080, "surface seam: dims must match the create resolution");

    // Empty frame: a stream with only ineligible records (an empty rect and a border with no widths)
    // translates to zero commands; the pipeline must still run and clear the previous frame to fully
    // transparent. Render a real rect first so there is something to clear.
    void* start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, (FlRect) { 32.0f, 32.0f, 8.0f, 8.0f }, red);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed before the empty frame");
    CHECK(pixel_color(pixel_at(&buf, 36, 36), red, 2.0f), "empty frame setup: the rect to be cleared must land first");

    start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, (FlRect) { 0.0f, 0.0f, 0.0f, 64.0f }, red); // empty: w == 0
    ir_border(ir_arena, (FlRect) { 0, 0, 64, 64 }, 0, 0, 0, 0, (FlCornerRadius) { 0 }, red);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed after empty frame");
    CHECK(pixel_transparent(pixel_at(&buf, 36, 36)), "empty frame: the target must clear to fully transparent");

    render_seam_change_resolution(backend, surface, 640, 360);
    start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, (FlRect) { 8.0f, 8.0f, 16.0f, 16.0f }, green);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed after change_resolution");
    CHECK(buf.width == 640 && buf.height == 360 && buf.stride == 640 * 4u,
          "change_resolution: readback must report the new dimensions");
    CHECK(pixel_color(pixel_at(&buf, 16, 16), green, 2.0f),
          "change_resolution: a rect rendered after resize must land");
    CHECK(pixel_transparent(pixel_at(&buf, 100, 100)),
          "change_resolution: pixels outside the rect must stay transparent");

    // The texture name is not asserted to change, because GL may recycle a just-deleted name on the gen after
    // resize. The new-size pixel readback above proves the target was rebuilt.
    surf = gpu_renderer_surface_texture(surface);
    CHECK(surf.handle != 0, "surface seam: texture name must stay valid after change_resolution");
    CHECK(surf.width == 640 && surf.height == 360, "surface seam: dims must update after change_resolution");

    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

// N-surface present isolation. The windowed compositor (backend_sdl_window.c) drives N windows off the
// one shared GL context, making it current per window and sampling that window's own surface texture each present.
// The SDL swap itself needs a real OS window (the two_windows demo is the manual proof); what is testable
// headlessly is the invariant the present path rests on: surfaces co-resident on one shared context stay
// isolated, each resolving its own texture and its own pixels regardless of render order.

int run_n_surface_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* a = backend->create_surface(backend->user_data, 128, 128);
    FlSurface* b = backend->create_surface(backend->user_data, 64, 96);
    FlRenderFrame frame = test_frame();
    FlPixelReadback buf;

    GpuSurfaceTexture ta = gpu_renderer_surface_texture(a);
    GpuSurfaceTexture tb = gpu_renderer_surface_texture(b);
    CHECK(ta.handle != 0 && tb.handle != 0, "n-surface: both co-resident surfaces have a texture name");
    CHECK(ta.handle != tb.handle, "n-surface: co-resident surfaces get distinct surface textures");
    CHECK(ta.width == 128 && ta.height == 128, "n-surface: surface A texture dims");
    CHECK(tb.width == 64 && tb.height == 96, "n-surface: surface B texture dims");

    // Red into A, then green into B, back to back on the shared context, the windowed render order.
    void* start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, (FlRect) { 0.0f, 0.0f, 128.0f, 128.0f }, red);
    ir_frame_render(backend, a, &frame, ir_arena, start);

    start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, (FlRect) { 0.0f, 0.0f, 64.0f, 96.0f }, green);
    ir_frame_render(backend, b, &frame, ir_arena, start);

    CHECK(render_seam_read_pixels(backend, a, &buf), "n-surface: read back A");
    CHECK(buf.width == 128 && buf.height == 128, "n-surface: A readback reports A's dims");
    CHECK(pixel_color(pixel_at(&buf, 64, 64), red, 2.0f), "n-surface: A keeps its red after B was rendered");
    CHECK(render_seam_read_pixels(backend, b, &buf), "n-surface: read back B");
    CHECK(buf.width == 64 && buf.height == 96, "n-surface: B readback reports B's dims");
    CHECK(pixel_color(pixel_at(&buf, 32, 48), green, 2.0f), "n-surface: B holds its own green");

    // Re-render A (red -> white), interleaving the two surfaces' frames on the shared context.
    start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, (FlRect) { 0.0f, 0.0f, 128.0f, 128.0f }, white);
    ir_frame_render(backend, a, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, b, &buf), "n-surface: re-read B after A re-rendered");
    CHECK(pixel_color(pixel_at(&buf, 32, 48), green, 2.0f), "n-surface: B unchanged when A re-renders");
    CHECK(render_seam_read_pixels(backend, a, &buf), "n-surface: re-read A");
    CHECK(pixel_color(pixel_at(&buf, 64, 64), white, 2.0f), "n-surface: A now carries its new fill");

    backend->destroy_surface(backend->user_data, a);
    backend->destroy_surface(backend->user_data, b);
    printf("gpu_smoke_test: n-surface present isolation (shared GL context) PASS\n");
    return 0;
}

// Surface struct reclamation. Each surface owns a single arena backing both the struct and its readback
// buffer, freed wholesale in destroy_surface, so a churn loop must leave the backend-lifetime perm_arena's
// allocation cursor exactly where it started, while each fresh surface still comes up with its own target
// texture at its own dimensions.

int run_surface_churn_leak_test(FlRenderBackend* backend) {
    GpuShared* shared = (GpuShared*)backend->user_data;
    void* perm_before = fl_arena_current_pos(shared->perm_arena);

    for (int i = 0; i < 32; ++i) {
        FlSurface* s = backend->create_surface(backend->user_data, 256, 192);
        GpuSurfaceTexture tex = gpu_renderer_surface_texture(s);
        CHECK(tex.handle != 0, "surface churn: each fresh surface has a target texture");
        CHECK(tex.width == 256 && tex.height == 192, "surface churn: fresh surface carries its own dims");
        backend->destroy_surface(backend->user_data, s);
    }

    void* perm_after = fl_arena_current_pos(shared->perm_arena);
    CHECK(perm_before == perm_after,
          "surface churn: perm_arena cursor unchanged across 32 open/close cycles (no struct leak)");
    printf("gpu_smoke_test: surface churn struct reclamation PASS\n");
    return 0;
}
// GPU perf fence: the GPU-gated sibling of unit_tests' scene_raster_fill. scene_zoom_change isolates the
// documented zoom-stall hot path: a UI-scale change (the scale_changed seam) wipes the glyph atlas
// wholesale, so the next frame's prepare_text_glyphs pass re-rasterizes every resident glyph. The scene renders a
// text frame cold, then forces that wipe and re-renders, asserting the tier-1 GPU work counters (glyph_dispatches,
// svg_dispatches, tile_overflow) at today's locked baseline through the gpu_debug_read_frame_stats seam and printing
// tier-3 wall-clock ns (never asserted, because the llvmpipe CI GPU makes GPU timings meaningless).

int run_perf_fence_scenes(FlRenderBackend* backend, FlArena* ir_arena) {
    void* ud = backend->user_data;
    FlSurface* surface = backend->create_surface(ud, 1920, 1080);
    FlArena* arena = arena_new();
    FlFontCache* fc = font_cache_new(arena);
    FlFontId font = font_cache_load_font_memory(fc, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    CHECK(font != FONT_ID_INVALID, "perf_fence: the embedded font loads");
    // Let render()'s prepare_text_glyphs realize the runs' glyphs itself; that pass is the zoom-stall hot path.
    backend->set_font_cache(ud, fc);

    // scene_zoom_change: the two embedded codepoints ('A', 'o') at a spread of raster sizes. Each distinct
    // (font, size, glyph_index) is one atlas key, so the frame carries n_sizes*2 distinct glyphs, the resident set a
    // scale change forces the backend to rebuild. Spread horizontally so no 8x8 tile ever nears GPU_TILE_CAPACITY.
    static const u16 sizes[] = { 24, 28, 32, 36, 40 };
    const int n_sizes = (int)sizeof_array(sizes);
    const u32 glyph_a = font_cache_get_glyph_index(fc, font, 'A');
    const u32 glyph_o = font_cache_get_glyph_index(fc, font, 'o');
    CHECK(glyph_a != 0 && glyph_o != 0, "perf_fence: 'A' and 'o' resolve to glyph indices");
    const u32 expect_glyph_dispatches = (u32)(n_sizes * 2); // 'A' + 'o' at each size, one dispatch per glyph

    void* start = ir_rewind_begin(ir_arena);
    for (int s = 0; s < n_sizes; s++) {
        const f32 x = 100.0f + 300.0f * (f32)s;
        FlGlyph glyphs[2] = {
            { .x = x, .y = 200.0f, .glyph_index = glyph_a },
            { .x = x + 60.0f, .y = 200.0f, .glyph_index = glyph_o },
        };
        ir_text_run(ir_arena, font, sizes[s], 1.0f, white, glyphs, 2);
    }
    FlRenderFrame frame = test_frame();

    // svg_dispatches reuses the monotonic svg_raster_count (prior SVG sections already advanced it), so the
    // meaningful per-scene assertion is a delta: a text-only scene rasterizes no SVG cell. The delta check only
    // has teeth while svg_before is nonzero (a seam bug returning 0 would then wrap the unsigned subtraction and
    // fail loudly), which is why main() must keep an SVG section before this one; the CHECK below guards it.
    const u32 svg_before = gpu_debug_svg_raster_count(ud);
    CHECK(svg_before > 0, "perf_fence: expects an earlier SVG section to have advanced svg_raster_count");

    // Establish a known-cold glyph atlas: a scale change wipes it wholesale, so any glyph an earlier
    // section left resident cannot skew this scene's dispatch counts. 1.5 is a scale no prior section sets, so
    // the wipe fires regardless of the scale on entry.
    backend->scale_changed(ud, 1.5f);

    // Frame 1, cold atlas: every distinct glyph rasterizes exactly once.
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    GpuFrameStats cold;
    gpu_debug_read_frame_stats(ud, surface, arena, &cold);
    CHECK(cold.glyph_dispatches == expect_glyph_dispatches,
          "perf_fence: the cold frame rasterizes every distinct glyph once (baseline drift)");
    CHECK(cold.tile_overflow == 0, "perf_fence: the sparse text scene overflows no tile (baseline drift)");

    // Frame 2: a UI-scale change wipes the atlas, so this re-render re-rasterizes the whole resident set.
    backend->scale_changed(ud, 2.0f);
    const u64 t0 = fl_os_monotonic_ns();
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    const u64 t1 = fl_os_monotonic_ns();
    GpuFrameStats zoom;
    gpu_debug_read_frame_stats(ud, surface, arena, &zoom);

    // Tier-3: wall-clock, printed for human sanity, never asserted.
    printf("[perf_fence] scene_zoom_change(N=%u): %llu ns, glyph_dispatches=%u svg_dispatches(delta)=%u "
           "tile_overflow=%u\n",
           expect_glyph_dispatches, (unsigned long long)(t1 - t0), zoom.glyph_dispatches,
           zoom.svg_dispatches - svg_before, zoom.tile_overflow);

    // Tier-1: deterministic work counters locked at today's measured baseline.
    CHECK(zoom.glyph_dispatches == expect_glyph_dispatches,
          "perf_fence: a UI-scale change re-rasterizes every resident glyph (the zoom-stall spike; baseline drift)");
    CHECK(zoom.tile_overflow == 0, "perf_fence: the re-rendered scene still overflows no tile (baseline drift)");
    CHECK(zoom.svg_dispatches - svg_before == 0, "perf_fence: a text-only scene rasterizes no SVG cell");

    // A second micro-scene exercises the tile_overflow summation with a nonzero result (the zoom scene only ever
    // sees 0): 70 solid 8x8 rects stacked on one 8x8 tile overflow its GPU_TILE_CAPACITY (64) list, dropping
    // exactly 6, the same setup run_classification_tests uses, here totalled through gpu_debug_read_frame_stats.
    void* ovf_start = ir_rewind_begin(ir_arena);
    for (int i = 0; i < 70; i++) {
        emit_solid_rect(ir_arena, (FlRect) { 32.0f, 32.0f, 8.0f, 8.0f }, red);
    }
    FlRenderFrame ovf_frame = test_frame();
    ir_frame_render(backend, surface, &ovf_frame, ir_arena, ovf_start);
    GpuFrameStats ovf;
    gpu_debug_read_frame_stats(ud, surface, arena, &ovf);
    CHECK(ovf.tile_overflow == 6, "perf_fence: the summation loop totals a genuine 64+6 single-tile overflow");

    // Unbind the font cache before destroying it (the pointer lives on the backend) and restore the create scale
    // for the tests that follow.
    backend->set_font_cache(ud, nullptr);
    backend->scale_changed(ud, 1.0f);
    backend->destroy_surface(ud, surface);
    font_cache_destroy(fc);
    fl_arena_destroy(arena);
    printf("gpu_smoke_test: perf fence (scene_zoom_change) PASS\n");
    return 0;
}

#endif // !defined(__APPLE__)
