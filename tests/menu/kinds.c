// UI menu item kind tests

#include "menu/common.h"
#include <math.h>

// The item kinds without state: a section header, a separator, the disabled flag and right-aligned shortcut text.
// Header, separator and disabled rows are laid out but skipped by keyboard navigation, and they never activate.
// A click on a header or separator stays inside the menu, because they are part of its surface. Shortcut text is
// display only, and no key dispatch is connected to it.
//
// Menu layout (per-level index in parens; navigable rows are the action items only):
//   0 header "Edit"            (non-nav)
//   1 item   "Cut"  Ctrl+X
//   2 item   "Copy"
//   3 separator                (non-nav)
//   4 item   "Paste" disabled  (non-nav)
//   5 item   "Delete"
static const FlString k_menu_sep_name = { .data = "##menu_sep", .length = 10, .is_static = 1 };

static void static_kinds_frame(void* user_data) {
    MenuTestState* st = (MenuTestState*)user_data;
    st->ctx = g_ui_ctx;

    ui_menu_region(k_menu_region_id, k_menu_id) {}

    ui_menu(k_menu_id, .min_width = 200) {
        ui_menu_header(FL_S("Edit"));
        ui_menu_item(FL_S("Cut"), .shortcut = FL_S("Ctrl+X"));
        ui_menu_item(FL_S("Copy"));
        ui_menu_separator();
        if (ui_menu_item(FL_S("Paste"), .disabled = true).clicked) {
            st->disabled_clicked = true;
        }
        if (ui_menu_item(FL_S("Delete")).clicked) {
            st->delete_clicked = true;
        }
    }

    st->menu_open = ui_menu_is_open_impl(k_menu_id);
}

static void s4a_open(FlApp* app, MenuTestState* st, i32 x, i32 y) {
    fl_app_inject_mouse_move(app, x, y);
    fl_app_tick(app, st, static_kinds_frame);
    fl_app_tick(app, st, static_kinds_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Right, true);
    fl_app_tick(app, st, static_kinds_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Right, false);
    fl_app_tick(app, st, static_kinds_frame);
}

static void s4a_click(FlApp* app, MenuTestState* st, i32 x, i32 y) {
    fl_app_inject_mouse_move(app, x, y);
    fl_app_tick(app, st, static_kinds_frame);
    fl_app_tick(app, st, static_kinds_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, st, static_kinds_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    fl_app_tick(app, st, static_kinds_frame);
}

static void s4a_key_tap(FlApp* app, MenuTestState* st, FlKey key) {
    fl_app_inject_key(app, key, true);
    fl_app_tick(app, st, static_kinds_frame);
    fl_app_inject_key(app, key, false);
    fl_app_tick(app, st, static_kinds_frame);
}

static void s4a_type_char(FlApp* app, MenuTestState* st, FlString ch) {
    fl_app_inject_text(app, ch);
    fl_app_tick(app, st, static_kinds_frame);
}

UTEST(ui_menu, static_item_kinds) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menu_static"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    MenuTestState st = { 0 };
    fl_app_tick(app, &st, static_kinds_frame);

    s4a_open(app, &st, 100, 100);
    ASSERT_TRUE_MSG(st.menu_open, "right-click opened the static-kinds menu");
    UiMenuState* m = &st.ctx->shared->menu;

    i32 hx = 0, hy = 0, spx = 0, spy = 0;
    ASSERT_TRUE_MSG(menu_row_center(st.ctx, MENU_ROW(FL_S("Edit"), 0, 0), &hx, &hy), "header row did not lay out");
    ASSERT_TRUE_MSG(menu_row_center(st.ctx, MENU_ROW(k_menu_sep_name, 0, 3), &spx, &spy),
                    "separator row did not lay out");

    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1,
                  "open highlights the first navigable item (Cut), skipping the header");

    s4a_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 2, "Down: Cut -> Copy");
    s4a_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 5, "Down: Copy -> Delete (separator + disabled Paste skipped)");
    s4a_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "Down wraps Delete -> Cut (past the header)");
    s4a_key_tap(app, &st, FlKey_Up);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 5, "Up wraps Cut -> Delete (past the header)");

    // 'e' matches only the header "Edit", which is not navigable. The separator records a zero-length label, so it
    // never matches.
    s4a_type_char(app, &st, FL_S("e"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 5, "type 'e' does not land on the non-navigable header");
    menu_typeahead_reset(&st); // so 'c' starts a new prefix and does not extend "e" to "ec"
    s4a_type_char(app, &st, FL_S("c"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "type 'c' finds Cut, scanning past the skipped header");

    s4a_click(app, &st, spx, spy);
    ASSERT_TRUE_MSG(st.menu_open, "clicking a separator does not dismiss the menu");

    s4a_click(app, &st, hx, hy);
    ASSERT_TRUE_MSG(st.menu_open, "clicking a header does not dismiss the menu");

    i32 px = 0, py = 0;
    ASSERT_TRUE_MSG(menu_row_center(st.ctx, MENU_ROW(FL_S("Paste"), 0, 4), &px, &py),
                    "disabled Paste row did not lay out");
    s4a_click(app, &st, px, py);
    ASSERT_FALSE_MSG(st.disabled_clicked, "a disabled item never reports .clicked");
    ASSERT_TRUE_MSG(st.menu_open, "clicking a disabled item leaves the menu open");

    // One Down gives the keyboard the highlight. Then walk to Delete (index 5) before confirming.
    s4a_key_tap(app, &st, FlKey_Down);
    while (m->levels[0].highlight_index != 5) {
        s4a_key_tap(app, &st, FlKey_Down);
    }
    s4a_key_tap(app, &st, FlKey_Enter);
    ASSERT_TRUE_MSG(st.delete_clicked, "Enter activates the highlighted Delete action");
    ASSERT_FALSE_MSG(st.menu_open, "activating Delete (default close policy) tore the menu down");

    fl_app_destroy(app);
}

// The leaf kinds with state and their keep-open behavior, and custom content placed between items.
typedef struct MenuStatefulState {
    FlUiCtx* ctx;
    bool menu_open; // ui_menu_is_open read after the menu block on the last tick
    bool chk;
    bool chk_close;
    i32 radio_sel;
    bool custom_hit;
} MenuStatefulState;

static const FlLayoutId k_menu_sf_region_id = { .name = { .data = "sf_region", .length = 9, .is_static = 1 }, .id = 0 };

static const FlLayoutId k_menu_sf_id = { .name = { .data = "sf_menu", .length = 7, .is_static = 1 }, .id = 0 };

static void menu_stateful_frame(void* user_data) {
    MenuStatefulState* st = (MenuStatefulState*)user_data;
    st->ctx = g_ui_ctx;

    ui_menu_region(k_menu_sf_region_id, k_menu_sf_id) {}

    ui_menu(k_menu_sf_id, .min_width = 220) {
        ui_menu_check(FL_S("Chk"), &st->chk); // 0: keep-open checkable
        ui_menu_check(FL_S("ChkClose"), &st->chk_close, .close_policy = FlMenuClosePolicy_Close); // 1: forced to close
        ui_menu_radio(FL_S("R"), &st->radio_sel, 0);                                              // 2
        ui_menu_radio(FL_S("R"), &st->radio_sel, 1);                                              // 3
        ui_menu_radio(FL_S("R"), &st->radio_sel, 2);                                              // 4
        // Custom content between items: a plain button that uses no menu API.
        if (ui_button(FL_S("Custom")).clicked) {
            st->custom_hit = true;
        }
    }

    st->menu_open = ui_menu_is_open_impl(k_menu_sf_id);
}

static void menu_sf_open_at(FlApp* app, MenuStatefulState* st, i32 x, i32 y) {
    fl_app_inject_mouse_move(app, x, y);
    fl_app_tick(app, st, menu_stateful_frame);
    fl_app_tick(app, st, menu_stateful_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Right, true);
    fl_app_tick(app, st, menu_stateful_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Right, false);
    fl_app_tick(app, st, menu_stateful_frame);
}

static void menu_sf_click_at(FlApp* app, MenuStatefulState* st, i32 x, i32 y) {
    fl_app_inject_mouse_move(app, x, y);
    fl_app_tick(app, st, menu_stateful_frame);
    fl_app_tick(app, st, menu_stateful_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, st, menu_stateful_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    fl_app_tick(app, st, menu_stateful_frame);
}

// ui_menu_item and ui_button derive their ids the same way, from name + index, with index 0 for a button.
static bool menu_sf_click_item(FlApp* app, MenuStatefulState* st, FlString label, int index) {
    i32 cx = 0, cy = 0;
    if (!menu_item_state_center(st->ctx, (FlLayoutId) { .name = label, .id = index }, &cx, &cy)) {
        return false;
    }
    menu_sf_click_at(app, st, cx, cy);
    return true;
}

UTEST(ui_menu, stateful_item_kinds) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menu_sf"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    MenuStatefulState st = { 0 };
    fl_app_tick(app, &st, menu_stateful_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");

    menu_sf_open_at(app, &st, 120, 120);
    ASSERT_TRUE_MSG(st.menu_open, "right-click opened the stateful menu");

    ASSERT_FALSE_MSG(st.chk, "checkable starts unchecked");
    ASSERT_TRUE_MSG(menu_sf_click_item(app, &st, FL_S("Chk"), 0), "checkable row did not lay out");
    ASSERT_TRUE_MSG(st.chk, "clicking the checkable toggled its bool* on");
    ASSERT_TRUE_MSG(st.menu_open, "a checkable keeps the menu open by default");
    ASSERT_TRUE_MSG(menu_sf_click_item(app, &st, FL_S("Chk"), 0), "checkable row did not lay out (2)");
    ASSERT_FALSE_MSG(st.chk, "clicking the checkable again toggled it back off");
    ASSERT_TRUE_MSG(st.menu_open, "the menu is still open after a second toggle");

    ASSERT_EQ_MSG(st.radio_sel, 0, "radio group starts on value 0");
    ASSERT_TRUE_MSG(menu_sf_click_item(app, &st, FL_S("R"), 3), "radio (value 1) did not lay out"); // value 1
    ASSERT_EQ_MSG(st.radio_sel, 1, "picking the second radio moved the shared selection to 1");
    ASSERT_TRUE_MSG(st.menu_open, "a radio item keeps the menu open by default");
    ASSERT_TRUE_MSG(menu_sf_click_item(app, &st, FL_S("R"), 4), "radio (value 2) did not lay out"); // value 2
    ASSERT_EQ_MSG(st.radio_sel, 2, "picking the third radio moved the shared selection to 2 (exclusive)");
    ASSERT_TRUE_MSG(st.menu_open, "still open after a second radio pick");

    // The custom button is registered in the menu's popup scope, so it is part of the menu surface.
    i32 bx = 0, by = 0;
    ASSERT_TRUE_MSG(menu_item_state_center(st.ctx, (FlLayoutId) { .name = FL_S("Custom"), .id = 0 }, &bx, &by),
                    "the custom button between items did not lay out");
    menu_sf_click_at(app, &st, bx, by);
    ASSERT_TRUE_MSG(st.custom_hit, "the custom button between items is hittable (reported .clicked)");
    ASSERT_TRUE_MSG(st.menu_open, "clicking custom content inside the menu does not dismiss the chain");

    ASSERT_TRUE_MSG(menu_sf_click_item(app, &st, FL_S("ChkClose"), 1), "force-close checkable did not lay out");
    ASSERT_TRUE_MSG(st.chk_close, "the force-close checkable still toggled its bool*");
    ASSERT_FALSE_MSG(st.menu_open, "FlMenuClosePolicy_Close forced the checkable to dismiss the menu");

    fl_app_destroy(app);
}

static bool is_rectangle_with_id(const UiRenderCommand* cmd, u32 id) {
    return cmd->type == UiRenderCmd_Rectangle && cmd->id == id;
}

static bool menu_row_bg(u32 id, UiColor4* out) {
    for (i32 i = 0, n = ui_get_render_command_count(); i < n; i++) {
        UiRenderCommand cmd = { 0 };
        if (ui_get_render_command(i, &cmd) && is_rectangle_with_id(&cmd, id)) {
            *out = cmd.bg_color;
            return true;
        }
    }
    return false;
}

// A disabled row tracks the pointer, but at a fraction of an available row's highlight strength.
UTEST(ui_menu, disabled_highlight_is_dimmer) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menu_dim"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    // menu_frame's items are Cut(0), Copy(1), Paste(2, disabled), Stay(3).
    MenuTestState st = { 0 };
    fl_app_tick(app, &st, menu_frame);
    menu_open_at(app, &st, 120, 120);
    ASSERT_TRUE_MSG(st.menu_open, "menu opened");

    const FlThemeColors* theme = ui_get_theme_colors(st.ctx);
    float resting = (float)theme->surface.elevated.r;
    float selected = (float)theme->surface.selected.r;
    ASSERT_NE_MSG((int)resting, (int)selected, "the theme must distinguish the resting and selected surfaces");

    UiColor4 live = { 0 }, dead = { 0 };
    ASSERT_TRUE_MSG(menu_hover_item(app, &st, MENU_ROW(FL_S("Copy"), 0, 1)), "Copy was not laid out");
    ASSERT_TRUE_MSG(
        menu_row_bg(ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Copy"), .id = ui_menu_row_id_seed(0, 1) }),
                    &live),
        "the hovered available row painted no background");

    ASSERT_TRUE_MSG(menu_hover_item(app, &st, MENU_ROW(FL_S("Paste"), 0, 2)), "Paste was not laid out");
    ASSERT_TRUE_MSG(
        menu_row_bg(ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Paste"), .id = ui_menu_row_id_seed(0, 2) }),
                    &dead),
        "the hovered disabled row painted no background");

    // Written as a fraction of the available row's travel, so it holds whichever direction the theme's selected
    // surface lies in.
    float live_travel = live.r - resting;
    float dead_travel = dead.r - resting;
    ASSERT_TRUE_MSG(dead_travel != 0.0f, "a hovered disabled row is still visibly highlighted");
    ASSERT_TRUE_MSG(live_travel != 0.0f, "sanity: a hovered available row is highlighted");
    ASSERT_TRUE_MSG(dead_travel / live_travel > 0.0f && dead_travel / live_travel < 0.5f,
                    "a disabled row's highlight is well under an available row's");

    fl_app_destroy(app);
}
