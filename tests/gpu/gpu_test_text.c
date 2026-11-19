// GPU smoke test: cross-backend glyph raster, text pixel and text atlas tests.

#include "gpu_test_common.h"

// Cross-backend glyph + text cases: the glyph raster / text-pixel / atlas-regen suites run on both
// backends through the neutral seam (prepare_glyph, the glyph atlas readback, scale_changed, and rendered text
// runs). Every expectation is a property both backends satisfy, checked with the same tolerance, with no
// per-backend golden fork.

// Part D: glyph rasterization. Light-hinted outlines from the embedded test fonts go through the
// shared font layer and the backend's prepare_glyph seam into R8 atlas cells, verified by atlas readback.
// The fonts (glyph_test_fonts.h) carry 'A' (a solid 0.6 x 0.7 em square) and 'o' (a ring whose counter winds
// opposite), once as TrueType (quadratics) and once as CFF (cubics), so both curve flavors run the same checks.

static u8 glyph_texel(const GpuGlyphAtlasReadback* atlas, u32 x, u32 y) {
    return atlas->texels[(size_t)y * atlas->width + x];
}

// Coverage thresholds: "full" tolerates the shader's chordwise approximation of curved edges, "zero"
// tolerates one LSB of float noise on opposite-wound cancellation.
enum { GLYPH_FULL = 250, GLYPH_ZERO = 1 };

enum { GLYPH_SIZE = 32 * 4 }; // quarter-px key for 32px, the loaded size

typedef struct GlyphCtx {
    FlRenderBackend* backend;
    FlFontCache* fc;
    FlArena* arena;
    FlFontId font;
    u32 glyph_a;
    u32 glyph_o;
    GpuGlyphAtlasReadback atlas;
} GlyphCtx;

typedef struct FontCase {
    FlFontCache* fc;
    FlFontId font;
    const char* what;
} FontCase;

static bool prepare_outline(GlyphCtx* c, u32 glyph_index) {
    FlGlyphOutline outline;
    if (!font_cache_get_glyph_outline(c->fc, c->font, GLYPH_SIZE, glyph_index, c->arena, &outline)) {
        return false;
    }
    c->backend->prepare_glyph(c->backend->user_data, c->font, GLYPH_SIZE, glyph_index, &outline);
    return true;
}

static const GpuGlyphEntry* find_glyph(GlyphCtx* c, u32 glyph_index) {
    return gpu_debug_find_glyph(c->backend->user_data, c->font, GLYPH_SIZE, glyph_index);
}

static int prepare_glyph_pair(GlyphCtx* c) {
    c->glyph_a = font_cache_get_glyph_index(c->fc, c->font, 'A');
    c->glyph_o = font_cache_get_glyph_index(c->fc, c->font, 'o');
    CHECK(c->glyph_a != 0 && c->glyph_o != 0, "glyph raster: 'A' and 'o' must resolve to glyph indices");

    CHECK(prepare_outline(c, c->glyph_a), "glyph raster: 'A' outline must load");
    CHECK(prepare_outline(c, c->glyph_o), "glyph raster: 'o' outline must load");

    // .notdef (glyph 0) is empty in the test fonts: the metrics-only path (an entry, no cell, no dispatch).
    CHECK(prepare_outline(c, 0), "glyph raster: the empty .notdef outline must load");
    const GpuGlyphEntry* notdef = find_glyph(c, 0);
    CHECK(notdef != nullptr && notdef->cell.w == 0 && notdef->cell.h == 0,
          "glyph raster: an empty glyph takes a metrics-only entry");
    CHECK(notdef->advance > 10.0f, "glyph raster: the metrics-only entry still carries its advance");

    CHECK(gpu_debug_read_glyph_atlas(c->backend->user_data, c->arena, &c->atlas), "glyph raster: atlas readback");
    return 0;
}

static int check_square_entry(const GpuGlyphEntry* a) {
    // 'A', the solid square: design 0.6 x 0.7 em -> roughly 19 x 22 px at 32 px (hinting may nudge it).
    CHECK(a != nullptr, "glyph raster: the square's entry must resolve");
    CHECK(a->cell.w >= 12 && a->cell.w <= 32 && a->cell.h >= 14 && a->cell.h <= 36,
          "glyph raster: the square's cell extent lands near its design size");
    CHECK(a->advance > 20.0f && a->advance < 30.0f, "glyph raster: the entry carries the advance");
    return 0;
}

static int check_square_padding(const GpuGlyphAtlasReadback* atlas, const GpuGlyphEntry* a) {
    // Zero coverage on all four padding borders (the cell is written wall to wall, ring included).
    for (u32 i = 0; i < a->cell.w; i++) {
        CHECK(glyph_texel(atlas, a->cell.x + i, a->cell.y) <= GLYPH_ZERO
                  && glyph_texel(atlas, a->cell.x + i, a->cell.y + a->cell.h - 1) <= GLYPH_ZERO,
              "glyph raster: the padding rows must be empty");
    }
    for (u32 i = 0; i < a->cell.h; i++) {
        CHECK(glyph_texel(atlas, a->cell.x, a->cell.y + i) <= GLYPH_ZERO
                  && glyph_texel(atlas, a->cell.x + a->cell.w - 1, a->cell.y + i) <= GLYPH_ZERO,
              "glyph raster: the padding columns must be empty");
    }
    return 0;
}

static int check_square_interior(const GpuGlyphAtlasReadback* atlas, const GpuGlyphEntry* a) {
    // A hinted box edge may land on a texel boundary, so the square's ramp need not have a partial step.
    // The ring below demands one from its curves.
    u32 ax = a->cell.x + a->cell.w / 2;
    u32 ay = a->cell.y + a->cell.h / 2;
    CHECK(glyph_texel(atlas, ax, ay) >= GLYPH_FULL, "glyph raster: the square interior is fully covered");
    u8 high_water = 0;
    for (u32 x = a->cell.x; x <= ax; x++) {
        u8 t = glyph_texel(atlas, x, ay);
        CHECK(t + GLYPH_ZERO >= high_water, "glyph raster: coverage must rise monotonically into the square");
        high_water = t > high_water ? t : high_water;
    }
    return 0;
}

static int check_square_reprepare(GlyphCtx* c, GpuAtlasCell cell_before) {
    CHECK(prepare_outline(c, c->glyph_a), "glyph raster: 'A' outline must re-load");
    const GpuGlyphEntry* a = find_glyph(c, c->glyph_a);
    CHECK(a != nullptr && a->cell.x == cell_before.x && a->cell.y == cell_before.y,
          "glyph raster: re-preparing a glyph keeps its cell");
    return 0;
}

static int check_ring_entry(const GpuGlyphAtlasReadback* atlas, const GpuGlyphEntry* o, u32* mid_ring_x, u32* oy) {
    // 'o', the ring: outer r 0.28 em, counter r 0.14 em. At mid-height the band spans the leftmost
    // quarter of the glyph width, so mid-ring sits an eighth in from the left bbox edge.
    CHECK(o != nullptr && o->cell.w >= 12 && o->cell.h >= 12, "glyph raster: the ring's entry must resolve");
    u32 ox = o->cell.x + o->cell.w / 2;
    *oy = o->cell.y + o->cell.h / 2;
    CHECK(glyph_texel(atlas, ox, *oy) <= GLYPH_ZERO,
          "glyph raster: the opposite-wound counter must cancel to zero coverage");
    u32 inner_w = o->cell.w - 2 * GPU_GLYPH_ATLAS_PAD;
    *mid_ring_x = o->cell.x + GPU_GLYPH_ATLAS_PAD + inner_w / 8;
    CHECK(glyph_texel(atlas, *mid_ring_x, *oy) >= GLYPH_FULL, "glyph raster: mid-ring coverage is full");
    CHECK(glyph_texel(atlas, o->cell.x + 1, o->cell.y + 1) <= GLYPH_ZERO,
          "glyph raster: the cell corner outside the round glyph stays empty");
    return 0;
}

static int check_ring_ramp(const GpuGlyphAtlasReadback* atlas, const GpuGlyphEntry* o, u32 mid_ring_x, u32 oy) {
    bool saw_partial = false;
    u8 high_water = 0;
    for (u32 x = o->cell.x; x <= mid_ring_x; x++) {
        u8 t = glyph_texel(atlas, x, oy);
        CHECK(t + GLYPH_ZERO >= high_water, "glyph raster: coverage must rise monotonically into the ring");
        saw_partial |= t > 20 && t < 235;
        high_water = t > high_water ? t : high_water;
    }
    CHECK(saw_partial, "glyph raster: a curved edge must produce a genuine AA-ramp texel");
    return 0;
}

static int check_glyph_square(GlyphCtx* c) {
    const GpuGlyphEntry* a = find_glyph(c, c->glyph_a);
    if (check_square_entry(a) != 0) {
        return 1;
    }
    if (check_square_padding(&c->atlas, a) != 0) {
        return 1;
    }
    if (check_square_interior(&c->atlas, a) != 0) {
        return 1;
    }
    return check_square_reprepare(c, a->cell);
}

static int check_glyph_ring(GlyphCtx* c) {
    const GpuGlyphEntry* o = find_glyph(c, c->glyph_o);
    u32 mid_ring_x = 0;
    u32 oy = 0;
    if (check_ring_entry(&c->atlas, o, &mid_ring_x, &oy) != 0) {
        return 1;
    }
    return check_ring_ramp(&c->atlas, o, mid_ring_x, oy);
}

// The whole battery for one font flavor at one size, against one atlas readback snapshot.
static int run_glyph_font_checks(FlRenderBackend* backend, FlArena* arena, const FontCase* font_case) {
    GlyphCtx c = { .backend = backend, .fc = font_case->fc, .arena = arena, .font = font_case->font };
    if (prepare_glyph_pair(&c) != 0) {
        return 1;
    }
    if (check_glyph_square(&c) != 0) {
        return 1;
    }
    if (check_glyph_ring(&c) != 0) {
        return 1;
    }

    printf("gpu_smoke_test: glyph raster (%s) checks PASS\n", font_case->what);
    return 0;
}

int run_glyph_raster_tests(FlRenderBackend* backend) {
    FlSurface* surface = backend->create_surface(backend->user_data, 1920, 1080);
    FlArena* arena = arena_new();
    FlFontCache* fc = font_cache_new(arena);

    CHECK(backend->prepare_glyph != nullptr, "glyph raster: the GPU backend must expose prepare_glyph");

    FlFontId ttf = font_cache_load_font_memory(fc, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    FlFontId cff = font_cache_load_font_memory(fc, k_glyph_test_cff, k_glyph_test_cff_size, 32);
    CHECK(ttf != FONT_ID_INVALID && cff != FONT_ID_INVALID, "glyph raster: the embedded fonts load from memory");

    const FontCase ttf_case = { fc, ttf, "ttf quadratic" };
    const FontCase cff_case = { fc, cff, "cff cubic" };
    if (run_glyph_font_checks(backend, arena, &ttf_case) != 0) {
        return 1;
    }
    if (run_glyph_font_checks(backend, arena, &cff_case) != 0) {
        return 1;
    }

    const GpuGlyphEntry* ea
        = gpu_debug_find_glyph(backend->user_data, ttf, GLYPH_SIZE, font_cache_get_glyph_index(fc, ttf, 'A'));
    const GpuGlyphEntry* eb
        = gpu_debug_find_glyph(backend->user_data, cff, GLYPH_SIZE, font_cache_get_glyph_index(fc, cff, 'A'));
    CHECK(ea != nullptr && eb != nullptr && !cells_overlap(ea->cell, eb->cell),
          "glyph raster: both fonts pack into one atlas in disjoint cells");

    font_cache_destroy(fc);
    fl_arena_destroy(arena);
    printf("gpu_smoke_test: glyph raster tests PASS\n");
    backend->destroy_surface(backend->user_data, surface);
    return 0;
}

// Part E: text drawing end to end. Glyphs are prepared through the seam, then an FlCmd_TextRun is rendered and read
// back. 'A' in the embedded test font is a solid 0.6 x 0.7 em square (glyph_test_fonts.h), so a covered cell
// is a solid block of the run's color and the rows around it stay transparent. The glyph is keyed by its
// glyph index, the key the translator looks up.

typedef struct TextCtx {
    FlRenderBackend* backend;
    FlSurface* surface;
    FlArena* ir_arena;
    FlArena* arena;
    FlFontCache* fc;
    FlFontId font;
    FlPixelReadback buf;
} TextCtx;

static bool glyph_cell_has_area(const GpuGlyphEntry* e) {
    return e != nullptr && e->cell.w * e->cell.h != 0;
}

static bool text_run_paints(TextCtx* c, FlFontId font, u16 size, u32 glyph_index) {
    const FlColor color = { 230, 80, 200, 255 };
    const f32 pen_x = 600.0f;
    const f32 pen_y = 300.0f;
    FlGlyph glyph = { .x = pen_x, .y = pen_y, .glyph_index = glyph_index };
    void* start = ir_rewind_begin(c->ir_arena);
    ir_text_run(c->ir_arena, font, size, 1.0f, color, &glyph, 1);
    FlRenderFrame frame = test_frame();
    ir_frame_render(c->backend, c->surface, &frame, c->ir_arena, start);

    const GpuGlyphEntry* e = gpu_debug_find_glyph(c->backend->user_data, font, size, glyph_index);
    FlPixelReadback buf;
    if (!glyph_cell_has_area(e) || !render_seam_read_pixels(c->backend, c->surface, &buf)) {
        return false;
    }
    const int cx = (int)(pen_x + e->left) + (int)e->cell.w / 2;
    const int cy = (int)(pen_y - e->top) + (int)e->cell.h / 2;
    return pixel_color(pixel_at(&buf, cx, cy), color, 3.0f);
}

static int prepare_square_glyph(TextCtx* c, u32* idx_out, const GpuGlyphEntry** entry_out) {
    void* ud = c->backend->user_data;
    const u16 size = GLYPH_SIZE;
    u32 idx = font_cache_get_glyph_index(c->fc, c->font, 'A');
    FlGlyphOutline outline;
    CHECK(idx != 0 && font_cache_get_glyph_outline(c->fc, c->font, size, idx, c->arena, &outline),
          "text pixels: the square glyph's outline loads");
    c->backend->prepare_glyph(ud, c->font, size, idx, &outline);
    const GpuGlyphEntry* e = gpu_debug_find_glyph(ud, c->font, size, idx);
    CHECK(e != nullptr && e->cell.w > 0 && e->cell.h > 0, "text pixels: the glyph prepared into a real cell");
    *idx_out = idx;
    *entry_out = e;
    return 0;
}

static int check_text_run_pixels(TextCtx* c, u32 idx, const GpuGlyphEntry* e) {
    // Two copies of the square in one run, far enough apart to leave a transparent gap between the cells.
    const FlColor text_col = { 230, 80, 200, 255 }; // a vivid tint, clearly not white, which proves colorization
    const f32 pen_y = 300.0f;
    const f32 pen_x0 = 200.0f;
    const f32 pen_x1 = 260.0f;
    FlGlyph glyphs[2] = {
        { .x = pen_x0, .y = pen_y, .glyph_index = idx },
        { .x = pen_x1, .y = pen_y, .glyph_index = idx },
    };

    void* start = ir_rewind_begin(c->ir_arena);
    ir_text_run(c->ir_arena, c->font, GLYPH_SIZE, 1.0f, text_col, glyphs, 2);
    FlRenderFrame frame = test_frame();
    ir_frame_render(c->backend, c->surface, &frame, c->ir_arena, start);
    CHECK(render_seam_read_pixels(c->backend, c->surface, &c->buf), "text pixels: readback");

    // The first square's painted rect: pen offset by the cell's left/top metrics, padded-cell sized.
    int x0 = (int)(pen_x0 + e->left);
    int y0 = (int)(pen_y - e->top);
    int cw = (int)e->cell.w;
    int ch = (int)e->cell.h;
    int cx = x0 + cw / 2;
    int cy = y0 + ch / 2;

    CHECK(pixel_color(pixel_at(&c->buf, cx, cy), text_col, 3.0f),
          "text pixels: the square interior is the run's color at full coverage");

    // Transparent a few pixels above the cell top and below the baseline (the square sits on the baseline).
    CHECK(pixel_transparent(pixel_at(&c->buf, cx, y0 - 3)), "text pixels: the row above the glyph stays transparent");
    CHECK(pixel_transparent(pixel_at(&c->buf, cx, y0 + ch + 3)),
          "text pixels: the row below the glyph (under the baseline) stays transparent");

    int gap_x = x0 + cw + 3;
    CHECK(gap_x < (int)(pen_x1 + e->left), "text pixels: the two squares leave a gap (test setup)");
    CHECK(pixel_transparent(pixel_at(&c->buf, gap_x, cy)),
          "text pixels: an empty column between glyphs does not darken");

    CHECK(pixel_color(pixel_at(&c->buf, (int)(pen_x1 + e->left) + cw / 2, cy), text_col, 3.0f),
          "text pixels: the second glyph in the run also paints its square");
    return 0;
}

static int check_fallback_runs(TextCtx* c) {
    // A codepoint the font lacks shapes to the fallback face, and a run in that face paints its glyph. 'B' is
    // absent from the embedded test font and present in the fallback one. These legs drive render()'s own
    // prepare pass (set_font_cache, no host-side prepare_glyph).
    const u16 size = GLYPH_SIZE;
    FlFontId fb_font = font_cache_load_font_memory(c->fc, k_fallback_test_ttf, k_fallback_test_ttf_size, 32);
    CHECK(fb_font != FONT_ID_INVALID, "text pixels: the fallback font loads");
    CHECK(font_cache_get_glyph_index(c->fc, c->font, 'B') == 0, "text pixels: 'B' is absent from the primary face");
    font_cache_set_fallback_font(c->fc, fb_font);
    c->backend->set_font_cache(c->backend->user_data, c->fc);

    const FlShapedRun shaped = font_cache_shape_run(c->fc, c->font, size, FL_S("B"));
    CHECK(shaped.count == 1 && shaped.glyphs[0].font == fb_font && shaped.glyphs[0].glyph_index != 0,
          "text pixels: a fallback-only codepoint shapes to the fallback face");
    CHECK(text_run_paints(c, fb_font, size, shaped.glyphs[0].glyph_index),
          "text pixels: the fallback glyph paints its square in the run's color");

    FlFontId builtin = font_cache_load_default_font(c->fc, 1);
    CHECK(builtin != FONT_ID_INVALID, "text pixels: the built-in font loads");
    CHECK(text_run_paints(c, builtin, size, 'B'), "text pixels: a built-in font run paints the fallback face's glyph");
    return 0;
}

int run_text_pixel_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    TextCtx c = {
        .backend = backend,
        .surface = backend->create_surface(backend->user_data, 1920, 1080),
        .ir_arena = ir_arena,
        .arena = arena_new(),
    };
    c.fc = font_cache_new(c.arena);
    c.font = font_cache_load_font_memory(c.fc, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    CHECK(c.font != FONT_ID_INVALID, "text pixels: the embedded font loads");

    u32 idx = 0;
    const GpuGlyphEntry* e = nullptr;
    if (prepare_square_glyph(&c, &idx, &e) != 0) {
        return 1;
    }
    if (check_text_run_pixels(&c, idx, e) != 0) {
        return 1;
    }
    if (check_fallback_runs(&c) != 0) {
        return 1;
    }

    backend->set_font_cache(backend->user_data, nullptr); // the cache is about to die; the pointer lives on the backend
    font_cache_destroy(c.fc);
    fl_arena_destroy(c.arena);
    printf("gpu_smoke_test: text pixel tests PASS\n");
    backend->destroy_surface(backend->user_data, c.surface);
    return 0;
}

// Part F: glyph-atlas regeneration. A UI-scale change (the scale_changed seam) regenerates the atlas
// wholesale at the new pixel sizes, a resolution change alone does not (scale, not window size, is
// the trigger), and a full atlas repacks wholesale rather than evicting per glyph. Resolution and scale are
// restored to the create state on the way out, because the frame-lifecycle test that follows assumes it.

typedef struct AtlasCtx {
    FlRenderBackend* backend;
    FlSurface* surface;
    FlArena* ir_arena;
    FlArena* arena;
    FlFontCache* fc;
    FlFontId font;
    u32 idx;
    FlGlyphOutline o32;
    u32 before_w;
    u32 before_h;
    FlGlyphOutline big;
} AtlasCtx;

static const GpuGlyphEntry* atlas_find(AtlasCtx* c, u16 size, u32 glyph_index) {
    return gpu_debug_find_glyph(c->backend->user_data, c->font, size, glyph_index);
}

static int check_resize_keeps_glyphs(AtlasCtx* c) {
    void* ud = c->backend->user_data;
    // A resolution change alone does not regenerate: scale, not window size, is the trigger
    CHECK(font_cache_get_glyph_outline(c->fc, c->font, 32 * 4, c->idx, c->arena, &c->o32),
          "text atlas: the size-32 outline loads");
    c->backend->prepare_glyph(ud, c->font, 32 * 4, c->idx, &c->o32);
    const GpuGlyphEntry* before = atlas_find(c, 32 * 4, c->idx);
    CHECK(before != nullptr, "text atlas: the glyph prepares at the create resolution");
    c->before_w = before->cell.w;
    c->before_h = before->cell.h;

    // Resizing the window only rebuilds the framebuffer; the atlas is untouched because the UI scale is an
    // absolute value the core hands in, not a function of height.
    render_seam_change_resolution(c->backend, c->surface, 2560, 1080);
    CHECK(atlas_find(c, 32 * 4, c->idx) != nullptr,
          "text atlas: a width-only resize keeps the glyphs (no scale change)");
    render_seam_change_resolution(c->backend, c->surface, 2560, 720);
    CHECK(atlas_find(c, 32 * 4, c->idx) != nullptr,
          "text atlas: a height resize alone no longer regenerates (scale, not window size, is the trigger)");
    return 0;
}

static int check_scale_regeneration(AtlasCtx* c) {
    void* ud = c->backend->user_data;
    // Wholesale regeneration on a UI-scale change, via the scale_changed seam
    CHECK(c->backend->scale_changed != nullptr, "text atlas: the GPU backend exposes the scale_changed seam");

    // An unchanged scale is a no-op (guarded on last_ui_scale, which the create set to the 1.0 default).
    c->backend->scale_changed(ud, 1.0f);
    CHECK(atlas_find(c, 32 * 4, c->idx) != nullptr,
          "text atlas: scale_changed at the same scale is a no-op (glyphs survive)");

    c->backend->scale_changed(ud, 2.0f);
    CHECK(atlas_find(c, 32 * 4, c->idx) == nullptr,
          "text atlas: a UI-scale change forgets every glyph (wholesale regen)");

    FlGlyphOutline o48;
    CHECK(font_cache_get_glyph_outline(c->fc, c->font, 48 * 4, c->idx, c->arena, &o48),
          "text atlas: the size-48 outline loads");
    c->backend->prepare_glyph(ud, c->font, 48 * 4, c->idx, &o48);
    const GpuGlyphEntry* after = atlas_find(c, 48 * 4, c->idx);
    CHECK(after != nullptr && after->cell.x == 0 && after->cell.y == 0,
          "text atlas: the new-size glyph repacks from the origin");
    CHECK(after->cell.w > c->before_w && after->cell.h > c->before_h,
          "text atlas: the glyph re-rasterizes larger at the new size (crisp, not upscaled)");
    return 0;
}

static int check_atlas_overflow(AtlasCtx* c) {
    void* ud = c->backend->user_data;
    // Atlas-full: a wholesale repack at the frame boundary, not per-glyph eviction
    // One very large glyph at many distinct keys fills the atlas. The placement that overflows flags a deferred
    // wipe (gpu_glyph_atlas_on_overflow): the backend never wipes inline (that would blank glyphs already placed
    // this frame), so the wholesale repack is honored at the next render() boundary. The glyph is sized large so
    // the GPU_GLYPH_ATLAS_SIZE^2 skyline overflows in a small, bounded loop; the entry-table cap
    // (GPU_GLYPH_ATLAS_MAX_ENTRIES) bounds it regardless, so the loop always terminates on a rejected placement.
    const u16 big_size = 512 * 4; // quarter-px key for 512px
    CHECK(font_cache_get_glyph_outline(c->fc, c->font, big_size, c->idx, c->arena, &c->big),
          "text atlas: the large outline loads");
    c->backend->prepare_glyph(ud, c->font, big_size, 5000, &c->big);
    CHECK(atlas_find(c, big_size, 5000) != nullptr, "text atlas: the stress sentinel places");
    bool overflowed = false;
    for (u32 i = 0; i < GPU_GLYPH_ATLAS_MAX_ENTRIES && !overflowed; i++) {
        c->backend->prepare_glyph(ud, c->font, big_size, 6000 + i, &c->big);
        // A rejected placement (atlas full) is absent and flags the deferred wipe.
        overflowed = atlas_find(c, big_size, 6000 + i) == nullptr;
    }
    CHECK(overflowed, "text atlas: enough big glyphs overflow the atlas");
    CHECK(atlas_find(c, big_size, 5000) != nullptr,
          "text atlas: the overflow defers the wipe (the sentinel is still resident before the frame boundary)");

    // An empty frame is enough; the wipe runs at render()'s tail regardless of content.
    void* ovf_start = ir_rewind_begin(c->ir_arena);
    FlRenderFrame ovf_frame = test_frame();
    ir_frame_render(c->backend, c->surface, &ovf_frame, c->ir_arena, ovf_start);
    CHECK(atlas_find(c, big_size, 5000) == nullptr,
          "text atlas: a full atlas repacks wholesale at the frame boundary (the sentinel is forgotten)");

    c->backend->prepare_glyph(ud, c->font, big_size, 7777, &c->big);
    CHECK(atlas_find(c, big_size, 7777) != nullptr,
          "text atlas: the backend keeps placing glyphs after a wholesale repack");
    return 0;
}

static int check_text_after_repack(AtlasCtx* c) {
    c->backend->prepare_glyph(c->backend->user_data, c->font, 32 * 4, c->idx, &c->o32);
    const GpuGlyphEntry* e = atlas_find(c, 32 * 4, c->idx);
    CHECK(e != nullptr && e->cell.w > 0, "text atlas: the closing glyph prepares");
    FlGlyph one = { .x = 300.0f, .y = 300.0f, .glyph_index = c->idx };
    void* start = ir_rewind_begin(c->ir_arena);
    ir_text_run(c->ir_arena, c->font, 32 * 4, 1.0f, white, &one, 1);
    FlRenderFrame frame = test_frame();
    ir_frame_render(c->backend, c->surface, &frame, c->ir_arena, start);
    FlPixelReadback buf;
    CHECK(render_seam_read_pixels(c->backend, c->surface, &buf), "text atlas: readback after the stress scene");
    int cx = (int)(300.0f + e->left) + (int)e->cell.w / 2;
    int cy = (int)(300.0f - e->top) + (int)e->cell.h / 2;
    CHECK(pixel_color(pixel_at(&buf, cx, cy), white, 3.0f),
          "text atlas: text still rasterizes (the white run) after a wholesale repack");
    return 0;
}

int run_text_atlas_tests(FlRenderBackend* backend, FlArena* ir_arena) {
    AtlasCtx c = {
        .backend = backend,
        .surface = backend->create_surface(backend->user_data, 1920, 1080),
        .ir_arena = ir_arena,
        .arena = arena_new(),
    };
    c.fc = font_cache_new(c.arena);
    c.font = font_cache_load_font_memory(c.fc, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    CHECK(c.font != FONT_ID_INVALID, "text atlas: the embedded font loads");
    c.idx = font_cache_get_glyph_index(c.fc, c.font, 'A');
    CHECK(c.idx != 0, "text atlas: 'A' resolves to a glyph index");

    if (check_resize_keeps_glyphs(&c) != 0) {
        return 1;
    }
    if (check_scale_regeneration(&c) != 0) {
        return 1;
    }
    if (check_atlas_overflow(&c) != 0) {
        return 1;
    }
    if (check_text_after_repack(&c) != 0) {
        return 1;
    }

    // Restore the create resolution and scale for the tests that follow.
    render_seam_change_resolution(backend, c.surface, 1920, 1080);
    backend->scale_changed(backend->user_data, 1.0f);

    font_cache_destroy(c.fc);
    fl_arena_destroy(c.arena);
    printf("gpu_smoke_test: text atlas (regen + repack) tests PASS\n");
    backend->destroy_surface(backend->user_data, c.surface);
    return 0;
}
