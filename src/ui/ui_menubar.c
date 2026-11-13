#include "ui_menu.h"
#include "ui_menu_core.h"
#include <core/assert.h>
#include <core/math.h>
#include "external/clay/clay.h"
#include "input_actions.h"
#include "layout.h"
#include "scroll_core.h"
#include <core/string.h>
#include "ui.h"
#include "ui_color_utils.h"
#include "ui_popup.h"
#include "ui_private.h"
#include "ui_tokens.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Menu-bar top-row height in logical px: a compact strip, a touch shorter than a context-menu row.
#define UI_MENUBAR_HEIGHT 30.0f

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Menu bar - a SECOND surface over the menu core. The bar IS level 0, rendered as a persistent horizontal strip
// instead of a floating panel; each top's dropdown is level 1, anchored UNDER its bar button. State mutation goes
// through the same primitives ui_menu_begin_impl drives; the bar supplies only the per-surface open TRIGGER - a
// click, never the submenu's hover-dwell.

// Activate the bar chain on the closed -> open transition: arm the shared open path at depth 1 (bar level only, no
// dropdown yet), seeding level 0 with the bar's recorded tops so the caller can immediately branch-switch into the
// clicked one. Mirrors ui_menu_open_impl but with no point anchor (the strip is laid out, not floated) and with level 0
// taken from the bar's own record (its item identities drive the branch-switch that follows).
static void menubar_activate(FlUiCtx* ctx, u32 scope) {
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
    for (int i = 1; i < UI_MENU_MAX_DEPTH; i++) {
        menu_reset_level(ctx, i);
    }

    // Level 0 becomes the bar's tops, so only its cross-open state clears.
    mf->levels[0] = mf->bar_tops;
    m->levels[0].expanded_child_id = 0; // nothing expanded yet → the bar is the deepest level
    m->levels[0].flip_left = false;
    m->levels[0].scroll_offset = 0.0f;   // the horizontal bar strip never scrolls, but keep the state clean
    m->levels[0].init_highlight = false; // the caller sets the highlight to the clicked top explicitly
}

// The bar top the pointer is over: its item index (-1 when none) and whether it can be navigated to.
typedef struct BarHot {
    int index;
    bool navigable;
} BarHot;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Whether the pointer is over a top that a press can open or switch to.
static bool menubar_hot_is_actionable(BarHot hot) {
    return hot.index >= 0 && hot.navigable;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Resolve which top the pointer is over, scanning the bar's own prev-frame record (see menu_find_hot_item) rather
// than level 0, since this must work while the bar is passive - when level 0 holds another menu's rows and m->depth
// doesn't cover the bar.
static BarHot menubar_hot_top(const UiMenuFrame* mf, u32 hot_id) {
    BarHot none = { .index = -1 };
    if (hot_id == 0) {
        return none;
    }
    const UiMenuLevelFrame* bar = &mf->bar_tops;
    int n = bar->item_count < UI_MENU_MAX_ITEMS ? bar->item_count : UI_MENU_MAX_ITEMS;
    for (int i = 0; i < n; i++) {
        if (bar->item_id[i] == hot_id) {
            return (BarHot) { .index = i, .navigable = bar->item_navigable[i] };
        }
    }
    return none;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Re-expand whichever top the bar-level highlight now points at, so a Left/Right keyboard move switches the OPEN root
// while a dropdown is showing. A non-submenu highlight collapses the dropdown.
static void menubar_reopen_under_highlight(FlUiCtx* ctx) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    UiMenuLevel* bar = &m->levels[0];
    UiMenuLevelFrame* barf = &mf->levels[0];
    int hi = bar->highlight_index;
    if (menu_highlight_is_submenu(bar, barf)) {
        menu_branch_switch(ctx, 0, barf->item_id[hi], true);
    } else {
        menu_collapse_to(m, 1);
    }
}

// Passive: cold hover does NOTHING (the bar's per-surface trigger is click, not the submenu's hover-dwell). A press on
// a navigable top opens it - activate the chain, then branch-switch into that top (level 0 -> 1). Returns whether the
// bar now owns the chain.
static bool menubar_try_open(FlUiCtx* ctx, u32 scope, BarHot hot) {
    if (!ctx->mouse_pressed_this_frame || !menubar_hot_is_actionable(hot)) {
        return false;
    }

    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;
    u32 top_id = mf->bar_tops.item_id[hot.index];
    menubar_activate(ctx, scope);
    menu_branch_switch(ctx, 0, top_id, true);
    m->levels[0].highlight_index = hot.index;
    m->hover_drives = true;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Owned: dismissal. A press outside the bar scope closes the whole chain; ESC pops one level (dropdown -> strip, then
// strip -> closed), exactly as the context menu's ESC does at depth 1. Re-pressing the OPEN top releases the chain
// entirely (the mouse's undo of the press that opened it), rather than collapsing to the focused strip the way ESC's
// first pop does. Returns whether the bar still owns the chain.
static bool menubar_stay_open(FlUiCtx* ctx, u32 scope, BarHot hot) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    if (ctx->frame_counter <= m->open_frame) {
        return true;
    }

    if (menu_dismissed(ctx, scope, hot.index >= 0)) {
        return false;
    }

    bool on_open_top = ctx->mouse_pressed_this_frame && menubar_hot_is_actionable(hot)
                       && m->levels[0].expanded_child_id == mf->levels[0].item_id[hot.index];
    if (on_open_top) {
        menu_close_internal(ctx);
    }
    return !on_open_top;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Open/close arbitration for the bar chain. Returns whether the bar owns the menu chain after this frame's
// transitions; the caller runs no further bar handling once it returns false, since that code reads m->depth, which
// a teardown has zeroed.
static bool menubar_arbitrate_open(FlUiCtx* ctx, u32 scope, BarHot hot, bool owned) {
    return owned ? menubar_stay_open(ctx, scope, hot) : menubar_try_open(ctx, scope, hot);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Mouse: a press on a top switches the open root; hovering a sibling while a dropdown is open switches it instantly
// (no dwell).
static void menubar_handle_mouse(FlUiCtx* ctx, BarHot hot) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    if (!menubar_hot_is_actionable(hot)) {
        return;
    }

    UiMenuLevel* bar = &m->levels[0];
    u32 top_id = mf->levels[0].item_id[hot.index];
    bool pressed = ctx->mouse_pressed_this_frame;
    bool hover_switch = m->hover_drives && m->depth > 1 && bar->expanded_child_id != top_id;
    if (!pressed && !hover_switch) {
        return;
    }

    menu_branch_switch(ctx, 0, top_id, true);
    bar->highlight_index = hot.index;
    if (pressed) {
        m->hover_drives = true;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Move the bar-level highlight one top in `dir`, switching the open root when a dropdown is up.
static void menubar_step_top(FlUiCtx* ctx, int dir) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    menu_move_highlight(&m->levels[0], &mf->levels[0], dir);
    m->hover_drives = false;
    if (m->depth > 1) {
        menubar_reopen_under_highlight(ctx);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// With no dropdown open, Down / Confirm opens the highlighted top.
static void menubar_open_highlighted_top(FlUiCtx* ctx) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    int hi = m->levels[0].highlight_index;
    bool hi_top = menu_highlight_is_submenu(&m->levels[0], &mf->levels[0]);
    bool opens = ui_action_pressed(ctx, FlAction_Nav_Down) || ui_action_pressed(ctx, FlAction_Confirm);
    if (!hi_top || !opens) {
        return;
    }

    menu_branch_switch(ctx, 0, mf->levels[0].item_id[hi], true);
    m->hover_drives = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Mouse and keyboard driving of an open bar chain: root switching from the bar strip, navigation inside the open
// dropdown, type-ahead, and the wheel. Reads the prev-frame item set, so it must precede the per-frame item_count
// reset.
static void menubar_handle_input(FlUiCtx* ctx, BarHot hot) {
    UiMenuState* m = &ctx->shared->menu;

    menubar_handle_mouse(ctx, hot);

    // Keyboard: Left/Right move between tops (and switch the open root if a dropdown is up); Up/Down navigate the
    // open dropdown; with no dropdown open, Down / Confirm opens the highlighted top.
    if (ui_action_pressed_or_repeated(ctx, FlAction_Nav_Left)) {
        menubar_step_top(ctx, -1);
    }
    if (ui_action_pressed_or_repeated(ctx, FlAction_Nav_Right)) {
        menubar_step_top(ctx, +1);
    }

    // Tracks a keyboard move of the deepest (dropdown) highlight, so scroll-into-view runs only then - never
    // re-centering a wheel scroll or a stationary highlight.
    bool nav_moved = false;
    if (m->depth > 1) {
        nav_moved |= menu_step_highlight(ctx, m->depth - 1, FlAction_Nav_Down, +1);
        nav_moved |= menu_step_highlight(ctx, m->depth - 1, FlAction_Nav_Up, -1);
    } else {
        menubar_open_highlighted_top(ctx);
    }

    // Type-ahead over the deepest open level: within a dropdown when one is open, else across the bar's own tops.
    // Purely a highlight move within the current level - an open dropdown stays put (unlike Left/Right, which
    // switch the open root).
    nav_moved |= menu_typeahead(ctx, m->depth - 1);

    // Scroll the deepest open level's highlight into view when the keyboard moved it (over-tall dropdown); a no-op
    // on the horizontal bar strip and when it fits.
    if (nav_moved) {
        menu_scroll_into_view(ctx, m->depth - 1);
    }
    // Mouse wheel scrolls the hovered over-tall dropdown (prev-frame set, so before the reset below).
    menu_apply_wheel(ctx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ui_menubar_begin_impl(FlLayoutId bar_id, FlMenuBarConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;
    u32 scope = ui_layout_id_to_clay_id(bar_id);
    mf->menubar_scope = scope;
    bool owned = menu_owned_by(ctx, scope);

    menu_track_pointer_move(ctx, m);

    // An owning bar's tops are the chain's level 0, so the shared navigation helpers read them there. A passive bar
    // leaves level 0 alone: it belongs to whichever menu owns the chain.
    if (owned) {
        mf->levels[0] = mf->bar_tops;
    }

    // Which top is the pointer over (prev-frame set)? Used by both the passive click-to-open trigger and the
    // active hover-to-switch trigger.
    BarHot hot = menubar_hot_top(mf, ctx->hot_id);

    owned = menubar_arbitrate_open(ctx, scope, hot, owned);

    if (owned && ctx->frame_counter > m->open_frame) {
        menubar_handle_input(ctx, hot);
    }

    // Reset the bar's per-frame record. The highlight PERSISTS across frames; only the per-frame item counter resets
    // so the tops re-index from 0 as they emit. The strip is horizontal and never scrolls, so its natural height stays
    // 0 (menu_configure_panel is only used for the vertical dropdowns).
    mf->build_level = 0;
    mf->bar_tops.item_count = 0;
    mf->bar_tops.natural_height = 0.0f;
    mf->close_requested = false;
    mf->z_base = cfg.z_index != 0 ? cfg.z_index : UI_Z_OVERLAY;

    if (owned) {
        // Own the focus scope so the strip's tops register in-scope (click-outside membership) and the bar owns
        // arrows/ESC. The menu owns its dismissal, so the popup layer's auto-close stays disabled.
        ui_focus_scope_arm(ctx, scope, true);
        ctx->popup_auto_close = false;
        ctx->current_popup_scope_id = scope;
    }

    // The horizontal strip container (always rendered - the tops are persistent).
    const FlThemeColors* theme = ui_get_theme_colors(ctx);
    fl_layout_start(bar_id);
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_grow(), sizing_fixed(UI_MENUBAR_HEIGHT) },
        .direction = LayoutDirection_LeftToRight,
        .child_alignment = ui_align(Left, Center),
        .padding = fl_padding_hv(FlSpacing_XS, 0),
        .child_gap = FlSpacing_XS,
        .color = theme->surface.elevated,
    });
}

// Whether the bar currently owns the open chain.
static bool menubar_owned(const FlUiCtx* ctx) {
    return menu_owned_by(ctx, ctx->menu_frame.menubar_scope);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Record the top into the bar's own record (identity + navigability), like a menu row; while the bar owns the chain
// that record is its level 0, so the shared branch-switch / highlight / hot-item logic operates on it. A top behaves as
// a submenu parent: it opens a child level, never dispatches an action. Returns the top's resolved widget id.
static u32 menubar_record_top(FlUiCtx* ctx, FlLayoutId top_id, bool navigable) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuLevelFrame* tops = &ctx->menu_frame.bar_tops;
    int index = top_id.id;
    u32 rid = ui_layout_id_to_clay_id(top_id);

    if (index < UI_MENU_MAX_ITEMS) {
        tops->item_navigable[index] = navigable;
        tops->item_id[index] = rid;
        tops->item_submenu[index] = navigable;       // a navigable top is a container that opens a dropdown
        menu_record_label(tops, index, top_id.name); // type-ahead over the bar's tops as well as the dropdowns
    }
    bool claims_highlight = navigable && menubar_owned(ctx);
    if (claims_highlight && m->levels[0].init_highlight) {
        m->levels[0].highlight_index = index;
        m->levels[0].init_highlight = false;
    }
    return rid;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Visual highlight: while the bar owns the chain, the managed bar-level highlight (keyboard/hover) and the open root
// both read as "current"; while passive, a plain hover affordance.
static bool menubar_top_current(const FlUiCtx* ctx, int index, bool is_open_top, bool hovered) {
    if (!menubar_owned(ctx)) {
        return hovered;
    }
    return ctx->shared->menu.levels[0].highlight_index == index || is_open_top;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void menubar_paint_top(FlUiCtx* ctx, FlLayoutId top_id, bool highlighted, bool disabled) {
    const FlThemeColors* theme = ui_get_theme_colors(ctx);
    FlColor bg = highlighted ? theme->surface.selected : theme->surface.elevated;
    FlColor txt = disabled ? theme->text.secondary : theme->text.primary;

    ui_layout_area_id(top_id, .sizing = { sizing_fit(), sizing_fixed(UI_MENUBAR_HEIGHT) },
                      .direction = LayoutDirection_LeftToRight, .child_alignment = ui_align(Center, Center),
                      .padding = fl_padding_hv(FlSpacing_SM, FlSpacing_XS), .radius = corner_radius(FlRadius_SM),
                      .color = bg) {
        ui_text_styled_impl(top_id.name,
                            (FlTextConfig) { .color = txt, .valign = FlTextVAlign_Center, .no_wrap = true });
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The dropdown is level 1: a floating panel anchored UNDER the bar button (element-anchor, so the popup core opens it
// below the top). It reuses the menu scope (ui_popup_begin inherits the armed popup_focus_scope_id), so its items
// register into the one shared scope.
static void menubar_dropdown_begin(FlUiCtx* ctx, FlLayoutId top_id, float min_width) {
    UiMenuFrame* mf = &ctx->menu_frame;

    int child_level = 1;
    mf->build_level = child_level;
    mf->levels[child_level].item_count = 0;

    ui_popup_begin(top_id, (FlPopupConfig) { .anchor = FlPopupAnchor_Element,
                                             .z_index = mf->z_base,
                                             .capture_input = true,
                                             .auto_close = false });
    // Dropdown panel (height-capped + scrollable when the dropdown is over-tall; sizing_fit otherwise).
    fl_layout_start_auto();
    menu_configure_panel(ctx, child_level, min_width);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ui_menubar_menu_begin_impl(FlString label, FlMenuBarMenuConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    int index = mf->bar_tops.item_count++;
    FlLayoutId top_id = { .name = label, .id = index };
    FlSignal signal = ui_item_behavior(top_id, FlItemFlags_clickable, (FlWidgetConfig) { 0 });

    bool navigable = !cfg.disabled;
    u32 rid = menubar_record_top(ctx, top_id, navigable);

    bool is_open_top = menubar_owned(ctx) && m->levels[0].expanded_child_id == rid;
    bool highlighted = navigable && menubar_top_current(ctx, index, is_open_top, signal.hovered);
    menubar_paint_top(ctx, top_id, highlighted, cfg.disabled);

    if (!is_open_top) {
        return false;
    }
    menubar_dropdown_begin(ctx, top_id, menu_min_width(cfg.min_width));
    return true;
}

void ui_menubar_menu_end_impl(void) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuFrame* mf = &ctx->menu_frame;

    fl_layout_end(); // close the dropdown panel container
    ui_popup_end();  // close the floating element (clears current_popup_scope_id)
    mf->build_level = 0;

    // ui_popup_end zeroed current_popup_scope_id; restore it so tops emitted AFTER the open one still register in the
    // bar scope (click-outside membership / hover-switch).
    if (menubar_owned(ctx)) {
        ctx->current_popup_scope_id = mf->menubar_scope;
    }
}

void ui_menubar_end_impl(void) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuFrame* mf = &ctx->menu_frame;

    fl_layout_end(); // close the strip container

    // Disarm the render-time scope the bar armed at begin (and re-armed after each open dropdown). It covers the bar
    // block only: left set, every widget emitted for the rest of the frame would register as a member of the bar's
    // popup scope, and the click-outside test would answer yes for the whole app.
    ctx->current_popup_scope_id = 0;

    // A dropdown action item that asked to close tears the whole chain down AFTER the strip and its dropdown have
    // rendered once more this frame - same deferred teardown as the context menu.
    if (mf->close_requested) {
        menu_close_internal(ctx);
    }
    mf->menubar_scope = 0;
}

bool ui_menubar_is_open_impl(FlLayoutId bar_id) {
    FlUiCtx* ctx = ui_get_ctx();
    return menu_owned_by(ctx, ui_layout_id_to_clay_id(bar_id));
}
