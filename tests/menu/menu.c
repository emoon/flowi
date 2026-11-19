// UI context menu keyboard, hover and type-ahead tests

#include "menu/common.h"
#include <math.h>

UTEST(ui_menu, context_menu_tracer) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    // A roomy window, so the menu opens fully downward with no flip or clamp.
    FlAppSettings settings = { .window_title = FL_S("menu"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    MenuTestState st = { 0 };

    fl_app_tick(app, &st, menu_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    ASSERT_FALSE_MSG(st.menu_open, "menu starts closed");

    menu_open_at(app, &st, 120, 120);
    ASSERT_TRUE_MSG(st.menu_open, "right-click in the region opened the menu");

    i32 cx = 0, cy = 0;
    ASSERT_TRUE_MSG(menu_row_center(st.ctx, MENU_ROW(FL_S("Cut"), 0, 0), &cx, &cy), "Cut item was not laid out");

    // Three ticks let the hover settle into a hovered signal during item emission: the move feeds next_hot_id one
    // frame, hot_id picks it up the next, and state.hot (read while the item is emitted) is set the frame after.
    i32 copyx = 0, copyy = 0;
    ASSERT_TRUE_MSG(menu_row_center(st.ctx, MENU_ROW(FL_S("Copy"), 0, 1), &copyx, &copyy),
                    "Copy item was not laid out");
    fl_app_inject_mouse_move(app, copyx, copyy);
    fl_app_tick(app, &st, menu_frame);
    fl_app_tick(app, &st, menu_frame);
    fl_app_tick(app, &st, menu_frame);
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 1,
                  "hovering Copy highlights index 1 at level 0");
    fl_app_inject_mouse_move(app, cx, cy); // move back over Cut (index 0)
    fl_app_tick(app, &st, menu_frame);
    fl_app_tick(app, &st, menu_frame);
    fl_app_tick(app, &st, menu_frame);
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 0,
                  "moving to Cut moves the highlight to index 0");

    // Hovering the disabled Paste item still highlights it. The greyed label shows that it is unavailable.
    ASSERT_TRUE_MSG(menu_hover_item(app, &st, MENU_ROW(FL_S("Paste"), 0, 2)), "Paste item was not laid out");
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 2,
                  "hovering a disabled row moves the highlight onto it");

    menu_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 3,
                  "Down from a hover-highlighted disabled row lands on the next navigable row");

    i32 dx = 0, dy = 0;
    ASSERT_TRUE_MSG(menu_row_center(st.ctx, MENU_ROW(FL_S("Paste"), 0, 2), &dx, &dy), "Paste item was not laid out");
    menu_click_at(app, &st, dx, dy);
    ASSERT_FALSE_MSG(st.disabled_clicked, "a disabled item never reports clicked");
    ASSERT_TRUE_MSG(st.menu_open, "clicking a disabled item leaves the menu open");

    i32 sx = 0, sy = 0;
    ASSERT_TRUE_MSG(menu_row_center(st.ctx, MENU_ROW(FL_S("Stay"), 0, 3), &sx, &sy), "Stay item was not laid out");
    menu_click_at(app, &st, sx, sy);
    ASSERT_TRUE_MSG(st.stay_clicked, "keep-open item reports clicked");
    ASSERT_TRUE_MSG(st.menu_open, "keep-open item does not close the menu");

    ASSERT_TRUE_MSG(menu_row_center(st.ctx, MENU_ROW(FL_S("Cut"), 0, 0), &cx, &cy), "Cut item geometry");
    menu_click_at(app, &st, cx, cy);
    ASSERT_TRUE_MSG(st.cut_clicked, "default action item reports clicked");
    ASSERT_FALSE_MSG(st.menu_open, "default close policy tore the menu down on activation");

    menu_open_at(app, &st, 120, 120);
    ASSERT_TRUE_MSG(st.menu_open, "reopened the menu");
    fl_app_inject_key(app, FlKey_Escape, true);
    fl_app_tick(app, &st, menu_frame);
    fl_app_inject_key(app, FlKey_Escape, false);
    fl_app_tick(app, &st, menu_frame);
    ASSERT_FALSE_MSG(st.menu_open, "ESC dismisses the menu");

    // The top-left corner is well clear of a menu opened at 120,120.
    menu_open_at(app, &st, 120, 120);
    ASSERT_TRUE_MSG(st.menu_open, "reopened the menu for the click-outside case");
    menu_click_at(app, &st, 5, 5);
    ASSERT_FALSE_MSG(st.menu_open, "a click outside the menu rects dismisses it");

    fl_app_destroy(app);
}

UTEST(ui_menu, confirm_on_a_hovered_disabled_row_is_ignored) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menu_disabled"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    MenuTestState st = { 0 };
    fl_app_tick(app, &st, menu_frame);
    menu_open_at(app, &st, 120, 120);
    ASSERT_TRUE_MSG(st.menu_open, "right-click in the region opened the menu");

    ASSERT_TRUE_MSG(menu_hover_item(app, &st, MENU_ROW(FL_S("Paste"), 0, 2)), "Paste item was not laid out");
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 2, "hovering the disabled row highlights it");

    menu_key_tap(app, &st, FlKey_Enter);
    ASSERT_FALSE_MSG(st.disabled_clicked, "Enter on a disabled row must not report clicked");
    ASSERT_TRUE_MSG(st.menu_open, "Enter on a disabled row leaves the menu open");

    fl_app_destroy(app);
}

UTEST(ui_menu, region_opens_on_the_press_not_while_held) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menu_held"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    MenuTestState st = { 0 };
    fl_app_tick(app, &st, menu_frame);
    fl_app_inject_mouse_move(app, 120, 120);
    fl_app_tick(app, &st, menu_frame);
    fl_app_tick(app, &st, menu_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Right, true);
    fl_app_tick(app, &st, menu_frame);
    ASSERT_TRUE_MSG(st.menu_open, "the right-button press opened the menu");

    menu_key_tap(app, &st, FlKey_Escape);
    ASSERT_FALSE_MSG(st.menu_open, "ESC dismisses the menu while the right button is still down");

    fl_app_tick(app, &st, menu_frame);
    fl_app_tick(app, &st, menu_frame);
    ASSERT_FALSE_MSG(st.menu_open, "holding the right button must not reopen the dismissed menu");

    fl_app_inject_mouse_button(app, FlMouseButton_Right, false);
    fl_app_destroy(app);
}

// The keyboard also drives the per-level highlight, and the most recent input (keyboard or hover) owns it.
// Gamepad A and B share CONFIRM and CANCEL with Enter and ESC, so these paths cover the pad actions too. Headless
// has no pad injection.
UTEST(ui_menu, keyboard_nav) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menu_kbd"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    MenuTestState st = { 0 };
    fl_app_tick(app, &st, menu_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");

    // Items are Cut(0), Copy(1), Paste(2, disabled), Stay(3, keep-open).
    menu_open_at(app, &st, 120, 120);
    ASSERT_TRUE_MSG(st.menu_open, "menu opened for keyboard nav");

    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 0,
                  "open highlights the first navigable item (Cut)");

    menu_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 1, "Down: Cut -> Copy");
    menu_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 3,
                  "Down: Copy -> Stay (disabled Paste skipped)");

    menu_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 0, "Down wraps Stay -> Cut");

    menu_key_tap(app, &st, FlKey_Up);
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 3, "Up wraps Cut -> Stay");
    menu_key_tap(app, &st, FlKey_Up);
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 1, "Up: Stay -> Copy (disabled Paste skipped)");

    ASSERT_TRUE_MSG(menu_hover_item(app, &st, MENU_ROW(FL_S("Stay"), 0, 3)), "Stay item was not laid out for hover");
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 3,
                  "hover reclaims the highlight (mouse moved last)");

    menu_key_tap(app, &st, FlKey_Up);
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 1,
                  "keyboard reclaims the highlight from a stationary hover (Stay -> Copy)");

    // Copy has no handler in menu_frame, so move to Cut, which sets cut_clicked, before activating.
    menu_key_tap(app, &st, FlKey_Up);
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 0, "Up: Copy -> Cut before activate");
    fl_app_inject_key(app, FlKey_Enter, true);
    fl_app_tick(app, &st, menu_frame);
    ASSERT_TRUE_MSG(st.cut_clicked, "Enter activates the highlighted item (Cut reported clicked)");
    ASSERT_FALSE_MSG(st.menu_open, "activating a default-close item tore the menu down");
    fl_app_inject_key(app, FlKey_Enter, false);
    fl_app_tick(app, &st, menu_frame);

    menu_open_at(app, &st, 120, 120);
    ASSERT_TRUE_MSG(st.menu_open, "reopened the menu for the ESC-close case");
    fl_app_inject_key(app, FlKey_Escape, true);
    fl_app_tick(app, &st, menu_frame);
    fl_app_inject_key(app, FlKey_Escape, false);
    fl_app_tick(app, &st, menu_frame);
    ASSERT_FALSE_MSG(st.menu_open, "ESC (CANCEL) closes the menu");

    fl_app_destroy(app);
}

// inject_text queues the character in the headless backend. The next tick's pump drains it into
// text_input_this_frame and the menu's begin() adds it to the type-ahead prefix. The mouse stays put across the
// tick, so type-ahead keeps the shared highlight.
static void menu_type_char(FlApp* app, MenuTestState* st, FlString ch) {
    fl_app_inject_text(app, ch);
    fl_app_tick(app, st, menu_frame);
}

// Ticks with no input so the input clock advances and the type-ahead timeout elapses in real time, with a pinned
// dt, without the reset above.
static void menu_idle_ticks(FlApp* app, MenuTestState* st, int n) {
    for (int i = 0; i < n; i++) {
        fl_app_tick(app, st, menu_frame);
    }
}

static FlString test_label_at(const void* source, u32 row) {
    const FlString* labels = source;
    return labels[row];
}

// The menu's type-ahead prefix holds at most UI_MENU_TYPEAHEAD_CAP bytes. Two rows that share their first 24
// bytes both match a longer typed prefix, so the cursor row stays where it is.
UTEST(ui_menu, type_ahead_prefix_cap) {
    const char* alphabet = "abcdefghijklmnopqrstuvwxyzab";
    const FlString labels[2]
        = { { .data = "abcdefghijklmnopqrstuvwxQQQQ", .length = 28 }, { .data = alphabet, .length = 28 } };
    TypeAheadState state = { 0 };
    type_ahead_feed(&state, (FlString) { .data = alphabet, .length = 28 }, 1.0, UI_MENU_TYPEAHEAD_CAP);
    TypeAheadLabels rows = { .source = labels, .label_at = test_label_at, .count = 2 };
    i32 idx = type_ahead_find(&state, rows, 0);
    ASSERT_EQ_MSG((int)idx, 0, "a prefix past the cap truncates to the cap and keeps the current row");
    ASSERT_EQ_MSG((int)state.len, UI_MENU_TYPEAHEAD_CAP, "prefix length is bounded by the menu cap");
}

// The trail highlight truncates each channel of the float blend. Rounding, or a lerp in another order, changes
// the result.
UTEST(ui_menu, blend_truncates_channels) {
    FlColor a = { 5, 5, 5, 255 };
    FlColor b = { 0, 0, 0, 255 };
    FlColor c = ui_menu_blend(a, b, 0.6f);
    ASSERT_EQ_MSG((int)c.r, 1, "5 -> 0 at t=0.6 truncates to 1");
    ASSERT_EQ_MSG((int)c.a, 255, "an equal alpha stays put");
}

// A printable character moves the deepest level's highlight to the next item whose label starts with it. This is
// the third input that drives the per-level highlight, with hover and the arrow keys.
UTEST(ui_menu, type_ahead) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menu_type"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    fl_app_set_test_dt(app, 0.1f); // 0.1s per tick, so the type-ahead timeout of about 1.0s below is reproducible

    MenuTestState st = { 0 };
    fl_app_tick(app, &st, menu_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    UiMenuState* m = &st.ctx->shared->menu;

    // Items are Cut(0), Copy(1), Paste(2, disabled), Stay(3).
    menu_open_at(app, &st, 120, 120);
    ASSERT_TRUE_MSG(st.menu_open, "menu opened for type-ahead");
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 0, "open highlights Cut");

    menu_type_char(app, &st, FL_S("c"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "type 'c': Cut -> Copy");
    ASSERT_TRUE_MSG(st.menu_open, "type-ahead leaves the menu open");

    menu_type_char(app, &st, FL_S("c"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 0, "type 'c' again cycles Copy -> Cut (wrapped)");

    menu_typeahead_reset(&st);
    menu_type_char(app, &st, FL_S("s"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 3, "type 's': -> Stay");

    menu_typeahead_reset(&st);
    menu_type_char(app, &st, FL_S("z"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 3, "no-match keystroke leaves the highlight put");

    // 'p' matches only the disabled Paste.
    menu_typeahead_reset(&st);
    menu_type_char(app, &st, FL_S("p"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 3, "disabled Paste is not a type-ahead target");

    // From Stay, hover Cut. Then 'c' cycles to Copy although the pointer is still on Cut.
    ASSERT_TRUE_MSG(menu_hover_item(app, &st, MENU_ROW(FL_S("Cut"), 0, 0)), "Cut item was not laid out for hover");
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 0, "hover reclaims the highlight from type-ahead (Stay -> Cut)");
    menu_typeahead_reset(&st);
    menu_type_char(app, &st, FL_S("c"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "type reclaims from a stationary hover (Cut -> Copy)");

    // The timeout with real elapsed time: after the idle, 'u' starts a new prefix that matches nothing. An
    // accumulated "cu" would narrow to Cut.
    menu_click_at(app, &st, 5, 5);
    menu_open_at(app, &st, 120, 120);
    menu_type_char(app, &st, FL_S("c"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "timeout setup: 'c' Cut -> Copy");
    menu_idle_ticks(app, &st, 12); // 12 * 0.1s = 1.2s > TYPE_AHEAD_TIMEOUT, so the prefix goes stale
    menu_type_char(app, &st, FL_S("u"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "after a >1s pause 'u' is a fresh no-match, not 'cu'");

    // Two characters without a pause: 'c' then 'u' accumulates "cu", which only Cut matches. The second byte
    // narrows the match and does not cycle.
    menu_click_at(app, &st, 5, 5);
    ASSERT_FALSE_MSG(st.menu_open, "click-outside closed the menu before the multi-char case");
    menu_open_at(app, &st, 120, 120);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 0, "reopen highlights Cut");
    menu_type_char(app, &st, FL_S("c"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "multi-char step 1: 'c' Cut -> Copy");
    menu_type_char(app, &st, FL_S("u"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 0, "multi-char step 2: 'cu' narrows to Cut");

    fl_app_destroy(app);
}
