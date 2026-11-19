// Shared context-menu fixtures for the unit_tests_menu*.c translation units

#include "menu/common.h"

const FlLayoutId k_menu_region_id = { .name = { .data = "ctx_region", .length = 10, .is_static = 1 }, .id = 0 };

const FlLayoutId k_menu_id = { .name = { .data = "ctx_menu", .length = 8, .is_static = 1 }, .id = 0 };

void menu_frame(void* user_data) {
    MenuTestState* st = (MenuTestState*)user_data;
    st->ctx = g_ui_ctx;

    // The region is the top-level element, and Clay grows it to fill the window.
    ui_menu_region(k_menu_region_id, k_menu_id) {}

    ui_menu(k_menu_id, .min_width = 200) {
        if (ui_menu_item(FL_S("Cut"), .shortcut = FL_S("Ctrl+X")).clicked) {
            st->cut_clicked = true;
        }
        ui_menu_item(FL_S("Copy"), .shortcut = FL_S("Ctrl+C"));
        if (ui_menu_item(FL_S("Paste"), .disabled = true).clicked) {
            st->disabled_clicked = true;
        }
        if (ui_menu_item(FL_S("Stay"), .close_policy = FlMenuClosePolicy_KeepOpen).clicked) {
            st->stay_clicked = true;
        }
    }

    // Read after the block, so an item close or dismissal inside ui_menu_end shows on this tick.
    st->menu_open = ui_menu_is_open_impl(k_menu_id);
}

// The center of the laid-out item with this id, or false if it has no size yet.
bool menu_item_state_center(FlUiCtx* ctx, FlLayoutId id, i32* out_cx, i32* out_cy) {
    ItemState* item = hashmap_get(&ctx->shared->item_state, ui_layout_id_to_clay_id(id));
    if (!item || item->state.size.x <= 0.0f) {
        return false;
    }
    *out_cx = (i32)(item->state.pos.x + item->state.size.x * 0.5f);
    *out_cy = (i32)(item->state.pos.y + item->state.size.y * 0.5f);
    return true;
}

// Same id derivation as menu_emit_row, which folds the level into the seed so rows at different levels never
// collide (ui_menu_row_id_seed).
bool menu_row_center(FlUiCtx* ctx, MenuRow row, i32* out_cx, i32* out_cy) {
    const FlLayoutId id = { .name = row.label, .id = ui_menu_row_id_seed(row.level, row.index) };
    return menu_item_state_center(ctx, id, out_cx, out_cy);
}

void menu_open_at(FlApp* app, MenuTestState* st, i32 x, i32 y) {
    fl_app_inject_mouse_move(app, x, y);
    fl_app_tick(app, st, menu_frame);
    fl_app_tick(app, st, menu_frame); // hover settles into hot state on the region
    fl_app_inject_mouse_button(app, FlMouseButton_Right, true);
    fl_app_tick(app, st, menu_frame); // the right-press edge opens the menu at the cursor
    fl_app_inject_mouse_button(app, FlMouseButton_Right, false);
    fl_app_tick(app, st, menu_frame);
}

// Hovers first so hot state propagates before the click.
void menu_click_at(FlApp* app, MenuTestState* st, i32 x, i32 y) {
    fl_app_inject_mouse_move(app, x, y);
    fl_app_tick(app, st, menu_frame);
    fl_app_tick(app, st, menu_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, st, menu_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    fl_app_tick(app, st, menu_frame);
}

// The press tick carries the action press edge into the frame body, where begin() reads it and moves or activates
// the highlight. The release tick clears it. The mouse stays put, so the keyboard owns the highlight.
void menu_key_tap(FlApp* app, MenuTestState* st, FlKey key) {
    fl_app_inject_key(app, key, true);
    fl_app_tick(app, st, menu_frame);
    fl_app_inject_key(app, key, false);
    fl_app_tick(app, st, menu_frame);
}

// Settles hover into a hovered signal: the move feeds next_hot_id one frame, hot_id the next, and state.hot the
// frame after. Returns false if the item was not laid out.
bool menu_hover_item(FlApp* app, MenuTestState* st, MenuRow row) {
    i32 cx = 0, cy = 0;
    if (!menu_row_center(st->ctx, row, &cx, &cy)) {
        return false;
    }
    fl_app_inject_mouse_move(app, cx, cy);
    fl_app_tick(app, st, menu_frame);
    fl_app_tick(app, st, menu_frame);
    fl_app_tick(app, st, menu_frame);
    return true;
}

// Makes the accumulated type-ahead prefix stale so the next character starts a new one, without relying on the
// headless frame delta to cross the timeout. Any positive input-clock time is then past it.
void menu_typeahead_reset(MenuTestState* st) {
    st->ctx->shared->menu.typeahead.deadline = 0.0;
}
