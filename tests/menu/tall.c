// UI over-tall menu scrolling tests

#include "menu/common.h"
#include <math.h>

// Scrollable menu levels. A level whose item list is taller than the viewport is capped in height and scrolls:
// with the mouse wheel over the hovered level, and with keyboard scroll-into-view when the highlight moves past
// the visible edge. The cases check the per-level scroll_offset that positions the panel's clipped content. A
// level that fits keeps offset 0.

#define MENU_TALL_COUNT 15

// Distinct, stable labels for the root leaf rows and the submenu rows. FL_S_() gives the brace-init form an array
// needs. Names of several characters keep the derived widget ids clear of the submenu panel id, which is hashed
// from the parent row id.
static const FlString k_tall_root[MENU_TALL_COUNT] = {
    FL_S_("root_00"), FL_S_("root_01"), FL_S_("root_02"), FL_S_("root_03"), FL_S_("root_04"),
    FL_S_("root_05"), FL_S_("root_06"), FL_S_("root_07"), FL_S_("root_08"), FL_S_("root_09"),
    FL_S_("root_10"), FL_S_("root_11"), FL_S_("root_12"), FL_S_("root_13"), FL_S_("root_14"),
};

static const FlString k_tall_sub[MENU_TALL_COUNT] = {
    FL_S_("sub_00"), FL_S_("sub_01"), FL_S_("sub_02"), FL_S_("sub_03"), FL_S_("sub_04"),
    FL_S_("sub_05"), FL_S_("sub_06"), FL_S_("sub_07"), FL_S_("sub_08"), FL_S_("sub_09"),
    FL_S_("sub_10"), FL_S_("sub_11"), FL_S_("sub_12"), FL_S_("sub_13"), FL_S_("sub_14"),
};

// A submenu parent ("More") at index 0, then MENU_TALL_COUNT leaf rows (indices 1..MENU_TALL_COUNT). Both the root
// level and the submenu overflow the short test window, so each scrolls.
static void menu_tall_frame(void* user_data) {
    MenuTestState* st = (MenuTestState*)user_data;
    st->ctx = g_ui_ctx;

    ui_menu_region(k_menu_region_id, k_menu_id) {}

    ui_menu(k_menu_id, .min_width = 140) {
        ui_submenu(FL_S("More")) {
            for (int i = 0; i < MENU_TALL_COUNT; i++) {
                ui_menu_item(k_tall_sub[i]);
            }
        }
        for (int i = 0; i < MENU_TALL_COUNT; i++) {
            ui_menu_item(k_tall_root[i]);
        }
    }

    st->menu_open = ui_menu_is_open_impl(k_menu_id);
}

// A right-click opens the chain without opening a submenu. Dwell needs a stationary hover, which this is not.
static void menu_tall_open(FlApp* app, MenuTestState* st) {
    fl_app_inject_mouse_move(app, 20, 20);
    fl_app_tick(app, st, menu_tall_frame);
    fl_app_tick(app, st, menu_tall_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Right, true);
    fl_app_tick(app, st, menu_tall_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Right, false);
    fl_app_tick(app, st, menu_tall_frame);
}

static void menu_tall_key_tap(FlApp* app, MenuTestState* st, FlKey key) {
    fl_app_inject_key(app, key, true);
    fl_app_tick(app, st, menu_tall_frame);
    fl_app_inject_key(app, key, false);
    fl_app_tick(app, st, menu_tall_frame);
}

// Settles the pointer over a laid-out row so the wheel, and Clay's scroll-target arbitration, targets that level.
static bool menu_tall_hover(FlApp* app, MenuTestState* st, MenuRow row) {
    i32 cx = 0, cy = 0;
    if (!menu_row_center(st->ctx, row, &cx, &cy)) {
        return false;
    }
    fl_app_inject_mouse_move(app, cx, cy);
    fl_app_tick(app, st, menu_tall_frame);
    fl_app_tick(app, st, menu_tall_frame);
    fl_app_tick(app, st, menu_tall_frame);
    return true;
}

// Injects one wheel delta and ticks so it takes effect, which shows on the following tick. Positive dy scrolls up.
static void menu_tall_wheel(FlApp* app, MenuTestState* st, f32 dy) {
    fl_app_inject_wheel(app, 0.0f, dy);
    fl_app_tick(app, st, menu_tall_frame);
}

// The mouse wheel over an over-tall level scrolls that level's contents and clamps at both ends.
UTEST(ui_menu, overtall_wheel) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    // A short window, so the menu of about 16 rows overflows and the root level scrolls.
    FlAppSettings settings = { .window_title = FL_S("menu_tall_wheel"), .width = 400, .height = 240, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    MenuTestState st = { 0 };
    fl_app_tick(app, &st, menu_tall_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    UiMenuState* m = &st.ctx->shared->menu;

    menu_tall_open(app, &st);
    ASSERT_TRUE_MSG(st.menu_open, "tall menu opened");
    ASSERT_EQ_MSG(0.0, (double)m->levels[0].scroll_offset, "a freshly opened level starts scrolled to the top");

    // natural_height is the total recorded last frame.
    ASSERT_GT_MSG((double)st.ctx->menu_frame.levels[0].natural_height, (double)st.ctx->layout_height,
                  "the root item list is taller than the whole viewport (scroll must engage)");

    // Hover a row so the wheel targets this level.
    ASSERT_TRUE_MSG(menu_tall_hover(app, &st, MENU_ROW(FL_S("More"), 0, 0)), "More row was not laid out");
    menu_tall_wheel(app, &st, -3.0f);
    double after_one = (double)m->levels[0].scroll_offset;
    ASSERT_GT_MSG(after_one, 0.0, "wheel down scrolls the hovered level (offset grows from the top)");

    for (int i = 0; i < 40; i++) {
        menu_tall_wheel(app, &st, -5.0f);
    }
    double saturated = (double)m->levels[0].scroll_offset;
    ASSERT_GT_MSG(saturated, after_one, "sustained wheel-down scrolls further than a single notch");
    menu_tall_wheel(app, &st, -5.0f);
    ASSERT_EQ_MSG(saturated, (double)m->levels[0].scroll_offset, "offset clamps at the bottom (no over-scroll)");

    for (int i = 0; i < 60; i++) {
        menu_tall_wheel(app, &st, 5.0f);
    }
    ASSERT_EQ_MSG(0.0, (double)m->levels[0].scroll_offset, "wheel up returns to the top and clamps at 0");

    // The wheel scrolls only the hovered level and leaves the parent, still open and also too tall, at the top.
    ASSERT_TRUE_MSG(menu_tall_hover(app, &st, MENU_ROW(FL_S("More"), 0, 0)),
                    "More row for the descend was not laid out");
    menu_tall_key_tap(app, &st, FlKey_Right); // the highlight is on More, so this descends into its submenu (level 1)
    ASSERT_EQ_MSG((int)m->depth, 2, "descended into the More submenu for the wheel-isolation check");
    ASSERT_TRUE_MSG(menu_tall_hover(app, &st, MENU_ROW(k_tall_sub[0], 1, 0)), "first submenu row was not laid out");
    menu_tall_wheel(app, &st, -4.0f);
    ASSERT_GT_MSG((double)m->levels[1].scroll_offset, 0.0, "wheel over the submenu scrolls the submenu level");
    ASSERT_EQ_MSG(0.0, (double)m->levels[0].scroll_offset, "wheel over the submenu leaves the parent root untouched");

    fl_app_destroy(app);
}

// Arrow-key navigation past the visible edge scrolls the highlight into view in both directions, and a submenu
// that is too tall scrolls independently of its parent level, which is too tall as well.
UTEST(ui_menu, overtall_keyboard) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menu_tall_kbd"), .width = 400, .height = 240, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    MenuTestState st = { 0 };
    fl_app_tick(app, &st, menu_tall_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    UiMenuState* m = &st.ctx->shared->menu;

    menu_tall_open(app, &st);
    ASSERT_TRUE_MSG(st.menu_open, "tall menu opened");
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 0, "open highlights the first row (the More submenu parent)");
    ASSERT_EQ_MSG(0.0, (double)m->levels[0].scroll_offset, "root starts at the top");

    menu_tall_key_tap(app, &st, FlKey_Up);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, MENU_TALL_COUNT, "Up wraps to the last row");
    ASSERT_GT_MSG((double)m->levels[0].scroll_offset, 0.0, "wrapping to the last row scrolls the highlight into view");

    menu_tall_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 0, "Down wraps back to the first row");
    ASSERT_EQ_MSG(0.0, (double)m->levels[0].scroll_offset,
                  "wrapping back to the first row scrolls the root to the top");

    // Each level scrolls on its own. The highlight is on "More" (index 0), and Right descends into its submenu
    // (level 1).
    menu_tall_key_tap(app, &st, FlKey_Right);
    ASSERT_EQ_MSG((int)m->depth, 2, "Right descends into the More submenu (depth 2)");
    // Two ticks so the submenu that just opened records its geometry before it is navigated.
    fl_app_tick(app, &st, menu_tall_frame);
    fl_app_tick(app, &st, menu_tall_frame);
    ASSERT_EQ_MSG(0.0, (double)m->levels[1].scroll_offset, "the freshly opened submenu starts at the top");

    menu_tall_key_tap(app, &st, FlKey_Up);
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, MENU_TALL_COUNT - 1, "Up wraps to the submenu's last row");
    ASSERT_GT_MSG((double)m->levels[1].scroll_offset, 0.0, "the submenu scrolls to reveal its last row");
    ASSERT_EQ_MSG(0.0, (double)m->levels[0].scroll_offset, "scrolling the submenu leaves the PARENT level untouched");

    menu_tall_key_tap(app, &st, FlKey_Left);
    ASSERT_EQ_MSG((int)m->depth, 1, "Left ascends back to the root");
    ASSERT_EQ_MSG(0.0, (double)m->levels[1].scroll_offset, "collapsing the submenu resets its scroll offset");
    menu_tall_key_tap(app, &st, FlKey_Right);
    ASSERT_EQ_MSG((int)m->depth, 2, "re-descended into the submenu");
    ASSERT_EQ_MSG(0.0, (double)m->levels[1].scroll_offset, "a reopened level starts at the top, not mid-scroll");

    fl_app_destroy(app);
}
