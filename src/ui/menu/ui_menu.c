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

// Defaults

// Menu row height in logical px.
#define UI_MENU_ITEM_HEIGHT 36.0f
// Separator row height in logical px.
#define UI_MENU_SEPARATOR_HEIGHT 9.0f
// Section header row height in logical px.
#define UI_MENU_HEADER_HEIGHT 28.0f
// Width of the check/radio gutter in logical px.
#define UI_MENU_MARKER_WIDTH 18.0f
// Size of the check/radio indicator in logical px.
#define UI_MENU_MARKER_DOT 10.0f
// Highlight strength (0 resting, 1 selected) for a parent on the open path and for a disabled row under the pointer.
#define UI_MENU_TRAIL_HIGHLIGHT 0.6f
#define UI_MENU_DISABLED_HIGHLIGHT 0.25f

// Internal helpers

// Leading mark of a stateful row.
typedef enum MenuMarker {
    MenuMarker_None = 0,
    MenuMarker_Check,
    MenuMarker_Radio,
} MenuMarker;

// Whether activating an item closes the chain.
static bool policy_closes(FlMenuClosePolicy policy, bool kind_default_close) {
    switch (policy) {
        case FlMenuClosePolicy_Close:
            return true;
        case FlMenuClosePolicy_KeepOpen:
            return false;
        case FlMenuClosePolicy_Default:
        default:
            return kind_default_close;
    }
}

FlColor ui_menu_blend(FlColor a, FlColor b, float t) {
    float it = 1.0f - t;
    return (FlColor) {
        (u8)(a.r * it + b.r * t),
        (u8)(a.g * it + b.g * t),
        (u8)(a.b * it + b.b * t),
        (u8)(a.a * it + b.a * t),
    };
}

// Open-path state

void ui_menu_open_impl(FlLayoutId menu_id, FlVec2 point) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuState* m = &ctx->shared->menu;
    u32 scope = ui_layout_id_to_clay_id(menu_id);

    if (menu_owned_by(ctx, scope)) {
        return;
    }

    menu_arm_chain(ctx, scope, 0);
    m->anchor = point;

    m->hover_drives = false;
    m->last_mouse_pos = (FlVec2) { ctx->input_state.mouse.x, ctx->input_state.mouse.y };
}

void ui_menu_close_impl(FlLayoutId menu_id) {
    FlUiCtx* ctx = ui_get_ctx();
    if (menu_owned_by(ctx, ui_layout_id_to_clay_id(menu_id))) {
        menu_close_internal(ctx);
    }
}

bool ui_menu_is_open_impl(FlLayoutId menu_id) {
    FlUiCtx* ctx = ui_get_ctx();
    return menu_owned_by(ctx, ui_layout_id_to_clay_id(menu_id));
}

// Row emission (shared by leaf items and submenu parents)

typedef struct MenuRow {
    FlSignal signal;
    u32 id;
    FlLayoutId layout;
    int level;
    int index;
    bool highlighted;
} MenuRow;

typedef struct MenuRowSpec {
    FlString label;
    FlString shortcut;
    bool disabled;
    bool is_submenu;
    MenuMarker marker;
    bool marked;
} MenuRowSpec;

// Record the row into its level; the first navigable row claims the initial highlight.
static void record_row(FlUiCtx* ctx, const MenuRow* row, const MenuRowSpec* spec) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuLevelFrame* lf = &ctx->menu_frame.levels[row->level];

    bool navigable = !spec->disabled;
    if (row->index < UI_MENU_MAX_ITEMS) {
        lf->item_navigable[row->index] = navigable;
        lf->item_id[row->index] = row->id;
        lf->item_submenu[row->index] = spec->is_submenu;
        menu_record_label(lf, row->index, spec->label);
    }
    if (navigable && m->levels[row->level].init_highlight) {
        m->levels[row->level].highlight_index = row->index;
        m->levels[row->level].init_highlight = false;
    }
}

static FlColor row_background(const FlThemeColors* theme, bool highlighted, bool is_deepest, bool disabled) {
    if (!highlighted) {
        return theme->surface.elevated;
    }

    float t = disabled ? UI_MENU_DISABLED_HIGHLIGHT : (is_deepest ? 1.0f : UI_MENU_TRAIL_HIGHLIGHT);
    return ui_menu_blend(theme->surface.elevated, theme->surface.selected, t);
}

static void emit_marker(const FlThemeColors* theme, MenuMarker marker, bool marked) {
    float r = marker == MenuMarker_Radio ? (float)FlRadius_Full : (float)FlRadius_SM;
    ui_layout_area(.sizing = { sizing_fixed(UI_MENU_MARKER_WIDTH), sizing_fixed(UI_MENU_ITEM_HEIGHT) },
                   .direction = LayoutDirection_TopToBottom, .child_alignment = ui_align(Center, Center)) {
        if (marked) {
            ui_layout_area(.sizing = { sizing_fixed(UI_MENU_MARKER_DOT), sizing_fixed(UI_MENU_MARKER_DOT) },
                           .color = theme->interactive.base, .radius = corner_radius(r)) {}
        }
    }
}

// Emit the chevron (submenu) or the shortcut text at the trailing edge.
static void emit_trailer(const FlThemeColors* theme, const MenuRowSpec* row) {
    if (!row->is_submenu && row->shortcut.length == 0) {
        return;
    }

    ui_layout_area(.sizing = { sizing_grow(), sizing_fit() }) {}
    ui_text_styled_impl(
        row->is_submenu ? FL_S(">") : row->shortcut,
        (FlTextConfig) { .color = theme->text.secondary, .valign = FlTextVAlign_Center, .no_wrap = true });
}

// Claim the next row index at the current build level, register its widget and record it.
static MenuRow register_row(FlUiCtx* ctx, const MenuRowSpec* spec, f32 height, FlItemFlags flags) {
    UiMenuFrame* mf = &ctx->menu_frame;

    int level = mf->build_level;
    int index = mf->levels[level].item_count++;
    menu_record_row_geometry(mf, level, index, height);

    FlLayoutId layout = { .name = spec->label, .id = ui_menu_row_id_seed(level, index) };
    MenuRow row = { .id = ui_layout_id_to_clay_id(layout), .layout = layout, .level = level, .index = index };
    row.signal = ui_item_behavior(layout, flags, (FlWidgetConfig) { 0 });
    record_row(ctx, &row, spec);
    return row;
}

static MenuRow emit_row(FlUiCtx* ctx, MenuRowSpec spec) {
    UiMenuState* m = &ctx->shared->menu;

    const FlThemeColors* theme = ui_get_theme_colors(ctx);
    MenuRow row = register_row(ctx, &spec, UI_MENU_ITEM_HEIGHT,
                               FlItemFlags_clickable | FlItemFlags_focusable | FlItemFlags_scroll_target);
    if (spec.disabled) {
        row.signal.clicked = false;
        row.signal.activated = false;
        row.signal.right_clicked = false;
    }
    if (row.signal.hovered && m->hover_drives) {
        m->levels[row.level].highlight_index = row.index;
    }

    row.highlighted = m->levels[row.level].highlight_index == row.index;
    bool is_deepest = (row.level == m->depth - 1);
    FlColor bg = row_background(theme, row.highlighted, is_deepest, spec.disabled);
    FlColor txt = spec.disabled ? theme->text.secondary : theme->text.primary;

    ui_layout_area_id(row.layout, .sizing = { sizing_grow(), sizing_fixed(UI_MENU_ITEM_HEIGHT) },
                      .direction = LayoutDirection_LeftToRight, .child_alignment = ui_align(Left, Center),
                      .padding = fl_padding_hv(FlSpacing_SM, FlSpacing_XS), .child_gap = FlSpacing_MD,
                      .radius = corner_radius(FlRadius_SM), .color = bg) {
        if (spec.marker != MenuMarker_None) {
            emit_marker(theme, spec.marker, spec.marked);
        }
        ui_text_styled_impl(spec.label,
                            (FlTextConfig) { .color = txt, .valign = FlTextVAlign_Center, .no_wrap = true });
        emit_trailer(theme, &spec);
    }

    return row;
}

static bool submenu_flip_left(bool prefer_left, Clay_BoundingBox parent, float cw, float sw) {
    bool right_fits = (cw <= 0.0f) || (parent.x + parent.width + cw <= sw);
    bool left_fits = (cw <= 0.0f) || (parent.x - cw >= 0.0f);
    if (prefer_left) {
        return left_fits || !right_fits;
    }
    return !right_fits && left_fits;
}

static float submenu_place_x(Clay_BoundingBox parent, float cw, float sw, bool flip_left) {
    float x = flip_left ? (parent.x - cw) : (parent.x + parent.width);
    if (cw > 0.0f) {
        x = fmaxf(fminf(x, sw - cw), 0.0f);
    }
    return x;
}

static float submenu_place_y(Clay_BoundingBox parent, float ch, float sh) {
    if (ch <= 0.0f || parent.y + ch <= sh) {
        return parent.y;
    }

    float y_up = parent.y + parent.height - ch;
    return fmaxf((y_up >= 0.0f) ? y_up : (sh - ch), 0.0f);
}

// Open the floating panel for a submenu beside its parent row.
static void submenu_panel_begin(FlUiCtx* ctx, u32 parent_item_id, int parent_level, float min_width) {
    UiMenuState* m = &ctx->shared->menu;
    int child_level = parent_level + 1;
    Clay_BoundingBox parent = Clay_GetElementData((Clay_ElementId) { .id = parent_item_id }).boundingBox;

    u32 panel_clay_id = parent_item_id * 2654435761u + 0x9E3779B9u;
    Clay_ElementId panel_id = { .id = panel_clay_id };

    Clay_ElementData prev = Clay_GetElementData(panel_id);
    float cw = prev.found ? prev.boundingBox.width : 0.0f;
    float ch = prev.found ? prev.boundingBox.height : 0.0f;

    bool flip_left = submenu_flip_left(m->levels[parent_level].flip_left, parent, cw, ctx->layout_width);
    m->levels[child_level].flip_left = flip_left;
    float x = submenu_place_x(parent, cw, ctx->layout_width, flip_left);
    float y = submenu_place_y(parent, ch, ctx->layout_height);

    Clay__OpenElementWithId(panel_id);
    Clay_ElementDeclaration decl = {
        .layout
        = { .sizing = { .width = { .type = CLAY__SIZING_TYPE_FIT }, .height = { .type = CLAY__SIZING_TYPE_FIT } } },
        .floating = { .attachTo = CLAY_ATTACH_TO_ROOT,
                      .attachPoints = { .element = CLAY_ATTACH_POINT_LEFT_TOP, .parent = CLAY_ATTACH_POINT_LEFT_TOP },
                      .offset = { .x = x, .y = y },
                      .zIndex = (i16)(ctx->menu_frame.z_base + child_level),
                      .pointerCaptureMode = CLAY_POINTER_CAPTURE_MODE_CAPTURE },
    };
    Clay__ConfigureOpenElement(decl);

    fl_layout_start_auto();
    menu_configure_panel(ctx, child_level, min_width);
}

// Builder

static int deepest_level(const UiMenuState* m) {
    return m->depth > 0 ? m->depth - 1 : 0;
}

// Left: collapse one level.
static void key_ascend(FlUiCtx* ctx) {
    UiMenuState* m = &ctx->shared->menu;

    if (ui_action_pressed(ctx, FlAction_Nav_Left) && m->depth > 1) {
        menu_collapse_to(m, m->depth - 1);
        m->hover_drives = false;
    }
}

// Right, or Confirm on a submenu row: open the highlighted submenu. Returns whether it opened.
static bool key_descend(FlUiCtx* ctx, int level) {
    UiMenuState* m = &ctx->shared->menu;
    const UiMenuLevelFrame* lf = &ctx->menu_frame.levels[level];

    if (!menu_highlight_is_submenu(&m->levels[level], lf)) {
        return false;
    }
    if (!ui_action_pressed(ctx, FlAction_Nav_Right) && !ui_action_pressed(ctx, FlAction_Confirm)) {
        return false;
    }
    menu_branch_switch(ctx, level, lf->item_id[m->levels[level].highlight_index], true);
    m->hover_drives = false;
    return true;
}

// Keyboard navigation for the deepest open level.
static void handle_keyboard(FlUiCtx* ctx) {
    UiMenuState* m = &ctx->shared->menu;

    if (ctx->frame_counter <= m->open_frame) {
        return;
    }

    int lvl = deepest_level(m);
    bool nav_moved = menu_step_highlight(ctx, lvl, FlAction_Nav_Up, -1);
    nav_moved |= menu_step_highlight(ctx, lvl, FlAction_Nav_Down, +1);

    key_ascend(ctx);
    lvl = deepest_level(m);
    if (key_descend(ctx, lvl)) {
        lvl = deepest_level(m);
        nav_moved = true;
    }

    nav_moved |= menu_typeahead(ctx, lvl);

    if (nav_moved) {
        menu_scroll_into_view(ctx, lvl);
    }
}

static void dwell_reset(UiMenuState* m) {
    m->dwell_item_id = 0;
    m->hover_accum = 0.0f;
}

static void dwell_tick(FlUiCtx* ctx) {
    UiMenuState* m = &ctx->shared->menu;

    if (m->dwell_item_id != ctx->hot_id) {
        m->dwell_item_id = ctx->hot_id;
        m->hover_accum = 0.0f;
    }
    m->hover_accum += ctx->delta_time;
}

// Switch to the hovered sibling after the grace period, or at once when the pointer moves away from the open submenu.
static void dwell_switch_sibling(FlUiCtx* ctx, MenuHit hit, float move_dx) {
    const UiMenuState* m = &ctx->shared->menu;

    int child_lvl = hit.level + 1 < UI_MENU_MAX_DEPTH ? hit.level + 1 : hit.level;
    float dir = m->levels[child_lvl].flip_left ? -1.0f : 1.0f;
    bool moving_away = (move_dx * dir) < 0.0f;
    if (moving_away || m->hover_accum >= UI_MENU_BRANCH_GRACE_SECONDS) {
        menu_branch_switch(ctx, hit.level, ctx->hot_id, hit.submenu);
    }
}

// Hover-driven opening and switching of submenus.
static void handle_dwell(FlUiCtx* ctx, float move_dx) {
    UiMenuState* m = &ctx->shared->menu;

    MenuHit hit = menu_find_hot_item(ctx);
    u32 cur_child = hit.level >= 0 ? m->levels[hit.level].expanded_child_id : 0;
    bool inert = hit.level < 0 || !hit.navigable;
    if (inert || ctx->hot_id == cur_child) {
        dwell_reset(m);
        return;
    }

    dwell_tick(ctx);
    if (cur_child != 0) {
        dwell_switch_sibling(ctx, hit, move_dx);
    } else if (hit.submenu && m->hover_accum >= UI_MENU_DWELL_SECONDS) {
        menu_branch_switch(ctx, hit.level, ctx->hot_id, true);
    }
}

static void handle_input(FlUiCtx* ctx) {
    UiMenuState* m = &ctx->shared->menu;

    handle_keyboard(ctx);

    float move_dx = menu_track_pointer_move(ctx, m).x;

    if (ctx->frame_counter > m->open_frame && m->hover_drives) {
        handle_dwell(ctx, move_dx);
    }

    menu_apply_wheel(ctx);
}

bool ui_menu_begin_impl(FlLayoutId menu_id, FlMenuConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;
    u32 scope = ui_layout_id_to_clay_id(menu_id);

    if (!menu_owned_by(ctx, scope) || menu_dismissed(ctx, scope, false)) {
        return false;
    }

    menu_arm_focus_scope(ctx, scope);

    handle_input(ctx);

    mf->build_level = 0;
    mf->levels[0].item_count = 0;
    mf->close_requested = false;

    i16 z = cfg.z_index != 0 ? cfg.z_index : UI_Z_OVERLAY;
    mf->z_base = z;

    ui_popup_begin(menu_id, (FlPopupConfig) { .anchor = FlPopupAnchor_Point,
                                              .point = m->anchor,
                                              .z_index = z,
                                              .capture_input = true,
                                              .auto_close = false });

    fl_layout_start_auto();
    menu_configure_panel(ctx, 0, menu_min_width(cfg.min_width));

    return true;
}

// Apply keyboard activation and the close policy to a leaf row. Returns its signal.
static FlSignal finish_leaf(FlUiCtx* ctx, MenuRow row, FlMenuItemConfig cfg, bool kind_default_close) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    FlSignal signal = row.signal;

    bool can_confirm = row.highlighted && row.level == m->depth - 1 && ctx->frame_counter > m->open_frame;
    if (can_confirm && ui_action_pressed(ctx, FlAction_Confirm)) {
        signal.clicked = true;
        signal.activated = true;
    }

    bool activated = !cfg.disabled && signal.clicked;
    if (activated && policy_closes(cfg.close_policy, kind_default_close)) {
        mf->close_requested = true;
    }

    return signal;
}

FlSignal ui_menu_item_impl(FlString label, FlMenuItemConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();
    MenuRow row = emit_row(ctx, (MenuRowSpec) { .label = label, .shortcut = cfg.shortcut, .disabled = cfg.disabled });
    return finish_leaf(ctx, row, cfg, /*kind_default_close=*/true);
}

FlSignal ui_menu_check_impl(FlString label, bool* checked, FlMenuItemConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();
    bool marked = checked != nullptr && *checked;
    MenuRow row = emit_row(ctx, (MenuRowSpec) { .label = label,
                                                .shortcut = cfg.shortcut,
                                                .disabled = cfg.disabled,
                                                .marker = MenuMarker_Check,
                                                .marked = marked });
    FlSignal signal = finish_leaf(ctx, row, cfg, /*kind_default_close=*/false);

    if (signal.clicked && checked != nullptr) {
        *checked = !*checked;
    }
    return signal;
}

FlSignal ui_menu_radio_impl(FlString label, int* selected, int value, FlMenuItemConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();
    bool marked = selected != nullptr && *selected == value;
    MenuRow row = emit_row(ctx, (MenuRowSpec) { .label = label,
                                                .shortcut = cfg.shortcut,
                                                .disabled = cfg.disabled,
                                                .marker = MenuMarker_Radio,
                                                .marked = marked });
    FlSignal signal = finish_leaf(ctx, row, cfg, /*kind_default_close=*/false);

    if (signal.clicked && selected != nullptr) {
        *selected = value;
    }
    return signal;
}

// Static (non-interactive) item kinds - separator, section header

// Register a separator or header row. Returns its layout id.
static FlLayoutId record_static_row(FlUiCtx* ctx, FlString name, f32 row_height) {
    MenuRowSpec spec = { .label = name, .disabled = true };
    return register_row(ctx, &spec, row_height, FlItemFlags_scroll_target).layout;
}

void ui_menu_separator_impl(void) {
    FlUiCtx* ctx = ui_get_ctx();
    const FlThemeColors* theme = ui_get_theme_colors(ctx);

    FlLayoutId item_id = record_static_row(ctx, FL_S("##menu_sep"), UI_MENU_SEPARATOR_HEIGHT);

    ui_layout_area_id(item_id, .sizing = { sizing_grow(), sizing_fixed(UI_MENU_SEPARATOR_HEIGHT) },
                      .direction = LayoutDirection_TopToBottom, .child_alignment = ui_align(Center, Center),
                      .padding = fl_padding_hv(FlSpacing_SM, 0), .color = theme->surface.elevated) {
        ui_layout_area(.sizing = { sizing_grow(), sizing_fixed(1.0f) }, .color = theme->border.separator) {}
    }
}

void ui_menu_header_impl(FlString label) {
    FlUiCtx* ctx = ui_get_ctx();
    const FlThemeColors* theme = ui_get_theme_colors(ctx);

    FlLayoutId item_id = record_static_row(ctx, label, UI_MENU_HEADER_HEIGHT);

    ui_layout_area_id(item_id, .sizing = { sizing_grow(), sizing_fixed(UI_MENU_HEADER_HEIGHT) },
                      .direction = LayoutDirection_LeftToRight, .child_alignment = ui_align(Left, Center),
                      .padding = fl_padding_hv(FlSpacing_SM, FlSpacing_XS), .color = theme->surface.elevated) {
        ui_text_styled_impl(
            label, (FlTextConfig) { .color = theme->text.secondary, .valign = FlTextVAlign_Center, .no_wrap = true });
    }
}

bool ui_submenu_begin_impl(FlString label, FlSubmenuConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    int parent_level = mf->build_level;

    MenuRow row = emit_row(ctx, (MenuRowSpec) { .label = label, .disabled = cfg.disabled, .is_submenu = true });

    bool is_expanded = m->levels[parent_level].expanded_child_id == row.id;
    bool clicked_open = !cfg.disabled && row.signal.clicked;
    if (clicked_open && !is_expanded) {
        menu_branch_switch(ctx, parent_level, row.id, true);
        is_expanded = true;
    }

    if (cfg.disabled || !is_expanded) {
        return false;
    }

    int child_level = parent_level + 1;
    FL_ASSERT_MSG(child_level < UI_MENU_MAX_DEPTH, "menu open-path overflow (fixed cap UI_MENU_MAX_DEPTH)");
    if (child_level >= UI_MENU_MAX_DEPTH) {
        return false;
    }

    menu_begin_level(ctx, child_level);

    submenu_panel_begin(ctx, row.id, parent_level, menu_min_width(cfg.min_width));
    return true;
}

void ui_submenu_end_impl(void) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuFrame* mf = &ctx->menu_frame;

    fl_layout_end();
    Clay__CloseElement();

    if (mf->build_level > 0) {
        mf->build_level--;
    }
}

void ui_menu_end_impl(void) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuFrame* mf = &ctx->menu_frame;

    fl_layout_end();
    ui_popup_end();

    if (mf->close_requested) {
        menu_close_internal(ctx);
    }
}

// Right-click region

// Whether the point is inside the region's layout box from the previous frame.
static bool region_contains(FlLayoutId region_id, FlVec2 point) {
    Clay_ElementData ed = Clay_GetElementData((Clay_ElementId) { .id = ui_layout_id_to_clay_id(region_id) });
    Clay_BoundingBox b = ed.boundingBox;
    return ed.found && point.x >= b.x && point.x < b.x + b.width && point.y >= b.y && point.y < b.y + b.height;
}

// Grow in both axes when the caller left the sizing unset.
static FlLayoutSizing region_sizing(FlLayoutSizing requested) {
    bool unset = requested.width.type == FlLayoutSizingType_Fit && requested.width.size.min_max.max == 0.0f
                 && requested.height.type == FlLayoutSizingType_Fit && requested.height.size.min_max.max == 0.0f;
    return unset ? (FlLayoutSizing) { sizing_grow(), sizing_grow() } : requested;
}

FlSignal ui_menu_region_begin_impl(FlLayoutId region_id, FlLayoutId menu_id, FlMenuRegionConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();

    FlSignal signal
        = ui_item_behavior(region_id, FlItemFlags_clickable | FlItemFlags_right_click, (FlWidgetConfig) { 0 });

    FlVec2 mouse = { ctx->input_state.mouse.x, ctx->input_state.mouse.y };
    if (ctx->input_state.mouse.buttons[FlMouseButton_Right] && region_contains(region_id, mouse)) {
        ui_menu_open_impl(menu_id, mouse);
    }

    fl_layout_start(region_id);
    fl_layout_configure((FlLayoutConfig) { .sizing = region_sizing(cfg.sizing) });

    return signal;
}

void ui_menu_region_end_impl(void) {
    fl_layout_end();
}
