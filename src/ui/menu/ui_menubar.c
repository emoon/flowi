#include "ui_menu.h"
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
// Menu bar top row height in logical px.
#define UI_MENUBAR_HEIGHT 30.0f

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Menu bar surface: the bar is level 0 and each top's dropdown is level 1.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Open the chain for the bar, with its tops as level 0.
static void activate(FlUiCtx* ctx, u32 scope) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    menu_arm_chain(ctx, scope, 1);

    mf->levels[0] = mf->bar_tops;
    m->levels[0].expanded_child_id = 0;
    m->levels[0].flip_left = false;
    m->levels[0].scroll_offset = 0.0f;
    m->levels[0].init_highlight = false;
}

// The bar top the pointer is over: its item index (-1 when none) and whether it can be navigated to.
typedef struct BarHot {
    int index;
    bool navigable;
} BarHot;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool hot_is_actionable(BarHot hot) {
    return hot.index >= 0 && hot.navigable;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The top under the pointer, from the bar's own record.
static BarHot hot_top(const UiMenuFrame* mf, u32 hot_id) {
    int i = menu_find_in_level(&mf->bar_tops, hot_id);
    return (BarHot) { .index = i, .navigable = i >= 0 && mf->bar_tops.item_navigable[i] };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Open the top at the bar highlight, or collapse the dropdown if it is not a submenu.
static void reopen_under_highlight(FlUiCtx* ctx) {
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Open the chain when a navigable top is pressed. Returns whether the bar now owns it.
static bool try_open(FlUiCtx* ctx, u32 scope, BarHot hot) {
    if (!ctx->mouse_pressed_this_frame || !hot_is_actionable(hot)) {
        return false;
    }

    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;
    u32 top_id = mf->bar_tops.item_id[hot.index];
    activate(ctx, scope);
    menu_branch_switch(ctx, 0, top_id, true);
    m->levels[0].highlight_index = hot.index;
    m->hover_drives = true;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Close the chain on a press outside, ESC, or a press on the open top. Returns whether the bar still owns it.
static bool stay_open(FlUiCtx* ctx, u32 scope, BarHot hot) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    if (ctx->frame_counter <= m->open_frame) {
        return true;
    }

    if (menu_dismissed(ctx, scope, hot.index >= 0)) {
        return false;
    }

    bool on_open_top = ctx->mouse_pressed_this_frame && hot_is_actionable(hot)
                       && m->levels[0].expanded_child_id == mf->levels[0].item_id[hot.index];
    if (on_open_top) {
        menu_close_internal(ctx);
    }
    return !on_open_top;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Open or close the bar chain for this frame. Returns whether the bar owns it afterwards.
static bool arbitrate_open(FlUiCtx* ctx, u32 scope, BarHot hot, bool owned) {
    return owned ? stay_open(ctx, scope, hot) : try_open(ctx, scope, hot);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Switch the open top on a press, or on hovering a sibling while a dropdown is open.
static void handle_mouse(FlUiCtx* ctx, BarHot hot) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    if (!hot_is_actionable(hot)) {
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

static void step_top(FlUiCtx* ctx, int dir) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    menu_move_highlight(&m->levels[0], &mf->levels[0], dir);
    m->hover_drives = false;
    if (m->depth > 1) {
        reopen_under_highlight(ctx);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void open_highlighted_top(FlUiCtx* ctx) {
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

static void handle_input(FlUiCtx* ctx, BarHot hot) {
    UiMenuState* m = &ctx->shared->menu;

    handle_mouse(ctx, hot);

    if (ui_action_pressed_or_repeated(ctx, FlAction_Nav_Left)) {
        step_top(ctx, -1);
    }
    if (ui_action_pressed_or_repeated(ctx, FlAction_Nav_Right)) {
        step_top(ctx, +1);
    }

    bool nav_moved = false;
    if (m->depth > 1) {
        nav_moved |= menu_step_highlight(ctx, m->depth - 1, FlAction_Nav_Down, +1);
        nav_moved |= menu_step_highlight(ctx, m->depth - 1, FlAction_Nav_Up, -1);
    } else {
        open_highlighted_top(ctx);
    }

    nav_moved |= menu_typeahead(ctx, m->depth - 1);

    if (nav_moved) {
        menu_scroll_into_view(ctx, m->depth - 1);
    }
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

    if (owned) {
        mf->levels[0] = mf->bar_tops;
    }

    BarHot hot = hot_top(mf, ctx->hot_id);

    owned = arbitrate_open(ctx, scope, hot, owned);

    if (owned && ctx->frame_counter > m->open_frame) {
        handle_input(ctx, hot);
    }

    mf->build_level = 0;
    mf->bar_tops.item_count = 0;
    mf->bar_tops.natural_height = 0.0f;
    mf->close_requested = false;
    mf->z_base = cfg.z_index != 0 ? cfg.z_index : UI_Z_OVERLAY;

    if (owned) {
        menu_arm_focus_scope(ctx, scope);
        ctx->current_popup_scope_id = scope;
    }

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool bar_owns_chain(const FlUiCtx* ctx) {
    return menu_owned_by(ctx, ctx->menu_frame.menubar_scope);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Record the top into the bar's record. Returns its widget id.
static u32 record_top(FlUiCtx* ctx, FlLayoutId top_id, bool navigable) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuLevelFrame* tops = &ctx->menu_frame.bar_tops;
    int index = top_id.id;
    u32 rid = ui_layout_id_to_clay_id(top_id);

    if (index < UI_MENU_MAX_ITEMS) {
        tops->item_navigable[index] = navigable;
        tops->item_id[index] = rid;
        tops->item_submenu[index] = navigable;
        menu_record_label(tops, index, top_id.name);
    }
    bool claims_highlight = navigable && bar_owns_chain(ctx);
    if (claims_highlight && m->levels[0].init_highlight) {
        m->levels[0].highlight_index = index;
        m->levels[0].init_highlight = false;
    }
    return rid;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool top_current(const FlUiCtx* ctx, int index, bool is_open_top, bool hovered) {
    if (!bar_owns_chain(ctx)) {
        return hovered;
    }
    return ctx->shared->menu.levels[0].highlight_index == index || is_open_top;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void paint_top(FlUiCtx* ctx, FlLayoutId top_id, bool highlighted, bool disabled) {
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

// Open the dropdown panel under the top.
static void dropdown_begin(FlUiCtx* ctx, FlLayoutId top_id, float min_width) {
    UiMenuFrame* mf = &ctx->menu_frame;

    int child_level = 1;
    menu_begin_level(ctx, child_level);

    ui_popup_begin(top_id, (FlPopupConfig) { .anchor = FlPopupAnchor_Element,
                                             .z_index = mf->z_base,
                                             .capture_input = true,
                                             .auto_close = false });
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
    u32 rid = record_top(ctx, top_id, navigable);

    bool is_open_top = bar_owns_chain(ctx) && m->levels[0].expanded_child_id == rid;
    bool highlighted = navigable && top_current(ctx, index, is_open_top, signal.hovered);
    paint_top(ctx, top_id, highlighted, cfg.disabled);

    if (!is_open_top) {
        return false;
    }
    dropdown_begin(ctx, top_id, menu_min_width(cfg.min_width));
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_menubar_menu_end_impl(void) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuFrame* mf = &ctx->menu_frame;

    fl_layout_end();
    ui_popup_end();
    mf->build_level = 0;

    if (bar_owns_chain(ctx)) {
        ctx->current_popup_scope_id = mf->menubar_scope;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_menubar_end_impl(void) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuFrame* mf = &ctx->menu_frame;

    fl_layout_end();

    ctx->current_popup_scope_id = 0;

    if (mf->close_requested) {
        menu_close_internal(ctx);
    }
    mf->menubar_scope = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ui_menubar_is_open_impl(FlLayoutId bar_id) {
    FlUiCtx* ctx = ui_get_ctx();
    return menu_owned_by(ctx, ui_layout_id_to_clay_id(bar_id));
}
