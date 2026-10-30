// Headless smoke test for the GPU render backend.
//
// Section 1 - packer + translator unit tests. Pure CPU, runs before any EGL/GL call.
//
// Section 2 - the all-compute pipeline end-to-end on a GL 4.3 core context created via EGL surfaceless: no
// window, no native display, so it runs on whatever driver EGL picks - llvmpipe in a GPU-less CI runner
// (LIBGL_ALWAYS_SOFTWARE=1 forces it elsewhere).

#include "app/app.h"
#include <core/arena.h>
#include <core/core.h>
#include <core/os/os.h>
#include "external/clay/clay.h"
#include <render/gpu/gpu_atlas_pack.h>
#include <render/gpu/gpu_debug.h>
#include <render/gpu/gpu_glyph_atlas.h>
#include <render/gpu/gpu_glyph_lower.h>
#include <render/gpu/gpu_image_atlas.h>
#include <render/gpu/gpu_image_slots.h>
#include <render/gpu/gpu_ir_to_draw_cmd.h>
#include <render/gpu/gpu_renderer.h>
#if !defined(__APPLE__)
#include <render/gpu/gpu_renderer_internal.h> // GL-typed, Linux-only
#include <render/gpu/gpu_shaders.h>           // GL-typed, Linux-only
#endif
#include "ui/color.h"
#include "ui/image_decode.h"
#include "ui/image_registry.h"
#include "ui/font_cache.h"
#include "ui/layout.h"
#include "ui/painter.h"
#include "ui/dock.h"
#include <flowi/ui/dock.h>
#include <ui/render_backend.h>
#include <flowi/ui/viewport3d.h>
#include "ui/render_ir_build.h"
#include "ui/ui.h"
#include "ui/ui_menu.h"
#include "ui/ui_private.h"
#include "ui/vector_image.h"
#include "vfs/vfs.h"

#if !defined(__APPLE__)
#include <EGL/egl.h>
#include <EGL/eglext.h>
#endif
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "fixtures/fonts/fallback_test_font.h"
#include "fixtures/fonts/glyph_test_fonts.h"
#include "render_seam.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define CHECK(cond, msg)                                       \
    do {                                                       \
        if (!(cond)) {                                         \
            fprintf(stderr, "gpu_smoke_test FAIL: %s\n", msg); \
            return 1;                                          \
        }                                                      \
    } while (0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The frame envelope every test starts from: full opacity, no background images.
static FlRenderFrame test_frame(void) {
    return (FlRenderFrame) {
        .global_alpha = 255,
        .background = { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID },
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pixel probes over a readback. A transparent pixel reads exactly zero in every channel (nothing drew there); an
// exact-color pixel matches c's RGBA within tolerance.
static const u8* pixel_at(const FlPixelReadback* buf, int x, int y) {
    return (const u8*)buf->pixels + (size_t)y * buf->stride + (size_t)x * 4u;
}

static bool channels_near(const u8* px, f32 r, f32 g, f32 b, f32 a, f32 tolerance) {
    return fabsf((f32)px[0] - r) <= tolerance && fabsf((f32)px[1] - g) <= tolerance
           && fabsf((f32)px[2] - b) <= tolerance && fabsf((f32)px[3] - a) <= tolerance;
}

static bool pixel_transparent(const u8* px) {
    return channels_near(px, 0, 0, 0, 0, 0.0f);
}

static bool pixel_color(const u8* px, FlColor c, f32 tolerance) {
    return channels_near(px, (f32)c.r, (f32)c.g, (f32)c.b, (f32)c.a, tolerance);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// IR emission + small test helpers. A stream is built between ir_begin/ir_bytes on an 8-aligned arena run,
// exactly like the core does it (ui.c).

static void* ir_begin(FlArena* arena) {
    fl_arena_align(arena, 8);
    return fl_arena_current_pos(arena);
}

static size_t ir_bytes(FlArena* arena, const void* start) {
    return (size_t)((const u8*)fl_arena_current_pos(arena) - (const u8*)start);
}

static void* ir_rewind_begin(FlArena* ir_arena) {
    fl_arena_rewind(ir_arena);
    return ir_begin(ir_arena);
}

static void ir_frame_render(FlRenderBackend* backend, FlSurface* surface, FlRenderFrame* frame, FlArena* ir_arena,
                            const void* start) {
    frame->commands = start;
    frame->bytes = ir_bytes(ir_arena, start);
    render_seam_render(backend, surface, frame);
}

static const FlColor white = { 255, 255, 255, 255 };

static bool cells_overlap(GpuAtlasCell a, GpuAtlasCell b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}

// Walk a pixel run from outside a shape to inside it, asserting the SDF AA contract every edge shares.
// require_partial additionally demands a genuine intermediate (a real AA ramp, not a step).
static int check_aa_ramp(const FlPixelReadback* buf, const int (*pts)[2], int count, FlColor color,
                         bool require_partial, const char* what) {
    u8 prev_alpha = 0;
    bool saw_partial = false;
    for (int i = 0; i < count; i++) {
        const u8* px = pixel_at(buf, pts[i][0], pts[i][1]);
        if (px[3] < prev_alpha) {
            fprintf(stderr, "gpu_smoke_test FAIL: %s: alpha must rise monotonically along the ramp\n", what);
            return 1;
        }
        prev_alpha = px[3];
        if (px[3] > 0 && px[3] < 255) {
            saw_partial = true;
            f32 a = (f32)px[3] / 255.0f;
            if (!channels_near(px, (f32)color.r * a, (f32)color.g * a, (f32)color.b * a, (f32)px[3], 3.0f)) {
                fprintf(stderr,
                        "gpu_smoke_test FAIL: %s: an AA pixel must be the color premultiplied by its coverage\n", what);
                return 1;
            }
        }
    }
    if (prev_alpha != 255) {
        fprintf(stderr, "gpu_smoke_test FAIL: %s: the ramp must end fully opaque inside the shape\n", what);
        return 1;
    }
    if (require_partial && !saw_partial) {
        fprintf(stderr, "gpu_smoke_test FAIL: %s: the edge must produce at least one partial pixel\n", what);
        return 1;
    }
    return 0;
}

static int check_aa_ramp_line(const FlPixelReadback* buf, int x0, int y0, int dx, int dy, int count, FlColor color,
                              bool require_partial, const char* what) {
    int pts[16][2]; // comfortably above the longest run any caller walks
    CHECK(count <= 16, "check_aa_ramp_line: run longer than the point buffer");
    for (int i = 0; i < count; i++) {
        pts[i][0] = x0 + i * dx;
        pts[i][1] = y0 + i * dy;
    }
    return check_aa_ramp(buf, pts, count, color, require_partial, what);
}

// Emit a polyline into the IR arena from an array of points (no scaling - these are already pixel coords).
static void emit_polyline(FlArena* arena, const FlPolyPoint* points, u32 count, FlColor color, FlColor fill_color,
                          f32 width, f32 baseline_y, u32 flags) {
    FlPolyPoint* dst = fl_ir_polyline(arena, color, fill_color, width, baseline_y, flags, count, NULL);
    for (u32 i = 0; i < count; i++) {
        dst[i] = points[i];
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Everything from here to the shared clear-screen case is the GL/EGL all-compute suite; it compiles only off
// Apple, where the Metal backend runs the cross-backend cases instead.
#if !defined(__APPLE__)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void emit_solid_rect(FlArena* arena, f32 x, f32 y, f32 w, f32 h, FlColor c) {
    fl_ir_fill_rect(arena, (FlRect) { x, y, w, h }, (FlCornerRadius) { 0 }, (FlColor[4]) { c, c, c, c }, nullptr);
}

static void emit_rect_effect(FlArena* arena, f32 x, f32 y, f32 w, f32 h, FlColor c, const FlEffect* fx) {
    fl_ir_fill_rect(arena, (FlRect) { x, y, w, h }, (FlCornerRadius) { 0 }, (FlColor[4]) { c, c, c, c }, fx);
}

// Read an f32 back from a bit pattern the translator stored in a packed u32 word.
static f32 bits_f32(u32 bits) {
    union {
        u32 u;
        f32 f;
    } pun = { .u = bits };
    return pun.f;
}

// The packed RGBA8 (R in the low byte) the translator's pack_color produces for a color at the given
// global_alpha: alpha is c.a * global_alpha rounded to nearest, RGB pass through.
static u32 packed_rgba(FlColor c, u8 global_alpha) {
    u32 a = ((u32)c.a * (u32)global_alpha + 127u) / 255u;
    return (u32)c.r | ((u32)c.g << 8) | ((u32)c.b << 16) | (a << 24);
}

static const FlColor red = { 200, 40, 50, 255 };
static const FlColor green = { 40, 200, 60, 255 };
static const FlColor blue = { 20, 40, 200, 128 }; // half-alpha

static const f32 TAU = 6.28318530717958647692f; // one full turn, radians

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Section 1: translator unit tests (no GL context - nothing here touches EGL or GL).

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Skyline packer + image-atlas bookkeeping unit tests (no GL - pure CPU placement logic).

static int run_packer_tests(void) {
    FlArena* arena = arena_new();

    GpuAtlasPack pack = { 0 };
    gpu_atlas_pack_init(&pack, arena, 64, 64);
    u32 x;
    u32 y;
    CHECK(gpu_atlas_pack_insert(&pack, 64, 64, &x, &y) && x == 0 && y == 0,
          "packer: an atlas-sized rect packs at the origin");
    CHECK(!gpu_atlas_pack_insert(&pack, 1, 1, &x, &y), "packer: a full atlas rejects further inserts");

    gpu_atlas_pack_init(&pack, arena, 64, 64);
    CHECK(!gpu_atlas_pack_insert(&pack, 0, 8, &x, &y), "packer: zero-width rects are rejected");
    CHECK(!gpu_atlas_pack_insert(&pack, 65, 8, &x, &y), "packer: rects wider than the atlas are rejected");

    CHECK(gpu_atlas_pack_insert(&pack, 32, 16, &x, &y) && x == 0 && y == 0, "packer: first rect lands bottom-left");
    CHECK(gpu_atlas_pack_insert(&pack, 32, 16, &x, &y) && x == 32 && y == 0,
          "packer: the bottom row fills before a new row starts");
    CHECK(gpu_atlas_pack_insert(&pack, 32, 16, &x, &y) && x == 0 && y == 16,
          "packer: the next row starts at the lowest available skyline");

    gpu_atlas_pack_init(&pack, arena, 256, 256);
    GpuAtlasCell placed[128];
    u32 placed_count = 0;
    u32 rng = 0xC0FFEEu;
    for (u32 i = 0; i < 128; i++) {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        u32 w = 1u + (rng & 63u);
        u32 h = 1u + ((rng >> 6) & 63u);
        if (!gpu_atlas_pack_insert(&pack, w, h, &x, &y)) {
            continue;
        }
        CHECK(x + w <= 256 && y + h <= 256, "packer sweep: every placement stays inside the atlas");
        GpuAtlasCell cell = { x, y, w, h };
        for (u32 j = 0; j < placed_count; j++) {
            CHECK(!cells_overlap(placed[j], cell), "packer sweep: placements must be pairwise disjoint");
        }
        placed[placed_count++] = cell;
    }
    CHECK(placed_count > 16, "packer sweep: a 256x256 atlas fits a healthy number of small rects");

    GpuImageAtlas atlas;
    gpu_image_atlas_init(&atlas, arena);
    const GpuImageAtlasEntry* a = gpu_image_atlas_place(&atlas, 1, 64, 48);
    const GpuImageAtlasEntry* b = gpu_image_atlas_place(&atlas, 2, 200, 16);
    CHECK(a != NULL && b != NULL, "atlas: two small images place");
    CHECK(a->img_w == 64 && a->img_h == 48, "atlas: the entry keeps the image extent");
    CHECK(!cells_overlap(a->cell, b->cell), "atlas: two images pack into disjoint cells");
    CHECK(a->cell.x + a->cell.w <= GPU_IMAGE_ATLAS_SIZE && a->cell.y + a->cell.h <= GPU_IMAGE_ATLAS_SIZE,
          "atlas: cells stay inside the texture");
    CHECK(gpu_image_atlas_find(&atlas, 1) == a && gpu_image_atlas_find(&atlas, 2) == b,
          "atlas: handles resolve to their entries");
    CHECK(gpu_image_atlas_find(&atlas, 99) == NULL, "atlas: an unknown handle does not resolve");
    CHECK(gpu_image_atlas_place(&atlas, 3, GPU_IMAGE_ATLAS_MAX_DIM + 1, 8) == NULL,
          "atlas: images past the per-image cap are rejected (the next slice's path)");
    CHECK(gpu_image_atlas_place(&atlas, 1, 8, 8) == NULL, "atlas: a handle cannot be placed twice");

    GpuAtlasCell freed = a->cell;
    gpu_image_atlas_release(&atlas, 1);
    CHECK(gpu_image_atlas_find(&atlas, 1) == NULL, "atlas release: the handle stops resolving");
    gpu_image_atlas_release(&atlas, 1); // releasing again is a no-op
    const GpuImageAtlasEntry* c = gpu_image_atlas_place(&atlas, 4, 32, 32);
    CHECK(c != NULL && c->cell.x == freed.x && c->cell.y == freed.y,
          "atlas release: the freed cell is reused by the next fitting placement");
    CHECK(c->cell.w == freed.w && c->cell.h == freed.h && c->img_w == 32 && c->img_h == 32,
          "atlas release: a reused cell keeps its full extent; the entry keeps the image extent");
    gpu_image_atlas_release(&atlas, 4);
    const GpuImageAtlasEntry* d = gpu_image_atlas_place(&atlas, 5, 64, 48);
    CHECK(d != NULL && d->cell.x == freed.x && d->cell.y == freed.y,
          "atlas release: releasing a reused cell returns its full extent to the pool");

    fl_arena_destroy(arena);
    printf("gpu_smoke_test: packer/atlas unit tests PASS\n");
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Glyph-atlas bookkeeping unit tests (no GL - pure CPU keyed placement + lookup over the packer).

static int run_glyph_atlas_tests(void) {
    FlArena* arena = arena_new();

    GpuGlyphAtlas atlas = { 0 };
    gpu_glyph_atlas_init(&atlas, arena);

    const GpuGlyphEntry* a = gpu_glyph_atlas_place(&atlas, 1, 32, 7, 20, 24, -1.0f, 23.0f, 25.5f);
    CHECK(a != NULL, "glyph atlas: a glyph places");
    CHECK(a->cell.w == 20 && a->cell.h == 24, "glyph atlas: the cell keeps the padded extent");
    CHECK(a->cell.x + a->cell.w <= GPU_GLYPH_ATLAS_SIZE && a->cell.y + a->cell.h <= GPU_GLYPH_ATLAS_SIZE,
          "glyph atlas: cells stay inside the texture");
    CHECK(a->left == -1.0f && a->top == 23.0f && a->advance == 25.5f, "glyph atlas: metrics land verbatim");

    CHECK(gpu_glyph_atlas_find(&atlas, 1, 32, 7) == a, "glyph atlas: the key resolves to its entry");
    CHECK(gpu_glyph_atlas_find(&atlas, 1, 32, 8) == NULL, "glyph atlas: unknown glyph ids do not resolve");
    CHECK(gpu_glyph_atlas_find(&atlas, 1, 34, 7) == NULL, "glyph atlas: the size is part of the key");
    CHECK(gpu_glyph_atlas_find(&atlas, 2, 32, 7) == NULL, "glyph atlas: the font is part of the key");
    CHECK(gpu_glyph_atlas_place(&atlas, 1, 32, 7, 20, 24, 0.0f, 0.0f, 0.0f) == NULL,
          "glyph atlas: a key cannot be placed twice");

    const GpuGlyphEntry* b = gpu_glyph_atlas_place(&atlas, 1, 32, 8, 12, 30, 0.0f, 30.0f, 13.0f);
    CHECK(b != NULL, "glyph atlas: a second glyph places");
    CHECK(!cells_overlap(a->cell, b->cell), "glyph atlas: two glyphs pack into disjoint cells");

    const GpuGlyphEntry* space = gpu_glyph_atlas_place(&atlas, 1, 32, 9, 0, 0, 0.0f, 0.0f, 8.0f);
    CHECK(space != NULL && space->cell.w == 0 && space->cell.h == 0,
          "glyph atlas: empty glyphs take a metrics-only entry");
    CHECK(gpu_glyph_atlas_find(&atlas, 1, 32, 9) == space && space->advance == 8.0f,
          "glyph atlas: metrics-only entries resolve with their advance");

    gpu_glyph_atlas_init(&atlas, arena);
    CHECK(gpu_glyph_atlas_find(&atlas, 1, 32, 7) == NULL, "glyph atlas: re-init forgets placements");
    const GpuGlyphEntry* c = gpu_glyph_atlas_place(&atlas, 9, 16, 1, 8, 8, 0.0f, 8.0f, 8.0f);
    CHECK(c != NULL && c->cell.x == 0 && c->cell.y == 0, "glyph atlas: re-init packs from the origin again");

    fl_arena_destroy(arena);
    printf("gpu_smoke_test: glyph-atlas unit tests PASS\n");
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Outline-lowering unit tests (no GL - gpu_glyph_lower.c is pure CPU).

// Evaluate one wire-format quadratic (6 floats: p0, control, p1) at t.
static Vec2 lowered_quad_eval(const f32* q, f32 t) {
    f32 s = 1.0f - t;
    return (Vec2) {
        s * s * q[0] + 2.0f * s * t * q[2] + t * t * q[4],
        s * s * q[1] + 2.0f * s * t * q[3] + t * t * q[5],
    };
}

static int run_glyph_lower_tests(void) {
    f32 floats[4 * 6];

    // A line lowers to one degenerate quadratic (control = midpoint) in y-down cell coordinates: with the
    // cell origin at (1, 10), outline (2,3) -> (5,9) lands as (1,7) -> (4,1).
    FlGlyphSegment line = { .kind = FlGlyphSegment_Line, .pts = { { 2.0f, 3.0f }, { 5.0f, 9.0f } } };
    FlGlyphOutline outline = { .segments = &line, .segment_count = 1 };
    CHECK(gpu_glyph_lower_outline(&outline, 1.0f, 10.0f, floats) == 1, "lower: a line yields one quadratic");
    CHECK(floats[0] == 1.0f && floats[1] == 7.0f && floats[4] == 4.0f && floats[5] == 1.0f,
          "lower: endpoints map through the y-down cell transform");
    CHECK(floats[2] == 2.5f && floats[3] == 4.0f, "lower: a line's control is its midpoint");

    FlGlyphSegment quad
        = { .kind = FlGlyphSegment_Quadratic, .pts = { { 0.0f, 0.0f }, { 4.0f, 8.0f }, { 8.0f, 0.0f } } };
    outline = (FlGlyphOutline) { .segments = &quad, .segment_count = 1 };
    CHECK(gpu_glyph_lower_outline(&outline, 0.0f, 8.0f, floats) == 1, "lower: a quadratic yields one quadratic");
    CHECK(floats[0] == 0.0f && floats[1] == 8.0f && floats[2] == 4.0f && floats[3] == 0.0f && floats[4] == 8.0f
              && floats[5] == 8.0f,
          "lower: a quadratic's points pass through the transform verbatim");

    // This cubic is the degree-elevated form of the quadratic above (c1 = p0 + 2/3 (c - p0), c2 = p3 + 2/3
    // (c - p3)); elevation commutes with de Casteljau subdivision and the 3/4-rule control recovers an
    // elevated piece's quadratic exactly, so the lowered chain must reproduce the source curve pointwise.
    FlGlyphSegment cubic
        = { .kind = FlGlyphSegment_Cubic,
            .pts = { { 0.0f, 0.0f }, { 8.0f / 3.0f, 16.0f / 3.0f }, { 16.0f / 3.0f, 16.0f / 3.0f }, { 8.0f, 0.0f } } };
    outline = (FlGlyphOutline) { .segments = &cubic, .segment_count = 1 };
    CHECK(gpu_glyph_lower_outline(&outline, 0.0f, 8.0f, floats) == 4, "lower: a cubic yields four quadratics");
    CHECK(floats[0] == 0.0f && floats[1] == 8.0f && floats[3 * 6 + 4] == 8.0f && floats[3 * 6 + 5] == 8.0f,
          "lower: the chain spans the cubic's endpoints");
    for (int i = 0; i < 3; i++) {
        CHECK(floats[i * 6 + 4] == floats[(i + 1) * 6 + 0] && floats[i * 6 + 5] == floats[(i + 1) * 6 + 1],
              "lower: adjacent quadratics share their joint exactly");
    }
    for (int i = 0; i < 4; i++) {
        for (int s = 0; s <= 4; s++) {
            f32 local_t = (f32)s * 0.25f;
            f32 global_t = ((f32)i + local_t) * 0.25f;
            // The source quadratic through the same y-down transform: x = 8t(1-t) + 8t^2 collapses to the
            // forms below with p0' = (0,8), c' = (4,0), p1' = (8,8).
            f32 gs = 1.0f - global_t;
            f32 rx = 2.0f * gs * global_t * 4.0f + global_t * global_t * 8.0f;
            f32 ry = 8.0f - 16.0f * gs * global_t;
            Vec2 pt = lowered_quad_eval(floats + i * 6, local_t);
            CHECK(fabsf(pt.x - rx) <= 0.001f && fabsf(pt.y - ry) <= 0.001f,
                  "lower: the elevated-quadratic cubic lowers exactly onto the source curve");
        }
    }

    FlGlyphSegment mixed[3] = { line, quad, cubic };
    outline = (FlGlyphOutline) { .segments = mixed, .segment_count = 3 };
    f32 mixed_floats[3 * GPU_GLYPH_LOWER_MAX_QUADS_PER_SEGMENT * 6];
    CHECK(gpu_glyph_lower_outline(&outline, 0.0f, 8.0f, mixed_floats) == 6,
          "lower: a mixed outline lowers to the per-kind sum");

    printf("gpu_smoke_test: glyph-lowering unit tests PASS\n");
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Translation target for the unit tests, comfortably larger than any emitted geometry so the full-target
// root clip never clamps an AABB a test asserts on.
enum { TR_TARGET = 2048 };

static GpuDrawList translate(FlArena* out_arena, FlArena* ir_arena, const void* start, u8 global_alpha,
                             const GpuImageAtlas* atlas, GpuImageSlots* slots) {
    return gpu_ir_to_draw_cmds(out_arena, start, ir_bytes(ir_arena, start), global_alpha, TR_TARGET, TR_TARGET, atlas,
                               NULL, slots, NULL, NULL, NULL);
}

// Runs the translation off the test thread so a regressed zero-byte_size guard hangs this worker instead of
// the test process.
typedef struct MalformedTranslateJob {
    FlArena* out_arena;
    const void* commands;
    size_t bytes;
    GpuDrawList list;
    atomic_bool done;
} MalformedTranslateJob;

static void* malformed_translate_thread(void* user_data) {
    MalformedTranslateJob* job = (MalformedTranslateJob*)user_data;
    job->list = gpu_ir_to_draw_cmds(job->out_arena, job->commands, job->bytes, 255, TR_TARGET, TR_TARGET, NULL, NULL,
                                    NULL, NULL, NULL, NULL);
    atomic_store(&job->done, true);
    return NULL;
}

static int run_translator_tests(void) {
    FlArena* ir_arena = arena_new();
    FlArena* out_arena = arena_new();

    GpuDrawList list
        = gpu_ir_to_draw_cmds(out_arena, NULL, 0, 255, TR_TARGET, TR_TARGET, NULL, NULL, NULL, NULL, NULL, NULL);
    CHECK(list.count == 0, "translate: empty stream should produce no commands");
    CHECK(list.clip_count == 1, "translate: clips[0] (the full target) exists even for an empty stream");

    void* start = ir_begin(ir_arena);
    emit_solid_rect(ir_arena, 10.0f, 20.0f, 30.0f, 40.0f, red);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 1, "translate: one solid rect should produce one command");
    CHECK(list.cmds[0].kind == GpuDraw_Rect, "translate: kind should be Rect");
    CHECK(list.cmds[0].rect[0] == 10.0f && list.cmds[0].rect[1] == 20.0f && list.cmds[0].rect[2] == 30.0f
              && list.cmds[0].rect[3] == 40.0f,
          "translate: rect fields should pass through unchanged");
    const u32 packed_red = 200u | (40u << 8) | (50u << 16) | (255u << 24);
    for (int c = 0; c < 4; c++) {
        CHECK(list.cmds[0].color[c] == packed_red,
              "translate: a solid fill should pack the same RGBA8 (R in the low byte) into all four corners");
        CHECK(list.cmds[0].radius[c] == 0.0f, "translate: a square rect should carry zero radii");
    }
    CHECK(list.aabbs[0].x0 == 10.0f && list.aabbs[0].y0 == 20.0f && list.aabbs[0].x1 == 40.0f
              && list.aabbs[0].y1 == 60.0f,
          "translate: AABB should be rect min/max");

    list = translate(out_arena, ir_arena, start, 128, NULL, NULL);
    CHECK((list.cmds[0].color[0] >> 24) == 128u, "translate: global_alpha 128 over a=255 should fold to 128");
    CHECK((list.cmds[0].color[0] & 0x00FFFFFFu) == (200u | (40u << 8) | (50u << 16)),
          "translate: global_alpha must not touch RGB");

    start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, 0.0f, 0.0f, 8.0f, 8.0f, blue); // a = 128
    list = translate(out_arena, ir_arena, start, 128, NULL, NULL);
    CHECK((list.cmds[0].color[0] >> 24) == 64u, "translate: global_alpha 128 over a=128 should round to 64");

    start = ir_rewind_begin(ir_arena);
    fl_ir_fill_rect(ir_arena, (FlRect) { 0, 0, 100, 50 }, (FlCornerRadius) { 3.0f, 5.0f, 7.0f, 9.0f },
                    (FlColor[4]) { red, red, red, red }, nullptr);
    fl_ir_fill_rect(ir_arena, (FlRect) { 0, 0, 40, 20 }, (FlCornerRadius) { 30.0f, 5.0f, -3.0f, 10.0f },
                    (FlColor[4]) { red, red, red, red }, nullptr);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 2, "translate: rounded rects should translate");
    CHECK(list.cmds[0].radius[0] == 3.0f && list.cmds[0].radius[1] == 5.0f && list.cmds[0].radius[2] == 7.0f
              && list.cmds[0].radius[3] == 9.0f,
          "translate: in-range per-corner radii should pass through in TL TR BL BR order");
    CHECK(list.cmds[1].radius[0] == 10.0f, "translate: an oversized radius clamps to half the smaller dimension");
    CHECK(list.cmds[1].radius[1] == 5.0f, "translate: clamping must not disturb in-range radii");
    CHECK(list.cmds[1].radius[2] == 0.0f, "translate: a negative radius clamps to 0");
    CHECK(list.cmds[1].radius[3] == 10.0f, "translate: a radius at exactly half the smaller dimension survives");
    CHECK(list.aabbs[0].x1 == 100.0f && list.aabbs[0].y1 == 50.0f,
          "translate: radii must not affect the AABB (the rect bounds it)");

    const FlColor tl = { 10, 20, 30, 255 };
    const FlColor tr = { 40, 50, 60, 128 };
    const FlColor bl = { 70, 80, 90, 64 };
    const FlColor br = { 100, 110, 120, 0 };
    start = ir_rewind_begin(ir_arena);
    fl_ir_fill_rect(ir_arena, (FlRect) { 0, 0, 50, 50 }, (FlCornerRadius) { 0 }, (FlColor[4]) { tl, tr, bl, br },
                    nullptr);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 1, "translate: a gradient (distinct corner colors) should translate");
    CHECK(list.cmds[0].color[0] == (10u | (20u << 8) | (30u << 16) | (255u << 24))
              && list.cmds[0].color[1] == (40u | (50u << 8) | (60u << 16) | (128u << 24))
              && list.cmds[0].color[2] == (70u | (80u << 8) | (90u << 16) | (64u << 24))
              && list.cmds[0].color[3] == (100u | (110u << 8) | (120u << 16)),
          "translate: corner colors should pack per corner in TL TR BL BR order");
    list = translate(out_arena, ir_arena, start, 128, NULL, NULL);
    CHECK((list.cmds[0].color[0] >> 24) == 128u && (list.cmds[0].color[1] >> 24) == 64u
              && (list.cmds[0].color[2] >> 24) == 32u && (list.cmds[0].color[3] >> 24) == 0u,
          "translate: global_alpha should fold into every corner's alpha");

    start = ir_rewind_begin(ir_arena);
    fl_ir_fill_rect(ir_arena, (FlRect) { 0, 0, 50, 50 }, (FlCornerRadius) { .tl = 4.0f },
                    (FlColor[4]) { red, red, red, red }, nullptr);
    fl_ir_fill_rect(ir_arena, (FlRect) { 0, 0, 50, 50 }, (FlCornerRadius) { 0 }, (FlColor[4]) { red, red, red, blue },
                    nullptr);
    emit_solid_rect(ir_arena, 0.0f, 0.0f, 0.0f, 50.0f, red);                       // empty: w == 0
    FlPolyPoint* pts = fl_ir_polyline(ir_arena, red, red, 1.0f, 0.0f, 0, 1, NULL); // one point: no segment
    pts[0] = (FlPolyPoint) { 0.0f, 0.0f, 1.0f };
    fl_ir_clip_push(ir_arena, (FlRect) { 0, 0, 50, 50 });
    fl_ir_clip_pop(ir_arena);
    emit_solid_rect(ir_arena, 1.0f, 2.0f, 3.0f, 4.0f, red);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 3, "translate: empty/non-rect records should be skipped; rounded and gradient kept");
    CHECK(list.cmds[0].radius[0] == 4.0f, "translate: the first surviving command should be the rounded rect");
    CHECK(list.cmds[1].color[3] != list.cmds[1].color[0],
          "translate: the second surviving command should be the gradient");
    CHECK(list.cmds[2].rect[0] == 1.0f && list.cmds[2].rect[3] == 4.0f,
          "translate: the last surviving command should be the trailing solid rect");
    CHECK(list.clip_count == 1 && list.cmds[0].clip == 0 && list.cmds[2].clip == 0,
          "translate: unclipped commands reference clips[0], and an unused push adds no clip entry");
    CHECK(list.clips[0].x0 == 0.0f && list.clips[0].y0 == 0.0f && list.clips[0].x1 == (f32)TR_TARGET
              && list.clips[0].y1 == (f32)TR_TARGET,
          "translate: clips[0] is the full target rect");

    start = ir_rewind_begin(ir_arena);
    fl_ir_clip_push(ir_arena, (FlRect) { 100, 100, 200, 200 }); // A
    emit_solid_rect(ir_arena, 0.0f, 0.0f, 400.0f, 400.0f, red); // index 0: clipped by A
    fl_ir_clip_push(ir_arena, (FlRect) { 150, 50, 300, 100 });  // B; effective = A ^ B = {150,100,300,150}
    emit_solid_rect(ir_arena, 0.0f, 0.0f, 400.0f, 400.0f, red); // index 1: clipped by A ^ B
    fl_ir_clip_pop(ir_arena);
    emit_solid_rect(ir_arena, 0.0f, 0.0f, 400.0f, 400.0f, red); // index 2: back to A (dedups with index 0)
    fl_ir_clip_pop(ir_arena);
    emit_solid_rect(ir_arena, 0.0f, 0.0f, 400.0f, 400.0f, red); // index 3: full target
    fl_ir_clip_push(ir_arena, (FlRect) { 100, 100, 200, 200 }); // A re-pushed
    emit_solid_rect(ir_arena, 0.0f, 0.0f, 400.0f, 400.0f, red); // index 4: dedups with A's entry
    fl_ir_clip_pop(ir_arena);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 5, "clip: every partially visible command translates");
    CHECK(list.clip_count == 3, "clip: dedup keeps one entry per distinct effective rect");
    CHECK(list.cmds[0].clip == 1 && list.clips[1].x0 == 100.0f && list.clips[1].y0 == 100.0f
              && list.clips[1].x1 == 300.0f && list.clips[1].y1 == 300.0f,
          "clip: a pushed rect becomes the command's clip entry");
    CHECK(list.cmds[1].clip == 2 && list.clips[2].x0 == 150.0f && list.clips[2].y0 == 100.0f
              && list.clips[2].x1 == 300.0f && list.clips[2].y1 == 150.0f,
          "clip: a nested push intersects with the enclosing clip");
    CHECK(list.cmds[2].clip == 1, "clip: a pop restores the enclosing clip, reusing its entry");
    CHECK(list.cmds[3].clip == 0, "clip: popping the last level restores the full target");
    CHECK(list.cmds[4].clip == 1, "clip: re-pushing an identical rect dedups to the existing entry");
    CHECK(list.aabbs[0].x0 == 100.0f && list.aabbs[0].y0 == 100.0f && list.aabbs[0].x1 == 300.0f
              && list.aabbs[0].y1 == 300.0f,
          "clip: the AABB is pre-intersected with the clip rect");
    CHECK(list.aabbs[1].x0 == 150.0f && list.aabbs[1].y1 == 150.0f,
          "clip: a nested command's AABB intersects the whole stack");
    CHECK(list.cmds[0].rect[0] == 0.0f && list.cmds[0].rect[2] == 400.0f,
          "clip: the command's rect passes through unclipped (the raster pass cuts coverage)");

    start = ir_rewind_begin(ir_arena);
    fl_ir_clip_push(ir_arena, (FlRect) { 0, 0, 50, 50 });
    emit_solid_rect(ir_arena, 100.0f, 100.0f, 20.0f, 20.0f, red); // outside the clip: dropped
    emit_solid_rect(ir_arena, 40.0f, 40.0f, 20.0f, 20.0f, red);   // partial overlap: survives
    fl_ir_clip_push(ir_arena, (FlRect) { 60, 60, 10, 10 });       // disjoint from the outer clip: empty
    emit_solid_rect(ir_arena, 0.0f, 0.0f, 100.0f, 100.0f, red);   // under an empty clip: dropped
    fl_ir_clip_pop(ir_arena);
    fl_ir_clip_pop(ir_arena);
    emit_solid_rect(ir_arena, (f32)TR_TARGET + 10.0f, 0.0f, 20.0f, 20.0f, red); // off-target: dropped
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 1, "clip: fully clipped-out and off-target commands never translate");
    CHECK(list.aabbs[0].x0 == 40.0f && list.aabbs[0].y0 == 40.0f && list.aabbs[0].x1 == 50.0f
              && list.aabbs[0].y1 == 50.0f,
          "clip: the surviving command's AABB clamps to the clip");

    start = ir_rewind_begin(ir_arena);
    fl_ir_clip_pop(ir_arena); // nothing to pop
    emit_solid_rect(ir_arena, 10.0f, 10.0f, 20.0f, 20.0f, red);
    fl_ir_clip_push(ir_arena, (FlRect) { 0, 0, 30, 30 }); // never popped
    emit_solid_rect(ir_arena, 10.0f, 10.0f, 40.0f, 40.0f, red);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 2, "clip: an unbalanced stream still translates");
    CHECK(list.cmds[0].clip == 0, "clip: a pop on an empty stack is ignored");
    CHECK(list.cmds[1].clip == 1 && list.aabbs[1].x1 == 30.0f && list.aabbs[1].y1 == 30.0f,
          "clip: an unclosed push still clips the commands after it");

    start = ir_rewind_begin(ir_arena);
    for (int d = 0; d < 40; d++) {
        fl_ir_clip_push(ir_arena, (FlRect) { (f32)d, (f32)d, (f32)TR_TARGET, (f32)TR_TARGET });
    }
    emit_solid_rect(ir_arena, 0.0f, 0.0f, 100.0f, 100.0f, red);
    for (int d = 0; d < 40; d++) {
        fl_ir_clip_pop(ir_arena);
    }
    emit_solid_rect(ir_arena, 0.0f, 0.0f, 100.0f, 100.0f, red);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 2, "clip: a stack deeper than capacity still translates");
    CHECK(list.aabbs[0].x0 == 31.0f && list.aabbs[0].y0 == 31.0f,
          "clip: pushes past capacity are ignored, keeping the deepest tracked intersection");
    CHECK(list.cmds[1].clip == 0, "clip: pops unwind an overflowed stack back to the full target");

    // A GpuDraw_Image command's radius words carry the source rect in atlas texel coordinates and its
    // color[0] is the premultiplied tint.
    GpuImageAtlas atlas;
    gpu_image_atlas_init(&atlas, out_arena);
    const GpuImageAtlasEntry* img_a = gpu_image_atlas_place(&atlas, 7, 8, 8);
    CHECK(img_a != NULL, "image translate: test placement must succeed");
    const f32 ax = (f32)img_a->cell.x;
    const f32 ay = (f32)img_a->cell.y;
    const FlImageSource src_a = { .kind = FlImageKind_Static, .handle = 7 };

    start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, src_a, FlImageMode_Color, (FlRect) { 10, 20, 8, 8 }, (FlRect) { 0 }, white,
                FlImageFilter_Linear, nullptr); // whole image, linear
    fl_ir_image(ir_arena, src_a, FlImageMode_Color, (FlRect) { 30, 20, 4, 3 }, (FlRect) { 2, 1, 4, 3 }, white,
                FlImageFilter_Nearest, nullptr); // sub-rect, nearest
    fl_ir_image(ir_arena, src_a, FlImageMode_Color, (FlRect) { 50, 20, 8, 8 }, (FlRect) { 6, 6, 8, 8 }, white,
                FlImageFilter_Linear, nullptr); // overhanging src
    fl_ir_image(ir_arena, (FlImageSource) { .kind = FlImageKind_Dynamic }, FlImageMode_Color, (FlRect) { 70, 20, 8, 8 },
                (FlRect) { 0 }, white, FlImageFilter_Linear,
                nullptr); // dynamic source: skipped without slot bookkeeping (slots == NULL here)
    fl_ir_image(ir_arena, (FlImageSource) { .kind = FlImageKind_Static, .handle = 99 }, FlImageMode_Color,
                (FlRect) { 90, 20, 8, 8 }, (FlRect) { 0 }, white, FlImageFilter_Linear,
                nullptr); // unresolved handle: skipped
    list = translate(out_arena, ir_arena, start, 255, &atlas, NULL);
    CHECK(list.count == 3, "image translate: resolved static images translate; dynamic and unknown are skipped");
    CHECK(list.cmds[0].kind == GpuDraw_Image, "image translate: kind is Image");
    CHECK(list.cmds[0].rect[0] == 10.0f && list.cmds[0].rect[2] == 8.0f, "image translate: dst passes through");
    CHECK(list.cmds[0].radius[0] == ax && list.cmds[0].radius[1] == ay && list.cmds[0].radius[2] == ax + 8.0f
              && list.cmds[0].radius[3] == ay + 8.0f,
          "image translate: an empty src rect maps to the whole cell in atlas texels");
    CHECK(list.cmds[0].color[0] == 0xFFFFFFFFu, "image translate: the opaque white tint is the identity");
    CHECK(list.cmds[0].flags == 0, "image translate: linear filtering carries no flag");
    CHECK(list.aabbs[0].x0 == 10.0f && list.aabbs[0].x1 == 18.0f, "image translate: the AABB is the dst rect");
    CHECK(list.cmds[1].radius[0] == ax + 2.0f && list.cmds[1].radius[1] == ay + 1.0f
              && list.cmds[1].radius[2] == ax + 6.0f && list.cmds[1].radius[3] == ay + 4.0f,
          "image translate: a src sub-rect maps into the cell");
    CHECK(list.cmds[1].flags == GPU_DRAW_IMAGE_NEAREST, "image translate: nearest filtering sets the flag");
    CHECK(list.cmds[2].radius[0] == ax + 6.0f && list.cmds[2].radius[2] == ax + 8.0f,
          "image translate: a src rect overhanging the image clamps to its extent");

    start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, src_a, FlImageMode_Color, (FlRect) { 10, 20, 8, 8 }, (FlRect) { 0 },
                (FlColor) { 200, 100, 50, 128 }, FlImageFilter_Linear, nullptr);
    list = translate(out_arena, ir_arena, start, 255, &atlas, NULL);
    CHECK(list.cmds[0].color[0] == (100u | (50u << 8) | (25u << 16) | (128u << 24)),
          "image translate: the tint packs premultiplied (rgb * a, round-to-nearest)");
    list = translate(out_arena, ir_arena, start, 128, &atlas, NULL);
    CHECK((list.cmds[0].color[0] >> 24) == 64u && (list.cmds[0].color[0] & 0xFFu) == 50u,
          "image translate: global_alpha folds into the premultiplied tint");

    start = ir_rewind_begin(ir_arena);
    fl_ir_clip_push(ir_arena, (FlRect) { 0, 0, 14, 24 });
    fl_ir_image(ir_arena, src_a, FlImageMode_Color, (FlRect) { 10, 20, 8, 8 }, (FlRect) { 0 }, white,
                FlImageFilter_Linear, nullptr); // partial overlap
    fl_ir_image(ir_arena, src_a, FlImageMode_Color, (FlRect) { 100, 20, 8, 8 }, (FlRect) { 0 }, white,
                FlImageFilter_Linear, nullptr); // fully clipped out
    fl_ir_clip_pop(ir_arena);
    list = translate(out_arena, ir_arena, start, 255, &atlas, NULL);
    CHECK(list.count == 1, "image translate: a fully clipped image never translates");
    CHECK(list.aabbs[0].x1 == 14.0f && list.aabbs[0].y1 == 24.0f,
          "image translate: the image AABB pre-intersects with its clip");
    CHECK(list.cmds[0].clip == 1, "image translate: the image carries its clip index");
    gpu_image_atlas_release(&atlas, 7);
    list = translate(out_arena, ir_arena, start, 255, &atlas, NULL);
    CHECK(list.count == 0, "image translate: a released handle stops translating");

    start = ir_rewind_begin(ir_arena);
    u32 rng = 0x12345678u;
    enum { SWEEP_COUNT = 100 };
    f32 expected[SWEEP_COUNT][4];
    for (u32 i = 0; i < SWEEP_COUNT; i++) {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        f32 x = (f32)(rng & 1023u);
        f32 y = (f32)((rng >> 10) & 1023u);
        f32 w = (f32)(1u + ((rng >> 20) & 255u));
        f32 h = (f32)(1u + ((rng >> 24) & 127u));
        expected[i][0] = x;
        expected[i][1] = y;
        expected[i][2] = w;
        expected[i][3] = h;
        emit_solid_rect(ir_arena, x, y, w, h, (FlColor) { (u8)rng, (u8)(rng >> 8), (u8)(rng >> 16), 255 });
    }
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == SWEEP_COUNT, "translate sweep: every solid rect should translate");
    for (u32 i = 0; i < SWEEP_COUNT; i++) {
        CHECK(list.cmds[i].rect[0] == expected[i][0] && list.cmds[i].rect[1] == expected[i][1]
                  && list.cmds[i].rect[2] == expected[i][2] && list.cmds[i].rect[3] == expected[i][3],
              "translate sweep: command order/fields should match emission order");
        CHECK(list.aabbs[i].x0 == expected[i][0] && list.aabbs[i].y0 == expected[i][1]
                  && list.aabbs[i].x1 == expected[i][0] + expected[i][2]
                  && list.aabbs[i].y1 == expected[i][1] + expected[i][3],
              "translate sweep: AABBs should stay parallel to commands");
    }

    // A zero-byte_size header must break both IR walks (the draw-bound prescan and the translation pass)
    // instead of spinning forever.
    start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, 1.0f, 2.0f, 3.0f, 4.0f, red);
    FlCmdHeader* bad = fl_ir_header(ir_arena, FlCmd_FillRect, (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdFillRect)));
    arena_alloc_zero(ir_arena, FlCmdFillRect); // payload present but never reached past the guard
    bad->byte_size = 0;
    emit_solid_rect(ir_arena, 5.0f, 6.0f, 7.0f, 8.0f, red); // after the corruption: must be dropped
    MalformedTranslateJob job = { .out_arena = out_arena, .commands = start, .bytes = ir_bytes(ir_arena, start) };
    Thread worker;
    CHECK(os_thread_create(&worker, malformed_translate_thread, &job), "malformed guard: worker thread must start");
    bool finished = false;
    for (int i = 0; i < 5000 && !finished; i++) { // ~5s bound; the passing path finishes on the first poll
        finished = atomic_load(&job.done);
        if (!finished) {
            sleep_ms(1);
        }
    }
    CHECK(finished, "malformed guard: a zero-byte_size header must terminate the IR walks, not hang them");
    os_thread_join(&worker);
    CHECK(job.list.count == 1, "malformed guard: records ahead of the zero-size header still translate");
    CHECK(job.list.cmds[0].rect[0] == 1.0f && job.list.cmds[0].rect[1] == 2.0f,
          "malformed guard: the surviving record is the one before the corruption");

    fl_arena_destroy(out_arena);
    fl_arena_destroy(ir_arena);
    printf("gpu_smoke_test: translator unit tests PASS\n");
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Effect annotations: a record's optional FlEffect expands the classification AABB to cover where the effect
// reaches and packs the effect parameters into every command the record commits. Decorative only - it never
// changes which command survives beyond reaching more tiles. Still Section 1 - pure CPU, no GL.

static u32 effect_kind_of(const GpuDrawCmd* cmd) {
    return (cmd->flags & GPU_DRAW_EFFECT_MASK) >> GPU_DRAW_EFFECT_SHIFT;
}

static int run_effect_translator_tests(void) {
    FlArena* ir_arena = arena_new();
    FlArena* out_arena = arena_new();

    void* start = ir_begin(ir_arena);
    emit_solid_rect(ir_arena, 100.0f, 100.0f, 40.0f, 40.0f, red);
    GpuDrawList list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 1, "effect: a plain rect translates");
    CHECK(effect_kind_of(&list.cmds[0]) == 0, "effect: a rect with no effect carries no effect kind");
    CHECK(list.cmds[0].effect[0] == 0 && list.cmds[0].effect[1] == 0 && list.cmds[0].effect[2] == 0
              && list.cmds[0].effect[3] == 0,
          "effect: a rect with no effect leaves the effect words zero");
    CHECK(list.aabbs[0].x0 == 100.0f && list.aabbs[0].y0 == 100.0f && list.aabbs[0].x1 == 140.0f
              && list.aabbs[0].y1 == 140.0f,
          "effect: a rect with no effect bounds exactly the rect");

    start = ir_rewind_begin(ir_arena);
    const FlColor shadow_color = { 30, 220, 40, 255 };
    FlEffect shadow
        = { .kind = FlEffectKind_DropShadow, .color = shadow_color, .off_x = 20.0f, .off_y = 10.0f, .blur = 5.0f };
    emit_rect_effect(ir_arena, 100.0f, 100.0f, 40.0f, 40.0f, red, &shadow);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 1, "shadow: the shadowed rect translates");
    CHECK(effect_kind_of(&list.cmds[0]) == 1, "shadow: the flags carry effect kind 1 (drop shadow)");
    CHECK(bits_f32(list.cmds[0].effect[0]) == 20.0f && bits_f32(list.cmds[0].effect[1]) == 10.0f
              && bits_f32(list.cmds[0].effect[2]) == 5.0f,
          "shadow: offset and blur round-trip into the effect words");
    CHECK(list.cmds[0].effect[3] == packed_rgba(shadow_color, 255),
          "shadow: the effect color packs like any other color");
    // rect [100,100,140,140]; shadow reach = [115,105,165,155]; union = [100,100,165,155].
    CHECK(list.aabbs[0].x0 == 100.0f && list.aabbs[0].y0 == 100.0f && list.aabbs[0].x1 == 165.0f
              && list.aabbs[0].y1 == 155.0f,
          "shadow: the AABB unions the rect with its offset+blur reach");

    start = ir_rewind_begin(ir_arena);
    FlEffect glow = { .kind = FlEffectKind_Glow, .color = shadow_color, .off_x = 20.0f, .off_y = 10.0f, .blur = 8.0f };
    emit_rect_effect(ir_arena, 100.0f, 100.0f, 40.0f, 40.0f, red, &glow);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(effect_kind_of(&list.cmds[0]) == 2, "glow: the flags carry effect kind 2 (glow)");
    CHECK(list.aabbs[0].x0 == 92.0f && list.aabbs[0].y0 == 92.0f && list.aabbs[0].x1 == 148.0f
              && list.aabbs[0].y1 == 148.0f,
          "glow: the AABB dilates the rect by the blur, ignoring the offset");

    start = ir_rewind_begin(ir_arena);
    FlEffect none
        = { .kind = FlEffectKind_None, .color = { 1, 2, 3, 4 }, .off_x = 50.0f, .off_y = 50.0f, .blur = 9.0f };
    emit_rect_effect(ir_arena, 100.0f, 100.0f, 40.0f, 40.0f, red, &none);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(effect_kind_of(&list.cmds[0]) == 0 && list.cmds[0].effect[3] == 0,
          "effect: a kind-0 (none) effect is ignored even with the flag set");
    CHECK(list.aabbs[0].x1 == 140.0f && list.aabbs[0].y1 == 140.0f, "effect: a kind-0 (none) effect expands nothing");

    start = ir_rewind_begin(ir_arena);
    FlEffect sharp
        = { .kind = FlEffectKind_DropShadow, .color = { 0, 0, 0, 255 }, .off_x = 6.0f, .off_y = -4.0f, .blur = -5.0f };
    emit_rect_effect(ir_arena, 100.0f, 100.0f, 40.0f, 40.0f, red, &sharp);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(bits_f32(list.cmds[0].effect[2]) == 0.0f, "shadow: a negative blur clamps to zero in the words");
    // reach = [106,96,146,136]; union with [100,100,140,140] = [100,96,146,140].
    CHECK(list.aabbs[0].x0 == 100.0f && list.aabbs[0].y0 == 96.0f && list.aabbs[0].x1 == 146.0f
              && list.aabbs[0].y1 == 140.0f,
          "shadow: a clamped blur shifts the reach by the offset only");

    start = ir_rewind_begin(ir_arena);
    emit_rect_effect(ir_arena, 100.0f, 100.0f, 40.0f, 40.0f, red, &shadow);
    list = translate(out_arena, ir_arena, start, 128, NULL, NULL);
    CHECK((list.cmds[0].effect[3] >> 24) == ((255u * 128u + 127u) / 255u),
          "shadow: global_alpha folds into the effect color's alpha");

    start = ir_rewind_begin(ir_arena);
    fl_ir_clip_push(ir_arena, (FlRect) { 200, 100, 100, 100 }); // clip [200,100,300,200]
    FlEffect reach
        = { .kind = FlEffectKind_DropShadow, .color = { 0, 0, 0, 255 }, .off_x = 80.0f, .off_y = 0.0f, .blur = 4.0f };
    emit_rect_effect(ir_arena, 130.0f, 120.0f, 20.0f, 20.0f, red, &reach); // body [130,120,150,140], left of clip
    fl_ir_clip_pop(ir_arena);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 1, "shadow: a body outside the clip still survives when its shadow reaches in");
    // union(body, reach) = union([130,120,150,140], [206,116,234,144]) = [130,116,234,144];
    // intersect clip [200,100,300,200] = [200,116,234,144].
    CHECK(list.aabbs[0].x0 == 200.0f && list.aabbs[0].y0 == 116.0f && list.aabbs[0].x1 == 234.0f
              && list.aabbs[0].y1 == 144.0f,
          "shadow: the surviving AABB is the shadow reach intersected with the clip");

    start = ir_rewind_begin(ir_arena);
    const FlColor bar_color = { 10, 20, 30, 255 };
    FlEffect bar_glow = { .kind = FlEffectKind_Glow, .color = bar_color, .blur = 3.0f };
    FlBar* bars = fl_ir_bars(ir_arena, 200.0f, red, 3, &bar_glow);
    bars[0] = (FlBar) { 10.0f, 8.0f, 20.0f };
    bars[1] = (FlBar) { 40.0f, 8.0f, 30.0f };
    bars[2] = (FlBar) { 70.0f, 8.0f, 40.0f };
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 3, "bar glow: every bar of the set translates");
    for (u32 i = 0; i < 3; i++) {
        CHECK(effect_kind_of(&list.cmds[i]) == 2 && list.cmds[i].effect[3] == packed_rgba(bar_color, 255),
              "bar glow: the one effect stamps onto every bar the set commits");
    }

    // record_effect must locate the trailing FlEffect for every kind the translator renders - a wrong per-kind
    // payload size would mislocate it. The round-tripped color word proves it was found at the right offset.
    const FlColor fx_color = { 12, 34, 56, 255 };
    const u32 fx_packed = packed_rgba(fx_color, 255);

    start = ir_rewind_begin(ir_arena);
    FlEffect arc_fx
        = { .kind = FlEffectKind_DropShadow, .color = fx_color, .off_x = 4.0f, .off_y = 0.0f, .blur = 2.0f };
    fl_ir_arc(ir_arena, 200.0f, 200.0f, 40.0f, 8.0f, 0.0f, 0.25f * TAU, red, 0, &arc_fx);
    FlEffect circle_fx = { .kind = FlEffectKind_Glow, .color = fx_color, .blur = 3.0f };
    fl_ir_circle(ir_arena, 200.0f, 200.0f, 20.0f, red, red, 0.0f, &circle_fx);
    FlEffect diamond_fx
        = { .kind = FlEffectKind_DropShadow, .color = fx_color, .off_x = 5.0f, .off_y = 5.0f, .blur = 1.0f };
    fl_ir_diamond(ir_arena, 200.0f, 200.0f, 15.0f, 15.0f, red, red, 0.0f, &diamond_fx);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 3, "effect kinds: arc, circle, and diamond each translate");
    CHECK(effect_kind_of(&list.cmds[0]) == 1 && list.cmds[0].effect[3] == fx_packed,
          "effect kinds: an arc's effect is located past its payload and stamped");
    CHECK(effect_kind_of(&list.cmds[1]) == 2 && list.cmds[1].effect[3] == fx_packed,
          "effect kinds: a circle's effect is located past its payload and stamped");
    CHECK(effect_kind_of(&list.cmds[2]) == 1 && list.cmds[2].effect[3] == fx_packed,
          "effect kinds: a diamond's effect is located past its payload and stamped");

    // Border is core-tier (its builder has no effect parameter); assemble the record directly with the effect
    // at the tail, as the painter's annotation path would.
    start = ir_rewind_begin(ir_arena);
    FlEffect border_fx
        = { .kind = FlEffectKind_DropShadow, .color = fx_color, .off_x = 3.0f, .off_y = 3.0f, .blur = 2.0f };
    FlCmdHeader* bheader = fl_ir_header(
        ir_arena, FlCmd_Border, (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdBorder)) + fl_ir_effect_bytes(&border_fx));
    FlCmdBorder* bd = arena_alloc_zero(ir_arena, FlCmdBorder);
    bd->rect = (FlRect) { 100, 100, 60, 40 };
    bd->w_l = 4.0f;
    bd->w_r = 4.0f;
    bd->w_t = 4.0f;
    bd->w_b = 4.0f;
    bd->color = red;
    fl_ir_append_effect(ir_arena, bheader, &border_fx);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 1 && list.cmds[0].kind == GpuDraw_Border, "effect kinds: the border translates");
    CHECK(effect_kind_of(&list.cmds[0]) == 1 && list.cmds[0].effect[3] == fx_packed,
          "effect kinds: a border's effect is located past its payload and stamped");

    // Image is core-tier too, and needs an atlas entry to translate.
    GpuImageAtlas fx_atlas;
    gpu_image_atlas_init(&fx_atlas, out_arena);
    gpu_image_atlas_place(&fx_atlas, 42, 16, 16);
    start = ir_rewind_begin(ir_arena);
    FlEffect image_fx = { .kind = FlEffectKind_Glow, .color = fx_color, .blur = 4.0f };
    FlCmdHeader* iheader = fl_ir_header(
        ir_arena, FlCmd_Image, (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdImage)) + fl_ir_effect_bytes(&image_fx));
    FlCmdImage* im = arena_alloc_zero(ir_arena, FlCmdImage);
    im->source = (FlImageSource) { .kind = FlImageKind_Static, .handle = 42 };
    im->dst = (FlRect) { 100, 100, 16, 16 };
    im->tint = white;
    fl_ir_append_effect(ir_arena, iheader, &image_fx);
    list = translate(out_arena, ir_arena, start, 255, &fx_atlas, NULL);
    CHECK(list.count == 1 && list.cmds[0].kind == GpuDraw_Image, "effect kinds: the atlas image translates");
    CHECK(effect_kind_of(&list.cmds[0]) == 2 && list.cmds[0].effect[3] == fx_packed,
          "effect kinds: an image's effect is located past its payload and stamped");

    fl_arena_destroy(out_arena);
    fl_arena_destroy(ir_arena);
    printf("gpu_smoke_test: effect translator unit tests PASS\n");
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Optional-tier shape translation: arcs, circles, diamonds, and bar sets become their SDF draw kinds (each bar
// of a set reuses GpuDraw_Rect). Still Section 1 - pure CPU, no GL.

static bool near_f32(f32 a, f32 b, f32 tolerance) {
    return fabsf(a - b) <= tolerance;
}

static int run_shape_translator_tests(void) {
    FlArena* ir_arena = arena_new();
    FlArena* out_arena = arena_new();
    const u32 packed_red = 200u | (40u << 8) | (50u << 16) | (255u << 24);
    const u32 packed_green = 40u | (200u << 8) | (60u << 16) | (255u << 24);

    // A quarter ring from 12 to 3 o'clock. The encoding carries center + outer/inner radius and the wedge
    // frame - bisector cos/sin and half-aperture sin/cos, all four 1/sqrt(2) for this quarter.
    void* start = ir_begin(ir_arena);
    fl_ir_arc(ir_arena, 100.0f, 100.0f, 50.0f, 20.0f, 0.0f, 0.25f * TAU, red, 0, NULL);
    GpuDrawList list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 1, "arc: one record translates to one command");
    CHECK(list.cmds[0].kind == GpuDraw_Arc, "arc: kind is Arc");
    CHECK(list.cmds[0].flags == 0, "arc: butt caps and a partial sweep carry no flags");
    CHECK(list.cmds[0].rect[0] == 100.0f && list.cmds[0].rect[1] == 100.0f && list.cmds[0].rect[2] == 50.0f
              && list.cmds[0].rect[3] == 30.0f,
          "arc: rect packs cx, cy, outer radius, inner radius (outer minus thickness)");
    for (int c = 0; c < 4; c++) {
        CHECK(near_f32(list.cmds[0].radius[c], 0.70710678f, 1e-4f),
              "arc: a 12-to-3 quarter has bisector and half-aperture both at an eighth turn");
    }
    CHECK(list.cmds[0].color[0] == packed_red, "arc: the color packs like every other kind");
    CHECK(near_f32(list.aabbs[0].x0, 100.0f, 1e-3f) && near_f32(list.aabbs[0].y0, 50.0f, 1e-3f)
              && near_f32(list.aabbs[0].x1, 150.0f, 1e-3f) && near_f32(list.aabbs[0].y1, 100.0f, 1e-3f),
          "arc: the AABB bounds the swept quadrant, not the full circle");

    list = translate(out_arena, ir_arena, start, 128, NULL, NULL);
    CHECK((list.cmds[0].color[0] >> 24) == 128u, "arc: global_alpha folds into the packed color");

    start = ir_rewind_begin(ir_arena);
    fl_ir_arc(ir_arena, 100.0f, 100.0f, 50.0f, 20.0f, 0.0f, 0.25f * TAU, red, 0, NULL);
    fl_ir_arc(ir_arena, 100.0f, 100.0f, 50.0f, 20.0f, 0.25f * TAU, -0.25f * TAU, red, 0, NULL);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 2, "arc: both sweep directions translate");
    CHECK(memcmp(&list.cmds[0], &list.cmds[1], sizeof(GpuDrawCmd)) == 0,
          "arc: a negative sweep encodes exactly like its positive-clockwise equivalent");
    CHECK(memcmp(&list.aabbs[0], &list.aabbs[1], sizeof(GpuAabb)) == 0,
          "arc: a negative sweep bounds exactly like its positive-clockwise equivalent");

    // Round caps widen the AABB by the cap discs (centerline radius 40, cap radius 10).
    start = ir_rewind_begin(ir_arena);
    fl_ir_arc(ir_arena, 100.0f, 100.0f, 50.0f, 20.0f, 0.0f, 0.25f * TAU, red, FlArcFlags_round_caps, NULL);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.cmds[0].flags == GPU_DRAW_ARC_ROUND_CAPS, "arc: round caps map to the flag");
    CHECK(near_f32(list.aabbs[0].x0, 90.0f, 1e-3f) && near_f32(list.aabbs[0].y1, 110.0f, 1e-3f),
          "arc: round caps widen the AABB by the cap discs at both ends");
    CHECK(near_f32(list.aabbs[0].y0, 50.0f, 1e-3f) && near_f32(list.aabbs[0].x1, 150.0f, 1e-3f),
          "arc: the outer cardinal extremes still bound the capped sector");

    start = ir_rewind_begin(ir_arena);
    fl_ir_arc(ir_arena, 100.0f, 100.0f, 50.0f, 8.0f, 1.0f, TAU + 1.0f, red, 0, NULL);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.cmds[0].flags == GPU_DRAW_ARC_FULL, "arc: a sweep past a full turn sets the FULL flag");
    CHECK(list.aabbs[0].x0 == 50.0f && list.aabbs[0].y0 == 50.0f && list.aabbs[0].x1 == 150.0f
              && list.aabbs[0].y1 == 150.0f,
          "arc: a full ring bounds the whole circle");

    start = ir_rewind_begin(ir_arena);
    fl_ir_arc(ir_arena, 100.0f, 100.0f, 50.0f, 80.0f, 0.0f, 0.25f * TAU, red, 0, NULL);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.cmds[0].rect[3] == 0.0f, "arc: thickness past the radius clamps the inner radius to 0");

    start = ir_rewind_begin(ir_arena);
    fl_ir_arc(ir_arena, 100.0f, 100.0f, 0.0f, 8.0f, 0.0f, 1.0f, red, 0, NULL);
    fl_ir_arc(ir_arena, 100.0f, 100.0f, 50.0f, 0.0f, 0.0f, 1.0f, red, 0, NULL);
    fl_ir_arc(ir_arena, 100.0f, 100.0f, 50.0f, 8.0f, 0.0f, 0.0f, red, 0, NULL);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 0, "arc: zero radius, thickness, or sweep drops the record");

    start = ir_rewind_begin(ir_arena);
    fl_ir_clip_push(ir_arena, (FlRect) { 0, 0, 60, 60 });
    fl_ir_arc(ir_arena, 100.0f, 100.0f, 50.0f, 20.0f, 0.0f, 0.25f * TAU, red, 0, NULL);        // top-right quadrant
    fl_ir_arc(ir_arena, 100.0f, 100.0f, 50.0f, 20.0f, 0.75f * TAU, 0.25f * TAU, red, 0, NULL); // 9-to-12
    fl_ir_clip_pop(ir_arena);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 1, "arc: a sector wholly outside the clip drops even though its circle overlaps");
    CHECK(near_f32(list.aabbs[0].x0, 50.0f, 1e-3f) && near_f32(list.aabbs[0].y0, 50.0f, 1e-3f)
              && near_f32(list.aabbs[0].x1, 60.0f, 1e-3f) && near_f32(list.aabbs[0].y1, 60.0f, 1e-3f),
          "arc: the surviving sector's AABB clamps to the clip");

    start = ir_rewind_begin(ir_arena);
    fl_ir_circle(ir_arena, 40.0f, 60.0f, 10.0f, red, green, 3.0f, NULL);
    fl_ir_circle(ir_arena, 40.0f, 60.0f, 10.0f, red, green, -5.0f, NULL); // negative width clamps to 0
    fl_ir_circle(ir_arena, 40.0f, 60.0f, 10.0f, red, green, 10.0f, NULL); // border consumes the disc
    fl_ir_circle(ir_arena, 40.0f, 60.0f, 0.0f, red, green, 0.0f, NULL);   // degenerate: dropped
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 3, "circle: a zero radius drops the record");
    CHECK(list.cmds[0].kind == GpuDraw_Circle, "circle: kind is Circle");
    CHECK(list.cmds[0].rect[0] == 40.0f && list.cmds[0].rect[1] == 60.0f && list.cmds[0].rect[2] == 10.0f
              && list.cmds[0].rect[3] == 3.0f,
          "circle: rect packs cx, cy, radius, border width");
    CHECK(list.cmds[0].color[0] == packed_red && list.cmds[0].color[1] == packed_green,
          "circle: fill then border color");
    CHECK(list.aabbs[0].x0 == 30.0f && list.aabbs[0].y0 == 50.0f && list.aabbs[0].x1 == 50.0f
              && list.aabbs[0].y1 == 70.0f,
          "circle: the AABB is center +- radius");
    CHECK(list.cmds[1].rect[3] == 0.0f, "circle: a negative border width clamps to 0");
    CHECK(list.cmds[2].rect[3] == 0.0f && list.cmds[2].color[0] == packed_green,
          "circle: a border consuming the disc normalizes to a borderless border-color disc");

    // Diamond border normalization is against the inradius (rx*ry/len = 4.8 here).
    start = ir_rewind_begin(ir_arena);
    fl_ir_diamond(ir_arena, 40.0f, 60.0f, 8.0f, 6.0f, red, green, 2.0f, NULL);
    fl_ir_diamond(ir_arena, 40.0f, 60.0f, 8.0f, 6.0f, red, green, 5.0f, NULL); // past the inradius
    fl_ir_diamond(ir_arena, 40.0f, 60.0f, 0.0f, 6.0f, red, green, 0.0f, NULL); // degenerate: dropped
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 2, "diamond: a zero half-extent drops the record");
    CHECK(list.cmds[0].kind == GpuDraw_Diamond, "diamond: kind is Diamond");
    CHECK(list.cmds[0].rect[0] == 40.0f && list.cmds[0].rect[1] == 60.0f && list.cmds[0].rect[2] == 8.0f
              && list.cmds[0].rect[3] == 6.0f,
          "diamond: rect packs cx, cy, rx, ry");
    CHECK(list.cmds[0].radius[0] == 2.0f, "diamond: the border width rides in radius[0]");
    CHECK(list.cmds[0].color[0] == packed_red && list.cmds[0].color[1] == packed_green,
          "diamond: fill then border color");
    CHECK(list.aabbs[0].x0 == 32.0f && list.aabbs[0].y0 == 54.0f && list.aabbs[0].x1 == 48.0f
              && list.aabbs[0].y1 == 66.0f,
          "diamond: the AABB is center +- half-extents");
    CHECK(list.cmds[1].radius[0] == 0.0f && list.cmds[1].color[0] == packed_green,
          "diamond: a border past the inradius normalizes to a borderless border-color rhombus");

    // Each bar expands to one solid rect command off the shared baseline at y = 100.
    start = ir_rewind_begin(ir_arena);
    FlBar* bars = fl_ir_bars(ir_arena, 100.0f, red, 5, NULL);
    bars[0] = (FlBar) { 10.0f, 5.0f, 20.0f };  // up: y 80..100
    bars[1] = (FlBar) { 20.0f, 5.0f, -15.0f }; // down: y 100..115
    bars[2] = (FlBar) { 30.0f, 0.0f, 10.0f };  // zero width: dropped
    bars[3] = (FlBar) { 40.0f, 5.0f, 0.0f };   // zero height: dropped
    bars[4] = (FlBar) { 50.0f, 5.0f, 30.0f };  // up: y 70..100
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 3, "bars: degenerate bars drop bar-by-bar, the rest expand");
    const f32 expected_bars[3][4] = { { 10, 80, 5, 20 }, { 20, 100, 5, 15 }, { 50, 70, 5, 30 } };
    for (int b = 0; b < 3; b++) {
        CHECK(list.cmds[b].kind == GpuDraw_Rect, "bars: every bar reuses the rect raster path");
        CHECK(list.cmds[b].rect[0] == expected_bars[b][0] && list.cmds[b].rect[1] == expected_bars[b][1]
                  && list.cmds[b].rect[2] == expected_bars[b][2] && list.cmds[b].rect[3] == expected_bars[b][3],
              "bars: heights extend up from the baseline, negative ones down");
        CHECK(list.aabbs[b].x0 == expected_bars[b][0] && list.aabbs[b].y0 == expected_bars[b][1]
                  && list.aabbs[b].x1 == expected_bars[b][0] + expected_bars[b][2]
                  && list.aabbs[b].y1 == expected_bars[b][1] + expected_bars[b][3],
              "bars: each bar carries its own AABB");
        for (int c = 0; c < 4; c++) {
            CHECK(list.cmds[b].color[c] == packed_red, "bars: the set color packs into all four corners");
            CHECK(list.cmds[b].radius[c] == 0.0f, "bars: bars are square-cornered rects");
        }
    }

    start = ir_rewind_begin(ir_arena);
    fl_ir_clip_push(ir_arena, (FlRect) { 0, 0, 18, 200 });
    bars = fl_ir_bars(ir_arena, 100.0f, red, 3, NULL);
    bars[0] = (FlBar) { 10.0f, 5.0f, 20.0f };  // wholly inside
    bars[1] = (FlBar) { 15.0f, 10.0f, 20.0f }; // straddles the clip edge at x = 18
    bars[2] = (FlBar) { 30.0f, 5.0f, 20.0f };  // wholly outside: dropped
    fl_ir_clip_pop(ir_arena);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 2, "bars: a bar wholly outside the clip drops; the rest survive");
    CHECK(list.aabbs[1].x1 == 18.0f, "bars: a straddling bar's AABB clamps to the clip");
    CHECK(list.cmds[1].clip == 1, "bars: every bar carries the set's clip index");

    fl_arena_destroy(out_arena);
    fl_arena_destroy(ir_arena);
    printf("gpu_smoke_test: shape translator unit tests PASS\n");
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Polyline translation: a polyline expands to one GpuDraw_Polyline command per segment - a stroke capsule
// (line-aware classification) or, when FILLED, a fill-to-baseline trapezoid emitted before the strokes.
// Still Section 1 - pure CPU, no GL.

static int run_polyline_translator_tests(void) {
    FlArena* ir_arena = arena_new();
    FlArena* out_arena = arena_new();
    const u32 packed_red = packed_rgba(red, 255);
    const u32 packed_green = packed_rgba(green, 255);

    void* start = ir_begin(ir_arena);
    FlPolyPoint* pts = fl_ir_polyline(ir_arena, red, green, 2.0f, 0.0f, 0, 3, NULL);
    pts[0] = (FlPolyPoint) { 10.0f, 10.0f, 1.0f };
    pts[1] = (FlPolyPoint) { 30.0f, 20.0f, 0.5f };
    pts[2] = (FlPolyPoint) { 50.0f, 10.0f, 0.25f };
    GpuDrawList list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 2, "polyline: a 3-point open stroke expands to two segments");
    CHECK(list.cmds[0].kind == GpuDraw_Polyline && (list.cmds[0].flags & GPU_DRAW_POLY_STROKE) != 0,
          "polyline: a stroke segment is a GpuDraw_Polyline with the stroke flag set");
    CHECK(list.cmds[0].rect[0] == 10.0f && list.cmds[0].rect[1] == 10.0f && list.cmds[0].rect[2] == 30.0f
              && list.cmds[0].rect[3] == 20.0f,
          "polyline: a stroke segment's rect is its two endpoints");
    CHECK(list.cmds[0].radius[0] == 1.0f, "polyline: radius.x is the stroke half-width");
    CHECK(list.cmds[0].radius[1] == 1.0f && list.cmds[0].radius[2] == 0.5f,
          "polyline: radius.yz are the per-endpoint intensities");
    CHECK(list.cmds[0].color[0] == packed_red, "polyline: color.x is the packed stroke color");
    CHECK(list.cmds[1].rect[0] == 30.0f && list.cmds[1].rect[2] == 50.0f && list.cmds[1].radius[2] == 0.25f,
          "polyline: the second segment continues from the shared vertex");
    // The capsule AABB is the segment bbox dilated by the half-width plus the AA ramp (here 1 + 0.5).
    CHECK(list.aabbs[0].x0 == 10.0f - 1.5f && list.aabbs[0].y0 == 10.0f - 1.5f && list.aabbs[0].x1 == 30.0f + 1.5f
              && list.aabbs[0].y1 == 20.0f + 1.5f,
          "polyline: a stroke segment's AABB is the capsule bound, not the whole polyline");

    // FILLED: 3 points -> 2 fill trapezoids (first, behind the stroke) + 2 stroke capsules.
    fl_arena_rewind(out_arena);
    start = ir_rewind_begin(ir_arena);
    pts = fl_ir_polyline(ir_arena, red, green, 2.0f, 100.0f, FlPolylineFlags_filled, 3, NULL);
    pts[0] = (FlPolyPoint) { 10.0f, 10.0f, 1.0f };
    pts[1] = (FlPolyPoint) { 30.0f, 20.0f, 1.0f };
    pts[2] = (FlPolyPoint) { 50.0f, 10.0f, 1.0f };
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 4, "polyline filled: a 3-point fill expands to two trapezoids and two strokes");
    CHECK((list.cmds[0].flags & GPU_DRAW_POLY_STROKE) == 0 && (list.cmds[1].flags & GPU_DRAW_POLY_STROKE) == 0,
          "polyline filled: the fill trapezoids come first, behind the stroke");
    CHECK(list.cmds[0].kind == GpuDraw_Polyline && list.cmds[0].color[0] == packed_green
              && list.cmds[0].radius[3] == 100.0f,
          "polyline filled: a fill segment carries the fill color and the baseline_y");
    CHECK((list.cmds[2].flags & GPU_DRAW_POLY_STROKE) != 0 && (list.cmds[3].flags & GPU_DRAW_POLY_STROKE) != 0,
          "polyline filled: the stroke capsules come after the fill");
    // A fill trapezoid's AABB spans the segment's x and reaches the baseline in y (here y 10..100, +0.5 AA).
    CHECK(list.aabbs[0].x0 == 10.0f && list.aabbs[0].x1 == 30.0f && list.aabbs[0].y0 == 10.0f - 0.5f
              && list.aabbs[0].y1 == 100.0f + 0.5f,
          "polyline filled: a fill trapezoid's AABB reaches the baseline");

    fl_arena_rewind(out_arena);
    start = ir_rewind_begin(ir_arena);
    pts = fl_ir_polyline(ir_arena, red, green, 2.0f, 0.0f, FlPolylineFlags_closed, 3, NULL);
    pts[0] = (FlPolyPoint) { 10.0f, 10.0f, 1.0f };
    pts[1] = (FlPolyPoint) { 30.0f, 20.0f, 1.0f };
    pts[2] = (FlPolyPoint) { 50.0f, 10.0f, 1.0f };
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 3, "polyline closed: the closing edge adds a third segment");
    CHECK(list.cmds[2].rect[0] == 50.0f && list.cmds[2].rect[1] == 10.0f && list.cmds[2].rect[2] == 10.0f
              && list.cmds[2].rect[3] == 10.0f,
          "polyline closed: the closing segment runs last -> first");

    fl_arena_rewind(out_arena);
    start = ir_rewind_begin(ir_arena);
    pts = fl_ir_polyline(ir_arena, red, green, 2.0f, 0.0f, 0, 1, NULL); // one point: no segment
    pts[0] = (FlPolyPoint) { 10.0f, 10.0f, 1.0f };
    pts = fl_ir_polyline(ir_arena, red, green, 0.0f, 0.0f, 0, 3, NULL); // width 0, not filled: nothing
    pts[0] = (FlPolyPoint) { 10.0f, 10.0f, 1.0f };
    pts[1] = (FlPolyPoint) { 30.0f, 20.0f, 1.0f };
    pts[2] = (FlPolyPoint) { 50.0f, 10.0f, 1.0f };
    pts = fl_ir_polyline(ir_arena, red, green, 2.0f, 0.0f, 0, 3, NULL); // a zero-length middle segment drops
    pts[0] = (FlPolyPoint) { 10.0f, 10.0f, 1.0f };
    pts[1] = (FlPolyPoint) { 10.0f, 10.0f, 1.0f }; // coincident with pts[0]
    pts[2] = (FlPolyPoint) { 50.0f, 10.0f, 1.0f };
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 1, "polyline degenerate: only the one non-zero-length segment of the third polyline survives");
    CHECK(list.cmds[0].rect[0] == 10.0f && list.cmds[0].rect[2] == 50.0f,
          "polyline degenerate: the surviving segment skips the coincident pair");

    fl_arena_rewind(out_arena);
    start = ir_rewind_begin(ir_arena);
    pts = fl_ir_polyline(ir_arena, red, green, 2.0f, 100.0f, FlPolylineFlags_filled, 2, NULL);
    pts[0] = (FlPolyPoint) { 20.0f, 10.0f, 1.0f };
    pts[1] = (FlPolyPoint) { 20.0f, 40.0f, 1.0f }; // vertical
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 1 && (list.cmds[0].flags & GPU_DRAW_POLY_STROKE) != 0,
          "polyline filled: a vertical edge has a stroke but no fill trapezoid");

    fl_arena_rewind(out_arena);
    start = ir_rewind_begin(ir_arena);
    pts = fl_ir_polyline(ir_arena, red, green, 0.0f, 100.0f, FlPolylineFlags_filled, 2, NULL);
    pts[0] = (FlPolyPoint) { 10.0f, 10.0f, 1.0f };
    pts[1] = (FlPolyPoint) { 50.0f, 20.0f, 1.0f };
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 1 && (list.cmds[0].flags & GPU_DRAW_POLY_STROKE) == 0 && list.cmds[0].color[0] == packed_green,
          "polyline filled: a zero-width polyline keeps its fill trapezoid and emits no stroke");

    fl_arena_destroy(out_arena);
    fl_arena_destroy(ir_arena);
    printf("gpu_smoke_test: polyline translator unit tests PASS\n");
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Border translation: the outer rect, the clamped outer radii, and the four side widths cross to the shader
// verbatim - no inner radius anywhere in the encoding (the IR contract: the backend derives the inner
// geometry). Still Section 1 - pure CPU, no GL.

static int run_border_translator_tests(void) {
    FlArena* ir_arena = arena_new();
    FlArena* out_arena = arena_new();
    const u32 packed_red = 200u | (40u << 8) | (50u << 16) | (255u << 24);
    const u32 packed_green = 40u | (200u << 8) | (60u << 16) | (255u << 24);

    void* start = ir_begin(ir_arena);
    fl_ir_border(ir_arena, (FlRect) { 10, 20, 60, 40 }, 1.5f, 2.0f, 3.0f, 4.25f, (FlCornerRadius) { 5, 6, 7, 8 }, red);
    GpuDrawList list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 1, "border: one record translates to one command");
    CHECK(list.cmds[0].kind == GpuDraw_Border, "border: kind is Border");
    CHECK(list.cmds[0].rect[0] == 10.0f && list.cmds[0].rect[1] == 20.0f && list.cmds[0].rect[2] == 60.0f
              && list.cmds[0].rect[3] == 40.0f,
          "border: rect packs the outer rect unchanged");
    CHECK(list.cmds[0].radius[0] == 5.0f && list.cmds[0].radius[1] == 6.0f && list.cmds[0].radius[2] == 7.0f
              && list.cmds[0].radius[3] == 8.0f,
          "border: in-range outer radii pass through in TL TR BL BR order");
    CHECK(list.cmds[0].color[0] == packed_red, "border: the color packs like every other kind");
    CHECK(bits_f32(list.cmds[0].color[1]) == 1.5f && bits_f32(list.cmds[0].color[2]) == 2.0f
              && bits_f32(list.cmds[0].color[3]) == 3.0f && bits_f32(list.cmds[0].aux) == 4.25f,
          "border: side widths cross bit-verbatim in left/right/top/bottom order — no inner radius is encoded");
    CHECK(list.aabbs[0].x0 == 10.0f && list.aabbs[0].y0 == 20.0f && list.aabbs[0].x1 == 70.0f
              && list.aabbs[0].y1 == 60.0f,
          "border: the AABB is the outer rect");

    list = translate(out_arena, ir_arena, start, 128, NULL, NULL);
    CHECK((list.cmds[0].color[0] >> 24) == 128u, "border: global_alpha folds into the packed color");

    start = ir_rewind_begin(ir_arena);
    fl_ir_border(ir_arena, (FlRect) { 0, 0, 40, 20 }, 2, 2, 2, 2, (FlCornerRadius) { 30.0f, 5.0f, -3.0f, 10.0f }, red);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.cmds[0].radius[0] == 10.0f && list.cmds[0].radius[1] == 5.0f && list.cmds[0].radius[2] == 0.0f
              && list.cmds[0].radius[3] == 10.0f,
          "border: outer radii clamp like the fill path (oversized to half the smaller dimension, negative to 0)");

    start = ir_rewind_begin(ir_arena);
    fl_ir_border(ir_arena, (FlRect) { 0, 0, 50, 50 }, -3.0f, 2.0f, 0.0f, 0.0f, (FlCornerRadius) { 0 }, red);
    fl_ir_border(ir_arena, (FlRect) { 0, 0, 50, 50 }, 0, 0, 0, 0, (FlCornerRadius) { 4, 4, 4, 4 }, red);
    fl_ir_border(ir_arena, (FlRect) { 0, 0, 50, 50 }, -1.0f, -1.0f, -1.0f, -1.0f, (FlCornerRadius) { 0 }, red);
    fl_ir_border(ir_arena, (FlRect) { 0, 0, 0, 50 }, 2, 2, 2, 2, (FlCornerRadius) { 0 }, red);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 1, "border: all-zero/negative widths and empty rects drop");
    CHECK(bits_f32(list.cmds[0].color[1]) == 0.0f && bits_f32(list.cmds[0].color[2]) == 2.0f,
          "border: a negative width clamps to 0 without disturbing the others");

    // Either axis alone is enough to consume the rect.
    start = ir_rewind_begin(ir_arena);
    fl_ir_border(ir_arena, (FlRect) { 0, 0, 40, 40 }, 20.0f, 20.0f, 1.0f, 1.0f, (FlCornerRadius) { 4, 4, 4, 4 }, green);
    fl_ir_border(ir_arena, (FlRect) { 0, 0, 40, 40 }, 1.0f, 1.0f, 0.0f, 50.0f, (FlCornerRadius) { 0 }, green);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 2, "border: borders consuming the rect still translate");
    for (int i = 0; i < 2; i++) {
        CHECK(list.cmds[i].kind == GpuDraw_Rect, "border: a border consuming the rect normalizes to a plain fill");
        for (int c = 0; c < 4; c++) {
            CHECK(list.cmds[i].color[c] == packed_green,
                  "border: the normalized fill carries the border color in all four corners");
        }
    }
    CHECK(list.cmds[0].radius[0] == 4.0f, "border: the normalized fill keeps the outer radii");

    start = ir_rewind_begin(ir_arena);
    fl_ir_clip_push(ir_arena, (FlRect) { 0, 0, 30, 200 });
    fl_ir_border(ir_arena, (FlRect) { 10, 10, 40, 40 }, 2, 2, 2, 2, (FlCornerRadius) { 0 }, red); // straddles
    fl_ir_border(ir_arena, (FlRect) { 60, 10, 40, 40 }, 2, 2, 2, 2, (FlCornerRadius) { 0 }, red); // outside
    fl_ir_clip_pop(ir_arena);
    list = translate(out_arena, ir_arena, start, 255, NULL, NULL);
    CHECK(list.count == 1, "border: a border wholly outside the clip drops");
    CHECK(list.aabbs[0].x1 == 30.0f, "border: a straddling border's AABB clamps to the clip");
    CHECK(list.cmds[0].clip == 1, "border: the border carries its clip index");

    fl_arena_destroy(out_arena);
    fl_arena_destroy(ir_arena);
    printf("gpu_smoke_test: border translator unit tests PASS\n");
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sampler-slot bookkeeping unit tests (no GL - pure CPU): the gpu_image_slots registry and the per-frame
// slot table.

static u32 count_dynamic_entries(const GpuImageSlots* slots) {
    u32 n = 0;
    for (u32 i = 0; i < slots->image_count; i++) {
        n += slots->images[i].is_dynamic ? 1u : 0u;
    }
    return n;
}

static int run_slots_tests(void) {
    FlArena* arena = arena_new();
    GpuImageSlots slots;
    gpu_image_slots_init(&slots, arena);
    gpu_image_slots_begin_frame(&slots);

    CHECK(gpu_image_slots_find_static(&slots, 7) < 0, "slots: an unknown handle does not resolve");
    int big = gpu_image_slots_register_static(&slots, 7, 300, 40, 11);
    CHECK(big >= 0, "slots: a large image registers");
    CHECK(gpu_image_slots_find_static(&slots, 7) == big, "slots: the handle resolves to its entry");
    CHECK(slots.images[big].w == 300 && slots.images[big].h == 40 && slots.images[big].tex == 11
              && !slots.images[big].is_dynamic,
          "slots: the entry keeps the image extent and texture");
    CHECK(gpu_image_slots_register_static(&slots, 7, 8, 8, 12) < 0, "slots: a handle cannot register twice");

    // Dynamic entries key on the buffer pointer, so re-touching resolves to the same entry - which is what
    // keeps the translator's two passes consistent.
    static u8 buf_a[16 * 16 * 4];
    static u8 buf_b[16];
    int da = gpu_image_slots_touch_dynamic(&slots, buf_a, 4, 4, 16, false);
    CHECK(da >= 0, "slots: a dynamic buffer creates an entry");
    CHECK(gpu_image_slots_touch_dynamic(&slots, buf_a, 4, 4, 16, false) == da,
          "slots: re-touching the same buffer resolves to the same entry");
    CHECK(slots.images[da].is_dynamic && slots.images[da].dirty && slots.images[da].last_ref == slots.frame,
          "slots: a touch marks the entry dirty and referenced this frame");
    int db = gpu_image_slots_touch_dynamic(&slots, buf_b, 2, 2, 8, true);
    CHECK(db >= 0 && db != da, "slots: distinct buffers get distinct entries");
    CHECK(slots.images[db].alpha_only, "slots: the entry keeps the alpha-coverage mode");

    slots.images[da].tex = 55; // as if the renderer had realized it
    CHECK(gpu_image_slots_touch_dynamic(&slots, buf_a, 8, 8, 32, false) == da, "slots: a dims change keeps the entry");
    CHECK(slots.images[da].tex == 0 && slots.images[da].w == 8 && slots.images[da].h == 8,
          "slots: a dims change resets the texture for recreation");
    const u32* dead;
    CHECK(gpu_image_slots_take_dead(&slots, &dead) == 1 && dead[0] == 55,
          "slots: the replaced texture joins the dead list");
    CHECK(gpu_image_slots_take_dead(&slots, &dead) == 0, "slots: take_dead drains the list");

    gpu_image_slots_frame_reset(&slots);
    CHECK(gpu_image_slots_assign(&slots, (u32)big, 1) == 0 && gpu_image_slots_assign(&slots, (u32)da, 0) == 1,
          "slots: slots assign in first-reference order");
    CHECK(gpu_image_slots_assign(&slots, (u32)big, 0) == 0, "slots: a repeat reference reuses its slot");
    CHECK(slots.slot_count == 2 && slots.slot_image[0] == (u32)big && slots.slot_image[1] == (u32)da,
          "slots: the frame table records the assignments");
    CHECK(slots.slot_filter[0] == 1, "slots: the first reference's filter stands");

    gpu_image_slots_frame_reset(&slots);
    for (u32 i = 0; i < GPU_IMAGE_SLOT_COUNT + 2; i++) {
        CHECK(gpu_image_slots_register_static(&slots, 100 + i, 300, 8, 20 + i) >= 0,
              "slots overflow: the registry holds more entries than the per-frame slots");
    }
    for (u32 i = 0; i < GPU_IMAGE_SLOT_COUNT; i++) {
        CHECK(gpu_image_slots_assign(&slots, (u32)gpu_image_slots_find_static(&slots, 100 + i), 0) == (int)i,
              "slots overflow: the first GPU_IMAGE_SLOT_COUNT images claim slots in order");
    }
    CHECK(gpu_image_slots_assign(&slots, (u32)gpu_image_slots_find_static(&slots, 100 + GPU_IMAGE_SLOT_COUNT), 0) < 0,
          "slots overflow: the next image is refused");
    CHECK(slots.overflow == 1, "slots overflow: the counter counts the refusal");
    CHECK(gpu_image_slots_assign(&slots, (u32)gpu_image_slots_find_static(&slots, 101 + GPU_IMAGE_SLOT_COUNT), 0) < 0
              && slots.overflow == 2,
          "slots overflow: every refused command counts");
    CHECK(gpu_image_slots_assign(&slots, (u32)slots.slot_image[0], 0) == 0,
          "slots overflow: already-slotted images keep resolving");
    gpu_image_slots_frame_reset(&slots);
    CHECK(slots.slot_count == 0 && slots.overflow == 0, "slots: frame reset clears the table and counter");

    gpu_image_slots_release_static(&slots, 7);
    CHECK(gpu_image_slots_find_static(&slots, 7) < 0, "slots release: the handle stops resolving");
    gpu_image_slots_release_static(&slots, 7);
    CHECK(gpu_image_slots_take_dead(&slots, &dead) == 1 && dead[0] == 11,
          "slots release: the texture joins the dead list once");

    u32 dynamics = 0;
    for (u32 i = 0; i < slots.image_count; i++) {
        if (slots.images[i].is_dynamic) {
            slots.images[i].tex = 70 + dynamics; // as if realized, so eviction has textures to retire
            dynamics++;
        }
    }
    CHECK(dynamics == 2, "slots eviction: both dynamic entries are live before aging");
    for (u32 i = 0; i <= GPU_IMAGE_DYNAMIC_EVICT_FRAMES; i++) {
        gpu_image_slots_begin_frame(&slots);
    }
    CHECK(count_dynamic_entries(&slots) == 0, "slots eviction: stale dynamic entries evict at frame start");
    CHECK(gpu_image_slots_find_static(&slots, 100) >= 0, "slots eviction: statics never evict");
    CHECK(gpu_image_slots_take_dead(&slots, &dead) == 2, "slots eviction: evicted textures join the dead list");

    fl_arena_destroy(arena);
    printf("gpu_smoke_test: image-slot unit tests PASS\n");
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Slot-image translation (no GL): large statics and dynamic buffers become GpuDraw_SlotImage commands with
// per-frame slot indices in aux.

static int run_slot_translator_tests(void) {
    FlArena* ir_arena = arena_new();
    FlArena* out_arena = arena_new();

    GpuImageAtlas atlas;
    gpu_image_atlas_init(&atlas, out_arena);
    CHECK(gpu_image_atlas_place(&atlas, 3, 8, 8) != NULL, "slot translate: atlas placement must succeed");

    GpuImageSlots slots;
    gpu_image_slots_init(&slots, out_arena);
    gpu_image_slots_begin_frame(&slots);
    CHECK(gpu_image_slots_register_static(&slots, 7, 300, 40, 11) >= 0,
          "slot translate: large-image registration must succeed");

    static u8 dyn_buf[4 * 4 * 4];
    const FlImageSource src_small = { .kind = FlImageKind_Static, .handle = 3 };
    const FlImageSource src_large = { .kind = FlImageKind_Static, .handle = 7 };
    const FlImageSource src_dyn
        = { .kind = FlImageKind_Dynamic, .dynamic = { .pixels = dyn_buf, .w = 4, .h = 4, .stride = 16 } };

    // One frame mixing all three image paths. Source sub-rects are in the texture's own texels.
    void* start = ir_begin(ir_arena);
    fl_ir_image(ir_arena, src_small, FlImageMode_Color, (FlRect) { 10, 10, 8, 8 }, (FlRect) { 0 }, white,
                FlImageFilter_Linear, nullptr);
    fl_ir_image(ir_arena, src_large, FlImageMode_Color, (FlRect) { 30, 10, 300, 40 }, (FlRect) { 0 }, white,
                FlImageFilter_Nearest, nullptr);
    fl_ir_image(ir_arena, src_dyn, FlImageMode_Color, (FlRect) { 30, 60, 4, 4 }, (FlRect) { 0 }, white,
                FlImageFilter_Linear, nullptr);
    fl_ir_image(ir_arena, src_large, FlImageMode_Color, (FlRect) { 30, 110, 50, 20 }, (FlRect) { 250, 20, 60, 30 },
                white, FlImageFilter_Nearest, nullptr);
    GpuDrawList list = translate(out_arena, ir_arena, start, 255, &atlas, &slots);
    CHECK(list.count == 4, "slot translate: all three image paths translate");
    CHECK(list.cmds[0].kind == GpuDraw_Image, "slot translate: the atlas image keeps its kind");
    CHECK(list.cmds[1].kind == GpuDraw_SlotImage && list.cmds[1].aux == 0,
          "slot translate: the large static claims the first slot");
    CHECK(list.cmds[1].rect[0] == 30.0f && list.cmds[1].rect[2] == 300.0f && list.cmds[1].rect[3] == 40.0f,
          "slot translate: the dst rect passes through");
    CHECK(list.cmds[1].radius[0] == 0.0f && list.cmds[1].radius[1] == 0.0f && list.cmds[1].radius[2] == 300.0f
              && list.cmds[1].radius[3] == 40.0f,
          "slot translate: an empty src rect maps to the whole texture in its own texels");
    CHECK(list.cmds[2].kind == GpuDraw_SlotImage && list.cmds[2].aux == 1,
          "slot translate: the dynamic buffer claims the next slot");
    CHECK(list.cmds[3].aux == 0, "slot translate: a repeat reference reuses the image's slot");
    CHECK(list.cmds[3].radius[0] == 250.0f && list.cmds[3].radius[1] == 20.0f && list.cmds[3].radius[2] == 300.0f
              && list.cmds[3].radius[3] == 40.0f,
          "slot translate: an overhanging src sub-rect clamps to the texture extent");
    CHECK(list.aabbs[1].x0 == 30.0f && list.aabbs[1].x1 == 330.0f, "slot translate: the AABB is the dst rect");
    CHECK(slots.slot_count == 2 && slots.slot_filter[0] == 1 && slots.slot_filter[1] == 0,
          "slot translate: the frame table holds both images with their first-reference filters");
    CHECK(slots.images[slots.slot_image[1]].dirty,
          "slot translate: the referenced dynamic buffer is marked for upload");
    CHECK(slots.overflow == 0, "slot translate: no overflow with free slots");

    start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, src_large, FlImageMode_Color, (FlRect) { 0, 0, 10, 10 }, (FlRect) { 0 },
                (FlColor) { 200, 100, 50, 128 }, FlImageFilter_Linear, nullptr);
    list = translate(out_arena, ir_arena, start, 255, &atlas, &slots);
    CHECK(list.cmds[0].color[0] == (100u | (50u << 8) | (25u << 16) | (128u << 24)),
          "slot translate: the tint packs premultiplied");

    start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, src_large, FlImageMode_Color, (FlRect) { 0, 0, 10, 10 }, (FlRect) { 0 }, white,
                FlImageFilter_Linear, nullptr);
    fl_ir_image(ir_arena, src_dyn, FlImageMode_Color, (FlRect) { 20, 0, 4, 4 }, (FlRect) { 0 }, white,
                FlImageFilter_Linear, nullptr);
    list = translate(out_arena, ir_arena, start, 255, &atlas, NULL);
    CHECK(list.count == 0, "slot translate: without slot bookkeeping, slot-tier records skip");

    start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, (FlImageSource) { .kind = FlImageKind_Dynamic }, FlImageMode_Color, (FlRect) { 0, 0, 8, 8 },
                (FlRect) { 0 }, white, FlImageFilter_Linear, nullptr);
    fl_ir_image(ir_arena,
                (FlImageSource) { .kind = FlImageKind_Dynamic, .dynamic = { .pixels = dyn_buf, .w = 0, .h = 4 } },
                FlImageMode_Color, (FlRect) { 20, 0, 8, 8 }, (FlRect) { 0 }, white, FlImageFilter_Linear, nullptr);
    list = translate(out_arena, ir_arena, start, 255, &atlas, &slots);
    CHECK(list.count == 0 && slots.slot_count == 0, "slot translate: degenerate dynamic records skip");

    static u8 dyn_clipped[4];
    u32 entries_before = slots.image_count;
    start = ir_rewind_begin(ir_arena);
    fl_ir_clip_push(ir_arena, (FlRect) { 0, 0, 8, 8 });
    fl_ir_image(ir_arena, src_large, FlImageMode_Color, (FlRect) { 100, 100, 300, 40 }, (FlRect) { 0 }, white,
                FlImageFilter_Linear, nullptr);
    fl_ir_image(ir_arena,
                (FlImageSource) { .kind = FlImageKind_Dynamic,
                                  .dynamic = { .pixels = dyn_clipped, .w = 1, .h = 1, .stride = 4 } },
                FlImageMode_Color, (FlRect) { 200, 200, 4, 4 }, (FlRect) { 0 }, white, FlImageFilter_Linear, nullptr);
    fl_ir_clip_pop(ir_arena);
    list = translate(out_arena, ir_arena, start, 255, &atlas, &slots);
    CHECK(list.count == 0 && slots.slot_count == 0 && slots.overflow == 0,
          "slot translate: fully clipped-out slot images claim nothing");
    CHECK(slots.image_count == entries_before, "slot translate: a clipped-out dynamic buffer never enters the table");

    // Slot overflow drops predictably: with GPU_IMAGE_SLOT_COUNT + 2 distinct dynamic buffers the trailing
    // two drop, and the two translation passes agree (the command list holds exactly the survivors, no
    // trailing garbage).
    static u8 bufs[GPU_IMAGE_SLOT_COUNT + 2][4];
    start = ir_rewind_begin(ir_arena);
    for (u32 i = 0; i < GPU_IMAGE_SLOT_COUNT + 2; i++) {
        fl_ir_image(ir_arena,
                    (FlImageSource) { .kind = FlImageKind_Dynamic,
                                      .dynamic = { .pixels = bufs[i], .w = 1, .h = 1, .stride = 4 } },
                    FlImageMode_Color, (FlRect) { (f32)(i * 10), 0, 4, 4 }, (FlRect) { 0 }, white, FlImageFilter_Linear,
                    nullptr);
    }
    list = translate(out_arena, ir_arena, start, 255, &atlas, &slots);
    CHECK(list.count == GPU_IMAGE_SLOT_COUNT, "slot overflow: exactly the slotted images translate");
    for (u32 i = 0; i < GPU_IMAGE_SLOT_COUNT; i++) {
        CHECK(list.cmds[i].kind == GpuDraw_SlotImage && list.cmds[i].aux == i && list.cmds[i].rect[0] == (f32)(i * 10),
              "slot overflow: survivors keep stream order with slots 0..N-1");
    }
    CHECK(slots.overflow == 2, "slot overflow: the counter counts both dropped commands");
    CHECK(slots.slot_count == GPU_IMAGE_SLOT_COUNT, "slot overflow: the frame table is exactly full");

    fl_arena_destroy(out_arena);
    fl_arena_destroy(ir_arena);
    printf("gpu_smoke_test: slot translator unit tests PASS\n");
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Background translator tests - the frame envelope's background state emits clear + fullscreen image commands
// at the FRONT of the stream.
static int run_background_translator_tests(void) {
    FlArena* ir_arena = arena_new();
    FlArena* out_arena = arena_new();

    GpuImageSlots slots;
    gpu_image_slots_init(&slots, out_arena);
    gpu_image_slots_begin_frame(&slots);
    CHECK(gpu_image_slots_register_static(&slots, 7, 300, 40, 11) >= 0, "background translate: register a prev image");
    CHECK(gpu_image_slots_register_static(&slots, 8, 300, 40, 12) >= 0,
          "background translate: register a current image");

    const FlColor clear = { 20, 30, 40, 255 };
    const FlColor green = { 40, 200, 60, 255 };

    FlBackground bg_clear = { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID, .clear = clear };
    GpuDrawList list
        = gpu_ir_to_draw_cmds(out_arena, NULL, 0, 255, TR_TARGET, TR_TARGET, NULL, NULL, &slots, &bg_clear, NULL, NULL);
    CHECK(list.count == 1, "background: a clear-only envelope emits one command");
    CHECK(list.cmds[0].kind == GpuDraw_Rect, "background: the clear is a fill rect");
    CHECK(list.cmds[0].rect[0] == 0.0f && list.cmds[0].rect[1] == 0.0f && list.cmds[0].rect[2] == (f32)TR_TARGET
              && list.cmds[0].rect[3] == (f32)TR_TARGET,
          "background: the clear covers the full target");
    CHECK(list.cmds[0].color[0] == packed_rgba(clear, 255) && list.cmds[0].clip == 0,
          "background: the clear carries the clear color at full opacity under the root clip");

    FlBackground bg_clearless
        = { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID, .clear = { 20, 30, 40, 0 } };
    list = gpu_ir_to_draw_cmds(out_arena, NULL, 0, 255, TR_TARGET, TR_TARGET, NULL, NULL, &slots, &bg_clearless, NULL,
                               NULL);
    CHECK(list.count == 0, "background: a zero-alpha clear emits no command");

    // A single image tints white at the background alpha, premultiplied - alpha 128 gives (128,128,128,128).
    FlBackground bg_one = { .current = 7, .prev = FL_IMAGE_HANDLE_INVALID, .alpha = 128, .clear = clear };
    list = gpu_ir_to_draw_cmds(out_arena, NULL, 0, 255, TR_TARGET, TR_TARGET, NULL, NULL, &slots, &bg_one, NULL, NULL);
    CHECK(list.count == 2, "background: clear + one image");
    CHECK(list.cmds[0].kind == GpuDraw_Rect, "background: the clear leads the image");
    CHECK(list.cmds[1].kind == GpuDraw_SlotImage && list.cmds[1].aux == 0,
          "background: the image rides the first sampler slot");
    CHECK(list.cmds[1].rect[2] == (f32)TR_TARGET && list.cmds[1].rect[3] == (f32)TR_TARGET,
          "background: the image is fullscreen");
    CHECK(list.cmds[1].color[0] == (128u | (128u << 8) | (128u << 16) | (128u << 24)),
          "background: the single image tints white at the background alpha");

    void* start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, 10, 10, 50, 50, green);
    list = gpu_ir_to_draw_cmds(out_arena, start, ir_bytes(ir_arena, start), 255, TR_TARGET, TR_TARGET, NULL, NULL,
                               &slots, &bg_one, NULL, NULL);
    CHECK(list.count == 3, "background: clear + image + the UI rect");
    CHECK(list.cmds[2].kind == GpuDraw_Rect && list.cmds[2].rect[0] == 10.0f,
          "background: the UI command follows the background commands");

    // A valid prev makes the background two images - prev opaque, current at crossfade_t - so over-compositing
    // yields lerp(prev, current, t). The background's own alpha is unused here.
    FlBackground bg_xf = { .current = 8, .prev = 7, .crossfade_t = 0.5f, .alpha = 200, .clear = clear };
    list = gpu_ir_to_draw_cmds(out_arena, NULL, 0, 255, TR_TARGET, TR_TARGET, NULL, NULL, &slots, &bg_xf, NULL, NULL);
    CHECK(list.count == 3, "crossfade: clear + prev + current");
    CHECK(list.cmds[1].kind == GpuDraw_SlotImage && list.cmds[1].color[0] == 0xFFFFFFFFu,
          "crossfade: prev draws opaque (white tint)");
    CHECK(list.cmds[2].kind == GpuDraw_SlotImage
              && list.cmds[2].color[0] == (128u | (128u << 8) | (128u << 16) | (128u << 24)),
          "crossfade: current rides crossfade_t (0.5 -> 128) as its tint alpha, not the background alpha");
    CHECK(list.cmds[1].aux != list.cmds[2].aux, "crossfade: prev and current occupy distinct slots");

    // The frame's global_alpha fades only the UI over the background, never the background itself.
    FlBackground bg_opaque = { .current = 7, .prev = FL_IMAGE_HANDLE_INVALID, .alpha = 255, .clear = clear };
    start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, 10, 10, 50, 50, green);
    list = gpu_ir_to_draw_cmds(out_arena, start, ir_bytes(ir_arena, start), 128, TR_TARGET, TR_TARGET, NULL, NULL,
                               &slots, &bg_opaque, NULL, NULL);
    CHECK(list.count == 3, "global_alpha: clear + image + UI");
    CHECK((list.cmds[0].color[0] >> 24) == 255u, "global_alpha: the background clear stays opaque");
    CHECK((list.cmds[1].color[0] >> 24) == 255u,
          "global_alpha: the background image tint ignores the frame global_alpha");
    CHECK((list.cmds[2].color[0] >> 24) == 128u, "global_alpha: the UI over the background is faded");

    fl_arena_destroy(out_arena);
    fl_arena_destroy(ir_arena);
    printf("gpu_smoke_test: background translator unit tests PASS\n");
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Text-run translator unit tests (no GL - the glyph-atlas bookkeeping is pure CPU): a run explodes to one
// GpuDraw_Glyph quad per non-empty prepared glyph, each positioned at the pen baseline offset by the cell's
// left/top metrics, sized to the padded cell, sampling the cell's texel rect and colorized by the run's
// premultiplied tint. The (font, size, glyph) triple is the atlas key.

static int run_text_translator_tests(void) {
    FlArena* ir_arena = arena_new();
    FlArena* out_arena = arena_new();

    GpuGlyphAtlas glyphs = { 0 };
    gpu_glyph_atlas_init(&glyphs, out_arena);
    const FlFontId font = 3;
    const u16 size = 24;
    const GpuGlyphEntry* g_a = gpu_glyph_atlas_place(&glyphs, font, size, 'A', 10, 14, -1.0f, 12.0f, 11.0f);
    const GpuGlyphEntry* g_b = gpu_glyph_atlas_place(&glyphs, font, size, 'b', 8, 16, 0.0f, 14.0f, 9.0f);
    gpu_glyph_atlas_place(&glyphs, font, size, ' ', 0, 0, 0.0f, 0.0f, 6.0f); // empty (metrics-only) glyph
    CHECK(g_a != NULL && g_b != NULL, "text translate: the setup glyphs place");

    const FlColor run_color = { 220, 210, 40, 255 };
    FlGlyph run[4] = {
        { 'A', 100.0f, 200.0f },
        { ' ', 111.0f, 200.0f }, // metrics-only: emits nothing
        { 'b', 117.0f, 200.0f },
        { 'Z', 125.0f, 200.0f }, // never prepared: skipped like an unresolved image
    };
    void* start = ir_begin(ir_arena);
    fl_ir_text_run(ir_arena, font, size, 1.0f, run_color, run, 4);
    GpuDrawList list = gpu_ir_to_draw_cmds(out_arena, start, ir_bytes(ir_arena, start), 255, TR_TARGET, TR_TARGET, NULL,
                                           &glyphs, NULL, NULL, NULL, NULL);
    CHECK(list.count == 2, "text translate: a run explodes to one quad per non-empty prepared glyph");

    // At full alpha the premultiplied run tint is identical to the straight pack.
    const GpuDrawCmd* a = &list.cmds[0];
    CHECK(a->kind == GpuDraw_Glyph, "text translate: a glyph emits GpuDraw_Glyph");
    CHECK(a->rect[0] == 100.0f + g_a->left && a->rect[1] == 200.0f - g_a->top,
          "text translate: the quad sits at the pen baseline offset by left/top");
    CHECK(a->rect[2] == (f32)g_a->cell.w && a->rect[3] == (f32)g_a->cell.h,
          "text translate: the quad is the padded cell size");
    CHECK(a->radius[0] == (f32)g_a->cell.x && a->radius[1] == (f32)g_a->cell.y
              && a->radius[2] == (f32)(g_a->cell.x + g_a->cell.w) && a->radius[3] == (f32)(g_a->cell.y + g_a->cell.h),
          "text translate: radius carries the cell's source texel rect");
    CHECK(a->color[0] == packed_rgba(run_color, 255), "text translate: an opaque run colorizes with its tint");
    CHECK(a->clip == 0, "text translate: glyphs stamp the root clip");
    CHECK(list.aabbs[0].x0 == a->rect[0] && list.aabbs[0].y0 == a->rect[1]
              && list.aabbs[0].x1 == a->rect[0] + a->rect[2] && list.aabbs[0].y1 == a->rect[1] + a->rect[3],
          "text translate: each glyph's AABB bounds its own quad");

    const GpuDrawCmd* b = &list.cmds[1];
    CHECK(b->kind == GpuDraw_Glyph && b->rect[0] == 117.0f + g_b->left && b->rect[1] == 200.0f - g_b->top,
          "text translate: the empty and unprepared glyphs are skipped, 'b' follows 'A'");
    CHECK(list.aabbs[1].x0 == b->rect[0] && list.aabbs[1].y0 == b->rect[1]
              && list.aabbs[1].x1 == b->rect[0] + b->rect[2] && list.aabbs[1].y1 == b->rect[1] + b->rect[3],
          "text translate: 'b''s AABB bounds its own quad, not the run");

    const FlColor half = { 200, 100, 50, 128 };
    start = ir_rewind_begin(ir_arena);
    FlGlyph one = { 'A', 0.0f, 50.0f };
    fl_ir_text_run(ir_arena, font, size, 1.0f, half, &one, 1);
    list = gpu_ir_to_draw_cmds(out_arena, start, ir_bytes(ir_arena, start), 255, TR_TARGET, TR_TARGET, NULL, &glyphs,
                               NULL, NULL, NULL, NULL);
    u32 ea = ((u32)half.a * 255u + 127u) / 255u;
    u32 er = ((u32)half.r * ea + 127u) / 255u;
    u32 eg = ((u32)half.g * ea + 127u) / 255u;
    u32 eb = ((u32)half.b * ea + 127u) / 255u;
    CHECK(list.count == 1 && list.cmds[0].color[0] == (er | (eg << 8) | (eb << 16) | (ea << 24)),
          "text translate: the run tint packs premultiplied (alpha-coverage colorization)");

    start = ir_rewind_begin(ir_arena);
    fl_ir_text_run(ir_arena, font, (u16)(size + 2), 1.0f, run_color, run, 4);
    list = gpu_ir_to_draw_cmds(out_arena, start, ir_bytes(ir_arena, start), 255, TR_TARGET, TR_TARGET, NULL, &glyphs,
                               NULL, NULL, NULL, NULL);
    CHECK(list.count == 0, "text translate: a run at an unprepared size resolves no glyphs");

    start = ir_rewind_begin(ir_arena);
    fl_ir_text_run(ir_arena, font, size, 1.0f, run_color, run, 4);
    list = gpu_ir_to_draw_cmds(out_arena, start, ir_bytes(ir_arena, start), 255, TR_TARGET, TR_TARGET, NULL, NULL, NULL,
                               NULL, NULL, NULL);
    CHECK(list.count == 0, "text translate: a NULL glyph atlas emits no text commands");

    start = ir_rewind_begin(ir_arena);
    fl_ir_clip_push(ir_arena, (FlRect) { 0.0f, 0.0f, 4.0f, 4.0f }); // a tiny box far from the glyphs at ~(100, 200)
    fl_ir_text_run(ir_arena, font, size, 1.0f, run_color, run, 4);
    fl_ir_clip_pop(ir_arena);
    list = gpu_ir_to_draw_cmds(out_arena, start, ir_bytes(ir_arena, start), 255, TR_TARGET, TR_TARGET, NULL, &glyphs,
                               NULL, NULL, NULL, NULL);
    CHECK(list.count == 0, "text translate: glyphs fully outside the clip drop");

    start = ir_rewind_begin(ir_arena);
    fl_ir_clip_push(ir_arena, (FlRect) { 90.0f, 180.0f, 80.0f, 40.0f }); // covers 'A' (~100,200) and 'b' (~117,200)
    fl_ir_text_run(ir_arena, font, size, 1.0f, run_color, run, 4);
    fl_ir_clip_pop(ir_arena);
    list = gpu_ir_to_draw_cmds(out_arena, start, ir_bytes(ir_arena, start), 255, TR_TARGET, TR_TARGET, NULL, &glyphs,
                               NULL, NULL, NULL, NULL);
    CHECK(list.count == 2 && list.cmds[0].clip != 0 && list.cmds[0].clip == list.cmds[1].clip,
          "text translate: glyphs inside a clip survive and inherit its (non-root) clip index");

    start = ir_rewind_begin(ir_arena);
    FlGlyph ga = { 'A', 0.0f, 50.0f };
    fl_ir_text_run(ir_arena, font, size, 1.0f, run_color, &ga, 1);
    list = gpu_ir_to_draw_cmds(out_arena, start, ir_bytes(ir_arena, start), 128, TR_TARGET, TR_TARGET, NULL, &glyphs,
                               NULL, NULL, NULL, NULL);
    u32 ga_a = ((u32)run_color.a * 128u + 127u) / 255u;
    u32 ga_r = ((u32)run_color.r * ga_a + 127u) / 255u;
    u32 ga_g = ((u32)run_color.g * ga_a + 127u) / 255u;
    u32 ga_b = ((u32)run_color.b * ga_a + 127u) / 255u;
    CHECK(list.count == 1 && list.cmds[0].color[0] == (ga_r | (ga_g << 8) | (ga_b << 16) | (ga_a << 24)),
          "text translate: the frame global_alpha folds into the run tint (premultiplied)");

    // Display scale: run->scale multiplies each glyph quad's bearing offset and extent around the pen, while
    // the atlas source rect (radius[]) stays in raster texels so the shader's linear sample magnifies the
    // cell's coverage. A scale != 1 leaves the run rasterized at size but displays it at size * scale.
    const f32 s = 1.5f;
    start = ir_rewind_begin(ir_arena);
    FlGlyph scaled = { 'A', 100.0f, 200.0f };
    fl_ir_text_run(ir_arena, font, size, s, run_color, &scaled, 1);
    list = gpu_ir_to_draw_cmds(out_arena, start, ir_bytes(ir_arena, start), 255, TR_TARGET, TR_TARGET, NULL, &glyphs,
                               NULL, NULL, NULL, NULL);
    CHECK(list.count == 1 && list.cmds[0].kind == GpuDraw_Glyph,
          "text translate: a scaled run still emits a glyph quad");
    const GpuDrawCmd* sc = &list.cmds[0];
    CHECK(sc->rect[0] == 100.0f + g_a->left * s && sc->rect[1] == 200.0f - g_a->top * s,
          "text translate: scale multiplies the bearing offset around the pen");
    CHECK(sc->rect[2] == (f32)g_a->cell.w * s && sc->rect[3] == (f32)g_a->cell.h * s,
          "text translate: scale multiplies the quad extent (display size = raster size * scale)");
    CHECK(sc->radius[0] == (f32)g_a->cell.x && sc->radius[1] == (f32)g_a->cell.y
              && sc->radius[2] == (f32)(g_a->cell.x + g_a->cell.w) && sc->radius[3] == (f32)(g_a->cell.y + g_a->cell.h),
          "text translate: the source texel rect is unscaled — the quad magnifies the same raster cell");
    CHECK(list.aabbs[0].x1 == sc->rect[0] + sc->rect[2] && list.aabbs[0].y1 == sc->rect[1] + sc->rect[3],
          "text translate: the AABB bounds the scaled quad");

    // A text run's effect sits past the inline FlGlyph array, not right after the payload - reading it at the
    // wrong offset picks up glyph bytes. fl_ir_text_run takes no effect, so assemble the record directly.
    const FlColor tfx_color = { 9, 87, 65, 255 };
    FlEffect text_fx
        = { .kind = FlEffectKind_DropShadow, .color = tfx_color, .off_x = 2.0f, .off_y = 2.0f, .blur = 1.0f };
    start = ir_rewind_begin(ir_arena);
    FlCmdHeader* theader = fl_ir_header(ir_arena, FlCmd_TextRun,
                                        (u32)(sizeof(FlCmdHeader) + sizeof(FlCmdTextRun) + sizeof(FlGlyph))
                                            + fl_ir_effect_bytes(&text_fx));
    FlCmdTextRun* trun = arena_alloc_zero(ir_arena, FlCmdTextRun);
    trun->font = font;
    trun->size = size;
    trun->color = run_color;
    trun->glyph_count = 1;
    trun->scale = 1.0f;
    FlGlyph* trun_glyphs = arena_alloc_array(ir_arena, FlGlyph, 1);
    trun_glyphs[0] = (FlGlyph) { 'A', 100.0f, 200.0f };
    fl_ir_append_effect(ir_arena, theader, &text_fx);
    list = gpu_ir_to_draw_cmds(out_arena, start, ir_bytes(ir_arena, start), 255, TR_TARGET, TR_TARGET, NULL, &glyphs,
                               NULL, NULL, NULL, NULL);
    CHECK(list.count == 1 && list.cmds[0].kind == GpuDraw_Glyph, "text effect: the annotated run translates");
    CHECK(effect_kind_of(&list.cmds[0]) == 1 && list.cmds[0].effect[3] == packed_rgba(tfx_color, 255),
          "text effect: the effect is located past the glyph array and stamped onto the quad");
    CHECK(bits_f32(list.cmds[0].effect[0]) == 2.0f && bits_f32(list.cmds[0].effect[1]) == 2.0f,
          "text effect: the offset words round-trip, proving the read landed on the FlEffect");

    fl_arena_destroy(ir_arena);
    fl_arena_destroy(out_arena);
    printf("gpu_smoke_test: text translator unit tests PASS\n");
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// SVG translator (pure CPU, no GL): a static handle that resolves to the R8 SVG atlas (not the image atlas,
// not a slot) emits a GpuDraw_Svg quad carrying the cell's padding-inset source sub-rect and the colorized
// tint. Opaque white keeps the icon's authored fill; any other tint recolors.

static int run_svg_translator_tests(void) {
    FlArena* ir_arena = arena_new();
    FlArena* out_arena = arena_new();

    GpuSvgAtlas svg = { 0 };
    gpu_svg_atlas_init(&svg, out_arena, GpuSvgTexel_R8);
    const FlImageHandle handle = 77;
    const FlColor authored = { 50, 150, 200, 255 };
    // A 34 x 26 padded cell: a 32 x 24 content block plus the 1-texel ring (the aspect-fit of a 40x30 icon at 32).
    const GpuSvgEntry* cell = gpu_svg_atlas_place(&svg, handle, 32, 34, 26, FlVectorClass_Monochrome, authored, NULL);
    CHECK(cell != NULL, "svg translate: the setup icon places into the atlas");

    // The authored fill is opaque, so the premultiply leaves rgb intact and equals the straight pack.
    const FlRect dst = { 100.0f, 120.0f, 32.0f, 24.0f };
    void* start = ir_begin(ir_arena);
    fl_ir_image(ir_arena, (FlImageSource) { .kind = FlImageKind_Static, .handle = handle }, FlImageMode_Color, dst,
                (FlRect) { 0 }, white, FlImageFilter_Linear, nullptr);
    GpuDrawList list = gpu_ir_to_draw_cmds(out_arena, start, ir_bytes(ir_arena, start), 255, TR_TARGET, TR_TARGET, NULL,
                                           NULL, NULL, NULL, &svg, NULL);
    CHECK(list.count == 1, "svg translate: a vector-mono handle emits one command");
    const GpuDrawCmd* c = &list.cmds[0];
    CHECK(c->kind == GpuDraw_Svg, "svg translate: a vector-mono handle emits GpuDraw_Svg");
    CHECK(c->rect[0] == dst.x && c->rect[1] == dst.y && c->rect[2] == dst.w && c->rect[3] == dst.h,
          "svg translate: the quad is the dst rect");
    CHECK(c->radius[0] == (f32)(cell->cell.x + GPU_SVG_ATLAS_PAD)
              && c->radius[1] == (f32)(cell->cell.y + GPU_SVG_ATLAS_PAD)
              && c->radius[2] == (f32)(cell->cell.x + cell->cell.w - GPU_SVG_ATLAS_PAD)
              && c->radius[3] == (f32)(cell->cell.y + cell->cell.h - GPU_SVG_ATLAS_PAD),
          "svg translate: radius carries the cell's padding-inset content sub-rect");
    CHECK(c->color[0] == packed_rgba(authored, 255),
          "svg translate: an opaque-white tint colorizes with the icon's authored fill");
    CHECK(c->clip == 0, "svg translate: the icon stamps the root clip");

    const FlColor override = { 230, 80, 200, 255 };
    start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, (FlImageSource) { .kind = FlImageKind_Static, .handle = handle }, FlImageMode_Color, dst,
                (FlRect) { 0 }, override, FlImageFilter_Linear, nullptr);
    list = gpu_ir_to_draw_cmds(out_arena, start, ir_bytes(ir_arena, start), 255, TR_TARGET, TR_TARGET, NULL, NULL, NULL,
                               NULL, &svg, NULL);
    CHECK(list.count == 1 && list.cmds[0].kind == GpuDraw_Svg && list.cmds[0].color[0] == packed_rgba(override, 255),
          "svg translate: a non-identity tint recolors the icon");

    start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, (FlImageSource) { .kind = FlImageKind_Static, .handle = handle }, FlImageMode_Color, dst,
                (FlRect) { 0 }, white, FlImageFilter_Linear, nullptr);
    list = gpu_ir_to_draw_cmds(out_arena, start, ir_bytes(ir_arena, start), 128, TR_TARGET, TR_TARGET, NULL, NULL, NULL,
                               NULL, &svg, NULL);
    u32 fa = ((u32)authored.a * 128u + 127u) / 255u;
    u32 fr = ((u32)authored.r * fa + 127u) / 255u;
    u32 fg = ((u32)authored.g * fa + 127u) / 255u;
    u32 fb = ((u32)authored.b * fa + 127u) / 255u;
    CHECK(list.count == 1 && list.cmds[0].color[0] == (fr | (fg << 8) | (fb << 16) | (fa << 24)),
          "svg translate: the frame global_alpha folds into the tint (premultiplied)");

    // A handle that lives only in the SVG tier drops rather than falling through to a slot or misrouting.
    start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, (FlImageSource) { .kind = FlImageKind_Static, .handle = handle }, FlImageMode_Color, dst,
                (FlRect) { 0 }, white, FlImageFilter_Linear, nullptr);
    list = gpu_ir_to_draw_cmds(out_arena, start, ir_bytes(ir_arena, start), 255, TR_TARGET, TR_TARGET, NULL, NULL, NULL,
                               NULL, NULL, NULL);
    CHECK(list.count == 0, "svg translate: a NULL SVG atlas emits no command for a vector-only handle");

    // Multi-color path: a handle living in the RGBA8 color atlas emits GpuDraw_SvgColor sampling the baked
    // premultiplied cell, modulated by a white tint. The colors are baked, so a record tint is ignored.
    GpuSvgAtlas svg_color = { 0 };
    gpu_svg_atlas_init(&svg_color, out_arena, GpuSvgTexel_Rgba8);
    const FlImageHandle color_handle = 91;
    const GpuSvgEntry* color_cell
        = gpu_svg_atlas_place(&svg_color, color_handle, 32, 34, 26, FlVectorClass_MultiColor, (FlColor) { 0 }, NULL);
    CHECK(color_cell != NULL, "svg color translate: the icon places into the RGBA8 atlas");

    start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, (FlImageSource) { .kind = FlImageKind_Static, .handle = color_handle }, FlImageMode_Color,
                dst, (FlRect) { 0 }, override, FlImageFilter_Linear, nullptr);
    list = gpu_ir_to_draw_cmds(out_arena, start, ir_bytes(ir_arena, start), 255, TR_TARGET, TR_TARGET, NULL, NULL, NULL,
                               NULL, NULL, &svg_color);
    CHECK(list.count == 1, "svg color translate: a multi-color handle emits one command");
    c = &list.cmds[0];
    CHECK(c->kind == GpuDraw_SvgColor, "svg color translate: a multi-color handle emits GpuDraw_SvgColor");
    CHECK(c->rect[0] == dst.x && c->rect[1] == dst.y && c->rect[2] == dst.w && c->rect[3] == dst.h,
          "svg color translate: the quad is the dst rect");
    CHECK(c->radius[0] == (f32)(color_cell->cell.x + GPU_SVG_ATLAS_PAD)
              && c->radius[1] == (f32)(color_cell->cell.y + GPU_SVG_ATLAS_PAD)
              && c->radius[2] == (f32)(color_cell->cell.x + color_cell->cell.w - GPU_SVG_ATLAS_PAD)
              && c->radius[3] == (f32)(color_cell->cell.y + color_cell->cell.h - GPU_SVG_ATLAS_PAD),
          "svg color translate: radius carries the cell's padding-inset content sub-rect");
    CHECK(c->color[0] == 0xFFFFFFFFu,
          "svg color translate: the baked cell samples under opaque white (a record tint is ignored)");

    // The frame global_alpha is applied per frame to the persistent premultiplied cell, never baked into it.
    start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, (FlImageSource) { .kind = FlImageKind_Static, .handle = color_handle }, FlImageMode_Color,
                dst, (FlRect) { 0 }, white, FlImageFilter_Linear, nullptr);
    list = gpu_ir_to_draw_cmds(out_arena, start, ir_bytes(ir_arena, start), 128, TR_TARGET, TR_TARGET, NULL, NULL, NULL,
                               NULL, NULL, &svg_color);
    CHECK(list.count == 1 && list.cmds[0].color[0] == (128u | (128u << 8) | (128u << 16) | (128u << 24)),
          "svg color translate: the frame global_alpha rides the white tint premultiplied");

    fl_arena_destroy(ir_arena);
    fl_arena_destroy(out_arena);
    printf("gpu_smoke_test: svg translator unit tests PASS\n");
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Section 2 host plumbing: a GL 4.3 core context on the EGL surfaceless platform (no window, no native
// display - exactly what CI's llvmpipe runs).

static EGLDisplay create_surfaceless_gl43_context(void) {
    const char* client_exts = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    if (client_exts == NULL || strstr(client_exts, "EGL_MESA_platform_surfaceless") == NULL) {
        fprintf(stderr, "gpu_smoke_test: EGL_MESA_platform_surfaceless not available\n");
        return EGL_NO_DISPLAY;
    }

    PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display
        = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (get_platform_display == NULL) {
        fprintf(stderr, "gpu_smoke_test: eglGetPlatformDisplayEXT not exported by the EGL driver\n");
        return EGL_NO_DISPLAY;
    }
    EGLDisplay dpy = get_platform_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL)) {
        fprintf(stderr, "gpu_smoke_test: no surfaceless EGL display\n");
        return EGL_NO_DISPLAY;
    }
    eglBindAPI(EGL_OPENGL_API);

    static const EGLint config_attrs[] = { EGL_SURFACE_TYPE, 0, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE };
    EGLConfig config;
    EGLint num_configs = 0;
    eglChooseConfig(dpy, config_attrs, &config, 1, &num_configs);

    static const EGLint context_attrs[] = {
        EGL_CONTEXT_MAJOR_VERSION,           4,        EGL_CONTEXT_MINOR_VERSION, 3, EGL_CONTEXT_OPENGL_PROFILE_MASK,
        EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE,
    };
    EGLContext context = eglCreateContext(dpy, num_configs > 0 ? config : NULL, EGL_NO_CONTEXT, context_attrs);
    if (context == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) {
        fprintf(stderr, "gpu_smoke_test: failed to create/current a GL 4.3 core context\n");
        eglTerminate(dpy);
        return EGL_NO_DISPLAY;
    }
    return dpy;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Part A - end-to-end through the UI core: a full-screen red container plus a text run. The text leg guards
// the IR-driven glyph path: ui_set_font_cache binds the cache to the GPU backend, and render() must realize
// the run's glyphs into its atlas itself (no host-side prepare pass).

static int run_ui_frame_test(FlArena* arena, FlRenderBackend* backend) {
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

    const int layout_w = 256;
    const int layout_h = 256;
    ctx->layout_width = (float)layout_w;
    ctx->layout_height = (float)layout_h;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)layout_w, .height = (float)layout_h });

    const FlColor rect_color = ui_color(200, 40, 50, 255);

    ui_update_begin(ctx);
    // Padding keeps the text off the (16,16) probe so the red-background asserts below stay valid; the text sits
    // inside the padded box. 'A' in the embedded test font is a solid square, so it paints a solid green block.
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = rect_color,
                   .direction = LayoutDirection_TopToBottom, .padding = padding_all(40)) {
        ui_text_styled(FL_S("AAA"), .color = ui_color(40, 220, 90, 255));
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    CHECK(ctx->shared->renderer.read_pixels != nullptr, "backend has no pixel-readback capability");
    CHECK(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    CHECK(buf.pixels != nullptr, "readback buffer is null");
    CHECK(buf.width == 1920 && buf.height == 1080, "readback should be the offscreen target size");
    CHECK(buf.stride == buf.width * 4u, "readback stride should be interleaved RGBA8");

    const u8* px = pixel_at(&buf, 16, 16);
    printf("gpu_smoke_test: UI frame pixel(16,16) = (%u, %u, %u, %u)\n", px[0], px[1], px[2], px[3]);
    CHECK(px[0] > 100, "UI frame: expected a strong red channel inside the rectangle");
    CHECK(px[0] > px[1] && px[0] > px[2], "UI frame: expected red to dominate green and blue");
    CHECK(px[3] == 255, "UI frame: an opaque rect must carry full coverage alpha");

    // The text run drew iff some pixel is the run's green tint (not the red rect, not transparent).
    bool saw_text = false;
    for (u32 y = 0; y < buf.height && !saw_text; ++y) {
        const u8* row = (const u8*)buf.pixels + (size_t)y * buf.stride;
        for (u32 x = 0; x < buf.width; ++x) {
            const u8* p = row + (size_t)x * 4u;
            if (p[3] != 0 && p[1] > 150 && p[1] > p[0] && p[1] > p[2]) {
                saw_text = true;
                break;
            }
        }
    }
    CHECK(saw_text, "UI frame: text drawn through the ui core renders on GPU (glyphs prepared into the atlas)");

    ui_destroy(ctx);
    backend->destroy_surface(backend->user_data, surface);
    ui_shared_destroy(shared); // shared tier outlives the per-window ctx; exercises image_registry_destroy
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ui_image() widget path under the GPU backend, end-to-end: image_load_from_memory -> begin-frame
// pump (the GPU realizes on this render thread and acks the core registry) -> ui_image() inside a real layout
// -> render -> read back, proving the per-backend realization ack opens the emit gate and the image draws.

// 4x4 solid-red RGBA PNG (color type 6) - RGBA so the decode yields the 4 channels the GPU atlas takes.
static const unsigned char k_ui_image_png_rgba_red[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00,
    0x04, 0x00, 0x00, 0x00, 0x04, 0x08, 0x06, 0x00, 0x00, 0x00, 0xa9, 0xf1, 0x9e, 0x7e, 0x00, 0x00, 0x00, 0x12, 0x49,
    0x44, 0x41, 0x54, 0x78, 0xda, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x19, 0x33, 0x90, 0x2e, 0x00, 0x00, 0x3c, 0x40,
    0x1f, 0xe1, 0x1a, 0xf3, 0xa5, 0x48, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

#define UI_IMAGE_READY_TIMEOUT_MS 5000

static int run_ui_image_widget_test(FlArena* arena, FlRenderBackend* backend) {
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

    // The decode runs on a job worker; the GPU backend defers realization to the begin-frame pump, which
    // ui_update_begin runs on THIS (GL) thread. So the frame where the decode lands realizes the image and
    // renders it in one pass.
    const int layout_w = 256;
    const int layout_h = 256;

    FlPixelReadback buf = { 0 };
    bool ready = false;
    bool drew_red = false;
    int frame = 0;
    for (; frame < UI_IMAGE_READY_TIMEOUT_MS && !drew_red; frame++) {
        ctx->layout_width = (float)layout_w;
        ctx->layout_height = (float)layout_h;
        Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)layout_w, .height = (float)layout_h });

        ui_update_begin(ctx); // pumps image_begin_frame: a landed decode realizes into the atlas + acks here
        // White tint passes the color image through unmodified (the tint multiplies the texels; the default
        // {0,0,0,0} would render it black). fill_container grows the image to cover the probe.
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
            ui_image(handle, .color = ui_color(255, 255, 255, 255), .fill_container = true);
        }
        ui_update_end(ctx, 1.0f / 60.0f);

        ready = fl_image_is_ready(handle);
        if (ready) {
            CHECK(ctx->shared->renderer.read_pixels != nullptr,
                  "ui_image widget: backend has no pixel-readback capability");
            CHECK(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf),
                  "ui_image widget: read_pixels failed");
            // Content renders into the top-left layout region at scale 1.0, and the image fills that region,
            // so its center samples the image's red texels.
            const u8* p = pixel_at(&buf, (u32)layout_w / 2, (u32)layout_h / 2);
            drew_red = p[0] > 150 && p[0] > p[1] && p[0] > p[2] && p[3] == 255;
        }
        if (!drew_red) {
            sleep_ms(1);
        }
    }

    CHECK(ready, "ui_image widget: image never reported ready under the GPU backend (registry realization ack)");

    ImageResult meta = image_get_data(handle);
    CHECK(meta.width == 4 && meta.height == 4, "ui_image widget: registry reports the decoded 4x4 logical size");

    const u8* px = pixel_at(&buf, (u32)layout_w / 2, (u32)layout_h / 2);
    printf("gpu_smoke_test: ui_image widget pixel(%d,%d) = (%u, %u, %u, %u) after %d frame(s)\n", layout_w / 2,
           layout_h / 2, px[0], px[1], px[2], px[3], frame);
    CHECK(drew_red, "ui_image widget: the static image drew red through ui_image() under the GPU backend");

    printf("gpu_smoke_test: ui_image() widget path (static image ready + draws on GPU) PASS\n");

    // Dynamic images stay OUTSIDE the registry: they reference a live caller buffer, carry their
    // own dimensions, and never call image_registry_mark_realized - there is no ImageHandle to key on and no
    // readiness gate, hence no ready-poll loop below.
    FlDynamicImage* dyn = dynamic_image_create(4, 4, arena);
    CHECK(dyn != nullptr, "ui_image widget: dynamic_image_create returned null");
    for (int i = 0; i < dyn->width * dyn->height; i++) {
        u8* px_rgba = dyn->user_buffer + (size_t)i * 4u; // premultiplied opaque red (matches atlas/slot upload)
        px_rgba[0] = 255;
        px_rgba[1] = 0;
        px_rgba[2] = 0;
        px_rgba[3] = 255;
    }

    FlPixelReadback dyn_buf = { 0 };
    bool dyn_red = false;
    for (int i = 0; i < 3; i++) { // a few ticks let the slot upload + draw settle; no readiness poll needed
        Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)layout_w, .height = (float)layout_h });
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
            ui_dynamic_image(dyn, .color = ui_color(255, 255, 255, 255), .fill_container = true);
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }
    CHECK(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &dyn_buf),
          "ui_image widget: dynamic read_pixels failed");
    const u8* dp = pixel_at(&dyn_buf, (u32)layout_w / 2, (u32)layout_h / 2);
    dyn_red = dp[0] > 150 && dp[0] > dp[1] && dp[0] > dp[2] && dp[3] == 255;
    CHECK(dyn_red, "ui_image widget: dynamic image draws on GPU with no registry/readiness gate");

    CHECK(fl_image_is_ready(handle), "ui_image widget: the static handle stays realized after dynamic rendering");

    printf("gpu_smoke_test: ui_dynamic_image() widget path (no readiness gate, draws on GPU) PASS\n");

    // Reset the process-global image system so the later fl_app GPU-path test rebinds it to its own backend.
    image_destroy_system();
    ui_destroy(ctx);
    backend->destroy_surface(backend->user_data, surface);
    ui_shared_destroy(shared); // shared tier outlives the per-window ctx; exercises image_registry_destroy
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Menu point-anchor flip/clamp golden: a menu opened near the top-left corner opens DOWNWARD from the
// anchor, while a menu opened near the bottom edge FLIPS to open UPWARD. A full-screen backdrop makes the
// (opaque, dark) menu surface trivially separable from the (red) background by the red channel.

#define MENU_GOLDEN_W 400
#define MENU_GOLDEN_H 400

// The popup core measures the menu box one frame late, so several ticks let flip/clamp settle.
static void menu_golden_render(FlUiCtx* ctx, FlLayoutId menu_id, FlVec2 anchor, int frames) {
    for (int i = 0; i < frames; i++) {
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .color = ui_color(180, 40, 40, 255)) {}
        ui_menu_open_impl(menu_id, anchor);
        ui_menu(menu_id, .min_width = 180) {
            ui_menu_item(FL_S("Cut"));
            ui_menu_item(FL_S("Copy"));
            ui_menu_item(FL_S("Paste"));
            ui_menu_item(FL_S("Delete"));
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }
}

// Highlighted-row theme: elevated and selected differ only in the GREEN channel, both with low red so the menu
// surface still separates from the red backdrop. A highlighted row paints surface.selected, every other row
// surface.elevated, so probing two row centers by green channel proves which row carries the highlight.
#define MENU_HL_ELEVATED_G 30
#define MENU_HL_SELECTED_G 210

// With no pointer or key input the shared highlight initializes to the first navigable item on open and
// persists, so row 0 is highlighted.
static void menu_highlight_render(FlUiCtx* ctx, FlLayoutId menu_id, FlVec2 anchor, int frames) {
    static const FlThemeColors hl_theme = {
        .surface.elevated = { 20, MENU_HL_ELEVATED_G, 60, 255 },
        .surface.selected = { 20, MENU_HL_SELECTED_G, 60, 255 },
        .text.primary = { 240, 240, 240, 255 },
        .text.secondary = { 160, 160, 160, 255 },
        .border.default_color = { 20, 30, 60, 255 },
    };
    for (int i = 0; i < frames; i++) {
        ui_update_begin(ctx);
        ui_set_theme(ctx, &hl_theme);
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .color = ui_color(180, 40, 40, 255)) {}
        ui_menu_open_impl(menu_id, anchor);
        ui_menu(menu_id, .min_width = 180) {
            ui_menu_item(FL_S("Cut"));
            ui_menu_item(FL_S("Copy"));
            ui_menu_item(FL_S("Paste"));
            ui_menu_item(FL_S("Delete"));
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }
}

// Submenu placement + highlight golden (Menu S3). Under sm_theme every menu surface keeps a low RED
// channel (so any nested-panel pixel separates from the red backdrop by red alone), while the GREEN channel
// encodes the highlight state: unhighlighted rows g=20, the active (deepest) row g=120, and a trail (open-parent)
// row the 0.6 blend g=80 - three well-separated bands so a probe can tell them apart.
static void menu_submenu_golden_render(FlUiCtx* ctx, FlLayoutId menu_id, FlVec2 anchor, int frames) {
    static const FlThemeColors sm_theme = {
        .surface.elevated = { 20, 20, 60, 255 },  // unhighlighted rows: low green
        .surface.selected = { 20, 120, 90, 255 }, // active row: high green (still low red)
        .text.primary = { 240, 240, 240, 255 },
        .text.secondary = { 160, 160, 160, 255 },
        .border.default_color = { 20, 20, 60, 255 },
    };
    u32 sub_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Sub"), .id = 0 });
    for (int i = 0; i < frames; i++) {
        ui_update_begin(ctx);
        ui_set_theme(ctx, &sm_theme);
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .color = ui_color(180, 40, 40, 255)) {}
        ui_menu_open_impl(menu_id, anchor);
        // begin() never clears the open path without input, and this golden injects none, so a single assignment
        // per frame holds the Sub branch expanded at depth 2.
        ctx->shared->menu.levels[0].expanded_child_id = sub_id;
        ctx->shared->menu.depth = 2;
        ui_menu(menu_id, .min_width = 180) {
            ui_submenu(FL_S("Sub")) {
                ui_menu_item(FL_S("Alpha"));
                ui_menu_item(FL_S("Beta"));
                ui_menu_item(FL_S("Gamma"));
            }
            ui_menu_item(FL_S("Below"));
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }
}

// Scan a rectangular band [x0,x1) x [y0,y1) of the readback; true on the first pixel inside the per-channel
// [min,max] bounds. A band absorbs the sub-pixel placement of thin features a single-pixel probe would miss.
static bool menu_band_has(const FlPixelReadback* buf, int x0, int x1, int y0, int y1, int rmin, int rmax, int gmin,
                          int gmax, int bmin, int bmax) {
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            const u8* p = pixel_at(buf, x, y);
            if (p[0] >= rmin && p[0] <= rmax && p[1] >= gmin && p[1] <= gmax && p[2] >= bmin && p[2] <= bmax) {
                return true;
            }
        }
    }
    return false;
}

// Static-kinds golden (Menu S4a). One menu carrying every non-stateful kind - a section header, action
// items (one with right-aligned shortcut text), a separator rule, and a disabled item. Every label is a single
// 'A': the embedded test font renders 'A' as a SOLID square, so each text run becomes a full-coverage filled block
// in its text color that a band scan can find reliably.
static void menu_static_kinds_render(FlUiCtx* ctx, FlLayoutId menu_id, FlVec2 anchor, int frames) {
    static const FlThemeColors sk_theme = {
        .surface.elevated = { 20, 20, 60, 255 },  // unhighlighted / static rows
        .surface.selected = { 20, 110, 60, 255 }, // highlighted row (high green)
        .border.separator = { 30, 30, 220, 255 }, // the separator rule (high blue)
        .border.default_color = { 20, 20, 60, 255 },
        .text.primary = { 240, 240, 240, 255 },  // white (normal labels)
        .text.secondary = { 235, 165, 30, 255 }, // amber (header caption, disabled label, shortcut)
    };
    for (int i = 0; i < frames; i++) {
        ui_update_begin(ctx);
        ui_set_theme(ctx, &sk_theme);
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .color = ui_color(180, 40, 40, 255)) {}
        ui_menu_open_impl(menu_id, anchor);
        ui_menu(menu_id, .min_width = 200) {
            ui_menu_header(FL_S("A"));
            ui_menu_item(FL_S("A"), .shortcut = FL_S("A"));
            ui_menu_item(FL_S("A"));
            ui_menu_separator();
            ui_menu_item(FL_S("A"), .disabled = true);
            ui_menu_item(FL_S("A"));
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }
}

// Stateful-kinds golden (Menu S4b). Every menu surface keeps a low RED channel (so it separates from the
// red backdrop); the check/radio MARKER paints interactive.base = HIGH GREEN (present only on the checked checkable
// and the selected radio); the custom swatch paints HIGH BLUE. surface.selected == surface.elevated here so the
// open-highlight doesn't add stray green - markers are the only green, the swatch the only blue.
static void menu_stateful_golden_render(FlUiCtx* ctx, FlLayoutId menu_id, FlVec2 anchor, int frames) {
    static const FlThemeColors st_theme = {
        .surface.elevated = { 20, 20, 60, 255 },  // menu surface
        .surface.selected = { 20, 20, 60, 255 },  // == elevated: keep the open-highlight from adding green
        .interactive.base = { 20, 230, 40, 255 }, // the check/radio marker (high green)
        .border.default_color = { 20, 20, 60, 255 },
        .text.primary = { 240, 240, 240, 255 },
        .text.secondary = { 160, 160, 160, 255 },
    };
    static bool checked_on = true;
    static bool checked_off = false;
    static i32 radio_sel = 0;
    const FlColor swatch = ui_color(30, 30, 230, 255);
    for (int i = 0; i < frames; i++) {
        ui_update_begin(ctx);
        ui_set_theme(ctx, &st_theme);
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .color = ui_color(180, 40, 40, 255)) {}
        ui_menu_open_impl(menu_id, anchor);
        ui_menu(menu_id, .min_width = 200) {
            ui_menu_check(FL_S("C"), &checked_on);
            ui_menu_check(FL_S("C"), &checked_off);
            ui_menu_radio(FL_S("R"), &radio_sel, 0);
            ui_menu_radio(FL_S("R"), &radio_sel, 1);
            ui_layout_area(.sizing = { sizing_grow(), sizing_fixed(20.0f) },
                           .child_alignment = { LayoutAlignmentX_Left, LayoutAlignmentY_Center },
                           .padding = fl_padding_hv(FlSpacing_SM, 0), .color = st_theme.surface.elevated) {
                ui_layout_area(.sizing = { sizing_fixed(40.0f), sizing_fixed(14.0f) }, .color = swatch) {}
            }
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }
}

// Menu-bar golden (Menu S5). The bar is the menu core's level 0 (a persistent horizontal strip) and a
// top's dropdown is level 1, anchored UNDER its button. Under mb_theme every menu surface (strip + dropdown) keeps
// a low RED channel, so any menu pixel separates from the red backdrop by red alone.
static void menubar_golden_render(FlUiCtx* ctx, FlLayoutId bar_id, int frames) {
    static const FlThemeColors mb_theme = {
        .surface.elevated = { 20, 20, 60, 255 },  // strip + dropdown surface: low red
        .surface.selected = { 20, 120, 90, 255 }, // highlighted / open top: still low red
        .text.primary = { 240, 240, 240, 255 },
        .text.secondary = { 160, 160, 160, 255 },
        .border.default_color = { 20, 20, 60, 255 },
    };
    u32 bar_scope = ui_layout_id_to_clay_id(bar_id);
    u32 file_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("File"), .id = 0 });
    for (int i = 0; i < frames; i++) {
        ui_update_begin(ctx);
        ui_set_theme(ctx, &mb_theme);
        // Force the bar chain open with File expanded (level 0 = bar, level 1 = File's dropdown). open_frame pinned to
        // the current frame so begin()'s input guards all skip and the forced state survives.
        UiMenuState* m = &ctx->shared->menu;
        m->active = true;
        m->owner_scope_id = bar_scope;
        m->open_frame = ctx->frame_counter;
        m->depth = 2;
        m->levels[0].expanded_child_id = file_id;
        m->levels[1].highlight_index = -1;
        m->levels[1].init_highlight = true;
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }, .direction = LayoutDirection_TopToBottom,
                       .color = ui_color(180, 40, 40, 255)) {
            ui_menubar(bar_id) {
                ui_menubar_menu(FL_S("File")) {
                    ui_menu_item(FL_S("New"));
                    ui_menu_item(FL_S("Open"));
                }
                ui_menubar_menu(FL_S("Edit")) {
                    ui_menu_item(FL_S("Undo"));
                }
                ui_menubar_menu(FL_S("View")) {
                    ui_menu_item(FL_S("Zoom"));
                }
            }
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }
}

static int run_menu_flip_test(FlArena* arena, FlRenderBackend* backend) {
    UiShared* shared = ui_shared_create(arena, backend);
    FlUiCtx* ctx = ui_create(arena, shared);
    CHECK(ctx != nullptr, "ui_create returned null (menu)");
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    ui_set_surface(ctx, surface);

    fl_vfs_init(arena);
    FlFontCache* font_cache = font_cache_new(arena);
    ui_set_font_cache(ctx, font_cache);

    // Identity UI scale so 1 logical px == 1 layout px and the probe coordinates below are exact.
    ctx->layout_width = (float)MENU_GOLDEN_W;
    ctx->layout_height = (float)MENU_GOLDEN_H;
    ctx->ui_scale = 1.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)MENU_GOLDEN_W, .height = (float)MENU_GOLDEN_H });

    const FlLayoutId menu_id = { .name = FL_S("gpu_menu"), .id = 0 };
    FlPixelReadback buf;
    CHECK(ctx->shared->renderer.read_pixels != nullptr, "menu: backend has no pixel readback");

    // --- Case 1: opened near the TOP-LEFT corner → opens downward. The menu is ~180 wide and ~150 tall, well
    // inside the 400x400 layout, so neither clamp nor flip should engage. ---
    const FlVec2 corner = { 60.0f, 40.0f };
    menu_golden_render(ctx, menu_id, corner, 3);
    CHECK(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "menu: read_pixels (corner) failed");
    // x=200 is inside the menu width (anchor 60 + 180 = 240) but past the short labels, so it lands on bare menu
    // surface, not glyphs.
    CHECK(pixel_at(&buf, 200, 80)[0] < 100, "menu(corner): below the anchor is the dark menu surface");
    CHECK(pixel_at(&buf, 200, 20)[0] > 150, "menu(corner): above the anchor is the red backdrop (opened downward)");

    // Close so the next case re-anchors instead of hitting the already-open guard.
    ui_menu_close_impl(menu_id);

    // --- Case 2: opened near the BOTTOM edge → flips to open upward. ---
    const FlVec2 bottom = { 60.0f, (float)MENU_GOLDEN_H - 20.0f };
    menu_golden_render(ctx, menu_id, bottom, 3);
    CHECK(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "menu: read_pixels (flip) failed");
    CHECK(pixel_at(&buf, 200, MENU_GOLDEN_H - 60)[0] < 100, "menu(flip): above the anchor is the dark menu surface");
    CHECK(pixel_at(&buf, 200, MENU_GOLDEN_H - 5)[0] > 150,
          "menu(flip): below the bottom-edge anchor stays red (menu flipped upward)");

    ui_menu_close_impl(menu_id);

    // --- Case 3: opened near the RIGHT edge → clamps left to stay on-screen. Anchor x=360 + 180 wide would reach
    // 540, past the 400-wide layout, so the popup core clamps the left edge to 400-180 = 220. The clamp is what
    // makes a pixel at x=300 (left of the un-clamped 360 origin) land on the dark menu surface instead of red. ---
    const FlVec2 right_edge = { (float)MENU_GOLDEN_W - 40.0f, 40.0f };
    menu_golden_render(ctx, menu_id, right_edge, 3);
    CHECK(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "menu: read_pixels (clamp) failed");
    CHECK(pixel_at(&buf, 300, 80)[0] < 100,
          "menu(clamp): the menu shifted left so x=300 is dark menu surface (would be red without the clamp)");
    CHECK(pixel_at(&buf, 398, 80)[0] < 100, "menu(clamp): the right edge of the menu stays on-screen");

    ui_menu_close_impl(menu_id);

    // --- Case 4 (Menu S2): the highlighted-item visual state. Open at the top-left corner under the highlight theme;
    // the shared highlight initializes to the first navigable row on open, so row 0 paints surface.selected and the
    // rows below paint surface.elevated. Probe two row centers at x=200 (interior, past the short labels → bare row
    // background, no glyphs). Rows are 36px tall under XS=4 panel padding from the y=40 anchor: row 0 spans [44,80]
    // (center ~62), row 1 spans [80,116] (center ~98). ---
    const FlVec2 hl_anchor = { 60.0f, 40.0f };
    menu_highlight_render(ctx, menu_id, hl_anchor, 3);
    CHECK(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "menu: read_pixels (highlight) failed");
    const u8* row0 = pixel_at(&buf, 200, 62);
    const u8* row1 = pixel_at(&buf, 200, 98);
    CHECK(row0[0] < 100 && row1[0] < 100, "menu(highlight): both probed rows are the dark menu surface");
    CHECK(row0[1] > 150, "menu(highlight): row 0 is the highlighted row (surface.selected, high green)");
    CHECK(row1[1] < 100, "menu(highlight): row 1 is unhighlighted (surface.elevated, low green)");

    ui_menu_close_impl(menu_id);

    // --- Case 5 (Menu S3): a submenu opens to the RIGHT of its parent. Root panel x in [20,200]; the nested panel
    // opens at the parent row's right edge, so a pixel well right of the root panel is dark menu surface, while the
    // far left stays red (the submenu did not flip). Probe y=100, a non-label column. ---
    menu_submenu_golden_render(ctx, menu_id, (FlVec2) { 20.0f, 40.0f }, 4);
    CHECK(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf),
          "menu: read_pixels (submenu right) failed");
    CHECK(pixel_at(&buf, 340, 100)[0] < 100, "submenu(right): the nested panel opened to the right of its parent");
    CHECK(pixel_at(&buf, 10, 100)[0] > 150, "submenu(right): nothing on the far left (the submenu did not flip)");

    // Trail vs active highlight (criterion 7), read by the green channel. Sub is the open parent (root row 0) → trail
    // (g~80); its child's first row Alpha is the deepest active row (g~120); the next child row Beta is unhighlighted
    // (g~20). Root panel rows: padding XS=4 from anchor y=40, so Sub spans [44,80] (center 62); the child panel
    // top-aligns to the Sub row top, with its own XS padding, so Alpha spans [48,84] (center 66) and Beta [84,120].
    const u8* trail_px = pixel_at(&buf, 150, 62);  // Sub row interior (past the label, before the chevron)
    const u8* active_px = pixel_at(&buf, 340, 66); // Alpha row interior
    const u8* plain_px = pixel_at(&buf, 340, 100); // Beta row interior
    CHECK(trail_px[0] < 100 && active_px[0] < 100, "submenu(highlight): trail and active rows are dark menu surface");
    CHECK(plain_px[1] < 50, "submenu(highlight): an unhighlighted child row is low-green (surface.elevated)");
    CHECK(trail_px[1] > 55 && trail_px[1] < 105,
          "submenu(highlight): the open parent shows the trail blend (mid green)");
    CHECK(active_px[1] > 105,
          "submenu(highlight): the deepest level shows the full-strength active highlight (high green)");

    ui_menu_close_impl(menu_id);

    // --- Case 6 (Menu S3): the submenu FLIPS LEFT when opening right would overflow. Root opened near the right edge
    // (x=210, width 180 -> right edge 390), so opening the 180-wide child rightward would reach 570 and overflow; it
    // flips to the parent's left instead. A pixel left of the root panel is now dark menu surface, and the sliver
    // right of the root stays red (nothing opened rightward). ---
    menu_submenu_golden_render(ctx, menu_id, (FlVec2) { 210.0f, 40.0f }, 4);
    CHECK(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf),
          "menu: read_pixels (submenu flip) failed");
    CHECK(pixel_at(&buf, 180, 100)[0] < 100, "submenu(flip): the nested panel flipped to the left of its parent");
    CHECK(pixel_at(&buf, 395, 100)[0] > 150, "submenu(flip): nothing to the right (it did not open rightward)");

    ui_menu_close_impl(menu_id);

    // --- Case 7 (Menu S3): the submenu FLIPS UP when top-aligning would overflow the bottom edge. Root opened low
    // (y=300) so the root (2 rows, ~80 tall) still fits downward, but the taller child panel (3 rows, ~116 tall)
    // top-aligned at the Sub row (y~304) would reach ~420 and overflow; it bottom-aligns to the parent row instead,
    // opening upward (spanning ~[224,340]). In the child's column (right of the root panel) a pixel ABOVE the parent
    // row is dark menu surface, while a pixel below the child's bottom stays red. ---
    menu_submenu_golden_render(ctx, menu_id, (FlVec2) { 20.0f, 300.0f }, 4);
    CHECK(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf),
          "menu: read_pixels (submenu flip-up) failed");
    CHECK(pixel_at(&buf, 300, 260)[0] < 100,
          "submenu(flip-up): the child panel flipped upward (dark above the parent)");
    CHECK(pixel_at(&buf, 300, 360)[0] > 150, "submenu(flip-up): nothing below the child (it did not open downward)");

    ui_menu_close_impl(menu_id);

    // --- Case 8 (Menu S4b): stateful item kinds + custom content. Open at the top-left corner. Panel content starts
    // at x=44 (anchor 40 + XS=4 panel padding); each check/radio row is 36px tall and stacks from y=44: row0 [44,80],
    // row1 [80,116], row2 [116,152], row3 [152,188], then the 20px swatch row [188,208]. The marker gutter sits at
    // the row's left: panel content x=44 + row SM=8 padding = 52, an 18px gutter [52,70] with a 10px dot centered
    // (~[56,66]). A present marker is HIGH GREEN (interactive.base); the swatch is HIGH BLUE. Band scans absorb the
    // sub-pixel placement of the small dot. ---
    menu_stateful_golden_render(ctx, menu_id, (FlVec2) { 40.0f, 40.0f }, 4);
    CHECK(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "menu: read_pixels (stateful) failed");

    CHECK(menu_band_has(&buf, 52, 70, 52, 72, 0, 100, 150, 255, 0, 120),
          "stateful(check): the checked checkable paints a marker (high-green) in its gutter");
    CHECK(!menu_band_has(&buf, 52, 70, 88, 108, 0, 100, 150, 255, 0, 120),
          "stateful(check): the unchecked checkable paints no marker");

    CHECK(menu_band_has(&buf, 52, 70, 124, 144, 0, 100, 150, 255, 0, 120),
          "stateful(radio): the selected radio paints a bullet marker (high-green)");
    CHECK(!menu_band_has(&buf, 52, 70, 160, 180, 0, 100, 150, 255, 0, 120),
          "stateful(radio): the unselected radio paints no marker (exclusive selection)");

    CHECK(menu_band_has(&buf, 52, 100, 190, 206, 0, 100, 0, 100, 150, 255),
          "stateful(custom): the custom swatch row renders (high-blue) — arbitrary content composes between items");

    ui_menu_close_impl(menu_id);

    // --- Case 9 (Menu S5): the menu bar with one dropdown open. The strip is 30px tall across the top (y in
    // [0,30]); File (the first, leftmost top) is forced open so its 180-wide dropdown opens DOWNWARD below it.
    // Probes read the red channel: menu surface is dark, the backdrop red. ---
    const FlLayoutId bar_id = { .name = FL_S("gpu_menubar"), .id = 0 };
    menubar_golden_render(ctx, bar_id, 4);
    CHECK(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "menubar: read_pixels failed");
    CHECK(pixel_at(&buf, 100, 15)[0] < 100, "menubar(strip): the bar strip renders across the top (dark surface)");
    CHECK(pixel_at(&buf, 380, 15)[0] < 100, "menubar(strip): the strip spans the full width");
    // File's dropdown opened below the strip, under the leftmost button (x in ~[4,184]).
    CHECK(pixel_at(&buf, 60, 70)[0] < 100, "menubar(open): File's dropdown opened below the bar button (dark surface)");
    CHECK(pixel_at(&buf, 300, 70)[0] > 150, "menubar(open): only File is open — no dropdown to the right");

    ctx->shared->menu.active = false; // release the forced bar chain before teardown

    ui_destroy(ctx);
    backend->destroy_surface(backend->user_data, surface);
    ui_shared_destroy(shared); // shared tier outlives the per-window ctx; exercises image_registry_destroy
    return 0;
}

// Static-kinds pixel golden (Menu S4a) - the ONE menu golden that needs real glyphs. It renders 'A' labels into
// the SHARED backend glyph atlas, so it must run AFTER run_text_atlas_tests (whose atlas-full stress assumes the
// atlas holds only its own glyphs) and before only run_frame_lifecycle_tests (which never touches the glyph
// atlas). It changes no resolution, so the create-resolution invariant that test relies on still holds.
static int run_menu_static_kinds_test(FlArena* arena, FlRenderBackend* backend) {
    UiShared* shared = ui_shared_create(arena, backend);
    FlUiCtx* ctx = ui_create(arena, shared);
    CHECK(ctx != nullptr, "ui_create returned null (menu static kinds)");
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    ui_set_surface(ctx, surface);

    fl_vfs_init(arena);
    FlFontCache* font_cache = font_cache_new(arena);
    ui_set_font_cache(ctx, font_cache);

    // The embedded test font renders 'A' as a solid square, so the single-'A' labels paint as full-coverage filled
    // blocks a band scan can find by color.
    FlFontId font = font_cache_load_font_memory(font_cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    CHECK(font != FONT_ID_INVALID, "menu static kinds: the embedded font loads");
    ui_font_set(font);

    ctx->layout_width = (float)MENU_GOLDEN_W;
    ctx->layout_height = (float)MENU_GOLDEN_H;
    ctx->ui_scale = 1.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)MENU_GOLDEN_W, .height = (float)MENU_GOLDEN_H });

    const FlLayoutId menu_id = { .name = FL_S("gpu_menu_static"), .id = 0 };
    FlPixelReadback buf;
    CHECK(ctx->shared->renderer.read_pixels != nullptr, "menu static kinds: backend has no pixel readback");

    // Open at the top-left corner. Panel x in [40,240] (min_width 200), content top y=44 with XS=4 padding. Rows
    // stack: header 28px [44,72], item 36px [72,108], item [108,144], separator 9px [144,153], disabled [153,189],
    // item [189,225]. Channels: low red = menu surface, green split = highlight, high blue = separator rule, amber
    // (high R, low B) = secondary text (header/disabled/shortcut), white = primary text.
    menu_static_kinds_render(ctx, menu_id, (FlVec2) { 40.0f, 40.0f }, 4);
    CHECK(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "menu static kinds: read_pixels failed");

    // On open the highlight lands on the first NAVIGABLE row (the item below), never the non-interactive header.
    // x=150 is bare row background: right of the left label, left of the right-aligned shortcut.
    const u8* hdr_bg = pixel_at(&buf, 150, 58); // header row center
    const u8* sel_bg = pixel_at(&buf, 150, 90); // first item row center
    CHECK(hdr_bg[0] < 100 && sel_bg[0] < 100, "static(header): both rows are the dark menu surface");
    CHECK(hdr_bg[1] < 60, "static(header): the header row is unhighlighted (elevated) — it is non-interactive");
    CHECK(sel_bg[1] > 90, "static(header): the first navigable row carries the open highlight (selected)");

    CHECK(menu_band_has(&buf, 60, 180, 144, 153, 0, 100, 0, 100, 180, 255),
          "static(separator): a separator rule (high-blue) renders in the separator row");
    CHECK(!menu_band_has(&buf, 60, 180, 110, 142, 0, 100, 0, 100, 180, 255),
          "static(separator): a normal item row has no rule pixel");

    // The solid-'A' glyph fills at full coverage, so amber reads ~ (235,165,30): a low-BLUE discriminator (<=110)
    // separates it from white text (blue ~240) and the dark surfaces stay below the red floor (>=120).
    CHECK(menu_band_has(&buf, 44, 110, 155, 187, 120, 255, 80, 215, 0, 110),
          "static(disabled): the disabled item label is greyed (secondary/amber text)");
    CHECK(!menu_band_has(&buf, 44, 110, 110, 142, 120, 255, 80, 215, 0, 110),
          "static(disabled): a normal item label is primary (white), not greyed");

    CHECK(menu_band_has(&buf, 150, 234, 74, 106, 120, 255, 80, 215, 0, 110),
          "static(shortcut): shortcut text renders right-aligned on the item row");
    CHECK(!menu_band_has(&buf, 150, 234, 110, 142, 120, 255, 80, 215, 0, 110),
          "static(shortcut): an item without a shortcut has no trailing-edge text");

    ui_menu_close_impl(menu_id);
    ui_destroy(ctx);
    backend->destroy_surface(backend->user_data, surface);
    ui_shared_destroy(shared); // shared tier outlives the per-window ctx; exercises image_registry_destroy
    printf("gpu_smoke_test: menu static-kinds (S4a) golden PASS\n");
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Part B - hand-built IR in pixel coordinates, driving the backend seam directly. The readback is
// premultiplied coverage alpha over a transparent target, so every expectation below is computable.

static int run_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    FlRenderFrame frame = test_frame();
    FlPixelReadback buf;

    // Src-over blend ordering and coverage alpha over the transparent target.
    void* start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, 0.0f, 0.0f, 256.0f, 256.0f, red);     // A: opaque base
    emit_solid_rect(ir_arena, 192.0f, 64.0f, 128.0f, 128.0f, blue); // B: half-transparent, overlaps A's right edge
    emit_solid_rect(ir_arena, 300.5f, 8.0f, 20.0f, 16.0f, green);   // C: fractional left edge
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed after direct IR frame");

    const f32 ab = 128.0f / 255.0f; // B's alpha
    CHECK(pixel_color(pixel_at(&buf, 16, 16), red, 2.0f), "direct IR: pixel inside A only");
    CHECK(channels_near(pixel_at(&buf, 200, 100), 200.0f * (1 - ab) + 20.0f * ab, 40.0f, 50.0f * (1 - ab) + 200.0f * ab,
                        255, 3.0f),
          "direct IR: B should src-over blend onto A");
    CHECK(channels_near(pixel_at(&buf, 280, 100), 20.0f * ab, 40.0f * ab, 200.0f * ab, 128, 3.0f),
          "direct IR: B alone should land premultiplied with its own alpha");
    // Pixel (300, 10) spans x 300..301; C starts at 300.5 -> exactly half coverage of an opaque color.
    CHECK(channels_near(pixel_at(&buf, 300, 10), 40.0f * 0.5f, 200.0f * 0.5f, 60.0f * 0.5f, 127.5f, 3.0f),
          "direct IR: a half-covered edge pixel should carry half coverage alpha");
    CHECK(pixel_transparent(pixel_at(&buf, 400, 300)), "direct IR: untouched pixels should stay fully transparent");

    start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, 0.0f, 0.0f, 64.0f, 64.0f, red);
    frame.global_alpha = 128;
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed after global_alpha frame");
    CHECK(channels_near(pixel_at(&buf, 16, 16), 200.0f * ab, 40.0f * ab, 50.0f * ab, 128, 3.0f),
          "direct IR: global_alpha must halve an opaque rect");
    CHECK(pixel_transparent(pixel_at(&buf, 200, 100)), "direct IR: the second frame must fully replace the first");

    // --- Rounded corners and per-corner gradients ---------------------------------------------------------------
    // One frame of non-overlapping rects; every expectation below is a property of the SDF coverage model
    // (clamp on distance) and bilinear corner interpolation, not a golden image.
    const FlColor pure_red = { 255, 0, 0, 255 };
    const FlColor pure_green = { 0, 255, 0, 255 };
    const FlColor pure_blue = { 0, 0, 255, 255 };
    const FlColor black = { 0, 0, 0, 255 };
    const FlColor half_red = { 200, 40, 50, 128 };
    const FlColor half_blue = { 20, 40, 200, 128 };

    start = ir_rewind_begin(ir_arena);
    // R1: one rounded corner (TL, radius 16) mixed with three square ones.
    fl_ir_fill_rect(ir_arena, (FlRect) { 32, 32, 64, 64 }, (FlCornerRadius) { .tl = 16.0f },
                    (FlColor[4]) { red, red, red, red }, nullptr);
    // R2: all four corners rounded, radius 8.
    fl_ir_fill_rect(ir_arena, (FlRect) { 128, 32, 64, 64 }, (FlCornerRadius) { 8, 8, 8, 8 },
                    (FlColor[4]) { red, red, red, red }, nullptr);
    // G1: horizontal gradient black -> red.
    fl_ir_fill_rect(ir_arena, (FlRect) { 0, 128, 128, 32 }, (FlCornerRadius) { 0 },
                    (FlColor[4]) { black, pure_red, black, pure_red }, nullptr);
    // G2: vertical gradient green -> blue.
    fl_ir_fill_rect(ir_arena, (FlRect) { 160, 128, 32, 128 }, (FlCornerRadius) { 0 },
                    (FlColor[4]) { pure_green, pure_green, pure_blue, pure_blue }, nullptr);
    // G3: four distinct corners (corner-order check: TL red, TR green, BL blue, BR white).
    fl_ir_fill_rect(ir_arena, (FlRect) { 224, 128, 64, 64 }, (FlCornerRadius) { 0 },
                    (FlColor[4]) { pure_red, pure_green, pure_blue, white }, nullptr);
    // G4: half-alpha gradient over the transparent target (premultiplied-output check).
    fl_ir_fill_rect(ir_arena, (FlRect) { 320, 128, 64, 32 }, (FlCornerRadius) { 0 },
                    (FlColor[4]) { half_red, half_blue, half_red, half_blue }, nullptr);
    frame.global_alpha = 255;
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the rounded/gradient frame");

    // R1 inside/outside sampling: the TL corner circle is centered at (48, 48) with radius 16.
    CHECK(pixel_transparent(pixel_at(&buf, 33, 33)),
          "rounded: a pixel outside the corner arc must stay fully transparent");
    CHECK(pixel_color(pixel_at(&buf, 40, 40), red, 2.0f),
          "rounded: a pixel inside the corner arc must be the exact solid fill");
    if (check_aa_ramp_line(&buf, 33, 33, 1, 1, 6, red, true, "rounded corner diagonal") != 0) {
        return 1;
    }
    CHECK(pixel_color(pixel_at(&buf, 95, 32), red, 2.0f),
          "rounded: a radius-0 corner mixed with a rounded one must stay sharp");
    CHECK(pixel_transparent(pixel_at(&buf, 96, 32)), "rounded: nothing may bleed past a radius-0 corner");

    // R2: all four corner-tip pixels are outside their arcs (tip-to-center distance ~10.6 > 8.5); all four
    // edge midpoints are on straight edges and fully covered.
    CHECK(pixel_transparent(pixel_at(&buf, 128, 32)) && pixel_transparent(pixel_at(&buf, 191, 32))
              && pixel_transparent(pixel_at(&buf, 128, 95)) && pixel_transparent(pixel_at(&buf, 191, 95)),
          "rounded: every corner-tip pixel of a uniformly rounded rect must be transparent");
    CHECK(pixel_color(pixel_at(&buf, 160, 32), red, 2.0f) && pixel_color(pixel_at(&buf, 160, 95), red, 2.0f)
              && pixel_color(pixel_at(&buf, 128, 63), red, 2.0f) && pixel_color(pixel_at(&buf, 191, 63), red, 2.0f),
          "rounded: straight-edge midpoints of a rounded rect must be fully covered");

    {
        u8 prev_r = 0;
        for (int x = 0; x < 128; x++) {
            const u8* gpx = pixel_at(&buf, x, 144);
            CHECK(gpx[0] >= prev_r, "gradient: red must be monotonic along the horizontal axis");
            CHECK(gpx[1] == 0 && gpx[2] == 0, "gradient: black->red must not invent green or blue");
            CHECK(gpx[3] == 255, "gradient: an opaque gradient stays fully opaque");
            prev_r = gpx[0];
        }
        CHECK(pixel_at(&buf, 0, 144)[0] < 8, "gradient: the left end must be near black");
        CHECK(pixel_at(&buf, 127, 144)[0] > 247, "gradient: the right end must be near pure red");
        const u8* mid = pixel_at(&buf, 64, 144);
        CHECK(mid[0] > 120 && mid[0] < 136, "gradient: the midpoint must sit near half intensity");
    }
    // G2: the same property on the vertical axis (catches a u/v swap).
    {
        u8 prev_b = 0;
        for (int y = 128; y < 256; y++) {
            const u8* gpx = pixel_at(&buf, 176, y);
            CHECK(gpx[2] >= prev_b, "gradient: blue must be monotonic along the vertical axis");
            CHECK((int)gpx[1] + (int)gpx[2] > 240, "gradient: green->blue must conserve intensity");
            prev_b = gpx[2];
        }
        CHECK(pixel_at(&buf, 176, 128)[1] > 247, "gradient: the top end must be near pure green");
        CHECK(pixel_at(&buf, 176, 255)[2] > 247, "gradient: the bottom end must be near pure blue");
    }
    // G3: catches TL/TR/BL/BR order mixups in either the translator packing or the shader's quadrant mapping.
    CHECK(pixel_at(&buf, 226, 130)[0] > 200 && pixel_at(&buf, 226, 130)[1] < 40 && pixel_at(&buf, 226, 130)[2] < 40,
          "gradient corners: TL must be red-dominated");
    CHECK(pixel_at(&buf, 285, 130)[1] > 200 && pixel_at(&buf, 285, 130)[0] < 40 && pixel_at(&buf, 285, 130)[2] < 40,
          "gradient corners: TR must be green-dominated");
    CHECK(pixel_at(&buf, 226, 190)[2] > 200 && pixel_at(&buf, 226, 190)[0] < 40 && pixel_at(&buf, 226, 190)[1] < 40,
          "gradient corners: BL must be blue-dominated");
    CHECK(pixel_at(&buf, 285, 190)[0] > 200 && pixel_at(&buf, 285, 190)[1] > 200 && pixel_at(&buf, 285, 190)[2] > 200,
          "gradient corners: BR must be white-dominated");
    // G4: an interior pixel of a half-alpha gradient over the transparent target - the stored alpha is the
    // interpolated alpha (no coverage loss in the interior) and RGB is premultiplied by it. At pixel center
    // x = 352.5, u = 32.5/64, the interpolated color is (200 - 180u, 40, 50 + 150u) at alpha 128.
    {
        const f32 u = 32.5f / 64.0f;
        const f32 a = 128.0f / 255.0f;
        CHECK(channels_near(pixel_at(&buf, 352, 144), (200.0f - 180.0f * u) * a, 40.0f * a, (50.0f + 150.0f * u) * a,
                            128.0f, 3.0f),
              "gradient: a half-alpha gradient must land premultiplied with the interpolated alpha");
    }

    // --- Clip cut: pixels land exactly at clip boundaries -------------------------------------------------------
    start = ir_rewind_begin(ir_arena);
    // K1: a 64x64 red rect clipped to {16,16,32,32} - every visible pixel comes from the clip, not the rect.
    fl_ir_clip_push(ir_arena, (FlRect) { 16, 16, 32, 32 });
    emit_solid_rect(ir_arena, 0.0f, 0.0f, 64.0f, 64.0f, red);
    fl_ir_clip_pop(ir_arena);
    // K2: a green rect whose clip starts at the fractional x = 100.5 - pixel column 100 is half covered.
    fl_ir_clip_push(ir_arena, (FlRect) { 100.5f, 0.0f, 32.0f, 32.0f });
    emit_solid_rect(ir_arena, 96.0f, 0.0f, 64.0f, 32.0f, green);
    fl_ir_clip_pop(ir_arena);
    // K3: a rounded rect (TL radius 16, arc circle centered at (208,48)) clipped to its left 8 pixels
    // ({192..200} x {32..96}) - the clip edge at x = 200 slices through the corner arc.
    fl_ir_clip_push(ir_arena, (FlRect) { 192, 32, 8, 64 });
    fl_ir_fill_rect(ir_arena, (FlRect) { 192, 32, 64, 64 }, (FlCornerRadius) { .tl = 16.0f },
                    (FlColor[4]) { red, red, red, red }, nullptr);
    fl_ir_clip_pop(ir_arena);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the clip frame");

    CHECK(pixel_color(pixel_at(&buf, 16, 16), red, 2.0f) && pixel_color(pixel_at(&buf, 47, 47), red, 2.0f),
          "clip: pixels inside an integer clip keep full coverage");
    CHECK(pixel_transparent(pixel_at(&buf, 15, 30)) && pixel_transparent(pixel_at(&buf, 48, 30))
              && pixel_transparent(pixel_at(&buf, 30, 15)) && pixel_transparent(pixel_at(&buf, 30, 48)),
          "clip: the rect must not bleed past any integer clip edge");
    // K2: the fractional clip edge at x = 100.5 half-covers pixel 100 (centers at 100.5), exactly like a
    // fractional rect edge would.
    CHECK(pixel_transparent(pixel_at(&buf, 99, 10)), "clip: pixels left of a fractional clip edge stay transparent");
    CHECK(channels_near(pixel_at(&buf, 100, 10), 40.0f * 0.5f, 200.0f * 0.5f, 60.0f * 0.5f, 127.5f, 3.0f),
          "clip: a fractionally clipped pixel carries analytic half coverage");
    CHECK(pixel_color(pixel_at(&buf, 101, 10), green, 2.0f),
          "clip: pixels right of a fractional clip edge keep full coverage");
    CHECK(pixel_color(pixel_at(&buf, 199, 44), red, 2.0f),
          "clip on a rounded corner: inside both the arc and the clip is the solid fill");
    CHECK(pixel_transparent(pixel_at(&buf, 200, 44)) && pixel_transparent(pixel_at(&buf, 201, 80)),
          "clip on a rounded corner: nothing lands past the clip edge, arc or not");
    CHECK(pixel_transparent(pixel_at(&buf, 193, 33)),
          "clip on a rounded corner: outside the arc stays transparent inside the clip");
    CHECK(pixel_color(pixel_at(&buf, 193, 90), red, 2.0f),
          "clip on a rounded corner: the straight edge below the corner still fills inside the clip");

    // Tile-list capacity: the per-tile index list holds 64 entries; commands beyond that are counted as
    // overflow and dropped. 64 red rects then 6 blue ones on a single 8x8 tile (pixels 32..40) - the blues
    // land past capacity, so the pixel must stay red and uncorrupted.
    start = ir_rewind_begin(ir_arena);
    for (int i = 0; i < 64; i++) {
        emit_solid_rect(ir_arena, 32.0f, 32.0f, 8.0f, 8.0f, red);
    }
    for (int i = 0; i < 6; i++) {
        emit_solid_rect(ir_arena, 32.0f, 32.0f, 8.0f, 8.0f, (FlColor) { 20, 40, 200, 255 });
    }
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed after overflow frame");
    CHECK(pixel_color(pixel_at(&buf, 36, 36), red, 2.0f),
          "overflow: only the first 64 commands on a tile may render, without corrupting the pixel");
    CHECK(pixel_transparent(pixel_at(&buf, 48, 36)), "overflow: the stack must stay confined to its own tile");

    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Border pixel tests - every expectation is a property of the outer-minus-inner SDF coverage model.

static int run_border_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    FlRenderFrame frame = test_frame();
    FlPixelReadback buf;

    // One frame of non-overlapping borders.
    void* start = ir_rewind_begin(ir_arena);
    // B1: square border, uniform width 4 - bands at {32..36} and {92..96} on each axis.
    fl_ir_border(ir_arena, (FlRect) { 32, 32, 64, 64 }, 4, 4, 4, 4, (FlCornerRadius) { 0 }, red);
    // B2: per-side widths l=8 r=2 t=4 b=0 - each side's band is its own width; the bottom has none.
    fl_ir_border(ir_arena, (FlRect) { 128, 32, 64, 64 }, 8, 2, 4, 0, (FlCornerRadius) { 0 }, green);
    // B3: rounded, radius 16, width 4 - the TL outer arc is centered at (240, 48), outer 16, inner 12.
    fl_ir_border(ir_arena, (FlRect) { 224, 32, 64, 64 }, 4, 4, 4, 4, (FlCornerRadius) { 16, 16, 16, 16 }, red);
    // B4: width 10 past the radius 6 - the inner corner at (330, 42) is sharp.
    fl_ir_border(ir_arena, (FlRect) { 320, 32, 64, 64 }, 10, 10, 10, 10, (FlCornerRadius) { 6, 6, 6, 6 }, red);
    // B5: widths consuming the rect - renders as a filled rounded rect in the border color.
    fl_ir_border(ir_arena, (FlRect) { 416, 32, 64, 64 }, 40, 40, 40, 40, (FlCornerRadius) { 8, 8, 8, 8 }, red);
    // B6: half-alpha border - band pixels land premultiplied like every other kind.
    fl_ir_border(ir_arena, (FlRect) { 512, 32, 64, 64 }, 4, 4, 4, 4, (FlCornerRadius) { 0 }, blue);
    // B7: rounded with per-side widths - each corner's inner radius derives from its own two adjacent
    // sides (TL pairs left+top, TR right+top, BL left+bottom, BR right+bottom): radius 12 with widths
    // l=8 r=2 t=6 b=1 gives inner radii TL 4, TR 6, BL 4, BR 10, with the inner rect at {616, 38, 54, 57}.
    fl_ir_border(ir_arena, (FlRect) { 608, 32, 64, 64 }, 8, 2, 6, 1, (FlCornerRadius) { 12, 12, 12, 12 }, red);
    // B8: fractional uniform width 4.5 - the inner edge sits on a half-pixel boundary at x = 708.5.
    fl_ir_border(ir_arena, (FlRect) { 704, 32, 64, 64 }, 4.5f, 4.5f, 4.5f, 4.5f, (FlCornerRadius) { 0 }, red);
    // B9: dashed, 8 on 8 off. Both runs are 64 long, an exact 4 periods, so the rescale is the identity and
    // the pattern lands on known coordinates: dashes at 0..8, 16..24, 32..40, 48..56 along each edge,
    // measured from the outer rect's origin down whichever axis the nearest edge runs.
    fl_ir_border_stroked(ir_arena, (FlRect) { 800, 32, 64, 64 }, 4, 4, 4, 4, (FlCornerRadius) { 0 }, red,
                         (FlStrokeStyle) { 8.0f, 8.0f });
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the border frame");

    CHECK(pixel_color(pixel_at(&buf, 33, 64), red, 2.0f) && pixel_color(pixel_at(&buf, 94, 64), red, 2.0f)
              && pixel_color(pixel_at(&buf, 64, 33), red, 2.0f) && pixel_color(pixel_at(&buf, 64, 94), red, 2.0f),
          "border: all four side bands must be hit");
    CHECK(pixel_color(pixel_at(&buf, 33, 33), red, 2.0f),
          "border: a square corner pixel sits in both bands and must be hit");
    CHECK(pixel_transparent(pixel_at(&buf, 64, 64)), "border: the interior hole must stay untouched");
    CHECK(pixel_transparent(pixel_at(&buf, 31, 64)) && pixel_transparent(pixel_at(&buf, 64, 31)),
          "border: nothing may land outside the outer edge");
    CHECK(pixel_color(pixel_at(&buf, 35, 64), red, 2.0f) && pixel_transparent(pixel_at(&buf, 36, 64)),
          "border: an integer inner edge cuts exactly between band and hole");

    CHECK(pixel_color(pixel_at(&buf, 135, 64), green, 2.0f) && pixel_transparent(pixel_at(&buf, 136, 64)),
          "border per-side: the left band is exactly w_l wide");
    CHECK(pixel_color(pixel_at(&buf, 190, 64), green, 2.0f) && pixel_transparent(pixel_at(&buf, 189, 64)),
          "border per-side: the right band is exactly w_r wide");
    CHECK(pixel_color(pixel_at(&buf, 160, 35), green, 2.0f) && pixel_transparent(pixel_at(&buf, 160, 36)),
          "border per-side: the top band is exactly w_t wide");
    CHECK(pixel_transparent(pixel_at(&buf, 160, 94)), "border per-side: a zero-width side draws nothing");

    // B3: the rounded corner band, walked diagonally inward from outside the outer arc into the hole.
    if (check_aa_ramp_line(&buf, 225, 33, 1, 1, 6, red, true, "border rounded-corner diagonal") != 0) {
        return 1;
    }
    CHECK(pixel_transparent(pixel_at(&buf, 232, 40)),
          "border rounded: the hole must reach across the rounded corner's inner arc");
    CHECK(pixel_transparent(pixel_at(&buf, 224, 32)),
          "border rounded: the corner tip outside the outer arc stays transparent");
    CHECK(pixel_color(pixel_at(&buf, 226, 64), red, 2.0f) && pixel_color(pixel_at(&buf, 256, 34), red, 2.0f),
          "border rounded: straight-edge band midpoints keep full coverage");
    CHECK(pixel_transparent(pixel_at(&buf, 256, 64)), "border rounded: the interior hole stays untouched");

    // B4: a width past the radius makes the inner corner sharp - a rounded inner corner would leave the
    // pixel diagonally outside it partial rather than fully covered.
    CHECK(pixel_color(pixel_at(&buf, 329, 41), red, 2.0f),
          "border width>radius: the pixel diagonally outside the sharp inner corner is fully covered");
    CHECK(pixel_transparent(pixel_at(&buf, 330, 42)),
          "border width>radius: the pixel diagonally inside the sharp inner corner is hole");
    CHECK(pixel_color(pixel_at(&buf, 324, 64), red, 2.0f) && pixel_transparent(pixel_at(&buf, 352, 64)),
          "border width>radius: band and hole still split along the straight edges");

    CHECK(pixel_color(pixel_at(&buf, 448, 64), red, 2.0f) && pixel_color(pixel_at(&buf, 448, 33), red, 2.0f),
          "border consuming the rect: the center and edge midpoints fill in the border color");
    CHECK(pixel_transparent(pixel_at(&buf, 416, 32)),
          "border consuming the rect: the rounded corner tip still stays transparent");

    {
        const f32 a = 128.0f / 255.0f;
        CHECK(channels_near(pixel_at(&buf, 514, 64), 20.0f * a, 40.0f * a, 200.0f * a, 128.0f, 3.0f),
              "border: a half-alpha band must land premultiplied");
        CHECK(pixel_transparent(pixel_at(&buf, 544, 64)),
              "border: a half-alpha border's hole must stay fully transparent");
    }

    // B7: the per-corner derivation, probed where the corners disagree. Each probe sits ~4.95px from its
    // corner's inner arc center along the corner diagonal - past TL's inner radius 4 (band) but within
    // TR's inner radius 6 (hole), so swapping which sides feed which corner cannot pass both. BR's inner
    // radius 10 is probed at 9.19px (hole) and 10.61px (band) from its arc center at (660, 85).
    CHECK(pixel_color(pixel_at(&buf, 616, 38), red, 2.0f),
          "border per-corner: TL (l=8, t=6 -> inner radius 4) keeps the band 4.95px from its arc center");
    CHECK(pixel_transparent(pixel_at(&buf, 667, 40)),
          "border per-corner: TR (r=2, t=6 -> inner radius 6) is hole 4.95px from its arc center");
    CHECK(pixel_transparent(pixel_at(&buf, 666, 91)),
          "border per-corner: BR (r=2, b=1 -> inner radius 10) hollows far deeper into the corner");
    CHECK(pixel_color(pixel_at(&buf, 667, 92), red, 2.0f),
          "border per-corner: the band resumes past BR's inner radius 10");
    CHECK(pixel_color(pixel_at(&buf, 612, 64), red, 2.0f) && pixel_transparent(pixel_at(&buf, 620, 64))
              && pixel_color(pixel_at(&buf, 640, 95), red, 2.0f) && pixel_transparent(pixel_at(&buf, 640, 94)),
          "border per-corner: the straight-edge bands still match their own side widths");

    // B8: width 4.5 puts the inner edge through pixel-center column 708.5 - opaque band, exact half
    // coverage, then the untouched hole.
    CHECK(pixel_color(pixel_at(&buf, 707, 64), red, 2.0f),
          "border fractional: the band pixel before the inner edge is fully covered");
    CHECK(channels_near(pixel_at(&buf, 708, 64), 200.0f * 0.5f, 40.0f * 0.5f, 50.0f * 0.5f, 127.5f, 3.0f),
          "border fractional: the inner-edge pixel carries analytic half coverage, premultiplied");
    CHECK(pixel_transparent(pixel_at(&buf, 709, 64)),
          "border fractional: the hole begins right past the fractional inner edge");

    // B9: each side measures its own run - the top band reads the pattern across x while the left band reads
    // it down y, so a probe that is a dash on one axis and a gap on the other pins which run the pixel was
    // assigned to. Every probe sits >= 3px of arc length clear of a dash boundary, past the 0.5px AA ramp.
    CHECK(pixel_color(pixel_at(&buf, 804, 34), red, 2.0f), "border dashed: the top band is drawn inside a dash");
    CHECK(pixel_transparent(pixel_at(&buf, 812, 34)), "border dashed: the top band is cut away inside a gap");
    CHECK(pixel_color(pixel_at(&buf, 820, 34), red, 2.0f), "border dashed: the pattern repeats along the run");
    CHECK(pixel_color(pixel_at(&buf, 802, 36), red, 2.0f),
          "border dashed: the left band runs the pattern down its own edge");
    CHECK(pixel_transparent(pixel_at(&buf, 802, 44)),
          "border dashed: the left band's gaps fall by distance down the edge, not across it");
    CHECK(pixel_transparent(pixel_at(&buf, 832, 64)), "border dashed: the interior hole is still untouched");

    printf("gpu_smoke_test: border pixel tests PASS\n");
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Image pixel tests - synthetic decodes go through the backend's prepare_image seam (render
// thread; the GPU backend leaves prepare_image_on_job unset), then FlCmd_Image frames property-check both
// modes off the readback. Atlas placement is backend-private, so every expectation reads only rendered pixels.

static int run_image_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    CHECK(!backend->prepare_image_on_job,
          "image: the GPU backend must defer prepare_image to the render thread (thread-confined GL uploads)");
    CHECK(backend->prepare_image != NULL && backend->release_image != NULL,
          "image: the backend must expose the prepare and release seams");

    FlArena* scratch = arena_new();
    FlImageLoadRequest req = { .relative_path = FL_S("synthetic") };

    // Color-mode source: 8x8 opaque RGBA, every texel derived from its coordinates. Opaque texels keep
    // premultiplied == straight, so expectations stay exact.
    enum { IMG_W = 8, IMG_H = 8 };
    static u8 rgba[IMG_W * IMG_H * 4];
    for (int y = 0; y < IMG_H; y++) {
        for (int x = 0; x < IMG_W; x++) {
            u8* px = &rgba[(y * IMG_W + x) * 4];
            px[0] = (u8)(x * 30);
            px[1] = (u8)(y * 30);
            px[2] = 77;
            px[3] = 255;
        }
    }
    FlImageDecodeResult color_decode = {
        .pixels = rgba,
        .width = IMG_W,
        .height = IMG_H,
        .stride = IMG_W * 4,
        .channels = 4,
        .premultiplied = true,
    };
    backend->prepare_image(backend->user_data, 101, &color_decode, scratch, &req);

    // Alpha-coverage source: 4x4, one alpha step per column (0, 85, 170, 255).
    static u8 alpha_px[4 * 4];
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            alpha_px[y * 4 + x] = (u8)(x * 85);
        }
    }
    FlImageDecodeResult alpha_decode = { .pixels = alpha_px, .width = 4, .height = 4, .stride = 4, .channels = 1 };
    backend->prepare_image(backend->user_data, 102, &alpha_decode, scratch, &req);

    // Filter probe: 2x1 black|white, scaled 8x so linear and nearest sampling are distinguishable.
    static u8 ramp_px[2 * 4] = { 0, 0, 0, 255, 255, 255, 255, 255 };
    FlImageDecodeResult ramp_decode = { .pixels = ramp_px, .width = 2, .height = 1, .stride = 8, .channels = 4 };
    backend->prepare_image(backend->user_data, 103, &ramp_decode, scratch, &req);

    // Strided source: 2x2 RGBA whose rows carry 4 bytes of padding (stride 12 over a tight 8) - the upload
    // path must repack rows, never read the padding.
    static u8 strided_px[2 * 12] = {
        10, 20, 30, 255, 40,  50,  60,  255, 0xAA, 0xAA, 0xAA, 0xAA, // row 0 + padding
        70, 80, 90, 255, 110, 120, 130, 255, 0xAA, 0xAA, 0xAA, 0xAA, // row 1 + padding
    };
    FlImageDecodeResult strided_decode = {
        .pixels = strided_px,
        .width = 2,
        .height = 2,
        .stride = 12,
        .channels = 4,
        .premultiplied = true,
    };
    backend->prepare_image(backend->user_data, 104, &strided_decode, scratch, &req);

    // Oversized source: past the per-image cap the seam routes to a dedicated slot texture instead of the
    // atlas - the handle keeps rendering. The slot-image section owns the thorough coverage.
    static u8 big_dummy[(GPU_IMAGE_ATLAS_MAX_DIM + 1) * 4 * 4];
    memset(big_dummy, 0x40, sizeof(big_dummy));
    FlImageDecodeResult big_decode = {
        .pixels = big_dummy,
        .width = GPU_IMAGE_ATLAS_MAX_DIM + 1,
        .height = 4,
        .stride = (GPU_IMAGE_ATLAS_MAX_DIM + 1) * 4,
        .channels = 4,
        .premultiplied = true,
    };
    backend->prepare_image(backend->user_data, 105, &big_decode, scratch, &req);

    FlRenderFrame frame = test_frame();
    FlPixelReadback buf;
    const FlImageSource img_color = { .kind = FlImageKind_Static, .handle = 101 };
    const FlImageSource img_alpha = { .kind = FlImageKind_Static, .handle = 102 };
    const FlImageSource img_ramp = { .kind = FlImageKind_Static, .handle = 103 };

    void* start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, img_color, FlImageMode_Color, (FlRect) { 16, 16, 8, 8 }, (FlRect) { 0 }, white,
                FlImageFilter_Nearest, nullptr); // nearest, native
    fl_ir_image(ir_arena, img_color, FlImageMode_Color, (FlRect) { 48, 16, 8, 8 }, (FlRect) { 0 }, white,
                FlImageFilter_Linear, nullptr); // linear, native
    fl_ir_image(ir_arena, img_color, FlImageMode_Color, (FlRect) { 80, 16, 8, 8 }, (FlRect) { 0 },
                (FlColor) { 128, 255, 255, 255 }, FlImageFilter_Nearest, nullptr); // tinted
    fl_ir_image(ir_arena, img_color, FlImageMode_Color, (FlRect) { 112, 16, 4, 4 }, (FlRect) { 4, 4, 4, 4 }, white,
                FlImageFilter_Nearest, nullptr); // sub-rect
    fl_ir_image(ir_arena, img_alpha, FlImageMode_AlphaCoverage, (FlRect) { 16, 48, 4, 4 }, (FlRect) { 0 },
                (FlColor) { 200, 40, 50, 255 }, FlImageFilter_Nearest, nullptr); // alpha-coverage
    fl_ir_image(ir_arena, img_ramp, FlImageMode_Color, (FlRect) { 48, 48, 16, 4 }, (FlRect) { 0 }, white,
                FlImageFilter_Linear, nullptr); // ramp, linear
    fl_ir_image(ir_arena, img_ramp, FlImageMode_Color, (FlRect) { 48, 64, 16, 4 }, (FlRect) { 0 }, white,
                FlImageFilter_Nearest, nullptr); // ramp, nearest
    fl_ir_image(ir_arena, (FlImageSource) { .kind = FlImageKind_Static, .handle = 104 }, FlImageMode_Color,
                (FlRect) { 80, 48, 2, 2 }, (FlRect) { 0 }, white, FlImageFilter_Nearest, nullptr); // strided source
    fl_ir_image(ir_arena, (FlImageSource) { .kind = FlImageKind_Static, .handle = 105 }, FlImageMode_Color,
                (FlRect) { 96, 48, 8, 8 }, (FlRect) { 0 }, white, FlImageFilter_Nearest, nullptr); // large: slot tier
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the image frame");

    // Color mode at native size is texel passthrough - for nearest and linear alike (pixel centers map to
    // texel centers, where linear weights collapse to one texel).
    for (int y = 0; y < IMG_H; y++) {
        for (int x = 0; x < IMG_W; x++) {
            CHECK(channels_near(pixel_at(&buf, 16 + x, 16 + y), (f32)(x * 30), (f32)(y * 30), 77, 255, 1.0f),
                  "image color: nearest at native size must pass every texel through exactly");
            CHECK(channels_near(pixel_at(&buf, 48 + x, 16 + y), (f32)(x * 30), (f32)(y * 30), 77, 255, 1.0f),
                  "image color: linear at native size samples texel centers — also exact passthrough");
        }
    }
    CHECK(pixel_transparent(pixel_at(&buf, 15, 20)) && pixel_transparent(pixel_at(&buf, 24, 20)),
          "image color: nothing renders outside the dst rect");

    // Tint modulation: the red channel halves (128/255), the others pass through.
    CHECK(channels_near(pixel_at(&buf, 84, 18), 120.0f * 128.0f / 255.0f, 60, 77, 255, 2.0f),
          "image color: the tint must modulate channels independently");

    // The src sub-rect shows exactly the bottom-right source quadrant.
    CHECK(channels_near(pixel_at(&buf, 112, 16), 4 * 30, 4 * 30, 77, 255, 1.0f)
              && channels_near(pixel_at(&buf, 115, 19), 7 * 30, 7 * 30, 77, 255, 1.0f),
          "image color: a src sub-rect must sample only its region");

    // Alpha-coverage: stored white x alpha, colorized by the tint - column x carries alpha x * 85,
    // premultiplied against the tint.
    for (int x = 0; x < 4; x++) {
        f32 a = (f32)(x * 85);
        CHECK(channels_near(pixel_at(&buf, 16 + x, 49), a * 200.0f / 255.0f, a * 40.0f / 255.0f, a * 50.0f / 255.0f, a,
                            3.0f),
              "image alpha-coverage: the tint colorizes stored white x alpha, premultiplied");
    }

    {
        u8 prev = 0;
        bool saw_mid = false;
        for (int x = 0; x < 16; x++) {
            const u8* lp = pixel_at(&buf, 48 + x, 50);
            CHECK(lp[0] >= prev, "image linear: the scaled ramp must rise monotonically");
            prev = lp[0];
            if (lp[0] > 32 && lp[0] < 224) {
                saw_mid = true;
            }
            const u8* np = pixel_at(&buf, 48 + x, 66);
            CHECK(np[0] <= 1 || np[0] >= 254, "image nearest: the scaled ramp must hold only the two texel values");
        }
        CHECK(saw_mid, "image linear: the scaled ramp must pass through intermediate values");
        CHECK(pixel_at(&buf, 48, 66)[0] <= 1 && pixel_at(&buf, 63, 66)[0] >= 254,
              "image nearest: the step must run black to white across the dst");
    }

    CHECK(pixel_color(pixel_at(&buf, 80, 48), (FlColor) { 10, 20, 30, 255 }, 1.0f)
              && pixel_color(pixel_at(&buf, 81, 48), (FlColor) { 40, 50, 60, 255 }, 1.0f)
              && pixel_color(pixel_at(&buf, 80, 49), (FlColor) { 70, 80, 90, 255 }, 1.0f)
              && pixel_color(pixel_at(&buf, 81, 49), (FlColor) { 110, 120, 130, 255 }, 1.0f),
          "image strided: row padding must be repacked away, every texel exact");

    CHECK(pixel_color(pixel_at(&buf, 100, 52), (FlColor) { 64, 64, 64, 64 }, 1.0f),
          "image oversize: an image past the per-image cap renders from its slot texture");

    backend->release_image(backend->user_data, 101);
    start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, img_color, FlImageMode_Color, (FlRect) { 16, 16, 8, 8 }, (FlRect) { 0 }, white,
                FlImageFilter_Nearest, nullptr);
    fl_ir_image(ir_arena, img_alpha, FlImageMode_AlphaCoverage, (FlRect) { 16, 48, 4, 4 }, (FlRect) { 0 },
                (FlColor) { 200, 40, 50, 255 }, FlImageFilter_Nearest, nullptr);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed after release_image");
    CHECK(pixel_transparent(pixel_at(&buf, 20, 20)), "image release: a released handle must stop rendering");
    CHECK(channels_near(pixel_at(&buf, 19, 49), 255.0f * 200.0f / 255.0f, 255.0f * 40.0f / 255.0f,
                        255.0f * 50.0f / 255.0f, 255, 3.0f),
          "image release: other images keep rendering after an unrelated release");

    fl_arena_destroy(scratch);
    printf("gpu_smoke_test: image tests PASS\n");
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

#endif // !defined(__APPLE__) - pause the GL-only block for the cross-backend slot-image suite below

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cross-backend slot-image pixel tests - the non-atlas image paths end-to-end: large statics on
// dedicated slot textures, dynamic buffers streamed on referencing frames, strided rows, full slot occupancy,
// and release_image on the slot tier. Runs on BOTH backends through the neutral seam; slot assignment is
// backend-private, so every expectation reads only rendered pixels and no per-backend golden fork is needed.
static int run_slot_image_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    FlArena* scratch = arena_new();
    FlImageLoadRequest req = { .relative_path = FL_S("synthetic-large") };

    // Large color source: 300 wide (past the atlas's 256 cap), every texel derived from its coordinates,
    // with a hard blue split at x == 150 so linear and nearest sampling are distinguishable when scaled.
    // Opaque texels keep premultiplied == straight, so expectations stay exact.
    enum { LW = 300, LH = 8 };
    static u8 large_px[LW * LH * 4];
    for (int y = 0; y < LH; y++) {
        for (int x = 0; x < LW; x++) {
            u8* px = &large_px[(y * LW + x) * 4];
            px[0] = (u8)x;
            px[1] = (u8)(y * 30);
            px[2] = x >= 150 ? 255 : 0;
            px[3] = 255;
        }
    }
    FlImageDecodeResult large_decode = {
        .pixels = large_px,
        .width = LW,
        .height = LH,
        .stride = LW * 4,
        .channels = 4,
        .premultiplied = true,
    };
    backend->prepare_image(backend->user_data, 201, &large_decode, scratch, &req);

    // Large alpha-coverage source: 300x4 single-channel, one alpha per column.
    static u8 large_alpha[LW * 4];
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < LW; x++) {
            large_alpha[y * LW + x] = (u8)x;
        }
    }
    // Stride 0 = tightly packed, so the upload derives the row pitch as w * bpp itself - the single-channel
    // side of that fallback, which a source spelling its tight stride explicitly never reaches.
    FlImageDecodeResult alpha_decode = { .pixels = large_alpha, .width = LW, .height = 4, .stride = 0, .channels = 1 };
    backend->prepare_image(backend->user_data, 202, &alpha_decode, scratch, &req);

    FlRenderFrame frame = test_frame();
    FlPixelReadback buf;
    const FlImageSource img_large = { .kind = FlImageKind_Static, .handle = 201 };
    const FlImageSource img_alpha = { .kind = FlImageKind_Static, .handle = 202 };
    const FlColor alpha_tint = { 200, 40, 50, 255 };

    enum { DW = 16, DH = 16 };
    static u8 dyn_px[DW * DH * 4];
    for (int i = 0; i < DW * DH; i++) {
        dyn_px[i * 4 + 0] = 10;
        dyn_px[i * 4 + 1] = 200;
        dyn_px[i * 4 + 2] = 30;
        dyn_px[i * 4 + 3] = 255;
    }
    static u8 dyn_alpha[8];
    for (int x = 0; x < 8; x++) {
        dyn_alpha[x] = (u8)(x * 36);
    }
    // Strided dynamic buffers: 4x2, one with whole-pixel padding (stride 24, the GL_UNPACK_ROW_LENGTH
    // path), one with a stride no whole pixel count reaches (18, the repack path). Padding is sentinel.
    static u8 dyn_strided[24 * 2];
    static u8 dyn_repack[18 * 2];
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
    const FlImageSource img_dyn
        = { .kind = FlImageKind_Dynamic, .dynamic = { .pixels = dyn_px, .w = DW, .h = DH, .stride = DW * 4 } };
    // Stride 0: tightly packed, the per-frame dynamic stream deriving w * bpp for a single-channel source.
    const FlImageSource img_dyn_alpha
        = { .kind = FlImageKind_Dynamic, .dynamic = { .pixels = dyn_alpha, .w = 8, .h = 1, .stride = 0 } };
    const FlImageSource img_dyn_strided
        = { .kind = FlImageKind_Dynamic, .dynamic = { .pixels = dyn_strided, .w = 4, .h = 2, .stride = 24 } };
    const FlImageSource img_dyn_repack
        = { .kind = FlImageKind_Dynamic, .dynamic = { .pixels = dyn_repack, .w = 4, .h = 2, .stride = 18 } };

    // Frame 1: the slot-image matrix - large static native and scaled (nearest), src sub-rect, large
    // alpha-coverage colorization, and all four dynamic buffers side by side.
    void* start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, img_large, FlImageMode_Color, (FlRect) { 8, 8, LW, LH }, (FlRect) { 0 }, white,
                FlImageFilter_Nearest, nullptr); // native
    fl_ir_image(ir_arena, img_large, FlImageMode_Color, (FlRect) { 8, 24, 600, 16 }, (FlRect) { 0 }, white,
                FlImageFilter_Nearest, nullptr); // 2x up
    fl_ir_image(ir_arena, img_large, FlImageMode_Color, (FlRect) { 700, 24, 100, 8 }, (FlRect) { 0 }, white,
                FlImageFilter_Nearest, nullptr); // 3x down
    fl_ir_image(ir_arena, img_large, FlImageMode_Color, (FlRect) { 860, 24, 80, 8 }, (FlRect) { 260, 0, 40, 8 }, white,
                FlImageFilter_Nearest, nullptr); // sub-rect 2x
    fl_ir_image(ir_arena, img_alpha, FlImageMode_AlphaCoverage, (FlRect) { 8, 48, LW, 4 }, (FlRect) { 0 }, alpha_tint,
                FlImageFilter_Nearest, nullptr); // colorize
    fl_ir_image(ir_arena, img_dyn, FlImageMode_Color, (FlRect) { 400, 48, DW, DH }, (FlRect) { 0 }, white,
                FlImageFilter_Nearest, nullptr);
    fl_ir_image(ir_arena, img_dyn_alpha, FlImageMode_AlphaCoverage, (FlRect) { 448, 48, 8, 1 }, (FlRect) { 0 },
                (FlColor) { 40, 200, 60, 255 }, FlImageFilter_Nearest, nullptr);
    fl_ir_image(ir_arena, img_dyn_strided, FlImageMode_Color, (FlRect) { 480, 48, 4, 2 }, (FlRect) { 0 }, white,
                FlImageFilter_Nearest, nullptr);
    fl_ir_image(ir_arena, img_dyn_repack, FlImageMode_Color, (FlRect) { 500, 48, 4, 2 }, (FlRect) { 0 }, white,
                FlImageFilter_Nearest, nullptr);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the slot-image frame");

    // Native size is texel passthrough - including columns past the 256 atlas cap, which proves the
    // command samples a dedicated slot texture, not an atlas cell.
    {
        const int xs[] = { 0, 1, 128, 149, 150, 255, 256, 270, 299 };
        for (size_t i = 0; i < sizeof(xs) / sizeof(xs[0]); i++) {
            int x = xs[i];
            f32 b = x >= 150 ? 255.0f : 0.0f;
            CHECK(channels_near(pixel_at(&buf, 8 + x, 8), (f32)(x & 255), 0, b, 255, 1.0f)
                      && channels_near(pixel_at(&buf, 8 + x, 15), (f32)(x & 255), 210, b, 255, 1.0f),
                  "slot image: native size must pass every sampled texel through exactly");
        }
    }
    CHECK(pixel_transparent(pixel_at(&buf, 7, 10)) && pixel_transparent(pixel_at(&buf, 309, 10)),
          "slot image: nothing renders outside the dst rect");

    // Scaled into a dst rect (nearest): a 2x upscale maps dst pixel pairs to one texel; a 3x downscale
    // samples texel 3x+1 (a mid-texel coordinate, so no fetch sits on a texel boundary); the src sub-rect
    // shows only its region, scaled.
    {
        const int xs[] = { 0, 10, 149, 150, 200, 299 };
        for (size_t i = 0; i < sizeof(xs) / sizeof(xs[0]); i++) {
            int x = xs[i];
            f32 b = x >= 150 ? 255.0f : 0.0f;
            CHECK(channels_near(pixel_at(&buf, 8 + 2 * x, 24 + 2), (f32)(x & 255), 30, b, 255, 1.0f)
                      && channels_near(pixel_at(&buf, 8 + 2 * x + 1, 24 + 2), (f32)(x & 255), 30, b, 255, 1.0f),
                  "slot image scaled: a 2x nearest upscale maps each dst pixel pair to its texel");
        }
    }
    {
        const int xs[] = { 0, 33, 49, 50, 99 };
        for (size_t i = 0; i < sizeof(xs) / sizeof(xs[0]); i++) {
            int x = xs[i];
            int texel = 3 * x + 1;
            f32 b = texel >= 150 ? 255.0f : 0.0f;
            CHECK(channels_near(pixel_at(&buf, 700 + x, 24 + 1), (f32)(texel & 255), 30, b, 255, 1.0f),
                  "slot image scaled: a 3x nearest downscale samples texel 3x+1");
        }
    }
    for (int x = 0; x < 80; x += 13) {
        int texel = 260 + x / 2; // 2x upscale of the {260, 0, 40, 8} sub-rect: dst pixel pairs share a texel
        CHECK(channels_near(pixel_at(&buf, 860 + x, 24 + 1), (f32)(texel & 255), 30, 255, 255, 1.0f),
              "slot image: a src sub-rect samples only its region, scaled");
    }

    {
        const int xs[] = { 0, 100, 255, 280 };
        for (size_t i = 0; i < sizeof(xs) / sizeof(xs[0]); i++) {
            f32 a = (f32)(xs[i] & 255);
            CHECK(channels_near(pixel_at(&buf, 8 + xs[i], 49), a * 200.0f / 255.0f, a * 40.0f / 255.0f,
                                a * 50.0f / 255.0f, a, 3.0f),
                  "slot image alpha-coverage: the tint colorizes coverage, premultiplied");
        }
    }

    CHECK(pixel_color(pixel_at(&buf, 408, 56), (FlColor) { 10, 200, 30, 255 }, 1.0f),
          "dynamic image: the first referencing frame renders the buffer content");

    for (int x = 0; x < 8; x++) {
        f32 a = (f32)(x * 36);
        CHECK(channels_near(pixel_at(&buf, 448 + x, 48), a * 40.0f / 255.0f, a * 200.0f / 255.0f, a * 60.0f / 255.0f, a,
                            3.0f),
              "dynamic image alpha-coverage: the tint colorizes the live coverage buffer");
    }

    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 4; x++) {
            CHECK(channels_near(pixel_at(&buf, 480 + x, 48 + y), (f32)(40 * x + 5), (f32)(80 * y + 10), 99, 255, 1.0f),
                  "dynamic strided: whole-pixel row padding rides the upload without leaking");
            CHECK(channels_near(pixel_at(&buf, 500 + x, 48 + y), (f32)(50 * x + 3), (f32)(90 * y + 7), 7, 255, 1.0f),
                  "dynamic strided: sub-pixel row padding repacks without leaking");
        }
    }

    // Frame 2: mutate both dynamic buffers and re-render the same commands. The color buffer's new alpha of
    // 128 must NOT show: dynamic color renders opaque, matching the SW backend's planar (alpha-dropping)
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
    start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, img_dyn, FlImageMode_Color, (FlRect) { 400, 48, DW, DH }, (FlRect) { 0 }, white,
                FlImageFilter_Nearest, nullptr);
    fl_ir_image(ir_arena, img_dyn_alpha, FlImageMode_AlphaCoverage, (FlRect) { 448, 48, 8, 1 }, (FlRect) { 0 },
                (FlColor) { 40, 200, 60, 255 }, FlImageFilter_Nearest, nullptr);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the mutated dynamic frame");
    CHECK(pixel_color(pixel_at(&buf, 408, 56), (FlColor) { 200, 20, 160, 255 }, 1.0f),
          "dynamic image: a content change between frames re-uploads, and color mode renders opaque");
    for (int x = 0; x < 8; x++) {
        f32 a = (f32)(252 - x * 36);
        CHECK(channels_near(pixel_at(&buf, 448 + x, 48), a * 40.0f / 255.0f, a * 200.0f / 255.0f, a * 60.0f / 255.0f, a,
                            3.0f),
              "dynamic image alpha-coverage: a content change between frames re-uploads");
    }

    // Frame 3: linear filtering on a slot texture - the slot's sampler comes from the referencing command.
    // A 2x linear upscale across the blue split at texel 150 ramps through genuine intermediates where
    // nearest (frame 1's 2x upscale) stepped 0 -> 255 between adjacent pixel pairs.
    start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, img_large, FlImageMode_Color, (FlRect) { 8, 96, 600, 8 }, (FlRect) { 0 }, white,
                FlImageFilter_Linear, nullptr);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the linear slot frame");
    CHECK(pixel_at(&buf, 8 + 298, 97)[2] <= 32 && pixel_at(&buf, 8 + 301, 97)[2] >= 224,
          "slot image linear: the split stays sharp away from the boundary");
    CHECK(pixel_at(&buf, 8 + 299, 97)[2] > 32 && pixel_at(&buf, 8 + 299, 97)[2] < 224
              && pixel_at(&buf, 8 + 300, 97)[2] > 32 && pixel_at(&buf, 8 + 300, 97)[2] < 224,
          "slot image linear: a 2x upscale ramps through genuine intermediates at the split");
    CHECK(pixel_at(&buf, 8 + 299, 97)[2] < pixel_at(&buf, 8 + 300, 97)[2],
          "slot image linear: the ramp rises monotonically across the split");

    // Frame 4: full slot occupancy - GPU_IMAGE_SLOT_COUNT + 2 distinct dynamic images in one frame, so the
    // trailing two land past the sampler array's capacity.
    {
        enum { OV_COUNT = GPU_IMAGE_SLOT_COUNT + 2 };
        static u8 ov_px[OV_COUNT][4];
        start = ir_rewind_begin(ir_arena);
        for (int i = 0; i < OV_COUNT; i++) {
            ov_px[i][0] = (u8)(10 * i + 5);
            ov_px[i][1] = 77;
            ov_px[i][2] = 200;
            ov_px[i][3] = 255;
            fl_ir_image(ir_arena,
                        (FlImageSource) { .kind = FlImageKind_Dynamic,
                                          .dynamic = { .pixels = ov_px[i], .w = 1, .h = 1, .stride = 4 } },
                        FlImageMode_Color, (FlRect) { (f32)(8 + i * 8), 80, 4, 4 }, (FlRect) { 0 }, white,
                        FlImageFilter_Nearest, nullptr);
        }
        ir_frame_render(backend, surface, &frame, ir_arena, start);
        CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the overflow frame");
        for (int i = 0; i < OV_COUNT; i++) {
            if (i < GPU_IMAGE_SLOT_COUNT) {
                CHECK(channels_near(pixel_at(&buf, 9 + i * 8, 81), (f32)(10 * i + 5), 77, 200, 255, 1.0f),
                      "slot overflow: every slotted image renders its own texture");
            } else {
                CHECK(pixel_transparent(pixel_at(&buf, 9 + i * 8, 81)),
                      "slot overflow: commands past the slot capacity drop to transparent, predictably");
            }
        }
    }

    // Upload-streaming contract: a dynamic image uploads only on frames that reference it. The debug counter
    // ticks once per dynamic stream.
    {
        static u8 stream_px[4 * 4 * 4];
        for (int i = 0; i < 4 * 4; i++) {
            stream_px[i * 4 + 0] = 60;
            stream_px[i * 4 + 1] = 120;
            stream_px[i * 4 + 2] = 180;
            stream_px[i * 4 + 3] = 255;
        }
        const FlImageSource img_stream
            = { .kind = FlImageKind_Dynamic, .dynamic = { .pixels = stream_px, .w = 4, .h = 4, .stride = 16 } };

        u32 c0 = gpu_debug_dynamic_upload_count(backend->user_data);

        start = ir_rewind_begin(ir_arena);
        fl_ir_image(ir_arena, img_stream, FlImageMode_Color, (FlRect) { 8, 120, 4, 4 }, (FlRect) { 0 }, white,
                    FlImageFilter_Nearest, nullptr);
        ir_frame_render(backend, surface, &frame, ir_arena, start);
        u32 c1 = gpu_debug_dynamic_upload_count(backend->user_data);
        CHECK(c1 == c0 + 1, "dynamic upload counter: a referencing frame streams the buffer exactly once");

        // A plain static draw, no dynamic image - the entry stays live but uploads nothing.
        start = ir_rewind_begin(ir_arena);
        fl_ir_image(ir_arena, img_alpha, FlImageMode_AlphaCoverage, (FlRect) { 8, 48, LW, 4 }, (FlRect) { 0 },
                    alpha_tint, FlImageFilter_Nearest, nullptr);
        ir_frame_render(backend, surface, &frame, ir_arena, start);
        u32 c2 = gpu_debug_dynamic_upload_count(backend->user_data);
        CHECK(c2 == c1, "dynamic upload counter: a frame that references no dynamic image uploads nothing");

        start = ir_rewind_begin(ir_arena);
        fl_ir_image(ir_arena, img_stream, FlImageMode_Color, (FlRect) { 8, 120, 4, 4 }, (FlRect) { 0 }, white,
                    FlImageFilter_Nearest, nullptr);
        ir_frame_render(backend, surface, &frame, ir_arena, start);
        u32 c3 = gpu_debug_dynamic_upload_count(backend->user_data);
        CHECK(c3 == c2 + 1, "dynamic upload counter: re-referencing the buffer streams it again");
    }

    // Release: the released image's texture is deleted on the next render.
    backend->release_image(backend->user_data, 201);
    start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, img_large, FlImageMode_Color, (FlRect) { 8, 8, LW, LH }, (FlRect) { 0 }, white,
                FlImageFilter_Nearest, nullptr);
    fl_ir_image(ir_arena, img_alpha, FlImageMode_AlphaCoverage, (FlRect) { 8, 48, LW, 4 }, (FlRect) { 0 }, alpha_tint,
                FlImageFilter_Nearest, nullptr);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed after slot release");
    CHECK(pixel_transparent(pixel_at(&buf, 108, 12)), "slot release: a released large image stops rendering");
    CHECK(channels_near(pixel_at(&buf, 8 + 100, 49), 100.0f * 200.0f / 255.0f, 100.0f * 40.0f / 255.0f,
                        100.0f * 50.0f / 255.0f, 100, 3.0f),
          "slot release: other slot images keep rendering after an unrelated release");

    fl_arena_destroy(scratch);
    printf("gpu_smoke_test: slot-image pixel tests PASS\n");
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

#if !defined(__APPLE__) // resume the GL-only block (the background suite below stays GL-only for now)
// An axis-aligned quad in clip space at a fixed depth, UVs spanning the whole texture - the shape every 3D draw
// uses, differing only in x extent, depth, and color. It sat outside the GL-only block for the viewport3d
// fl_app case, but that case only ever runs from the GL/EGL branch of main(), so every caller is in here.
typedef struct TestQuad {
    FlMeshVertex verts[4];
} TestQuad;

static TestQuad test_quad(f32 left, f32 right, f32 depth, FlColor color) {
    return (TestQuad) { .verts = {
                            { .x = left, .y = -0.8f, .z = depth, .u = 0.0f, .v = 0.0f, .color = color },
                            { .x = right, .y = -0.8f, .z = depth, .u = 1.0f, .v = 0.0f, .color = color },
                            { .x = right, .y = 0.8f, .z = depth, .u = 1.0f, .v = 1.0f, .color = color },
                            { .x = left, .y = 0.8f, .z = depth, .u = 0.0f, .v = 1.0f, .color = color },
                        } };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Background pixel tests: the frame envelope's background composited end-to-end. Two solid
// fullscreen-tier sources (300 wide, past the atlas cap, so they ride slot textures like real backgrounds)
// make every expectation a closed-form composite the readback can check.
static int run_background_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    FlArena* scratch = arena_new();
    FlImageLoadRequest req = { .relative_path = FL_S("synthetic-background") };

    enum { BW = 300, BH = 8 };
    static u8 prev_px[BW * BH * 4];
    static u8 curr_px[BW * BH * 4];
    for (int i = 0; i < BW * BH; i++) {
        prev_px[i * 4 + 0] = 200;
        prev_px[i * 4 + 1] = 40;
        prev_px[i * 4 + 2] = 50;
        prev_px[i * 4 + 3] = 255;
        curr_px[i * 4 + 0] = 40;
        curr_px[i * 4 + 1] = 50;
        curr_px[i * 4 + 2] = 200;
        curr_px[i * 4 + 3] = 255;
    }
    FlImageDecodeResult prev_decode = {
        .pixels = prev_px,
        .width = BW,
        .height = BH,
        .stride = BW * 4,
        .channels = 4,
        .premultiplied = true,
    };
    FlImageDecodeResult curr_decode = {
        .pixels = curr_px,
        .width = BW,
        .height = BH,
        .stride = BW * 4,
        .channels = 4,
        .premultiplied = true,
    };
    backend->prepare_image(backend->user_data, 301, &prev_decode, scratch, &req); // prev = solid red
    backend->prepare_image(backend->user_data, 302, &curr_decode, scratch, &req); // current = solid blue

    FlRenderFrame frame = test_frame();
    FlPixelReadback buf;
    const FlColor clear = { 20, 30, 40, 255 };

    void* start = ir_rewind_begin(ir_arena);
    frame.background
        = (FlBackground) { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID, .clear = clear };
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the clear-only frame");
    CHECK(pixel_color(pixel_at(&buf, 960, 540), (FlColor) { 20, 30, 40, 255 }, 2.0f),
          "background: the clear color fills the target");
    CHECK(pixel_color(pixel_at(&buf, 8, 8), (FlColor) { 20, 30, 40, 255 }, 2.0f),
          "background: the clear reaches the corners");

    // A single background image composites with its alpha over the clear: lerp(clear, image, alpha/255).
    start = ir_rewind_begin(ir_arena);
    frame.background = (FlBackground) { .current = 302, .prev = FL_IMAGE_HANDLE_INVALID, .alpha = 128, .clear = clear };
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the single-image frame");
    {
        f32 a = 128.0f / 255.0f;
        CHECK(channels_near(pixel_at(&buf, 960, 540), 20 * (1 - a) + 40 * a, 30 * (1 - a) + 50 * a,
                            40 * (1 - a) + 200 * a, 255, 3.0f),
              "background: a single image composites with its alpha over the clear");
    }

    // Crossfade interpolates with crossfade_t: prev (red) to current (blue), checked at t = 0, 0.5, 1.
    const struct {
        f32 t;
        f32 r, g, b;
    } cases[] = {
        { 0.0f, 200, 40, 50 },  // full prev
        { 0.5f, 120, 45, 125 }, // lerp(red, blue, ~0.5)
        { 1.0f, 40, 50, 200 },  // full current
    };
    for (int i = 0; i < 3; i++) {
        start = ir_rewind_begin(ir_arena);
        frame.background = (FlBackground) { .current = 302, .prev = 301, .crossfade_t = cases[i].t, .clear = clear };
        ir_frame_render(backend, surface, &frame, ir_arena, start);
        CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on a crossfade frame");
        CHECK(channels_near(pixel_at(&buf, 960, 540), cases[i].r, cases[i].g, cases[i].b, 255, 3.0f),
              "crossfade: the blend matches lerp(prev, current, t) at t in {0, 0.5, 1}");
    }

    start = ir_rewind_begin(ir_arena);
    const FlColor green = { 40, 200, 60, 255 };
    emit_solid_rect(ir_arena, 100, 100, 200, 200, green);
    frame.background
        = (FlBackground) { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID, .clear = clear };
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the UI-over-background frame");
    CHECK(pixel_color(pixel_at(&buf, 200, 200), green, 2.0f), "background: the UI rect draws over the background");
    CHECK(pixel_color(pixel_at(&buf, 500, 500), (FlColor) { 20, 30, 40, 255 }, 2.0f),
          "background: the clear shows where no UI covers");

    fl_arena_destroy(scratch);
    printf("gpu_smoke_test: background pixel tests PASS\n");
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Effect rendering: a drop shadow casts the shape's silhouette offset + blurred and under the
// primitive; a glow casts it centered + blurred and additive. The shape's own coverage rides on top untouched.
// The readback is premultiplied coverage over a transparent target, so every expectation below is computable
// from the shape's SDF.

static int run_effect_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    FlRenderFrame frame = test_frame();
    FlPixelReadback buf;

    // A red square [100,100]..[140,140] with an opaque green drop shadow offset (30, 30), blur 4. The shadow's
    // silhouette is the square shifted by the offset: [130,130]..[170,170], blurred at its edges.
    const FlColor shadow_color = { 30, 220, 40, 255 };
    FlEffect shadow
        = { .kind = FlEffectKind_DropShadow, .color = shadow_color, .off_x = 30.0f, .off_y = 30.0f, .blur = 4.0f };

    void* start = ir_rewind_begin(ir_arena);
    emit_rect_effect(ir_arena, 100.0f, 100.0f, 40.0f, 40.0f, red, &shadow);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the drop-shadow frame");

    CHECK(pixel_color(pixel_at(&buf, 160, 160), (FlColor) { 30, 220, 40, 255 }, 3.0f),
          "drop shadow: present at full coverage in the offset silhouette, clear of the primitive");
    CHECK(pixel_transparent(pixel_at(&buf, 300, 300)), "drop shadow: no shadow far outside the silhouette's reach");
    CHECK(pixel_color(pixel_at(&buf, 135, 135), red, 2.0f),
          "drop shadow: a covered interior pixel is the primitive alone, the shadow hidden beneath it");

    // Walk inward across the shadow's right edge (at x = 170, along y = 160, clear of the square at x <= 140).
    CHECK(check_aa_ramp_line(&buf, 178, 160, -1, 0, 16, shadow_color, true, "drop shadow falloff") == 0,
          "drop shadow: a blurred edge falls off monotonically, premultiplied by coverage");

    // Glow over a transparent target: centered (the offset is ignored), so it bleeds symmetrically on both
    // sides of the square. A blur-6 green glow on the same red square.
    const FlColor glow_color = { 0, 200, 0, 255 };
    FlEffect glow = { .kind = FlEffectKind_Glow, .color = glow_color, .off_x = 30.0f, .off_y = 30.0f, .blur = 6.0f };
    start = ir_rewind_begin(ir_arena);
    emit_rect_effect(ir_arena, 100.0f, 100.0f, 40.0f, 40.0f, red, &glow);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the glow frame");
    // Pixels mirrored about the square's center column (x = 120): x = 143 and x = 96 are the same distance
    // outside the right and left edges.
    const u8* right = pixel_at(&buf, 143, 120);
    const u8* left = pixel_at(&buf, 96, 120);
    CHECK(right[1] > 20, "glow: the centered glow bleeds past the primitive's edge");
    CHECK(channels_near(left, (f32)right[0], (f32)right[1], (f32)right[2], (f32)right[3], 3.0f),
          "glow: centered — equal bleed on both edges, the offset ignored");

    // Additivity: over an opaque grey backdrop a glow pixel is brighter than the backdrop alone - green lifted
    // by the glow, where src-over could only darken.
    const FlColor grey = { 80, 80, 80, 255 };
    start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, 80.0f, 80.0f, 120.0f, 120.0f, grey); // backdrop under the glow's bleed
    emit_rect_effect(ir_arena, 100.0f, 100.0f, 40.0f, 40.0f, red, &glow);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the additive-glow frame");
    const u8* lit = pixel_at(&buf, 143, 120); // over the backdrop, in the glow bleed, clear of the red square
    CHECK(lit[1] > 100 && lit[1] > lit[0] && lit[1] > lit[2],
          "glow: additive — the green glow lifts the backdrop above its own grey");
    CHECK(lit[0] >= 80 - 2 && lit[2] >= 80 - 2,
          "glow: additive — the backdrop's other channels are preserved, only added to");

    printf("gpu_smoke_test: effect pixel tests PASS\n");
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Part C - structural tests on the classification readback + the heat-map debug mode (gpu_debug.h). The
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

static int run_classification_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    FlRenderFrame frame = test_frame();
    FlPixelReadback buf;
    FlArena* debug_arena = arena_new();
    GpuTileClassification cls;

    // A 64+6 stack on the one 8x8 tile at (4,4).
    void* start = ir_rewind_begin(ir_arena);
    for (int i = 0; i < 70; i++) {
        emit_solid_rect(ir_arena, 32.0f, 32.0f, 8.0f, 8.0f, red);
    }
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(gpu_debug_read_tile_classification(backend->user_data, surface, debug_arena, &cls),
          "classification readback failed");
    CHECK(cls.tile_cols == (1920 + GPU_TILE_SIZE - 1) / GPU_TILE_SIZE
              && cls.tile_rows == (1080 + GPU_TILE_SIZE - 1) / GPU_TILE_SIZE,
          "classification: grid must match the screen size in tiles");
    CHECK(gpu_tile_count(&cls, 4, 4) == GPU_TILE_CAPACITY,
          "classification: an overstuffed tile's count clamps to capacity");
    CHECK(gpu_tile_overflow(&cls, 4, 4) == 6, "classification: overflow counts the dropped commands");
    for (u32 slot = 0; slot < GPU_TILE_CAPACITY; slot++) {
        CHECK(gpu_tile_cmd_index(&cls, 4, 4, slot) == slot,
              "classification: the list holds the first 64 command indices in emission order");
    }

    start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, 4.0f, 0.0f, 8.0f, 8.0f, red);       // index 0: straddles tiles (0,0) and (1,0)
    emit_solid_rect(ir_arena, 100.0f, 100.0f, 4.0f, 4.0f, green); // index 1: wholly inside tile (12,12)
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    fl_arena_rewind(debug_arena);
    CHECK(gpu_debug_read_tile_classification(backend->user_data, surface, debug_arena, &cls),
          "classification readback failed on the spanning frame");
    CHECK(gpu_tile_count(&cls, 0, 0) == 1 && gpu_tile_cmd_index(&cls, 0, 0, 0) == 0,
          "classification: a spanning command must appear in the left tile's list");
    CHECK(gpu_tile_count(&cls, 1, 0) == 1 && gpu_tile_cmd_index(&cls, 1, 0, 0) == 0,
          "classification: a spanning command must appear in the right tile's list");
    CHECK(gpu_tile_count(&cls, 2, 0) == 0, "classification: a command outside a tile must not appear in it");
    CHECK(gpu_tile_count(&cls, 0, 1) == 0, "classification: the tile row below the spanning command stays empty");
    CHECK(gpu_tile_count(&cls, 12, 12) == 1 && gpu_tile_cmd_index(&cls, 12, 12, 0) == 1,
          "classification: a one-tile command lands only in its own tile, by index");
    CHECK(tile_total_count(&cls) == 3, "classification: the two commands cover exactly three tiles grid-wide");
    CHECK(tile_total_overflow(&cls) == 0, "classification: a normal scene must have zero overflow on every tile");

    // Heat-map debug mode: the raster pass visualizes the same per-tile data - a one-command tile lands at
    // t = 1/64 on the green->red ramp, hence near green.
    gpu_debug_set_mode(backend->user_data, GpuDebug_TileHeatMap);
    render_seam_render(backend, surface, &frame);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed in heat-map mode");
    CHECK(pixel_color(pixel_at(&buf, 2, 2), (FlColor) { 4, 251, 0, 255 }, 4.0f),
          "heat-map: a one-command tile must land near the green end of the ramp");
    CHECK(pixel_color(pixel_at(&buf, 100, 100), (FlColor) { 4, 251, 0, 255 }, 4.0f),
          "heat-map: the tile color covers the whole tile, not just the command's pixels");
    CHECK(pixel_transparent(pixel_at(&buf, 20, 2)), "heat-map: an empty tile stays transparent");

    // Re-render the overflow stack in heat-map mode.
    start = ir_rewind_begin(ir_arena);
    for (int i = 0; i < 70; i++) {
        emit_solid_rect(ir_arena, 32.0f, 32.0f, 8.0f, 8.0f, red);
    }
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the overflow heat-map frame");
    CHECK(pixel_color(pixel_at(&buf, 36, 36), (FlColor) { 255, 0, 255, 255 }, 0.0f),
          "heat-map: an overflowing tile must be flagged magenta");

    gpu_debug_set_mode(backend->user_data, GpuDebug_Off);
    render_seam_render(backend, surface, &frame);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed after debug mode off");
    CHECK(pixel_color(pixel_at(&buf, 36, 36), red, 2.0f),
          "heat-map off: normal rendering must be unaffected by the debug seam");

    // Clip culling is structural: a command classifies by its clip-intersected AABB.
    start = ir_rewind_begin(ir_arena);
    fl_ir_clip_push(ir_arena, (FlRect) { 0, 0, 8, 8 });           // exactly tile (0,0)
    emit_solid_rect(ir_arena, 200.0f, 200.0f, 16.0f, 16.0f, red); // fully clipped out: culled at translation
    emit_solid_rect(ir_arena, 0.0f, 0.0f, 64.0f, 8.0f, green);    // rect spans tiles (0..7,0); clip keeps (0,0)
    fl_ir_clip_pop(ir_arena);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    fl_arena_rewind(debug_arena);
    CHECK(gpu_debug_read_tile_classification(backend->user_data, surface, debug_arena, &cls),
          "classification readback failed on the clip frame");
    CHECK(gpu_tile_count(&cls, 0, 0) == 1 && gpu_tile_cmd_index(&cls, 0, 0, 0) == 0,
          "clip classification: the partially clipped command lands in its clip's tile, re-indexed after the cull");
    CHECK(gpu_tile_count(&cls, 1, 0) == 0,
          "clip classification: tiles the rect spans but the clip excludes hold nothing");
    CHECK(gpu_tile_count(&cls, 25, 25) == 0, "clip classification: a fully clipped-out command's own tiles stay empty");
    CHECK(tile_total_count(&cls) == 1,
          "clip classification: the fully clipped-out command lands in no tile list grid-wide");

    // Arc classification: the AABB bounds the swept sector, so the rest of the circle's tiles
    // hold nothing. A 12-to-3 quarter ring at center (64, 64), radius 40: its sector AABB spans pixels
    // x 64..104, y 24..64 - a handful of tiles in the top-right quadrant, against ~100 for the full circle.
    start = ir_rewind_begin(ir_arena);
    fl_ir_arc(ir_arena, 64.0f, 64.0f, 40.0f, 8.0f, 0.0f, 0.25f * TAU, red, 0, NULL);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    fl_arena_rewind(debug_arena);
    CHECK(gpu_debug_read_tile_classification(backend->user_data, surface, debug_arena, &cls),
          "classification readback failed on the arc frame");
    CHECK(gpu_tile_count(&cls, 11, 5) == 1, "arc classification: a tile inside the sector AABB holds the arc");
    CHECK(gpu_tile_count(&cls, 5, 11) == 0 && gpu_tile_count(&cls, 5, 5) == 0 && gpu_tile_count(&cls, 11, 11) == 0,
          "arc classification: tiles in the unswept quadrants hold nothing");
    CHECK(tile_total_count(&cls) <= 36,
          "arc classification: the sector AABB reaches far fewer tiles than the full circle");

    // Effect AABB expansion: a drop shadow widens the command's classification AABB. The same
    // un-shadowed body first, to prove the distant tile is the effect's doing.
    start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, 26.0f, 26.0f, 4.0f, 4.0f, red); // body [26,26,30,30]: tile (3,3) only
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    fl_arena_rewind(debug_arena);
    CHECK(gpu_debug_read_tile_classification(backend->user_data, surface, debug_arena, &cls),
          "classification readback failed on the un-shadowed frame");
    CHECK(gpu_tile_count(&cls, 3, 3) == 1, "effect classification: the bare body lands in its own tile");
    CHECK(gpu_tile_count(&cls, 13, 3) == 0,
          "effect classification: without an effect the body reaches no distant tile");

    start = ir_rewind_begin(ir_arena);
    FlEffect cls_shadow
        = { .kind = FlEffectKind_DropShadow, .color = { 0, 0, 0, 255 }, .off_x = 80.0f, .off_y = 0.0f, .blur = 4.0f };
    emit_rect_effect(ir_arena, 26.0f, 26.0f, 4.0f, 4.0f, red, &cls_shadow); // shadow reaches x ~102..114
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    fl_arena_rewind(debug_arena);
    CHECK(gpu_debug_read_tile_classification(backend->user_data, surface, debug_arena, &cls),
          "classification readback failed on the shadowed frame");
    CHECK(gpu_tile_count(&cls, 3, 3) == 1, "effect classification: the shadowed command still covers its body's tile");
    CHECK(gpu_tile_count(&cls, 13, 3) == 1,
          "effect classification: the command reaches the tile its shadow touches, not its body");
    CHECK(gpu_tile_count(&cls, 20, 3) == 0,
          "effect classification: the expanded AABB still bounds the shadow — tiles past its reach stay empty");

    // Line-aware polyline classification: a long thin diagonal stroke. An AABB would cover the
    // whole bounding box - here ~125 x 125 = ~15600 tiles. The per-segment line walk keeps only the tiles the
    // capsule reaches, so the grid-wide total stays near the diagonal's length.
    start = ir_rewind_begin(ir_arena);
    emit_polyline(ir_arena, (FlPolyPoint[]) { { 8.0f, 8.0f, 1.0f }, { 1000.0f, 1000.0f, 1.0f } }, 2, red, red, 2.0f,
                  0.0f, 0);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    fl_arena_rewind(debug_arena);
    CHECK(gpu_debug_read_tile_classification(backend->user_data, surface, debug_arena, &cls),
          "classification readback failed on the diagonal polyline frame");
    CHECK(gpu_tile_count(&cls, 62, 62) == 1, "polyline classification: a tile on the diagonal holds the segment");
    CHECK(gpu_tile_count(&cls, 10, 10) == 1, "polyline classification: a tile on the diagonal holds the segment");
    CHECK(gpu_tile_count(&cls, 124, 1) == 0 && gpu_tile_count(&cls, 1, 124) == 0,
          "polyline classification: tiles in the off-diagonal corners of the bbox hold nothing");
    CHECK(gpu_tile_count(&cls, 40, 20) == 0,
          "polyline classification: a tile inside the bbox but well off the line holds nothing");
    CHECK(tile_total_count(&cls) < 1000,
          "polyline classification: the line walk reaches O(line length) tiles, not the O(bbox area) an AABB would");
    CHECK(tile_total_overflow(&cls) == 0, "polyline classification: a single thin diagonal overflows no tile");

    gpu_debug_set_mode(backend->user_data, GpuDebug_TileHeatMap);
    render_seam_render(backend, surface, &frame);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed in heat-map mode on the diagonal");
    CHECK(pixel_color(pixel_at(&buf, 500, 500), (FlColor) { 4, 251, 0, 255 }, 4.0f),
          "polyline heat-map: a tile on the diagonal lights up");
    CHECK(pixel_transparent(pixel_at(&buf, 992, 12)),
          "polyline heat-map: an off-diagonal tile inside the bbox stays transparent");
    gpu_debug_set_mode(backend->user_data, GpuDebug_Off);

    // A representative waveform scene: hundreds of points spanning the screen, filled to a baseline, updated
    // as a per-frame command.
    enum { WAVE_POINTS = 400 };
    static FlPolyPoint wave[WAVE_POINTS];
    for (int i = 0; i < WAVE_POINTS; i++) {
        f32 x = (f32)i / (f32)(WAVE_POINTS - 1) * 1900.0f + 8.0f;
        f32 y = 540.0f + 320.0f * sinf((f32)i * 0.12f);
        wave[i] = (FlPolyPoint) { x, y, 1.0f };
    }
    start = ir_rewind_begin(ir_arena);
    emit_polyline(ir_arena, wave, WAVE_POINTS, red, green, 2.0f, 860.0f, FlPolylineFlags_filled);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    fl_arena_rewind(debug_arena);
    CHECK(gpu_debug_read_tile_classification(backend->user_data, surface, debug_arena, &cls),
          "classification readback failed on the waveform frame");
    CHECK(tile_total_overflow(&cls) == 0,
          "waveform: a dense per-frame waveform classifies within budget — no tile overflows");
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the waveform frame");
    CHECK(pixel_at(&buf, 8, 540)[3] > 0, "waveform: the curve renders (the first point's pixel is covered)");
    // The fill-to-baseline renders across the multi-segment curve: a pixel below the first point's curve
    // value (540) and above the baseline (860) is the opaque fill color, green dominating the stroke red.
    const u8* fill_px = pixel_at(&buf, 8, 700);
    CHECK(fill_px[3] > 0 && fill_px[1] > fill_px[0],
          "waveform: the fill-to-baseline region renders in the full multi-segment scene");

    fl_arena_destroy(debug_arena);
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The 3D tier: retained meshes, the per-viewport color+depth target, and the dedicated-image rule that makes a
// mesh texture addressable by whole-texture UVs. Driven through the backend seams directly (the core's
// Viewport3d element rides on top of exactly these calls), so the debug accessors are reachable.

// Record a GL object name in seen (capacity is the caller's loop bound) if it is not already there, and
// return the resulting distinct count.
static u32 track_name(u32* seen, u32 count, u32 name) {
    for (u32 i = 0; i < count; i++) {
        if (seen[i] == name) {
            return count;
        }
    }
    seen[count] = name;
    return count + 1;
}

// Submit one draw list to a viewport, composite its color target into the surface at 1:1 in the top-left
// corner, and read the surface back.
static bool viewport3d_render_and_read(FlRenderBackend* backend, FlSurface* surface, FlArena* ir_arena, u32 viewport_id,
                                       FlImageHandle color, u32 w, u32 h, const f32* view_projection,
                                       const FlViewport3dDraw* draws, u32 draw_count, FlPixelReadback* out) {
    backend->viewport3d_submit(backend->user_data, viewport_id, color, w, h, view_projection, draws, draw_count);

    FlRenderFrame frame = test_frame();
    void* start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, (FlImageSource) { .kind = FlImageKind_Static, .handle = color }, FlImageMode_Color,
                (FlRect) { 0, 0, (f32)w, (f32)h }, (FlRect) { 0 }, white, FlImageFilter_Nearest, nullptr);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    return backend->read_pixels(backend->user_data, surface, out);
}

static int run_viewport3d_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    CHECK(backend->mesh_create != NULL && backend->mesh_destroy != NULL && backend->viewport3d_submit != NULL,
          "viewport3d: the GL backend must bind the whole 3D seam");

    const int surface_w = 128;
    const int surface_h = 128;
    FlSurface* surface = backend->create_surface(backend->user_data, surface_w, surface_h);
    FlArena* scratch = arena_new();

    // More distinct viewport ids than the backend's table holds, each shown for two frames and dropped: idle
    // viewports are retired, so every one still builds its target.
    static const f32 k_identity_vp[16] = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    };
    // Reopening a viewport id under a new color handle replaces the target: the old handle stops being ready.
    {
        ImageRegistry* registry = image_registry_new(scratch);
        FlPixelReadback ignored;
        viewport3d_render_and_read(backend, surface, ir_arena, 29000, 9050, 8, 8, k_identity_vp, nullptr, 0, &ignored);
        CHECK(image_registry_is_ready((ImageHandle)9050, nullptr, nullptr), "viewport3d: the first target is ready");
        viewport3d_render_and_read(backend, surface, ir_arena, 29000, 9051, 8, 8, k_identity_vp, nullptr, 0, &ignored);
        CHECK(image_registry_is_ready((ImageHandle)9051, nullptr, nullptr), "viewport3d: the reopened target is ready");
        CHECK(!image_registry_is_ready((ImageHandle)9050, nullptr, nullptr),
              "viewport3d: a replaced target must not stay registered as ready");
        image_registry_destroy(registry);
    }
    for (u32 n = 0; n < GPU_VIEWPORT3D_CAP + 4; n++) {
        const FlImageHandle color = 9100 + n;
        FlPixelReadback ignored;
        for (int frame = 0; frame < 2; frame++) {
            viewport3d_render_and_read(backend, surface, ir_arena, 30000 + n, color, 8, 8, k_identity_vp, nullptr, 0,
                                       &ignored);
        }
        GpuViewport3dTarget built = gpu_debug_viewport3d_target(backend->user_data, 30000 + n);
        CHECK(built.found && built.width == 8,
              "viewport3d: a viewport past the table cap must still build its target once idle ones are retired");
    }

    // A 2x1 alpha lightmap: opaque left texel, transparent right. Small enough that the atlas would happily
    // take it, which is exactly what makes it the test for the dedicated flag.
    static u8 lightmap[2] = { 255, 0 };
    FlImageDecodeResult lightmap_decode = { .pixels = lightmap, .width = 2, .height = 1, .stride = 2, .channels = 1 };
    const FlImageHandle dedicated_handle = 8001;
    const FlImageHandle atlased_handle = 8002;
    FlImageLoadRequest dedicated_req = { .relative_path = FL_S("lightmap"), .dedicated = true };
    FlImageLoadRequest atlased_req = { .relative_path = FL_S("lightmap"), .dedicated = false };
    backend->prepare_image(backend->user_data, dedicated_handle, &lightmap_decode, scratch, &dedicated_req);
    backend->prepare_image(backend->user_data, atlased_handle, &lightmap_decode, scratch, &atlased_req);

    CHECK(!gpu_debug_image_in_atlas(backend->user_data, dedicated_handle),
          "viewport3d: a dedicated image must never enter the image atlas, whatever its size");
    CHECK(gpu_debug_image_in_atlas(backend->user_data, atlased_handle),
          "viewport3d: the same pixels without the flag still atlas — the flag decides, not the size");

    // Three quads in clip space. Red (textured) sits alone on the left at z=0. Green and blue overlap exactly
    // on the right, green nearer (z=0.5) than blue (z=0.9) - and green is submitted FIRST, so the order is
    // front-to-back. That ordering is the whole point: with the depth test off, the later blue draw would
    // simply overwrite the nearer green one, so saw_green && !saw_blue can only hold if GL_LESS actually
    // rejected blue's fragments.
    const FlColor red = { 255, 0, 0, 255 };
    const FlColor green = { 0, 255, 0, 255 };
    const FlColor blue = { 0, 0, 255, 255 };
    static const u16 quad_indices[6] = { 0, 1, 2, 0, 2, 3 };

    TestQuad red_quad = test_quad(-0.9f, -0.1f, 0.0f, red);
    TestQuad green_quad = test_quad(0.1f, 0.9f, 0.5f, green);
    TestQuad blue_quad = test_quad(0.1f, 0.9f, 0.9f, blue);

    FlMeshHandle red_mesh
        = backend->mesh_create(backend->user_data, red_quad.verts, 4, quad_indices, 6, FlMeshTopology_Triangles);
    FlMeshHandle green_mesh
        = backend->mesh_create(backend->user_data, green_quad.verts, 4, quad_indices, 6, FlMeshTopology_Triangles);
    FlMeshHandle blue_mesh
        = backend->mesh_create(backend->user_data, blue_quad.verts, 4, quad_indices, 6, FlMeshTopology_Triangles);
    CHECK(red_mesh != FL_MESH_HANDLE_INVALID && green_mesh != FL_MESH_HANDLE_INVALID
              && blue_mesh != FL_MESH_HANDLE_INVALID,
          "viewport3d: mesh_create must return live handles");
    CHECK(red_mesh != green_mesh && green_mesh != blue_mesh, "viewport3d: each mesh gets its own handle");

    // Identity view-projection: the quads' coordinates are already clip space, so the scene is a plain ortho
    // view and every expectation below is a fixed pixel address.
    static const f32 identity[16] = {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    };
    const FlViewport3dDraw draws[3] = {
        // nearer, submitted first
        { .mesh = green_mesh, .texture = FL_IMAGE_HANDLE_INVALID, .depth_test = true },
        // farther, submitted second
        { .mesh = blue_mesh, .texture = FL_IMAGE_HANDLE_INVALID, .depth_test = true },
        { .mesh = red_mesh, .texture = dedicated_handle, .filter = FlImageFilter_Linear, .depth_test = true },
    };

    const u32 viewport_id = 4242;
    const u32 vp_w = 64;
    const u32 vp_h = 64;
    const FlImageHandle vp_color = 8003;
    backend->viewport3d_submit(backend->user_data, viewport_id, vp_color, vp_w, vp_h, identity, draws, 3);

    GpuViewport3dTarget target = gpu_debug_viewport3d_target(backend->user_data, viewport_id);
    CHECK(target.found && target.width == vp_w && target.height == vp_h,
          "viewport3d: submit must build the target at the requested size");
    CHECK(!gpu_debug_image_in_atlas(backend->user_data, vp_color),
          "viewport3d: the color target is a whole texture, never an atlas cell");

    // Draw the viewport's color target back as an ordinary image, clipped to the left half of its 64x64
    // destination - the clip proves the result composites through the normal image path, not a special case.
    FlRenderFrame frame = test_frame();
    void* start = ir_rewind_begin(ir_arena);
    fl_ir_clip_push(ir_arena, (FlRect) { 0, 0, 32, 64 });
    fl_ir_image(ir_arena, (FlImageSource) { .kind = FlImageKind_Static, .handle = vp_color }, FlImageMode_Color,
                (FlRect) { 0, 0, (f32)vp_w, (f32)vp_h }, (FlRect) { 0 }, white, FlImageFilter_Nearest, nullptr);
    fl_ir_clip_pop(ir_arena);
    fl_ir_image(ir_arena, (FlImageSource) { .kind = FlImageKind_Static, .handle = vp_color }, FlImageMode_Color,
                (FlRect) { 64, 0, (f32)vp_w, (f32)vp_h }, (FlRect) { 0 }, white, FlImageFilter_Nearest, nullptr);
    ir_frame_render(backend, surface, &frame, ir_arena, start);

    FlPixelReadback buf;
    CHECK(backend->read_pixels(backend->user_data, surface, &buf), "viewport3d: read_pixels failed");

    // The unclipped copy at x=64: quad columns land at 64 + 0.05*64 .. and the y band covers the middle rows.
    // The red quad's left half samples the opaque lightmap texel, its right half the transparent one.
    bool saw_red = false;
    bool saw_green = false;
    bool saw_blue = false;
    for (u32 y = 0; y < (u32)surface_h; y++) {
        for (u32 x = 64; x < 128; x++) {
            const u8* px = pixel_at(&buf, (int)x, (int)y);
            if (px[3] == 0) {
                continue;
            }
            saw_red = saw_red || (px[0] > 150 && px[1] < 80 && px[2] < 80);
            saw_green = saw_green || (px[1] > 150 && px[0] < 80 && px[2] < 80);
            saw_blue = saw_blue || (px[2] > 150 && px[0] < 80 && px[1] < 80);
        }
    }
    CHECK(saw_red, "viewport3d: the textured quad renders where its lightmap texel is opaque");
    CHECK(saw_green, "viewport3d: the nearer untextured quad renders");
    CHECK(!saw_blue, "viewport3d: the farther quad must be occluded — the depth test did not run");

    // Sampled at the red quad's far-right column, vertically mid-band - where the lightmap texel is
    // transparent, so the UI sees through the viewport.
    CHECK(pixel_transparent(pixel_at(&buf, 64 + 28, 32)),
          "viewport3d: a texel the lightmap makes transparent leaves the target transparent");

    // Under the clip, only the left half of the first copy survives; the red quad spans x=3..29 there, so
    // x=20 is inside the clip and x=40 (still inside the quad's destination) is cut away.
    CHECK(!pixel_transparent(pixel_at(&buf, 20, 32)), "viewport3d: the clipped copy draws inside the clip rect");
    CHECK(pixel_transparent(pixel_at(&buf, 40, 32)), "viewport3d: the clipped copy is cut outside the clip rect");

    // Resize: the same viewport id at a new size must rebuild its target in place AND free what it replaced.
    // Name liveness (glIsFramebuffer on the old name) cannot test that - GL may hand a deleted name straight
    // back to the next Gen, so leaking and freeing both leave the old name "live". Name RECYCLING is the signal
    // instead: an implementation that frees gets the same handful of names back, while one that leaks must mint
    // a fresh name per resize (dropping the deletes in setup_targets turns this into RESIZE_COUNT names).
    //
    // Driver-dependent, but one-way-safe: leaking CANNOT recycle, so a failure here is always a real leak,
    // while passing relies on the driver handing freed names back promptly. Mesa (llvmpipe, what CI runs)
    // allocates the lowest free id and does. A driver that never reuses names would fail this spuriously - if
    // that ever happens, the assertion is wrong, not the backend.
    enum { RESIZE_COUNT = 8, RESIZE_NAME_BUDGET = 3 };
    u32 seen_fbo[RESIZE_COUNT] = { 0 };
    u32 seen_depth[RESIZE_COUNT] = { 0 };
    u32 seen_color[RESIZE_COUNT] = { 0 };
    u32 distinct_fbo = 0;
    u32 distinct_depth = 0;
    u32 distinct_color = 0;
    for (u32 i = 0; i < RESIZE_COUNT; i++) {
        // Every iteration is a genuine size change, so none of them short-circuits the rebuild.
        u32 w = 40u + i * 8u;
        u32 h = 72u - i * 4u;
        backend->viewport3d_submit(backend->user_data, viewport_id, vp_color, w, h, identity, draws, 3);
        GpuViewport3dTarget step = gpu_debug_viewport3d_target(backend->user_data, viewport_id);
        CHECK(step.found && step.width == w && step.height == h,
              "viewport3d: a resize must rebuild the color+depth target at the new size");
        CHECK(step.viewport_count == target.viewport_count,
              "viewport3d: a resize must reuse the viewport entry, not add one");
        CHECK(step.fbo != 0 && step.depth_rb != 0 && step.color_tex != 0,
              "viewport3d: a rebuilt target must have a framebuffer, depth buffer, and color texture");

        distinct_fbo = track_name(seen_fbo, distinct_fbo, step.fbo);
        distinct_depth = track_name(seen_depth, distinct_depth, step.depth_rb);
        distinct_color = track_name(seen_color, distinct_color, step.color_tex);

        // Render between resizes so the slot table drains its dead list (that is where the replaced color
        // textures are freed) - otherwise the deferred frees pile up and the count would be meaningless.
        start = ir_rewind_begin(ir_arena);
        ir_frame_render(backend, surface, &frame, ir_arena, start);
    }
    CHECK(distinct_fbo <= RESIZE_NAME_BUDGET,
          "viewport3d: repeated resizes must recycle framebuffer names — a growing set means they leak");
    CHECK(distinct_depth <= RESIZE_NAME_BUDGET,
          "viewport3d: repeated resizes must recycle depth-buffer names — a growing set means they leak");
    CHECK(distinct_color <= RESIZE_NAME_BUDGET,
          "viewport3d: repeated resizes must recycle color-texture names — a growing set means they leak");

    // Destroy the green quad: the blue one behind it becoming visible is only possible if the draw was dropped
    // rather than left pointing at dead buffers.
    backend->mesh_destroy(backend->user_data, green_mesh);
    backend->viewport3d_submit(backend->user_data, viewport_id, vp_color, vp_w, vp_h, identity, draws, 3);
    start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, (FlImageSource) { .kind = FlImageKind_Static, .handle = vp_color }, FlImageMode_Color,
                (FlRect) { 0, 0, (f32)vp_w, (f32)vp_h }, (FlRect) { 0 }, white, FlImageFilter_Nearest, nullptr);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(backend->read_pixels(backend->user_data, surface, &buf), "viewport3d: read_pixels failed after destroy");

    bool saw_blue_now = false;
    bool saw_green_now = false;
    for (u32 y = 0; y < 64; y++) {
        for (u32 x = 0; x < 64; x++) {
            const u8* px = pixel_at(&buf, (int)x, (int)y);
            saw_green_now = saw_green_now || (px[3] != 0 && px[1] > 150 && px[0] < 80 && px[2] < 80);
            saw_blue_now = saw_blue_now || (px[3] != 0 && px[2] > 150 && px[0] < 80 && px[1] < 80);
        }
    }
    CHECK(!saw_green_now, "viewport3d: a destroyed mesh stops drawing");
    CHECK(saw_blue_now, "viewport3d: the quad the destroyed one used to hide becomes visible");

    // Line draws, with and without the depth test. A white quad sits in front (z=0.2) of a cyan line (z=0.8)
    // that crosses it horizontally, and the line is submitted second: depth-tested the quad hides it, with the
    // test off the same line comes through.
    const FlColor cyan = { 0, 255, 255, 255 };
    const FlColor white_fill = { 255, 255, 255, 255 };
    TestQuad occluder = test_quad(-0.9f, 0.9f, 0.2f, white_fill);
    const FlMeshVertex line_verts[2] = {
        { .x = -0.9f, .y = 0.0f, .z = 0.8f, .color = cyan },
        { .x = 0.9f, .y = 0.0f, .z = 0.8f, .color = cyan },
    };
    static const u16 line_indices[2] = { 0, 1 };

    FlMeshHandle occluder_mesh
        = backend->mesh_create(backend->user_data, occluder.verts, 4, quad_indices, 6, FlMeshTopology_Triangles);
    FlMeshHandle line_mesh
        = backend->mesh_create(backend->user_data, line_verts, 2, line_indices, 2, FlMeshTopology_Lines);
    CHECK(line_mesh != FL_MESH_HANDLE_INVALID, "viewport3d: a line mesh must create like any other");

    FlViewport3dDraw line_draws[2] = {
        { .mesh = occluder_mesh, .texture = FL_IMAGE_HANDLE_INVALID, .depth_test = true },
        { .mesh = line_mesh, .texture = FL_IMAGE_HANDLE_INVALID, .depth_test = false },
    };
    CHECK(viewport3d_render_and_read(backend, surface, ir_arena, viewport_id, vp_color, vp_w, vp_h, identity,
                                     line_draws, 2, &buf),
          "viewport3d: read_pixels failed for the line drawn without the depth test");
    bool saw_line_over = false;
    bool saw_line_through = false;
    for (u32 x = 0; x < vp_w; x++) {
        for (u32 y = 0; y < vp_h; y++) {
            const u8* px = pixel_at(&buf, (int)x, (int)y);
            saw_line_over = saw_line_over || (px[3] != 0 && px[0] < 80 && px[1] > 150 && px[2] > 150);
        }
    }
    CHECK(saw_line_over, "viewport3d: a line with the depth test off draws through the geometry in front of it");

    line_draws[1].depth_test = true;
    CHECK(viewport3d_render_and_read(backend, surface, ir_arena, viewport_id, vp_color, vp_w, vp_h, identity,
                                     line_draws, 2, &buf),
          "viewport3d: read_pixels failed for the depth-tested line");
    for (u32 x = 0; x < vp_w; x++) {
        for (u32 y = 0; y < vp_h; y++) {
            const u8* px = pixel_at(&buf, (int)x, (int)y);
            saw_line_through = saw_line_through || (px[3] != 0 && px[0] < 80 && px[1] > 150 && px[2] > 150);
        }
    }
    CHECK(!saw_line_through, "viewport3d: the same line depth-tested is occluded by the quad in front of it");

    // Per-draw sampler filtering. One quad covering the whole viewport, textured with the 2x1 alpha lightmap,
    // drawn once per filter. Nearest reproduces the two texels as they are (every pixel fully opaque or fully
    // transparent) while linear leaves partial alpha across the texel boundary, so the count of
    // partially-transparent pixels along the middle row separates the two.
    TestQuad full_quad = test_quad(-1.0f, 1.0f, 0.0f, white_fill);
    FlMeshHandle full_mesh
        = backend->mesh_create(backend->user_data, full_quad.verts, 4, quad_indices, 6, FlMeshTopology_Triangles);

    u32 partial_alpha[2] = { 0, 0 }; // indexed by FlImageFilter
    const FlImageFilter filters[2] = { FlImageFilter_Linear, FlImageFilter_Nearest };
    for (u32 f = 0; f < 2; f++) {
        const FlViewport3dDraw textured
            = { .mesh = full_mesh, .texture = dedicated_handle, .filter = filters[f], .depth_test = true };
        CHECK(viewport3d_render_and_read(backend, surface, ir_arena, viewport_id, vp_color, vp_w, vp_h, identity,
                                         &textured, 1, &buf),
              "viewport3d: read_pixels failed for the filtered draw");
        for (u32 x = 0; x < vp_w; x++) {
            const u8* px = pixel_at(&buf, (int)x, (int)(vp_h / 2));
            if (px[3] != 0 && px[3] != 255) {
                partial_alpha[filters[f]]++;
            }
        }
    }
    CHECK(partial_alpha[FlImageFilter_Nearest] == 0,
          "viewport3d: nearest filtering shows the lightmap's texels as authored, with no blend between them");
    CHECK(partial_alpha[FlImageFilter_Linear] > 0,
          "viewport3d: linear filtering does blend across the texel boundary — otherwise the case above proves "
          "nothing about the filter");

    backend->mesh_destroy(backend->user_data, occluder_mesh);
    backend->mesh_destroy(backend->user_data, line_mesh);
    backend->mesh_destroy(backend->user_data, full_mesh);
    backend->mesh_destroy(backend->user_data, red_mesh);
    backend->mesh_destroy(backend->user_data, blue_mesh);
    fl_arena_destroy(scratch);
    backend->destroy_surface(backend->user_data, surface);
    printf("gpu_smoke_test: viewport3d (depth, lines, filtering, dedicated texture, clip, resize) PASS\n");
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Frame lifecycle - an all-ineligible frame still clears the target, and change_resolution rebuilds the
// offscreen target, tile lists, and readback buffer. Also covers the surface-texture seam (gpu_renderer.h),
// whose dimensions track the resolution across the resize.

static int run_frame_lifecycle_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    FlRenderFrame frame = test_frame();
    FlPixelReadback buf;

    // Surface-texture seam at the create resolution - the sections above never resized, so the dims must
    // still be the 1920x1080 the backend was created at.
    GpuSurfaceTexture surf = gpu_renderer_surface_texture(surface);
    CHECK(surf.handle != 0, "surface seam: texture name must be non-zero after create");
    CHECK(surf.width == 1920 && surf.height == 1080, "surface seam: dims must match the create resolution");

    // Empty frame: a stream with only ineligible records (an empty rect and a border with no widths)
    // translates to zero commands; the pipeline must still run and clear the previous frame to fully
    // transparent. Render a real rect first so there is something to clear.
    void* start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, 32.0f, 32.0f, 8.0f, 8.0f, red);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed before the empty frame");
    CHECK(pixel_color(pixel_at(&buf, 36, 36), red, 2.0f), "empty frame setup: the rect to be cleared must land first");

    start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, 0.0f, 0.0f, 0.0f, 64.0f, red); // empty: w == 0
    fl_ir_border(ir_arena, (FlRect) { 0, 0, 64, 64 }, 0, 0, 0, 0, (FlCornerRadius) { 0 }, red);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed after empty frame");
    CHECK(pixel_transparent(pixel_at(&buf, 36, 36)), "empty frame: the target must clear to fully transparent");

    render_seam_change_resolution(backend, surface, 640, 360);
    start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, 8.0f, 8.0f, 16.0f, 16.0f, green);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed after change_resolution");
    CHECK(buf.width == 640 && buf.height == 360 && buf.stride == 640 * 4u,
          "change_resolution: readback must report the new dimensions");
    CHECK(pixel_color(pixel_at(&buf, 16, 16), green, 2.0f),
          "change_resolution: a rect rendered after resize must land");
    CHECK(pixel_transparent(pixel_at(&buf, 100, 100)),
          "change_resolution: pixels outside the rect must stay transparent");

    // The texture name is not asserted to change - GL may recycle a just-deleted name on the gen after
    // resize; the new-size pixel readback above is the sound proof the target was actually rebuilt.
    surf = gpu_renderer_surface_texture(surface);
    CHECK(surf.handle != 0, "surface seam: texture name must stay valid after change_resolution");
    CHECK(surf.width == 640 && surf.height == 360, "surface seam: dims must update after change_resolution");

    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// N-surface present isolation. The windowed compositor (backend_sdl_window.c) drives N windows off the
// ONE shared GL context, making it current per window and sampling that window's own surface texture each present.
// The SDL swap itself needs a real OS window (the two_windows demo is the manual proof); what is testable
// headlessly is the invariant the present path rests on - surfaces co-resident on one shared context stay
// isolated, each resolving its own texture and its own pixels regardless of render order.

static int run_n_surface_tests(FlRenderBackend* backend, FlArena* ir_arena) {
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

    // Red into A, then green into B - back-to-back on the shared context, the windowed render order.
    void* start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, 0.0f, 0.0f, 128.0f, 128.0f, red);
    ir_frame_render(backend, a, &frame, ir_arena, start);

    start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, 0.0f, 0.0f, 64.0f, 96.0f, green);
    ir_frame_render(backend, b, &frame, ir_arena, start);

    CHECK(render_seam_read_pixels(backend, a, &buf), "n-surface: read back A");
    CHECK(buf.width == 128 && buf.height == 128, "n-surface: A readback reports A's dims");
    CHECK(pixel_color(pixel_at(&buf, 64, 64), red, 2.0f), "n-surface: A keeps its red after B was rendered");
    CHECK(render_seam_read_pixels(backend, b, &buf), "n-surface: read back B");
    CHECK(buf.width == 64 && buf.height == 96, "n-surface: B readback reports B's dims");
    CHECK(pixel_color(pixel_at(&buf, 32, 48), green, 2.0f), "n-surface: B holds its own green");

    // Re-render A (red -> white), interleaving the two surfaces' frames on the shared context.
    start = ir_rewind_begin(ir_arena);
    emit_solid_rect(ir_arena, 0.0f, 0.0f, 128.0f, 128.0f, white);
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Surface struct reclamation. Each surface owns a single arena backing both the struct and its readback
// buffer, freed wholesale in destroy_surface - so a churn loop must leave the backend-lifetime perm_arena's
// allocation cursor exactly where it started, while each fresh surface still comes up with its own target
// texture at its own dimensions.

static int run_surface_churn_leak_test(FlRenderBackend* backend) {
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

#endif // !defined(__APPLE__) - Sections 1 + 2 (GL/EGL all-compute suite)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cross-backend SVG vector-icon cases: the SVG raster / color-raster / zoom / compaction /
// large-raster suites run on BOTH backends through the neutral seam (prepare_vector, the SVG atlas readbacks,
// scale_changed, and rendered icon draws). Every expectation is a property both backends satisfy (coverage
// thresholds, painter-order compositing, even-odd holes, premultiplied opacity, partial-re-raster cadence),
// checked with the same tolerance - no per-backend golden fork.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Part G - monochrome SVG icon end to end (GPU SVG 1b): a solid-fill icon prepared through the vector seam
// (backend->prepare_vector) rasterizes coverage into the R8 SVG atlas, then draws via FlCmdImage's alpha-
// coverage + tint path. The icon is a single full-bounds rect, so at size 32 it aspect-fits to a fully covered
// 32 x 24 content cell - the readback interior is solid and the padding ring is clean zero, with no curved AA
// to reason about. The tint contract: opaque-white -> the icon's authored fill; any other tint -> recolor.

// #3296c8 = (50, 150, 200): a vivid fill clearly distinct from white, so the authored-color default is provable.
static const char k_svg_icon[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"30\">"
                                 "<rect x=\"0\" y=\"0\" width=\"40\" height=\"30\" fill=\"#3296c8\"/>"
                                 "</svg>";

enum { SVG_FULL = 250, SVG_ZERO = 1 }; // same tolerances as the glyph atlas readback

static u8 svg_texel(const GpuSvgAtlasReadback* atlas, u32 x, u32 y) {
    return atlas->texels[(size_t)y * atlas->width + x];
}

// The GPU backend retains a pointer to a handle's FlVectorImage to re-raster it on a zoom step or an atlas
// compaction, and the backend persists across all the Part G tests - so both the struct and its contours must
// outlive any single test. A stack-local FlVectorImage img would dangle even with its contours in an arena;
// the Part G tests share this one persistent parse instead. All of them use the same k_svg_icon.
static const FlVectorImage* svg_test_icon(void) {
    static FlVectorImage* icon = NULL;
    if (icon == NULL) {
        FlArena* a = arena_new(); // process-lifetime: the parse must outlive every test that retains it
        icon = arena_alloc_array(a, FlVectorImage, 1);
        bool ok = vector_image_parse((const u8*)k_svg_icon, sizeof(k_svg_icon) - 1, a, icon);
        if (!ok || icon->classification != FlVectorClass_Monochrome) {
            return NULL; // the caller CHECKs this - a parse regression fails the test rather than crashing
        }
    }
    return icon;
}

static int run_svg_raster_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    void* ud = backend->user_data;
    CHECK(backend->prepare_vector != NULL, "svg raster: the GPU backend must expose prepare_vector");

    FlArena* arena = arena_new();
    const FlVectorImage* img = svg_test_icon();
    CHECK(img != NULL, "svg raster: the monochrome icon parses");
    CHECK(img->classification == FlVectorClass_Monochrome, "svg raster: a single fill classifies monochrome");

    const u16 size = 32;              // svg_quantize_size(32) == 32 here, so the cell raster size matches
    const FlImageHandle handle = 555; // unused by any image-atlas/slot test -> falls through to the SVG atlas
    backend->prepare_vector(ud, handle, size, img);

    // --- Atlas readback: 40x30 at size 32 -> scale 0.8 -> a 32 x 24 content block plus a 1-texel padding ring.
    const GpuSvgEntry* e = gpu_debug_find_svg(ud, handle);
    CHECK(e != NULL, "svg raster: the prepared icon resolves to a cell");
    CHECK(e->cls == FlVectorClass_Monochrome, "svg raster: the cell records the monochrome classification");
    CHECK(e->mono_color.r == 50 && e->mono_color.g == 150 && e->mono_color.b == 200 && e->mono_color.a == 255,
          "svg raster: the cell carries the icon's authored fill color");
    CHECK(e->cell.w == 32 + 2 * GPU_SVG_ATLAS_PAD && e->cell.h == 24 + 2 * GPU_SVG_ATLAS_PAD,
          "svg raster: the cell is the aspect-fit content extent plus the padding ring");

    GpuSvgAtlasReadback atlas;
    CHECK(gpu_debug_read_svg_atlas(ud, arena, &atlas), "svg raster: atlas readback");

    // The padding ring is empty on all four borders (the cell is written wall to wall, ring included).
    for (u32 i = 0; i < e->cell.w; i++) {
        CHECK(svg_texel(&atlas, e->cell.x + i, e->cell.y) <= SVG_ZERO
                  && svg_texel(&atlas, e->cell.x + i, e->cell.y + e->cell.h - 1) <= SVG_ZERO,
              "svg raster: the padding rows must be empty");
    }
    for (u32 i = 0; i < e->cell.h; i++) {
        CHECK(svg_texel(&atlas, e->cell.x, e->cell.y + i) <= SVG_ZERO
                  && svg_texel(&atlas, e->cell.x + e->cell.w - 1, e->cell.y + i) <= SVG_ZERO,
              "svg raster: the padding columns must be empty");
    }
    CHECK(svg_texel(&atlas, e->cell.x + e->cell.w / 2, e->cell.y + e->cell.h / 2) >= SVG_FULL,
          "svg raster: the solid icon interior is fully covered");
    CHECK(svg_texel(&atlas, e->cell.x + GPU_SVG_ATLAS_PAD, e->cell.y + GPU_SVG_ATLAS_PAD) >= SVG_FULL,
          "svg raster: coverage runs to the content edge (the first interior texel is full)");

    // --- Pixel + tint: draw the icon twice in one frame, native 1:1 so each interior pixel reads a fully
    // covered texel (premultiplied alpha 255 leaves rgb intact).
    const int cw = (int)e->cell.w - 2 * GPU_SVG_ATLAS_PAD; // 32 content texels
    const int ch = (int)e->cell.h - 2 * GPU_SVG_ATLAS_PAD; // 24 content texels
    const FlColor authored = { 50, 150, 200, 255 };
    const FlColor recolor = { 230, 80, 200, 255 };
    const FlImageSource src = { .kind = FlImageKind_Static, .handle = handle };

    void* start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, src, FlImageMode_Color, (FlRect) { 100, 100, (f32)cw, (f32)ch }, (FlRect) { 0 }, white,
                FlImageFilter_Linear, nullptr);
    fl_ir_image(ir_arena, src, FlImageMode_Color, (FlRect) { 200, 100, (f32)cw, (f32)ch }, (FlRect) { 0 }, recolor,
                FlImageFilter_Linear, nullptr);
    FlRenderFrame frame = test_frame();
    ir_frame_render(backend, surface, &frame, ir_arena, start);

    FlPixelReadback buf;
    CHECK(render_seam_read_pixels(backend, surface, &buf), "svg raster: readback");
    CHECK(pixel_color(pixel_at(&buf, 100 + cw / 2, 100 + ch / 2), authored, 3.0f),
          "svg raster: an opaque-white tint draws the icon's authored fill color");
    CHECK(pixel_color(pixel_at(&buf, 200 + cw / 2, 100 + ch / 2), recolor, 3.0f),
          "svg raster: a non-identity tint recolors the icon");

    fl_arena_destroy(arena);
    printf("gpu_smoke_test: svg raster tests PASS\n");
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Part G1b - multi-color SVG icon end to end: an icon with >=2 distinct fills prepares through the vector seam
// into the RGBA8 color atlas (color_svg_prog composites its shapes in document order, painter's algorithm), then
// draws via FlCmdImage mode 0 sampling the baked premultiplied RGBA. Property checks, not byte-exact goldens:
// the dispatch golden is HITL on real hardware.

// Two opaque fills in document order: a full-bounds red rect under a left-half green rect. The left half
// composites green-over-red -> green; the right half stays red.
static const char k_svg_color_order[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"30\">"
                                        "<rect x=\"0\" y=\"0\" width=\"40\" height=\"30\" fill=\"#ff0000\"/>"
                                        "<rect x=\"0\" y=\"0\" width=\"20\" height=\"30\" fill=\"#00ff00\"/></svg>";

// An even-odd blue donut (one path, outer + inner subpath -> a rectangular hole) beside a red rect. The hole
// sits over empty space, so under even-odd it composites to transparent - the nested same-winding subpaths
// cancel.
static const char k_svg_color_evenodd[]
    = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"30\">"
      "<path fill=\"#0000ff\" fill-rule=\"evenodd\" d=\"M0 0 H20 V30 H0 Z M5 5 H15 V25 H5 Z\"/>"
      "<rect x=\"25\" y=\"0\" width=\"15\" height=\"30\" fill=\"#ff0000\"/></svg>";

// A full-opacity red left half and a half-opacity blue right half (non-overlapping). The blue bakes
// premultiplied to ~(0, 0, 128, 128) - opacity folded into the color at lowering time.
static const char k_svg_color_opacity_icon[]
    = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"30\">"
      "<rect x=\"0\" y=\"0\" width=\"20\" height=\"30\" fill=\"#ff0000\"/>"
      "<rect x=\"20\" y=\"0\" width=\"20\" height=\"30\" fill=\"#0000ff\" opacity=\"0.5\"/></svg>";

// Parse a multi-color icon into a process-lifetime arena: the backend retains the FlVectorImage pointer for the
// handle's life (re-raster on zoom/compaction), so each icon must outlive the test that prepares it.
static const FlVectorImage* color_test_icon(const char* svg, int len) {
    FlArena* a = arena_new(); // process-lifetime: never freed, so the retained contours never dangle
    FlVectorImage* icon = arena_alloc_array(a, FlVectorImage, 1);
    if (!vector_image_parse((const u8*)svg, (size_t)len, a, icon)) {
        return NULL;
    }
    return icon;
}

// One RGBA8 color-atlas texel at content coordinate (cx, cy) inside a cell (content origin = cell origin + the
// padding ring; cy is y-down, 0 at the content top), as four interleaved premultiplied bytes.
static const u8* color_cell_texel(const GpuColorSvgAtlasReadback* a, const GpuSvgEntry* e, u32 cx, u32 cy) {
    u32 x = e->cell.x + GPU_SVG_ATLAS_PAD + cx;
    u32 y = e->cell.y + GPU_SVG_ATLAS_PAD + cy;
    return a->texels + ((size_t)y * a->width + x) * 4u;
}

static int run_svg_color_raster_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    void* ud = backend->user_data;
    CHECK(backend->prepare_vector != NULL, "svg color: the GPU backend must expose prepare_vector");

    FlArena* arena = arena_new();
    const u16 size = 32;  // svg_quantize_size(32) == 32 here, so the cell raster size matches (a 40x30 -> 32x24)
    const f32 tol = 4.0f; // premultiply rounding slack

    const FlVectorImage* order = color_test_icon(k_svg_color_order, (int)sizeof(k_svg_color_order) - 1);
    CHECK(order != NULL && order->classification == FlVectorClass_MultiColor,
          "svg color: the two-fill icon parses multi-color");
    const FlVectorImage* donut = color_test_icon(k_svg_color_evenodd, (int)sizeof(k_svg_color_evenodd) - 1);
    CHECK(donut != NULL && donut->classification == FlVectorClass_MultiColor,
          "svg color: the even-odd donut + second color parses multi-color");
    const FlVectorImage* faded = color_test_icon(k_svg_color_opacity_icon, (int)sizeof(k_svg_color_opacity_icon) - 1);
    CHECK(faded != NULL && faded->classification == FlVectorClass_MultiColor,
          "svg color: the opacity icon parses multi-color");

    // Fresh handles past the monochrome section's, so they fall through to the color SVG atlas.
    const FlImageHandle h_order = 600;
    const FlImageHandle h_donut = 601;
    const FlImageHandle h_faded = 602;
    backend->prepare_vector(ud, h_order, size, order);
    backend->prepare_vector(ud, h_donut, size, donut);
    backend->prepare_vector(ud, h_faded, size, faded);

    const GpuSvgEntry* eo = gpu_debug_find_color_svg(ud, h_order);
    const GpuSvgEntry* ed = gpu_debug_find_color_svg(ud, h_donut);
    const GpuSvgEntry* ef = gpu_debug_find_color_svg(ud, h_faded);
    CHECK(eo != NULL && ed != NULL && ef != NULL, "svg color: each prepared icon resolves to an RGBA8 cell");
    CHECK(eo->cls == FlVectorClass_MultiColor, "svg color: the cell records the multi-color classification");
    CHECK(eo->cell.w == 32 + 2 * GPU_SVG_ATLAS_PAD && eo->cell.h == 24 + 2 * GPU_SVG_ATLAS_PAD,
          "svg color: the cell is the aspect-fit content extent plus the padding ring");

    GpuColorSvgAtlasReadback ca;
    CHECK(gpu_debug_read_color_svg_atlas(ud, arena, &ca), "svg color: color atlas readback");

    // --- (a) document-order compositing. Content is 32 x 24; green covers the left 16, red the full width.
    CHECK(channels_near(color_cell_texel(&ca, eo, 8, 12), 0, 255, 0, 255, tol),
          "svg color: the later shape paints over the earlier (left half is the top green)");
    CHECK(channels_near(color_cell_texel(&ca, eo, 24, 12), 255, 0, 0, 255, tol),
          "svg color: a region only the bottom shape covers keeps its color (right half stays red)");

    // --- (b) even-odd hole.
    CHECK(channels_near(color_cell_texel(&ca, ed, 2, 12), 0, 0, 255, 255, tol),
          "svg color: the even-odd ring is filled");
    CHECK(channels_near(color_cell_texel(&ca, ed, 8, 12), 0, 0, 0, 0, tol),
          "svg color: the even-odd hole composites transparent");
    CHECK(channels_near(color_cell_texel(&ca, ed, 26, 12), 255, 0, 0, 255, tol),
          "svg color: the second color fills its own region");

    // --- (c) per-shape opacity baked premultiplied.
    CHECK(channels_near(color_cell_texel(&ca, ef, 8, 12), 255, 0, 0, 255, tol),
          "svg color: a full-opacity region bakes its color intact");
    CHECK(channels_near(color_cell_texel(&ca, ef, 24, 12), 0, 0, 128, 128, tol),
          "svg color: a half-opacity shape bakes premultiplied (~50% in color and alpha)");

    // --- Draw path: the order icon native 1:1 with a white tint - no recolor, the colors are baked.
    const int cw = (int)eo->cell.w - 2 * GPU_SVG_ATLAS_PAD;
    const int ch = (int)eo->cell.h - 2 * GPU_SVG_ATLAS_PAD;
    const FlColor green = { 0, 255, 0, 255 };
    const FlColor red = { 255, 0, 0, 255 };
    void* start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, (FlImageSource) { .kind = FlImageKind_Static, .handle = h_order }, FlImageMode_Color,
                (FlRect) { 100, 100, (f32)cw, (f32)ch }, (FlRect) { 0 }, white, FlImageFilter_Linear, nullptr);
    FlRenderFrame frame = test_frame();
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    FlPixelReadback buf;
    CHECK(render_seam_read_pixels(backend, surface, &buf), "svg color: readback");
    CHECK(pixel_color(pixel_at(&buf, 100 + cw / 4, 100 + ch / 2), green, 3.0f),
          "svg color: the draw path samples the baked cell (left half green)");
    CHECK(pixel_color(pixel_at(&buf, 100 + 3 * cw / 4, 100 + ch / 2), red, 3.0f),
          "svg color: the draw path samples the baked cell (right half red)");

    fl_arena_destroy(arena);
    printf("gpu_smoke_test: svg color raster tests PASS\n");
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Part G2 - crisp under zoom (GPU SVG 1c): the builder pattern an icon host runs - quantize the physical
// dst size onto the ladder (svg_raster_size_for_dst), prepare the cell keyed by (handle, quantized size), draw
// at the exact dst. An icon re-rasters JUST its own cell, and only when its quantized step changes.

static int run_svg_zoom_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    void* ud = backend->user_data;
    FlArena* arena = arena_new();

    const FlVectorImage* img = svg_test_icon();
    CHECK(img != NULL, "svg zoom: the monochrome icon parses");

    const FlImageHandle A = 556; // fresh handles -> fall through to the SVG atlas, distinct from Part G's 555
    const FlImageHandle B = 557;
    const FlColor authored = { 50, 150, 200, 255 };
    const FlImageSource srcA = { .kind = FlImageKind_Static, .handle = A };

    // --- Partial re-raster: the dispatch count ticks only when the quantized step changes.
    struct {
        f32 dst;
        u32 expect_step;
        bool crossing;
    } zoom[] = {
        { 28.0f, 32, true },  //
        { 25.0f, 32, false }, //
        { 32.0f, 32, false }, //
        { 40.0f, 48, true },  //
        { 45.0f, 48, false }, //
        { 48.0f, 48, false }, //
        { 60.0f, 64, true },  //
    };
    u32 prev_count = gpu_debug_svg_raster_count(ud);
    for (u32 i = 0; i < sizeof(zoom) / sizeof(zoom[0]); i++) {
        // 4:3 icon -> larger side drives it; the monochrome class keeps the full ladder to 2048.
        u16 q = svg_raster_size_for_dst(zoom[i].dst, zoom[i].dst * 0.75f, FlVectorClass_Monochrome);
        CHECK((u32)q == zoom[i].expect_step, "svg zoom: the physical dst quantizes to the expected ladder step");
        backend->prepare_vector(ud, A, q, img);
        u32 count = gpu_debug_svg_raster_count(ud);
        if (zoom[i].crossing) {
            CHECK(count == prev_count + 1, "svg zoom: a quantized-step crossing rasters exactly one cell");
        } else {
            CHECK(count == prev_count, "svg zoom: zooming within a step does not re-raster (count stays flat)");
        }
        const GpuSvgEntry* live = gpu_debug_find_svg(ud, A);
        CHECK(live != NULL && live->size == q, "svg zoom: the handle resolves to its current quantized cell");
        prev_count = count;
    }

    // --- A crossing touches only the affected icon's cell.
    backend->prepare_vector(ud, A, 32, img);
    backend->prepare_vector(ud, B, 32, img);
    const GpuSvgEntry* b_before = gpu_debug_find_svg(ud, B);
    CHECK(b_before != NULL, "svg zoom: the sibling icon places");
    GpuAtlasCell b_cell = b_before->cell;

    GpuSvgAtlasReadback atlas0;
    CHECK(gpu_debug_read_svg_atlas(ud, arena, &atlas0), "svg zoom: atlas readback (before)");
    u8* b_snapshot = arena_alloc_array(arena, u8, (size_t)b_cell.w * b_cell.h);
    for (u32 y = 0; y < b_cell.h; y++) {
        for (u32 x = 0; x < b_cell.w; x++) {
            b_snapshot[(size_t)y * b_cell.w + x] = svg_texel(&atlas0, b_cell.x + x, b_cell.y + y);
        }
    }
    u32 count_before_cross = gpu_debug_svg_raster_count(ud);

    backend->prepare_vector(ud, A, 48, img);
    CHECK(gpu_debug_svg_raster_count(ud) == count_before_cross + 1, "svg zoom: re-rastering A dispatches once");

    const GpuSvgEntry* b_after = gpu_debug_find_svg(ud, B);
    CHECK(b_after == b_before && b_after->cell.x == b_cell.x && b_after->cell.y == b_cell.y,
          "svg zoom: the sibling's live cell is unchanged by A's re-raster");
    FlArena* arena2 = arena_new();
    GpuSvgAtlasReadback atlas1;
    CHECK(gpu_debug_read_svg_atlas(ud, arena2, &atlas1), "svg zoom: atlas readback (after)");
    bool b_texels_unchanged = true;
    for (u32 y = 0; y < b_cell.h && b_texels_unchanged; y++) {
        for (u32 x = 0; x < b_cell.w; x++) {
            if (svg_texel(&atlas1, b_cell.x + x, b_cell.y + y) != b_snapshot[(size_t)y * b_cell.w + x]) {
                b_texels_unchanged = false;
                break;
            }
        }
    }
    CHECK(b_texels_unchanged, "svg zoom: the sibling's atlas texels are byte-unchanged by A's re-raster");
    fl_arena_destroy(arena2);

    // --- Crisp at each step: a 1:1 draw at the step (dst == the cell's content extent) reads a fully covered
    // interior, at two distinct steps.
    const u16 steps[] = { 32, 48 };
    for (u32 i = 0; i < 2; i++) {
        backend->prepare_vector(ud, A, steps[i], img);
        const GpuSvgEntry* e = gpu_debug_find_svg(ud, A);
        CHECK(e != NULL && e->size == steps[i], "svg zoom: the cell is live at the step");
        int cw = (int)e->cell.w - 2 * GPU_SVG_ATLAS_PAD;
        int ch = (int)e->cell.h - 2 * GPU_SVG_ATLAS_PAD;
        void* start = ir_rewind_begin(ir_arena);
        fl_ir_image(ir_arena, srcA, FlImageMode_Color, (FlRect) { 100, 100, (f32)cw, (f32)ch }, (FlRect) { 0 }, white,
                    FlImageFilter_Linear, nullptr);
        FlRenderFrame frame = test_frame();
        ir_frame_render(backend, surface, &frame, ir_arena, start);
        FlPixelReadback buf;
        CHECK(render_seam_read_pixels(backend, surface, &buf), "svg zoom: readback at the step");
        CHECK(pixel_color(pixel_at(&buf, 100 + cw / 2, 100 + ch / 2), authored, 3.0f),
              "svg zoom: a 1:1 draw at the quantized step is crisp (full interior coverage -> authored fill)");
    }

    // --- Non-uniform stretch samples one cell. A is live at step 48 (content 48 x 36). Drawing it into a
    // 48 x 48 box stretches Y (36 -> 48) by sampling, and must add no raster: the quantize is on the uniform
    // size, never per-axis.
    backend->prepare_vector(ud, A, 48, img);
    const GpuSvgEntry* e48 = gpu_debug_find_svg(ud, A);
    int cw48 = (int)e48->cell.w - 2 * GPU_SVG_ATLAS_PAD;
    u32 count_before_stretch = gpu_debug_svg_raster_count(ud);
    void* start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, srcA, FlImageMode_Color, (FlRect) { 100, 100, (f32)cw48, (f32)cw48 }, (FlRect) { 0 }, white,
                FlImageFilter_Linear, nullptr);
    FlRenderFrame frame = test_frame();
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(gpu_debug_svg_raster_count(ud) == count_before_stretch,
          "svg zoom: a non-uniform stretch adds no raster (the single cell is sampled, not re-rastered)");
    FlPixelReadback buf;
    CHECK(render_seam_read_pixels(backend, surface, &buf), "svg zoom: readback for the stretch");
    CHECK(pixel_color(pixel_at(&buf, 100 + cw48 / 2, 100 + cw48 / 2), authored, 3.0f),
          "svg zoom: the stretched icon still samples its fill (softness, not a hole)");

    fl_arena_destroy(arena);
    printf("gpu_smoke_test: svg zoom (crisp-under-zoom + partial re-raster) tests PASS\n");
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Part G3 - atlas compaction on full: the append-only skyline packer leaks a handle's prior cells as
// orphans every time it re-keys onto a new size step, so a long zoom session eventually fills the atlas. On the
// placement failure the backend compacts wholesale - repack only the live cells, drop the orphans - re-rasters
// every survivor from its retained geometry into the new layout, then retries.

static int run_svg_compaction_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    void* ud = backend->user_data;

    const FlVectorImage* img = svg_test_icon();
    CHECK(img != NULL, "svg compaction: the monochrome icon parses");

    const FlImageHandle A = 560; // re-keyed across big steps to force overflow
    const FlImageHandle B = 561; // a small sibling that must survive compaction
    const FlColor authored = { 50, 150, 200, 255 };
    const FlImageSource srcA = { .kind = FlImageKind_Static, .handle = A };
    const FlImageSource srcB = { .kind = FlImageKind_Static, .handle = B };

    // The sibling is never re-keyed, so it stays live across every overflow.
    backend->prepare_vector(ud, B, 32, img);
    const GpuSvgEntry* b0 = gpu_debug_find_svg(ud, B);
    CHECK(b0 != NULL && b0->size == 32, "svg compaction: the sibling places live at its step");

    // Re-key A between two large steps; each call orphans A's prior big cell. The orphans pile up in the
    // append-only atlas until a placement fails and the backend compacts. The steps are sized to the 4096^2 R8
    // atlas: a 1024-step orphan is ~1MB, so a couple dozen re-keys bury the live set and force the overflow well
    // within the loop bound.
    const u32 compactions_before = gpu_debug_svg_compact_count(ud);
    const u16 big[] = { 1024, 768 };
    bool forced = false;
    for (u32 i = 0; i < 80 && !forced; i++) {
        backend->prepare_vector(ud, A, big[i % 2], img);
        forced = gpu_debug_svg_compact_count(ud) > compactions_before;
    }
    CHECK(forced, "svg compaction: re-keying a big icon across sizes overflows the atlas and forces a compaction");
    CHECK(gpu_debug_svg_compact_count(ud) == compactions_before + 1,
          "svg compaction: a single overflow triggers exactly one compaction");

    const GpuSvgEntry* a_live = gpu_debug_find_svg(ud, A);
    CHECK(a_live != NULL, "svg compaction: the re-keyed icon resolves to a live cell after compaction");

    const GpuSvgEntry* b_live = gpu_debug_find_svg(ud, B);
    CHECK(b_live != NULL && b_live->size == 32, "svg compaction: the sibling is retained at its current size");

    const int bcw = (int)b_live->cell.w - 2 * GPU_SVG_ATLAS_PAD;
    const int bch = (int)b_live->cell.h - 2 * GPU_SVG_ATLAS_PAD;
    const int acw = (int)a_live->cell.w - 2 * GPU_SVG_ATLAS_PAD;
    const int ach = (int)a_live->cell.h - 2 * GPU_SVG_ATLAS_PAD;
    void* start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, srcB, FlImageMode_Color, (FlRect) { 100, 100, (f32)bcw, (f32)bch }, (FlRect) { 0 }, white,
                FlImageFilter_Linear, nullptr);
    fl_ir_image(ir_arena, srcA, FlImageMode_Color, (FlRect) { 100, 300, (f32)acw, (f32)ach }, (FlRect) { 0 }, white,
                FlImageFilter_Linear, nullptr);
    FlRenderFrame frame = test_frame();
    ir_frame_render(backend, surface, &frame, ir_arena, start);

    FlPixelReadback buf;
    CHECK(render_seam_read_pixels(backend, surface, &buf), "svg compaction: readback");
    CHECK(pixel_color(pixel_at(&buf, 100 + bcw / 2, 100 + bch / 2), authored, 3.0f),
          "svg compaction: the surviving sibling renders its authored fill after compaction");
    CHECK(pixel_color(pixel_at(&buf, 100 + acw / 2, 300 + ach / 2), authored, 3.0f),
          "svg compaction: the re-keyed icon renders its authored fill after compaction");

    printf("gpu_smoke_test: svg compaction (atlas-full reclaim) tests PASS\n");
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Part G4 - sharpening past 512px: with the R8 atlas at 4096^2 and the mono ladder
// extended to 2048, a >512px dst keys a >512 step and prepare_vector rasters a cell whose content side exceeds
// 512, filling its interior.

static int run_svg_large_raster_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    (void)ir_arena;
    void* ud = backend->user_data;
    FlArena* arena = arena_new();

    const FlVectorImage* img = svg_test_icon(); // 40x30 monochrome solid fill
    CHECK(img != NULL, "svg large: the monochrome icon parses");

    // An 800x600 physical dst: 800 is the larger side, so it snaps up to ladder step 1024.
    const u16 q = svg_raster_size_for_dst(800.0f, 600.0f, FlVectorClass_Monochrome);
    CHECK(q == 1024, "svg large: an 800px mono dst keys the 1024 step (past the old 512 cap)");

    const FlImageHandle handle = 570; // distinct from the other Part G handles
    backend->prepare_vector(ud, handle, q, img);

    // The cell rasters at the quantized size: 40x30 fit to a 1024 larger side -> 1024 x 768 content plus the
    // padding ring.
    const GpuSvgEntry* e = gpu_debug_find_svg(ud, handle);
    CHECK(e != NULL, "svg large: the >512px icon resolves to a cell");
    CHECK(e->size == 1024, "svg large: the cell records the >512 quantized raster size");
    const u32 content_w = e->cell.w - 2 * GPU_SVG_ATLAS_PAD;
    const u32 content_h = e->cell.h - 2 * GPU_SVG_ATLAS_PAD;
    CHECK(content_w > 512, "svg large: the cell's content side exceeds 512 (no longer pinned at the old cap)");
    CHECK(content_w == 1024 && content_h == 768,
          "svg large: the aspect-fit content extent matches the 1024 raster size (40x30 -> 1024x768)");

    GpuSvgAtlasReadback atlas;
    CHECK(gpu_debug_read_svg_atlas(ud, arena, &atlas), "svg large: atlas readback");
    CHECK(atlas.width == GPU_SVG_ATLAS_SIZE_MONO,
          "svg large: the mono atlas is the grown 4096 extent (the >512 cell needs the larger texture)");
    CHECK(svg_texel(&atlas, e->cell.x + e->cell.w / 2, e->cell.y + e->cell.h / 2) >= SVG_FULL,
          "svg large: the >512 cell's interior is fully covered (the larger raster actually ran)");
    CHECK(svg_texel(&atlas, e->cell.x + GPU_SVG_ATLAS_PAD, e->cell.y + GPU_SVG_ATLAS_PAD) >= SVG_FULL,
          "svg large: coverage runs to the content edge of the >512 cell");

    fl_arena_destroy(arena);
    printf("gpu_smoke_test: svg large-raster (sharpen past 512px) tests PASS\n");
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Cross-backend glyph + text cases: the glyph raster / text-pixel / atlas-regen suites run on BOTH
// backends through the neutral seam (prepare_glyph, the glyph atlas readback, scale_changed, and rendered text
// runs). Every expectation is a property both backends satisfy, checked with the same tolerance - no
// per-backend golden fork.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Part D - glyph rasterization: light-hinted outlines from the embedded test fonts through the
// shared font layer and the backend's prepare_glyph seam into R8 atlas cells, verified by atlas readback.
// The fonts (glyph_test_fonts.h) carry 'A' - a solid 0.6 x 0.7 em square - and 'o' - a ring whose counter winds
// opposite - once as TrueType (quadratics) and once as CFF (cubics), so both curve flavors run the same checks.

static u8 glyph_texel(const GpuGlyphAtlasReadback* atlas, u32 x, u32 y) {
    return atlas->texels[(size_t)y * atlas->width + x];
}

// Coverage thresholds: "full" tolerates the shader's chordwise approximation of curved edges, "zero"
// tolerates one LSB of float noise on opposite-wound cancellation.
enum { GLYPH_FULL = 250, GLYPH_ZERO = 1 };

// The whole battery for one font flavor at one size, against one atlas readback snapshot.
static int run_glyph_font_checks(FlRenderBackend* backend, FlFontCache* fc, FlArena* arena, FlFontId font,
                                 const char* what) {
    const u16 size = 32 * 4; // quarter-px key for 32px, the loaded size

    u32 glyph_a = font_cache_get_glyph_index(fc, font, 'A');
    u32 glyph_o = font_cache_get_glyph_index(fc, font, 'o');
    CHECK(glyph_a != 0 && glyph_o != 0, "glyph raster: 'A' and 'o' must resolve to glyph indices");

    FlGlyphOutline outline;
    CHECK(font_cache_get_glyph_outline(fc, font, size, glyph_a, arena, &outline),
          "glyph raster: 'A' outline must load");
    backend->prepare_glyph(backend->user_data, font, size, glyph_a, &outline);
    CHECK(font_cache_get_glyph_outline(fc, font, size, glyph_o, arena, &outline),
          "glyph raster: 'o' outline must load");
    backend->prepare_glyph(backend->user_data, font, size, glyph_o, &outline);

    // .notdef (glyph 0) is empty in the test fonts: the metrics-only path - an entry, no cell, no dispatch.
    CHECK(font_cache_get_glyph_outline(fc, font, size, 0, arena, &outline),
          "glyph raster: the empty .notdef outline must load");
    backend->prepare_glyph(backend->user_data, font, size, 0, &outline);
    const GpuGlyphEntry* notdef = gpu_debug_find_glyph(backend->user_data, font, size, 0);
    CHECK(notdef != NULL && notdef->cell.w == 0 && notdef->cell.h == 0,
          "glyph raster: an empty glyph takes a metrics-only entry");
    CHECK(notdef->advance > 10.0f, "glyph raster: the metrics-only entry still carries its advance");

    GpuGlyphAtlasReadback atlas;
    CHECK(gpu_debug_read_glyph_atlas(backend->user_data, arena, &atlas), "glyph raster: atlas readback");

    // --- 'A', the solid square: design 0.6 x 0.7 em -> roughly 19 x 22 px at 32 px (hinting may nudge it).
    const GpuGlyphEntry* a = gpu_debug_find_glyph(backend->user_data, font, size, glyph_a);
    CHECK(a != NULL, "glyph raster: the square's entry must resolve");
    CHECK(a->cell.w >= 12 && a->cell.w <= 32 && a->cell.h >= 14 && a->cell.h <= 36,
          "glyph raster: the square's cell extent lands near its design size");
    CHECK(a->advance > 20.0f && a->advance < 30.0f, "glyph raster: the entry carries the advance");

    // Zero coverage on all four padding borders (the cell is written wall to wall, ring included).
    for (u32 i = 0; i < a->cell.w; i++) {
        CHECK(glyph_texel(&atlas, a->cell.x + i, a->cell.y) <= GLYPH_ZERO
                  && glyph_texel(&atlas, a->cell.x + i, a->cell.y + a->cell.h - 1) <= GLYPH_ZERO,
              "glyph raster: the padding rows must be empty");
    }
    for (u32 i = 0; i < a->cell.h; i++) {
        CHECK(glyph_texel(&atlas, a->cell.x, a->cell.y + i) <= GLYPH_ZERO
                  && glyph_texel(&atlas, a->cell.x + a->cell.w - 1, a->cell.y + i) <= GLYPH_ZERO,
              "glyph raster: the padding columns must be empty");
    }

    // A hinted box edge may land on a texel boundary, so the square's ramp need not have a partial step -
    // the ring below demands one from its curves.
    u32 ax = a->cell.x + a->cell.w / 2;
    u32 ay = a->cell.y + a->cell.h / 2;
    CHECK(glyph_texel(&atlas, ax, ay) >= GLYPH_FULL, "glyph raster: the square interior is fully covered");
    u8 high_water = 0;
    for (u32 x = a->cell.x; x <= ax; x++) {
        u8 t = glyph_texel(&atlas, x, ay);
        CHECK(t + GLYPH_ZERO >= high_water, "glyph raster: coverage must rise monotonically into the square");
        high_water = t > high_water ? t : high_water;
    }

    GpuAtlasCell cell_before = a->cell;
    CHECK(font_cache_get_glyph_outline(fc, font, size, glyph_a, arena, &outline),
          "glyph raster: 'A' outline must re-load");
    backend->prepare_glyph(backend->user_data, font, size, glyph_a, &outline);
    a = gpu_debug_find_glyph(backend->user_data, font, size, glyph_a);
    CHECK(a != NULL && a->cell.x == cell_before.x && a->cell.y == cell_before.y,
          "glyph raster: re-preparing a glyph keeps its cell");

    // --- 'o', the ring: outer r 0.28 em, counter r 0.14 em. At mid-height the band spans the leftmost
    // quarter of the glyph width, so mid-ring sits an eighth in from the left bbox edge.
    const GpuGlyphEntry* o = gpu_debug_find_glyph(backend->user_data, font, size, glyph_o);
    CHECK(o != NULL && o->cell.w >= 12 && o->cell.h >= 12, "glyph raster: the ring's entry must resolve");
    u32 ox = o->cell.x + o->cell.w / 2;
    u32 oy = o->cell.y + o->cell.h / 2;
    CHECK(glyph_texel(&atlas, ox, oy) <= GLYPH_ZERO,
          "glyph raster: the opposite-wound counter must cancel to zero coverage");
    u32 inner_w = o->cell.w - 2 * GPU_GLYPH_ATLAS_PAD;
    u32 mid_ring_x = o->cell.x + GPU_GLYPH_ATLAS_PAD + inner_w / 8;
    CHECK(glyph_texel(&atlas, mid_ring_x, oy) >= GLYPH_FULL, "glyph raster: mid-ring coverage is full");
    CHECK(glyph_texel(&atlas, o->cell.x + 1, o->cell.y + 1) <= GLYPH_ZERO,
          "glyph raster: the cell corner outside the round glyph stays empty");

    bool saw_partial = false;
    high_water = 0;
    for (u32 x = o->cell.x; x <= mid_ring_x; x++) {
        u8 t = glyph_texel(&atlas, x, oy);
        CHECK(t + GLYPH_ZERO >= high_water, "glyph raster: coverage must rise monotonically into the ring");
        saw_partial |= t > 20 && t < 235;
        high_water = t > high_water ? t : high_water;
    }
    CHECK(saw_partial, "glyph raster: a curved edge must produce a genuine AA-ramp texel");

    printf("gpu_smoke_test: glyph raster (%s) checks PASS\n", what);
    return 0;
}

static int run_glyph_raster_tests(FlRenderBackend* backend) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    FlArena* arena = arena_new();
    FlFontCache* fc = font_cache_new(arena);

    CHECK(backend->prepare_glyph != NULL, "glyph raster: the GPU backend must expose prepare_glyph");

    FlFontId ttf = font_cache_load_font_memory(fc, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    FlFontId cff = font_cache_load_font_memory(fc, k_glyph_test_cff, k_glyph_test_cff_size, 32);
    CHECK(ttf != FONT_ID_INVALID && cff != FONT_ID_INVALID, "glyph raster: the embedded fonts load from memory");

    if (run_glyph_font_checks(backend, fc, arena, ttf, "ttf quadratic") != 0
        || run_glyph_font_checks(backend, fc, arena, cff, "cff cubic") != 0) {
        return 1;
    }

    const GpuGlyphEntry* ea
        = gpu_debug_find_glyph(backend->user_data, ttf, 32 * 4, font_cache_get_glyph_index(fc, ttf, 'A'));
    const GpuGlyphEntry* eb
        = gpu_debug_find_glyph(backend->user_data, cff, 32 * 4, font_cache_get_glyph_index(fc, cff, 'A'));
    CHECK(ea != NULL && eb != NULL && !cells_overlap(ea->cell, eb->cell),
          "glyph raster: both fonts pack into one atlas in disjoint cells");

    font_cache_destroy(fc);
    fl_arena_destroy(arena);
    printf("gpu_smoke_test: glyph raster tests PASS\n");
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Part E - text drawing end to end: glyphs prepared through the seam, then an FlCmd_TextRun rendered and read
// back. 'A' in the embedded test font is a solid 0.6 x 0.7 em square (glyph_test_fonts.h), so a covered cell
// is a solid block of the run's color and the rows around it stay transparent. The glyph is keyed by its
// codepoint (FlGlyph.glyph_id is a codepoint today, the key the translator looks up); the glyph index only
// fetches the outline.

static int run_text_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    void* ud = backend->user_data;
    FlArena* arena = arena_new();
    FlFontCache* fc = font_cache_new(arena);
    FlFontId font = font_cache_load_font_memory(fc, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    CHECK(font != FONT_ID_INVALID, "text pixels: the embedded font loads");

    const u16 size = 32 * 4; // quarter-px key for 32px, the loaded size
    const u32 cp = 'A';
    u32 idx = font_cache_get_glyph_index(fc, font, cp);
    FlGlyphOutline outline;
    CHECK(idx != 0 && font_cache_get_glyph_outline(fc, font, size, idx, arena, &outline),
          "text pixels: the square glyph's outline loads");
    backend->prepare_glyph(ud, font, size, cp, &outline);
    const GpuGlyphEntry* e = gpu_debug_find_glyph(ud, font, size, cp);
    CHECK(e != NULL && e->cell.w > 0 && e->cell.h > 0, "text pixels: the glyph prepared into a real cell");

    // Two copies of the square in one run, far enough apart to leave a transparent gap between the cells.
    const FlColor text_col = { 230, 80, 200, 255 }; // a vivid tint, clearly not white - proves colorization
    const f32 pen_y = 300.0f;
    const f32 pen_x0 = 200.0f;
    const f32 pen_x1 = 260.0f;
    FlGlyph glyphs[2] = { { cp, pen_x0, pen_y }, { cp, pen_x1, pen_y } };

    void* start = ir_rewind_begin(ir_arena);
    fl_ir_text_run(ir_arena, font, size, 1.0f, text_col, glyphs, 2);
    FlRenderFrame frame = test_frame();
    ir_frame_render(backend, surface, &frame, ir_arena, start);

    FlPixelReadback buf;
    CHECK(render_seam_read_pixels(backend, surface, &buf), "text pixels: readback");

    // The first square's painted rect: pen offset by the cell's left/top metrics, padded-cell sized.
    int x0 = (int)(pen_x0 + e->left);
    int y0 = (int)(pen_y - e->top);
    int cw = (int)e->cell.w;
    int ch = (int)e->cell.h;
    int cx = x0 + cw / 2;
    int cy = y0 + ch / 2;

    CHECK(pixel_color(pixel_at(&buf, cx, cy), (FlColor) { 230, 80, 200, 255 }, 3.0f),
          "text pixels: the square interior is the run's color at full coverage");

    // Transparent a few pixels above the cell top and below the baseline (the square sits on the baseline).
    CHECK(pixel_transparent(pixel_at(&buf, cx, y0 - 3)), "text pixels: the row above the glyph stays transparent");
    CHECK(pixel_transparent(pixel_at(&buf, cx, y0 + ch + 3)),
          "text pixels: the row below the glyph (under the baseline) stays transparent");

    int gap_x = x0 + cw + 3;
    CHECK(gap_x < (int)(pen_x1 + e->left), "text pixels: the two squares leave a gap (test setup)");
    CHECK(pixel_transparent(pixel_at(&buf, gap_x, cy)), "text pixels: an empty column between glyphs does not darken");

    CHECK(pixel_color(pixel_at(&buf, (int)(pen_x1 + e->left) + cw / 2, cy), (FlColor) { 230, 80, 200, 255 }, 3.0f),
          "text pixels: the second glyph in the run also paints its square");

    // --- A codepoint the run's font lacks paints the fallback face's glyph, not .notdef. 'B' is absent from the
    // embedded test font and present in the fallback one. This leg drives render()'s own prepare pass
    // (set_font_cache, no host-side prepare_glyph) because that pass is where resolution happens.
    FlFontId fb_font = font_cache_load_font_memory(fc, k_fallback_test_ttf, k_fallback_test_ttf_size, 32);
    CHECK(fb_font != FONT_ID_INVALID, "text pixels: the fallback font loads");
    CHECK(font_cache_get_glyph_index(fc, font, 'B') == 0, "text pixels: 'B' is absent from the primary face");
    font_cache_set_fallback_font(fc, fb_font);
    backend->set_font_cache(ud, fc);

    const f32 fb_pen_x = 600.0f;
    FlGlyph fb_run[1] = { { 'B', fb_pen_x, pen_y } };
    void* fb_start = ir_rewind_begin(ir_arena);
    fl_ir_text_run(ir_arena, font, size, 1.0f, text_col, fb_run, 1);
    FlRenderFrame fb_frame = test_frame();
    ir_frame_render(backend, surface, &fb_frame, ir_arena, fb_start);

    // Keyed by the RUN's font even though the outline came from the fallback face - the key the translator uses.
    // An unresolved fallback would prepare the primary's empty .notdef instead: an entry with no cell.
    const GpuGlyphEntry* fe = gpu_debug_find_glyph(ud, font, size, 'B');
    CHECK(fe != NULL && fe->cell.w > 0 && fe->cell.h > 0,
          "text pixels: a fallback-only codepoint prepares a real cell (not the primary's empty .notdef)");

    CHECK(render_seam_read_pixels(backend, surface, &buf), "text pixels: fallback readback");
    CHECK(pixel_color(pixel_at(&buf, (int)(fb_pen_x + fe->left) + (int)fe->cell.w / 2,
                               (int)(pen_y - fe->top) + (int)fe->cell.h / 2),
                      (FlColor) { 230, 80, 200, 255 }, 3.0f),
          "text pixels: the fallback glyph paints its square in the run's color");

    backend->set_font_cache(ud, NULL); // the cache is about to die; the pointer lives on the backend
    font_cache_destroy(fc);
    fl_arena_destroy(arena);
    printf("gpu_smoke_test: text pixel tests PASS\n");
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Part F - glyph-atlas regeneration: a UI-scale change (the scale_changed seam) regenerates the atlas
// wholesale at the new pixel sizes, a resolution change alone does NOT (scale, not window size, is
// the trigger), and a full atlas repacks wholesale rather than evicting per glyph. Resolution and scale are
// restored to the create state on the way out - the frame-lifecycle test that follows assumes it.

static int run_text_atlas_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    void* ud = backend->user_data;
    FlArena* arena = arena_new();
    FlFontCache* fc = font_cache_new(arena);
    FlFontId font = font_cache_load_font_memory(fc, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    CHECK(font != FONT_ID_INVALID, "text atlas: the embedded font loads");
    u32 idx = font_cache_get_glyph_index(fc, font, 'A');
    CHECK(idx != 0, "text atlas: 'A' resolves to a glyph index");

    // --- A resolution change alone does NOT regenerate: scale, not window size, is the trigger ---
    FlGlyphOutline o32;
    CHECK(font_cache_get_glyph_outline(fc, font, 32 * 4, idx, arena, &o32), "text atlas: the size-32 outline loads");
    backend->prepare_glyph(ud, font, 32 * 4, 'A', &o32);
    const GpuGlyphEntry* before = gpu_debug_find_glyph(ud, font, 32 * 4, 'A');
    CHECK(before != NULL, "text atlas: the glyph prepares at the create resolution");
    u32 before_w = before->cell.w;
    u32 before_h = before->cell.h;

    // Resizing the window only rebuilds the framebuffer; the atlas is untouched because the UI scale is an
    // absolute value the core hands in, not a function of height.
    render_seam_change_resolution(backend, surface, 2560, 1080);
    CHECK(gpu_debug_find_glyph(ud, font, 32 * 4, 'A') != NULL,
          "text atlas: a width-only resize keeps the glyphs (no scale change)");
    render_seam_change_resolution(backend, surface, 2560, 720);
    CHECK(gpu_debug_find_glyph(ud, font, 32 * 4, 'A') != NULL,
          "text atlas: a height resize alone no longer regenerates (scale, not window size, is the trigger)");

    // --- Wholesale regeneration on a UI-scale change, via the scale_changed seam ---
    CHECK(backend->scale_changed != NULL, "text atlas: the GPU backend exposes the scale_changed seam");

    // An unchanged scale is a no-op (guarded on last_ui_scale, which the create set to the 1.0 default).
    backend->scale_changed(ud, 1.0f);
    CHECK(gpu_debug_find_glyph(ud, font, 32 * 4, 'A') != NULL,
          "text atlas: scale_changed at the same scale is a no-op (glyphs survive)");

    backend->scale_changed(ud, 2.0f);
    CHECK(gpu_debug_find_glyph(ud, font, 32 * 4, 'A') == NULL,
          "text atlas: a UI-scale change forgets every glyph (wholesale regen)");

    FlGlyphOutline o48;
    CHECK(font_cache_get_glyph_outline(fc, font, 48 * 4, idx, arena, &o48), "text atlas: the size-48 outline loads");
    backend->prepare_glyph(ud, font, 48 * 4, 'A', &o48);
    const GpuGlyphEntry* after = gpu_debug_find_glyph(ud, font, 48 * 4, 'A');
    CHECK(after != NULL && after->cell.x == 0 && after->cell.y == 0,
          "text atlas: the new-size glyph repacks from the origin");
    CHECK(after->cell.w > before_w && after->cell.h > before_h,
          "text atlas: the glyph re-rasterizes larger at the new size (crisp, not upscaled)");

    // --- Atlas-full: a wholesale repack at the frame boundary, not per-glyph eviction ---
    // One very large glyph at many distinct keys fills the atlas. The placement that overflows flags a DEFERRED
    // wipe (gpu_glyph_atlas_on_overflow): the backend never wipes inline - that would blank glyphs already placed
    // this frame - so the wholesale repack is honored at the next render() boundary. The glyph is sized large so
    // the GPU_GLYPH_ATLAS_SIZE^2 skyline overflows in a small, bounded loop; the entry-table cap
    // (GPU_GLYPH_ATLAS_MAX_ENTRIES) bounds it regardless, so the loop always terminates on a rejected placement.
    const u16 big_size = 512 * 4; // quarter-px key for 512px
    FlGlyphOutline big;
    CHECK(font_cache_get_glyph_outline(fc, font, big_size, idx, arena, &big), "text atlas: the large outline loads");
    backend->prepare_glyph(ud, font, big_size, 5000, &big);
    CHECK(gpu_debug_find_glyph(ud, font, big_size, 5000) != NULL, "text atlas: the stress sentinel places");
    bool overflowed = false;
    for (u32 i = 0; i < GPU_GLYPH_ATLAS_MAX_ENTRIES; i++) {
        backend->prepare_glyph(ud, font, big_size, 6000 + i, &big);
        // A rejected placement (atlas full) is absent and flags the deferred wipe.
        if (gpu_debug_find_glyph(ud, font, big_size, 6000 + i) == NULL) {
            overflowed = true;
            break;
        }
    }
    CHECK(overflowed, "text atlas: enough big glyphs overflow the atlas");
    CHECK(gpu_debug_find_glyph(ud, font, big_size, 5000) != NULL,
          "text atlas: the overflow defers the wipe (the sentinel is still resident before the frame boundary)");

    // An empty frame is enough; the wipe runs at render()'s tail regardless of content.
    void* ovf_start = ir_rewind_begin(ir_arena);
    FlRenderFrame ovf_frame = test_frame();
    ir_frame_render(backend, surface, &ovf_frame, ir_arena, ovf_start);
    CHECK(gpu_debug_find_glyph(ud, font, big_size, 5000) == NULL,
          "text atlas: a full atlas repacks wholesale at the frame boundary (the sentinel is forgotten)");

    backend->prepare_glyph(ud, font, big_size, 7777, &big);
    CHECK(gpu_debug_find_glyph(ud, font, big_size, 7777) != NULL,
          "text atlas: the backend keeps placing glyphs after a wholesale repack");

    backend->prepare_glyph(ud, font, 32 * 4, 'A', &o32);
    const GpuGlyphEntry* e = gpu_debug_find_glyph(ud, font, 32 * 4, 'A');
    CHECK(e != NULL && e->cell.w > 0, "text atlas: the closing glyph prepares");
    FlGlyph one = { 'A', 300.0f, 300.0f };
    void* start = ir_rewind_begin(ir_arena);
    fl_ir_text_run(ir_arena, font, 32 * 4, 1.0f, white, &one, 1);
    FlRenderFrame frame = test_frame();
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    FlPixelReadback buf;
    CHECK(render_seam_read_pixels(backend, surface, &buf), "text atlas: readback after the stress scene");
    int cx = (int)(300.0f + e->left) + (int)e->cell.w / 2;
    int cy = (int)(300.0f - e->top) + (int)e->cell.h / 2;
    CHECK(pixel_color(pixel_at(&buf, cx, cy), white, 3.0f),
          "text atlas: text still rasterizes (the white run) after a wholesale repack");

    // Restore the create resolution and scale for the tests that follow.
    render_seam_change_resolution(backend, surface, 1920, 1080);
    backend->scale_changed(ud, 1.0f);

    font_cache_destroy(fc);
    fl_arena_destroy(arena);
    printf("gpu_smoke_test: text atlas (regen + repack) tests PASS\n");
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared clear-screen case - the ONE pixel case that runs on both backends against one golden. It is
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

// The clear colour the case renders and the golden encodes - kept in step with k_clear_screen_golden_png.
#define CLEAR_SCREEN_R 34
#define CLEAR_SCREEN_G 68
#define CLEAR_SCREEN_B 102

static int run_clear_screen_test(FlRenderBackend* backend, FlArena* ir_arena) {
    const int w = 64;
    const int h = 64;

    FlSurface* surface = backend->create_surface(backend->user_data, w, h);

    // the backend exposes its offscreen target through the single-name platform-conditional accessor -
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared rect + border pixel cases - the core-tier primitives running on both backends through the
// neutral seam. Every expectation is a property of the rounded-rect SDF coverage model (coverage = clamp(0.5 - d))
// both backends compute identically, checked with the same pixel_color / channels_near tolerance - one
// comparison, no per-backend golden fork. Both cases render over a transparent background (no clear command), so
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

static int run_rect_pixel_test(FlRenderBackend* backend, FlArena* ir_arena) {
    const int w = 128;
    const int h = 128;
    FlSurface* surface = backend->create_surface(backend->user_data, w, h);

    const void* start = shared_ir_begin(ir_arena);
    fl_ir_fill_rect(ir_arena, (FlRect) { 32, 32, 64, 64 }, (FlCornerRadius) { .tl = 16.0f },
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
            CHECK(channels_near(px, (f32)rect_fill.r * a, (f32)rect_fill.g * a, (f32)rect_fill.b * a, (f32)px[3], 3.0f),
                  "rect: a corner AA pixel is the fill premultiplied by its coverage");
        }
    }
    CHECK(saw_partial, "rect: the rounded corner produces at least one genuine partial-coverage pixel");
    CHECK(prev_a == 255, "rect: the diagonal ends fully opaque inside the fill");

    backend->destroy_surface(backend->user_data, surface);
    printf("gpu_smoke_test: rect (rounded corner) PASS\n");
    return 0;
}

static int run_border_pixel_test(FlRenderBackend* backend, FlArena* ir_arena) {
    const int w = 128;
    const int h = 128;
    FlSurface* surface = backend->create_surface(backend->user_data, w, h);

    // Outer edges at 32/96, inner edges at 36/92 (outer + width), so the band is {32..36} and {92..96} per axis.
    const void* start = shared_ir_begin(ir_arena);
    fl_ir_border(ir_arena, (FlRect) { 32, 32, 64, 64 }, 4, 4, 4, 4, (FlCornerRadius) { 0 }, rect_fill);
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared polyline pixel cases - the optional-tier Polyline primitive running on both backends
// through the neutral seam. A polyline lowers to one draw command per edge: a stroke capsule (round caps/joins
// by policy) or, when filled, a fill-to-baseline trapezoid, with per-point intensity modulating stroke alpha.
// This suite pins both backends to the same coverage model within the shared pixel_color / channels_near
// tolerance, no per-backend golden fork. Rendered over a transparent background (no clear command), so an
// untouched pixel reads exactly zero.

// The stroke / fill colours (opaque, so they store premultiplied to themselves).
static const FlColor poly_stroke = { 200, 40, 50, 255 };
static const FlColor poly_fill = { 40, 200, 60, 255 };

static int run_polyline_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    FlRenderFrame frame = test_frame();
    FlPixelReadback buf;

    // A thick horizontal stroke (width 7, half-width 3.5) centered on y = 100, with a clean AA ramp across the
    // top edge at y = 96.5 (a fractional edge lands on a pixel-row center, so it produces a genuine partial -
    // an integer edge would be exact-coverage).
    void* start = ir_rewind_begin(ir_arena);
    emit_polyline(ir_arena, (FlPolyPoint[]) { { 100.0f, 100.0f, 1.0f }, { 200.0f, 100.0f, 1.0f } }, 2, poly_stroke,
                  poly_stroke, 7.0f, 0.0f, 0);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the horizontal stroke");
    CHECK(pixel_color(pixel_at(&buf, 150, 100), poly_stroke, 2.0f),
          "polyline: the stroke centerline is the opaque stroke color (on-line hit)");
    CHECK(pixel_transparent(pixel_at(&buf, 150, 90)),
          "polyline: a pixel well off the stroke stays fully transparent (off-line miss)");
    if (check_aa_ramp_line(&buf, 150, 94, 0, 1, 6, poly_stroke, true, "polyline horizontal stroke edge") != 0) {
        return 1;
    }

    start = ir_rewind_begin(ir_arena);
    emit_polyline(ir_arena, (FlPolyPoint[]) { { 100.0f, 300.0f, 1.0f }, { 300.0f, 500.0f, 1.0f } }, 2, poly_stroke,
                  poly_stroke, 4.0f, 0.0f, 0);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the diagonal stroke");
    CHECK(pixel_at(&buf, 200, 400)[3] == 255, "polyline diagonal: the centerline midpoint is opaque (on-line hit)");
    CHECK(pixel_transparent(pixel_at(&buf, 240, 400)),
          "polyline diagonal: a pixel inside the bbox but off the line stays transparent (off-line miss)");

    // Round joins fall out of the overlapping end caps, so the vertex pixel where two capsules meet must be
    // fully covered - no gap or seam at the join.
    start = ir_rewind_begin(ir_arena);
    emit_polyline(ir_arena,
                  (FlPolyPoint[]) { { 100.0f, 600.0f, 1.0f }, { 200.0f, 500.0f, 1.0f }, { 300.0f, 600.0f, 1.0f } }, 3,
                  poly_stroke, poly_stroke, 6.0f, 0.0f, 0);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the bent stroke");
    CHECK(pixel_at(&buf, 200, 500)[3] == 255,
          "polyline join: the elbow where two capsules meet is fully covered (round join, no gap)");

    // A closed polyline: a triangle A(100,100) B(300,100) C(200,250). FlPolylineFlags_closed emits the last->first
    // edge (C->A) too.
    start = ir_rewind_begin(ir_arena);
    emit_polyline(ir_arena,
                  (FlPolyPoint[]) { { 100.0f, 100.0f, 1.0f }, { 300.0f, 100.0f, 1.0f }, { 200.0f, 250.0f, 1.0f } }, 3,
                  poly_stroke, poly_stroke, 6.0f, 0.0f, FlPolylineFlags_closed);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the closed polyline");
    CHECK(pixel_color(pixel_at(&buf, 200, 100), poly_stroke, 2.0f),
          "polyline closed: the top edge (A->B) is the opaque stroke color");
    CHECK(pixel_color(pixel_at(&buf, 150, 175), poly_stroke, 2.0f),
          "polyline closed: the closing edge (last->first, C->A) draws — its midpoint is the opaque stroke color");
    CHECK(pixel_transparent(pixel_at(&buf, 200, 150)),
          "polyline closed: the interior stays untouched (a closed stroke is an outline, not a fill)");

    // Fill-to-baseline: a horizontal curve at y = 700 filled down to baseline y = 800.
    start = ir_rewind_begin(ir_arena);
    emit_polyline(ir_arena, (FlPolyPoint[]) { { 100.0f, 700.0f, 1.0f }, { 300.0f, 700.0f, 1.0f } }, 2, poly_stroke,
                  poly_fill, 2.0f, 800.0f, FlPolylineFlags_filled);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the filled polyline");
    CHECK(pixel_color(pixel_at(&buf, 200, 750), poly_fill, 2.0f),
          "polyline fill: a pixel between the curve and the baseline is the opaque fill color");
    CHECK(pixel_transparent(pixel_at(&buf, 200, 650)), "polyline fill: a pixel above the curve stays transparent");
    CHECK(pixel_transparent(pixel_at(&buf, 200, 803)), "polyline fill: a pixel below the baseline stays transparent");

    // Per-point intensity modulates the stroke alpha (and the premultiplied rgb with it).
    start = ir_rewind_begin(ir_arena);
    emit_polyline(ir_arena, (FlPolyPoint[]) { { 100.0f, 900.0f, 0.5f }, { 300.0f, 900.0f, 0.5f } }, 2, poly_stroke,
                  poly_stroke, 8.0f, 0.0f, 0);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the intensity stroke");
    CHECK(pixel_color(pixel_at(&buf, 200, 900), (FlColor) { 100, 20, 25, 128 }, 3.0f),
          "polyline intensity: a uniform intensity of 0.5 halves the premultiplied stroke color and alpha");

    start = ir_rewind_begin(ir_arena);
    emit_polyline(ir_arena, (FlPolyPoint[]) { { 100.0f, 950.0f, 1.0f }, { 300.0f, 950.0f, 0.0f } }, 2, poly_stroke,
                  poly_stroke, 8.0f, 0.0f, 0);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the intensity-gradient stroke");
    CHECK(pixel_at(&buf, 140, 950)[3] > pixel_at(&buf, 260, 950)[3],
          "polyline intensity: a 1.0 -> 0.0 gradient dims the stroke alpha along the segment");
    CHECK(pixel_at(&buf, 140, 950)[3] > 180, "polyline intensity: near the 1.0 end the stroke stays near-opaque");
    CHECK(pixel_at(&buf, 285, 950)[3] < 30,
          "polyline intensity: near the 0.0 end the stroke fades to near-transparent (a real ramp, not a constant)");

    printf("gpu_smoke_test: polyline pixel tests PASS\n");
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Arc / ring / circle-sector pixel cases (primitive, Metal follow-up) - the optional-tier arc
// SDF rendered end-to-end: a knob-indicator ring with butt caps, the same ring with round caps, a filled
// sector (thickness reaching the centre), and a full-sweep seamless ring. Both backends are pinned to the same
// coverage model and the same knob angle convention within the shared pixel_color / channels_near
// tolerance, no per-backend golden fork.

// Sample the pixel whose center a knob-convention polar point lands on (angle 0 at 12 o'clock,
// positive clockwise), y-down pixel space. Callers put centers on the half-pixel grid so cardinal points land
// exactly on pixel centers; elsewhere the containing pixel's center is within ~0.71px of the analytic point,
// so checks keep at least that much margin from any shape edge.
static const u8* pixel_at_knob_angle(const FlPixelReadback* buf, f32 cx, f32 cy, f32 angle, f32 r) {
    int x = (int)(cx + r * sinf(angle));
    int y = (int)(cy - r * cosf(angle));
    return pixel_at(buf, x, y);
}

static int run_arc_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    FlRenderFrame frame = test_frame();
    FlPixelReadback buf;

    const FlColor green = { 40, 200, 60, 255 };
    const FlColor red = { 200, 40, 50, 255 };
    const FlColor blue = { 20, 40, 200, 128 }; // half-alpha
    const f32 tau = 6.28318530717958647692f;   // one full turn, radians (knob angles)

    // --- Knob indicator arc, butt caps --------------------------------------------------------------------
    // Centered (100.5, 100.5), outer radius 60, thickness 12 (ring band 48..60, centerline 54), sweeping three
    // quarter-turns from 7:30 through 12 to 4:30. The bottom quarter is the gap.
    const f32 kcx = 100.5f;
    const f32 kcy = 100.5f;
    const f32 kend = 0.375f * tau;
    const f32 kdelta = 4.0f / 54.0f; // ~4px of arc length on the centerline
    void* start = ir_rewind_begin(ir_arena);
    fl_ir_arc(ir_arena, kcx, kcy, 60.0f, 12.0f, -kend, 0.75f * tau, green, 0, NULL);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the knob arc frame");

    CHECK(pixel_color(pixel_at_knob_angle(&buf, kcx, kcy, 0.0f, 54.0f), green, 2.0f),
          "knob arc: the centerline at 12 o'clock is solid");
    CHECK(pixel_color(pixel_at_knob_angle(&buf, kcx, kcy, 0.25f * tau, 54.0f), green, 2.0f)
              && pixel_color(pixel_at_knob_angle(&buf, kcx, kcy, -0.25f * tau, 54.0f), green, 2.0f),
          "knob arc: the centerline at 3 and 9 o'clock is solid");
    CHECK(pixel_transparent(pixel_at_knob_angle(&buf, kcx, kcy, 0.5f * tau, 54.0f)),
          "knob arc: the gap at 6 o'clock stays transparent");
    CHECK(pixel_transparent(pixel_at(&buf, 100, 100)), "knob arc: the ring interior (the knob face) stays transparent");
    CHECK(pixel_transparent(pixel_at_knob_angle(&buf, kcx, kcy, 0.0f, 40.0f)),
          "knob arc: inside the inner radius stays transparent");
    CHECK(pixel_transparent(pixel_at_knob_angle(&buf, kcx, kcy, 0.0f, 70.0f)),
          "knob arc: outside the outer radius stays transparent");

    // Radial AA walking inward across the outer edge at 3 o'clock (edge at x = 160.5).
    if (check_aa_ramp_line(&buf, 165, 100, -1, 0, 11, green, true, "knob arc outer edge") != 0) {
        return 1;
    }

    // Butt caps cut sharp at both end angles (~4px of centerline arc past the edge is fully clear).
    CHECK(pixel_color(pixel_at_knob_angle(&buf, kcx, kcy, kend - kdelta, 54.0f), green, 2.0f),
          "knob arc: just inside the end angle the centerline is solid");
    CHECK(pixel_transparent(pixel_at_knob_angle(&buf, kcx, kcy, kend + kdelta, 54.0f)),
          "knob arc: a butt cap cuts sharp past the end angle");
    CHECK(pixel_transparent(pixel_at_knob_angle(&buf, kcx, kcy, -kend - kdelta, 54.0f)),
          "knob arc: the start cap cuts sharp too");

    // --- The same knob with round caps --------------------------------------------------------------------
    // The cap disc (radius 6) covers ~4px past the end angle; ~10px past stays clear.
    start = ir_rewind_begin(ir_arena);
    fl_ir_arc(ir_arena, kcx, kcy, 60.0f, 12.0f, -kend, 0.75f * tau, green, FlArcFlags_round_caps, NULL);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the round-cap frame");
    CHECK(pixel_color(pixel_at_knob_angle(&buf, kcx, kcy, kend + kdelta, 54.0f), green, 2.0f),
          "knob arc round caps: the cap disc covers past the end angle");
    CHECK(pixel_transparent(pixel_at_knob_angle(&buf, kcx, kcy, kend + 10.0f / 54.0f, 54.0f)),
          "knob arc round caps: the cap disc ends at its radius");

    // --- Filled sector and full ring ----------------------------------------------------------------------
    // Thickness reaching the center makes a pie wedge; a full-turn sweep is a seamless ring.
    start = ir_rewind_begin(ir_arena);
    fl_ir_arc(ir_arena, 300.5f, 100.5f, 40.0f, 40.0f, 0.0f, 0.25f * tau, red, 0, NULL);
    fl_ir_arc(ir_arena, 500.5f, 100.5f, 40.0f, 8.0f, 0.0f, tau, blue, 0, NULL);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the sector/ring frame");

    CHECK(pixel_color(pixel_at_knob_angle(&buf, 300.5f, 100.5f, 0.125f * tau, 20.0f), red, 2.0f),
          "filled sector: the wedge interior is solid");
    CHECK(pixel_color(pixel_at_knob_angle(&buf, 300.5f, 100.5f, 0.125f * tau, 4.0f), red, 2.0f),
          "filled sector: solid right up near the apex");
    CHECK(pixel_transparent(pixel_at_knob_angle(&buf, 300.5f, 100.5f, 0.625f * tau, 20.0f)),
          "filled sector: the opposite quadrant stays transparent");
    CHECK(pixel_transparent(pixel_at_knob_angle(&buf, 300.5f, 100.5f, -0.02f * tau, 20.0f))
              && pixel_color(pixel_at_knob_angle(&buf, 300.5f, 100.5f, 0.02f * tau, 20.0f), red, 2.0f),
          "filled sector: the butt edge at 12 o'clock cuts sharp");

    for (int c = 0; c < 4; c++) {
        const f32 a = 128.0f / 255.0f;
        CHECK(channels_near(pixel_at_knob_angle(&buf, 500.5f, 100.5f, 0.25f * tau * (f32)c, 36.0f), 20.0f * a,
                            40.0f * a, 200.0f * a, 128.0f, 3.0f),
              "full ring: every cardinal renders, half-alpha premultiplied, with no seam");
    }

    printf("gpu_smoke_test: arc pixel tests PASS\n");
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Circle / diamond pixel cases (primitive, Metal follow-up) - the optional-tier filled-SDF
// shapes rendered end-to-end: a bordered circle (inward border ring), a borderless circle, a bordered diamond,
// and a clip-cut circle. Both backends are pinned to the same coverage model and the same border-ring split
// (outer-minus-inner coverage) within the shared pixel_color tolerance, no per-backend golden fork.
static int run_circle_diamond_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    FlRenderFrame frame = test_frame();
    FlPixelReadback buf;

    const FlColor green = { 40, 200, 60, 255 };
    const FlColor red = { 200, 40, 50, 255 };

    // --- Circles and diamond (modulation points / automation nodes), plus a clip cut ----------------------
    void* start = ir_rewind_begin(ir_arena);
    fl_ir_circle(ir_arena, 200.5f, 300.5f, 30.0f, red, green, 6.0f, NULL); // bordered: fill inside r = 24
    fl_ir_circle(ir_arena, 300.5f, 300.5f, 20.0f, red, green, 0.0f, NULL); // borderless: green never shows
    // Dashed ring, 6 on 6 off around the ring centerline at radius 18 (circumference 113.10). Nine periods
    // fit closest, so the rescale lands the pattern at 12.57 with a 6.28 dash - putting the top of the ring
    // a quarter period into a dash and the bottom a quarter period into a gap.
    fl_ir_circle_stroked(ir_arena, 700.5f, 300.5f, 20.0f, red, green, 4.0f, (FlStrokeStyle) { 6.0f, 6.0f }, NULL);
    fl_ir_diamond(ir_arena, 400.5f, 300.5f, 30.0f, 20.0f, red, green, 4.0f, NULL);
    fl_ir_clip_push(ir_arena, (FlRect) { 600.0f, 280.0f, 21.0f, 41.0f }); // cuts at x = 621
    fl_ir_circle(ir_arena, 620.5f, 300.5f, 15.0f, red, red, 0.0f, NULL);
    fl_ir_clip_pop(ir_arena);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the circle/diamond frame");

    CHECK(pixel_color(pixel_at(&buf, 200, 300), red, 2.0f), "circle: the center is the fill color");
    CHECK(pixel_color(pixel_at(&buf, 215, 300), red, 2.0f), "circle: inside the border ring is still fill");
    CHECK(pixel_color(pixel_at(&buf, 227, 300), green, 2.0f), "circle: the rim band is the border color");
    CHECK(pixel_transparent(pixel_at(&buf, 235, 300)), "circle: outside the radius is transparent");
    // AA at the outer rim (edge at x = 230.5): the ramp lands in the border color.
    if (check_aa_ramp_line(&buf, 233, 300, -1, 0, 6, green, true, "circle rim") != 0) {
        return 1;
    }
    CHECK(pixel_color(pixel_at(&buf, 300, 300), red, 2.0f) && pixel_color(pixel_at(&buf, 318, 300), red, 2.0f),
          "circle: a zero border width renders pure fill out to the rim");
    CHECK(pixel_transparent(pixel_at(&buf, 322, 300)), "circle: a zero border width still ends at the radius");

    // Diamond: fill inside the 4px-inset rhombus, the border band along the edge and at both vertices. (The
    // edge normal scale is rx*ry/len = 16.64, so the sampled offsets sit well clear of both band edges.)
    CHECK(pixel_color(pixel_at(&buf, 400, 300), red, 2.0f), "diamond: the center is the fill color");
    CHECK(pixel_color(pixel_at(&buf, 409, 306), red, 2.0f), "diamond: inside the inset fill region");
    CHECK(pixel_color(pixel_at(&buf, 413, 309), green, 2.0f), "diamond: the border band along the slant edge");
    CHECK(pixel_transparent(pixel_at(&buf, 418, 312)), "diamond: outside the rhombus is transparent");
    CHECK(pixel_color(pixel_at(&buf, 427, 300), green, 2.0f) && pixel_color(pixel_at(&buf, 400, 282), green, 2.0f),
          "diamond: just inside the x and y vertices is border color");
    CHECK(pixel_transparent(pixel_at(&buf, 433, 300)), "diamond: just outside the x vertex is transparent");
    // AA across the slant edge at pixel row 309 (centers y = 309.5, edge at x = 417.0, ~1.8px ramp
    // width): the partials land in the border color.
    if (check_aa_ramp_line(&buf, 421, 309, -1, 0, 7, green, true, "diamond slant edge") != 0) {
        return 1;
    }

    CHECK(pixel_color(pixel_at(&buf, 700, 282), green, 2.0f), "dashed circle: the ring is drawn inside a dash");
    CHECK(pixel_transparent(pixel_at(&buf, 700, 318)), "dashed circle: the ring is cut away inside a gap");
    CHECK(pixel_color(pixel_at(&buf, 700, 300), red, 2.0f), "dashed circle: the fill disc is untouched by the mask");
    CHECK(pixel_color(pixel_at(&buf, 700, 314), red, 2.0f),
          "dashed circle: the fill still covers the disc under a gap, out towards the ring");

    CHECK(pixel_color(pixel_at(&buf, 612, 300), red, 2.0f),
          "clipped circle: inside both the circle and the clip renders");
    CHECK(pixel_transparent(pixel_at(&buf, 625, 300)),
          "clipped circle: the clip cuts the circle even inside its radius");

    printf("gpu_smoke_test: circle/diamond pixel tests PASS\n");
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Meter-bar pixel cases (primitive, Metal follow-up) - the batched optional-tier primitive
// rendered end-to-end: a single bar-set record off a shared baseline carrying a full row of bars. Bar sets are
// batched at the IR record level (one FlCmd_Bar carries the whole row) and lower CPU-side to one GpuDraw_Rect
// per bar off the baseline. Rendering the whole row from one fl_ir_bars record is the readback that proves the
// record stays batched (not degraded to one fill-rect call per bar).
static int run_bar_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    FlRenderFrame frame = test_frame();
    FlPixelReadback buf;

    const FlColor green = { 40, 200, 60, 255 };

    // --- Meter bars ----------------------------------------------------------------------------------------
    // One bar-set record off a baseline at y = 400: two upward bars, a gap, and a negative bar hanging below.
    void* start = ir_rewind_begin(ir_arena);
    FlBar* bars = fl_ir_bars(ir_arena, 400.0f, green, 3, NULL);
    bars[0] = (FlBar) { 50.0f, 6.0f, 30.0f };
    bars[1] = (FlBar) { 60.0f, 6.0f, 60.0f };
    bars[2] = (FlBar) { 70.0f, 6.0f, -20.0f };
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "read_pixels failed on the meter bar frame");

    CHECK(pixel_color(pixel_at(&buf, 52, 380), green, 2.0f) && pixel_color(pixel_at(&buf, 62, 350), green, 2.0f),
          "meter bars: pixels inside each bar are solid");
    CHECK(pixel_transparent(pixel_at(&buf, 52, 360)), "meter bars: above a bar's height stays transparent");
    CHECK(pixel_transparent(pixel_at(&buf, 57, 380)), "meter bars: the gap between bars stays transparent");
    CHECK(pixel_color(pixel_at(&buf, 72, 410), green, 2.0f), "meter bars: a negative height hangs below the baseline");
    CHECK(pixel_transparent(pixel_at(&buf, 72, 395)) && pixel_transparent(pixel_at(&buf, 52, 405)),
          "meter bars: bars extend from the baseline in one direction only");
    CHECK(pixel_color(pixel_at(&buf, 52, 370), green, 2.0f) && pixel_transparent(pixel_at(&buf, 52, 369)),
          "meter bars: an integer bar top cuts exactly on the pixel boundary");

    printf("gpu_smoke_test: bar pixel tests PASS\n");
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared gradient + clip + image pixel cases - the remaining core-tier primitives running on both
// backends through the neutral seam: per-corner gradients (bilinear corner blend), clip cuts (the clip-rect SDF
// cut), and the atlas sampler path (RGBA color, alpha-coverage, linear/nearest filtering, src sub-rects, and a
// clipped image). Every expectation is a property both backends satisfy within the same channels_near
// tolerance - no per-backend golden fork. The slot / oversize image tier stays GL-only (run_image_tests / the
// slot suites); it is not part of the atlas sampler path.

static int run_gradient_pixel_test(FlRenderBackend* backend, FlArena* ir_arena) {
    const int w = 256;
    const int h = 256;
    FlSurface* surface = backend->create_surface(backend->user_data, w, h);

    const FlColor pure_red = { 255, 0, 0, 255 };
    const FlColor pure_green = { 0, 255, 0, 255 };
    const FlColor pure_blue = { 0, 0, 255, 255 };
    const FlColor black = { 0, 0, 0, 255 };
    const FlColor half_red = { 200, 40, 50, 128 };
    const FlColor half_blue = { 20, 40, 200, 128 };

    void* start = ir_rewind_begin(ir_arena);
    // G1: horizontal gradient black -> red (TL/BL black, TR/BR red).
    fl_ir_fill_rect(ir_arena, (FlRect) { 0, 0, 128, 16 }, (FlCornerRadius) { 0 },
                    (FlColor[4]) { black, pure_red, black, pure_red }, nullptr);
    // G2: vertical gradient green -> blue (TL/TR green, BL/BR blue).
    fl_ir_fill_rect(ir_arena, (FlRect) { 160, 0, 16, 128 }, (FlCornerRadius) { 0 },
                    (FlColor[4]) { pure_green, pure_green, pure_blue, pure_blue }, nullptr);
    // G3: four distinct corners (TL red, TR green, BL blue, BR white) - the corner-order check.
    fl_ir_fill_rect(ir_arena, (FlRect) { 0, 160, 64, 64 }, (FlCornerRadius) { 0 },
                    (FlColor[4]) { pure_red, pure_green, pure_blue, white }, nullptr);
    // G4: half-alpha gradient over the transparent target (premultiplied-output check).
    fl_ir_fill_rect(ir_arena, (FlRect) { 80, 160, 64, 32 }, (FlCornerRadius) { 0 },
                    (FlColor[4]) { half_red, half_blue, half_red, half_blue }, nullptr);
    FlRenderFrame frame = test_frame();
    ir_frame_render(backend, surface, &frame, ir_arena, start);

    FlPixelReadback buf;
    CHECK(render_seam_read_pixels(backend, surface, &buf), "gradient: read_pixels failed");

    // G1: the horizontal axis.
    u8 prev_r = 0;
    for (int x = 0; x < 128; x++) {
        const u8* gpx = pixel_at(&buf, x, 8);
        CHECK(gpx[0] >= prev_r, "gradient: red must be monotonic along the horizontal axis");
        CHECK(gpx[1] == 0 && gpx[2] == 0, "gradient: black->red must not invent green or blue");
        CHECK(gpx[3] == 255, "gradient: an opaque gradient stays fully opaque");
        prev_r = gpx[0];
    }
    CHECK(pixel_at(&buf, 0, 8)[0] < 8, "gradient: the horizontal left end is near black");
    CHECK(pixel_at(&buf, 127, 8)[0] > 247, "gradient: the horizontal right end is near pure red");
    CHECK(pixel_at(&buf, 64, 8)[0] > 120 && pixel_at(&buf, 64, 8)[0] < 136,
          "gradient: the horizontal midpoint sits near half intensity");

    // G2: the same property on the vertical axis (catches a u/v swap).
    u8 prev_b = 0;
    for (int y = 0; y < 128; y++) {
        const u8* gpx = pixel_at(&buf, 168, y);
        CHECK(gpx[2] >= prev_b, "gradient: blue must be monotonic along the vertical axis");
        CHECK((int)gpx[1] + (int)gpx[2] > 240, "gradient: green->blue must conserve intensity");
        prev_b = gpx[2];
    }
    CHECK(pixel_at(&buf, 168, 0)[1] > 247, "gradient: the vertical top end is near pure green");
    CHECK(pixel_at(&buf, 168, 127)[2] > 247, "gradient: the vertical bottom end is near pure blue");

    // G3: each corner region is dominated by its own corner color (catches TL/TR/BL/BR mixups in either the
    // translator packing or the shader's quadrant mapping).
    CHECK(pixel_at(&buf, 2, 162)[0] > 200 && pixel_at(&buf, 2, 162)[1] < 40 && pixel_at(&buf, 2, 162)[2] < 40,
          "gradient corners: TL is red-dominated");
    CHECK(pixel_at(&buf, 61, 162)[1] > 200 && pixel_at(&buf, 61, 162)[0] < 40 && pixel_at(&buf, 61, 162)[2] < 40,
          "gradient corners: TR is green-dominated");
    CHECK(pixel_at(&buf, 2, 221)[2] > 200 && pixel_at(&buf, 2, 221)[0] < 40 && pixel_at(&buf, 2, 221)[1] < 40,
          "gradient corners: BL is blue-dominated");
    CHECK(pixel_at(&buf, 61, 221)[0] > 200 && pixel_at(&buf, 61, 221)[1] > 200 && pixel_at(&buf, 61, 221)[2] > 200,
          "gradient corners: BR is white-dominated");

    // G4: at pixel center x = 112.5, u = 32.5/64, the interpolated color is (200 - 180u, 40, 50 + 150u) at
    // alpha 128.
    {
        const f32 u = 32.5f / 64.0f;
        const f32 a = 128.0f / 255.0f;
        CHECK(channels_near(pixel_at(&buf, 112, 176), (200.0f - 180.0f * u) * a, 40.0f * a, (50.0f + 150.0f * u) * a,
                            128.0f, 3.0f),
              "gradient: a half-alpha gradient lands premultiplied with the interpolated alpha");
    }

    backend->destroy_surface(backend->user_data, surface);
    printf("gpu_smoke_test: gradient (per-corner) PASS\n");
    return 0;
}

static int run_clip_pixel_test(FlRenderBackend* backend, FlArena* ir_arena) {
    const int w = 256;
    const int h = 256;
    FlSurface* surface = backend->create_surface(backend->user_data, w, h);

    const FlColor clip_red = { 200, 40, 50, 255 };
    const FlColor clip_green = { 40, 200, 60, 255 };

    void* start = ir_rewind_begin(ir_arena);
    // K1: a 64x64 red rect clipped to {16,16,32,32}.
    fl_ir_clip_push(ir_arena, (FlRect) { 16, 16, 32, 32 });
    fl_ir_fill_rect(ir_arena, (FlRect) { 0, 0, 64, 64 }, (FlCornerRadius) { 0 },
                    (FlColor[4]) { clip_red, clip_red, clip_red, clip_red }, nullptr);
    fl_ir_clip_pop(ir_arena);
    // K2: a green rect whose clip starts at the fractional x = 100.5 - pixel column 100 is half covered.
    fl_ir_clip_push(ir_arena, (FlRect) { 100.5f, 0.0f, 32.0f, 32.0f });
    fl_ir_fill_rect(ir_arena, (FlRect) { 96, 0, 64, 32 }, (FlCornerRadius) { 0 },
                    (FlColor[4]) { clip_green, clip_green, clip_green, clip_green }, nullptr);
    fl_ir_clip_pop(ir_arena);
    // K3: a rounded rect (TL radius 16, arc circle centered at (208,48)) clipped to its left 8 pixels
    // ({192..200} x {32..96}) - the clip edge at x = 200 slices through the corner arc.
    fl_ir_clip_push(ir_arena, (FlRect) { 192, 32, 8, 64 });
    fl_ir_fill_rect(ir_arena, (FlRect) { 192, 32, 64, 64 }, (FlCornerRadius) { .tl = 16.0f },
                    (FlColor[4]) { clip_red, clip_red, clip_red, clip_red }, nullptr);
    fl_ir_clip_pop(ir_arena);
    FlRenderFrame frame = test_frame();
    ir_frame_render(backend, surface, &frame, ir_arena, start);

    FlPixelReadback buf;
    CHECK(render_seam_read_pixels(backend, surface, &buf), "clip: read_pixels failed");

    CHECK(pixel_color(pixel_at(&buf, 16, 16), clip_red, 2.0f) && pixel_color(pixel_at(&buf, 47, 47), clip_red, 2.0f),
          "clip: pixels inside an integer clip keep full coverage");
    CHECK(pixel_transparent(pixel_at(&buf, 15, 30)) && pixel_transparent(pixel_at(&buf, 48, 30))
              && pixel_transparent(pixel_at(&buf, 30, 15)) && pixel_transparent(pixel_at(&buf, 30, 48)),
          "clip: the rect must not bleed past any integer clip edge");
    // K2: the fractional clip edge at x = 100.5 half-covers pixel 100 (center at 100.5), like a fractional rect edge.
    CHECK(pixel_transparent(pixel_at(&buf, 99, 10)), "clip: pixels left of a fractional clip edge stay transparent");
    CHECK(channels_near(pixel_at(&buf, 100, 10), 40.0f * 0.5f, 200.0f * 0.5f, 60.0f * 0.5f, 127.5f, 3.0f),
          "clip: a fractionally clipped pixel carries analytic half coverage");
    CHECK(pixel_color(pixel_at(&buf, 101, 10), clip_green, 2.0f),
          "clip: pixels right of a fractional clip edge keep full coverage");
    CHECK(pixel_color(pixel_at(&buf, 199, 44), clip_red, 2.0f),
          "clip on a rounded corner: inside both the arc and the clip is the solid fill");
    CHECK(pixel_transparent(pixel_at(&buf, 200, 44)) && pixel_transparent(pixel_at(&buf, 201, 80)),
          "clip on a rounded corner: nothing lands past the clip edge, arc or not");
    CHECK(pixel_transparent(pixel_at(&buf, 193, 33)),
          "clip on a rounded corner: outside the arc stays transparent inside the clip");
    CHECK(pixel_color(pixel_at(&buf, 193, 90), clip_red, 2.0f),
          "clip on a rounded corner: the straight edge below the corner still fills inside the clip");

    backend->destroy_surface(backend->user_data, surface);
    printf("gpu_smoke_test: clip (integer/fractional/rounded) PASS\n");
    return 0;
}

static int run_image_pixel_test(FlRenderBackend* backend, FlArena* ir_arena) {
    const int w = 256;
    const int h = 256;
    FlSurface* surface = backend->create_surface(backend->user_data, w, h);
    CHECK(!backend->prepare_image_on_job,
          "image: the GPU backend defers prepare_image to the render thread (thread-confined uploads)");
    CHECK(backend->prepare_image != nullptr && backend->release_image != nullptr,
          "image: the backend exposes the prepare and release seams");

    FlArena* scratch = arena_new();
    FlImageLoadRequest req = { .relative_path = FL_S("synthetic") };

    // Color source: 8x8 opaque RGBA, each texel derived from its coordinates (opaque texels keep premultiplied ==
    // straight, so expectations stay exact). Distinct handles from the GL-only run_image_tests (101..) so both
    // suites can share the backend's atlas on the Linux build.
    enum { IMG_W = 8, IMG_H = 8 };
    static u8 rgba[IMG_W * IMG_H * 4];
    for (int y = 0; y < IMG_H; y++) {
        for (int x = 0; x < IMG_W; x++) {
            u8* px = &rgba[(y * IMG_W + x) * 4];
            px[0] = (u8)(x * 30);
            px[1] = (u8)(y * 30);
            px[2] = 77;
            px[3] = 255;
        }
    }
    FlImageDecodeResult color_decode = {
        .pixels = rgba,
        .width = IMG_W,
        .height = IMG_H,
        .stride = IMG_W * 4,
        .channels = 4,
        .premultiplied = true,
    };
    backend->prepare_image(backend->user_data, 301, &color_decode, scratch, &req);

    // Alpha-coverage source: 4x4, one alpha step per column (0, 85, 170, 255) - the white x alpha atlas form.
    static u8 alpha_px[4 * 4];
    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            alpha_px[y * 4 + x] = (u8)(x * 85);
        }
    }
    FlImageDecodeResult alpha_decode = { .pixels = alpha_px, .width = 4, .height = 4, .stride = 4, .channels = 1 };
    backend->prepare_image(backend->user_data, 302, &alpha_decode, scratch, &req);

    // Filter probe: 2x1 black|white scaled 8x so linear and nearest sampling are distinguishable.
    static u8 ramp_px[2 * 4] = { 0, 0, 0, 255, 255, 255, 255, 255 };
    FlImageDecodeResult ramp_decode = { .pixels = ramp_px, .width = 2, .height = 1, .stride = 8, .channels = 4 };
    backend->prepare_image(backend->user_data, 303, &ramp_decode, scratch, &req);

    // The two row-pitch forms the atlas-cell upload must handle, checked on whichever backend runs this case:
    //  - a padded source (stride 12 over a tight 8, 0xAA sentinel padding) - the pitch rides the upload, so none
    //    of the padding may leak into the cell;
    //  - a source declaring no stride at all (0 = tightly packed), where the upload derives w * bpp itself.
    // Same four texels either way, so both must land identically.
    static u8 padded_px[2 * 12] = {
        255, 0, 0,   255, 0,   255, 0, 255, 0xAA, 0xAA, 0xAA, 0xAA, // red, green, then row padding
        0,   0, 255, 255, 255, 255, 0, 255, 0xAA, 0xAA, 0xAA, 0xAA, // blue, yellow, then row padding
    };
    FlImageDecodeResult padded_decode
        = { .pixels = padded_px, .width = 2, .height = 2, .stride = 12, .channels = 4, .premultiplied = true };
    backend->prepare_image(backend->user_data, 304, &padded_decode, scratch, &req);

    static u8 tight_px[2 * 2 * 4] = {
        255, 0, 0,   255, 0,   255, 0, 255, //
        0,   0, 255, 255, 255, 255, 0, 255,
    };
    FlImageDecodeResult tight_decode
        = { .pixels = tight_px, .width = 2, .height = 2, .stride = 0, .channels = 4, .premultiplied = true };
    backend->prepare_image(backend->user_data, 305, &tight_decode, scratch, &req);

    const FlImageSource img_color = { .kind = FlImageKind_Static, .handle = 301 };
    const FlImageSource img_alpha = { .kind = FlImageKind_Static, .handle = 302 };
    const FlImageSource img_ramp = { .kind = FlImageKind_Static, .handle = 303 };
    const FlImageSource img_padded = { .kind = FlImageKind_Static, .handle = 304 };
    const FlImageSource img_tight = { .kind = FlImageKind_Static, .handle = 305 };

    void* start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, img_color, FlImageMode_Color, (FlRect) { 16, 16, 8, 8 }, (FlRect) { 0 }, white,
                FlImageFilter_Nearest, nullptr);
    fl_ir_image(ir_arena, img_color, FlImageMode_Color, (FlRect) { 48, 16, 8, 8 }, (FlRect) { 0 }, white,
                FlImageFilter_Linear, nullptr);
    fl_ir_image(ir_arena, img_color, FlImageMode_Color, (FlRect) { 80, 16, 8, 8 }, (FlRect) { 0 },
                (FlColor) { 128, 255, 255, 255 }, FlImageFilter_Nearest, nullptr);
    fl_ir_image(ir_arena, img_color, FlImageMode_Color, (FlRect) { 112, 16, 4, 4 }, (FlRect) { 4, 4, 4, 4 }, white,
                FlImageFilter_Nearest, nullptr); // src sub-rect (bottom-right quadrant)
    fl_ir_image(ir_arena, img_alpha, FlImageMode_AlphaCoverage, (FlRect) { 16, 48, 4, 4 }, (FlRect) { 0 },
                (FlColor) { 200, 40, 50, 255 }, FlImageFilter_Nearest, nullptr);
    fl_ir_image(ir_arena, img_ramp, FlImageMode_Color, (FlRect) { 48, 48, 16, 4 }, (FlRect) { 0 }, white,
                FlImageFilter_Linear, nullptr);
    fl_ir_image(ir_arena, img_ramp, FlImageMode_Color, (FlRect) { 48, 64, 16, 4 }, (FlRect) { 0 }, white,
                FlImageFilter_Nearest, nullptr);
    fl_ir_clip_push(ir_arena, (FlRect) { 96, 48, 4, 8 });
    fl_ir_image(ir_arena, img_color, FlImageMode_Color, (FlRect) { 96, 48, 8, 8 }, (FlRect) { 0 }, white,
                FlImageFilter_Nearest, nullptr);
    fl_ir_clip_pop(ir_arena);
    // The two row-pitch forms, at native size so every texel is a checkable passthrough.
    fl_ir_image(ir_arena, img_padded, FlImageMode_Color, (FlRect) { 16, 80, 2, 2 }, (FlRect) { 0 }, white,
                FlImageFilter_Nearest, nullptr);
    fl_ir_image(ir_arena, img_tight, FlImageMode_Color, (FlRect) { 32, 80, 2, 2 }, (FlRect) { 0 }, white,
                FlImageFilter_Nearest, nullptr);
    FlRenderFrame frame = test_frame();
    ir_frame_render(backend, surface, &frame, ir_arena, start);

    FlPixelReadback buf;
    CHECK(render_seam_read_pixels(backend, surface, &buf), "image: read_pixels failed");

    // Color mode at native size is texel passthrough - for nearest and linear alike (pixel centers map to texel
    // centers, where the linear weights collapse to one texel).
    for (int y = 0; y < IMG_H; y++) {
        for (int x = 0; x < IMG_W; x++) {
            CHECK(channels_near(pixel_at(&buf, 16 + x, 16 + y), (f32)(x * 30), (f32)(y * 30), 77, 255, 1.0f),
                  "image color: nearest at native size passes every texel through exactly");
            CHECK(channels_near(pixel_at(&buf, 48 + x, 16 + y), (f32)(x * 30), (f32)(y * 30), 77, 255, 1.0f),
                  "image color: linear at native size samples texel centers — also exact passthrough");
        }
    }
    CHECK(pixel_transparent(pixel_at(&buf, 15, 20)) && pixel_transparent(pixel_at(&buf, 24, 20)),
          "image color: nothing renders outside the dst rect");

    // Tint modulation: the red channel halves (128/255), the others pass through.
    CHECK(channels_near(pixel_at(&buf, 84, 18), 120.0f * 128.0f / 255.0f, 60, 77, 255, 2.0f),
          "image color: the tint modulates channels independently");

    CHECK(channels_near(pixel_at(&buf, 112, 16), 4 * 30, 4 * 30, 77, 255, 1.0f)
              && channels_near(pixel_at(&buf, 115, 19), 7 * 30, 7 * 30, 77, 255, 1.0f),
          "image color: a src sub-rect samples only its region");

    // Alpha-coverage: stored white x alpha, colorized by the tint - column x carries alpha x*85, premultiplied.
    for (int x = 0; x < 4; x++) {
        f32 a = (f32)(x * 85);
        CHECK(channels_near(pixel_at(&buf, 16 + x, 49), a * 200.0f / 255.0f, a * 40.0f / 255.0f, a * 50.0f / 255.0f, a,
                            3.0f),
              "image alpha-coverage: the tint colorizes stored white x alpha, premultiplied");
    }

    {
        u8 prev = 0;
        bool saw_mid = false;
        for (int x = 0; x < 16; x++) {
            const u8* lp = pixel_at(&buf, 48 + x, 50);
            CHECK(lp[0] >= prev, "image linear: the scaled ramp rises monotonically");
            prev = lp[0];
            if (lp[0] > 32 && lp[0] < 224) {
                saw_mid = true;
            }
            const u8* np = pixel_at(&buf, 48 + x, 66);
            CHECK(np[0] <= 1 || np[0] >= 254, "image nearest: the scaled ramp holds only the two texel values");
        }
        CHECK(saw_mid, "image linear: the scaled ramp passes through intermediate values");
        CHECK(pixel_at(&buf, 48, 66)[0] <= 1 && pixel_at(&buf, 63, 66)[0] >= 254,
              "image nearest: the step runs black to white across the dst");
    }

    // Clipped image: the left half {96..99} shows the image, the right half {100..103} is cut away.
    CHECK(channels_near(pixel_at(&buf, 98, 50), 2 * 30, 2 * 30, 77, 255, 1.0f),
          "clipped image: inside the clip the image renders");
    CHECK(pixel_transparent(pixel_at(&buf, 101, 50)),
          "clipped image: the clip cuts the image past its edge, like it cuts a fill");

    for (int i = 0; i < 2; i++) {
        int ox = 16 + i * 16; // the padded source, then the tight one
        const char* what = i == 0 ? "image padded stride: row padding rides the upload without leaking"
                                  : "image zero stride: a stride-less source uploads as tightly packed";
        CHECK(channels_near(pixel_at(&buf, ox + 0, 80), 255, 0, 0, 255, 1.0f), what);
        CHECK(channels_near(pixel_at(&buf, ox + 1, 80), 0, 255, 0, 255, 1.0f), what);
        CHECK(channels_near(pixel_at(&buf, ox + 0, 81), 0, 0, 255, 255, 1.0f), what);
        CHECK(channels_near(pixel_at(&buf, ox + 1, 81), 255, 255, 0, 255, 1.0f), what);
    }

    backend->release_image(backend->user_data, 301);
    start = ir_rewind_begin(ir_arena);
    fl_ir_image(ir_arena, img_color, FlImageMode_Color, (FlRect) { 16, 16, 8, 8 }, (FlRect) { 0 }, white,
                FlImageFilter_Nearest, nullptr);
    fl_ir_image(ir_arena, img_ramp, FlImageMode_Color, (FlRect) { 48, 48, 16, 4 }, (FlRect) { 0 }, white,
                FlImageFilter_Linear, nullptr);
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    CHECK(render_seam_read_pixels(backend, surface, &buf), "image: read_pixels after release failed");
    CHECK(pixel_transparent(pixel_at(&buf, 20, 20)), "image release: the released handle stops rendering");
    CHECK(pixel_at(&buf, 48, 50)[0] < 8 && pixel_at(&buf, 63, 50)[0] > 247,
          "image release: an untouched handle keeps rendering after another is released");

    // Release the rest so this case leaves no residue in the backend-wide atlas (the Linux build shares it with the
    // GL-only run_image_tests, which uses disjoint handles but a common atlas).
    backend->release_image(backend->user_data, 302);
    backend->release_image(backend->user_data, 303);
    backend->release_image(backend->user_data, 304);
    backend->release_image(backend->user_data, 305);

    fl_arena_destroy(scratch);
    backend->destroy_surface(backend->user_data, surface);
    printf("gpu_smoke_test: image (atlas sampler + clip) PASS\n");
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fl_app GPU path. The bare-backend cases above drive the render backend directly; this drives the
// fl_app RUNNER on the GPU backend it selects (the headless Surface Binding - EGL-surfaceless on Linux, a bare
// MTLDevice on macOS). The frame draws a rect and a circle (the circle is a GPU-tier IR primitive the
// software backend drops, so its presence in the readback is the GPU path's signature). fl_app owns its own
// headless context, so on Linux this runs after the shared EGL context is torn down.

static void fl_app_gpu_frame(void* user_data) {
    (void)user_data;
    // Paint only records inside an open painting scope (painter.h: a paint call outside any scope is a no-op).
    // A full-size root area is that scope; its origin is (0,0). fl_app composes ui_scale = content_scale x
    // ui_zoom; a headless backend reports content scale 1.0 and the default zoom is 1.0, so the scale is 1.0
    // here and element-local painter coordinates map 1:1 to the 256x256 surface.
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        Painter* p = ui_get_painter();
        painter_rect(p, 20.0f, 20.0f, 100.0f, 100.0f, ui_color(40, 60, 200, 255));
        painter_circle(p, 180.0f, 180.0f, 50.0f, ui_color(230, 80, 60, 255), ui_color(0, 0, 0, 0), 0.0f);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A Viewport3d inside a Detachable Panel, driven through the public API on the fl_app runner: a viewport is a
// regular layout element, so it docks, sizes to its leaf's rect, and reports that rect back for the caller's
// projection matrix.
//
// It takes more than one frame by construction: layout runs after the frame callback, so the first begin
// reports a zero size and has no target to draw yet. Frame 2 onward it renders.

#if !defined(__APPLE__) // only caller is the GL/EGL branch of main(); unused on the Metal path
#define VIEWPORT_PANEL_ID 0x763350u
#define VIEWPORT_PANEL_ID_RIGHT 0x763351u

// The two views of the one shared mesh: the same 0.4 shrink, pushed to opposite ends of its viewport. Both
// panels are full height, so "which band the mesh lands in" reads straight off the framebuffer and does not
// depend on how wide the dock made either panel.
static const f32 k_view_top[16] = {
    0.4f, 0.0f, 0.0f, 0.0f, //
    0.0f, 0.4f, 0.0f, 0.0f, //
    0.0f, 0.0f, 1.0f, 0.0f, //
    0.0f, 0.5f, 0.0f, 1.0f, //
};
static const f32 k_view_bottom[16] = {
    0.4f, 0.0f,  0.0f, 0.0f, //
    0.0f, 0.4f,  0.0f, 0.0f, //
    0.0f, 0.0f,  1.0f, 0.0f, //
    0.0f, -0.5f, 0.0f, 1.0f, //
};

typedef struct Viewport3dFrameState {
    FlMeshHandle mesh; // one mesh, drawn by both viewports - retained meshes are backend-shared
    FlVec2 last_size;
    FlVec2 last_size_right;
    bool viewport_declared;
    bool right_viewport_declared;
    bool layout_authored;
} Viewport3dFrameState;

// Emit one viewport filling panel, drawing the shared mesh through view_projection. Returns the size the
// viewport reported, or a zero size when the panel is not the visible tab this frame.
static FlVec2 emit_panel_viewport(Viewport3dFrameState* state, u64 panel, FlString name, const f32* view_projection,
                                  bool* declared) {
    if (!fl_dock_panel(panel, name)) {
        return (FlVec2) { 0 };
    }
    *declared = true;
    FlVec2 size = fl_viewport3d_begin((FlLayoutId) { .name = name });
    fl_viewport3d_set_view_projection(view_projection, 16);
    fl_viewport3d_draw_mesh(state->mesh, FL_IMAGE_HANDLE_INVALID, FlImageFilter_Linear);
    fl_viewport3d_end();
    fl_dock_panel_end(); // a true fl_dock_panel opened the panel's element inside its tile; this closes it
    return size;
}

static void viewport3d_app_frame(void* user_data) {
    Viewport3dFrameState* state = user_data;

    if (state->mesh == FL_MESH_HANDLE_INVALID) {
        // A single magenta quad filling most of clip space. Created on the first frame, when the UI context -
        // and through it the render backend the seam forwards to - is up.
        TestQuad quad = test_quad(-0.8f, 0.8f, 0.0f, (FlColor) { 220, 40, 200, 255 });
        static const u16 indices[6] = { 0, 1, 2, 0, 2, 3 };
        state->mesh = fl_mesh_create(quad.verts, 4, indices, 6);
    }

    fl_dock_workspace_begin();
    // Author the side-by-side layout once, as a built tree.
    if (!state->layout_authored) {
        state->layout_authored = true;
        dock_workspace_set_tree(dock_workspace_current(),
                                dock_tree_build(dock_split(FlDockAxis_X, 0.5f, dock_leaf(VIEWPORT_PANEL_ID),
                                                           dock_leaf(VIEWPORT_PANEL_ID_RIGHT))));
    }

    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        fl_dock_workspace_emit();
        state->last_size
            = emit_panel_viewport(state, VIEWPORT_PANEL_ID, FL_S("scene"), k_view_top, &state->viewport_declared);
        state->last_size_right = emit_panel_viewport(state, VIEWPORT_PANEL_ID_RIGHT, FL_S("scene_right"), k_view_bottom,
                                                     &state->right_viewport_declared);
    }

    fl_dock_workspace_end();
}

static int run_viewport3d_app_test(void) {
    FlAppSettings settings = {
        .window_title = FL_S("viewport3d"),
        .width = 256,
        .height = 256,
        .headless = true,
        .renderer = FlAppRenderer_Gpu,
    };
    FlApp* app = fl_app_create(&settings);
    CHECK(app != nullptr, "viewport3d app: fl_app_create(headless, GPU) failed");

    Viewport3dFrameState state = { .mesh = FL_MESH_HANDLE_INVALID };
    for (int i = 0; i < 4; i++) {
        CHECK(fl_app_tick(app, &state, viewport3d_app_frame), "viewport3d app: tick should keep running");
    }

    CHECK(state.viewport_declared, "viewport3d app: the panel never became visible, so nothing was exercised");
    CHECK(state.right_viewport_declared, "viewport3d app: the second panel never became visible");
    CHECK(state.mesh != FL_MESH_HANDLE_INVALID, "viewport3d app: mesh_create through the public API failed");
    CHECK(state.last_size.x > 0.0f && state.last_size.y > 0.0f,
          "viewport3d app: begin() must report the element's laid-out pixel size once it has one");
    CHECK(state.last_size_right.x > 0.0f && state.last_size_right.y > 0.0f,
          "viewport3d app: the second viewport must report its own laid-out size");

    FlPixelReadback rb;
    CHECK(fl_app_read_pixels(app, &rb), "viewport3d app: fl_app_read_pixels failed");

    // The two viewports are docked SIDE BY SIDE and draw the one shared mesh in the same frame, each through its
    // own matrix - one pushes it to the top of its own rect, the other to the bottom. So split the readback by x,
    // one tile per side, and collect each side's vertical extent: both sides must have drawn, and their two bands
    // must not overlap.
    //
    // The bands are measured against each other rather than against the window's midline: the tiles sit under the
    // workspace's title text and their leaf's tab header, so where the dock puts them is the dock's business.
    u32 left_min_y = rb.height, left_max_y = 0, left_count = 0;
    u32 right_min_y = rb.height, right_max_y = 0, right_count = 0;
    for (u32 y = 0; y < rb.height; ++y) {
        const u8* row = (const u8*)rb.pixels + (size_t)y * rb.stride;
        for (u32 x = 0; x < rb.width; ++x) {
            const u8* px = row + (size_t)x * 4u;
            // Magenta: red and blue high, green low. The dark dock chrome fakes neither.
            if (px[3] == 0 || px[0] <= 150 || px[2] <= 130 || px[1] >= 100) {
                continue;
            }
            if (x < rb.width / 2) {
                left_count++;
                if (y < left_min_y) {
                    left_min_y = y;
                }
                if (y > left_max_y) {
                    left_max_y = y;
                }
            } else {
                right_count++;
                if (y < right_min_y) {
                    right_min_y = y;
                }
                if (y > right_max_y) {
                    right_max_y = y;
                }
            }
        }
    }
    CHECK(left_count > 0, "viewport3d app: the mesh must render inside the docked viewport");
    CHECK(right_count > 0,
          "viewport3d app: the second docked viewport must render the same shared mesh in the same frame");
    // Which of the two ends up higher in the readback is the buffer's orientation, not this test's business.
    enum { MIN_BAND_GAP = 10 };
    CHECK(left_min_y > right_max_y + MIN_BAND_GAP || right_min_y > left_max_y + MIN_BAND_GAP,
          "viewport3d app: each viewport must apply its own view-projection — the two views' meshes must occupy "
          "separate, non-overlapping bands");

    fl_mesh_destroy(state.mesh);
    fl_app_destroy(app);
    printf("gpu_smoke_test: viewport3d docked elements (two viewports, one shared mesh) PASS\n");
    return 0;
}
#endif // !defined(__APPLE__) - viewport3d fl_app suite

static int run_em_app_gpu_path_test(void) {
    FlAppSettings settings = {
        .window_title = FL_S("fl_app gpu"),
        .width = 256,
        .height = 256,
        .headless = true,
        .renderer = FlAppRenderer_Gpu,
    };
    FlApp* app = fl_app_create(&settings);
    CHECK(app != nullptr, "fl_app_create(headless, GPU) failed");

    CHECK(fl_app_tick(app, nullptr, fl_app_gpu_frame), "fl_app_tick(GPU) should keep running");

    FlPixelReadback rb;
    CHECK(fl_app_read_pixels(app, &rb), "fl_app_read_pixels (GPU) failed");
    CHECK(rb.stride == rb.width * 4u, "GPU readback is RGBA8 (stride == width*4)");

    // Scan the readback for the two distinct fills - robust to ui_scale and exact coordinates. Fills are stored
    // premultiplied, so a fully-covered interior pixel is its own color; the dark theme clear is far below 150
    // on every channel, so it fakes neither fill.
    bool saw_rect = false;   // blue-dominant
    bool saw_circle = false; // red-dominant
    for (u32 y = 0; y < rb.height; ++y) {
        const u8* row = (const u8*)rb.pixels + (size_t)y * rb.stride;
        for (u32 x = 0; x < rb.width; ++x) {
            const u8* px = row + (size_t)x * 4u;
            if (px[3] == 0) {
                continue;
            }
            if (px[2] > 150 && px[2] > px[0] && px[2] > px[1]) {
                saw_rect = true;
            }
            if (px[0] > 150 && px[0] > px[1] && px[0] > px[2]) {
                saw_circle = true;
            }
        }
    }
    CHECK(saw_rect, "fl_app GPU path: the rect renders (a blue-dominant covered pixel)");
    CHECK(saw_circle, "fl_app GPU path: the circle renders on GPU through fl_app (a red-dominant covered pixel)");
    printf("gpu_smoke_test: fl_app GPU path (rect + circle) PASS\n");

    fl_app_destroy(app);
    return 0;
}

#if !defined(__APPLE__)
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// GPU perf fence - the GPU-gated sibling of unit_tests' scene_raster_fill. scene_zoom_change isolates the
// documented zoom-stall hot path: a UI-scale change (the scale_changed seam) wipes the glyph atlas
// wholesale, so the next frame's prepare_text_glyphs pass re-rasterizes every resident glyph. The scene renders a
// text frame cold, then forces that wipe and re-renders, asserting the tier-1 GPU work counters (glyph_dispatches,
// svg_dispatches, tile_overflow) at today's locked baseline through the gpu_debug_read_frame_stats seam and printing
// tier-3 wall-clock ns (never asserted - the llvmpipe CI GPU makes GPU timings meaningless).

static int run_perf_fence_scenes(FlRenderBackend* backend, FlArena* ir_arena) {
    void* ud = backend->user_data;
    FlSurface* surface = backend->create_surface(ud, 1920, 1080);
    FlArena* arena = arena_new();
    FlFontCache* fc = font_cache_new(arena);
    FlFontId font = font_cache_load_font_memory(fc, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    CHECK(font != FONT_ID_INVALID, "perf_fence: the embedded font loads");
    // Let render()'s prepare_text_glyphs realize the runs' glyphs itself - that pass IS the zoom-stall hot path.
    backend->set_font_cache(ud, fc);

    // scene_zoom_change: the two embedded codepoints ('A', 'o') at a spread of raster sizes. Each distinct
    // (font, size, glyph_id) is one atlas key, so the frame carries n_sizes*2 distinct glyphs - the resident set a
    // scale change forces the backend to rebuild. Spread horizontally so no 8x8 tile ever nears GPU_TILE_CAPACITY.
    static const u16 sizes[] = { 24, 28, 32, 36, 40 };
    const int n_sizes = (int)(sizeof(sizes) / sizeof(sizes[0]));
    const u32 expect_glyph_dispatches = (u32)(n_sizes * 2); // 'A' + 'o' at each size, one dispatch per glyph

    void* start = ir_rewind_begin(ir_arena);
    for (int s = 0; s < n_sizes; s++) {
        const f32 x = 100.0f + 300.0f * (f32)s;
        FlGlyph glyphs[2] = { { 'A', x, 200.0f }, { 'o', x + 60.0f, 200.0f } };
        fl_ir_text_run(ir_arena, font, sizes[s], 1.0f, white, glyphs, 2);
    }
    FlRenderFrame frame = test_frame();

    // svg_dispatches reuses the monotonic svg_raster_count (prior SVG sections already advanced it), so the
    // meaningful per-scene assertion is a delta: a text-only scene rasterizes no SVG cell. The delta check only
    // has teeth while svg_before is nonzero (a seam bug returning 0 would then wrap the unsigned subtraction and
    // fail loudly), which is why main() must keep an SVG section before this one - guard that ordering here.
    const u32 svg_before = gpu_debug_svg_raster_count(ud);
    CHECK(svg_before > 0, "perf_fence: expects an earlier SVG section to have advanced svg_raster_count");

    // Establish a known-cold glyph atlas: a scale change wipes it wholesale, so any glyph an earlier
    // section left resident cannot skew this scene's dispatch counts. 1.5 is a scale no prior section sets, so
    // the wipe fires regardless of the scale on entry.
    backend->scale_changed(ud, 1.5f);

    // Frame 1 - cold atlas: every distinct glyph rasterizes exactly once.
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    GpuFrameStats cold;
    gpu_debug_read_frame_stats(ud, surface, arena, &cold);
    CHECK(cold.glyph_dispatches == expect_glyph_dispatches,
          "perf_fence: the cold frame rasterizes every distinct glyph once (baseline drift)");
    CHECK(cold.tile_overflow == 0, "perf_fence: the sparse text scene overflows no tile (baseline drift)");

    // Frame 2 - a UI-scale change wipes the atlas, so this re-render re-rasterizes the whole resident set.
    backend->scale_changed(ud, 2.0f);
    const u64 t0 = get_monotonic_time_ns_os();
    ir_frame_render(backend, surface, &frame, ir_arena, start);
    const u64 t1 = get_monotonic_time_ns_os();
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
    // exactly 6 - the same setup run_classification_tests uses, here totalled through gpu_debug_read_frame_stats.
    void* ovf_start = ir_rewind_begin(ir_arena);
    for (int i = 0; i < 70; i++) {
        emit_solid_rect(ir_arena, 32.0f, 32.0f, 8.0f, 8.0f, red);
    }
    FlRenderFrame ovf_frame = test_frame();
    ir_frame_render(backend, surface, &ovf_frame, ir_arena, ovf_start);
    GpuFrameStats ovf;
    gpu_debug_read_frame_stats(ud, surface, arena, &ovf);
    CHECK(ovf.tile_overflow == 6, "perf_fence: the summation loop totals a genuine 64+6 single-tile overflow");

    // Unbind the font cache before destroying it (the pointer lives on the backend) and restore the create scale
    // for the tests that follow.
    backend->set_font_cache(ud, NULL);
    backend->scale_changed(ud, 1.0f);
    backend->destroy_surface(ud, surface);
    font_cache_destroy(fc);
    fl_arena_destroy(arena);
    printf("gpu_smoke_test: perf fence (scene_zoom_change) PASS\n");
    return 0;
}
#endif // !defined(__APPLE__) - GPU perf fence (Linux-only)

#if !defined(__APPLE__)
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Partial shader-compile failure: when one of the four programs fails to compile, create_gpu_renderer must
// delete the programs that did compile before returning the zeroed backend. The GpuShared is the failed arena's
// first allocation, so its address is recoverable from the arena cursor; the stale program IDs it still holds
// let glIsProgram prove the survivors were actually deleted (not just abandoned).

static int run_partial_compile_failure_test(void) {
    FlArena* fail_arena = arena_new();
    GpuShared* shared = (GpuShared*)fl_arena_aligned_pos(fail_arena, _Alignof(GpuShared));

    gpu_shaders_test_force_color_svg_failure(true);
    FlRenderBackend failed = create_gpu_renderer(fail_arena);
    gpu_shaders_test_force_color_svg_failure(false);

    CHECK(failed.render == NULL, "partial compile failure: create_gpu_renderer returns the zeroed backend");
    CHECK(shared->classify_prog != 0 && shared->raster_prog != 0 && shared->glyph_prog != 0,
          "partial compile failure: the other three programs compiled (the failure really was partial)");
    CHECK(shared->color_svg_prog == 0, "partial compile failure: the color svg program failed to compile");

    PFNGLISPROGRAMPROC is_program = (PFNGLISPROGRAMPROC)eglGetProcAddress("glIsProgram");
    CHECK(is_program != NULL, "partial compile failure: glIsProgram resolves");
    CHECK(!is_program(shared->classify_prog) && !is_program(shared->raster_prog) && !is_program(shared->glyph_prog),
          "partial compile failure: the compiled programs were deleted (no GL program leak)");

    fl_arena_destroy(fail_arena);
    printf("gpu_smoke_test: partial shader-compile failure cleanup PASS\n");
    return 0;
}
#endif // !defined(__APPLE__) - partial shader-compile failure (Linux-only)

#if defined(__APPLE__)
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// IOSurface Surface-texture variant, Metal only. The backend's second, additive accessor hands back the
// offscreen target's backing IOSurface so an out-of-process host can sample the UI cross-process. This case stands in
// for that host in-process: it renders the known clear colour, opens the returned IOSurface into a second MTLTexture
// on a freshly created MTLDevice (gpu_test_iosurface_readback, the Objective-C probe arm), and confirms the pixels
// read back through that independent device match both the golden colour and the primary accessor's own readback.

// Defined in gpu_iosurface_probe_metal.m (the Objective-C test arm); opens the IOSurface on a fresh device and blits
// its RGBA8 pixels into out_rgba (tight, width*height*4, row 0 first). Returns true on success.
bool gpu_test_iosurface_readback(void* iosurface, int width, int height, u8* out_rgba);

static int run_iosurface_surface_test(FlRenderBackend* backend, FlArena* ir_arena) {
    const int w = 64;
    const int h = 64;
    FlSurface* surface = backend->create_surface(backend->user_data, w, h);

    // Latch the surface into IOSurface mode up front (the host's setup step), then render - a first accessor call
    // rebuilds the target, so anything drawn before it would be discarded.
    GpuSurfaceIOSurface io = gpu_renderer_surface_iosurface(surface);
    CHECK(io.handle != NULL, "iosurface: the surface exposes a backend-owned IOSurface backing");
    CHECK(io.width == w && io.height == h, "iosurface: the handle carries the surface dims");

    GpuSurfaceTexture tex = gpu_renderer_surface_texture(surface);
    CHECK(tex.handle != 0,
          "iosurface: the primary MTLTexture accessor still returns the (now IOSurface-backed) target");
    CHECK(tex.width == w && tex.height == h, "iosurface: the primary accessor reports the same dims");

    // The primary accessor's two invalidation events (gpu_renderer.h) are the resolution change exercised further
    // down and this surface's first IOSurface call, which rebuilds the target in place. Only the second half is
    // assertable by handle: a repeat IOSurface call re-latches nothing, so both handles must survive it unchanged.
    // (The first call's invalidation is not observable here - Metal readily hands the rebuilt target the address the
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
    // 96x48 is deliberate - its tight row (384 B) is NOT 256-aligned, so unlike the 64x64 case (256 B, already
    // aligned) the readback's row-padding-strip arithmetic actually does work here.
    const int w2 = 96;
    const int h2 = 48;
    render_seam_change_resolution(backend, surface, w2, h2);
    GpuSurfaceIOSurface io2 = gpu_renderer_surface_iosurface(surface);
    CHECK(io2.handle != NULL, "iosurface: the handle survives change_resolution (still IOSurface-backed)");
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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

static int run_iosurface_churn_leak_test(FlRenderBackend* backend, FlArena* ir_arena) {
    const FlColor clear = { CLEAR_SCREEN_R, CLEAR_SCREEN_G, CLEAR_SCREEN_B, 255 };
    FlRenderFrame frame = test_frame();
    frame.commands = nullptr;
    frame.bytes = 0;
    frame.background.clear = clear;

    // Create/latch/render/read/destroy 32 times: exercises the metal_destroy_surface CFRelease path repeatedly.
    for (int i = 0; i < 32; ++i) {
        FlSurface* s = backend->create_surface(backend->user_data, 64, 64);
        GpuSurfaceIOSurface io = gpu_renderer_surface_iosurface(s);
        CHECK(io.handle != NULL, "iosurface churn: each fresh latched surface exposes an IOSurface backing");
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
        CHECK(io.handle != NULL && io.width == rw && io.height == rh,
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
    CHECK(oio.handle != NULL, "iosurface churn: ownership-check surface exposes a handle");
    gpu_test_cf_retain(oio.handle); // the test's independent +1
    backend->destroy_surface(backend->user_data, os);
    CHECK(gpu_test_cf_retain_count(oio.handle) == 1,
          "iosurface churn: destroy drops exactly the backend's IOSurface references (only the test's retain remains)");
    gpu_test_cf_release(oio.handle);

    printf("gpu_smoke_test: IOSurface ownership/leak churn PASS\n");
    return 0;
}
#endif // defined(__APPLE__)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int main(void) {
    // Single-threaded core init.
    FlArena* arena = fl_init(1);
    CHECK(arena != nullptr, "fl_init returned null arena");

#if defined(__APPLE__)
    // --- macOS / Metal: no EGL. The system default MTLDevice is the headless Surface Binding, always
    // available, so create the backend directly.
    FlRenderBackend backend = create_gpu_renderer(arena);
    CHECK(backend.render != nullptr, "create_gpu_renderer (Metal) failed — no MTLDevice or queue");

    FlArena* ir_arena = arena_new();
    if (run_clear_screen_test(&backend, ir_arena) != 0 || run_rect_pixel_test(&backend, ir_arena) != 0
        || run_border_pixel_test(&backend, ir_arena) != 0 || run_polyline_pixel_tests(&backend, ir_arena) != 0
        || run_arc_pixel_tests(&backend, ir_arena) != 0 || run_circle_diamond_pixel_tests(&backend, ir_arena) != 0
        || run_bar_pixel_tests(&backend, ir_arena) != 0 || run_gradient_pixel_test(&backend, ir_arena) != 0
        || run_clip_pixel_test(&backend, ir_arena) != 0 || run_image_pixel_test(&backend, ir_arena) != 0
        || run_glyph_raster_tests(&backend) != 0 || run_text_pixel_tests(&backend, ir_arena) != 0
        || run_text_atlas_tests(&backend, ir_arena) != 0 || run_svg_raster_tests(&backend, ir_arena) != 0
        || run_svg_color_raster_tests(&backend, ir_arena) != 0 || run_svg_zoom_tests(&backend, ir_arena) != 0
        || run_svg_compaction_tests(&backend, ir_arena) != 0 || run_svg_large_raster_tests(&backend, ir_arena) != 0
        || run_slot_image_pixel_tests(&backend, ir_arena) != 0 || run_iosurface_surface_test(&backend, ir_arena) != 0
        || run_iosurface_churn_leak_test(&backend, ir_arena) != 0) {
        return 1;
    }
    fl_arena_destroy(ir_arena);
    backend.destroy(backend.user_data);

    // fl_app Metal path: a self-contained headless run bringing up its own MTLDevice binding.
    if (run_em_app_gpu_path_test() != 0) {
        return 1;
    }
#else
    // --- Section 1: packer + translator unit tests, before any EGL/GL call -------------------------------------
    if (run_packer_tests() != 0 || run_glyph_atlas_tests() != 0 || run_glyph_lower_tests() != 0
        || run_translator_tests() != 0 || run_effect_translator_tests() != 0 || run_shape_translator_tests() != 0
        || run_polyline_translator_tests() != 0 || run_border_translator_tests() != 0 || run_slots_tests() != 0
        || run_slot_translator_tests() != 0 || run_background_translator_tests() != 0
        || run_text_translator_tests() != 0 || run_svg_translator_tests() != 0) {
        return 1;
    }

    // --- Section 2: the all-compute pipeline on an EGL surfaceless GL 4.3 context ------------------------------
    EGLDisplay dpy = create_surfaceless_gl43_context();
    CHECK(dpy != EGL_NO_DISPLAY, "EGL surfaceless GL 4.3 context creation failed");

    // Runs before the real backend so the failed attempt's GL state can't disturb the sections below.
    if (run_partial_compile_failure_test() != 0) {
        return 1;
    }

    FlRenderBackend backend = create_gpu_renderer(arena);
    CHECK(backend.render != nullptr, "create_gpu_renderer failed (GL floor or shader compile)");

    // Lifecycle must run last: its change_resolution(640, 360) would invalidate the 1920x1080 pixel
    // coordinates and tile grid the earlier sections assert against.
    FlArena* ir_arena = arena_new();
    if (run_clear_screen_test(&backend, ir_arena) != 0 || run_rect_pixel_test(&backend, ir_arena) != 0
        || run_border_pixel_test(&backend, ir_arena) != 0 || run_gradient_pixel_test(&backend, ir_arena) != 0
        || run_clip_pixel_test(&backend, ir_arena) != 0 || run_image_pixel_test(&backend, ir_arena) != 0
        || run_ui_frame_test(arena, &backend) != 0 || run_ui_image_widget_test(arena, &backend) != 0
        || run_menu_flip_test(arena, &backend) != 0 || run_pixel_tests(&backend, ir_arena) != 0
        || run_border_pixel_tests(&backend, ir_arena) != 0 || run_arc_pixel_tests(&backend, ir_arena) != 0
        || run_circle_diamond_pixel_tests(&backend, ir_arena) != 0 || run_bar_pixel_tests(&backend, ir_arena) != 0
        || run_image_tests(&backend, ir_arena) != 0 || run_slot_image_pixel_tests(&backend, ir_arena) != 0
        || run_background_pixel_tests(&backend, ir_arena) != 0 || run_effect_pixel_tests(&backend, ir_arena) != 0
        || run_polyline_pixel_tests(&backend, ir_arena) != 0 || run_classification_tests(&backend, ir_arena) != 0
        || run_glyph_raster_tests(&backend) != 0 || run_text_pixel_tests(&backend, ir_arena) != 0
        || run_text_atlas_tests(&backend, ir_arena) != 0 || run_svg_raster_tests(&backend, ir_arena) != 0
        || run_svg_color_raster_tests(&backend, ir_arena) != 0 || run_svg_zoom_tests(&backend, ir_arena) != 0
        || run_svg_compaction_tests(&backend, ir_arena) != 0 || run_svg_large_raster_tests(&backend, ir_arena) != 0
        || run_menu_static_kinds_test(arena, &backend) != 0 || run_n_surface_tests(&backend, ir_arena) != 0
        || run_surface_churn_leak_test(&backend) != 0 || run_perf_fence_scenes(&backend, ir_arena) != 0
        || run_viewport3d_tests(&backend, ir_arena) != 0 || run_frame_lifecycle_tests(&backend, ir_arena) != 0) {
        return 1;
    }

    fl_arena_destroy(ir_arena);
    backend.destroy(backend.user_data);
    eglTerminate(dpy);

    // fl_app GPU path: a self-contained run on fl_app's own EGL surfaceless context. Must come after the shared
    // context above is torn down - fl_app makes its own context current and unbinds it on destroy.
    if (run_em_app_gpu_path_test() != 0 || run_viewport3d_app_test() != 0) {
        return 1;
    }
#endif

    fl_arena_destroy(arena);

    printf("gpu_smoke_test: PASS\n");
    return 0;
}
