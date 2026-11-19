// UI menu submenu path and row id tests

#include "menu/common.h"
#include "widgets/common.h"
#include <math.h>

// Submenus extend the open path past depth 1 under the same focus scope. The delta time is pinned so the hover
// dwell and branch-switch grace timers are reproducible.
//
// Submenu tree (level.index):
//   L0: Item0(0)  Sub(1, submenu)  Item2(2)
//   L1 (under Sub): S0(0)  Sub2(1, submenu)  S1(2)
//   L2 (under Sub2): D0(0)  D1(1)
static void submenu_frame(void* user_data) {
    MenuTestState* st = (MenuTestState*)user_data;
    st->ctx = g_ui_ctx;

    ui_menu_region(k_menu_region_id, k_menu_id) {}

    ui_menu(k_menu_id, .min_width = 200) {
        ui_menu_item(FL_S("Item0"));
        ui_submenu(FL_S("Sub")) {
            ui_menu_item(FL_S("S0"));
            ui_submenu(FL_S("Sub2")) {
                ui_menu_item(FL_S("D0"));
                ui_menu_item(FL_S("D1"));
            }
            ui_menu_item(FL_S("S1"));
        }
        ui_menu_item(FL_S("Item2"));
        ui_submenu(FL_S("DisSub"), .disabled = true) { // index 3: a disabled submenu never opens
            ui_menu_item(FL_S("X"));
        }
    }

    st->menu_open = ui_menu_is_open_impl(k_menu_id);
}

static void s3_ticks(FlApp* app, MenuTestState* st, int n) {
    for (int i = 0; i < n; i++) {
        fl_app_tick(app, st, submenu_frame);
    }
}

// (100,100) is the top-left anchor, over Item0 and not over the Sub row, so opening does not dwell on a submenu.
static void s3_open(FlApp* app, MenuTestState* st) {
    fl_app_inject_mouse_move(app, 100, 100);
    s3_ticks(app, st, 2);
    fl_app_inject_mouse_button(app, FlMouseButton_Right, true);
    s3_ticks(app, st, 1);
    fl_app_inject_mouse_button(app, FlMouseButton_Right, false);
    s3_ticks(app, st, 1);
}

// A click outside the chain closes it fully. Moving into empty space would not, so this uses a press.
static void s3_close(FlApp* app, MenuTestState* st) {
    fl_app_inject_mouse_move(app, 5, 5);
    s3_ticks(app, st, 2);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    s3_ticks(app, st, 1);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    s3_ticks(app, st, 1);
}

static void s3_key_tap(FlApp* app, MenuTestState* st, FlKey key) {
    fl_app_inject_key(app, key, true);
    fl_app_tick(app, st, submenu_frame);
    fl_app_inject_key(app, key, false);
    fl_app_tick(app, st, submenu_frame);
}

static void s3_type_char(FlApp* app, MenuTestState* st, FlString ch) {
    fl_app_inject_text(app, ch);
    fl_app_tick(app, st, submenu_frame);
}

typedef struct SubmenuRig {
    FlApp* app;
    MenuTestState st;
} SubmenuRig;

// Keyboard descend and ascend, including depth 3.
static void submenu_check_keyboard_path(int* utest_result, SubmenuRig* rig) {
    FlApp* app = rig->app;
    MenuTestState* st = &rig->st;
    UiMenuState* m = &st->ctx->shared->menu;

    s3_open(app, st);
    ASSERT_TRUE_MSG(st->menu_open, "right-click opened the menu");
    ASSERT_EQ_MSG((int)m->depth, 1, "fresh menu is a single level");

    s3_key_tap(app, st, FlKey_Down);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "Down highlights the Sub row");
    s3_key_tap(app, st, FlKey_Right);
    ASSERT_EQ_MSG((int)m->depth, 2, "Right descends into the highlighted submenu (depth 2)");
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 0, "the new deepest level highlights its first navigable row");

    s3_key_tap(app, st, FlKey_Down);
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 1, "Down moves the deepest highlight to Sub2");
    s3_key_tap(app, st, FlKey_Right);
    ASSERT_EQ_MSG((int)m->depth, 3, "Right descends again (depth 3, open path > 1)");

    s3_key_tap(app, st, FlKey_Left);
    ASSERT_EQ_MSG((int)m->depth, 2, "Left ascends one level");
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 1, "ascending keeps the parent highlight on the row we came from");
    s3_key_tap(app, st, FlKey_Left);
    ASSERT_EQ_MSG((int)m->depth, 1, "Left ascends back to the root");
}

// ESC pops one level and closes only at the root.
static void submenu_check_escape_pops(int* utest_result, SubmenuRig* rig) {
    FlApp* app = rig->app;
    MenuTestState* st = &rig->st;
    UiMenuState* m = &st->ctx->shared->menu;

    s3_close(app, st); // reopen so the highlight is back on the first row
    s3_open(app, st);
    s3_key_tap(app, st, FlKey_Down);
    s3_key_tap(app, st, FlKey_Right);
    ASSERT_EQ_MSG((int)m->depth, 2, "set up depth 2 for the ESC-pop case");
    s3_key_tap(app, st, FlKey_Escape);
    ASSERT_EQ_MSG((int)m->depth, 1, "ESC pops one level (does not close the chain)");
    ASSERT_TRUE_MSG(st->menu_open, "ESC at depth 2 left the chain open");
    s3_key_tap(app, st, FlKey_Escape);
    ASSERT_FALSE_MSG(st->menu_open, "ESC at the root closes the chain");
}

// Hover dwell, diagonal moves and the branch-switch grace.
static void submenu_check_hover_timers(int* utest_result, SubmenuRig* rig) {
    FlApp* app = rig->app;
    MenuTestState* st = &rig->st;
    UiMenuState* m = &st->ctx->shared->menu;

    // Hover dwell from closed: under the threshold the submenu stays closed, and past about 0.3s it opens.
    s3_open(app, st);
    i32 sx = 0, sy = 0;
    ASSERT_TRUE_MSG(menu_row_center(st->ctx, MENU_ROW(FL_S("Sub"), 0, 1), &sx, &sy), "Sub row was not laid out");
    fl_app_inject_mouse_move(app, sx, sy);
    s3_ticks(app, st, 2); // hover settles onto Sub, with at most about 0.1s of dwell
    ASSERT_EQ_MSG((int)m->depth, 1, "a brief hover (< dwell threshold) does not open the submenu");
    s3_ticks(app, st, 6); // stay on Sub past the dwell of about 0.3s
    ASSERT_EQ_MSG((int)m->depth, 2, "resting past the dwell threshold opens the submenu");
    ASSERT_EQ_MSG((int)m->levels[0].expanded_child_id,
                  (int)ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Sub"), .id = 1 }),
                  "the open path is identity-keyed to the Sub widget id");

    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "the parent level keeps a trail highlight on the Sub row");
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 0, "the deepest level has its own active highlight (S0)");

    // A diagonal move into the open submenu keeps it open.
    i32 s0x = 0, s0y = 0;
    ASSERT_TRUE_MSG(menu_row_center(st->ctx, MENU_ROW(FL_S("S0"), 1, 0), &s0x, &s0y), "S0 (level 1) was not laid out");
    fl_app_inject_mouse_move(app, s0x, s0y);
    s3_ticks(app, st, 3);
    ASSERT_EQ_MSG((int)m->depth, 2, "moving into the open submenu keeps it open");
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 0, "hover drives the deepest-level highlight onto S0");

    // Branch-switch grace: resting on a sibling collapses the deeper path only after about 0.25s.
    i32 i0x = 0, i0y = 0;
    ASSERT_TRUE_MSG(menu_row_center(st->ctx, MENU_ROW(FL_S("Item0"), 0, 0), &i0x, &i0y), "Item0 was not laid out");
    fl_app_inject_mouse_move(app, i0x, i0y);
    s3_ticks(app, st, 2); // at most about 0.1s, so the grace has not elapsed
    ASSERT_EQ_MSG((int)m->depth, 2, "a sibling hover within the grace window keeps the deeper path open");
    s3_ticks(app, st, 4); // past the grace of about 0.25s
    ASSERT_EQ_MSG((int)m->depth, 1, "a sibling-leaf hover past the grace collapses the deeper path");

    // Moving away from the open submenu collapses it at once, before the grace elapses.
    fl_app_inject_mouse_move(app, sx, sy);
    s3_ticks(app, st, 8);
    ASSERT_EQ_MSG((int)m->depth, 2, "re-opened Sub for the early-cancel case");
    fl_app_inject_mouse_move(app, i0x, i0y); // onto the Item0 sibling from the right, which is not a move away
    s3_ticks(app, st, 1);
    ASSERT_EQ_MSG((int)m->depth, 2, "just landed on the sibling — grace has not elapsed yet");
    fl_app_inject_mouse_move(app, i0x - 60, i0y); // leftward is away from the submenu, which opened to the right
    s3_ticks(app, st, 1);
    ASSERT_EQ_MSG((int)m->depth, 1, "moving away from the open submenu cancels the grace and collapses immediately");
}

// A click opens at once, a disabled submenu never opens and empty space keeps the chain open.
static void submenu_check_click_disabled_and_sticky(int* utest_result, SubmenuRig* rig) {
    FlApp* app = rig->app;
    MenuTestState* st = &rig->st;
    UiMenuState* m = &st->ctx->shared->menu;

    // A click opens a submenu at once, with no dwell. The short dt keeps the dwell timer far from its threshold,
    // so the open can only come from the click.
    fl_app_set_test_dt(app, 0.02f);
    s3_close(app, st);
    s3_open(app, st);
    i32 sx = 0, sy = 0;
    ASSERT_TRUE_MSG(menu_row_center(st->ctx, MENU_ROW(FL_S("Sub"), 0, 1), &sx, &sy),
                    "Sub row was not laid out for the click case");
    fl_app_inject_mouse_move(app, sx, sy);
    s3_ticks(app, st, 2);
    ASSERT_EQ_MSG((int)m->depth, 1, "a brief hover before the click has not dwelled the submenu open");
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    s3_ticks(app, st, 1);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    s3_ticks(app, st, 1);
    ASSERT_EQ_MSG((int)m->depth, 2, "clicking a submenu parent opens it instantly");

    // A disabled submenu never opens, even after resting well past the dwell threshold.
    fl_app_set_test_dt(app, 0.1f);
    s3_close(app, st);
    s3_open(app, st);
    i32 dsx = 0, dsy = 0;
    ASSERT_TRUE_MSG(menu_row_center(st->ctx, MENU_ROW(FL_S("DisSub"), 0, 3), &dsx, &dsy),
                    "DisSub row was not laid out");
    fl_app_inject_mouse_move(app, dsx, dsy);
    s3_ticks(app, st, 8); // about 0.8s, well past the dwell of about 0.3s
    ASSERT_EQ_MSG((int)m->depth, 1, "a disabled submenu never opens on hover-dwell");

    // Moving the cursor into empty space keeps the chain open. Only a click outside, ESC or an action closes it.
    fl_app_inject_mouse_move(app, 600, 300); // empty space, clear of every menu rect
    s3_ticks(app, st, 2);
    ASSERT_TRUE_MSG(st->menu_open, "moving into empty space does not close the chain (sticky)");

    s3_close(app, st);
    ASSERT_FALSE_MSG(st->menu_open, "click-outside closed the chain");
}

UTEST(ui_menu, submenu_open_path) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    // 800x600 leaves room for submenus to open rightward without flipping: L0 at x=100 w=200, L1 at x=300, L2 at
    // x=500.
    FlAppSettings settings = { .window_title = FL_S("submenu"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    fl_app_set_test_dt(app, 0.1f); // 0.1s per tick keeps the dwell (~0.3s) and grace (~0.25s) timers reproducible

    SubmenuRig rig = { .app = app };
    fl_app_tick(app, &rig.st, submenu_frame);
    ASSERT_TRUE_MSG(rig.st.ctx != nullptr, "frame did not run / no UI context");

    submenu_check_keyboard_path(utest_result, &rig);
    RETURN_IF_TEST_FAILED();
    submenu_check_escape_pops(utest_result, &rig);
    RETURN_IF_TEST_FAILED();
    submenu_check_hover_timers(utest_result, &rig);
    RETURN_IF_TEST_FAILED();
    submenu_check_click_disabled_and_sticky(utest_result, &rig);
    RETURN_IF_TEST_FAILED();

    fl_app_destroy(app);
}

// Type-ahead applies to the deepest open level only.
UTEST(ui_menu, type_ahead_submenu) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menu_type_sub"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    fl_app_set_test_dt(app, 0.1f);

    MenuTestState st = { 0 };
    fl_app_tick(app, &st, submenu_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    UiMenuState* m = &st.ctx->shared->menu;

    // L0: Item0(0) Sub(1, submenu) Item2(2) DisSub(3). Descend into Sub -> L1: S0(0) Sub2(1, submenu) S1(2).
    s3_open(app, &st);
    s3_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "L0 highlight on Sub");
    s3_key_tap(app, &st, FlKey_Right);
    ASSERT_EQ_MSG((int)m->depth, 2, "the submenu is open (depth 2)");
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 0, "the child level highlights its first item (S0)");

    // S0, Sub2 and S1 all begin with 'S', so typing 's' at the deepest level cycles among them. A submenu parent
    // is a type-ahead target too.
    s3_type_char(app, &st, FL_S("s"));
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 1, "type 's' at L1: S0 -> Sub2");
    s3_type_char(app, &st, FL_S("s"));
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 2, "type 's' again: Sub2 -> S1");
    s3_type_char(app, &st, FL_S("s"));
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 0, "type 's' again wraps: S1 -> S0");

    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "the parent level's highlight stayed on Sub throughout");
    ASSERT_EQ_MSG((int)m->depth, 2, "typing did not open or collapse a level (still depth 2)");

    s3_close(app, &st);
    fl_app_destroy(app);
}

// A menu row's id includes its build level as well as its index in the level (ui_menu_row_id_seed), so a row in
// the parent level and a row in its open submenu that share both a label and an index still get distinct ids.
//
// Layout (level.index):
//   L0: Branch(0, submenu)  Open(1)
//   L1 (under Branch): First(0)  Open(1)   <- same label "Open" and same index (1) as the L0 row
typedef struct MenuCollideState {
    FlUiCtx* ctx;
    bool menu_open;
    bool parent_open_clicked;
    bool sub_open_clicked;
} MenuCollideState;

static void collide_frame(void* user_data) {
    MenuCollideState* st = (MenuCollideState*)user_data;
    st->ctx = g_ui_ctx;

    ui_menu_region(k_menu_region_id, k_menu_id) {}

    ui_menu(k_menu_id, .min_width = 200) {
        ui_submenu(FL_S("Branch")) {
            ui_menu_item(FL_S("First"));
            if (ui_menu_item(FL_S("Open")).clicked) {
                st->sub_open_clicked = true;
            }
        }
        if (ui_menu_item(FL_S("Open")).clicked) {
            st->parent_open_clicked = true;
        }
    }

    st->menu_open = ui_menu_is_open_impl(k_menu_id);
}

static void collide_ticks(FlApp* app, MenuCollideState* st, int n) {
    for (int i = 0; i < n; i++) {
        fl_app_tick(app, st, collide_frame);
    }
}

static void collide_open(FlApp* app, MenuCollideState* st) {
    fl_app_inject_mouse_move(app, 100, 100);
    collide_ticks(app, st, 2);
    fl_app_inject_mouse_button(app, FlMouseButton_Right, true);
    collide_ticks(app, st, 1);
    fl_app_inject_mouse_button(app, FlMouseButton_Right, false);
    collide_ticks(app, st, 1);
}

static void collide_key_tap(FlApp* app, MenuCollideState* st, FlKey key) {
    fl_app_inject_key(app, key, true);
    fl_app_tick(app, st, collide_frame);
    fl_app_inject_key(app, key, false);
    fl_app_tick(app, st, collide_frame);
}

static void collide_click_at(FlApp* app, MenuCollideState* st, i32 x, i32 y) {
    fl_app_inject_mouse_move(app, x, y);
    collide_ticks(app, st, 3); // hover settles into hot on the target row
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    collide_ticks(app, st, 1);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    collide_ticks(app, st, 1);
}

UTEST(ui_menu, submenu_id_collision) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menu_collide"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    fl_app_set_test_dt(app, 0.02f); // a short dt, so a stray hover does not dwell a submenu open

    MenuCollideState st = { 0 };
    fl_app_tick(app, &st, collide_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    UiMenuState* m = &st.ctx->shared->menu;

    // Descend into Branch with the keyboard so the submenu's "Open" (level 1, index 1) lays out.
    collide_open(app, &st);
    ASSERT_TRUE_MSG(st.menu_open, "right-click opened the menu");
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 0, "open highlights the first navigable row (Branch)");
    collide_key_tap(app, &st, FlKey_Right);
    ASSERT_EQ_MSG((int)m->depth, 2, "Right descended into the Branch submenu (depth 2)");

    const u32 parent_open_id
        = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Open"), .id = ui_menu_row_id_seed(0, 1) });
    const u32 sub_open_id
        = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Open"), .id = ui_menu_row_id_seed(1, 1) });
    ASSERT_NE_MSG((unsigned)parent_open_id, (unsigned)sub_open_id,
                  "the parent-level and submenu-level Open rows must resolve to distinct ids");

    i32 pox = 0, poy = 0, sox = 0, soy = 0;
    ASSERT_TRUE_MSG(menu_row_center(st.ctx, MENU_ROW(FL_S("Open"), 0, 1), &pox, &poy),
                    "parent-level Open row was not laid out");
    ASSERT_TRUE_MSG(menu_row_center(st.ctx, MENU_ROW(FL_S("Open"), 1, 1), &sox, &soy),
                    "submenu-level Open row was not laid out");
    ASSERT_TRUE_MSG(sox != pox || soy != poy, "the two Open rows occupy different screen positions");

    // With colliding ids, the shared row state would set the parent level's highlight too.
    fl_app_inject_mouse_move(app, sox, soy);
    collide_ticks(app, &st, 3);
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 1, "hovering the submenu Open highlights it at level 1");
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 0,
                  "the parent level's highlight stays on the Branch trail (hover did not leak to the parent Open)");

    collide_click_at(app, &st, sox, soy);
    ASSERT_TRUE_MSG(st.sub_open_clicked, "clicking the submenu Open activates the submenu row");
    ASSERT_FALSE_MSG(st.parent_open_clicked, "the parent-level Open (same label + index) must not activate");

    fl_app_destroy(app);
}
