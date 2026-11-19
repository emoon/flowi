// GPU smoke test: tile classification readback and heat-map tests.

#include "gpu_test_common.h"

#if !defined(__APPLE__)

// Part C: structural tests on the classification readback and the heat-map debug mode (gpu_debug.h). The
// readback is the classification pass's tile lists verbatim, so these assert on the structures the raster
// pass consumed, not on pixels.

static u32 tile_total_count(const GpuTileClassification* cls) {
    u32 total = 0;
    for (int ty = 0; ty < cls->tile_rows; ty++) {
        for (int tx = 0; tx < cls->tile_cols; tx++) {
            total += gpu_tile_count(cls, tx, ty);
        }
    }
    return total;
}

static u32 tile_total_overflow(const GpuTileClassification* cls) {
    u32 total = 0;
    for (int ty = 0; ty < cls->tile_rows; ty++) {
        for (int tx = 0; tx < cls->tile_cols; tx++) {
            total += gpu_tile_overflow(cls, tx, ty);
        }
    }
    return total;
}

typedef struct TileExpect {
    int tx, ty;
    u32 count;
    i32 first_cmd; // -1: the command index is not asserted
    const char* msg;
} TileExpect;

static bool tile_matches(const GpuTileClassification* cls, const TileExpect* e) {
    if (gpu_tile_count(cls, e->tx, e->ty) != e->count) {
        return false;
    }
    return e->first_cmd < 0 || gpu_tile_cmd_index(cls, e->tx, e->ty, 0) == (u32)e->first_cmd;
}

static int check_tiles(const GpuTileClassification* cls, const TileExpect* expect, size_t count) {
    for (size_t i = 0; i < count; i++) {
        CHECK(tile_matches(cls, &expect[i]), expect[i].msg);
    }
    return 0;
}

#define CHECK_TILES(cls, ...)                                \
    check_tiles((cls), (const TileExpect[]) { __VA_ARGS__ }, \
                sizeof((const TileExpect[]) { __VA_ARGS__ }) / sizeof(TileExpect))

typedef struct ClassifyCtx {
    FlRenderBackend* backend;
    FlSurface* surface;
    FlArena* ir_arena;
    FlArena* debug_arena;
    FlRenderFrame frame;
    FlPixelReadback buf;
    GpuTileClassification cls;
} ClassifyCtx;

static void emit_overflow_stack(FlArena* ir_arena) {
    for (int i = 0; i < 70; i++) {
        emit_solid_rect(ir_arena, (FlRect) { 32.0f, 32.0f, 8.0f, 8.0f }, red);
    }
}

static void render_ir(ClassifyCtx* c, const void* start) {
    ir_frame_render(c->backend, c->surface, &c->frame, c->ir_arena, start);
}

static bool read_classification(ClassifyCtx* c) {
    return gpu_debug_read_tile_classification(c->backend->user_data, c->surface, c->debug_arena, &c->cls);
}

static bool classify_frame(ClassifyCtx* c, const void* start) {
    render_ir(c, start);
    fl_arena_rewind(c->debug_arena);
    return read_classification(c);
}

static bool render_and_read_pixels(ClassifyCtx* c) {
    render_seam_render(c->backend, c->surface, &c->frame);
    return render_seam_read_pixels(c->backend, c->surface, &c->buf);
}

static int check_overflow_stack(ClassifyCtx* c) {
    // A 64+6 stack on the one 8x8 tile at (4,4).
    void* start = ir_rewind_begin(c->ir_arena);
    emit_overflow_stack(c->ir_arena);
    render_ir(c, start);
    CHECK(read_classification(c), "classification readback failed");
    CHECK(c->cls.tile_cols == (1920 + GPU_TILE_SIZE - 1) / GPU_TILE_SIZE
              && c->cls.tile_rows == (1080 + GPU_TILE_SIZE - 1) / GPU_TILE_SIZE,
          "classification: grid must match the screen size in tiles");
    CHECK(gpu_tile_count(&c->cls, 4, 4) == GPU_TILE_CAPACITY,
          "classification: an overstuffed tile's count clamps to capacity");
    CHECK(gpu_tile_overflow(&c->cls, 4, 4) == 6, "classification: overflow counts the dropped commands");
    for (u32 slot = 0; slot < GPU_TILE_CAPACITY; slot++) {
        CHECK(gpu_tile_cmd_index(&c->cls, 4, 4, slot) == slot,
              "classification: the list holds the first 64 command indices in emission order");
    }
    return 0;
}

static int check_spanning_commands(ClassifyCtx* c) {
    void* start = ir_rewind_begin(c->ir_arena);
    emit_solid_rect(c->ir_arena, (FlRect) { 4.0f, 0.0f, 8.0f, 8.0f }, red); // index 0: straddles tiles (0,0) and (1,0)
    emit_solid_rect(c->ir_arena, (FlRect) { 100.0f, 100.0f, 4.0f, 4.0f }, green); // index 1: wholly inside tile (12,12)
    CHECK(classify_frame(c, start), "classification readback failed on the spanning frame");
    int status
        = CHECK_TILES(&c->cls, { 0, 0, 1, 0, "classification: a spanning command must appear in the left tile's list" },
                      { 1, 0, 1, 0, "classification: a spanning command must appear in the right tile's list" },
                      { 2, 0, 0, -1, "classification: a command outside a tile must not appear in it" },
                      { 0, 1, 0, -1, "classification: the tile row below the spanning command stays empty" },
                      { 12, 12, 1, 1, "classification: a one-tile command lands only in its own tile, by index" });
    if (status != 0) {
        return 1;
    }
    CHECK(tile_total_count(&c->cls) == 3, "classification: the two commands cover exactly three tiles grid-wide");
    CHECK(tile_total_overflow(&c->cls) == 0, "classification: a normal scene must have zero overflow on every tile");
    return 0;
}

static int check_heat_map(ClassifyCtx* c) {
    // Heat-map debug mode: the raster pass visualizes the same per-tile data. A one-command tile lands at
    // t = 1/64 on the green->red ramp, hence near green.
    gpu_debug_set_mode(c->backend->user_data, GpuDebug_TileHeatMap);
    CHECK(render_and_read_pixels(c), "read_pixels failed in heat-map mode");
    CHECK(pixel_color(pixel_at(&c->buf, 2, 2), (FlColor) { 4, 251, 0, 255 }, 4.0f),
          "heat-map: a one-command tile must land near the green end of the ramp");
    CHECK(pixel_color(pixel_at(&c->buf, 100, 100), (FlColor) { 4, 251, 0, 255 }, 4.0f),
          "heat-map: the tile color covers the whole tile, not just the command's pixels");
    CHECK(pixel_transparent(pixel_at(&c->buf, 20, 2)), "heat-map: an empty tile stays transparent");

    // Re-render the overflow stack in heat-map mode.
    void* start = ir_rewind_begin(c->ir_arena);
    emit_overflow_stack(c->ir_arena);
    render_ir(c, start);
    CHECK(render_seam_read_pixels(c->backend, c->surface, &c->buf),
          "read_pixels failed on the overflow heat-map frame");
    CHECK(pixel_color(pixel_at(&c->buf, 36, 36), (FlColor) { 255, 0, 255, 255 }, 0.0f),
          "heat-map: an overflowing tile must be flagged magenta");

    gpu_debug_set_mode(c->backend->user_data, GpuDebug_Off);
    CHECK(render_and_read_pixels(c), "read_pixels failed after debug mode off");
    CHECK(pixel_color(pixel_at(&c->buf, 36, 36), red, 2.0f),
          "heat-map off: normal rendering must be unaffected by the debug seam");
    return 0;
}

static int check_clip_classification(ClassifyCtx* c) {
    // Clip culling is structural: a command classifies by its clip-intersected AABB.
    void* start = ir_rewind_begin(c->ir_arena);
    ir_clip_push(c->ir_arena, (FlRect) { 0, 0, 8, 8 }); // exactly tile (0,0)
    emit_solid_rect(c->ir_arena, (FlRect) { 200.0f, 200.0f, 16.0f, 16.0f },
                    red); // fully clipped out: culled at translation
    emit_solid_rect(c->ir_arena, (FlRect) { 0.0f, 0.0f, 64.0f, 8.0f },
                    green); // rect spans tiles (0..7,0); clip keeps (0,0)
    ir_clip_pop(c->ir_arena);
    CHECK(classify_frame(c, start), "classification readback failed on the clip frame");
    int status = CHECK_TILES(
        &c->cls,
        { 0, 0, 1, 0,
          "clip classification: the partially clipped command lands in its clip's tile, re-indexed after the cull" },
        { 1, 0, 0, -1, "clip classification: tiles the rect spans but the clip excludes hold nothing" },
        { 25, 25, 0, -1, "clip classification: a fully clipped-out command's own tiles stay empty" });
    if (status != 0) {
        return 1;
    }
    CHECK(tile_total_count(&c->cls) == 1,
          "clip classification: the fully clipped-out command lands in no tile list grid-wide");
    return 0;
}

static int check_arc_classification(ClassifyCtx* c) {
    // Arc classification: the AABB bounds the swept sector, so the rest of the circle's tiles
    // hold nothing. A 12-to-3 quarter ring at center (64, 64), radius 40: its sector AABB spans pixels
    // x 64..104, y 24..64: a handful of tiles in the top-right quadrant, against ~100 for the full circle.
    void* start = ir_rewind_begin(c->ir_arena);
    ir_arc(c->ir_arena, 64.0f, 64.0f, 40.0f, 8.0f, 0.0f, 0.25f * TAU, red, 0, nullptr);
    CHECK(classify_frame(c, start), "classification readback failed on the arc frame");
    int status
        = CHECK_TILES(&c->cls, { 11, 5, 1, -1, "arc classification: a tile inside the sector AABB holds the arc" });
    if (status != 0) {
        return 1;
    }
    CHECK(gpu_tile_count(&c->cls, 5, 11) == 0 && gpu_tile_count(&c->cls, 5, 5) == 0
              && gpu_tile_count(&c->cls, 11, 11) == 0,
          "arc classification: tiles in the unswept quadrants hold nothing");
    CHECK(tile_total_count(&c->cls) <= 36,
          "arc classification: the sector AABB reaches far fewer tiles than the full circle");
    return 0;
}

static int check_effect_classification(ClassifyCtx* c) {
    // Effect AABB expansion: a drop shadow widens the command's classification AABB. The same
    // un-shadowed body first, to prove the distant tile is the effect's doing.
    void* start = ir_rewind_begin(c->ir_arena);
    emit_solid_rect(c->ir_arena, (FlRect) { 26.0f, 26.0f, 4.0f, 4.0f }, red); // body [26,26,30,30]: tile (3,3) only
    CHECK(classify_frame(c, start), "classification readback failed on the un-shadowed frame");
    int status
        = CHECK_TILES(&c->cls, { 3, 3, 1, -1, "effect classification: the bare body lands in its own tile" },
                      { 13, 3, 0, -1, "effect classification: without an effect the body reaches no distant tile" });
    if (status != 0) {
        return 1;
    }

    start = ir_rewind_begin(c->ir_arena);
    FlEffect cls_shadow
        = { .kind = FlEffectKind_DropShadow, .color = { 0, 0, 0, 255 }, .off_x = 80.0f, .off_y = 0.0f, .blur = 4.0f };
    emit_rect_effect(c->ir_arena, (FlRect) { 26.0f, 26.0f, 4.0f, 4.0f }, red,
                     &cls_shadow); // shadow reaches x ~102..114
    CHECK(classify_frame(c, start), "classification readback failed on the shadowed frame");
    status = CHECK_TILES(
        &c->cls, { 3, 3, 1, -1, "effect classification: the shadowed command still covers its body's tile" },
        { 13, 3, 1, -1, "effect classification: the command reaches the tile its shadow touches, not its body" },
        { 20, 3, 0, -1,
          "effect classification: the expanded AABB still bounds the shadow — tiles past its reach stay empty" });
    if (status != 0) {
        return 1;
    }
    return 0;
}

static int check_polyline_classification(ClassifyCtx* c) {
    // Line-aware polyline classification: a long thin diagonal stroke. An AABB would cover the
    // whole bounding box, here ~125 x 125 = ~15600 tiles. The per-segment line walk keeps only the tiles the
    // capsule reaches, so the grid-wide total stays near the diagonal's length.
    void* start = ir_rewind_begin(c->ir_arena);
    emit_polyline(c->ir_arena, (FlPolyPoint[]) { { 8.0f, 8.0f, 1.0f }, { 1000.0f, 1000.0f, 1.0f } }, 2,
                  (PolylineStyle) { .color = red, .fill_color = red, .width = 2.0f, .baseline_y = 0.0f, .flags = 0 });
    CHECK(classify_frame(c, start), "classification readback failed on the diagonal polyline frame");
    int status
        = CHECK_TILES(&c->cls, { 62, 62, 1, -1, "polyline classification: a tile on the diagonal holds the segment" },
                      { 10, 10, 1, -1, "polyline classification: a tile on the diagonal holds the segment" });
    if (status != 0) {
        return 1;
    }
    CHECK(gpu_tile_count(&c->cls, 124, 1) == 0 && gpu_tile_count(&c->cls, 1, 124) == 0,
          "polyline classification: tiles in the off-diagonal corners of the bbox hold nothing");
    status = CHECK_TILES(
        &c->cls,
        { 40, 20, 0, -1, "polyline classification: a tile inside the bbox but well off the line holds nothing" });
    if (status != 0) {
        return 1;
    }
    CHECK(tile_total_count(&c->cls) < 1000,
          "polyline classification: the line walk reaches O(line length) tiles, not the O(bbox area) an AABB would");
    CHECK(tile_total_overflow(&c->cls) == 0, "polyline classification: a single thin diagonal overflows no tile");
    return 0;
}

static int check_polyline_heat_map(ClassifyCtx* c) {
    gpu_debug_set_mode(c->backend->user_data, GpuDebug_TileHeatMap);
    CHECK(render_and_read_pixels(c), "read_pixels failed in heat-map mode on the diagonal");
    CHECK(pixel_color(pixel_at(&c->buf, 500, 500), (FlColor) { 4, 251, 0, 255 }, 4.0f),
          "polyline heat-map: a tile on the diagonal lights up");
    CHECK(pixel_transparent(pixel_at(&c->buf, 992, 12)),
          "polyline heat-map: an off-diagonal tile inside the bbox stays transparent");
    gpu_debug_set_mode(c->backend->user_data, GpuDebug_Off);
    return 0;
}

static int check_waveform_classification(ClassifyCtx* c) {
    // A representative waveform scene: hundreds of points spanning the screen, filled to a baseline, updated
    // as a per-frame command.
    enum { WAVE_POINTS = 400 };
    static FlPolyPoint wave[WAVE_POINTS];
    for (int i = 0; i < WAVE_POINTS; i++) {
        f32 x = (f32)i / (f32)(WAVE_POINTS - 1) * 1900.0f + 8.0f;
        f32 y = 540.0f + 320.0f * sinf((f32)i * 0.12f);
        wave[i] = (FlPolyPoint) { x, y, 1.0f };
    }
    void* start = ir_rewind_begin(c->ir_arena);
    emit_polyline(
        c->ir_arena, wave, WAVE_POINTS,
        (PolylineStyle) {
            .color = red, .fill_color = green, .width = 2.0f, .baseline_y = 860.0f, .flags = FlPolylineFlags_filled });
    CHECK(classify_frame(c, start), "classification readback failed on the waveform frame");
    CHECK(tile_total_overflow(&c->cls) == 0,
          "waveform: a dense per-frame waveform classifies within budget — no tile overflows");
    CHECK(render_seam_read_pixels(c->backend, c->surface, &c->buf), "read_pixels failed on the waveform frame");
    CHECK(pixel_at(&c->buf, 8, 540)[3] > 0, "waveform: the curve renders (the first point's pixel is covered)");
    // The fill-to-baseline renders across the multi-segment curve: a pixel below the first point's curve
    // value (540) and above the baseline (860) is the opaque fill color, green dominating the stroke red.
    const u8* fill_px = pixel_at(&c->buf, 8, 700);
    CHECK(fill_px[3] > 0 && fill_px[1] > fill_px[0],
          "waveform: the fill-to-baseline region renders in the full multi-segment scene");
    return 0;
}

static int run_tile_list_checks(ClassifyCtx* c) {
    if (check_overflow_stack(c) != 0) {
        return 1;
    }
    if (check_spanning_commands(c) != 0) {
        return 1;
    }
    return check_heat_map(c);
}

static int run_shape_classification_checks(ClassifyCtx* c) {
    if (check_clip_classification(c) != 0) {
        return 1;
    }
    if (check_arc_classification(c) != 0) {
        return 1;
    }
    return check_effect_classification(c);
}

static int run_polyline_classification_checks(ClassifyCtx* c) {
    if (check_polyline_classification(c) != 0) {
        return 1;
    }
    if (check_polyline_heat_map(c) != 0) {
        return 1;
    }
    return check_waveform_classification(c);
}

int run_classification_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    ClassifyCtx ctx = {
        .backend = backend,
        .surface = backend->create_surface(backend->user_data, 1920, 1080),
        .ir_arena = ir_arena,
        .debug_arena = arena_new(),
        .frame = test_frame(),
    };

    if (run_tile_list_checks(&ctx) != 0) {
        return 1;
    }
    if (run_shape_classification_checks(&ctx) != 0) {
        return 1;
    }
    if (run_polyline_classification_checks(&ctx) != 0) {
        return 1;
    }

    fl_arena_destroy(ctx.debug_arena);
    backend->destroy_surface(backend->user_data, ctx.surface);
    return 0;
}

#endif // !defined(__APPLE__)
