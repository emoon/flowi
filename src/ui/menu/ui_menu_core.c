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

// Menu panel width in logical px - wide enough for a label plus a right-aligned shortcut; items grow to fill it so
// the hover highlight spans the full row.
#define UI_MENU_DEFAULT_MIN_WIDTH 180.0f
// Logical-px gap kept between an over-tall (scrolling) menu level and the top/bottom viewport edges, so a height-capped
// panel's border doesn't sit flush against the screen. Only affects levels tall enough to scroll.
#define UI_MENU_VIEWPORT_MARGIN 8.0f
// Wheel pixels scrolled per notch over a menu level (matches the scroll-area default feel).
#define UI_MENU_WHEEL_SPEED 30.0f
// Vertical padding (top + bottom) the menu panel reserves around its item list - padding_all(FlSpacing_XS) on both
// panel styles. Folded out of the viewport when sizing the scrollable interior so the cap math stays in content space.
#define UI_MENU_PANEL_VPAD (2.0f * (float)FlSpacing_XS)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Menu core - the open-path state machine, navigation, type-ahead and scrolling shared by every menu surface.

// Whether the chain is open and owned by the surface with this scope id.
bool menu_owned_by(const FlUiCtx* ctx, u32 scope) {
    return ctx->shared->menu.active && ctx->shared->menu.owner_scope_id == scope;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Tear down the open chain: drop the open path and release the popup focus scope it armed (restoring focus). Safe
// to call whether the scope is still the menu's or already gone.
void menu_close_internal(FlUiCtx* ctx) {
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Clear one level's per-frame build state and cross-open state, so it reopens fresh: no highlight yet, no expanded
// child, no flip, scrolled to the top.
void menu_reset_level(FlUiCtx* ctx, int level) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    m->levels[level].highlight_index = -1;
    mf->levels[level].item_count = 0;
    m->levels[level].init_highlight = true; // first navigable item claims the highlight as items emit this frame
    m->levels[level].expanded_child_id = 0;
    m->levels[level].flip_left = false;
    m->levels[level].scroll_offset = 0.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Update the last-input-wins latch from this frame's pointer position and return the pointer's movement. A pointer
// that actually moved reclaims the shared highlight from the keyboard; a stationary one leaves a keyboard selection
// alone. menu_emit_row only lets hover write the highlight while hover_drives is set.
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

// Collapse the open path so new_depth levels remain open: every level at or beyond new_depth drops its expanded
// child, so the deepest kept level becomes a leaf again. Resets the dwell/grace accumulator so a stale hot-item timer
// can't fire across the collapse.
void menu_collapse_to(UiMenuState* m, int new_depth) {
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Level-agnostic branch switch: collapse the open path back to level, then expand the sibling child_id at that
// level if it is a submenu parent. A leaf "expands" to nothing - it just collapses the deeper path. The new deepest
// level re-initializes its highlight to the first navigable child. Overflow past the fixed depth cap asserts in debug.
void menu_branch_switch(FlUiCtx* ctx, int level, u32 child_id, bool child_is_submenu) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The index of the recorded row with widget id `id` in the level's prev-frame set, or -1.
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

// Locate the menu item the pointer is over by scanning the recorded item set of every open level.
//
// PREV-FRAME SET: the level arrays hold the PREVIOUS frame's emission, which is exactly what ctx->hot_id was computed
// against, so every reader of them - this function, menu_move_highlight, menu_typeahead, the scroll geometry, and
// begin() itself - must run before this frame's items re-emit and reset the per-frame counters.
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

// Click-outside test: is the hot widget (the one the pointer is over) part of the menu's popup scope? The whole open
// chain shares one scope id, so a single membership check covers the union of all open menu rects.
static bool menu_hot_in_scope(FlUiCtx* ctx, u32 scope) {
    if (ctx->hot_id == 0) {
        return false;
    }
    ItemState* hot = hashmap_get(&ctx->shared->item_state, ctx->hot_id);
    return hot != nullptr && hot->popup_scope_id == scope;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Move the level's highlight by dir (+1 down, -1 up) to the next navigable item, wrapping at the ends and skipping
// non-navigable rows. Reads the prev-frame set (see menu_find_hot_item); a menu's contents are stable frame to
// frame. No-op when the level has no navigable items. A highlight of -1 (none yet) steps to the first item in the
// travel direction.
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

// Record a bounded copy of an emitted row's label into its level slot for type-ahead, truncating at the prefix cap.
// A copy, never the borrowed FlString, so the bytes stay valid at the next begin() where matching runs.
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

// Type-ahead: fold this frame's typed text into the menu's prefix and move level's highlight to the matching item,
// cycling on a repeated single letter. Reads the prev-frame set's labels and navigability (see menu_find_hot_item);
// non-navigable rows are presented to the matcher as empty labels so they never match. Returns true when it moved the
// highlight, which also hands the highlight to the keyboard.
bool menu_typeahead(FlUiCtx* ctx, int level) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    UiMenuLevel* lvl = &m->levels[level];
    const UiMenuLevelFrame* lf = &mf->levels[level];

    // Per-frame snapshot, not the raw kb->text_input which ui_input_update clears before the
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
    m->hover_drives = false; // the keyboard moved last, so it owns the highlight until the pointer moves again
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Record one emitted row's scroll geometry: its content-space top (relative to the first row, panel padding excluded)
// and height, plus the level's running natural height. The running height restarts as the level's first row (index 0)
// emits, so the natural_height read back at the next begin() - before this frame's first row re-emits - is the previous
// frame's complete total.
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Configure a menu level's panel container - shared by the context-menu root, submenu panels, and menu-bar dropdowns.
// When the level is over-tall the panel is height-capped and gets a vertical clip with the level's manual scroll
// offset; when it fits it stays sizing_fit. Clamps the offset to this frame's geometry first, so a level whose content
// shrank snaps back in range.
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Mouse wheel over the open chain scrolls the HOVERED level only (the one the pointer is over), never a parent. Applies
// the frame's wheel delta to that level's offset, clamped to its scrollable range. A no-op unless the pointer is over a
// menu item whose level is over-tall.
void menu_apply_wheel(FlUiCtx* ctx) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    FlVec2 wheel = ui_get_mouse_wheel();
    MenuHit hit = menu_find_hot_item(ctx); // pointer not over a menu item → nothing to scroll
    if (wheel.y == 0.0f || hit.level < 0) {
        return;
    }
    MenuScrollGeom g = menu_scroll_geom(ctx, &mf->levels[hit.level]);
    if (g.max_offset <= 0.0f) {
        return;
    }
    // wheel.y > 0 is a scroll-up gesture → content moves down → offset decreases (matches the scroll-area sign).
    f32 off = m->levels[hit.level].scroll_offset - wheel.y * UI_MENU_WHEEL_SPEED;
    m->levels[hit.level].scroll_offset = clamp_f32_impl(off, 0.0f, g.max_offset);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Whether row i has a recorded slot in the level's prev-frame set.
static bool menu_row_recorded(const UiMenuLevelFrame* lf, int i) {
    return i >= 0 && i < UI_MENU_MAX_ITEMS && i < lf->item_count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The scroll offset that brings a row (content-space top + height) fully inside the window with the minimal move: a
// row above the window aligns its top to the window top, one below aligns its bottom to the window bottom.
static f32 scroll_offset_to_show(f32 off, f32 top, f32 height, f32 viewport) {
    if (top < off) {
        return top;
    }
    f32 bottom = top + height;
    return bottom > off + viewport ? bottom - viewport : off;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Scroll level's highlighted row fully into view with the minimal adjustment in either direction. Reads the
// prev-frame set's row geometry against this frame's just-moved highlight_index. A no-op when the level fits or the
// highlight is out of the recorded range.
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

// Whether the level's highlight sits on a submenu parent (a container that opens a child level).
bool menu_highlight_is_submenu(const UiMenuLevel* level, const UiMenuLevelFrame* frame) {
    int hi = level->highlight_index;
    return hi >= 0 && hi < UI_MENU_MAX_ITEMS && frame->item_submenu[hi];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Move the level's highlight one row in `dir` when `action` fires (press edge or nav-repeat). The keyboard owns the
// highlight afterwards. Returns whether it moved.
bool menu_step_highlight(FlUiCtx* ctx, int level, FlAction action, int dir) {
    if (!ui_action_pressed_or_repeated(ctx, action)) {
        return false;
    }

    menu_move_highlight(&ctx->shared->menu.levels[level], &ctx->menu_frame.levels[level], dir);
    ctx->shared->menu.hover_drives = false;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The panel's minimum width: the caller's, or the default when unset.
float menu_min_width(float requested) {
    return requested > 0.0f ? requested : UI_MENU_DEFAULT_MIN_WIDTH;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Self-owned dismissal, past the opening-frame guard (see UiMenuState.open_frame). ESC is read through the action
// system (FlAction_Cancel), not keys_pressed, which ui_input_update clears before the frame body runs. ESC pops ONE
// level (closing the chain only at the root); a click outside the union of all open rects closes the whole chain.
// Leaving the cursor in empty space is sticky and does NOT close. `pointer_on_surface` exempts a press on the
// surface's own always-present strip (the menu bar's tops). Returns whether the chain closed.
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

// Open the chain for the surface with this scope id at depth 1 (its root level only), clearing the transient state of
// every level from first_level down. The caller seeds whatever per-surface state level 0 needs.
void menu_arm_chain(FlUiCtx* ctx, u32 scope, int first_level) {
    UiMenuState* m = &ctx->shared->menu;
    UiMenuFrame* mf = &ctx->menu_frame;

    m->active = true;
    m->owner_scope_id = scope;
    m->open_frame = ctx->frame_counter;
    m->depth = 1; // a single flat root level; submenus extend the path as they open
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

// Own the focus scope, taking over from any other armed popup/dropdown scope. The menu owns ESC/click-outside itself,
// so the popup layer's auto-close is disabled.
void menu_arm_focus_scope(FlUiCtx* ctx, u32 scope) {
    ui_focus_scope_arm(ctx, scope, true);
    ctx->popup_auto_close = false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Start building `level`: its items re-emit from index 0 this frame.
void menu_begin_level(FlUiCtx* ctx, int level) {
    ctx->menu_frame.build_level = level;
    ctx->menu_frame.levels[level].item_count = 0;
}
