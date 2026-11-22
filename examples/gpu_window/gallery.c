#include "gallery.h"
#include <math.h>

// Palette + gallery layout. Cells are a fixed grid anchored top-left.

static const FlColor k_bg = { 28, 30, 38, 255 };
static const FlColor k_panel = { 40, 44, 56, 255 };
static const FlColor k_panel_border = { 64, 70, 88, 255 };
static const FlColor k_accent = { 90, 160, 220, 255 };
static const FlColor k_warn = { 220, 140, 60, 255 };
static const FlColor k_green = { 120, 200, 120, 255 };
static const FlColor k_ink = { 226, 230, 240, 255 };
static const FlColor k_dim = { 150, 158, 176, 255 };
static const FlColor k_white = { 255, 255, 255, 255 };

#define MARGIN 16.0f
#define TITLE_H 40.0f
#define CELL_W 250.0f
#define CELL_H 160.0f
#define CELL_GAP 12.0f
#define LABEL_SIZE 14
#define TITLE_SIZE 24
#define GRID_COLS 3

static FlRect cell_rect(int col, int row) {
    return (FlRect) {
        MARGIN + (f32)col * (CELL_W + CELL_GAP),
        MARGIN + TITLE_H + (f32)row * (CELL_H + CELL_GAP),
        CELL_W,
        CELL_H,
    };
}

static FlRect square_at(FlRect cell, f32 dx, f32 dy, f32 size) {
    return (FlRect) { cell.x + dx, cell.y + dy, size, size };
}

static FlVec2 point_at(FlRect cell, f32 dx, f32 dy) {
    return (FlVec2) { cell.x + dx, cell.y + dy };
}

static FlPolyPoint view_point(View v, f32 x, f32 y) {
    return (FlPolyPoint) { v_x(v, x), v_y(v, y), 1.0f };
}

static FlImageSource static_image(FlImageHandle handle) {
    return (FlImageSource) { .kind = FlImageKind_Static, .handle = handle };
}

// A faint panel + label backing every gallery cell.
static void draw_cell_chrome(const Painter* p, FlRect cell, FlString label) {
    scene_fill_solid(p, cell, radius_all(8), k_panel);
    scene_border(p, cell, (BorderSpec) { 1, 1, 1, 1, radius_all(8), k_panel_border });
    scene_text(p, (TextSpec) { LABEL_SIZE, k_dim, label, point_at(cell, 12.0f, cell.h - 12.0f) });
}

static void cell_fill_gradient(const Gallery* g, FlRect c) {
    const Painter* p = &g->painter;
    draw_cell_chrome(p, c, FL_S("fill + gradient"));
    scene_fill_solid(p, square_at(c, 16, 16, 90), radius_all(14), k_accent);
    const FlColor tl = { 220, 80, 80, 255 };
    const FlColor tr = { 80, 220, 120, 255 };
    const FlColor bl = { 80, 120, 220, 255 };
    const FlColor br = { 220, 200, 80, 255 };
    scene_fill_rect(p, square_at(c, 128, 16, 90), radius_all(0), (FlColor[4]) { tl, tr, bl, br });
}

static void cell_borders(const Gallery* g, FlRect c) {
    const Painter* p = &g->painter;
    draw_cell_chrome(p, c, FL_S("borders"));
    FlRect a = square_at(c, 16, 16, 90);
    scene_fill_solid(p, a, radius_all(12), k_panel);
    scene_border(p, a, (BorderSpec) { 3, 3, 3, 3, radius_all(12), k_accent });
    FlRect b = square_at(c, 128, 16, 90);
    scene_fill_solid(p, b, radius_all(0), k_panel);
    scene_border(p, b, (BorderSpec) { 8, 2, 2, 8, radius_all(0), k_warn }); // thick left/bottom, thin right/top
}

static void cell_clip(const Gallery* g, FlRect c) {
    const Painter* p = &g->painter;
    draw_cell_chrome(p, c, FL_S("clip"));
    ir_clip_push(p->arena, v_rect(p->view, (FlRect) { c.x + 16, c.y + 16, 120, 70 }));
    scene_fill_solid(p, (FlRect) { c.x + 60, c.y + 8, 140, 110 }, radius_all(0), k_green);
    ir_clip_pop(p->arena);
}

static void cell_circle_diamond(const Gallery* g, FlRect c) {
    const Painter* p = &g->painter;
    draw_cell_chrome(p, c, FL_S("circle + diamond"));
    scene_circle(p, point_at(c, 62, 58), 36, (ShapeStyle) { k_accent, k_white, 3, nullptr });
    scene_diamond(p, point_at(c, 168, 58), (FlVec2) { 36, 46 }, (ShapeStyle) { k_warn, k_white, 3, nullptr });
}

static void cell_arcs(const Gallery* g, FlRect c) {
    const Painter* p = &g->painter;
    draw_cell_chrome(p, c, FL_S("arc / ring / sector"));
    f32 cy = c.y + 58;
    scene_arc(p, (ArcSpec) { { c.x + 50, cy }, 34, 7, 0.0f, 6.2831853f, k_accent, 0 });               // full ring
    scene_arc(p, (ArcSpec) { { c.x + 125, cy }, 34, 16, 0.2f, 4.2f, k_green, FL_IR_ARC_ROUND_CAPS }); // capped sweep
    scene_arc(p, (ArcSpec) { { c.x + 200, cy }, 34, 40, 0.0f, 2.3f, k_warn, 0 });                     // filled sector
}

enum { WAVE_POINTS = 48 };

static void draw_stroked_wave(const Painter* p, f32 x0, f32 mid) {
    f32 span = CELL_W - 28.0f;
    FlPolyPoint* stroke = scene_polyline(p, (PolylineSpec) { k_accent, k_accent, 2.5f, mid, 0, WAVE_POINTS });
    for_count(i, WAVE_POINTS) {
        f32 t = (f32)i / (f32)(WAVE_POINTS - 1);
        stroke[i] = view_point(p->view, x0 + t * span, mid - 22.0f * sinf(t * 6.2831853f));
    }
}

static void draw_filled_wave(const Painter* p, f32 x0, f32 base) {
    f32 span = CELL_W - 28.0f;
    FlPolyPoint* fill
        = scene_polyline(p, (PolylineSpec) { k_green, k_green, 1.5f, base, FlPolylineFlags_filled, WAVE_POINTS });
    for_count(i, WAVE_POINTS) {
        f32 t = (f32)i / (f32)(WAVE_POINTS - 1);
        fill[i] = view_point(p->view, x0 + t * span, base - 30.0f * (0.5f + 0.5f * sinf(t * 9.4f)));
    }
}

static void cell_polylines(const Gallery* g, FlRect c) {
    const Painter* p = &g->painter;
    draw_cell_chrome(p, c, FL_S("polyline (stroked + filled)"));
    draw_stroked_wave(p, c.x + 14, c.y + 50);
    draw_filled_wave(p, c.x + 14, c.y + 124);
}

static void cell_bars(const Gallery* g, FlRect c) {
    const Painter* p = &g->painter;
    draw_cell_chrome(p, c, FL_S("bars (batched)"));
    enum { N = 14 };
    f32 base = c.y + 128; // bars grow upward from a baseline near the cell's bottom
    f32 bw = 12.0f;
    f32 gap = 4.0f;
    f32 x0 = c.x + 16;
    FlBar* bars = scene_bars(p, base, k_accent, N);
    for_count(i, N) {
        f32 t = (f32)i / (f32)(N - 1);
        f32 h = 20.0f + 70.0f * (0.5f + 0.5f * sinf(t * 7.0f));
        bars[i] = (FlBar) { v_x(p->view, x0 + (f32)i * (bw + gap)), v_len(p->view, bw), v_len(p->view, h) };
    }
}

static void cell_images(const Gallery* g, FlRect c) {
    const Painter* p = &g->painter;
    draw_cell_chrome(p, c, FL_S("image (color + tint)"));
    FlImageSource src = static_image(g->image);
    scene_image(p, src, square_at(c, 16, 16, 90), (ImageStyle) { k_white, FlImageFilter_Nearest });
    scene_image(p, src, square_at(c, 128, 16, 90), (ImageStyle) { { 220, 150, 90, 255 }, FlImageFilter_Linear });
}

static void cell_text(const Gallery* g, FlRect c) {
    const Painter* p = &g->painter;
    draw_cell_chrome(p, c, FL_S("text (Roboto)"));
    scene_text(p, (TextSpec) { 22, k_ink, FL_S("Sphinx of black quartz,"), point_at(c, 14, 38) });
    scene_text(p, (TextSpec) { 16, k_dim, FL_S("judge my vow. 0123456789"), point_at(c, 14, 66) });
    scene_text(p, (TextSpec) { 14, k_green, FL_S("The quick brown fox jumps"), point_at(c, 14, 92) });
}

static void cell_svg_mono(const Gallery* g, FlRect c) {
    const Painter* p = &g->painter;
    draw_cell_chrome(p, c, FL_S("svg (mono, tinted)"));
    // White (identity) tint draws the icon's authored fill; any other tint recolors the coverage.
    scene_svg(p, g->svg_mono, square_at(c, 24, 22, 80), k_white);
    scene_svg(p, g->svg_mono, square_at(c, 136, 22, 80), k_warn);
}

static void cell_svg_color(const Gallery* g, FlRect c) {
    const Painter* p = &g->painter;
    draw_cell_chrome(p, c, FL_S("svg (multi-color)"));
    scene_svg(p, g->svg_color, square_at(c, 80, 20, 90), k_white);
}

static void cell_effects(const Gallery* g, FlRect c) {
    const Painter* p = &g->painter;
    View v = p->view;
    draw_cell_chrome(p, c, FL_S("effects (shadow + glow)"));
    // Offsets and blur are world units scaled by zoom, so the shadow and glow follow their shape under zoom.
    FlEffect drop = { .kind = FlEffectKind_DropShadow,
                      .color = { 0, 0, 0, 150 },
                      .off_x = v_len(v, 6),
                      .off_y = v_len(v, 7),
                      .blur = v_len(v, 5) };
    scene_circle(p, point_at(c, 62, 58), 32, (ShapeStyle) { k_accent, k_white, 2, &drop });
    FlEffect glow = { .kind = FlEffectKind_Glow, .color = { 120, 200, 120, 220 }, .blur = v_len(v, 9) };
    scene_diamond(p, point_at(c, 168, 58), (FlVec2) { 34, 44 }, (ShapeStyle) { k_green, k_white, 2, &glow });
}

typedef void (*CellFn)(const Gallery* g, FlRect cell);

static const CellFn k_cells[] = {
    cell_fill_gradient, cell_borders, cell_clip, cell_circle_diamond, cell_arcs,      cell_polylines,
    cell_bars,          cell_images,  cell_text, cell_svg_mono,       cell_svg_color, cell_effects,
};

static const void* begin_scene(FlArena* arena) {
    fl_arena_rewind(arena);
    fl_arena_align(arena, 8);
    return fl_arena_current_pos(arena);
}

static void draw_backdrop_and_title(const Painter* p) {
    // Full-window background, emitted in screen space (not view-transformed) so it fills the window while the
    // gallery pans and zooms over it.
    ir_fill_rect(p->arena, (FlRect) { 0, 0, p->view.screen_w, p->view.screen_h }, (FlCornerRadius) { 0 },
                 (FlColor[4]) { k_bg, k_bg, k_bg, k_bg }, nullptr);

    scene_text(
        p, (TextSpec) { TITLE_SIZE, k_ink, FL_S("flowi — GPU backend primitive gallery"), { MARGIN, MARGIN + 26.0f } });
}

// The scene has no animation.
void gallery_build(const Gallery* g, FlRenderFrame* frame) {
    FlArena* arena = g->painter.arena;
    const void* start = begin_scene(arena);

    draw_backdrop_and_title(&g->painter);
    for_count(i, sizeof_array(k_cells)) {
        k_cells[i](g, cell_rect((int)i % GRID_COLS, (int)i / GRID_COLS));
    }

    frame->commands = start;
    frame->bytes = (u64)((const u8*)fl_arena_current_pos(arena) - (const u8*)start);
}
