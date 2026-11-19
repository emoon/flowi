// GPU smoke test: cross-backend SVG vector-icon raster tests.

#include "gpu_test_common.h"

// Cross-backend SVG vector-icon cases: the SVG raster / color-raster / zoom / compaction /
// large-raster suites run on both backends through the neutral seam (prepare_vector, the SVG atlas readbacks,
// scale_changed, and rendered icon draws). Every expectation is a property both backends satisfy (coverage
// thresholds, painter-order compositing, even-odd holes, premultiplied opacity, partial-re-raster cadence),
// checked with the same tolerance, with no per-backend golden fork.

// Part G: a monochrome SVG icon end to end. A solid-fill icon prepared through the vector seam
// (backend->prepare_vector) rasterizes coverage into the R8 SVG atlas, then draws via FlCmdImage's alpha-
// coverage + tint path. The icon is a single full-bounds rect, so at size 32 it aspect-fits to a fully covered
// 32 x 24 content cell: the readback interior is solid and the padding ring is clean zero, with no curved AA
// to reason about. The tint contract: opaque white keeps the icon's authored fill; any other tint recolors.

// #3296c8 = (50, 150, 200): a vivid fill clearly distinct from white, so the authored-color default is provable.
static const char k_svg_icon[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"30\">"
                                 "<rect x=\"0\" y=\"0\" width=\"40\" height=\"30\" fill=\"#3296c8\"/>"
                                 "</svg>";

enum { SVG_FULL = 250, SVG_ZERO = 1 }; // same tolerances as the glyph atlas readback

static u8 svg_texel(const GpuSvgAtlasReadback* atlas, u32 x, u32 y) {
    return atlas->texels[(size_t)y * atlas->width + x];
}

// The GPU backend retains a pointer to a handle's FlVectorImage to re-raster it on a zoom step or an atlas
// compaction, and the backend persists across all the Part G tests, so both the struct and its contours must
// outlive any single test. A stack-local FlVectorImage img would dangle even with its contours in an arena;
// the Part G tests share this one persistent parse instead. All of them use the same k_svg_icon.
static const FlVectorImage* svg_test_icon(void) {
    static FlVectorImage* icon = nullptr;
    if (icon == nullptr) {
        FlArena* a = arena_new(); // process-lifetime: the parse must outlive every test that retains it
        icon = arena_alloc_array(a, FlVectorImage, 1);
        bool ok = vector_image_parse((const u8*)k_svg_icon, sizeof(k_svg_icon) - 1, a, icon);
        if (!ok || icon->classification != FlVectorClass_Monochrome) {
            return nullptr; // the caller CHECKs this; a parse regression fails the test rather than crashing
        }
    }
    return icon;
}

typedef struct SvgCtx {
    FlRenderBackend* backend;
    FlSurface* surface;
    FlArena* ir_arena;
    FlArena* arena;
    FlRenderFrame frame;
    FlPixelReadback buf;
} SvgCtx;

static SvgCtx svg_ctx_create(FlRenderBackend* backend, FlArena* ir_arena) {
    return (SvgCtx) {
        .backend = backend,
        .surface = backend->create_surface(backend->user_data, 1920, 1080),
        .ir_arena = ir_arena,
        .arena = arena_new(),
        .frame = test_frame(),
    };
}

static void svg_ctx_destroy(SvgCtx* c, const char* pass_message) {
    fl_arena_destroy(c->arena);
    printf("gpu_smoke_test: %s PASS\n", pass_message);
    c->backend->destroy_surface(c->backend->user_data, c->surface);
}

static void svg_emit(SvgCtx* c, FlImageHandle handle, FlRect dst, FlColor tint) {
    const FlImageSource src = { .kind = FlImageKind_Static, .handle = handle };
    ir_image(c->ir_arena, src, FlImageMode_Color, dst, (FlRect) { 0 }, tint, FlImageFilter_Linear, nullptr);
}

static bool svg_render_read(SvgCtx* c, const void* start) {
    ir_frame_render(c->backend, c->surface, &c->frame, c->ir_arena, start);
    return render_seam_read_pixels(c->backend, c->surface, &c->buf);
}

static int cell_content_w(const GpuSvgEntry* e) {
    return (int)e->cell.w - 2 * GPU_SVG_ATLAS_PAD;
}

static int cell_content_h(const GpuSvgEntry* e) {
    return (int)e->cell.h - 2 * GPU_SVG_ATLAS_PAD;
}

// Draw a cell native 1:1 at (x, y) in a fresh frame and read the surface back.
static bool svg_draw_native(SvgCtx* c, FlImageHandle handle, const GpuSvgEntry* e, FlVec2 at) {
    void* start = ir_rewind_begin(c->ir_arena);
    svg_emit(c, handle, (FlRect) { at.x, at.y, (f32)cell_content_w(e), (f32)cell_content_h(e) }, white);
    return svg_render_read(c, start);
}

static int check_mono_entry(const GpuSvgEntry* e) {
    CHECK(e != nullptr, "svg raster: the prepared icon resolves to a cell");
    CHECK(e->cls == FlVectorClass_Monochrome, "svg raster: the cell records the monochrome classification");
    CHECK(e->mono_color.r == 50 && e->mono_color.g == 150 && e->mono_color.b == 200 && e->mono_color.a == 255,
          "svg raster: the cell carries the icon's authored fill color");
    CHECK(e->cell.w == 32 + 2 * GPU_SVG_ATLAS_PAD && e->cell.h == 24 + 2 * GPU_SVG_ATLAS_PAD,
          "svg raster: the cell is the aspect-fit content extent plus the padding ring");
    return 0;
}

static int check_mono_padding(const GpuSvgAtlasReadback* atlas, const GpuSvgEntry* e) {
    // The padding ring is empty on all four borders (the cell is written wall to wall, ring included).
    for (u32 i = 0; i < e->cell.w; i++) {
        CHECK(svg_texel(atlas, e->cell.x + i, e->cell.y) <= SVG_ZERO
                  && svg_texel(atlas, e->cell.x + i, e->cell.y + e->cell.h - 1) <= SVG_ZERO,
              "svg raster: the padding rows must be empty");
    }
    for (u32 i = 0; i < e->cell.h; i++) {
        CHECK(svg_texel(atlas, e->cell.x, e->cell.y + i) <= SVG_ZERO
                  && svg_texel(atlas, e->cell.x + e->cell.w - 1, e->cell.y + i) <= SVG_ZERO,
              "svg raster: the padding columns must be empty");
    }
    return 0;
}

static int check_mono_interior(const GpuSvgAtlasReadback* atlas, const GpuSvgEntry* e) {
    const u8 center = svg_texel(atlas, e->cell.x + e->cell.w / 2, e->cell.y + e->cell.h / 2);
    CHECK(center >= SVG_FULL, "svg raster: the solid icon interior is fully covered");
    const u8 corner = svg_texel(atlas, e->cell.x + GPU_SVG_ATLAS_PAD, e->cell.y + GPU_SVG_ATLAS_PAD);
    CHECK(corner >= SVG_FULL, "svg raster: coverage runs to the content edge (the first interior texel is full)");
    return 0;
}

static int check_mono_draw(SvgCtx* c, const GpuSvgEntry* e, FlImageHandle handle) {
    // Pixel + tint: draw the icon twice in one frame, native 1:1 so each interior pixel reads a fully
    // covered texel (premultiplied alpha 255 leaves rgb intact).
    const int cw = cell_content_w(e); // 32 content texels
    const int ch = cell_content_h(e); // 24 content texels
    const FlColor authored = { 50, 150, 200, 255 };
    const FlColor recolor = { 230, 80, 200, 255 };

    void* start = ir_rewind_begin(c->ir_arena);
    svg_emit(c, handle, (FlRect) { 100, 100, (f32)cw, (f32)ch }, white);
    svg_emit(c, handle, (FlRect) { 200, 100, (f32)cw, (f32)ch }, recolor);
    CHECK(svg_render_read(c, start), "svg raster: readback");
    CHECK(pixel_color(pixel_at(&c->buf, 100 + cw / 2, 100 + ch / 2), authored, 3.0f),
          "svg raster: an opaque-white tint draws the icon's authored fill color");
    CHECK(pixel_color(pixel_at(&c->buf, 200 + cw / 2, 100 + ch / 2), recolor, 3.0f),
          "svg raster: a non-identity tint recolors the icon");
    return 0;
}

int run_svg_raster_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    SvgCtx c = svg_ctx_create(backend, ir_arena);
    void* ud = backend->user_data;
    CHECK(backend->prepare_vector != nullptr, "svg raster: the GPU backend must expose prepare_vector");

    const FlVectorImage* img = svg_test_icon();
    CHECK(img != nullptr, "svg raster: the monochrome icon parses");
    CHECK(img->classification == FlVectorClass_Monochrome, "svg raster: a single fill classifies monochrome");

    const u16 size = 32;              // svg_quantize_size(32) == 32 here, so the cell raster size matches
    const FlImageHandle handle = 555; // unused by any image-atlas/slot test -> falls through to the SVG atlas
    backend->prepare_vector(ud, handle, size, img);

    // Atlas readback: 40x30 at size 32 -> scale 0.8 -> a 32 x 24 content block plus a 1-texel padding ring.
    const GpuSvgEntry* e = gpu_debug_find_svg(ud, handle);
    if (check_mono_entry(e) != 0) {
        return 1;
    }

    GpuSvgAtlasReadback atlas;
    CHECK(gpu_debug_read_svg_atlas(ud, c.arena, &atlas), "svg raster: atlas readback");
    if (check_mono_padding(&atlas, e) != 0) {
        return 1;
    }
    if (check_mono_interior(&atlas, e) != 0) {
        return 1;
    }
    if (check_mono_draw(&c, e, handle) != 0) {
        return 1;
    }

    svg_ctx_destroy(&c, "svg raster tests");
    return 0;
}

// Part G1b: a multi-color SVG icon end to end. An icon with >=2 distinct fills prepares through the vector seam
// into the RGBA8 color atlas (color_svg_prog composites its shapes in document order, painter's algorithm), then
// draws via FlCmdImage mode 0 sampling the baked premultiplied RGBA. Property checks, not byte-exact goldens:
// the dispatch golden is HITL on real hardware.

// Two opaque fills in document order: a full-bounds red rect under a left-half green rect. The left half
// composites green-over-red -> green; the right half stays red.
static const char k_svg_color_order[] = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"30\">"
                                        "<rect x=\"0\" y=\"0\" width=\"40\" height=\"30\" fill=\"#ff0000\"/>"
                                        "<rect x=\"0\" y=\"0\" width=\"20\" height=\"30\" fill=\"#00ff00\"/></svg>";

// An even-odd blue donut (one path, outer + inner subpath -> a rectangular hole) beside a red rect. The hole
// sits over empty space, so under even-odd it composites to transparent: the nested same-winding subpaths
// cancel.
static const char k_svg_color_evenodd[]
    = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"40\" height=\"30\">"
      "<path fill=\"#0000ff\" fill-rule=\"evenodd\" d=\"M0 0 H20 V30 H0 Z M5 5 H15 V25 H5 Z\"/>"
      "<rect x=\"25\" y=\"0\" width=\"15\" height=\"30\" fill=\"#ff0000\"/></svg>";

// A full-opacity red left half and a half-opacity blue right half (non-overlapping). The blue bakes
// premultiplied to ~(0, 0, 128, 128): opacity folded into the color at lowering time.
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
        return nullptr;
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

typedef struct ColorIcons {
    const GpuSvgEntry* order;
    const GpuSvgEntry* donut;
    const GpuSvgEntry* faded;
} ColorIcons;

typedef struct ColorProbe {
    const GpuSvgEntry* entry;
    u32 cx, cy;
    ChannelExpect want;
    const char* msg;
} ColorProbe;

static int check_color_probes(const GpuColorSvgAtlasReadback* ca, const ColorProbe* probes, size_t count) {
    const f32 tol = 4.0f; // premultiply rounding slack
    for (size_t i = 0; i < count; i++) {
        CHECK(channels_near(color_cell_texel(ca, probes[i].entry, probes[i].cx, probes[i].cy), probes[i].want, tol),
              probes[i].msg);
    }
    return 0;
}

static int prepare_color_icons(SvgCtx* c, ColorIcons* icons) {
    void* ud = c->backend->user_data;
    const u16 size = 32; // svg_quantize_size(32) == 32 here, so the cell raster size matches (a 40x30 -> 32x24)

    const FlVectorImage* order = color_test_icon(k_svg_color_order, (int)sizeof(k_svg_color_order) - 1);
    CHECK(order != nullptr && order->classification == FlVectorClass_MultiColor,
          "svg color: the two-fill icon parses multi-color");
    const FlVectorImage* donut = color_test_icon(k_svg_color_evenodd, (int)sizeof(k_svg_color_evenodd) - 1);
    CHECK(donut != nullptr && donut->classification == FlVectorClass_MultiColor,
          "svg color: the even-odd donut + second color parses multi-color");
    const FlVectorImage* faded = color_test_icon(k_svg_color_opacity_icon, (int)sizeof(k_svg_color_opacity_icon) - 1);
    CHECK(faded != nullptr && faded->classification == FlVectorClass_MultiColor,
          "svg color: the opacity icon parses multi-color");

    // Fresh handles past the monochrome section's, so they fall through to the color SVG atlas.
    c->backend->prepare_vector(ud, 600, size, order);
    c->backend->prepare_vector(ud, 601, size, donut);
    c->backend->prepare_vector(ud, 602, size, faded);

    icons->order = gpu_debug_find_color_svg(ud, 600);
    icons->donut = gpu_debug_find_color_svg(ud, 601);
    icons->faded = gpu_debug_find_color_svg(ud, 602);
    CHECK(icons->order != nullptr && icons->donut != nullptr && icons->faded != nullptr,
          "svg color: each prepared icon resolves to an RGBA8 cell");
    CHECK(icons->order->cls == FlVectorClass_MultiColor, "svg color: the cell records the multi-color classification");
    CHECK(icons->order->cell.w == 32 + 2 * GPU_SVG_ATLAS_PAD && icons->order->cell.h == 24 + 2 * GPU_SVG_ATLAS_PAD,
          "svg color: the cell is the aspect-fit content extent plus the padding ring");
    return 0;
}

static int check_color_cells(const GpuColorSvgAtlasReadback* ca, const ColorIcons* icons) {
    int status = check_color_probes(
        ca,
        (const ColorProbe[]) {
            // (a) document-order compositing. Content is 32 x 24; green covers the left 16, red the full width.
            { icons->order,
              8,
              12,
              { 0, 255, 0, 255 },
              "svg color: the later shape paints over the earlier (left half is the top green)" },
            { icons->order,
              24,
              12,
              { 255, 0, 0, 255 },
              "svg color: a region only the bottom shape covers keeps its color (right half stays red)" },
            // (b) even-odd hole.
            { icons->donut, 2, 12, { 0, 0, 255, 255 }, "svg color: the even-odd ring is filled" },
            { icons->donut, 8, 12, { 0, 0, 0, 0 }, "svg color: the even-odd hole composites transparent" },
            { icons->donut, 26, 12, { 255, 0, 0, 255 }, "svg color: the second color fills its own region" },
        },
        5);
    if (status != 0) {
        return 1;
    }
    // (c) per-shape opacity baked premultiplied.
    return check_color_probes(
        ca,
        (const ColorProbe[]) {
            { icons->faded, 8, 12, { 255, 0, 0, 255 }, "svg color: a full-opacity region bakes its color intact" },
            { icons->faded,
              24,
              12,
              { 0, 0, 128, 128 },
              "svg color: a half-opacity shape bakes premultiplied (~50% in color and alpha)" },
        },
        2);
}

static int check_color_draw(SvgCtx* c, const GpuSvgEntry* order_entry) {
    // Draw path: the order icon native 1:1 with a white tint. Nothing recolors because the colors are baked.
    const int cw = cell_content_w(order_entry);
    const int ch = cell_content_h(order_entry);
    const FlColor green = { 0, 255, 0, 255 };
    const FlColor red = { 255, 0, 0, 255 };
    CHECK(svg_draw_native(c, 600, order_entry, (FlVec2) { 100, 100 }), "svg color: readback");
    CHECK(pixel_color(pixel_at(&c->buf, 100 + cw / 4, 100 + ch / 2), green, 3.0f),
          "svg color: the draw path samples the baked cell (left half green)");
    CHECK(pixel_color(pixel_at(&c->buf, 100 + 3 * cw / 4, 100 + ch / 2), red, 3.0f),
          "svg color: the draw path samples the baked cell (right half red)");
    return 0;
}

int run_svg_color_raster_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    SvgCtx c = svg_ctx_create(backend, ir_arena);
    CHECK(backend->prepare_vector != nullptr, "svg color: the GPU backend must expose prepare_vector");

    ColorIcons icons;
    if (prepare_color_icons(&c, &icons) != 0) {
        return 1;
    }

    GpuColorSvgAtlasReadback ca;
    CHECK(gpu_debug_read_color_svg_atlas(backend->user_data, c.arena, &ca), "svg color: color atlas readback");
    if (check_color_cells(&ca, &icons) != 0) {
        return 1;
    }
    if (check_color_draw(&c, icons.order) != 0) {
        return 1;
    }

    svg_ctx_destroy(&c, "svg color raster tests");
    return 0;
}

// Part G2: crisp under zoom. The builder pattern an icon host runs is to quantize the physical
// dst size onto the ladder (svg_raster_size_for_dst), prepare the cell keyed by (handle, quantized size), and draw
// at the exact dst. An icon re-rasters only its own cell, and only when its quantized step changes.

typedef struct ZoomStep {
    f32 dst;
    u32 expect_step;
    bool crossing;
} ZoomStep;

static int check_zoom_step(SvgCtx* c, const FlVectorImage* img, const ZoomStep* step, u32* prev_count) {
    void* ud = c->backend->user_data;
    FlRenderBackend* backend = c->backend;
    const FlImageHandle A = 556;
    // 4:3 icon -> larger side drives it; the monochrome class keeps the full ladder to 2048.
    u16 q = svg_raster_size_for_dst(step->dst, step->dst * 0.75f, FlVectorClass_Monochrome);
    CHECK((u32)q == step->expect_step, "svg zoom: the physical dst quantizes to the expected ladder step");
    backend->prepare_vector(ud, A, q, img);
    u32 count = gpu_debug_svg_raster_count(ud);
    if (step->crossing) {
        CHECK(count == *prev_count + 1, "svg zoom: a quantized-step crossing rasters exactly one cell");
    } else {
        CHECK(count == *prev_count, "svg zoom: zooming within a step does not re-raster (count stays flat)");
    }
    const GpuSvgEntry* live = gpu_debug_find_svg(ud, A);
    CHECK(live != nullptr && live->size == q, "svg zoom: the handle resolves to its current quantized cell");
    *prev_count = count;
    return 0;
}

static int check_zoom_ladder(SvgCtx* c, const FlVectorImage* img) {
    // Partial re-raster: the dispatch count ticks only when the quantized step changes.
    static const ZoomStep zoom[] = {
        { 28.0f, 32, true },  //
        { 25.0f, 32, false }, //
        { 32.0f, 32, false }, //
        { 40.0f, 48, true },  //
        { 45.0f, 48, false }, //
        { 48.0f, 48, false }, //
        { 60.0f, 64, true },  //
    };
    void* ud = c->backend->user_data;
    u32 prev_count = gpu_debug_svg_raster_count(ud);
    for (u32 i = 0; i < sizeof_array(zoom); i++) {
        if (check_zoom_step(c, img, &zoom[i], &prev_count) != 0) {
            return 1;
        }
    }
    return 0;
}

static void snapshot_cell(const GpuSvgAtlasReadback* atlas, GpuAtlasCell cell, u8* out) {
    for (u32 y = 0; y < cell.h; y++) {
        for (u32 x = 0; x < cell.w; x++) {
            out[(size_t)y * cell.w + x] = svg_texel(atlas, cell.x + x, cell.y + y);
        }
    }
}

static bool cell_matches_snapshot(const GpuSvgAtlasReadback* atlas, GpuAtlasCell cell, const u8* snapshot) {
    for (u32 y = 0; y < cell.h; y++) {
        for (u32 x = 0; x < cell.w; x++) {
            if (svg_texel(atlas, cell.x + x, cell.y + y) != snapshot[(size_t)y * cell.w + x]) {
                return false;
            }
        }
    }
    return true;
}

static int check_sibling_unaffected(SvgCtx* c, const FlVectorImage* img) {
    void* ud = c->backend->user_data;
    const FlImageHandle A = 556;
    const FlImageHandle B = 557;
    // A crossing touches only the affected icon's cell.
    c->backend->prepare_vector(ud, A, 32, img);
    c->backend->prepare_vector(ud, B, 32, img);
    const GpuSvgEntry* b_before = gpu_debug_find_svg(ud, B);
    CHECK(b_before != nullptr, "svg zoom: the sibling icon places");
    GpuAtlasCell b_cell = b_before->cell;

    GpuSvgAtlasReadback atlas0;
    CHECK(gpu_debug_read_svg_atlas(ud, c->arena, &atlas0), "svg zoom: atlas readback (before)");
    u8* b_snapshot = arena_alloc_array(c->arena, u8, (size_t)b_cell.w * b_cell.h);
    snapshot_cell(&atlas0, b_cell, b_snapshot);
    u32 count_before_cross = gpu_debug_svg_raster_count(ud);

    c->backend->prepare_vector(ud, A, 48, img);
    CHECK(gpu_debug_svg_raster_count(ud) == count_before_cross + 1, "svg zoom: re-rastering A dispatches once");

    const GpuSvgEntry* b_after = gpu_debug_find_svg(ud, B);
    CHECK(b_after == b_before && b_after->cell.x == b_cell.x && b_after->cell.y == b_cell.y,
          "svg zoom: the sibling's live cell is unchanged by A's re-raster");
    FlArena* arena2 = arena_new();
    GpuSvgAtlasReadback atlas1;
    CHECK(gpu_debug_read_svg_atlas(ud, arena2, &atlas1), "svg zoom: atlas readback (after)");
    bool b_texels_unchanged = cell_matches_snapshot(&atlas1, b_cell, b_snapshot);
    CHECK(b_texels_unchanged, "svg zoom: the sibling's atlas texels are byte-unchanged by A's re-raster");
    fl_arena_destroy(arena2);
    return 0;
}

static int check_crisp_steps(SvgCtx* c, const FlVectorImage* img) {
    // Crisp at each step: a 1:1 draw at the step (dst == the cell's content extent) reads a fully covered
    // interior, at two distinct steps.
    void* ud = c->backend->user_data;
    const FlColor authored = { 50, 150, 200, 255 };
    const u16 steps[] = { 32, 48 };
    for (u32 i = 0; i < 2; i++) {
        c->backend->prepare_vector(ud, 556, steps[i], img);
        const GpuSvgEntry* e = gpu_debug_find_svg(ud, 556);
        CHECK(e != nullptr && e->size == steps[i], "svg zoom: the cell is live at the step");
        CHECK(svg_draw_native(c, 556, e, (FlVec2) { 100, 100 }), "svg zoom: readback at the step");
        const u8* center = pixel_at(&c->buf, 100 + cell_content_w(e) / 2, 100 + cell_content_h(e) / 2);
        CHECK(pixel_color(center, authored, 3.0f),
              "svg zoom: a 1:1 draw at the quantized step is crisp (full interior coverage -> authored fill)");
    }
    return 0;
}

static int check_zoom_stretch(SvgCtx* c, const FlVectorImage* img) {
    // Non-uniform stretch samples one cell. A is live at step 48 (content 48 x 36). Drawing it into a
    // 48 x 48 box stretches Y (36 -> 48) by sampling, and must add no raster: the quantize is on the uniform
    // size, never per-axis.
    void* ud = c->backend->user_data;
    const FlColor authored = { 50, 150, 200, 255 };
    c->backend->prepare_vector(ud, 556, 48, img);
    const GpuSvgEntry* e48 = gpu_debug_find_svg(ud, 556);
    int cw48 = cell_content_w(e48);
    u32 count_before_stretch = gpu_debug_svg_raster_count(ud);
    void* start = ir_rewind_begin(c->ir_arena);
    svg_emit(c, 556, (FlRect) { 100, 100, (f32)cw48, (f32)cw48 }, white);
    ir_frame_render(c->backend, c->surface, &c->frame, c->ir_arena, start);
    CHECK(gpu_debug_svg_raster_count(ud) == count_before_stretch,
          "svg zoom: a non-uniform stretch adds no raster (the single cell is sampled, not re-rastered)");
    CHECK(render_seam_read_pixels(c->backend, c->surface, &c->buf), "svg zoom: readback for the stretch");
    CHECK(pixel_color(pixel_at(&c->buf, 100 + cw48 / 2, 100 + cw48 / 2), authored, 3.0f),
          "svg zoom: the stretched icon still samples its fill (softness, not a hole)");
    return 0;
}

int run_svg_zoom_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    SvgCtx c = svg_ctx_create(backend, ir_arena);

    const FlVectorImage* img = svg_test_icon();
    CHECK(img != nullptr, "svg zoom: the monochrome icon parses");

    if (check_zoom_ladder(&c, img) != 0) {
        return 1;
    }
    if (check_sibling_unaffected(&c, img) != 0) {
        return 1;
    }
    if (check_crisp_steps(&c, img) != 0) {
        return 1;
    }
    if (check_zoom_stretch(&c, img) != 0) {
        return 1;
    }

    svg_ctx_destroy(&c, "svg zoom (crisp-under-zoom + partial re-raster) tests");
    return 0;
}

// Part G3: atlas compaction on full. The append-only skyline packer leaks a handle's prior cells as
// orphans every time it re-keys onto a new size step, so a long zoom session eventually fills the atlas. On the
// placement failure the backend compacts wholesale (repacking only the live cells and dropping the orphans), re-rasters
// every survivor from its retained geometry into the new layout, then retries.

static int force_compaction(SvgCtx* c, const FlVectorImage* img) {
    void* ud = c->backend->user_data;
    const FlImageHandle A = 560; // re-keyed across big steps to force overflow
    // Re-key A between two large steps; each call orphans A's prior big cell. The orphans pile up in the
    // append-only atlas until a placement fails and the backend compacts. The steps are sized to the 4096^2 R8
    // atlas: a 1024-step orphan is ~1MB, so a couple dozen re-keys bury the live set and force the overflow well
    // within the loop bound.
    const u32 compactions_before = gpu_debug_svg_compact_count(ud);
    const u16 big[] = { 1024, 768 };
    bool forced = false;
    for (u32 i = 0; i < 80 && !forced; i++) {
        c->backend->prepare_vector(ud, A, big[i % 2], img);
        forced = gpu_debug_svg_compact_count(ud) > compactions_before;
    }
    CHECK(forced, "svg compaction: re-keying a big icon across sizes overflows the atlas and forces a compaction");
    CHECK(gpu_debug_svg_compact_count(ud) == compactions_before + 1,
          "svg compaction: a single overflow triggers exactly one compaction");
    return 0;
}

static int check_compaction_survivors(SvgCtx* c) {
    void* ud = c->backend->user_data;
    const FlColor authored = { 50, 150, 200, 255 };
    const GpuSvgEntry* a_live = gpu_debug_find_svg(ud, 560);
    CHECK(a_live != nullptr, "svg compaction: the re-keyed icon resolves to a live cell after compaction");

    const GpuSvgEntry* b_live = gpu_debug_find_svg(ud, 561);
    CHECK(b_live != nullptr && b_live->size == 32, "svg compaction: the sibling is retained at its current size");

    const int bcw = cell_content_w(b_live);
    const int bch = cell_content_h(b_live);
    const int acw = cell_content_w(a_live);
    const int ach = cell_content_h(a_live);
    void* start = ir_rewind_begin(c->ir_arena);
    svg_emit(c, 561, (FlRect) { 100, 100, (f32)bcw, (f32)bch }, white);
    svg_emit(c, 560, (FlRect) { 100, 300, (f32)acw, (f32)ach }, white);
    CHECK(svg_render_read(c, start), "svg compaction: readback");
    CHECK(pixel_color(pixel_at(&c->buf, 100 + bcw / 2, 100 + bch / 2), authored, 3.0f),
          "svg compaction: the surviving sibling renders its authored fill after compaction");
    CHECK(pixel_color(pixel_at(&c->buf, 100 + acw / 2, 300 + ach / 2), authored, 3.0f),
          "svg compaction: the re-keyed icon renders its authored fill after compaction");
    return 0;
}

int run_svg_compaction_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    SvgCtx c = svg_ctx_create(backend, ir_arena);
    void* ud = backend->user_data;

    const FlVectorImage* img = svg_test_icon();
    CHECK(img != nullptr, "svg compaction: the monochrome icon parses");

    // The sibling is never re-keyed, so it stays live across every overflow.
    backend->prepare_vector(ud, 561, 32, img);
    const GpuSvgEntry* b0 = gpu_debug_find_svg(ud, 561);
    CHECK(b0 != nullptr && b0->size == 32, "svg compaction: the sibling places live at its step");

    if (force_compaction(&c, img) != 0) {
        return 1;
    }
    if (check_compaction_survivors(&c) != 0) {
        return 1;
    }

    svg_ctx_destroy(&c, "svg compaction (atlas-full reclaim) tests");
    return 0;
}

// Part G4: sharpening past 512px. With the R8 atlas at 4096^2 and the mono ladder
// reaching 2048, a >512px dst keys a >512 step and prepare_vector rasters a cell whose content side exceeds
// 512, filling its interior.

static int check_large_cell(const GpuSvgEntry* e) {
    CHECK(e != nullptr, "svg large: the >512px icon resolves to a cell");
    CHECK(e->size == 1024, "svg large: the cell records the >512 quantized raster size");
    const u32 content_w = e->cell.w - 2 * GPU_SVG_ATLAS_PAD;
    const u32 content_h = e->cell.h - 2 * GPU_SVG_ATLAS_PAD;
    CHECK(content_w > 512, "svg large: the cell's content side exceeds 512 (no longer pinned at the old cap)");
    CHECK(content_w == 1024 && content_h == 768,
          "svg large: the aspect-fit content extent matches the 1024 raster size (40x30 -> 1024x768)");
    return 0;
}

static int check_large_atlas(const GpuSvgAtlasReadback* atlas, const GpuSvgEntry* e) {
    CHECK(atlas->width == GPU_SVG_ATLAS_SIZE_MONO,
          "svg large: the mono atlas is the grown 4096 extent (the >512 cell needs the larger texture)");
    const u8 center = svg_texel(atlas, e->cell.x + e->cell.w / 2, e->cell.y + e->cell.h / 2);
    CHECK(center >= SVG_FULL, "svg large: the >512 cell's interior is fully covered (the larger raster actually ran)");
    const u8 corner = svg_texel(atlas, e->cell.x + GPU_SVG_ATLAS_PAD, e->cell.y + GPU_SVG_ATLAS_PAD);
    CHECK(corner >= SVG_FULL, "svg large: coverage runs to the content edge of the >512 cell");
    return 0;
}

int run_svg_large_raster_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    SvgCtx c = svg_ctx_create(backend, ir_arena);
    void* ud = backend->user_data;

    const FlVectorImage* img = svg_test_icon(); // 40x30 monochrome solid fill
    CHECK(img != nullptr, "svg large: the monochrome icon parses");

    // An 800x600 physical dst: 800 is the larger side, so it snaps up to ladder step 1024.
    const u16 q = svg_raster_size_for_dst(800.0f, 600.0f, FlVectorClass_Monochrome);
    CHECK(q == 1024, "svg large: an 800px mono dst keys the 1024 step (past the old 512 cap)");

    const FlImageHandle handle = 570; // distinct from the other Part G handles
    backend->prepare_vector(ud, handle, q, img);

    // The cell rasters at the quantized size: 40x30 fit to a 1024 larger side -> 1024 x 768 content plus the
    // padding ring.
    const GpuSvgEntry* e = gpu_debug_find_svg(ud, handle);
    if (check_large_cell(e) != 0) {
        return 1;
    }

    GpuSvgAtlasReadback atlas;
    CHECK(gpu_debug_read_svg_atlas(ud, c.arena, &atlas), "svg large: atlas readback");
    if (check_large_atlas(&atlas, e) != 0) {
        return 1;
    }

    svg_ctx_destroy(&c, "svg large-raster (sharpen past 512px) tests");
    return 0;
}
