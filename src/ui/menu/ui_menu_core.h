#pragma once

#include "../ui_private.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Menu core: the open-path state machine, navigation, type-ahead and scrolling shared by the context menu and the menu
// bar. Surfaces drive it through these primitives and own only their per-surface open trigger and painting.

// The menu item the pointer is over. level is -1 when the hot widget is not a menu item (pointer outside the chain).
typedef struct MenuHit {
    int level;
    int index;
    bool submenu;
    bool navigable;
} MenuHit;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool menu_owned_by(const FlUiCtx* ctx, u32 scope);
void menu_close_internal(FlUiCtx* ctx);
void menu_reset_level(FlUiCtx* ctx, int level);
FlVec2 menu_track_pointer_move(FlUiCtx* ctx, UiMenuState* m);
void menu_collapse_to(UiMenuState* m, int new_depth);
void menu_branch_switch(FlUiCtx* ctx, int level, u32 child_id, bool child_is_submenu);
MenuHit menu_find_hot_item(const FlUiCtx* ctx);
void menu_move_highlight(UiMenuLevel* lvl, const UiMenuLevelFrame* lf, int dir);
void menu_record_label(UiMenuLevelFrame* lf, int index, FlString label);
bool menu_typeahead(FlUiCtx* ctx, int level);
void menu_record_row_geometry(UiMenuFrame* mf, int level, int index, f32 height);
void menu_configure_panel(FlUiCtx* ctx, int level, float min_w);
void menu_apply_wheel(FlUiCtx* ctx);
void menu_scroll_into_view(FlUiCtx* ctx, int level);
bool menu_highlight_is_submenu(const UiMenuLevel* level, const UiMenuLevelFrame* frame);
bool menu_step_highlight(FlUiCtx* ctx, int level, FlAction action, int dir);
float menu_min_width(float requested);
bool menu_dismissed(FlUiCtx* ctx, u32 scope, bool pointer_on_surface);
int menu_find_in_level(const UiMenuLevelFrame* lf, u32 id);
void menu_arm_chain(FlUiCtx* ctx, u32 scope, int first_level);
void menu_arm_focus_scope(FlUiCtx* ctx, u32 scope);
void menu_begin_level(FlUiCtx* ctx, int level);
