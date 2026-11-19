#pragma once

// Shared fixtures for the unit_tests_menu*.c translation units: the context-menu frame body, the id helpers and
// the pointer and key drivers that run it through the headless app loop.

#include "unit_tests_common.h"

// The menu cases run the headless fl_app loop, so the menu goes through the real right-click -> hover -> hot ->
// click path.
typedef struct MenuTestState {
    FlUiCtx* ctx;
    bool menu_open; // ui_menu_is_open read after the menu block on the last tick, so after any dismissal
    bool cut_clicked;
    bool stay_clicked;
    bool disabled_clicked;
    bool delete_clicked;
} MenuTestState;

extern const FlLayoutId k_menu_region_id;
extern const FlLayoutId k_menu_id;

void menu_frame(void* user_data);

// One menu row: its label, the level it is built at and its index within that level.
typedef struct MenuRow {
    FlString label;
    int level;
    int index;
} MenuRow;

#define MENU_ROW(label, level, index) ((MenuRow) { (label), (level), (index) })

// The center of the laid-out item with this id, or false if it has no size yet.
bool menu_item_state_center(FlUiCtx* ctx, FlLayoutId id, i32* out_cx, i32* out_cy);

// The center of a menu row. Same id derivation as menu_emit_row, which folds the level into the seed so rows at
// different levels never collide (ui_menu_row_id_seed).
bool menu_row_center(FlUiCtx* ctx, MenuRow row, i32* out_cx, i32* out_cy);

void menu_open_at(FlApp* app, MenuTestState* st, i32 x, i32 y);

// Hovers first so hot state propagates before the click.
void menu_click_at(FlApp* app, MenuTestState* st, i32 x, i32 y);

// The press tick carries the action press edge into the frame body, where begin() reads it and moves or activates
// the highlight. The release tick clears it. The mouse stays put, so the keyboard owns the highlight.
void menu_key_tap(FlApp* app, MenuTestState* st, FlKey key);

// Settles hover into a hovered signal: the move feeds next_hot_id one frame, hot_id the next, and state.hot the
// frame after. Returns false if the item was not laid out.
bool menu_hover_item(FlApp* app, MenuTestState* st, MenuRow row);

// Makes the accumulated type-ahead prefix stale so the next character starts a new one, without relying on the
// headless frame delta to cross the timeout. Any positive input-clock time is then past it.
void menu_typeahead_reset(MenuTestState* st);
