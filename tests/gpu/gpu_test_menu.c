// GPU smoke test: menu goldens through the GPU backend.

#include "gpu_test_common.h"

#if !defined(__APPLE__)

// Menu point-anchor flip/clamp golden: a menu opened near the top-left corner opens downward from the
// anchor, while a menu opened near the bottom edge flips to open upward. A full-screen backdrop makes the
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

// Highlighted-row theme: elevated and selected differ only in the green channel, both with low red so the menu
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

// Submenu placement and highlight golden. Under sm_theme every menu surface keeps a low red
// channel (so any nested-panel pixel separates from the red backdrop by red alone), while the green channel
// encodes the highlight state: unhighlighted rows g=20, the active (deepest) row g=120, and a trail (open-parent)
// row the 0.6 blend g=80: three well-separated bands so a probe can tell them apart.
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
typedef struct MenuBand {
    int x0, x1, y0, y1;
} MenuBand;

typedef struct ColorRange {
    int rmin, rmax, gmin, gmax, bmin, bmax;
} ColorRange;

static bool in_range(int value, int lo, int hi) {
    return value >= lo && value <= hi;
}

static bool color_in_range(const u8* p, ColorRange r) {
    return in_range(p[0], r.rmin, r.rmax) && in_range(p[1], r.gmin, r.gmax) && in_range(p[2], r.bmin, r.bmax);
}

static bool menu_band_has(const FlPixelReadback* buf, MenuBand band, ColorRange range) {
    for (int y = band.y0; y < band.y1; y++) {
        for (int x = band.x0; x < band.x1; x++) {
            if (color_in_range(pixel_at(buf, x, y), range)) {
                return true;
            }
        }
    }
    return false;
}

// Static-kinds golden. One menu carrying every non-stateful kind: a section header, action
// items (one with right-aligned shortcut text), a separator rule, and a disabled item. Every label is a single
// 'A': the embedded test font renders 'A' as a solid square, so each text run becomes a full-coverage filled block
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

// Stateful-kinds golden. Every menu surface keeps a low red channel (so it separates from the
// red backdrop); the check/radio marker paints interactive.base = high green (present only on the checked checkable
// and the selected radio); the custom swatch paints high blue. surface.selected == surface.elevated here so the
// open-highlight doesn't add stray green: markers are the only green, the swatch the only blue.
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

// Menu-bar golden. The bar is the menu core's level 0 (a persistent horizontal strip) and a
// top's dropdown is level 1, anchored under its button. Under mb_theme every menu surface (strip + dropdown) keeps
// a low red channel, so any menu pixel separates from the red backdrop by red alone.
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

typedef struct MenuEnv {
    UiShared* shared;
    FlUiCtx* ctx;
    FlSurface* surface;
    FlFontCache* font_cache;
    FlLayoutId menu_id;
    FlPixelReadback buf;
} MenuEnv;

typedef struct RedProbe {
    int x, y;
    bool dark;
    const char* msg;
} RedProbe;

#define RED_DARK(px, py, text) { .x = (px), .y = (py), .dark = true, .msg = (text) }
#define RED_BACKDROP(px, py, text) { .x = (px), .y = (py), .dark = false, .msg = (text) }
#define CHECK_RED(buf, ...)                                     \
    check_red_probes((buf), (const RedProbe[]) { __VA_ARGS__ }, \
                     sizeof((const RedProbe[]) { __VA_ARGS__ }) / sizeof(RedProbe))

// A menu surface is dark in the red channel; the backdrop behind it is red.
static int check_red_probes(const FlPixelReadback* buf, const RedProbe* probes, size_t count) {
    for (size_t i = 0; i < count; i++) {
        const u8* px = pixel_at(buf, probes[i].x, probes[i].y);
        CHECK(probes[i].dark ? px[0] < 100 : px[0] > 150, probes[i].msg);
    }
    return 0;
}

static bool menu_read(MenuEnv* m) {
    return render_seam_read_pixels(&m->ctx->shared->renderer, m->ctx->surface, &m->buf);
}

static void menu_env_begin(MenuEnv* m, FlArena* arena, FlRenderBackend* backend) {
    m->shared = ui_shared_create(arena, backend);
    m->ctx = ui_create(arena, m->shared);
}

static void menu_env_attach(MenuEnv* m, FlArena* arena, FlRenderBackend* backend) {
    m->surface = backend->create_surface(backend->user_data, 1920, 1080);
    ui_set_surface(m->ctx, m->surface);

    fl_vfs_init(arena);
    m->font_cache = font_cache_new(arena);
    ui_set_font_cache(m->ctx, m->font_cache);
}

// Identity UI scale so 1 logical px == 1 layout px and the probe coordinates are exact.
static void menu_env_set_layout(MenuEnv* m) {
    m->ctx->layout_width = (float)MENU_GOLDEN_W;
    m->ctx->layout_height = (float)MENU_GOLDEN_H;
    m->ctx->ui_scale = 1.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)MENU_GOLDEN_W, .height = (float)MENU_GOLDEN_H });
}

static void menu_env_destroy(MenuEnv* m, FlRenderBackend* backend) {
    ui_destroy(m->ctx);
    backend->destroy_surface(backend->user_data, m->surface);
    ui_shared_destroy(m->shared); // shared tier outlives the per-window ctx; exercises image_registry_destroy
}

static int check_menu_anchor_cases(MenuEnv* m) {
    // Case 1: opened near the top-left corner, so it opens downward. The menu is ~180 wide and ~150 tall, well
    // inside the 400x400 layout, so neither clamp nor flip should engage.
    const FlVec2 corner = { 60.0f, 40.0f };
    menu_golden_render(m->ctx, m->menu_id, corner, 3);
    CHECK(menu_read(m), "menu: read_pixels (corner) failed");
    // x=200 is inside the menu width (anchor 60 + 180 = 240) but past the short labels, so it lands on bare menu
    // surface, not glyphs.
    int status
        = CHECK_RED(&m->buf, RED_DARK(200, 80, "menu(corner): below the anchor is the dark menu surface"),
                    RED_BACKDROP(200, 20, "menu(corner): above the anchor is the red backdrop (opened downward)"));
    if (status != 0) {
        return 1;
    }

    // Close so the next case re-anchors instead of hitting the already-open guard.
    ui_menu_close_impl(m->menu_id);

    // Case 2: opened near the bottom edge, so it flips to open upward.
    const FlVec2 bottom = { 60.0f, (float)MENU_GOLDEN_H - 20.0f };
    menu_golden_render(m->ctx, m->menu_id, bottom, 3);
    CHECK(menu_read(m), "menu: read_pixels (flip) failed");
    status
        = CHECK_RED(&m->buf, RED_DARK(200, MENU_GOLDEN_H - 60, "menu(flip): above the anchor is the dark menu surface"),
                    RED_BACKDROP(200, MENU_GOLDEN_H - 5,
                                 "menu(flip): below the bottom-edge anchor stays red (menu flipped upward)"));
    if (status != 0) {
        return 1;
    }

    ui_menu_close_impl(m->menu_id);

    // Case 3: opened near the right edge, so it clamps left to stay on-screen. Anchor x=360 + 180 wide would reach
    // 540, past the 400-wide layout, so the popup core clamps the left edge to 400-180 = 220. The clamp is what
    // makes a pixel at x=300 (left of the un-clamped 360 origin) land on the dark menu surface instead of red.
    const FlVec2 right_edge = { (float)MENU_GOLDEN_W - 40.0f, 40.0f };
    menu_golden_render(m->ctx, m->menu_id, right_edge, 3);
    CHECK(menu_read(m), "menu: read_pixels (clamp) failed");
    status = CHECK_RED(
        &m->buf,
        RED_DARK(300, 80,
                 "menu(clamp): the menu shifted left so x=300 is dark menu surface (would be red without the clamp)"),
        RED_DARK(398, 80, "menu(clamp): the right edge of the menu stays on-screen"));
    ui_menu_close_impl(m->menu_id);
    return status;
}

static int check_menu_highlight(MenuEnv* m) {
    // Case 4: the highlighted-item visual state. Open at the top-left corner under the highlight theme;
    // the shared highlight initializes to the first navigable row on open, so row 0 paints surface.selected and the
    // rows below paint surface.elevated. Probe two row centers at x=200 (interior, past the short labels, so bare row
    // background, no glyphs). Rows are 36px tall under XS=4 panel padding from the y=40 anchor: row 0 spans [44,80]
    // (center ~62), row 1 spans [80,116] (center ~98).
    const FlVec2 hl_anchor = { 60.0f, 40.0f };
    menu_highlight_render(m->ctx, m->menu_id, hl_anchor, 3);
    CHECK(menu_read(m), "menu: read_pixels (highlight) failed");
    const u8* row0 = pixel_at(&m->buf, 200, 62);
    const u8* row1 = pixel_at(&m->buf, 200, 98);
    CHECK(row0[0] < 100 && row1[0] < 100, "menu(highlight): both probed rows are the dark menu surface");
    CHECK(row0[1] > 150, "menu(highlight): row 0 is the highlighted row (surface.selected, high green)");
    CHECK(row1[1] < 100, "menu(highlight): row 1 is unhighlighted (surface.elevated, low green)");

    ui_menu_close_impl(m->menu_id);
    return 0;
}

static int check_submenu_right(MenuEnv* m) {
    // Case 5: a submenu opens to the right of its parent. Root panel x in [20,200]; the nested panel
    // opens at the parent row's right edge, so a pixel well right of the root panel is dark menu surface, while the
    // far left stays red (the submenu did not flip). Probe y=100, a non-label column.
    menu_submenu_golden_render(m->ctx, m->menu_id, (FlVec2) { 20.0f, 40.0f }, 4);
    CHECK(menu_read(m), "menu: read_pixels (submenu right) failed");
    int status
        = CHECK_RED(&m->buf, RED_DARK(340, 100, "submenu(right): the nested panel opened to the right of its parent"),
                    RED_BACKDROP(10, 100, "submenu(right): nothing on the far left (the submenu did not flip)"));
    if (status != 0) {
        return 1;
    }

    // Trail vs active highlight, read by the green channel. Sub is the open parent (root row 0), so it is the trail
    // (g~80); its child's first row Alpha is the deepest active row (g~120); the next child row Beta is unhighlighted
    // (g~20). Root panel rows: padding XS=4 from anchor y=40, so Sub spans [44,80] (center 62); the child panel
    // top-aligns to the Sub row top, with its own XS padding, so Alpha spans [48,84] (center 66) and Beta [84,120].
    const u8* trail_px = pixel_at(&m->buf, 150, 62);  // Sub row interior (past the label, before the chevron)
    const u8* active_px = pixel_at(&m->buf, 340, 66); // Alpha row interior
    const u8* plain_px = pixel_at(&m->buf, 340, 100); // Beta row interior
    CHECK(trail_px[0] < 100 && active_px[0] < 100, "submenu(highlight): trail and active rows are dark menu surface");
    CHECK(plain_px[1] < 50, "submenu(highlight): an unhighlighted child row is low-green (surface.elevated)");
    CHECK(trail_px[1] > 55 && trail_px[1] < 105,
          "submenu(highlight): the open parent shows the trail blend (mid green)");
    CHECK(active_px[1] > 105,
          "submenu(highlight): the deepest level shows the full-strength active highlight (high green)");

    ui_menu_close_impl(m->menu_id);
    return 0;
}

static int check_submenu_flips(MenuEnv* m) {
    // Case 6: the submenu flips left when opening right would overflow. Root opened near the right edge
    // (x=210, width 180 -> right edge 390), so opening the 180-wide child rightward would reach 570 and overflow; it
    // flips to the parent's left instead. A pixel left of the root panel is now dark menu surface, and the sliver
    // right of the root stays red (nothing opened rightward).
    menu_submenu_golden_render(m->ctx, m->menu_id, (FlVec2) { 210.0f, 40.0f }, 4);
    CHECK(menu_read(m), "menu: read_pixels (submenu flip) failed");
    int status
        = CHECK_RED(&m->buf, RED_DARK(180, 100, "submenu(flip): the nested panel flipped to the left of its parent"),
                    RED_BACKDROP(395, 100, "submenu(flip): nothing to the right (it did not open rightward)"));
    if (status != 0) {
        return 1;
    }

    ui_menu_close_impl(m->menu_id);

    // Case 7: the submenu flips up when top-aligning would overflow the bottom edge. Root opened low
    // (y=300) so the root (2 rows, ~80 tall) still fits downward, but the taller child panel (3 rows, ~116 tall)
    // top-aligned at the Sub row (y~304) would reach ~420 and overflow; it bottom-aligns to the parent row instead,
    // opening upward (spanning ~[224,340]). In the child's column (right of the root panel) a pixel above the parent
    // row is dark menu surface, while a pixel below the child's bottom stays red.
    menu_submenu_golden_render(m->ctx, m->menu_id, (FlVec2) { 20.0f, 300.0f }, 4);
    CHECK(menu_read(m), "menu: read_pixels (submenu flip-up) failed");
    status = CHECK_RED(&m->buf,
                       RED_DARK(300, 260, "submenu(flip-up): the child panel flipped upward (dark above the parent)"),
                       RED_BACKDROP(300, 360, "submenu(flip-up): nothing below the child (it did not open downward)"));
    ui_menu_close_impl(m->menu_id);
    return status;
}

static int check_menu_stateful(MenuEnv* m) {
    // Case 8: stateful item kinds and custom content. Open at the top-left corner. Panel content starts
    // at x=44 (anchor 40 + XS=4 panel padding); each check/radio row is 36px tall and stacks from y=44: row0 [44,80],
    // row1 [80,116], row2 [116,152], row3 [152,188], then the 20px swatch row [188,208]. The marker gutter sits at
    // the row's left: panel content x=44 + row SM=8 padding = 52, an 18px gutter [52,70] with a 10px dot centered
    // (~[56,66]). A present marker is high green (interactive.base); the swatch is high blue. Band scans absorb the
    // sub-pixel placement of the small dot.
    menu_stateful_golden_render(m->ctx, m->menu_id, (FlVec2) { 40.0f, 40.0f }, 4);
    CHECK(menu_read(m), "menu: read_pixels (stateful) failed");

    const ColorRange marker = { 0, 100, 150, 255, 0, 120 };
    CHECK(menu_band_has(&m->buf, (MenuBand) { 52, 70, 52, 72 }, marker),
          "stateful(check): the checked checkable paints a marker (high-green) in its gutter");
    CHECK(!menu_band_has(&m->buf, (MenuBand) { 52, 70, 88, 108 }, marker),
          "stateful(check): the unchecked checkable paints no marker");

    CHECK(menu_band_has(&m->buf, (MenuBand) { 52, 70, 124, 144 }, marker),
          "stateful(radio): the selected radio paints a bullet marker (high-green)");
    CHECK(!menu_band_has(&m->buf, (MenuBand) { 52, 70, 160, 180 }, marker),
          "stateful(radio): the unselected radio paints no marker (exclusive selection)");

    const ColorRange swatch = { 0, 100, 0, 100, 150, 255 };
    CHECK(menu_band_has(&m->buf, (MenuBand) { 52, 100, 190, 206 }, swatch),
          "stateful(custom): the custom swatch row renders (high-blue) — arbitrary content composes between items");

    ui_menu_close_impl(m->menu_id);
    return 0;
}

static int check_menubar(MenuEnv* m) {
    // Case 9: the menu bar with one dropdown open. The strip is 30px tall across the top (y in
    // [0,30]); File (the first, leftmost top) is forced open so its 180-wide dropdown opens downward below it.
    // Probes read the red channel: menu surface is dark, the backdrop red.
    const FlLayoutId bar_id = { .name = FL_S("gpu_menubar"), .id = 0 };
    menubar_golden_render(m->ctx, bar_id, 4);
    CHECK(menu_read(m), "menubar: read_pixels failed");
    // File's dropdown opened below the strip, under the leftmost button (x in ~[4,184]).
    int status
        = CHECK_RED(&m->buf, RED_DARK(100, 15, "menubar(strip): the bar strip renders across the top (dark surface)"),
                    RED_DARK(380, 15, "menubar(strip): the strip spans the full width"),
                    RED_DARK(60, 70, "menubar(open): File's dropdown opened below the bar button (dark surface)"),
                    RED_BACKDROP(300, 70, "menubar(open): only File is open — no dropdown to the right"));

    m->ctx->shared->menu.active = false; // release the forced bar chain before teardown
    return status;
}

int run_menu_flip_test(FlArena* arena, FlRenderBackend* backend) {
    MenuEnv m = { .menu_id = { .name = FL_S("gpu_menu"), .id = 0 } };
    menu_env_begin(&m, arena, backend);
    CHECK(m.ctx != nullptr, "ui_create returned null (menu)");
    menu_env_attach(&m, arena, backend);
    menu_env_set_layout(&m);
    CHECK(m.ctx->shared->renderer.read_pixels != nullptr, "menu: backend has no pixel readback");

    if (check_menu_anchor_cases(&m) != 0) {
        return 1;
    }
    if (check_menu_highlight(&m) != 0) {
        return 1;
    }
    if (check_submenu_right(&m) != 0) {
        return 1;
    }
    if (check_submenu_flips(&m) != 0) {
        return 1;
    }
    if (check_menu_stateful(&m) != 0) {
        return 1;
    }
    if (check_menubar(&m) != 0) {
        return 1;
    }

    menu_env_destroy(&m, backend);
    return 0;
}

static int check_static_header_and_separator(MenuEnv* m) {
    // On open the highlight lands on the first navigable row (the item below), never the non-interactive header.
    // x=150 is bare row background: right of the left label, left of the right-aligned shortcut.
    const u8* hdr_bg = pixel_at(&m->buf, 150, 58); // header row center
    const u8* sel_bg = pixel_at(&m->buf, 150, 90); // first item row center
    CHECK(hdr_bg[0] < 100 && sel_bg[0] < 100, "static(header): both rows are the dark menu surface");
    CHECK(hdr_bg[1] < 60, "static(header): the header row is unhighlighted (elevated) — it is non-interactive");
    CHECK(sel_bg[1] > 90, "static(header): the first navigable row carries the open highlight (selected)");

    const ColorRange rule = { 0, 100, 0, 100, 180, 255 };
    CHECK(menu_band_has(&m->buf, (MenuBand) { 60, 180, 144, 153 }, rule),
          "static(separator): a separator rule (high-blue) renders in the separator row");
    CHECK(!menu_band_has(&m->buf, (MenuBand) { 60, 180, 110, 142 }, rule),
          "static(separator): a normal item row has no rule pixel");
    return 0;
}

static int check_static_text_kinds(MenuEnv* m) {
    // The solid-'A' glyph fills at full coverage, so amber reads ~ (235,165,30): a low-blue discriminator (<=110)
    // separates it from white text (blue ~240) and the dark surfaces stay below the red floor (>=120).
    const ColorRange amber = { 120, 255, 80, 215, 0, 110 };
    CHECK(menu_band_has(&m->buf, (MenuBand) { 44, 110, 155, 187 }, amber),
          "static(disabled): the disabled item label is greyed (secondary/amber text)");
    CHECK(!menu_band_has(&m->buf, (MenuBand) { 44, 110, 110, 142 }, amber),
          "static(disabled): a normal item label is primary (white), not greyed");

    CHECK(menu_band_has(&m->buf, (MenuBand) { 150, 234, 74, 106 }, amber),
          "static(shortcut): shortcut text renders right-aligned on the item row");
    CHECK(!menu_band_has(&m->buf, (MenuBand) { 150, 234, 110, 142 }, amber),
          "static(shortcut): an item without a shortcut has no trailing-edge text");
    return 0;
}

// Static-kinds pixel golden: the one menu golden that needs real glyphs. It renders 'A' labels into
// the shared backend glyph atlas, so it must run after run_text_atlas_tests (whose atlas-full stress assumes the
// atlas holds only its own glyphs) and before only run_frame_lifecycle_tests (which never touches the glyph
// atlas). It changes no resolution, so the create-resolution invariant that test relies on still holds.
int run_menu_static_kinds_test(FlArena* arena, FlRenderBackend* backend) {
    MenuEnv m = { .menu_id = { .name = FL_S("gpu_menu_static"), .id = 0 } };
    menu_env_begin(&m, arena, backend);
    CHECK(m.ctx != nullptr, "ui_create returned null (menu static kinds)");
    menu_env_attach(&m, arena, backend);

    // The embedded test font renders 'A' as a solid square, so the single-'A' labels paint as full-coverage filled
    // blocks a band scan can find by color.
    FlFontId font = font_cache_load_font_memory(m.font_cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 32);
    CHECK(font != FONT_ID_INVALID, "menu static kinds: the embedded font loads");
    ui_font_set(font);

    menu_env_set_layout(&m);
    CHECK(m.ctx->shared->renderer.read_pixels != nullptr, "menu static kinds: backend has no pixel readback");

    // Open at the top-left corner. Panel x in [40,240] (min_width 200), content top y=44 with XS=4 padding. Rows
    // stack: header 28px [44,72], item 36px [72,108], item [108,144], separator 9px [144,153], disabled [153,189],
    // item [189,225]. Channels: low red = menu surface, green split = highlight, high blue = separator rule, amber
    // (high R, low B) = secondary text (header/disabled/shortcut), white = primary text.
    menu_static_kinds_render(m.ctx, m.menu_id, (FlVec2) { 40.0f, 40.0f }, 4);
    CHECK(menu_read(&m), "menu static kinds: read_pixels failed");

    if (check_static_header_and_separator(&m) != 0) {
        return 1;
    }
    if (check_static_text_kinds(&m) != 0) {
        return 1;
    }

    ui_menu_close_impl(m.menu_id);
    menu_env_destroy(&m, backend);
    printf("gpu_smoke_test: menu static-kinds (S4a) golden PASS\n");
    return 0;
}

#endif // !defined(__APPLE__)
