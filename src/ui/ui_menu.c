#include "ui_menu.h"
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
// Defaults

// Menu panel width in logical px - wide enough for a label plus a right-aligned shortcut; items grow to fill it so
// the hover highlight spans the full row.
#define UI_MENU_DEFAULT_MIN_WIDTH 180.0f
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
// Menu-bar top-row height in logical px: a compact strip, a touch shorter than a context-menu row.
#define UI_MENUBAR_HEIGHT 30.0f
// Logical-px gap kept between an over-tall (scrolling) menu level and the top/bottom viewport edges, so a height-capped
// panel's border doesn't sit flush against the screen. Only affects levels tall enough to scroll.
#define UI_MENU_VIEWPORT_MARGIN 8.0f
// Wheel pixels scrolled per notch over a menu level (matches the scroll-area default feel).
#define UI_MENU_WHEEL_SPEED 30.0f
// Vertical padding (top + bottom) the menu panel reserves around its item list - padding_all(FlSpacing_XS) on both
// panel styles. Folded out of the viewport when sizing the scrollable interior so the cap math stays in content space.
#define UI_MENU_PANEL_VPAD (2.0f * (float)FlSpacing_XS)

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
// Tear down the open chain: drop the open path and release the popup focus scope it armed (restoring focus). Safe
// to call whether the scope is still the menu's or already gone.
static void menu_close_internal(FlUiCtx* ctx) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    if (ctx->popup_focus_scope_id == m->owner_scope_id && m->owner_scope_id != 0) {
        ui_focus_scope_release(ctx);
        ctx->saved_focused_id = 0;
        ctx->popup_closed_this_frame = true; // let any sibling popup-driven widget observe the close
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

// Clear one level's per-frame build state and cross-open state, so it reopens fresh: no highlight yet, no expanded
// child, no flip, scrolled to the top.
static void menu_reset_level(UiMenuState* m, UiMenuFrame* mf, int level) {
    m->levels[level].highlight_index = -1;
    mf->levels[level].item_count = 0;
    m->levels[level].init_highlight = true; // first navigable item claims the highlight as items emit this frame
    m->levels[level].expanded_child_id = 0;
    m->levels[level].flip_left = false;
    m->levels[level].scroll_offset = 0.0f;
}

// Update the last-input-wins latch from this frame's pointer position and return the pointer's movement. A pointer
// that actually moved reclaims the shared highlight from the keyboard; a stationary one leaves a keyboard selection
// alone. menu_emit_row only lets hover write the highlight while hover_drives is set.
static FlVec2 menu_track_pointer_move(FlUiCtx* ctx, UiMenuState* m) {
    FlVec2 mp = { ctx->input_state.mouse.x, ctx->input_state.mouse.y };
    FlVec2 move = { mp.x - m->last_mouse_pos.x, mp.y - m->last_mouse_pos.y };
    if (move.x != 0.0f || move.y != 0.0f) {
        m->hover_drives = true;
    }
    m->last_mouse_pos = mp;
    return move;
}

// Collapse the open path so new_depth levels remain open: every level at or beyond new_depth drops its expanded
// child, so the deepest kept level becomes a leaf again. Resets the dwell/grace accumulator so a stale hot-item timer
// can't fire across the collapse.
static void menu_collapse_to(UiMenuState* m, int new_depth) {
    if (new_depth < 1) {
        new_depth = 1;
    }
    if (new_depth > UI_MENU_MAX_DEPTH) {
        new_depth = UI_MENU_MAX_DEPTH;
    }
    for (int i = new_depth - 1; i < UI_MENU_MAX_DEPTH; i++) {
        m->levels[i].expanded_child_id = 0;
        // Reset scroll only on the levels that fully CLOSE (>= new_depth): the deepest kept level (new_depth - 1) is
        // still on screen, so an ESC-pop / Left-ascend must not jump its scroll - but a reopened deeper level starts
        // at the top.
        if (i >= new_depth) {
            m->levels[i].scroll_offset = 0.0f;
        }
    }
    m->depth = new_depth;
    m->dwell_item_id = 0;
    m->hover_accum = 0.0f;
}

// ESC: pop ONE level, closing the whole chain only at the root. Returns true when the chain fully closed.
static bool menu_handle_cancel(FlUiCtx* ctx, UiMenuState* m) {
    if (m->depth > 1) {
        menu_collapse_to(m, m->depth - 1);
        m->hover_drives = false;
        return false;
    }
    menu_close_internal(ctx);
    return true;
}

// Level-agnostic branch switch: collapse the open path back to level, then expand the sibling child_id at that
// level if it is a submenu parent. A leaf "expands" to nothing - it just collapses the deeper path. The new deepest
// level re-initializes its highlight to the first navigable child. Overflow past the fixed depth cap asserts in debug.
static void menu_branch_switch(UiMenuState* m, UiMenuFrame* mf, int level, u32 child_id, bool child_is_submenu) {
    menu_collapse_to(m, level + 1);
    if (!child_is_submenu || child_id == 0) {
        return; // sibling leaf (or nothing): the collapse above is the whole switch
    }
    int child = level + 1;
    FL_ASSERT_MSG(child < UI_MENU_MAX_DEPTH, "menu open-path overflow (fixed cap UI_MENU_MAX_DEPTH)");
    if (child >= UI_MENU_MAX_DEPTH) {
        return; // release safety: refuse to grow past the cap
    }
    m->levels[level].expanded_child_id = child_id;
    m->levels[child].highlight_index = -1;
    m->levels[child].init_highlight = true; // the first navigable child row claims the highlight as it emits
    mf->levels[child].item_count = 0;
    m->levels[child].expanded_child_id = 0;
    m->depth = child + 1;
}

// Locate the menu item the pointer is over by scanning the recorded item set of every open level. Returns false when
// the hot widget is not a menu item (pointer outside the chain).
//
// PREV-FRAME SET: the level arrays hold the PREVIOUS frame's emission, which is exactly what ctx->hot_id was computed
// against, so every reader of them - this function, menu_move_highlight, menu_typeahead, the scroll geometry, and
// begin() itself - must run before this frame's items re-emit and reset the per-frame counters.
static bool menu_find_hot_item(const UiMenuState* m, const UiMenuFrame* mf, u32 hot_id, int* out_level, int* out_index,
                               bool* out_submenu) {
    if (hot_id == 0) {
        return false;
    }
    for (int L = 0; L < m->depth && L < UI_MENU_MAX_DEPTH; L++) {
        const UiMenuLevelFrame* lvl = &mf->levels[L];
        int n = lvl->item_count < UI_MENU_MAX_ITEMS ? lvl->item_count : UI_MENU_MAX_ITEMS;
        for (int i = 0; i < n; i++) {
            if (lvl->item_id[i] == hot_id) {
                *out_level = L;
                *out_index = i;
                *out_submenu = lvl->item_submenu[i];
                return true;
            }
        }
    }
    return false;
}

// Click-outside test: is the hot widget (the one the pointer is over) part of the menu's popup scope? The whole open
// chain shares one scope id, so a single membership check covers the union of all open menu rects.
static bool menu_hot_in_scope(FlUiCtx* ctx, u32 scope) {
    if (ctx->hot_id == 0) {
        return false;
    }
    ItemState* hot = hashmap_get(&ctx->shared->item_state, ctx->hot_id);
    return hot != nullptr && hot->popup_scope_id == scope;
}

// Move the level's highlight by dir (+1 down, -1 up) to the next navigable item, wrapping at the ends and skipping
// non-navigable rows. Reads the prev-frame set (see menu_find_hot_item); a menu's contents are stable frame to
// frame. No-op when the level has no navigable items. A highlight of -1 (none yet) steps to the first item in the
// travel direction.
static void menu_move_highlight(UiMenuLevel* lvl, const UiMenuLevelFrame* lf, int dir) {
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

// Record a bounded copy of an emitted row's label into its level slot for type-ahead, truncating at the prefix cap.
// A copy, never the borrowed FlString, so the bytes stay valid at the next begin() where matching runs.
static void menu_record_label(UiMenuLevelFrame* lf, int index, FlString label) {
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
// Type-ahead: fold this frame's typed text into the menu's prefix and move level's highlight to the matching item,
// cycling on a repeated single letter. Reads the prev-frame set's labels and navigability (see menu_find_hot_item);
// non-navigable rows are presented to the matcher as empty labels so they never match. Returns true when it moved the
// highlight, so the caller can hand it to the keyboard in the arbitration.
static bool menu_typeahead(FlUiCtx* ctx, UiMenuState* m, UiMenuFrame* mf, int level) {
    UiMenuLevel* lvl = &m->levels[level];
    const UiMenuLevelFrame* lf = &mf->levels[level];

    // Per-frame snapshot, not the raw kb->text_input which fl_platform_input_update clears before the
    // frame body runs under the fl_app tick order.
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
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scrollable over-tall levels. A menu level whose natural item list is taller than the viewport is height-capped and
// its contents scroll; a level that fits stays natural-height. All offset/clamp math reuses scroll_core (scroll_max).
// Scrolling is immediate (no smoothing) so keyboard scroll-into-view lands the highlight in a single frame.

// Record one emitted row's scroll geometry: its content-space top (relative to the first row, panel padding excluded)
// and height, plus the level's running natural height. The running height restarts as the level's first row (index 0)
// emits, so the natural_height read back at the next begin() - before this frame's first row re-emits - is the previous
// frame's complete total.
static void menu_record_row_geometry(UiMenuFrame* mf, int level, int index, f32 height) {
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

// The scrollable geometry of a level, derived from the prev-frame set's recorded rows (natural_height) and the
// current viewport height. capped is true when the natural list overflows the interior; viewport is the visible
// interior height (content space, == content when it fits), max_offset the largest legal scroll offset (0 when
// it fits).
typedef struct MenuScrollGeom {
    bool capped;
    f32 content;    // total natural item-list height (content space)
    f32 viewport;   // visible interior height (== content when not capped)
    f32 max_offset; // scroll_max(content, viewport); 0 when not capped
} MenuScrollGeom;

static MenuScrollGeom menu_scroll_geom(const FlUiCtx* ctx, const UiMenuLevelFrame* lf) {
    MenuScrollGeom g = { .content = lf->natural_height, .viewport = lf->natural_height, .max_offset = 0.0f };
    // Interior height available to the item list: the viewport minus the top/bottom edge margins and the panel's own
    // vertical padding. A level taller than that is capped to it and scrolls the remainder. All of this is logical
    // (natural_height sums authored row heights), so the physical layout height is divided back down first - at any
    // ui_scale other than 1 the two spaces disagree and an over-tall level would never cap.
    f32 scale = ctx->ui_scale > 0.0f ? ctx->ui_scale : 1.0f;
    f32 interior_cap = ctx->layout_height / scale - 2.0f * UI_MENU_VIEWPORT_MARGIN - UI_MENU_PANEL_VPAD;
    if (interior_cap > 0.0f && g.content > interior_cap) {
        g.capped = true;
        g.viewport = interior_cap;
        g.max_offset = scroll_max(g.content, g.viewport);
    }
    return g;
}

// Configure a menu level's panel container - shared by the context-menu root, submenu panels, and menu-bar dropdowns.
// When the level is over-tall the panel is height-capped and gets a vertical clip with the level's manual scroll
// offset; when it fits it stays sizing_fit. Clamps the offset to this frame's geometry first, so a level whose content
// shrank snaps back in range.
static void menu_configure_panel(FlUiCtx* ctx, int level, float min_w) {
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
        // Manual clip: Clay clips children to the fixed panel height and shifts them up by the offset. The rows still
        // emit and record identity every frame (fully-clipped rows just draw nothing), so nav/hit-testing is unchanged.
        // childOffset is screen space and bypasses fl_layout_configure's scaling, so the logical offset is
        // multiplied here (everything else on this path stays logical).
        f32 scale = ctx->ui_scale > 0.0f ? ctx->ui_scale : 1.0f;
        cfg.clip = (FlLayoutClip) {
            .vertical = true,
            .manual_scroll = true,
            .scroll_pos = { 0.0f, -m->levels[level].scroll_offset * scale },
        };
    }
    fl_layout_configure(cfg);
}

// Mouse wheel over the open chain scrolls the HOVERED level only (the one the pointer is over), never a parent. Applies
// the frame's wheel delta to that level's offset, clamped to its scrollable range. A no-op unless the pointer is over a
// menu item whose level is over-tall.
static void menu_apply_wheel(FlUiCtx* ctx, UiMenuState* m, UiMenuFrame* mf) {
    FlVec2 wheel = ui_get_mouse_wheel();
    if (wheel.y == 0.0f) {
        return;
    }
    int L = -1, idx = -1;
    bool sub = false;
    if (!menu_find_hot_item(m, mf, ctx->hot_id, &L, &idx, &sub)) {
        return; // pointer not over a menu item → nothing to scroll
    }
    MenuScrollGeom g = menu_scroll_geom(ctx, &mf->levels[L]);
    if (g.max_offset <= 0.0f) {
        return;
    }
    // wheel.y > 0 is a scroll-up gesture → content moves down → offset decreases (matches the scroll-area sign).
    f32 off = m->levels[L].scroll_offset - wheel.y * UI_MENU_WHEEL_SPEED;
    m->levels[L].scroll_offset = clamp_f32_impl(off, 0.0f, g.max_offset);
}

// Scroll level's highlighted row fully into view with the minimal adjustment in either direction. Reads the
// prev-frame set's row geometry against this frame's just-moved highlight_index. A no-op when the level fits or the
// highlight is out of the recorded range.
static void menu_scroll_into_view(FlUiCtx* ctx, UiMenuState* m, UiMenuFrame* mf, int level) {
    if (level < 0 || level >= UI_MENU_MAX_DEPTH) {
        return;
    }
    const UiMenuLevelFrame* lf = &mf->levels[level];
    MenuScrollGeom g = menu_scroll_geom(ctx, lf);
    if (g.max_offset <= 0.0f) {
        return;
    }
    int i = m->levels[level].highlight_index;
    if (i < 0 || i >= UI_MENU_MAX_ITEMS || i >= lf->item_count) {
        return;
    }
    f32 top = lf->item_top[i];
    f32 bottom = top + lf->item_height[i];
    f32 off = m->levels[level].scroll_offset;
    if (top < off) {
        off = top; // row above the window → align its top to the window top
    } else if (bottom > off + g.viewport) {
        off = bottom - g.viewport; // row below the window → align its bottom to the window bottom
    }
    m->levels[level].scroll_offset = clamp_f32_impl(off, 0.0f, g.max_offset);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Open-path state

void ui_menu_open_impl(FlLayoutId menu_id, FlVec2 point) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;
    u32 scope = ui_layout_id_to_clay_id(menu_id);

    // Capture the cursor only on the closed -> open transition. The right-click that opens the menu is
    // level-triggered (true every frame the button is held), so re-anchoring here each frame would make the menu
    // live-follow the pointer while the button is held.
    if (m->active && m->owner_scope_id == scope) {
        return;
    }

    m->active = true;
    m->owner_scope_id = scope;
    m->anchor = point;
    m->open_frame = ctx->frame_counter;
    m->depth = 1; // a single flat root level; submenus extend the path as they open
    mf->build_level = 0;
    mf->close_requested = false;
    m->dwell_item_id = 0;
    m->hover_accum = 0.0f;
    m->typeahead = (TypeAheadState) { 0 };
    for (int i = 0; i < UI_MENU_MAX_DEPTH; i++) {
        menu_reset_level(m, mf, i);
    }

    // Seed the last-input-wins latch so a pointer merely sitting over a row at open time does not own the
    // highlight until it actually moves.
    m->hover_drives = false;
    m->last_mouse_pos = (FlVec2) { ctx->input_state.mouse.x, ctx->input_state.mouse.y };
}

void ui_menu_close_impl(FlLayoutId menu_id) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuState* m = &ctx->shared->menu;
    if (!m->active || m->owner_scope_id != ui_layout_id_to_clay_id(menu_id)) {
        return;
    }
    menu_close_internal(ctx);
}

bool ui_menu_is_open_impl(FlLayoutId menu_id) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuState* m = &ctx->shared->menu;
    return m->active && m->owner_scope_id == ui_layout_id_to_clay_id(menu_id);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Row emission (shared by leaf items and submenu parents)

typedef struct MenuRow {
    FlSignal signal; // the row's interaction signal (a submenu parent's is never an action - caller ignores it)
    u32 id;          // resolved widget id of the row
    int level;       // build level the row was emitted at
    int index;       // per-level item index
    bool highlighted;
} MenuRow;

// Emit one menu row at the current build level: register the widget, record its identity (id + submenu flag) and
// navigability into the level, apply the shared hover highlight (last-input-wins), and paint the row - label on the
// left, plus a right-aligned shortcut (leaf) or a chevron (submenu parent).
static MenuRow menu_emit_row(FlUiCtx* ctx, UiMenuState* m, UiMenuFrame* mf, FlString label, FlString shortcut,
                             bool disabled, bool is_submenu, MenuMarker marker, bool marked) {
    const FlThemeColors* theme = ui_get_theme_colors(ctx);
    int level = mf->build_level;
    int index = mf->levels[level].item_count++;
    menu_record_row_geometry(mf, level, index, UI_MENU_ITEM_HEIGHT); // scroll geometry for an over-tall level

    // Seed the id with (level, index): index disambiguates same-label rows within a level, and folding the build
    // level in keeps a parent-level row and an open submenu row from colliding when their label and in-level index
    // line up. Only one menu chain is open at a time, so cross-menu id reuse can't collide.
    FlLayoutId item_id = { .name = label, .id = ui_menu_row_id_seed(level, index) };
    u32 rid = ui_layout_id_to_clay_id(item_id);
    // Scroll_target marks the row as owning wheel input while it is hot, so ui_update_begin withholds the wheel from
    // Clay's auto-scroll and leaves it for the menu's manual per-level scroll (menu_apply_wheel).
    FlSignal signal = ui_item_behavior(
        item_id, FlItemFlags_clickable | FlItemFlags_focusable | FlItemFlags_scroll_target, (FlWidgetConfig) { 0 });

    bool navigable = !disabled;
    if (index < UI_MENU_MAX_ITEMS) {
        mf->levels[level].item_navigable[index] = navigable;
        mf->levels[level].item_id[index] = rid; // identity-keyed open path: maps hot_id -> this row
        mf->levels[level].item_submenu[index] = is_submenu;
        menu_record_label(&mf->levels[level], index, label); // type-ahead prefix source (matched at the next begin)
    }
    // Initial highlight on open: the first navigable row claims the highlight as items emit this frame.
    if (navigable && m->levels[level].init_highlight) {
        m->levels[level].highlight_index = index;
        m->levels[level].init_highlight = false;
    }
    if (disabled) {
        signal.clicked = false;
        signal.activated = false;
        signal.right_clicked = false;
    }
    if (signal.hovered && m->hover_drives) {
        // Hover writes only while it holds the last-input-wins latch. A disabled row takes the highlight too, but
        // stays out of item_navigable, so the keyboard skips it and it dispatches nothing.
        m->levels[level].highlight_index = index;
    }

    bool highlighted = m->levels[level].highlight_index == index;
    bool is_deepest = (level == m->depth - 1);

    // Every row paints a background (the menu surface by default, the highlight when current). Beyond the look, a
    // painted row is what makes the item hittable: the hot/hover path is driven by render commands, so a fully
    // transparent row would never report hover.
    FlColor bg = theme->surface.elevated;
    if (highlighted) {
        float t = disabled ? UI_MENU_DISABLED_HIGHLIGHT : (is_deepest ? 1.0f : UI_MENU_TRAIL_HIGHLIGHT);
        bg = ui_menu_blend(theme->surface.elevated, theme->surface.selected, t);
    }
    FlColor txt = disabled ? theme->text.secondary : theme->text.primary;

    ui_layout_area_id(item_id, .sizing = { sizing_grow(), sizing_fixed(UI_MENU_ITEM_HEIGHT) },
                      .direction = LayoutDirection_LeftToRight, .child_alignment = ui_align(Left, Center),
                      .padding = fl_padding_hv(FlSpacing_SM, FlSpacing_XS), .child_gap = FlSpacing_MD,
                      .radius = corner_radius(FlRadius_SM), .color = bg) {
        if (marker != MenuMarker_None) {
            // A near-square for a check, a full-radius dot for a radio bullet.
            float r = marker == MenuMarker_Radio ? (float)FlRadius_Full : (float)FlRadius_SM;
            ui_layout_area(.sizing = { sizing_fixed(UI_MENU_MARKER_WIDTH), sizing_fixed(UI_MENU_ITEM_HEIGHT) },
                           .direction = LayoutDirection_TopToBottom, .child_alignment = ui_align(Center, Center)) {
                if (marked) {
                    ui_layout_area(.sizing = { sizing_fixed(UI_MENU_MARKER_DOT), sizing_fixed(UI_MENU_MARKER_DOT) },
                                   .color = theme->interactive.base, .radius = corner_radius(r)) {}
                }
            }
        }
        ui_text_styled_impl(label, (FlTextConfig) { .color = txt, .valign = FlTextVAlign_Center, .no_wrap = true });
        if (is_submenu) {
            // A grow spacer pushes the chevron to the trailing edge (the "has children" affordance).
            ui_layout_area(.sizing = { sizing_grow(), sizing_fit() }) {}
            ui_text_styled_impl(
                FL_S(">"),
                (FlTextConfig) { .color = theme->text.secondary, .valign = FlTextVAlign_Center, .no_wrap = true });
        } else if (shortcut.length > 0) {
            // A grow spacer pushes the shortcut text to the trailing edge.
            ui_layout_area(.sizing = { sizing_grow(), sizing_fit() }) {}
            ui_text_styled_impl(
                shortcut,
                (FlTextConfig) { .color = theme->text.secondary, .valign = FlTextVAlign_Center, .no_wrap = true });
        }
    }

    return (MenuRow) { .signal = signal, .id = rid, .level = level, .index = index, .highlighted = highlighted };
}

// Open a nested submenu floating panel for child_level, positioned beside its parent row. It shares the menu's one
// popup focus scope (so click-outside / highlight membership still cover the whole chain) but uses a distinct Clay
// element id per submenu so panels don't collide. Placement is measured one frame late (Clay reports the panel's box
// next frame): open right of the parent by default, flip left on right-edge overflow with the parent's bias
// inherited; top-align to the parent row, flip up on bottom overflow, clamp if neither fits. The chosen horizontal
// side is stored on the child level so deeper submenus inherit the bias.
static void menu_submenu_panel_begin(FlUiCtx* ctx, UiMenuState* m, u32 parent_item_id, int parent_level,
                                     int child_level, float min_width) {
    Clay_ElementData parent = Clay_GetElementData((Clay_ElementId) { .id = parent_item_id });
    float px = parent.boundingBox.x, py = parent.boundingBox.y;
    float pw = parent.boundingBox.width, ph = parent.boundingBox.height;

    // Distinct, stable element id for this panel, derived from the parent item id (unique per submenu, frame-stable).
    u32 panel_clay_id = parent_item_id * 2654435761u + 0x9E3779B9u;
    Clay_ElementId panel_id = { .id = panel_clay_id };

    Clay_ElementData prev = Clay_GetElementData(panel_id);
    float cw = prev.found ? prev.boundingBox.width : 0.0f;
    float ch = prev.found ? prev.boundingBox.height : 0.0f;
    float sw = ctx->layout_width, sh = ctx->layout_height;

    // Horizontal: right by default, flipped left on overflow, inheriting the parent level's bias.
    bool prefer_left = m->levels[parent_level].flip_left;
    bool right_fits = (cw <= 0.0f) || (px + pw + cw <= sw);
    bool left_fits = (cw <= 0.0f) || (px - cw >= 0.0f);
    bool flip_left;
    if (prefer_left) {
        flip_left = left_fits ? true : (right_fits ? false : true);
    } else {
        flip_left = right_fits ? false : (left_fits ? true : false);
    }
    float x = flip_left ? (px - cw) : (px + pw);
    if (cw > 0.0f) {
        if (x + cw > sw) {
            x = sw - cw;
        }
        if (x < 0.0f) {
            x = 0.0f;
        }
    }
    m->levels[child_level].flip_left = flip_left;

    // Vertical: top-align to the parent row; flip up (bottom-align to the parent row's bottom) on bottom overflow;
    // clamp into the viewport if neither orientation fits.
    float y = py;
    if (ch > 0.0f && py + ch > sh) {
        float y_up = py + ph - ch;
        y = (y_up >= 0.0f) ? y_up : (sh - ch);
        if (y < 0.0f) {
            y = 0.0f;
        }
    }

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

// Keyboard/gamepad navigation for the deepest open level (arrow keys never reach into a parent level). Runs on the
// prev-frame item set, so it must precede the per-frame item_count reset, and is skipped on the opening frame (see
// UiMenuState.open_frame). Scroll-into-view fires only when a key actually moved the highlight, so a wheel scroll or
// a stationary highlight is never yanked back by a per-frame re-centering.
static void menu_handle_keyboard(FlUiCtx* ctx, UiMenuState* m, UiMenuFrame* mf) {
    if (ctx->frame_counter <= m->open_frame) {
        return;
    }

    int lvl = m->depth - 1; // deepest open level
    if (lvl < 0) {
        lvl = 0;
    }

    bool nav_moved = false;
    // Up/Down: move the highlight within the deepest level (press edge + the existing nav-repeat timing).
    if (ui_action_pressed(ctx, FlAction_Nav_Up) || ui_action_repeated(ctx, FlAction_Nav_Up)) {
        menu_move_highlight(&m->levels[lvl], &mf->levels[lvl], -1);
        m->hover_drives = false; // keyboard moved last → it owns the highlight until the pointer moves again
        nav_moved = true;
    }
    if (ui_action_pressed(ctx, FlAction_Nav_Down) || ui_action_repeated(ctx, FlAction_Nav_Down)) {
        menu_move_highlight(&m->levels[lvl], &mf->levels[lvl], +1);
        m->hover_drives = false;
        nav_moved = true;
    }
    // Left: ascend (collapse one level); the parent becomes the deepest and keeps its highlight on the row whose
    // submenu just closed. No-op at the root (Left there belongs to the background, but the menu owns the scope).
    if (ui_action_pressed(ctx, FlAction_Nav_Left) && m->depth > 1) {
        menu_collapse_to(m, m->depth - 1);
        m->hover_drives = false;
        lvl = m->depth - 1;
    }
    // Right / Confirm-on-a-submenu: descend into the highlighted submenu parent (instant cold-open via keyboard).
    // A submenu parent is a pure container, so Confirm opens it rather than activating; a leaf ignores Right.
    UiMenuLevel* dl = &m->levels[lvl];
    UiMenuLevelFrame* dlf = &mf->levels[lvl];
    int hi = dl->highlight_index;
    bool hi_submenu = hi >= 0 && hi < UI_MENU_MAX_ITEMS && dlf->item_submenu[hi];
    bool want_descend
        = ui_action_pressed(ctx, FlAction_Nav_Right) || (hi_submenu && ui_action_pressed(ctx, FlAction_Confirm));
    if (want_descend && hi_submenu) {
        menu_branch_switch(m, mf, lvl, dlf->item_id[hi], true);
        m->hover_drives = false;
        lvl = m->depth - 1;
        nav_moved = true; // the new deepest level's fresh highlight should be brought into view
    }

    // Type-ahead: a printable character jumps the deepest level's highlight to the next matching item (cycling on
    // a repeated letter). When it moves the highlight, the keyboard owns it, just like the arrow keys above.
    if (menu_typeahead(ctx, m, mf, lvl)) {
        m->hover_drives = false;
        nav_moved = true;
    }

    if (nav_moved) {
        menu_scroll_into_view(ctx, m, mf, lvl);
    }
}

// Pointer-driven cold-open dwell + branch-switch grace, off the prev-frame item set (so it must precede the
// per-frame item_count reset). move_dx is this frame's horizontal pointer movement.
static void menu_handle_dwell(FlUiCtx* ctx, UiMenuState* m, UiMenuFrame* mf, float move_dx) {
    int hL = -1, hidx = -1;
    bool hsub = false;
    if (!menu_find_hot_item(m, mf, ctx->hot_id, &hL, &hidx, &hsub)) {
        m->dwell_item_id = 0; // pointer in empty space (sticky: chain stays open) → no pending switch
        m->hover_accum = 0.0f;
        return;
    }

    u32 hot = ctx->hot_id;
    u32 cur_child = m->levels[hL].expanded_child_id;
    bool hnav = (hidx < UI_MENU_MAX_ITEMS) && mf->levels[hL].item_navigable[hidx];
    if (hot == cur_child || !hnav) {
        m->dwell_item_id = 0; // back over the already-expanded parent, or over a disabled row: no switch
        m->hover_accum = 0.0f;
        return;
    }

    if (m->dwell_item_id != hot) {
        m->dwell_item_id = hot; // a new hot item resets its hover_since_hot accumulator
        m->hover_accum = 0.0f;
    }
    m->hover_accum += ctx->delta_time;

    if (cur_child == 0) {
        // Cold level: only a submenu parent opens, and only after the dwell threshold.
        if (hsub && m->hover_accum >= UI_MENU_DWELL_SECONDS) {
            menu_branch_switch(m, mf, hL, hot, true);
        }
        return;
    }

    // A sibling is already open: switch after the grace, protecting a diagonal move into the open submenu. Fire
    // immediately if the cursor is moving AWAY from it (cheap dot-product on x).
    int child_lvl = hL + 1 < UI_MENU_MAX_DEPTH ? hL + 1 : hL;
    float dir = m->levels[child_lvl].flip_left ? -1.0f : 1.0f;
    bool moving_away = (move_dx * dir) < 0.0f;
    if (moving_away || m->hover_accum >= UI_MENU_BRANCH_GRACE_SECONDS) {
        menu_branch_switch(m, mf, hL, hot, hsub);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
bool ui_menu_begin_impl(FlLayoutId menu_id, FlMenuConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;
    u32 scope = ui_layout_id_to_clay_id(menu_id);

    if (!m->active || m->owner_scope_id != scope) {
        return false;
    }

    // Self-owned dismissal, past the opening-frame guard (see UiMenuState.open_frame).
    // ESC is read through the action system (FlAction_Cancel), not keys_pressed, which fl_platform_input_update
    // clears before the frame body runs. ESC pops ONE level (closing the chain only at the root); a click outside the
    // union of all open rects closes the whole chain. Leaving the cursor in empty space is sticky and does NOT close.
    if (ctx->frame_counter > m->open_frame) {
        if (ctx->mouse_pressed_this_frame && !menu_hot_in_scope(ctx, scope)) {
            menu_close_internal(ctx);
            return false;
        }
        if (ui_action_pressed(ctx, FlAction_Cancel) && menu_handle_cancel(ctx, m)) {
            return false;
        }
    }

    // Arm the menu's own focus scope, taking over from any other armed popup/dropdown scope. The menu owns
    // ESC/click-outside itself, so the popup layer's auto-close is disabled.
    ui_focus_scope_arm(ctx, scope, true);
    ctx->popup_auto_close = false;

    menu_handle_keyboard(ctx, m, mf);

    float move_dx = menu_track_pointer_move(ctx, m).x;

    // Only while the pointer is the active driver, and never on the opening frame.
    if (ctx->frame_counter > m->open_frame && m->hover_drives) {
        menu_handle_dwell(ctx, m, mf, move_dx);
    }

    // Mouse wheel scrolls the hovered over-tall level (prev-frame set, so before the reset below).
    menu_apply_wheel(ctx, m, mf);

    // Reset the root level's per-frame build state. The highlight PERSISTS across frames; only the per-frame item
    // counter resets so indices restart. Deeper levels reset their counters as ui_submenu_begin enters them.
    mf->build_level = 0;
    mf->levels[0].item_count = 0;
    mf->close_requested = false;

    float min_w = cfg.min_width > 0.0f ? cfg.min_width : UI_MENU_DEFAULT_MIN_WIDTH;
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
    menu_configure_panel(ctx, 0, min_w);

    return true;
}

// Fold keyboard/gamepad activation and the per-item close policy into a freshly-emitted leaf row, returning the
// resolved signal. kind_default_close is the kind's natural close default: an action closes on activation, a
// toggle keeps open.
static FlSignal menu_finish_leaf(FlUiCtx* ctx, UiMenuState* m, UiMenuFrame* mf, MenuRow row, FlMenuItemConfig cfg,
                                 bool kind_default_close) {
    FlSignal signal = row.signal;

    // Keyboard/gamepad activation: Enter / Space / gamepad-A fires the highlighted leaf at the DEEPEST level (arrow
    // keys never reach a parent), past the opening-frame guard (see UiMenuState.open_frame). The mouse path needs
    // no guard - row.signal.clicked flows through untouched.
    bool is_deepest = (row.level == m->depth - 1);
    if (row.highlighted && is_deepest && ctx->frame_counter > m->open_frame
        && ui_action_pressed(ctx, FlAction_Confirm)) {
        signal.clicked = true;
        signal.activated = true;
    }

    if (!cfg.disabled && signal.clicked && menu_policy_closes(cfg.close_policy, kind_default_close)) {
        mf->close_requested = true;
    }

    return signal;
}

FlSignal ui_menu_item_impl(FlString label, FlMenuItemConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    MenuRow row
        = menu_emit_row(ctx, m, mf, label, cfg.shortcut, cfg.disabled, /*is_submenu=*/false, MenuMarker_None, false);
    return menu_finish_leaf(ctx, m, mf, row, cfg, /*kind_default_close=*/true);
}

FlSignal ui_menu_check_impl(FlString label, bool* checked, FlMenuItemConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    // The marker reflects the state at emit time; the toggle below applies after, so the new state shows next frame
    // (the menu stays open by default). A null pointer is a live row that shows nothing and mutates nothing.
    bool marked = checked != nullptr && *checked;
    MenuRow row
        = menu_emit_row(ctx, m, mf, label, cfg.shortcut, cfg.disabled, /*is_submenu=*/false, MenuMarker_Check, marked);
    FlSignal signal = menu_finish_leaf(ctx, m, mf, row, cfg, /*kind_default_close=*/false);

    if (signal.clicked && checked != nullptr) {
        *checked = !*checked;
    }
    return signal;
}

FlSignal ui_menu_radio_impl(FlString label, int* selected, int value, FlMenuItemConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    bool marked = selected != nullptr && *selected == value;
    MenuRow row
        = menu_emit_row(ctx, m, mf, label, cfg.shortcut, cfg.disabled, /*is_submenu=*/false, MenuMarker_Radio, marked);
    FlSignal signal = menu_finish_leaf(ctx, m, mf, row, cfg, /*kind_default_close=*/false);

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
static FlLayoutId menu_record_static_row(UiMenuFrame* mf, FlString name, f32 row_height) {
    int level = mf->build_level;
    int index = mf->levels[level].item_count++;
    menu_record_row_geometry(mf, level, index, row_height); // scroll geometry for an over-tall level

    // Same (level, index) seed as menu_emit_row: a static row occupies an index in its level, so it must share the
    // level-folded id scheme or a separator/header could collide with a same-index row in another open level.
    FlLayoutId item_id = { .name = name, .id = ui_menu_row_id_seed(level, index) };
    u32 rid = ui_layout_id_to_clay_id(item_id);
    // Scroll_target (but not clickable/focusable): a static row owns the wheel while hot, so a wheel over a
    // separator/header scrolls the over-tall level like a row does.
    ui_item_behavior(item_id, FlItemFlags_scroll_target, (FlWidgetConfig) { 0 });

    if (index < UI_MENU_MAX_ITEMS) {
        mf->levels[level].item_navigable[index] = false; // never a nav / highlight target
        mf->levels[level].item_id[index] = rid;          // identity-keyed open path: a click here maps in-scope
        mf->levels[level].item_submenu[index] = false;
        mf->levels[level].item_label_len[index] = 0; // non-navigable: never a type-ahead match target
    }
    return item_id;
}

void ui_menu_separator_impl(void) {
    FlUiCtx* ctx = ui_get_ctx();
    const FlThemeColors* theme = ui_get_theme_colors(ctx);

    FlLayoutId item_id = menu_record_static_row(&ctx->menu_frame, FL_S("##menu_sep"), UI_MENU_SEPARATOR_HEIGHT);

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

    FlLayoutId item_id = menu_record_static_row(&ctx->menu_frame, label, UI_MENU_HEADER_HEIGHT);

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
    MenuRow row
        = menu_emit_row(ctx, m, mf, label, (FlString) { 0 }, cfg.disabled, /*is_submenu=*/true, MenuMarker_None, false);

    // Click is a per-surface open trigger (instant, no dwell): a click on a closed submenu parent - or on a sibling
    // while a different branch is open - branch-switches to it. Clicking the already-open parent is a no-op. The
    // click is consumed here as "open"; a submenu parent never dispatches an action signal to the caller.
    if (!cfg.disabled && row.signal.clicked && m->levels[parent_level].expanded_child_id != row.id) {
        menu_branch_switch(m, mf, parent_level, row.id, true);
    }

    // Open only while THIS submenu is the one expanded at its level (identity-keyed: compare resolved widget ids).
    bool open = !cfg.disabled && m->levels[parent_level].expanded_child_id == row.id;
    if (!open) {
        return false;
    }

    int child_level = parent_level + 1;
    FL_ASSERT_MSG(child_level < UI_MENU_MAX_DEPTH, "menu open-path overflow (fixed cap UI_MENU_MAX_DEPTH)");
    if (child_level >= UI_MENU_MAX_DEPTH) {
        return false; // release safety
    }

    mf->build_level = child_level;
    mf->levels[child_level].item_count = 0; // reset this level's per-frame build state before its items emit

    float min_w = cfg.min_width > 0.0f ? cfg.min_width : UI_MENU_DEFAULT_MIN_WIDTH;
    menu_submenu_panel_begin(ctx, m, row.id, parent_level, child_level, min_w);
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

FlSignal ui_menu_region_begin_impl(FlLayoutId region_id, FlLayoutId menu_id, FlMenuRegionConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();

    // Register the region as an interactive item (opting into right-click tracking so callers don't hand-wire the
    // flag) - this gives it a popup scope and a usable signal for callers that paint the region.
    FlSignal signal
        = ui_item_behavior(region_id, FlItemFlags_clickable | FlItemFlags_right_click, (FlWidgetConfig) { 0 });

    // Open on a right-click anywhere inside the region. The render-command-driven hot path only marks elements that
    // emit a render command, so a transparent catch-all region would never report hover; the cursor is tested
    // against the region's LAYOUT box (Clay tracks it whether or not the region paints anything). The box is last
    // frame's.
    if (ctx->input_state.mouse.buttons[FlMouseButton_Right]) {
        Clay_ElementData ed = Clay_GetElementData((Clay_ElementId) { .id = ui_layout_id_to_clay_id(region_id) });
        float mx = ctx->input_state.mouse.x;
        float my = ctx->input_state.mouse.y;
        if (ed.found && mx >= ed.boundingBox.x && mx < ed.boundingBox.x + ed.boundingBox.width && my >= ed.boundingBox.y
            && my < ed.boundingBox.y + ed.boundingBox.height) {
            ui_menu_open_impl(menu_id, (FlVec2) { mx, my });
        }
    }

    // Default to a grow/grow catch-all region when the caller didn't specify sizing (zero-init is Fit/Fit with no
    // bounds, treated here as "unset").
    FlLayoutSizing sizing = cfg.sizing;
    if (sizing.width.type == FlLayoutSizingType_Fit && sizing.width.size.min_max.max == 0.0f
        && sizing.height.type == FlLayoutSizingType_Fit && sizing.height.size.min_max.max == 0.0f) {
        sizing = (FlLayoutSizing) { sizing_grow(), sizing_grow() };
    }

    fl_layout_start(region_id);
    fl_layout_configure((FlLayoutConfig) { .sizing = sizing });

    return signal;
}

void ui_menu_region_end_impl(void) {
    fl_layout_end();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Menu bar - a SECOND surface over the menu core. The bar IS level 0, rendered as a persistent horizontal strip
// instead of a floating panel; each top's dropdown is level 1, anchored UNDER its bar button. State mutation goes
// through the same primitives ui_menu_begin_impl drives; the bar supplies only the per-surface open TRIGGER - a
// click, never the submenu's hover-dwell.

// Activate the bar chain on the closed -> open transition: arm the shared open path at depth 1 (bar level only, no
// dropdown yet), seeding level 0 with the bar's recorded tops so the caller can immediately branch-switch into the
// clicked one. Mirrors ui_menu_open_impl but with no point anchor (the strip is laid out, not floated) and with level 0
// taken from the bar's own record (its item identities drive the branch-switch that follows).
static void menubar_activate(FlUiCtx* ctx, UiMenuState* m, UiMenuFrame* mf, u32 scope) {
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
        menu_reset_level(m, mf, i);
    }

    // Level 0 becomes the bar's tops, so only its cross-open state clears.
    mf->levels[0] = mf->bar_tops;
    m->levels[0].expanded_child_id = 0; // nothing expanded yet → the bar is the deepest level
    m->levels[0].flip_left = false;
    m->levels[0].scroll_offset = 0.0f;   // the horizontal bar strip never scrolls, but keep the state clean
    m->levels[0].init_highlight = false; // the caller sets the highlight to the clicked top explicitly
}

// Resolve which top the pointer is over, scanning the bar's own prev-frame record (see menu_find_hot_item) rather
// than level 0, since this must work while the bar is passive - when level 0 holds another menu's rows and m->depth
// doesn't cover the bar. Returns the item index (and its navigability) or -1.
static int menubar_hot_top(const UiMenuFrame* mf, u32 hot_id, bool* out_nav) {
    *out_nav = false;
    if (hot_id == 0) {
        return -1;
    }
    const UiMenuLevelFrame* bar = &mf->bar_tops;
    int n = bar->item_count < UI_MENU_MAX_ITEMS ? bar->item_count : UI_MENU_MAX_ITEMS;
    for (int i = 0; i < n; i++) {
        if (bar->item_id[i] == hot_id) {
            *out_nav = bar->item_navigable[i];
            return i;
        }
    }
    return -1;
}

// Re-expand whichever top the bar-level highlight now points at, so a Left/Right keyboard move switches the OPEN root
// while a dropdown is showing. A non-submenu highlight collapses the dropdown.
static void menubar_reopen_under_highlight(UiMenuState* m, UiMenuFrame* mf) {
    UiMenuLevel* bar = &m->levels[0];
    UiMenuLevelFrame* barf = &mf->levels[0];
    int hi = bar->highlight_index;
    if (hi >= 0 && hi < UI_MENU_MAX_ITEMS && barf->item_submenu[hi]) {
        menu_branch_switch(m, mf, 0, barf->item_id[hi], true);
    } else {
        menu_collapse_to(m, 1);
    }
}

// Open/close arbitration for the bar chain: the passive click-to-open trigger, the click-outside / ESC dismissal,
// and the re-press-on-the-open-top release. Returns whether the bar owns the menu chain after this frame's
// transitions; the caller runs no further bar handling once it returns false, since that code reads m->depth, which
// a teardown has zeroed.
static bool menubar_arbitrate_open(FlUiCtx* ctx, UiMenuState* m, UiMenuFrame* mf, u32 scope, int hot_top, bool hot_nav,
                                   bool owned) {
    if (!owned) {
        // Passive: cold hover does NOTHING (the bar's per-surface trigger is click, not the submenu's hover-dwell). A
        // press on a navigable top opens it - activate the chain, then branch-switch into that top (level 0 -> 1).
        if (ctx->mouse_pressed_this_frame && hot_top >= 0 && hot_nav) {
            u32 top_id = mf->bar_tops.item_id[hot_top];
            menubar_activate(ctx, m, mf, scope);
            menu_branch_switch(m, mf, 0, top_id, true);
            m->levels[0].highlight_index = hot_top;
            m->hover_drives = true;
            return true;
        }
        return false;
    }

    if (ctx->frame_counter <= m->open_frame) {
        return true;
    }

    // Dismissal: a press outside the bar scope closes the whole chain; ESC pops one level (dropdown -> strip, then
    // strip -> closed), exactly as the context menu's ESC does at depth 1.
    if (ctx->mouse_pressed_this_frame && !menu_hot_in_scope(ctx, scope) && hot_top < 0) {
        menu_close_internal(ctx);
        return false;
    }
    if (ui_action_pressed(ctx, FlAction_Cancel) && menu_handle_cancel(ctx, m)) {
        return false;
    }

    // Re-pressing the OPEN top releases the chain entirely (the mouse's undo of the press that opened it), rather
    // than collapsing to the focused strip the way ESC's first pop does.
    if (ctx->mouse_pressed_this_frame && hot_top >= 0 && hot_nav
        && m->levels[0].expanded_child_id == mf->levels[0].item_id[hot_top]) {
        menu_close_internal(ctx);
        return false;
    }

    return true;
}

// Mouse and keyboard driving of an open bar chain: root switching from the bar strip, navigation inside the open
// dropdown, type-ahead, and the wheel. Reads the prev-frame item set, so it must precede the per-frame item_count
// reset.
static void menubar_handle_input(FlUiCtx* ctx, UiMenuState* m, UiMenuFrame* mf, int hot_top, bool hot_nav) {
    UiMenuLevel* bar = &m->levels[0];
    UiMenuLevelFrame* barf = &mf->levels[0];

    // Mouse: a press on a top switches the open root; hovering a sibling while a dropdown is open switches it
    // instantly (no dwell).
    if (ctx->mouse_pressed_this_frame && hot_top >= 0 && hot_nav) {
        menu_branch_switch(m, mf, 0, barf->item_id[hot_top], true);
        bar->highlight_index = hot_top;
        m->hover_drives = true;
    } else if (m->hover_drives && m->depth > 1 && hot_top >= 0 && hot_nav
               && bar->expanded_child_id != barf->item_id[hot_top]) {
        menu_branch_switch(m, mf, 0, barf->item_id[hot_top], true);
        bar->highlight_index = hot_top;
    }

    // Keyboard: Left/Right move between tops (and switch the open root if a dropdown is up); Up/Down navigate the
    // open dropdown; with no dropdown open, Down / Confirm opens the highlighted top.
    if (ui_action_pressed(ctx, FlAction_Nav_Left) || ui_action_repeated(ctx, FlAction_Nav_Left)) {
        menu_move_highlight(bar, barf, -1);
        m->hover_drives = false;
        if (m->depth > 1) {
            menubar_reopen_under_highlight(m, mf);
        }
    }
    if (ui_action_pressed(ctx, FlAction_Nav_Right) || ui_action_repeated(ctx, FlAction_Nav_Right)) {
        menu_move_highlight(bar, barf, +1);
        m->hover_drives = false;
        if (m->depth > 1) {
            menubar_reopen_under_highlight(m, mf);
        }
    }
    // Tracks a keyboard move of the deepest (dropdown) highlight, so scroll-into-view runs only then - never
    // re-centering a wheel scroll or a stationary highlight.
    bool nav_moved = false;
    if (m->depth > 1) {
        int deepest = m->depth - 1;
        if (ui_action_pressed(ctx, FlAction_Nav_Down) || ui_action_repeated(ctx, FlAction_Nav_Down)) {
            menu_move_highlight(&m->levels[deepest], &mf->levels[deepest], +1);
            m->hover_drives = false;
            nav_moved = true;
        }
        if (ui_action_pressed(ctx, FlAction_Nav_Up) || ui_action_repeated(ctx, FlAction_Nav_Up)) {
            menu_move_highlight(&m->levels[deepest], &mf->levels[deepest], -1);
            m->hover_drives = false;
            nav_moved = true;
        }
    } else {
        int hi = bar->highlight_index;
        bool hi_top = hi >= 0 && hi < UI_MENU_MAX_ITEMS && barf->item_submenu[hi];
        if (hi_top && (ui_action_pressed(ctx, FlAction_Nav_Down) || ui_action_pressed(ctx, FlAction_Confirm))) {
            menu_branch_switch(m, mf, 0, barf->item_id[hi], true);
            m->hover_drives = false;
        }
    }

    // Type-ahead over the deepest open level: within a dropdown when one is open, else across the bar's own tops.
    // Purely a highlight move within the current level - an open dropdown stays put (unlike Left/Right, which
    // switch the open root).
    if (menu_typeahead(ctx, m, mf, m->depth - 1)) {
        m->hover_drives = false;
        nav_moved = true;
    }

    // Scroll the deepest open level's highlight into view when the keyboard moved it (over-tall dropdown); a no-op
    // on the horizontal bar strip and when it fits.
    if (nav_moved) {
        menu_scroll_into_view(ctx, m, mf, m->depth - 1);
    }
    // Mouse wheel scrolls the hovered over-tall dropdown (prev-frame set, so before the reset below).
    menu_apply_wheel(ctx, m, mf);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void ui_menubar_begin_impl(FlLayoutId bar_id, FlMenuBarConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;
    u32 scope = ui_layout_id_to_clay_id(bar_id);
    mf->menubar_scope = scope;
    bool owned = m->active && m->owner_scope_id == scope;

    menu_track_pointer_move(ctx, m);

    // An owning bar's tops are the chain's level 0, so the shared navigation helpers read them there. A passive bar
    // leaves level 0 alone: it belongs to whichever menu owns the chain.
    if (owned) {
        mf->levels[0] = mf->bar_tops;
    }

    // Which top is the pointer over (prev-frame set)? Used by both the passive click-to-open trigger and the
    // active hover-to-switch trigger.
    bool hot_nav = false;
    int hot_top = menubar_hot_top(mf, ctx->hot_id, &hot_nav);

    owned = menubar_arbitrate_open(ctx, m, mf, scope, hot_top, hot_nav, owned);

    if (owned && ctx->frame_counter > m->open_frame) {
        menubar_handle_input(ctx, m, mf, hot_top, hot_nav);
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

bool ui_menubar_menu_begin_impl(FlString label, FlMenuBarMenuConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;
    const FlThemeColors* theme = ui_get_theme_colors(ctx);
    u32 scope = mf->menubar_scope;
    bool owned = m->active && m->owner_scope_id == scope;

    // Record the top into the bar's own record (identity + navigability), like a menu row; while the bar owns the
    // chain that record is its level 0, so the shared branch-switch / highlight / hot-item logic operates on it. A top
    // behaves as a submenu parent: it opens a child level, never dispatches an action.
    int index = mf->bar_tops.item_count++;
    FlLayoutId top_id = { .name = label, .id = index };
    u32 rid = ui_layout_id_to_clay_id(top_id);
    FlSignal signal = ui_item_behavior(top_id, FlItemFlags_clickable, (FlWidgetConfig) { 0 });

    bool navigable = !cfg.disabled;
    if (index < UI_MENU_MAX_ITEMS) {
        mf->bar_tops.item_navigable[index] = navigable;
        mf->bar_tops.item_id[index] = rid;
        mf->bar_tops.item_submenu[index] = navigable;   // a navigable top is a container that opens a dropdown
        menu_record_label(&mf->bar_tops, index, label); // type-ahead over the bar's tops as well as the dropdowns
    }
    if (owned && navigable && m->levels[0].init_highlight) {
        m->levels[0].highlight_index = index;
        m->levels[0].init_highlight = false;
    }

    bool is_open_top = owned && m->levels[0].expanded_child_id == rid;
    // Visual highlight: while the bar owns the chain, the managed bar-level highlight (keyboard/hover) and the open
    // root both read as "current"; while passive, a plain hover affordance.
    bool highlighted = navigable && (owned ? (m->levels[0].highlight_index == index || is_open_top) : signal.hovered);

    FlColor bg = highlighted ? theme->surface.selected : theme->surface.elevated;
    FlColor txt = cfg.disabled ? theme->text.secondary : theme->text.primary;

    ui_layout_area_id(top_id, .sizing = { sizing_fit(), sizing_fixed(UI_MENUBAR_HEIGHT) },
                      .direction = LayoutDirection_LeftToRight, .child_alignment = ui_align(Center, Center),
                      .padding = fl_padding_hv(FlSpacing_SM, FlSpacing_XS), .radius = corner_radius(FlRadius_SM),
                      .color = bg) {
        ui_text_styled_impl(label, (FlTextConfig) { .color = txt, .valign = FlTextVAlign_Center, .no_wrap = true });
    }

    if (!is_open_top) {
        return false;
    }

    // The dropdown is level 1: a floating panel anchored UNDER the bar button (element-anchor, so the popup core opens
    // it below the top). It reuses the menu scope (ui_popup_begin inherits the armed popup_focus_scope_id), so its
    // items register into the one shared scope.
    int child_level = 1;
    mf->build_level = child_level;
    mf->levels[child_level].item_count = 0;

    float min_w = cfg.min_width > 0.0f ? cfg.min_width : UI_MENU_DEFAULT_MIN_WIDTH;
    ui_popup_begin(top_id, (FlPopupConfig) { .anchor = FlPopupAnchor_Element,
                                             .z_index = mf->z_base,
                                             .capture_input = true,
                                             .auto_close = false });
    // Dropdown panel (height-capped + scrollable when the dropdown is over-tall; sizing_fit otherwise).
    fl_layout_start_auto();
    menu_configure_panel(ctx, child_level, min_w);
    return true;
}

void ui_menubar_menu_end_impl(void) {
    FlUiCtx* ctx = ui_get_ctx();
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    fl_layout_end(); // close the dropdown panel container
    ui_popup_end();  // close the floating element (clears current_popup_scope_id)
    mf->build_level = 0;

    // ui_popup_end zeroed current_popup_scope_id; restore it so tops emitted AFTER the open one still register in the
    // bar scope (click-outside membership / hover-switch).
    if (m->active && m->owner_scope_id == mf->menubar_scope) {
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
    UiMenuState* m = &ctx->shared->menu;
    return m->active && m->owner_scope_id == ui_layout_id_to_clay_id(bar_id);
}
