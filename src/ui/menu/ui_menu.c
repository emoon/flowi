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
// Defaults

// Per-item row height in logical px (compact menu row, smaller than a standard button).
#define UI_MENU_ITEM_HEIGHT 36.0f
// Separator row height: a thin rule centered in a short row (the row gives the rule vertical breathing room).
#define UI_MENU_SEPARATOR_HEIGHT 9.0f
// Section-header caption row height: shorter than an action row, no hover affordance.
#define UI_MENU_HEADER_HEIGHT 28.0f
// Leading marker gutter (logical px) for checkable / radio rows: a fixed-width cell before the label so a row keeps
// the label aligned whether or not its mark is showing.
#define UI_MENU_MARKER_WIDTH 18.0f
// Size of the painted indicator inside the gutter (a check square or a radio dot, both in the accent color).
#define UI_MENU_MARKER_DOT 10.0f
// Highlight strength (0 = the resting surface, 1 = the full selected color) for the two rows that are current but
// not activatable: a parent on the open path (trail), and a disabled row the pointer is merely over.
#define UI_MENU_TRAIL_HIGHLIGHT 0.6f
#define UI_MENU_DISABLED_HIGHLIGHT 0.25f

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal helpers

// Leading mark a stateful row paints in its gutter. None reserves no gutter at all; Check / Radio reserve the fixed
// gutter and paint the indicator only while marked.
typedef enum MenuMarker {
    MenuMarker_None = 0, // no gutter (action item, submenu parent)
    MenuMarker_Check,    // a check square in the gutter when marked (checkable)
    MenuMarker_Radio,    // a round dot in the gutter when marked (radio)
} MenuMarker;

// Resolve whether activating an item closes the chain, from the per-item policy and the item kind's natural default
// (action: close; checkable/radio: keep open). The two explicit policies force the exception in either direction.
static bool menu_policy_closes(FlMenuClosePolicy policy, bool kind_default_close) {
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Linear blend of two colors, truncating each channel.
FlColor ui_menu_blend(FlColor a, FlColor b, float t) {
    float it = 1.0f - t;
    return (FlColor) {
        (u8)(a.r * it + b.r * t),
        (u8)(a.g * it + b.g * t),
        (u8)(a.b * it + b.b * t),
        (u8)(a.a * it + b.a * t),
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Open-path state

void ui_menu_open_impl(FlLayoutId menu_id, FlVec2 point) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuState* m = &ctx->shared->menu;
    u32 scope = ui_layout_id_to_clay_id(menu_id);

    // Capture the cursor only on the closed -> open transition. The right-click that opens the menu is
    // level-triggered (true every frame the button is held), so re-anchoring here each frame would make the menu
    // live-follow the pointer while the button is held.
    if (menu_owned_by(ctx, scope)) {
        return;
    }

    menu_arm_chain(ctx, scope, 0);
    m->anchor = point;

    // Seed the last-input-wins latch so a pointer merely sitting over a row at open time does not own the
    // highlight until it actually moves.
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Row emission (shared by leaf items and submenu parents)

typedef struct MenuRow {
    FlSignal signal;   // the row's interaction signal (a submenu parent's is never an action - caller ignores it)
    u32 id;            // resolved widget id of the row
    FlLayoutId layout; // the row's layout id, for painting it
    int level;         // build level the row was emitted at
    int index;         // per-level item index
    bool highlighted;
} MenuRow;

// What a row shows: its label, an optional leading marker, and either a trailing shortcut (leaf) or a chevron
// (submenu parent).
typedef struct MenuRowSpec {
    FlString label;
    FlString shortcut;
    bool disabled;
    bool is_submenu;
    MenuMarker marker;
    bool marked;
} MenuRowSpec;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Record the row's identity (id + submenu flag) and navigability into its level, and let the first navigable row claim
// the initial highlight on open.
static void menu_record_row(FlUiCtx* ctx, const MenuRow* row, const MenuRowSpec* spec) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuLevelFrame* lf = &ctx->menu_frame.levels[row->level];

    bool navigable = !spec->disabled;
    if (row->index < UI_MENU_MAX_ITEMS) {
        lf->item_navigable[row->index] = navigable;
        lf->item_id[row->index] = row->id; // identity-keyed open path: maps hot_id -> this row
        lf->item_submenu[row->index] = spec->is_submenu;
        menu_record_label(lf, row->index, spec->label); // type-ahead prefix source (matched at the next begin)
    }
    if (navigable && m->levels[row->level].init_highlight) {
        m->levels[row->level].highlight_index = row->index;
        m->levels[row->level].init_highlight = false;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Every row paints a background (the menu surface by default, the highlight when current). Beyond the look, a painted
// row is what makes the item hittable: the hot/hover path is driven by render commands, so a fully transparent row
// would never report hover.
static FlColor menu_row_background(const FlThemeColors* theme, bool highlighted, bool is_deepest, bool disabled) {
    if (!highlighted) {
        return theme->surface.elevated;
    }

    float t = disabled ? UI_MENU_DISABLED_HIGHLIGHT : (is_deepest ? 1.0f : UI_MENU_TRAIL_HIGHLIGHT);
    return ui_menu_blend(theme->surface.elevated, theme->surface.selected, t);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The gutter marker: a near-square for a check, a full-radius dot for a radio bullet.
static void menu_emit_marker(const FlThemeColors* theme, MenuMarker marker, bool marked) {
    float r = marker == MenuMarker_Radio ? (float)FlRadius_Full : (float)FlRadius_SM;
    ui_layout_area(.sizing = { sizing_fixed(UI_MENU_MARKER_WIDTH), sizing_fixed(UI_MENU_ITEM_HEIGHT) },
                   .direction = LayoutDirection_TopToBottom, .child_alignment = ui_align(Center, Center)) {
        if (marked) {
            ui_layout_area(.sizing = { sizing_fixed(UI_MENU_MARKER_DOT), sizing_fixed(UI_MENU_MARKER_DOT) },
                           .color = theme->interactive.base, .radius = corner_radius(r)) {}
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A grow spacer pushes the trailing text (chevron for a submenu parent, else the shortcut) to the trailing edge.
static void menu_emit_trailer(const FlThemeColors* theme, const MenuRowSpec* row) {
    if (!row->is_submenu && row->shortcut.length == 0) {
        return;
    }

    ui_layout_area(.sizing = { sizing_grow(), sizing_fit() }) {}
    ui_text_styled_impl(
        row->is_submenu ? FL_S(">") : row->shortcut,
        (FlTextConfig) { .color = theme->text.secondary, .valign = FlTextVAlign_Center, .no_wrap = true });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Claim the next index at the current build level, register the row's widget and record it into the level - the one
// place a row (interactive or static) enters the open path. `flags` choose its interactions: scroll_target marks the
// row as owning wheel input while it is hot, so ui_update_begin withholds the wheel from Clay's auto-scroll and
// leaves it for the menu's manual per-level scroll (menu_apply_wheel).
static MenuRow menu_register_row(FlUiCtx* ctx, const MenuRowSpec* spec, f32 height, FlItemFlags flags) {
    UiMenuFrame* mf = &ctx->menu_frame;

    int level = mf->build_level;
    int index = mf->levels[level].item_count++;
    menu_record_row_geometry(mf, level, index, height); // scroll geometry for an over-tall level

    // Seed the id with (level, index): index disambiguates same-label rows within a level, and folding the build
    // level in keeps a parent-level row and an open submenu row from colliding when their label and in-level index
    // line up. Only one menu chain is open at a time, so cross-menu id reuse can't collide.
    FlLayoutId layout = { .name = spec->label, .id = ui_menu_row_id_seed(level, index) };
    MenuRow row = { .id = ui_layout_id_to_clay_id(layout), .layout = layout, .level = level, .index = index };
    row.signal = ui_item_behavior(layout, flags, (FlWidgetConfig) { 0 });
    menu_record_row(ctx, &row, spec);
    return row;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Emit one menu row at the current build level: register the widget, record it into the level, apply the shared hover
// highlight (last-input-wins), and paint the row - label on the left, plus a right-aligned shortcut (leaf) or a
// chevron (submenu parent).
static MenuRow menu_emit_row(FlUiCtx* ctx, MenuRowSpec spec) {
    UiMenuState* m = &ctx->shared->menu;

    const FlThemeColors* theme = ui_get_theme_colors(ctx);
    MenuRow row = menu_register_row(ctx, &spec, UI_MENU_ITEM_HEIGHT,
                                    FlItemFlags_clickable | FlItemFlags_focusable | FlItemFlags_scroll_target);
    if (spec.disabled) {
        row.signal.clicked = false;
        row.signal.activated = false;
        row.signal.right_clicked = false;
    }
    if (row.signal.hovered && m->hover_drives) {
        // Hover writes only while it holds the last-input-wins latch. A disabled row takes the highlight too, but
        // stays out of item_navigable, so the keyboard skips it and it dispatches nothing.
        m->levels[row.level].highlight_index = row.index;
    }

    row.highlighted = m->levels[row.level].highlight_index == row.index;
    bool is_deepest = (row.level == m->depth - 1);
    FlColor bg = menu_row_background(theme, row.highlighted, is_deepest, spec.disabled);
    FlColor txt = spec.disabled ? theme->text.secondary : theme->text.primary;

    ui_layout_area_id(row.layout, .sizing = { sizing_grow(), sizing_fixed(UI_MENU_ITEM_HEIGHT) },
                      .direction = LayoutDirection_LeftToRight, .child_alignment = ui_align(Left, Center),
                      .padding = fl_padding_hv(FlSpacing_SM, FlSpacing_XS), .child_gap = FlSpacing_MD,
                      .radius = corner_radius(FlRadius_SM), .color = bg) {
        if (spec.marker != MenuMarker_None) {
            menu_emit_marker(theme, spec.marker, spec.marked);
        }
        ui_text_styled_impl(spec.label,
                            (FlTextConfig) { .color = txt, .valign = FlTextVAlign_Center, .no_wrap = true });
        menu_emit_trailer(theme, &spec);
    }

    return row;
}

// Horizontal side for a submenu panel: right of the parent by default, left when only that side fits, with the parent
// level's bias winning when both fit.
static bool submenu_flip_left(bool prefer_left, Clay_BoundingBox parent, float cw, float sw) {
    bool right_fits = (cw <= 0.0f) || (parent.x + parent.width + cw <= sw);
    bool left_fits = (cw <= 0.0f) || (parent.x - cw >= 0.0f);
    if (prefer_left) {
        return left_fits || !right_fits;
    }
    return !right_fits && left_fits;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Panel x for the chosen side, clamped into the viewport once the panel's width is known.
static float submenu_place_x(Clay_BoundingBox parent, float cw, float sw, bool flip_left) {
    float x = flip_left ? (parent.x - cw) : (parent.x + parent.width);
    if (cw > 0.0f) {
        x = fmaxf(fminf(x, sw - cw), 0.0f);
    }
    return x;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Panel y: top-aligned to the parent row, flipped up (bottom-aligned to the row's bottom) on bottom overflow, clamped
// into the viewport if neither orientation fits.
static float submenu_place_y(Clay_BoundingBox parent, float ch, float sh) {
    if (ch <= 0.0f || parent.y + ch <= sh) {
        return parent.y;
    }

    float y_up = parent.y + parent.height - ch;
    return fmaxf((y_up >= 0.0f) ? y_up : (sh - ch), 0.0f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Open a nested submenu floating panel for child_level, positioned beside its parent row. It shares the menu's one
// popup focus scope (so click-outside / highlight membership still cover the whole chain) but uses a distinct Clay
// element id per submenu so panels don't collide. Placement is measured one frame late (Clay reports the panel's box
// next frame): open right of the parent by default, flip left on right-edge overflow with the parent's bias
// inherited; top-align to the parent row, flip up on bottom overflow, clamp if neither fits. The chosen horizontal
// side is stored on the child level so deeper submenus inherit the bias.
static void menu_submenu_panel_begin(FlUiCtx* ctx, u32 parent_item_id, int parent_level, float min_width) {
    UiMenuState* m = &ctx->shared->menu;
    int child_level = parent_level + 1;
    Clay_BoundingBox parent = Clay_GetElementData((Clay_ElementId) { .id = parent_item_id }).boundingBox;

    // Distinct, stable element id for this panel, derived from the parent item id (unique per submenu, frame-stable).
    u32 panel_clay_id = parent_item_id * 2654435761u + 0x9E3779B9u;
    Clay_ElementId panel_id = { .id = panel_clay_id };

    Clay_ElementData prev = Clay_GetElementData(panel_id);
    float cw = prev.found ? prev.boundingBox.width : 0.0f;
    float ch = prev.found ? prev.boundingBox.height : 0.0f;

    bool flip_left = submenu_flip_left(m->levels[parent_level].flip_left, parent, cw, ctx->layout_width);
    m->levels[child_level].flip_left = flip_left;
    float x = submenu_place_x(parent, cw, ctx->layout_width, flip_left);
    float y = submenu_place_y(parent, ch, ctx->layout_height);

    // Floating element attached to the root at the computed absolute position. current_popup_scope_id is already the
    // menu scope (armed by ui_popup_begin for the root), so child items register into the shared scope.
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

    // Panel container (matches the root menu panel styling; height-capped + scrollable when this level is over-tall).
    fl_layout_start_auto();
    menu_configure_panel(ctx, child_level, min_width);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Builder

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The deepest open level, clamped to the root while the chain is closed.
static int menu_deepest_level(const UiMenuState* m) {
    return m->depth > 0 ? m->depth - 1 : 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Left: ascend (collapse one level); the parent becomes the deepest and keeps its highlight on the row whose submenu
// just closed. No-op at the root (Left there belongs to the background, but the menu owns the scope).
static void menu_key_ascend(FlUiCtx* ctx) {
    UiMenuState* m = &ctx->shared->menu;

    if (ui_action_pressed(ctx, FlAction_Nav_Left) && m->depth > 1) {
        menu_collapse_to(m, m->depth - 1);
        m->hover_drives = false;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Right / Confirm-on-a-submenu: descend into the highlighted submenu parent (instant cold-open via keyboard). A submenu
// parent is a pure container, so Confirm opens it rather than activating; a leaf ignores Right. Returns whether it
// descended.
static bool menu_key_descend(FlUiCtx* ctx, int level) {
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Keyboard/gamepad navigation for the deepest open level (arrow keys never reach into a parent level). Runs on the
// prev-frame item set, so it must precede the per-frame item_count reset, and is skipped on the opening frame (see
// UiMenuState.open_frame). Scroll-into-view fires only when a key actually moved the highlight, so a wheel scroll or
// a stationary highlight is never yanked back by a per-frame re-centering.
static void menu_handle_keyboard(FlUiCtx* ctx) {
    UiMenuState* m = &ctx->shared->menu;

    if (ctx->frame_counter <= m->open_frame) {
        return;
    }

    // Up/Down: move the highlight within the deepest level (press edge + the existing nav-repeat timing).
    int lvl = menu_deepest_level(m);
    bool nav_moved = menu_step_highlight(ctx, lvl, FlAction_Nav_Up, -1);
    nav_moved |= menu_step_highlight(ctx, lvl, FlAction_Nav_Down, +1);

    menu_key_ascend(ctx);
    lvl = menu_deepest_level(m);
    if (menu_key_descend(ctx, lvl)) {
        lvl = menu_deepest_level(m);
        nav_moved = true; // the new deepest level's fresh highlight should be brought into view
    }

    // Type-ahead: a printable character jumps the deepest level's highlight to the next matching item (cycling on
    // a repeated letter). When it moves the highlight, the keyboard owns it, just like the arrow keys above.
    nav_moved |= menu_typeahead(ctx, lvl);

    if (nav_moved) {
        menu_scroll_into_view(ctx, lvl);
    }
}

static void menu_dwell_reset(UiMenuState* m) {
    m->dwell_item_id = 0;
    m->hover_accum = 0.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Accumulate hover time on the hot item; a new hot item restarts its hover_since_hot accumulator.
static void menu_dwell_tick(FlUiCtx* ctx) {
    UiMenuState* m = &ctx->shared->menu;

    if (m->dwell_item_id != ctx->hot_id) {
        m->dwell_item_id = ctx->hot_id;
        m->hover_accum = 0.0f;
    }
    m->hover_accum += ctx->delta_time;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A sibling is already open: switch after the grace, protecting a diagonal move into the open submenu. Fire
// immediately if the cursor is moving AWAY from it (cheap dot-product on x).
static void menu_dwell_switch_sibling(FlUiCtx* ctx, MenuHit hit, float move_dx) {
    const UiMenuState* m = &ctx->shared->menu;

    int child_lvl = hit.level + 1 < UI_MENU_MAX_DEPTH ? hit.level + 1 : hit.level;
    float dir = m->levels[child_lvl].flip_left ? -1.0f : 1.0f;
    bool moving_away = (move_dx * dir) < 0.0f;
    if (moving_away || m->hover_accum >= UI_MENU_BRANCH_GRACE_SECONDS) {
        menu_branch_switch(ctx, hit.level, ctx->hot_id, hit.submenu);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Pointer-driven cold-open dwell + branch-switch grace, off the prev-frame item set (so it must precede the
// per-frame item_count reset). move_dx is this frame's horizontal pointer movement.
static void menu_handle_dwell(FlUiCtx* ctx, float move_dx) {
    UiMenuState* m = &ctx->shared->menu;

    // Pointer in empty space (sticky: chain stays open) → no pending switch; likewise back over the already-expanded
    // parent, or over a disabled row.
    MenuHit hit = menu_find_hot_item(ctx);
    u32 cur_child = hit.level >= 0 ? m->levels[hit.level].expanded_child_id : 0;
    bool inert = hit.level < 0 || !hit.navigable;
    if (inert || ctx->hot_id == cur_child) {
        menu_dwell_reset(m);
        return;
    }

    menu_dwell_tick(ctx);
    if (cur_child != 0) {
        menu_dwell_switch_sibling(ctx, hit, move_dx);
    } else if (hit.submenu && m->hover_accum >= UI_MENU_DWELL_SECONDS) {
        // Cold level: only a submenu parent opens, and only after the dwell threshold.
        menu_branch_switch(ctx, hit.level, ctx->hot_id, true);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Drive the open chain from input: keyboard, pointer dwell, and the wheel.
static void menu_handle_input(FlUiCtx* ctx) {
    UiMenuState* m = &ctx->shared->menu;

    menu_handle_keyboard(ctx);

    float move_dx = menu_track_pointer_move(ctx, m).x;

    // Only while the pointer is the active driver, and never on the opening frame.
    if (ctx->frame_counter > m->open_frame && m->hover_drives) {
        menu_handle_dwell(ctx, move_dx);
    }

    // Mouse wheel scrolls the hovered over-tall level (prev-frame set, so before the reset in begin).
    menu_apply_wheel(ctx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ui_menu_begin_impl(FlLayoutId menu_id, FlMenuConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;
    u32 scope = ui_layout_id_to_clay_id(menu_id);

    if (!menu_owned_by(ctx, scope) || menu_dismissed(ctx, scope, false)) {
        return false;
    }

    // Arm the menu's own focus scope, taking over from any other armed popup/dropdown scope. The menu owns
    // ESC/click-outside itself, so the popup layer's auto-close is disabled.
    menu_arm_focus_scope(ctx, scope);

    menu_handle_input(ctx);

    // Reset the root level's per-frame build state. The highlight PERSISTS across frames; only the per-frame item
    // counter resets so indices restart. Deeper levels reset their counters as ui_submenu_begin enters them.
    mf->build_level = 0;
    mf->levels[0].item_count = 0;
    mf->close_requested = false;

    i16 z = cfg.z_index != 0 ? cfg.z_index : UI_Z_OVERLAY;
    mf->z_base = z; // submenu panels stack at z_base + level so each opens above its parent

    // Floating panel anchored at the captured cursor point. Point mode makes the popup core clamp horizontally and
    // flip upward when the menu would overflow the bottom edge.
    ui_popup_begin(menu_id, (FlPopupConfig) { .anchor = FlPopupAnchor_Point,
                                              .point = m->anchor,
                                              .z_index = z,
                                              .capture_input = true,
                                              .auto_close = false });

    // Root panel container (height-capped + scrollable when the root level is over-tall; sizing_fit otherwise).
    fl_layout_start_auto();
    menu_configure_panel(ctx, 0, menu_min_width(cfg.min_width));

    return true;
}

// Fold keyboard/gamepad activation and the per-item close policy into a freshly-emitted leaf row, returning the
// resolved signal. kind_default_close is the kind's natural close default: an action closes on activation, a
// toggle keeps open.
static FlSignal menu_finish_leaf(FlUiCtx* ctx, MenuRow row, FlMenuItemConfig cfg, bool kind_default_close) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    FlSignal signal = row.signal;

    // Keyboard/gamepad activation: Enter / Space / gamepad-A fires the highlighted leaf at the DEEPEST level (arrow
    // keys never reach a parent), past the opening-frame guard (see UiMenuState.open_frame). The mouse path needs
    // no guard - row.signal.clicked flows through untouched.
    bool can_confirm = row.highlighted && row.level == m->depth - 1 && ctx->frame_counter > m->open_frame;
    if (can_confirm && ui_action_pressed(ctx, FlAction_Confirm)) {
        signal.clicked = true;
        signal.activated = true;
    }

    bool activated = !cfg.disabled && signal.clicked;
    if (activated && menu_policy_closes(cfg.close_policy, kind_default_close)) {
        mf->close_requested = true;
    }

    return signal;
}

FlSignal ui_menu_item_impl(FlString label, FlMenuItemConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();
    MenuRow row
        = menu_emit_row(ctx, (MenuRowSpec) { .label = label, .shortcut = cfg.shortcut, .disabled = cfg.disabled });
    return menu_finish_leaf(ctx, row, cfg, /*kind_default_close=*/true);
}

FlSignal ui_menu_check_impl(FlString label, bool* checked, FlMenuItemConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();
    // The marker reflects the state at emit time; the toggle below applies after, so the new state shows next frame
    // (the menu stays open by default). A null pointer is a live row that shows nothing and mutates nothing.
    bool marked = checked != nullptr && *checked;
    MenuRow row = menu_emit_row(ctx, (MenuRowSpec) { .label = label,
                                                     .shortcut = cfg.shortcut,
                                                     .disabled = cfg.disabled,
                                                     .marker = MenuMarker_Check,
                                                     .marked = marked });
    FlSignal signal = menu_finish_leaf(ctx, row, cfg, /*kind_default_close=*/false);

    if (signal.clicked && checked != nullptr) {
        *checked = !*checked;
    }
    return signal;
}

FlSignal ui_menu_radio_impl(FlString label, int* selected, int value, FlMenuItemConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();
    bool marked = selected != nullptr && *selected == value;
    MenuRow row = menu_emit_row(ctx, (MenuRowSpec) { .label = label,
                                                     .shortcut = cfg.shortcut,
                                                     .disabled = cfg.disabled,
                                                     .marker = MenuMarker_Radio,
                                                     .marked = marked });
    FlSignal signal = menu_finish_leaf(ctx, row, cfg, /*kind_default_close=*/false);

    if (signal.clicked && selected != nullptr) {
        *selected = value; // selecting one option deselects its siblings - they share this one group state
    }
    return signal;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Static (non-interactive) item kinds - separator, section header

// Record a static row (separator / header) into the current level and register it as an in-scope but non-interactive
// item, returning its layout id for the caller to paint. It occupies an item index, keeping the per-level highlight
// index dense with the action rows, and registers with no interaction flags so a click on it lands in the menu's
// popup scope and therefore does NOT dismiss the chain.
static FlLayoutId menu_record_static_row(FlUiCtx* ctx, FlString name, f32 row_height) {
    // Non-navigable (never a nav / highlight / type-ahead target) with scroll_target only: no click or focus, but the
    // row owns the wheel while hot, so a wheel over a separator/header scrolls the over-tall level like a row does.
    MenuRowSpec spec = { .label = name, .disabled = true };
    return menu_register_row(ctx, &spec, row_height, FlItemFlags_scroll_target).layout;
}

void ui_menu_separator_impl(void) {
    FlUiCtx* ctx = ui_get_ctx();
    const FlThemeColors* theme = ui_get_theme_colors(ctx);

    FlLayoutId item_id = menu_record_static_row(ctx, FL_S("##menu_sep"), UI_MENU_SEPARATOR_HEIGHT);

    // A full-width row painting the menu surface (so the row's id has a render command and is hot-in-scope) with a
    // centered 1px rule in the separator color. Horizontal inset keeps the rule off the panel edges.
    ui_layout_area_id(item_id, .sizing = { sizing_grow(), sizing_fixed(UI_MENU_SEPARATOR_HEIGHT) },
                      .direction = LayoutDirection_TopToBottom, .child_alignment = ui_align(Center, Center),
                      .padding = fl_padding_hv(FlSpacing_SM, 0), .color = theme->surface.elevated) {
        ui_layout_area(.sizing = { sizing_grow(), sizing_fixed(1.0f) }, .color = theme->border.separator) {}
    }
}

void ui_menu_header_impl(FlString label) {
    FlUiCtx* ctx = ui_get_ctx();
    const FlThemeColors* theme = ui_get_theme_colors(ctx);

    FlLayoutId item_id = menu_record_static_row(ctx, label, UI_MENU_HEADER_HEIGHT);

    // A non-interactive caption: dimmed (secondary) text, left-aligned, on the menu surface. Never highlighted (not
    // navigable) and never activatable.
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

    // A submenu parent is a pure container: emit the row (chevron, marked submenu) but never expose an action signal.
    MenuRow row = menu_emit_row(ctx, (MenuRowSpec) { .label = label, .disabled = cfg.disabled, .is_submenu = true });

    // Click is a per-surface open trigger (instant, no dwell): a click on a closed submenu parent - or on a sibling
    // while a different branch is open - branch-switches to it. Clicking the already-open parent is a no-op. The
    // click is consumed here as "open"; a submenu parent never dispatches an action signal to the caller.
    bool is_expanded = m->levels[parent_level].expanded_child_id == row.id;
    bool clicked_open = !cfg.disabled && row.signal.clicked;
    if (clicked_open && !is_expanded) {
        menu_branch_switch(ctx, parent_level, row.id, true);
        is_expanded = true;
    }

    // Open only while THIS submenu is the one expanded at its level (identity-keyed: compare resolved widget ids).
    if (cfg.disabled || !is_expanded) {
        return false;
    }

    int child_level = parent_level + 1;
    FL_ASSERT_MSG(child_level < UI_MENU_MAX_DEPTH, "menu open-path overflow (fixed cap UI_MENU_MAX_DEPTH)");
    if (child_level >= UI_MENU_MAX_DEPTH) {
        return false; // release safety
    }

    menu_begin_level(ctx, child_level);

    menu_submenu_panel_begin(ctx, row.id, parent_level, menu_min_width(cfg.min_width));
    return true;
}

void ui_submenu_end_impl(void) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuFrame* mf = &ctx->menu_frame;

    fl_layout_end();      // close the submenu panel container
    Clay__CloseElement(); // close the floating element (back to the parent panel's layout)

    if (mf->build_level > 0) {
        mf->build_level--; // pop back to the parent level
    }
}

void ui_menu_end_impl(void) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuFrame* mf = &ctx->menu_frame;

    fl_layout_end(); // close the panel
    ui_popup_end();  // close the floating element + clear the render-time popup scope

    // Tear down AFTER rendering so the activating item still reported .clicked this frame and the menu drew once more.
    if (mf->close_requested) {
        menu_close_internal(ctx);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Right-click region

// Whether the cursor is inside the region's LAYOUT box. The render-command-driven hot path only marks elements that
// emit a render command, so a transparent catch-all region would never report hover; Clay tracks the layout box
// whether or not the region paints anything. The box is last frame's.
static bool menu_region_contains(FlLayoutId region_id, FlVec2 point) {
    Clay_ElementData ed = Clay_GetElementData((Clay_ElementId) { .id = ui_layout_id_to_clay_id(region_id) });
    Clay_BoundingBox b = ed.boundingBox;
    return ed.found && point.x >= b.x && point.x < b.x + b.width && point.y >= b.y && point.y < b.y + b.height;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Default to a grow/grow catch-all region when the caller didn't specify sizing (zero-init is Fit/Fit with no bounds,
// treated here as "unset").
static FlLayoutSizing menu_region_sizing(FlLayoutSizing requested) {
    bool unset = requested.width.type == FlLayoutSizingType_Fit && requested.width.size.min_max.max == 0.0f
                 && requested.height.type == FlLayoutSizingType_Fit && requested.height.size.min_max.max == 0.0f;
    return unset ? (FlLayoutSizing) { sizing_grow(), sizing_grow() } : requested;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlSignal ui_menu_region_begin_impl(FlLayoutId region_id, FlLayoutId menu_id, FlMenuRegionConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();

    // Register the region as an interactive item (opting into right-click tracking so callers don't hand-wire the
    // flag) - this gives it a popup scope and a usable signal for callers that paint the region.
    FlSignal signal
        = ui_item_behavior(region_id, FlItemFlags_clickable | FlItemFlags_right_click, (FlWidgetConfig) { 0 });

    FlVec2 mouse = { ctx->input_state.mouse.x, ctx->input_state.mouse.y };
    if (ctx->input_state.mouse.buttons[FlMouseButton_Right] && menu_region_contains(region_id, mouse)) {
        ui_menu_open_impl(menu_id, mouse);
    }

    fl_layout_start(region_id);
    fl_layout_configure((FlLayoutConfig) { .sizing = menu_region_sizing(cfg.sizing) });

    return signal;
}

void ui_menu_region_end_impl(void) {
    fl_layout_end();
}
