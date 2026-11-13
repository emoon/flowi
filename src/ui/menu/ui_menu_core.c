#include "ui_menu_core.h"
#include <core/assert.h>
#include <core/math.h>
#include "external/clay/clay.h"
#include "../input_actions.h"
#include "../layout.h"
#include "../scroll_core.h"
#include <core/string.h>
#include "../ui.h"
#include "../ui_color_utils.h"
#include "../ui_popup.h"
#include "../ui_private.h"
#include "../ui_tokens.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Defaults

// Default panel width in logical px.
#define UI_MENU_DEFAULT_MIN_WIDTH 180.0f
// Gap kept between a scrolling level and the top and bottom of the viewport.
#define UI_MENU_VIEWPORT_MARGIN 8.0f
// Wheel pixels scrolled per notch.
#define UI_MENU_WHEEL_SPEED 30.0f
// Vertical padding of the panel around its item list.
#define UI_MENU_PANEL_VPAD (2.0f * (float)FlSpacing_XS)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Menu core - the open-path state machine, navigation, type-ahead and scrolling shared by every menu surface.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool menu_owned_by(const FlUiCtx* ctx, u32 scope) {
    return ctx->shared->menu.active && ctx->shared->menu.owner_scope_id == scope;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Close the chain and release its focus scope.
void menu_close_internal(FlUiCtx* ctx) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    if (ctx->popup_focus_scope_id == m->owner_scope_id && m->owner_scope_id != 0) {
        ui_focus_scope_release(ctx);
        ctx->saved_focused_id = 0;
        ctx->popup_closed_this_frame = true;
    }

    m->active = false;
    m->depth = 0;
    mf->build_level = 0;
    mf->close_requested = false;
    m->owner_scope_id = 0;
    m->dwell_item_id = 0;
    m->hover_accum = 0.0f;
    for (int i = 0; i < UI_MENU_MAX_DEPTH; i++) {
        m->levels[i].expanded_child_id = 0;
        m->levels[i].flip_left = false;
        m->levels[i].scroll_offset = 0.0f;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Clear a level's highlight, expansion and scroll.
void menu_reset_level(FlUiCtx* ctx, int level) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    m->levels[level].highlight_index = -1;
    mf->levels[level].item_count = 0;
    m->levels[level].init_highlight = true;
    m->levels[level].expanded_child_id = 0;
    m->levels[level].flip_left = false;
    m->levels[level].scroll_offset = 0.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Track the pointer position. Returns how far it moved this frame.
FlVec2 menu_track_pointer_move(FlUiCtx* ctx, UiMenuState* m) {
    FlVec2 mp = { ctx->input_state.mouse.x, ctx->input_state.mouse.y };
    FlVec2 move = { mp.x - m->last_mouse_pos.x, mp.y - m->last_mouse_pos.y };
    if (move.x != 0.0f || move.y != 0.0f) {
        m->hover_drives = true;
    }
    m->last_mouse_pos = mp;
    return move;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Close every level at or beyond new_depth.
void menu_collapse_to(UiMenuState* m, int new_depth) {
    if (new_depth < 1) {
        new_depth = 1;
    }
    if (new_depth > UI_MENU_MAX_DEPTH) {
        new_depth = UI_MENU_MAX_DEPTH;
    }
    for (int i = new_depth - 1; i < UI_MENU_MAX_DEPTH; i++) {
        m->levels[i].expanded_child_id = 0;
        if (i >= new_depth) {
            m->levels[i].scroll_offset = 0.0f;
        }
    }
    m->depth = new_depth;
    m->dwell_item_id = 0;
    m->hover_accum = 0.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// ESC: pop one level, or close the chain at the root. Returns whether the chain closed.
static bool menu_handle_cancel(FlUiCtx* ctx, UiMenuState* m) {
    if (m->depth > 1) {
        menu_collapse_to(m, m->depth - 1);
        m->hover_drives = false;
        return false;
    }
    menu_close_internal(ctx);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Collapse the path back to `level`, then expand child_id there if it is a submenu.
void menu_branch_switch(FlUiCtx* ctx, int level, u32 child_id, bool child_is_submenu) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    menu_collapse_to(m, level + 1);
    if (!child_is_submenu || child_id == 0) {
        return;
    }
    int child = level + 1;
    FL_ASSERT_MSG(child < UI_MENU_MAX_DEPTH, "menu open-path overflow (fixed cap UI_MENU_MAX_DEPTH)");
    if (child >= UI_MENU_MAX_DEPTH) {
        return;
    }
    m->levels[level].expanded_child_id = child_id;
    m->levels[child].highlight_index = -1;
    m->levels[child].init_highlight = true;
    mf->levels[child].item_count = 0;
    m->levels[child].expanded_child_id = 0;
    m->depth = child + 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Index of the row with this widget id in the level, or -1.
int menu_find_in_level(const UiMenuLevelFrame* lf, u32 id) {
    if (id == 0) {
        return -1;
    }
    int n = lf->item_count < UI_MENU_MAX_ITEMS ? lf->item_count : UI_MENU_MAX_ITEMS;
    for (int i = 0; i < n; i++) {
        if (lf->item_id[i] == id) {
            return i;
        }
    }
    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The menu row under the pointer.
MenuHit menu_find_hot_item(const FlUiCtx* ctx) {
    const UiMenuState* m = &ctx->shared->menu;

    for (int L = 0; L < m->depth && L < UI_MENU_MAX_DEPTH; L++) {
        const UiMenuLevelFrame* lvl = &ctx->menu_frame.levels[L];
        int i = menu_find_in_level(lvl, ctx->hot_id);
        if (i >= 0) {
            return (MenuHit) {
                .level = L, .index = i, .submenu = lvl->item_submenu[i], .navigable = lvl->item_navigable[i]
            };
        }
    }
    return (MenuHit) { .level = -1, .index = -1 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Whether the widget under the pointer belongs to the menu's popup scope.
static bool menu_hot_in_scope(FlUiCtx* ctx, u32 scope) {
    if (ctx->hot_id == 0) {
        return false;
    }
    ItemState* hot = hashmap_get(&ctx->shared->item_state, ctx->hot_id);
    return hot != nullptr && hot->popup_scope_id == scope;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Move the highlight to the next navigable row in `dir`, wrapping at the ends.
void menu_move_highlight(UiMenuLevel* lvl, const UiMenuLevelFrame* lf, int dir) {
    int n = lf->item_count;
    if (n > UI_MENU_MAX_ITEMS) {
        n = UI_MENU_MAX_ITEMS;
    }
    if (n <= 0) {
        return;
    }
    int idx = lvl->highlight_index;
    for (int step = 0; step < n; step++) {
        idx += dir;
        if (idx < 0) {
            idx = n - 1;
        } else if (idx >= n) {
            idx = 0;
        }
        if (lf->item_navigable[idx]) {
            lvl->highlight_index = idx;
            return;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Store a truncated copy of a row's label for type-ahead.
void menu_record_label(UiMenuLevelFrame* lf, int index, FlString label) {
    if (index < 0 || index >= UI_MENU_MAX_ITEMS) {
        return;
    }
    u32 n = label.length < UI_MENU_TYPEAHEAD_CAP ? (u32)label.length : UI_MENU_TYPEAHEAD_CAP;
    for (u32 i = 0; i < n; i++) {
        lf->item_label[index][i] = label.data[i];
    }
    lf->item_label_len[index] = (u8)n;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Move the level's highlight to the row matching this frame's typed text. Returns whether it moved.
bool menu_typeahead(FlUiCtx* ctx, int level) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    UiMenuLevel* lvl = &m->levels[level];
    const UiMenuLevelFrame* lf = &mf->levels[level];

    FlString typed = ctx->text_input_this_frame;
    if (typed.length == 0 || typed.data == nullptr) {
        return false;
    }

    int n = lf->item_count;
    if (n > UI_MENU_MAX_ITEMS) {
        n = UI_MENU_MAX_ITEMS;
    }
    if (n < 0) {
        n = 0;
    }
    FlString labels[UI_MENU_MAX_ITEMS];
    for (int i = 0; i < n; i++) {
        labels[i] = lf->item_navigable[i] ? (FlString) { .data = lf->item_label[i], .length = lf->item_label_len[i] }
                                          : (FlString) { 0 };
    }
    i32 idx = type_ahead_match(&m->typeahead, labels, (u32)n, lvl->highlight_index, typed, ctx->input_state.time,
                               UI_MENU_TYPEAHEAD_CAP);
    if (idx < 0) {
        return false;
    }
    lvl->highlight_index = idx;
    m->hover_drives = false;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Record a row's top and height and add it to the level's natural height.
void menu_record_row_geometry(UiMenuFrame* mf, int level, int index, f32 height) {
    UiMenuLevelFrame* lf = &mf->levels[level];
    if (index == 0) {
        lf->natural_height = 0.0f;
    }
    if (index >= 0 && index < UI_MENU_MAX_ITEMS) {
        lf->item_top[index] = lf->natural_height;
        lf->item_height[index] = height;
    }
    lf->natural_height += height;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Scroll geometry of a level: whether it is capped, its viewport height and its largest scroll offset.
typedef struct MenuScrollGeom {
    bool capped;
    f32 content;
    f32 viewport;
    f32 max_offset;
} MenuScrollGeom;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static MenuScrollGeom menu_scroll_geom(const FlUiCtx* ctx, const UiMenuLevelFrame* lf) {
    MenuScrollGeom g = { .content = lf->natural_height, .viewport = lf->natural_height, .max_offset = 0.0f };
    f32 scale = ctx->ui_scale > 0.0f ? ctx->ui_scale : 1.0f;
    f32 interior_cap = ctx->layout_height / scale - 2.0f * UI_MENU_VIEWPORT_MARGIN - UI_MENU_PANEL_VPAD;
    if (interior_cap > 0.0f && g.content > interior_cap) {
        g.capped = true;
        g.viewport = interior_cap;
        g.max_offset = scroll_max(g.content, g.viewport);
    }
    return g;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Configure a level's panel, height-capped and scrollable when it is over-tall.
void menu_configure_panel(FlUiCtx* ctx, int level, float min_w) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;
    const FlThemeColors* theme = ui_get_theme_colors(ctx);
    MenuScrollGeom g = menu_scroll_geom(ctx, &mf->levels[level]);
    m->levels[level].scroll_offset = clamp_f32_impl(m->levels[level].scroll_offset, 0.0f, g.max_offset);

    FlLayoutSizingAxis height = g.capped ? sizing_fixed(g.viewport + UI_MENU_PANEL_VPAD) : sizing_fit();
    FlLayoutConfig cfg = {
        .sizing = { sizing_fit(min_w), height },
        .direction = LayoutDirection_TopToBottom,
        .padding = padding_all(FlSpacing_XS),
        .color = theme->surface.elevated,
        .radius = corner_radius(FlRadius_SM),
        .border = fl_border(theme->border.default_color, FlBorderWidth_Thin),
    };
    if (g.capped) {
        f32 scale = ctx->ui_scale > 0.0f ? ctx->ui_scale : 1.0f;
        cfg.clip = (FlLayoutClip) {
            .vertical = true,
            .manual_scroll = true,
            .scroll_pos = { 0.0f, -m->levels[level].scroll_offset * scale },
        };
    }
    fl_layout_configure(cfg);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Scroll the level under the pointer with the mouse wheel.
void menu_apply_wheel(FlUiCtx* ctx) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    FlVec2 wheel = ui_get_mouse_wheel();
    MenuHit hit = menu_find_hot_item(ctx);
    if (wheel.y == 0.0f || hit.level < 0) {
        return;
    }
    MenuScrollGeom g = menu_scroll_geom(ctx, &mf->levels[hit.level]);
    if (g.max_offset <= 0.0f) {
        return;
    }
    f32 off = m->levels[hit.level].scroll_offset - wheel.y * UI_MENU_WHEEL_SPEED;
    m->levels[hit.level].scroll_offset = clamp_f32_impl(off, 0.0f, g.max_offset);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool menu_row_recorded(const UiMenuLevelFrame* lf, int i) {
    return i >= 0 && i < UI_MENU_MAX_ITEMS && i < lf->item_count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The scroll offset that brings the row fully into the window.
static f32 scroll_offset_to_show(f32 off, f32 top, f32 height, f32 viewport) {
    if (top < off) {
        return top;
    }
    f32 bottom = top + height;
    return bottom > off + viewport ? bottom - viewport : off;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Scroll the level so its highlighted row is visible.
void menu_scroll_into_view(FlUiCtx* ctx, int level) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    if (level < 0 || level >= UI_MENU_MAX_DEPTH) {
        return;
    }
    const UiMenuLevelFrame* lf = &mf->levels[level];
    MenuScrollGeom g = menu_scroll_geom(ctx, lf);
    int i = m->levels[level].highlight_index;
    if (g.max_offset <= 0.0f || !menu_row_recorded(lf, i)) {
        return;
    }
    f32 off = scroll_offset_to_show(m->levels[level].scroll_offset, lf->item_top[i], lf->item_height[i], g.viewport);
    m->levels[level].scroll_offset = clamp_f32_impl(off, 0.0f, g.max_offset);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool menu_highlight_is_submenu(const UiMenuLevel* level, const UiMenuLevelFrame* frame) {
    int hi = level->highlight_index;
    return hi >= 0 && hi < UI_MENU_MAX_ITEMS && frame->item_submenu[hi];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Move the highlight one row in `dir` when `action` fires. Returns whether it moved.
bool menu_step_highlight(FlUiCtx* ctx, int level, FlAction action, int dir) {
    if (!ui_action_pressed_or_repeated(ctx, action)) {
        return false;
    }

    menu_move_highlight(&ctx->shared->menu.levels[level], &ctx->menu_frame.levels[level], dir);
    ctx->shared->menu.hover_drives = false;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float menu_min_width(float requested) {
    return requested > 0.0f ? requested : UI_MENU_DEFAULT_MIN_WIDTH;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Close the chain on a press outside it or on ESC. A press on `pointer_on_surface` is not outside. Returns whether the
// chain closed.
bool menu_dismissed(FlUiCtx* ctx, u32 scope, bool pointer_on_surface) {
    UiMenuState* m = &ctx->shared->menu;

    if (ctx->frame_counter <= m->open_frame) {
        return false;
    }
    bool outside = !menu_hot_in_scope(ctx, scope) && !pointer_on_surface;
    if (ctx->mouse_pressed_this_frame && outside) {
        menu_close_internal(ctx);
        return true;
    }
    return ui_action_pressed(ctx, FlAction_Cancel) && menu_handle_cancel(ctx, m);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Open the chain for `scope` at depth 1 and reset the levels from `first_level` down.
void menu_arm_chain(FlUiCtx* ctx, u32 scope, int first_level) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    m->active = true;
    m->owner_scope_id = scope;
    m->open_frame = ctx->frame_counter;
    m->depth = 1;
    mf->build_level = 0;
    mf->close_requested = false;
    m->dwell_item_id = 0;
    m->hover_accum = 0.0f;
    m->typeahead = (TypeAheadState) { 0 };
    for (int i = first_level; i < UI_MENU_MAX_DEPTH; i++) {
        menu_reset_level(ctx, i);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Take the focus scope for the menu and disable the popup layer's auto-close.
void menu_arm_focus_scope(FlUiCtx* ctx, u32 scope) {
    ui_focus_scope_arm(ctx, scope, true);
    ctx->popup_auto_close = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void menu_begin_level(FlUiCtx* ctx, int level) {
    ctx->menu_frame.build_level = level;
    ctx->menu_frame.levels[level].item_count = 0;
}
