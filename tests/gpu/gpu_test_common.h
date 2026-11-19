#pragma once

// Shared includes, CHECK macro, pixel/IR helpers and suite prototypes for the gpu_smoke_test sources.

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
#include "ui/image/image_decode.h"
#include "ui/image/image_registry.h"
#include "ui/font_cache.h"
#include "ui/layout.h"
#include "ui/clay_to_render_ir.h"
#include "ui/painter.h"
#include "ui/dock.h"
#include <flowi/ui/dock.h>
#include <ui/render_backend.h>
#include <flowi/ui/viewport3d.h>
#include "ui/render_ir_build.h"
#include "ui/ui.h"
#include "ui/menu/ui_menu.h"
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
#include <stdlib.h>
#include <string.h>

#include "fixtures/fonts/fallback_test_font.h"
#include "fixtures/fonts/glyph_test_fonts.h"
#include "render_seam.h"

#define CHECK(cond, msg)                                       \
    do {                                                       \
        if (!(cond)) {                                         \
            fprintf(stderr, "gpu_smoke_test FAIL: %s\n", msg); \
            return 1;                                          \
        }                                                      \
    } while (0)

// The frame envelope every test starts from: full opacity, no background images.
static inline FlRenderFrame test_frame(void) {
    return (FlRenderFrame) {
        .global_alpha = 255,
        .background = { .current = FL_IMAGE_HANDLE_INVALID, .prev = FL_IMAGE_HANDLE_INVALID },
    };
}

// Pixel probes over a readback. A transparent pixel reads exactly zero in every channel (nothing drew there); an
// exact-color pixel matches c's RGBA within tolerance.
static inline const u8* pixel_at(const FlPixelReadback* buf, int x, int y) {
    return (const u8*)buf->pixels + (size_t)y * buf->stride + (size_t)x * 4u;
}

typedef struct ChannelExpect {
    f32 r, g, b, a;
} ChannelExpect;

static inline bool channel_near(u8 actual, f32 expected, f32 tolerance) {
    return fabsf((f32)actual - expected) <= tolerance;
}

static inline bool channels_near(const u8* px, ChannelExpect e, f32 tolerance) {
    return channel_near(px[0], e.r, tolerance) && channel_near(px[1], e.g, tolerance)
           && channel_near(px[2], e.b, tolerance) && channel_near(px[3], e.a, tolerance);
}

static inline bool pixel_transparent(const u8* px) {
    return channels_near(px, (ChannelExpect) { 0, 0, 0, 0 }, 0.0f);
}

static inline bool pixel_color(const u8* px, FlColor c, f32 tolerance) {
    return channels_near(px, (ChannelExpect) { (f32)c.r, (f32)c.g, (f32)c.b, (f32)c.a }, tolerance);
}

// IR emission + small test helpers. A stream is built between ir_begin/ir_bytes on an 8-aligned arena run,
// exactly like the core does it (ui.c).

static inline void* ir_begin(FlArena* arena) {
    fl_arena_align(arena, 8);
    return fl_arena_current_pos(arena);
}

static inline size_t ir_bytes(FlArena* arena, const void* start) {
    return (size_t)((const u8*)fl_arena_current_pos(arena) - (const u8*)start);
}

static inline void* ir_rewind_begin(FlArena* ir_arena) {
    fl_arena_rewind(ir_arena);
    return ir_begin(ir_arena);
}

static inline void ir_frame_render(FlRenderBackend* backend, FlSurface* surface, FlRenderFrame* frame,
                                   FlArena* ir_arena, const void* start) {
    frame->commands = start;
    frame->bytes = ir_bytes(ir_arena, start);
    render_seam_render(backend, surface, frame);
}

static const FlColor white = { 255, 255, 255, 255 };

static inline bool cells_overlap(GpuAtlasCell a, GpuAtlasCell b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
typedef struct PolylineStyle {
    FlColor color;
    FlColor fill_color;
    f32 width;
    f32 baseline_y;
    u32 flags;
} PolylineStyle;

// Emit a polyline into the IR arena from an array of points (no scaling; the points are already pixel coordinates).
static inline void emit_polyline(FlArena* arena, const FlPolyPoint* points, u32 count, PolylineStyle style) {
    FlPolyPoint* dst
        = ir_polyline(arena, style.color, style.fill_color, style.width, style.baseline_y, style.flags, count, nullptr);
    for (u32 i = 0; i < count; i++) {
        dst[i] = points[i];
    }
}

static inline void emit_solid_rect(FlArena* arena, FlRect rect, FlColor c) {
    ir_fill_rect(arena, rect, (FlCornerRadius) { 0 }, (FlColor[4]) { c, c, c, c }, nullptr);
}

static inline void emit_rect_effect(FlArena* arena, FlRect rect, FlColor c, const FlEffect* fx) {
    ir_fill_rect(arena, rect, (FlCornerRadius) { 0 }, (FlColor[4]) { c, c, c, c }, fx);
}

// Read an f32 back from a bit pattern the translator stored in a packed u32 word.
static inline f32 bits_f32(u32 bits) {
    union {
        u32 u;
        f32 f;
    } pun = { .u = bits };
    return pun.f;
}

// The packed RGBA8 (R in the low byte) the translator's pack_color produces for a color at the given
// global_alpha: alpha is c.a * global_alpha rounded to nearest, RGB pass through.
static inline u32 packed_rgba(FlColor c, u8 global_alpha) {
    u32 a = ((u32)c.a * (u32)global_alpha + 127u) / 255u;
    return (u32)c.r | ((u32)c.g << 8) | ((u32)c.b << 16) | (a << 24);
}

static const FlColor red = { 200, 40, 50, 255 };
static const FlColor green = { 40, 200, 60, 255 };
static const FlColor blue = { 20, 40, 200, 128 }; // half-alpha

static const f32 TAU = 6.28318530717958647692f; // one full turn, radians

// Translation target for the unit tests, comfortably larger than any emitted geometry so the full-target
// root clip never clamps an AABB a test asserts on.
enum { TR_TARGET = 2048 };

// An axis-aligned quad in clip space at a fixed depth, UVs spanning the whole texture: the shape every 3D draw
// uses, differing only in x extent, depth, and color. Every caller is inside the GL-only block.
typedef struct TestQuad {
    FlMeshVertex verts[4];
} TestQuad;

static inline TestQuad test_quad(f32 left, f32 right, f32 depth, FlColor color) {
    return (TestQuad) { .verts = {
                            { .x = left, .y = -0.8f, .z = depth, .u = 0.0f, .v = 0.0f, .color = color },
                            { .x = right, .y = -0.8f, .z = depth, .u = 1.0f, .v = 0.0f, .color = color },
                            { .x = right, .y = 0.8f, .z = depth, .u = 1.0f, .v = 1.0f, .color = color },
                            { .x = left, .y = 0.8f, .z = depth, .u = 0.0f, .v = 1.0f, .color = color },
                        } };
}

#define CLEAR_SCREEN_R 34
#define CLEAR_SCREEN_G 68
#define CLEAR_SCREEN_B 102

static inline u32 effect_kind_of(const GpuDrawCmd* cmd) {
    return (cmd->flags & GPU_DRAW_EFFECT_MASK) >> GPU_DRAW_EFFECT_SHIFT;
}

// Shared helpers implemented in gpu_test_common.c.
typedef struct AaRampRun {
    int x0, y0, dx, dy, count;
} AaRampRun;

typedef struct AaRampSpec {
    FlColor color;
    bool require_partial;
    const char* what;
} AaRampSpec;

typedef struct PixelProbe {
    int x, y;
    FlColor color;
    bool transparent;
    const char* msg;
} PixelProbe;

#define PROBE_COLOR(px, py, c, text) { .x = (px), .y = (py), .color = (c), .transparent = false, .msg = (text) }
#define PROBE_CLEAR(px, py, text) { .x = (px), .y = (py), .transparent = true, .msg = (text) }
#define CHECK_PROBES(buf, ...)                                      \
    check_pixel_probes((buf), (const PixelProbe[]) { __VA_ARGS__ }, \
                       sizeof((const PixelProbe[]) { __VA_ARGS__ }) / sizeof(PixelProbe))

// Asserts each probe in order: an opaque-color probe matches within a tolerance of 2, a clear probe reads zero.
int check_pixel_probes(const FlPixelReadback* buf, const PixelProbe* probes, size_t count);

int check_aa_ramp_line(const FlPixelReadback* buf, AaRampRun run, AaRampSpec spec);
#if !defined(__APPLE__)
typedef struct TranslateJob {
    FlArena* ir_arena;
    const void* start;
    u8 global_alpha;
    const GpuImageAtlas* atlas;
    GpuImageSlots* slots;
} TranslateJob;

GpuDrawList translate_with(FlArena* out_arena, TranslateJob job);
GpuDrawList translate(FlArena* out_arena, FlArena* ir_arena, const void* start, u8 global_alpha);
EGLDisplay create_surfaceless_gl43_context(void);
int run_partial_compile_failure_test(void);
#endif

// Suites.
int run_slot_image_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena);
int run_svg_raster_tests(FlRenderBackend* backend, FlArena* ir_arena);
int run_svg_color_raster_tests(FlRenderBackend* backend, FlArena* ir_arena);
int run_svg_zoom_tests(FlRenderBackend* backend, FlArena* ir_arena);
int run_svg_compaction_tests(FlRenderBackend* backend, FlArena* ir_arena);
int run_svg_large_raster_tests(FlRenderBackend* backend, FlArena* ir_arena);
int run_glyph_raster_tests(FlRenderBackend* backend);
int run_text_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena);
int run_text_atlas_tests(FlRenderBackend* backend, FlArena* ir_arena);
int run_clear_screen_test(FlRenderBackend* backend, FlArena* ir_arena);
int run_rect_pixel_test(FlRenderBackend* backend, FlArena* ir_arena);
int run_border_pixel_test(FlRenderBackend* backend, FlArena* ir_arena);
int run_polyline_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena);
int run_arc_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena);
int run_circle_diamond_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena);
int run_bar_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena);
int run_gradient_pixel_test(FlRenderBackend* backend, FlArena* ir_arena);
int run_clip_pixel_test(FlRenderBackend* backend, FlArena* ir_arena);
int run_painter_rects_pixel_test(FlRenderBackend* backend, FlArena* ir_arena);
int run_image_pixel_test(FlRenderBackend* backend, FlArena* ir_arena);
int run_viewport3d_app_test(void);
int run_em_app_gpu_path_test(void);
#if !defined(__APPLE__)
int run_packer_tests(void);
int run_glyph_atlas_tests(void);
int run_glyph_lower_tests(void);
int run_translator_tests(void);
int run_effect_translator_tests(void);
int run_shape_translator_tests(void);
int run_polyline_translator_tests(void);
int run_border_translator_tests(void);
int run_slots_tests(void);
int run_slot_translator_tests(void);
int run_background_translator_tests(void);
int run_text_translator_tests(void);
int run_svg_translator_tests(void);
int run_ui_frame_test(FlArena* arena, FlRenderBackend* backend);
int run_ui_image_widget_test(FlArena* arena, FlRenderBackend* backend);
int run_menu_flip_test(FlArena* arena, FlRenderBackend* backend);
int run_menu_static_kinds_test(FlArena* arena, FlRenderBackend* backend);
int run_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena);
int run_border_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena);
int run_image_tests(FlRenderBackend* backend, FlArena* ir_arena);
int run_background_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena);
int run_effect_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena);
int run_classification_tests(FlRenderBackend* backend, FlArena* ir_arena);
int run_viewport3d_tests(FlRenderBackend* backend, FlArena* ir_arena);
int run_frame_lifecycle_tests(FlRenderBackend* backend, FlArena* ir_arena);
int run_n_surface_tests(FlRenderBackend* backend, FlArena* ir_arena);
int run_surface_churn_leak_test(FlRenderBackend* backend);
int run_perf_fence_scenes(FlRenderBackend* backend, FlArena* ir_arena);
#endif
#if defined(__APPLE__)
int run_iosurface_surface_test(FlRenderBackend* backend, FlArena* ir_arena);
int run_iosurface_churn_leak_test(FlRenderBackend* backend, FlArena* ir_arena);
#endif
