// UI menu bar tests

#include "menu/common.h"
#include <math.h>

// The menu bar reuses the menu core's builder, highlight and open path, and has its own open trigger: a click
// where a context menu uses hover dwell.
typedef struct MenuBarState {
    FlUiCtx* ctx;
    bool open_clicked;
} MenuBarState;

static const FlLayoutId k_menubar_id = { .name = { .data = "appbar", .length = 6, .is_static = 1 }, .id = 0 };

static void menubar_frame(void* user_data) {
    MenuBarState* st = (MenuBarState*)user_data;
    st->ctx = g_ui_ctx;

    ui_menubar(k_menubar_id) {
        ui_menubar_menu(FL_S("File")) {
            ui_menu_item(FL_S("New"));
            if (ui_menu_item(FL_S("Open")).clicked) {
                st->open_clicked = true;
            }
        }
        ui_menubar_menu(FL_S("Edit")) {
            ui_menu_item(FL_S("Undo"));
            ui_menu_item(FL_S("Redo"));
        }
        ui_menubar_menu(FL_S("View")) {
            ui_menu_item(FL_S("Zoom"));
        }
    }
}

static void mb_ticks(FlApp* app, MenuBarState* st, int n) {
    for (int i = 0; i < n; i++) {
        fl_app_tick(app, st, menubar_frame);
    }
}

// Settles hover into hot state: the move feeds next_hot_id one frame, hot_id the next.
static void mb_hover(FlApp* app, MenuBarState* st, i32 x, i32 y) {
    fl_app_inject_mouse_move(app, x, y);
    mb_ticks(app, st, 3);
}

// Hovers first so hot state propagates before the click.
static void mb_click(FlApp* app, MenuBarState* st, i32 x, i32 y) {
    fl_app_inject_mouse_move(app, x, y);
    mb_ticks(app, st, 2);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    mb_ticks(app, st, 1);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    mb_ticks(app, st, 1);
}

static void mb_key_tap(FlApp* app, MenuBarState* st, FlKey key) {
    fl_app_inject_key(app, key, true);
    fl_app_tick(app, st, menubar_frame);
    fl_app_inject_key(app, key, false);
    fl_app_tick(app, st, menubar_frame);
}

static void mb_type_char(FlApp* app, MenuBarState* st, FlString ch) {
    fl_app_inject_text(app, ch);
    fl_app_tick(app, st, menubar_frame);
}

// The bar shares the one app-wide type-ahead accumulator. Making it stale starts the next character as a new
// prefix.
static void mb_typeahead_reset(MenuBarState* st) {
    st->ctx->shared->menu.typeahead.deadline = 0.0;
}

UTEST(ui_menu, menubar) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menubar"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    MenuBarState st = { 0 };
    fl_app_tick(app, &st, menubar_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    ASSERT_FALSE_MSG(ui_menubar_is_open_impl(k_menubar_id), "the bar starts with no dropdown open");

    UiMenuState* m = &st.ctx->shared->menu;
    const u32 file_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("File"), .id = 0 });
    const u32 edit_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Edit"), .id = 1 });

    i32 fx = 0, fy = 0, ex = 0, ey = 0;
    ASSERT_TRUE_MSG(menu_row_center(st.ctx, MENU_ROW(FL_S("File"), 0, 0), &fx, &fy), "File top was not laid out");
    ASSERT_TRUE_MSG(menu_row_center(st.ctx, MENU_ROW(FL_S("Edit"), 0, 1), &ex, &ey), "Edit top was not laid out");

    mb_hover(app, &st, fx, fy);
    ASSERT_FALSE_MSG(ui_menubar_is_open_impl(k_menubar_id), "a cold hover over a top must not open it");

    mb_click(app, &st, fx, fy);
    ASSERT_TRUE_MSG(ui_menubar_is_open_impl(k_menubar_id), "clicking a top opens its menu");
    ASSERT_EQ_MSG((int)m->depth, 2, "an open dropdown is level 1 over the bar's level 0 (depth 2)");
    ASSERT_EQ_MSG((unsigned)m->levels[0].expanded_child_id, (unsigned)file_id, "File is the open root");

    i32 ox = 0, oy = 0;
    ASSERT_TRUE_MSG(menu_row_center(st.ctx, MENU_ROW(FL_S("Open"), 1, 1), &ox, &oy),
                    "File's Open item was not laid out");
    ASSERT_TRUE_MSG(oy > fy, "the dropdown opened below the bar button (element-anchored under the top)");

    mb_hover(app, &st, ex, ey);
    ASSERT_EQ_MSG((int)m->depth, 2, "switching root keeps a dropdown open (still depth 2)");
    ASSERT_EQ_MSG((unsigned)m->levels[0].expanded_child_id, (unsigned)edit_id,
                  "hovering Edit while open switched the open root to Edit");

    mb_key_tap(app, &st, FlKey_Escape);
    ASSERT_EQ_MSG((int)m->depth, 1, "ESC collapses the dropdown back to the bar (depth 1)");
    ASSERT_TRUE_MSG(ui_menubar_is_open_impl(k_menubar_id), "the bar stays focused after collapsing the dropdown");

    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "the bar-level highlight is on Edit after the hover-switch");
    mb_key_tap(app, &st, FlKey_Right);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 2, "Right moves the highlight Edit -> View");
    mb_key_tap(app, &st, FlKey_Left);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "Left moves the highlight View -> Edit");

    mb_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)m->depth, 2, "Down opens the highlighted top (descends to the dropdown)");
    ASSERT_EQ_MSG((unsigned)m->levels[0].expanded_child_id, (unsigned)edit_id,
                  "Down opened Edit (the highlighted top)");

    // Close the chain completely, so the next open is a click and not a hover switch onto an open neighbour.
    mb_key_tap(app, &st, FlKey_Escape); // dropdown -> focused strip
    mb_key_tap(app, &st, FlKey_Escape); // focused strip -> closed
    ASSERT_FALSE_MSG(ui_menubar_is_open_impl(k_menubar_id), "a second ESC closes the focused bar entirely");

    mb_click(app, &st, fx, fy);
    ASSERT_EQ_MSG((unsigned)m->levels[0].expanded_child_id, (unsigned)file_id, "clicking File re-opens it as the root");
    i32 nx = 0, ny = 0;
    ASSERT_TRUE_MSG(menu_row_center(st.ctx, MENU_ROW(FL_S("Open"), 1, 1), &nx, &ny), "File's Open item re-laid out");
    mb_click(app, &st, nx, ny);
    ASSERT_TRUE_MSG(st.open_clicked, "activating a dropdown action item reports .clicked");
    ASSERT_FALSE_MSG(ui_menubar_is_open_impl(k_menubar_id), "an action item tore the whole bar chain down");

    fl_app_destroy(app);
}

static void bar_over_content_frame(void* user_data) {
    MenuBarState* st = (MenuBarState*)user_data;
    st->ctx = g_ui_ctx;

    ui_menubar(k_menubar_id) {
        ui_menubar_menu(FL_S("File")) {
            if (ui_menu_item(FL_S("Open")).clicked) {
                st->open_clicked = true;
            }
        }
        ui_menubar_menu(FL_S("Edit")) {
            ui_menu_item(FL_S("Undo"));
        }
    }
    // Content drawn after the bar is outside the bar's popup scope. Inside it, a click on the content would count
    // as a click inside the menu and the chain would never dismiss.
    ui_button(FL_S("under_the_bar"));
}

// The two ways out of an open menu bar with the mouse. A click on app content outside the menu dismisses the whole
// chain. Pressing the open top again closes the bar fully, where collapsing to a focused strip would keep input.
UTEST(ui_menu, menubar_dismissal) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("bar_dismiss"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    MenuBarState st = { 0 };
    fl_app_tick(app, &st, bar_over_content_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");

    i32 fx = 0, fy = 0;
    ASSERT_TRUE_MSG(menu_row_center(st.ctx, MENU_ROW(FL_S("File"), 0, 0), &fx, &fy), "File top was not laid out");
    // The click-outside test reads membership in the bar's popup scope, so this asserts on the scope. Asserting
    // on a click would need the content placed clear of the dropdown's rect.
    mb_click(app, &st, fx, fy);
    ASSERT_TRUE_MSG(ui_menubar_is_open_impl(k_menubar_id), "clicking the File top opened its dropdown");

    fl_app_tick(app, &st, bar_over_content_frame); // the opening frame lays the content out again, so let it settle
    u32 bar_scope = ui_layout_id_to_clay_id(k_menubar_id);
    ItemState* under = hashmap_get(&st.ctx->shared->item_state,
                                   ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("under_the_bar"), .id = 0 }));
    ASSERT_TRUE_MSG(under != nullptr, "the content button was not registered");
    ASSERT_NE_MSG((unsigned)under->popup_scope_id, (unsigned)bar_scope,
                  "content drawn after the bar must not inherit its popup scope");

    mb_click(app, &st, fx, fy);
    ASSERT_FALSE_MSG(ui_menubar_is_open_impl(k_menubar_id), "re-pressing the open top releases the whole chain");
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.depth, 0, "a released bar owns no levels");

    fl_app_destroy(app);
}

// A menu bar on screen next to a popup menu opened from app content, emitted in either order within the frame.
typedef struct BarAndPopupState {
    FlUiCtx* ctx;
    bool popup_first;
    bool open_requested;
    int clicked; // index into k_popup_labels of the item that reported .clicked, -1 for none
} BarAndPopupState;

static const FlLayoutId k_popup_id = { .name = { .data = "addmenu", .length = 7, .is_static = 1 }, .id = 0 };

static const char* const k_popup_labels[] = { "Oscillator", "Sample", "Noise", "Filter" };

static void bar_and_popup_bar(void) {
    ui_menubar(k_menubar_id) {
        ui_menubar_menu(FL_S("File")) {
            ui_menu_item(FL_S("New"));
        }
        ui_menubar_menu(FL_S("Edit")) {
            ui_menu_item(FL_S("Undo"));
        }
        ui_menubar_menu(FL_S("View")) {
            ui_menu_item(FL_S("Zoom"));
        }
        ui_menubar_menu(FL_S("Navigate")) {
            ui_menu_item(FL_S("Next"));
        }
    }
}

static void bar_and_popup_menu(BarAndPopupState* st) {
    if (st->open_requested) {
        st->open_requested = false;
        ui_menu_open_impl(k_popup_id, (FlVec2) { 100.0f, 200.0f });
    }
    ui_menu(k_popup_id, .min_width = 200) {
        // The header takes row 0, so item i is at row i + 1, an index a bar top also has.
        ui_menu_header(FL_S("Generators"));
        for (int i = 0; i < 4; i++) {
            FlString label = string_from_cstr_len(k_popup_labels[i], strlen(k_popup_labels[i]));
            if (ui_menu_item(label).clicked) {
                st->clicked = i;
            }
        }
    }
}

static void bar_and_popup_frame(void* user_data) {
    BarAndPopupState* st = (BarAndPopupState*)user_data;
    st->ctx = g_ui_ctx;
    if (st->popup_first) {
        bar_and_popup_menu(st);
        bar_and_popup_bar();
    } else {
        bar_and_popup_bar();
        bar_and_popup_menu(st);
    }
}

static void bp_ticks(FlApp* app, BarAndPopupState* st, int n) {
    for (int i = 0; i < n; i++) {
        fl_app_tick(app, st, bar_and_popup_frame);
    }
}

static void bp_click(FlApp* app, BarAndPopupState* st, i32 x, i32 y) {
    fl_app_inject_mouse_move(app, x, y);
    bp_ticks(app, st, 2);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    bp_ticks(app, st, 1);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    bp_ticks(app, st, 1);
}

// A passive menu bar keeps its tops out of the popup's level-0 rows. Clicking popup row i activates that row and
// not the bar top that shares index i, and leaves the bar closed. The bar still opens normally once the popup is
// gone.
UTEST(ui_menu, menubar_beside_popup) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    for (int order = 0; order < 2; order++) {
        FlAppSettings settings = { .window_title = FL_S("bar_popup"), .width = 800, .height = 600, .headless = true };
        FlApp* app = fl_app_create(&settings);
        ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

        BarAndPopupState st = { .popup_first = order == 1, .clicked = -1 };
        bp_ticks(app, &st, 1);
        ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
        UiMenuState* m = &st.ctx->shared->menu;

        st.open_requested = true;
        bp_ticks(app, &st, 2);
        ASSERT_TRUE_MSG(m->active && m->owner_scope_id == ui_layout_id_to_clay_id(k_popup_id), "the popup opened");
        ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1,
                      "the popup's first navigable row (after its header) claims the initial highlight");

        i32 fx = 0, fy = 0;
        ASSERT_TRUE_MSG(menu_row_center(st.ctx, MENU_ROW(FL_S("Filter"), 0, 4), &fx, &fy),
                        "Filter row was not laid out");
        bp_click(app, &st, fx, fy);
        ASSERT_EQ_MSG(st.clicked, 3, "clicking the popup's Filter row reports Filter as clicked");
        ASSERT_FALSE_MSG(ui_menubar_is_open_impl(k_menubar_id), "a click inside the popup leaves the bar closed");
        ASSERT_FALSE_MSG(m->active, "the action item closed the popup");

        i32 tx = 0, ty = 0;
        ASSERT_TRUE_MSG(menu_row_center(st.ctx, MENU_ROW(FL_S("File"), 0, 0), &tx, &ty), "File top was not laid out");
        bp_click(app, &st, tx, ty);
        ASSERT_TRUE_MSG(ui_menubar_is_open_impl(k_menubar_id), "the bar opens on a click after the popup closed");
        ASSERT_EQ_MSG((unsigned)m->levels[0].expanded_child_id,
                      (unsigned)ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("File"), .id = 0 }),
                      "File is the open root");

        fl_app_destroy(app);
    }
}

// A dropdown whose widest row carries both a long label and a long shortcut, so the row exceeds the panel's minimum
// width.
static void widerow_frame(void* user_data) {
    MenuBarState* st = (MenuBarState*)user_data;
    st->ctx = g_ui_ctx;

    ui_menubar(k_menubar_id) {
        ui_menubar_menu(FL_S("Edit")) {
            ui_menu_item(FL_S("Cut"), .shortcut = FL_S("Ctrl X"));
            if (ui_menu_item(FL_S("Duplicate Time"), .shortcut = FL_S("Ctrl Shift D")).clicked) {
                st->open_clicked = true;
            }
        }
    }
}

// A menu panel treats min_width as a minimum, so a row wider than it widens the panel and its trailing shortcut
// stays inside the painted surface. The case runs at a HiDPI content scale to cover the multiply from min_width
// to physical px as well.
UTEST(ui_menu, panel_fits_wide_rows) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("widerow"), .width = 1200, .height = 900, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    fl_app_set_window_content_scale_for(app, fl_app_main_window(app), 1.5f);

    MenuBarState st = { 0 };
    for (int i = 0; i < 2; i++) {
        fl_app_tick(app, &st, widerow_frame);
    }
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");

    i32 ex = 0, ey = 0;
    ASSERT_TRUE_MSG(menu_row_center(st.ctx, MENU_ROW(FL_S("Edit"), 0, 0), &ex, &ey), "Edit top was not laid out");
    fl_app_inject_mouse_move(app, ex, ey);
    for (int i = 0; i < 2; i++) {
        fl_app_tick(app, &st, widerow_frame);
    }
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, &st, widerow_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    fl_app_tick(app, &st, widerow_frame);
    ASSERT_TRUE_MSG(ui_menubar_is_open_impl(k_menubar_id), "clicking the Edit top opened its dropdown");

    // The dropdown's floating panel. ui_popup_begin keys it by (anchor label, the bar's popup scope id).
    u32 bar_scope = ui_layout_id_to_clay_id(k_menubar_id);
    u32 panel_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Edit"), .id = bar_scope });
    Clay_ElementData panel = Clay_GetElementData((Clay_ElementId) { .id = panel_id });
    ASSERT_TRUE_MSG(panel.found, "the dropdown panel was not laid out");

    u32 row_id
        = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Duplicate Time"), .id = ui_menu_row_id_seed(1, 1) });
    ItemState* row = hashmap_get(&st.ctx->shared->item_state, row_id);
    ASSERT_TRUE_MSG(row != nullptr && row->state.size.x > 0.0f, "the wide row was not laid out");

    ASSERT_TRUE_MSG(row->state.pos.x >= panel.boundingBox.x
                        && row->state.pos.x + row->state.size.x <= panel.boundingBox.x + panel.boundingBox.width + 0.5f,
                    "a row wider than min_width must widen the panel, not overflow it");

    i32 cx = (i32)(row->state.pos.x + row->state.size.x * 0.5f);
    i32 cy = (i32)(row->state.pos.y + row->state.size.y * 0.5f);
    fl_app_inject_mouse_move(app, cx, cy);
    for (int i = 0; i < 3; i++) {
        fl_app_tick(app, &st, widerow_frame);
    }
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[1].highlight_index, 1, "hovering the wide row highlights it");
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, &st, widerow_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    fl_app_tick(app, &st, widerow_frame);
    ASSERT_TRUE_MSG(st.open_clicked, "clicking the wide row dispatches its action");

    fl_app_destroy(app);
}

// The menu bar routes type-ahead through the same deepest-level path as the context menu, within an open dropdown
// and across the bar's own tops.
UTEST(ui_menu, type_ahead_menubar) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menubar_type"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    MenuBarState st = { 0 };
    fl_app_tick(app, &st, menubar_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    UiMenuState* m = &st.ctx->shared->menu;

    // Tops File(0) Edit(1) View(2); File's dropdown is New(0) Open(1).
    i32 fx = 0, fy = 0;
    ASSERT_TRUE_MSG(menu_row_center(st.ctx, MENU_ROW(FL_S("File"), 0, 0), &fx, &fy), "File top was not laid out");
    mb_click(app, &st, fx, fy);
    ASSERT_TRUE_MSG(ui_menubar_is_open_impl(k_menubar_id), "clicking File opened its dropdown");
    ASSERT_EQ_MSG((int)m->depth, 2, "an open dropdown is depth 2");

    // Each is a separate single-letter jump, so the prefix is made stale between them.
    mb_type_char(app, &st, FL_S("o"));
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 1, "dropdown type 'o': -> Open");
    mb_typeahead_reset(&st);
    mb_type_char(app, &st, FL_S("n"));
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 0, "dropdown type 'n': -> New");

    // With the dropdown collapsed, type-ahead runs across the bar's own tops, so their labels are recorded even
    // while their dropdowns are closed. The strip highlight was on File.
    mb_key_tap(app, &st, FlKey_Escape);
    ASSERT_EQ_MSG((int)m->depth, 1, "ESC collapsed the dropdown to the focused strip");
    mb_typeahead_reset(&st);
    mb_type_char(app, &st, FL_S("v"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 2, "bar-strip type 'v': File -> View top");
    mb_typeahead_reset(&st);
    mb_type_char(app, &st, FL_S("e"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "bar-strip type 'e': View -> Edit top (wrapped)");

    fl_app_destroy(app);
}
