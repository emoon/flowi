///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// List API Implementation (CONTEXT.md "Lists")
//
// A thin adapter over list_uniform's virtualization core: ui_list_begin/_item/_end delegate the scroll engine
// and the uniform-height layout to list_uniform, and layer identity, per-item interaction, and the
// (multi-)selection gesture state machine on top. Once focused (clicked or Tab-reached) the list also runs
// keyboard nav, consuming the nav keys it handles so they do not also drive global focus navigation.
//
// What persists across frames is only the gesture anchor, the keyboard focus cursor and list-level focus, in
// list-level state keyed by the list's id. The resolved selection lives solely in the caller's FlSelectionSet,
// and the highlight is derived from id membership in it, so a row selected while off-screen highlights
// correctly once scrolled back into view.
//
// Drag-reorder returns on release a single request `{ drag_from_ids[], drag_target_id, drag_rel }`; the app
// owns the model mutation and drop validity. Dragging any row of a multi-selection drags the whole selection.
// The candidate drop position is resolved arithmetically from content-space Y against MODEL POSITIONS (uniform
// stride), so it reaches rows virtualized away exactly like the marquee coverage does.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "ui_list.h"
#include <core/arena.h>
#include <core/math.h>
#include <core/hashmap.h>
#include <core/log.h>
#include "list_uniform.h"
#include "type_ahead.h"
#include <ui/ui_theme.h>
#include <flowi/ui/item_behavior.h>
#include "layout.h"
#include "ui.h"
#include "ui_private.h"
#include <external/clay/clay.h>

#include <assert.h>
#include <math.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constants

#define LIST_ROW_DEFAULT_HEIGHT 32.0f // Fallback row height when the config leaves it unset
#define LIST_ROW_DEFAULT_GAP 8.0f     // Fallback gap between rows

// Per-row interaction the scoped face opts into by default: clicks (also gates hover), double-click and
// right-click.
#define LIST_ITEM_FLAGS (FlItemFlags_clickable | FlItemFlags_double_click | FlItemFlags_right_click)

// Upper bound on Selection Requests one frame can emit: a gesture is at most ClearAll + SetRange/SetOne (2),
// and Ctrl+A is one SetAll - never both in the same frame, so 4 is comfortably above any real frame. The marquee
// also emits at most ClearAll + SetRange (2), and never co-occurs with a click or Ctrl+A in one frame.
#define LIST_MAX_REQUESTS_PER_FRAME 4

// Element-id seeds for the marquee overlay, derived from the list's own name so they never collide with rows
// (which seed {name, pos + 1}). The rectangle is a floating element with a single colored child.
#define LIST_MARQUEE_FLOAT_SEED 0x4d515251 // 'MQRQ'
#define LIST_MARQUEE_FILL_SEED 0x4d515200  // 'MQR\0'

// Edge auto-scroll while a marquee drag is held past the viewport: base speed plus a gain on how far past the edge
// the cursor is, the overshoot capped so a cursor parked at the window edge doesn't fling the list. Pixels/second,
// integrated by the real frame delta so the rate is frame-rate independent.
#define LIST_AUTOSCROLL_BASE_SPEED 140.0f
#define LIST_AUTOSCROLL_GAIN 9.0f
#define LIST_AUTOSCROLL_MAX_OVERSHOOT 48.0f

// Keyboard focus cursor: the ring drawn on the cursor row (theme border.focus), distinct from the selection
// highlight fill, so the cursor is visible even on an unselected row.
#define LIST_FOCUS_BORDER_WIDTH 2

// Drag-reorder overlay seeds (derived from the list name so they never collide with rows or the marquee).
#define LIST_DRAG_GHOST_FLOAT_SEED 0x44475348 // 'DGSH'
#define LIST_DRAG_GHOST_FILL_SEED 0x44475300  // 'DGS\0'
#define LIST_DRAG_LINE_FLOAT_SEED 0x444c4e45  // 'DLNE'
#define LIST_DRAG_LINE_FILL_SEED 0x444c4e00   // 'DLN\0'
#define LIST_DRAG_GHOST_ALPHA 200             // ghost fill opacity (rows show through faintly)
#define LIST_DRAG_LINE_THICKNESS 2.0f         // insertion line height (logical px)

// Declarative-face row content: the icon + label the List itself draws for ui_list rows.
#define LIST_DECL_ROW_PAD_X 6     // horizontal padding inside a declarative row (logical px)
#define LIST_DECL_ICON_GAP 8      // gap between the leading icon and the label (logical px)
#define LIST_DECL_ICON_INSET 4.0f // shrink the icon square this much from the row height, top and bottom

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// List Context

// Per-list state persisted across frames, keyed by the list's id (string-id derived). Holds ONLY the gesture
// anchor and list-level focus - never the resolved selection (that lives in the caller's FlSelectionSet).
typedef struct ListState {
    u64 anchor_id;  // Stable id of the gesture anchor (range pivot); 0 = none yet.
    bool has_focus; // List-level focus - a click in this list or a Tab into it; gates keyboard nav + Ctrl+A.

    // Keyboard focus cursor - the row the arrow keys move (CONTEXT `## Lists`). Distinct from anchor_id: the
    // cursor roves on plain arrows without touching the selection, while the anchor is the shift-extend pivot.
    // A stable id (0 = unset), so it survives virtualization and re-sorts just like the selection ids do.
    u64 focus_cursor_id;

    // A focus seed was requested this frame (ui_list_request_focus) before the model was known: begin
    // validates focus_cursor_id against the ordered slice and falls back to the first row, then clears this.
    // Scoped to the seed frame so a roving cursor is never re-snapped on a later frame.
    bool focus_seed_pending;

    // Type-ahead prefix accumulated across frames while the list holds focus; the shared matcher owns the
    // accumulate/match/expire logic (type_ahead.h).
    TypeAheadState type_ahead;

    // Marquee (rubber-band) gesture. Armed by an empty-space press, latched live once the cursor crosses the
    // click-vs-drag threshold (the FlItem_DragThreshold substrate). The anchor is kept in CONTENT space (pixels
    // from the top of the content, not screen space) so it stays put while edge auto-scroll mutates the visible
    // range mid-gesture.
    bool marquee_pressed;         // a left press landed on empty list space; tracking toward the threshold
    bool marquee_active;          // threshold crossed - the marquee is live (selecting + drawn)
    bool marquee_additive;        // ctrl/⌘ held at gesture start: union with the existing selection
    f32 marquee_anchor_content_y; // anchor Y in content space (survives scroll)
    f32 marquee_anchor_x;         // anchor X in window space (drawing only - coverage is Y-only for full-width rows)

    // Drag-reorder gesture (CONTEXT.md Drop Relationship). Armed by a press that landed on a row, latched live
    // once the cursor crosses the click-vs-drag threshold. Only the pressed row id persists across frames - the
    // dragged-id set (whole selection vs the single row) is recomputed from live membership each frame, since the
    // selection cannot change while the drag holds (row clicks are suppressed).
    bool drag_pressed; // a left press landed on a row; tracking toward the threshold
    bool drag_active;  // threshold crossed - the reorder drag is live (ghost + insertion line drawn)
    u64 drag_press_id; // stable id of the row the press landed on (selected => drag the whole selection)
} ListState;

struct FlListContext {
    FlArena* arena;
    FlUiCtx* ui_ctx;

    hashmap(u32, ListState) list_states; // list clay id -> persisted anchor/focus

    // Current list being emitted (set in begin)
    FlLayoutId current_id;      // Identifies the list container; row ids derive from its name
    u32 current_clay_id;        // ui_layout_id_to_clay_id(current_id) - key into list_states
    const u64* ordered_ids;     // App's full ordered model, borrowed for this begin->end scope (may be NULL)
    u32 ordered_count;          // Length of ordered_ids and the virtualization total
    const FlString* labels;     // Optional per-row labels for type-ahead (length ordered_count); may be NULL
    FlSelectionSet* set;        // Caller-owned selection storage (NULL = opt out of auto-apply/highlight)
    u32 emit_pos;               // Auto-tracked emission position of the next row (absolute index)
    f32 current_row_height;     // Resolved row height this frame (marquee maps content Y -> row index with this)
    f32 current_gap;            // Resolved inter-row gap this frame
    bool suppress_drag_visuals; // config.suppress_drag_visuals this frame (gates the built-in ghost/insertion line)

    // Policy toggles resolved at begin (default off = the scoped face's always-multi-select, always-reorderable
    // behavior). The declarative face (ui_list) sets them from FlListOpts; the scoped begin leaves them off.
    bool single_select;   // true: ignore ctrl/shift/marquee/Ctrl+A - plain click replace only (single-selection).
    bool disable_reorder; // true: a row press never arms a drag-reorder gesture.

    // Per-frame Selection Requests (frame-arena backed; returned on the result and auto-applied at end).
    FlSelectionRequest* requests;
    u32 req_count;
    u32 req_capacity;

    bool frame_row_pressed; // Did a row of the current list take a press this frame? (focus tracking)

    // Keyboard focus, resolved at begin and read by item_begin to draw the cursor ring. has_kb_focus is the
    // list-holds-focus verdict for this frame; frame_focus_cursor_id is the cursor row's stable id (0 = none).
    bool has_kb_focus;
    u64 frame_focus_cursor_id;

    // Result tracking (keyed on app-supplied stable ids; 0 = none)
    u64 activated_id;
    u64 focused_id;
    u64 context_menu_id; // Stable id of the row right-clicked this frame (policy outcome; app opens its own menu)
    bool nav_up_edge;    // Nav-Up while the cursor was already on the first row
    bool nav_down_edge;  // Nav-Down while the cursor was already on the last row

    // Drag-reorder, surfaced on FlListResult. frame_drag_active is resolved at begin (from the persisted
    // gesture + the input layer's drag distance) so item_begin can suppress row clicks for the whole emission;
    // the rest is filled by the drag machine in list_end. The drag_from_ids buffer is frame-arena backed.
    bool frame_drag_active;      // a reorder drag is live this frame (set at begin; gates row-click suppression)
    bool drag_active_out;        // result.drag_active - live drag exposed for visual override
    bool reorder_emitted;        // result.reorder_emitted - a reorder request was produced this frame (on release)
    const u64* drag_from_ids;    // result.drag_from_ids - dragged stable ids (frame-arena backed)
    u32 drag_from_count;         // result.drag_from_count
    u64 drag_target_id;          // result.drag_target_id - candidate, then committed, drop target
    FlDropRelationship drag_rel; // result.drag_rel - Before/After (Into is never produced)
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helpers

// Fetch (creating if absent) the persisted state for the list keyed by clay_id.
static ListState* list_state_get(FlListContext* self, u32 clay_id) {
    ListState* st = hashmap_get(&self->list_states, clay_id);
    if (st == nullptr) {
        ListState fresh = { 0 };
        st = hashmap_insert(&self->list_states, clay_id, fresh);
    }

    FL_ASSERT(st != nullptr);
    return st;
}

// Append one Selection Request to this frame's array (capacity-guarded - see LIST_MAX_REQUESTS_PER_FRAME).
static void list_push_request(FlListContext* self, FlSelectionRequest req) {
    if (self->req_count < self->req_capacity) {
        self->requests[self->req_count++] = req;
    }
}

// Position of stable id id in the ordered slice, or -1 if absent (or id == 0, the "none" sentinel). Linear
// scan: the same enumeration selection_set_apply already walks, and the cursor/anchor mapping is at most once
// per nav keystroke, never per row.
static i32 list_index_of_id(const FlListContext* self, u64 id) {
    if (id == 0 || self->ordered_ids == nullptr) {
        return -1;
    }
    for (u32 i = 0; i < self->ordered_count; i++) {
        if (self->ordered_ids[i] == id) {
            return (i32)i;
        }
    }
    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Feed this frame's typed text into the shared type-ahead matcher and return the row it should jump to (or -1
// when there is nothing to jump to). cur is the current cursor index (-1 if unset); the matcher owns the
// cycle-vs-narrow and idle-reset behavior (see type_ahead.h).
static i32 list_typeahead(FlListContext* self, ListState* st, i32 cur) {
    if (self->labels == nullptr) {
        return -1;
    }
    // Per-frame snapshot of the typed text.
    FlString typed = self->ui_ctx->text_input_this_frame;
    f64 now = self->ui_ctx->input_state.time;
    return type_ahead_match(&st->type_ahead, self->labels, self->ordered_count, cur, typed, now, TYPE_AHEAD_CAP);
}

// Run the keyboard navigation state machine for a focused list, at begin (before list_uniform_begin so a cursor
// move can scroll the target into view this same frame). Moves the focus cursor on arrows / Home / End /
// type-ahead, extends the selection on Shift+arrows, selects all on Ctrl+A, and activates the cursor row on
// Enter / Space. Consumes the nav-direction keys it acts on so they do not also drive global focus navigation
// Returns the index to scroll to (-1 = leave scrolling alone).
static i32 list_handle_keyboard(FlListContext* self, ListState* st, bool has_focus) {
    if (!has_focus || self->ordered_ids == nullptr || self->ordered_count == 0) {
        return -1;
    }

    u32 count = self->ordered_count;
    u32 mods = ui_keyboard_modifiers();
    bool shift = (mods & FlKeyModifier_Shift) != 0;
    bool ctrl = (mods & FlKeyModifier_Control) != 0;
    i32 cur = list_index_of_id(self, st->focus_cursor_id);

    // Ctrl+A - select all, resolved against the full ordered slice (covers rows virtualized away). It does not
    // move the cursor; emitted regardless of set, only auto-apply is gated on set != NULL (see list_end_impl).
    // A single-selection list (declarative multi_select = false) has no "select all", so it is suppressed there.
    if (!self->single_select && ctrl && ui_key_pressed(FlKey_A)) {
        list_push_request(self, (FlSelectionRequest) { .kind = FlSelectionRequestKind_SetAll, .selected = true });
        return -1;
    }

    FlNavFrame nav = fl_get_nav_frame();
    i32 target = cur;
    bool moved = false;

    if (nav.nav_down) {
        fl_consume_nav_down(); // this list owns Down while focused - don't also spatial-nav away
        if (cur < 0) {
            target = 0;
        } else if (cur >= (i32)count - 1) {
            self->nav_down_edge = true;
        } else {
            target = cur + 1;
        }
        moved = (target != cur);
    } else if (nav.nav_up) {
        fl_consume_nav_up();
        if (cur < 0) {
            target = (i32)count - 1;
        } else if (cur == 0) {
            self->nav_up_edge = true;
        } else {
            target = cur - 1;
        }
        moved = (target != cur);
    } else if (ui_key_pressed(FlKey_Home)) {
        target = 0;
        moved = (cur != 0);
    } else if (ui_key_pressed(FlKey_End)) {
        target = (i32)count - 1;
        moved = (cur != (i32)count - 1);
    } else {
        i32 ta = list_typeahead(self, st, cur);
        if (ta >= 0) {
            target = ta;
            moved = (ta != cur);
        }
    }

    // Enter / Space activate the cursor row (committed id on the result), independent of any move this frame.
    if (ui_key_pressed(FlKey_Enter) || ui_key_pressed(FlKey_Kp_Enter) || ui_key_pressed(FlKey_Space)) {
        i32 c = (target >= 0) ? target : cur;
        if (c >= 0) {
            self->activated_id = self->ordered_ids[c];
        }
    }

    if (!moved || target < 0) {
        return -1;
    }

    u64 new_id = self->ordered_ids[target];
    bool directional = nav.nav_up || nav.nav_down || ui_key_pressed(FlKey_Home) || ui_key_pressed(FlKey_End);

    if (!self->single_select && shift && directional) {
        // Extend from the anchor to the new cursor (endpoint-only range - survives virtualization through
        // selection_set_apply). The anchor stays put so a continued shift-arrow re-ranges from the same pivot.
        u64 anchor = st->anchor_id;
        if (list_index_of_id(self, anchor) < 0) {
            anchor = (cur >= 0) ? self->ordered_ids[cur] : new_id;
            st->anchor_id = anchor;
        }
        list_push_request(self, (FlSelectionRequest) { .kind = FlSelectionRequestKind_ClearAll });
        list_push_request(self, (FlSelectionRequest) {
                                    .kind = FlSelectionRequestKind_SetRange,
                                    .id = anchor,
                                    .to_id = new_id,
                                    .selected = true,
                                });
    } else {
        // Plain cursor move: the selection is untouched (cursor is distinct from selection); the anchor follows
        // the cursor so a later Shift+arrow extends from where the cursor came to rest.
        st->anchor_id = new_id;
    }

    st->focus_cursor_id = new_id;
    return target;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Context Creation/Destruction

FlListContext* ui_list_create(FlArena* arena) {
    FlListContext* ctx = arena_alloc_zero(arena, FlListContext);
    ctx->arena = arena;
    ctx->ui_ctx = ui_get_ctx();
    hashmap_new(&ctx->list_states, arena, 8);

    log_info("List context created");
    return ctx;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Promote an armed press (item_begin) to a live drag once the drag distance crosses the threshold, latching until
// release. Runs BEFORE any row is emitted so item_begin can suppress clicks for the whole list, and disarms on
// disable_reorder so a press armed on an earlier frame cannot promote after the flag flips.

static void list_resolve_drag_promotion(FlListContext* self, ListState* st, bool disable_reorder) {
    if (disable_reorder) {
        st->drag_pressed = false; // drop any stale armed press so the marquee/click paths aren't suppressed
        st->drag_active = false;
        return;
    }

    if (!st->drag_pressed) {
        return;
    }

    bool held = self->ui_ctx->input_state.mouse.buttons[FlMouseButton_Left];
    f32 drag_sqr = self->ui_ctx->input_state.mouse.drag_max_distance_sqr[FlMouseButton_Left];
    if (st->drag_active || (held && drag_sqr > UI_MOUSE_DRAG_THRESHOLD * UI_MOUSE_DRAG_THRESHOLD)) {
        st->drag_active = true;
    }
    self->frame_drag_active = st->drag_active;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Join the global focus ring and resolve whether the list holds focus for keyboard handling. The list container
// ({name, 0}, the clay id list_uniform builds it under) is registered focusable per-frame, so Tab can reach it and
// spatial nav treats it as the focus owner; the element itself is emitted by list_uniform_begin later, and
// item_behavior only reads the prior frame's rect, so registering here is fine.
//
// A click (item_begin / marquee) sets has_focus and the global focused_id together; a Tab into the list arrives as
// global focus + a nav-next/prev press; and global focus leaving for another widget blurs the list, so only the
// focused region handles keys.

static bool list_resolve_focus(FlListContext* self, ListState* st, FlLayoutId id) {
    fl_item_behavior((FlLayoutId) { .name = id.name, .id = 0 }, FlItemFlags_focusable);

    u32 global_focus = self->ui_ctx->focused_id;
    if (global_focus == self->current_clay_id) {
        if (self->ui_ctx->nav_next_pressed || self->ui_ctx->nav_prev_pressed) {
            st->has_focus = true; // Tab-reached
        }
    } else if (global_focus != 0) {
        st->has_focus = false; // another widget owns focus now
    }

    return st->has_focus;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Resolve a focus seed requested this frame (ui_list_request_focus). The caller chose cursor_id before the model
// was known, so validate it now against the ordered slice and fall back to the first row when it is 0 or absent.
// Only on the seed frame - a roving cursor is never re-snapped.

static void list_resolve_focus_seed(FlListContext* self, ListState* st) {
    if (!st->focus_seed_pending) {
        return;
    }

    st->focus_seed_pending = false;
    if (list_index_of_id(self, st->focus_cursor_id) < 0) {
        st->focus_cursor_id = (self->ordered_count > 0 && self->ordered_ids != nullptr) ? self->ordered_ids[0] : 0;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core implementation (ctx explicit - the service API points here; the scoped face forwards with the global ctx)

// Begin core, with the declarative face's policy toggles threaded in. The scoped list_begin_impl forwards
// (false, false) for its always-multi-select, always-reorderable behavior; ui_list forwards the FlListOpts
// choices.
static FlListRange list_begin_ex_impl(FlListContext* self, FlLayoutId id, const u64* ordered_ids, u32 count,
                                      FlSelectionSet* set, FlListConfig config, bool single_select,
                                      bool disable_reorder) {
    // The slice is the model the List forwards to selection_set_apply; it must be present whenever the List is
    // the one applying (non-NULL set). A no-selection caller (NULL set) need not owe the slice.
    FL_ASSERT_DEBUG((set == nullptr || ordered_ids != nullptr)
                    && "ui_list_begin: ordered_ids required when set is non-NULL");

    self->single_select = single_select;
    self->disable_reorder = disable_reorder;
    self->context_menu_id = 0;
    self->current_id = id;
    self->current_clay_id = ui_layout_id_to_clay_id(id);
    self->ordered_ids = ordered_ids;
    self->ordered_count = count;
    self->labels = config.labels;
    self->set = set;
    self->activated_id = 0;
    self->focused_id = 0;
    self->frame_row_pressed = false;
    self->nav_up_edge = false;
    self->nav_down_edge = false;
    self->suppress_drag_visuals = config.suppress_drag_visuals;
    self->frame_drag_active = false;
    self->drag_active_out = false;
    self->reorder_emitted = false;
    self->drag_from_ids = nullptr;
    self->drag_from_count = 0;
    self->drag_target_id = 0;
    self->drag_rel = FlDropRelationship_Before;

    // Per-frame request buffer from the frame arena: result.requests is ALWAYS populated (independent of set),
    // and the storage is valid only until the next frame's arena rewind.
    self->req_capacity = LIST_MAX_REQUESTS_PER_FRAME;
    self->requests = arena_alloc_array(self->ui_ctx->render_frame_allocator, FlSelectionRequest, self->req_capacity);
    self->req_count = 0;

    ListState* st = list_state_get(self, self->current_clay_id);

    list_resolve_drag_promotion(self, st, disable_reorder);
    bool has_focus = list_resolve_focus(self, st, id);
    list_resolve_focus_seed(self, st);

    // Keyboard navigation runs before list_uniform_begin so a cursor move scrolls its target into view this
    // frame. It may emit Ctrl+A / shift-range requests into the buffer above and returns the scroll target.
    i32 scroll_to = list_handle_keyboard(self, st, has_focus);

    // Expose the cursor: the result's focused_id reports it (hover may override per-row in item_begin), and
    // item_begin draws the cursor ring from these two fields.
    self->has_kb_focus = has_focus;
    self->frame_focus_cursor_id = has_focus ? st->focus_cursor_id : 0;
    if (has_focus && list_index_of_id(self, st->focus_cursor_id) >= 0) {
        self->focused_id = st->focus_cursor_id;
    }

    f32 row_height = config.row_height > 0.0f ? config.row_height : LIST_ROW_DEFAULT_HEIGHT;
    f32 gap = config.gap > 0.0f ? config.gap : LIST_ROW_DEFAULT_GAP;
    self->current_row_height = row_height;
    self->current_gap = gap;

    // list_uniform provides the scroll engine and uniform-height layout container; the ordered-id count is the
    // virtualization total.
    FlListUniformRange r = list_uniform_begin(self->ui_ctx->list_uniform_ctx, id.name, count,
                                              (FlListUniformOpts) {
                                                  .height_hint = row_height,
                                                  .gap = gap,
                                                  .scroll_to = scroll_to,
                                              });

    // Position auto-tracking starts at the first visible row (the caller emits [start, end)).
    self->emit_pos = r.start;

    return (FlListRange) {
        .start = r.start,
        .end = r.end,
        .total = r.total,
    };
}

// Scoped-face begin: the always-multi-select, always-reorderable behavior (the policy toggles default off).
static FlListRange list_begin_impl(FlListContext* self, FlLayoutId id, const u64* ordered_ids, u32 count,
                                   FlSelectionSet* set, FlListConfig config) {
    return list_begin_ex_impl(self, id, ordered_ids, count, set, config, false, false);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlSignal list_item_begin_impl(FlListContext* self, u64 stable_id) {
    u32 pos = self->emit_pos;
    FlLayoutId item_id = { .name = self->current_id.name, .id = (i32)(pos + 1) };

    // Selected-ness is resolved purely by id membership in the caller's set (CONTEXT Selection Set), so a row
    // selected while off-screen highlights correctly once scrolled back into view. A NULL set is never selected -
    // the app owns its own highlight on that path.
    bool selected = (self->set != nullptr) && selection_set_contains(self->set, stable_id);

    // Open the row wrapper inside list_uniform's virtualized container (same id seed list_uniform_item_begin
    // uses: {list name, pos + 1}). The List draws the selection highlight here as the wrapper's background fill
    // (the caller's content composes over it). A transparent fill (alpha 0) emits no rectangle, so an unselected
    // row emits none. The clip makes Clay emit a render command bearing the row's id even for fill-less content,
    // so the hit-test machinery (render-command-driven) can mark the row hot and the per-row signal stays live.
    const FlThemeColors* theme = fl_theme_colors();
    FlColor highlight = { 0, 0, 0, 0 };
    if (selected) {
        highlight = theme->surface.selected;
    }
    // Keyboard focus cursor: a ring on the cursor row, drawn from the selection-independent focus color so the
    // cursor stays visible whether or not the row is selected (the cursor is distinct from the selection).
    bool is_cursor = self->has_kb_focus && stable_id != 0 && stable_id == self->frame_focus_cursor_id;
    FlLayoutBorder border
        = is_cursor ? fl_border(theme->border.focus, LIST_FOCUS_BORDER_WIDTH) : (FlLayoutBorder) { 0 };
    fl_layout_start(item_id);
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_grow(), sizing_fit() },
        .color = highlight,
        .border = border,
        .clip = { .horizontal = true, .vertical = true },
    });

    // Read this row's interaction off the wrapper's previous-frame rect (hover/click/double-click/right-click),
    // then stamp selected from membership so it flows out on the row's FlSignal.
    FlSignal signal = fl_item_behavior(item_id, LIST_ITEM_FLAGS);
    signal.selected = selected;

    // Result is keyed on the app-supplied stable id, never on dense position (CONTEXT Item Id). activated_id
    // is the committed row (double-click / keyboard activate); focused_id tracks the row under the cursor.
    if (signal.activated || signal.double_clicked) {
        self->activated_id = stable_id;
    }
    if (signal.hovered) {
        self->focused_id = stable_id;
    }
    // Right-click is a policy outcome: the List opens no menu - it just reports the row so the app opens its own
    // context menu at the cursor (the declarative face's menu hook; also available to the scoped face).
    if (signal.right_clicked) {
        self->context_menu_id = stable_id;
    }
    if (signal.pressed) {
        self->frame_row_pressed = true; // a press landed on this list this frame (focus tracking)
        // Pressing a row takes focus into the global ring so keyboard nav engages and other widgets blur.
        self->ui_ctx->focused_id = self->current_clay_id;
        // Arm a potential drag-reorder on this row (unless reorder is disabled). It stays a click until the
        // cursor crosses the threshold (promoted in list_begin next frame); the dragged-id set is resolved later
        // from this row's membership.
        if (!self->disable_reorder) {
            ListState* st = list_state_get(self, self->current_clay_id);
            st->drag_pressed = true;
            st->drag_active = false;
            st->drag_press_id = stable_id;
        }
    }

    // Selection gesture state machine - all internal; the app sees only the emitted Selection Requests (which
    // auto-apply into set by default). Plain click replaces, ctrl/⌘ toggles, shift ranges from the anchor. A
    // click that concluded a drag-reorder is suppressed (frame_drag_active) so the drop does not also re-select.
    if (signal.clicked && !self->frame_drag_active) {
        ListState* st = list_state_get(self, self->current_clay_id);
        u32 mods = ui_keyboard_modifiers();
        // A single-selection list ignores the extend/toggle modifiers so every click is a plain replace.
        bool ctrl = !self->single_select && (mods & FlKeyModifier_Control) != 0;
        bool shift = !self->single_select && (mods & FlKeyModifier_Shift) != 0;

        if (shift && st->anchor_id != 0) {
            // Range from the anchor, replacing the prior selection. Endpoint ids only, so the range survives
            // virtualization. The anchor stays put, so a follow-up shift-click re-ranges from the same pivot.
            list_push_request(self, (FlSelectionRequest) { .kind = FlSelectionRequestKind_ClearAll });
            list_push_request(self, (FlSelectionRequest) {
                                        .kind = FlSelectionRequestKind_SetRange,
                                        .id = st->anchor_id,
                                        .to_id = stable_id,
                                        .selected = true,
                                    });
        } else if (ctrl) {
            // Toggle just this row relative to its current membership; the anchor moves here.
            list_push_request(self, (FlSelectionRequest) {
                                        .kind = FlSelectionRequestKind_SetOne,
                                        .id = stable_id,
                                        .selected = !selected,
                                    });
            st->anchor_id = stable_id;
        } else {
            // Plain click: replace the selection with exactly this row; the anchor moves here.
            list_push_request(self, (FlSelectionRequest) { .kind = FlSelectionRequestKind_ClearAll });
            list_push_request(self, (FlSelectionRequest) {
                                        .kind = FlSelectionRequestKind_SetOne,
                                        .id = stable_id,
                                        .selected = true,
                                    });
            st->anchor_id = stable_id;
        }
        st->has_focus = true;            // clicking the list focuses it (gates keyboard nav + Ctrl+A)
        st->focus_cursor_id = stable_id; // and lands the keyboard cursor on the clicked row
    }

    self->emit_pos++;
    return signal;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void list_item_end_impl(FlListContext* self) {
    (void)self;
    fl_layout_end(); // close the row wrapper opened in list_item_begin_impl
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Marquee (rubber-band) selection - CONTEXT.md `## Lists`.
//
// A drag begun on empty list space (a gap between rows or below the content, i.e. a press no row consumed) sweeps
// a selection rectangle and selects the rows it covers, emitting an endpoint-only SetRange like the click
// gestures. The anchor is held in content space, so edge auto-scroll mutating the visible range mid-gesture does
// not move it.
//
// The container is NOT registered as an interactive item: the empty-space test is "a press this frame that no row
// consumed, inside the container's viewport rect". Coverage works in boundingBox/mouse pixel space and assumes
// ui_scale == 1 (logical == physical); only the overlay's draw divides by scale.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Emit one of the list's floating overlay rectangles: a root-attached, input-passthrough float wrapping a single
// filled child. Position and size are window-space (physical) pixels; a floating element is authored in logical
// pixels, so both are divided by ui_scale here. A zeroed border draws no outline.

static void list_draw_floating_rect(FlListContext* self, i32 float_seed, i32 fill_seed, i16 z_index, f32 x, f32 y,
                                    f32 w, f32 h, FlColor fill, FlLayoutBorder border) {
    f32 scale = self->ui_ctx->ui_scale > 0.0f ? self->ui_ctx->ui_scale : 1.0f;

    fl_floating_start((FlLayoutId) { .name = self->current_id.name, .id = float_seed });
    fl_floating_configure((FlFloatingConfig) {
        .attach_to = FlFloatingAttachTo_Root,
        .pointer_capture = FlFloatingPointerCapture_Passthrough,
        .z_index = z_index,
        .offset = { x / scale, y / scale },
        .sizing = { sizing_fixed(w / scale), sizing_fixed(h / scale) },
    });
    fl_layout_start((FlLayoutId) { .name = self->current_id.name, .id = fill_seed });
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_grow(), sizing_grow() },
        .color = fill,
        .border = border,
    });
    fl_layout_end();
    fl_layout_end();
}

// Draw the marquee rectangle as a floating overlay (visual only - selection is resolved separately). Coordinates
// in are window-space (physical) pixels; the floating offset/sizing are logical, so divide by ui_scale.
static void list_marquee_draw(FlListContext* self, f32 vp_top, f32 vp_left, f32 vp_right, f32 vp_bottom, f32 scroll,
                              f32 band_lo_content, f32 band_hi_content, f32 mouse_x) {
    f32 screen_top = clamp_f32_impl(vp_top + (band_lo_content - scroll), vp_top, vp_bottom);
    f32 screen_bottom = clamp_f32_impl(vp_top + (band_hi_content - scroll), vp_top, vp_bottom);
    ListState* st = list_state_get(self, self->current_clay_id);
    f32 x0 = clamp_f32_impl(fminf(st->marquee_anchor_x, mouse_x), vp_left, vp_right);
    f32 x1 = clamp_f32_impl(fmaxf(st->marquee_anchor_x, mouse_x), vp_left, vp_right);

    f32 w = x1 - x0;
    f32 h = screen_bottom - screen_top;
    if (w <= 0.0f || h <= 0.0f) {
        return; // nothing visible on-screen (band fully scrolled out, or zero-width sweep)
    }

    const FlThemeColors* theme = fl_theme_colors();
    FlColor fill = theme->surface.selected;
    fill.a = 64; // translucent wash so the rows show through
    FlColor border = theme->interactive.base;

    list_draw_floating_rect(self, LIST_MARQUEE_FLOAT_SEED, LIST_MARQUEE_FILL_SEED, UI_Z_OVERLAY, x0, screen_top, w, h,
                            fill, fl_border(border, 1));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool list_rect_contains(f32 x, f32 y, f32 left, f32 right, f32 top, f32 bottom) {
    return x >= left && x <= right && y >= top && y <= bottom;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The list container's viewport rect from the previous frame's resolved layout. Clay owns it independent of the
// interaction item registry, so the container need not be a registered interactive item. False when the container
// has not been laid out yet.

static bool list_get_viewport_rect(FlListContext* self, f32* top, f32* left, f32* right, f32* bottom) {
    u32 container_clay_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = self->current_id.name, .id = 0 });
    Clay_ElementData data = Clay_GetElementData((Clay_ElementId) { .id = container_clay_id });
    if (!data.found) {
        return false;
    }

    *top = data.boundingBox.y;
    *left = data.boundingBox.x;
    *right = *left + data.boundingBox.width;
    *bottom = *top + data.boundingBox.height;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Edge auto-scroll for a held gesture: when the real (unclamped) cursor is past a viewport edge, nudge the scroll
// for next frame, proportional to the overshoot.

static void list_edge_autoscroll(FlListContext* self, f32 mouse_y, f32 vp_top, f32 vp_bottom, f32 stride) {
    f32 overshoot = 0.0f;
    if (mouse_y > vp_bottom) {
        overshoot = mouse_y - vp_bottom;
    } else if (mouse_y < vp_top) {
        overshoot = mouse_y - vp_top; // negative
    }
    if (overshoot == 0.0f) {
        return;
    }

    FlUiCtx* ui = self->ui_ctx;
    f32 dir = overshoot > 0.0f ? 1.0f : -1.0f;
    f32 mag = fminf(fabsf(overshoot), LIST_AUTOSCROLL_MAX_OVERSHOOT);
    f32 step = dir * (LIST_AUTOSCROLL_BASE_SPEED + mag * LIST_AUTOSCROLL_GAIN) * ui->delta_time;
    list_uniform_nudge_scroll(ui->list_uniform_ctx, self->current_id.name, step, stride, self->ordered_count);
}

static void list_marquee_update(FlListContext* self, ListState* st) {
    FlUiCtx* ui = self->ui_ctx;
    const FlString name = self->current_id.name;

    f32 vp_top, vp_left, vp_right, vp_bottom;
    if (!list_get_viewport_rect(self, &vp_top, &vp_left, &vp_right, &vp_bottom)) {
        st->marquee_pressed = false;
        st->marquee_active = false;
        return;
    }

    f32 mouse_x = ui->input_state.mouse.x;
    f32 mouse_y = ui->input_state.mouse.y;
    bool held = ui->input_state.mouse.buttons[FlMouseButton_Left];

    // Arm on an empty-space press: a left press this frame, inside the viewport, that no row consumed. (A press on
    // a row sets frame_row_pressed and runs the click gesture instead - the two are mutually exclusive.)
    bool inside = list_rect_contains(mouse_x, mouse_y, vp_left, vp_right, vp_top, vp_bottom);
    if (ui->mouse_pressed_this_frame && !self->frame_row_pressed && inside) {
        st->marquee_pressed = true;
        st->marquee_active = false;
        st->marquee_additive = (ui_keyboard_modifiers() & FlKeyModifier_Control) != 0;
        f32 scroll = list_uniform_scroll_offset(ui->list_uniform_ctx, name);
        st->marquee_anchor_content_y = (mouse_y - vp_top) + scroll; // press point in content space
        st->marquee_anchor_x = mouse_x;
    }

    // The gesture lives only while the button is held.
    if (!held) {
        st->marquee_pressed = false;
        st->marquee_active = false;
        return;
    }
    if (!st->marquee_pressed) {
        return;
    }

    // Latch live only once past the movement threshold - so an empty-space press-and-release stays a (no-op)
    // click, never a spurious one-pixel marquee. Reuses the input layer's per-button drag-distance tracking (the
    // FlItem_DragThreshold substrate).
    f32 drag_sqr = ui->input_state.mouse.drag_max_distance_sqr[FlMouseButton_Left];
    if (!st->marquee_active && drag_sqr > UI_MOUSE_DRAG_THRESHOLD * UI_MOUSE_DRAG_THRESHOLD) {
        st->marquee_active = true;
    }
    if (!st->marquee_active) {
        return;
    }

    f32 stride = self->current_row_height + self->current_gap;
    f32 scroll = list_uniform_scroll_offset(ui->list_uniform_ctx, name);

    // The cursor's content-space Y, with the cursor clamped to the viewport edges: a past-edge cursor pins the
    // band edge at the viewport and extends the band only as fast as auto-scroll advances scroll - the band
    // doesn't teleport ahead of the rows it covers.
    f32 cur_content_y = (clamp_f32_impl(mouse_y, vp_top, vp_bottom) - vp_top) + scroll;
    f32 band_lo = fminf(st->marquee_anchor_content_y, cur_content_y);
    f32 band_hi = fmaxf(st->marquee_anchor_content_y, cur_content_y);

    // Map the band to model positions: a row at position p occupies [p*stride, p*stride + row_height]; it
    // intersects [band_lo, band_hi] iff its bottom > band_lo and its top < band_hi. Solving for p gives a closed
    // form - pure arithmetic on content Y, never a per-item lookup, so off-screen rows resolve identically.
    if (self->ordered_count > 0 && self->ordered_ids != nullptr && stride > 0.0f) {
        i64 count = (i64)self->ordered_count;
        i64 lo = (i64)ceilf((band_lo - self->current_row_height) / stride); // first row whose bottom > band_lo
        i64 hi = (i64)ceilf(band_hi / stride) - 1;                          // last row whose top < band_hi
        if (lo < 0) {
            lo = 0;
        }
        if (hi > count - 1) {
            hi = count - 1;
        }
        if (lo <= hi) {
            // Replace mode clears first; additive (ctrl/⌘) unions the covered span onto the existing selection.
            if (!st->marquee_additive) {
                list_push_request(self, (FlSelectionRequest) { .kind = FlSelectionRequestKind_ClearAll });
            }
            list_push_request(self, (FlSelectionRequest) {
                                        .kind = FlSelectionRequestKind_SetRange,
                                        .id = self->ordered_ids[lo],
                                        .to_id = self->ordered_ids[hi],
                                        .selected = true,
                                    });
        } else if (!st->marquee_additive) {
            // Band covers no row (e.g. a thin sweep inside a single gap): replace mode empties the selection.
            list_push_request(self, (FlSelectionRequest) { .kind = FlSelectionRequestKind_ClearAll });
        }
    }

    list_edge_autoscroll(self, mouse_y, vp_top, vp_bottom, stride);

    list_marquee_draw(self, vp_top, vp_left, vp_right, vp_bottom, scroll, band_lo, band_hi, mouse_x);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Drag-reorder - CONTEXT.md Drop Relationship.
//
// A press that landed on a row (armed in item_begin) and then crossed the click-vs-drag threshold begins an
// intra-list reorder. Dragging past the viewport edge auto-scrolls to extend the reach.

// Candidate insertion index in [0, count] for a reorder drag: the dragged item(s) land BEFORE row ins. The
// cursor's row and which half it falls in decide it (top half -> before that row, bottom half -> after), pure
// arithmetic on the uniform stride so an off-screen target resolves exactly like an on-screen one.
static u32 list_drag_insertion_index(f32 cur_content_y, f32 stride, f32 row_height, u32 count) {
    if (count == 0 || stride <= 0.0f) {
        return 0;
    }
    i32 row = (i32)floorf(cur_content_y / stride);
    if (row < 0) {
        row = 0;
    }
    if (row > (i32)count - 1) {
        row = (i32)count - 1;
    }
    f32 row_mid = (f32)row * stride + row_height * 0.5f;
    return (cur_content_y < row_mid) ? (u32)row : (u32)row + 1; // in [0, count]
}

// The dragged stable ids for this frame, into the frame arena: the WHOLE selection when the pressed row is
// selected (dragging any selected row drags all of them), else just the pressed row. Recomputed each frame - the
// selection cannot change while the drag holds (row clicks are suppressed), so this is stable across the gesture.
static const u64* list_drag_collect_from_ids(FlListContext* self, u64 press_id, u32* out_count) {
    if (self->set != nullptr && selection_set_contains(self->set, press_id)) {
        u64 n = selection_set_len(self->set);
        if (n > 0) {
            u64* buf = arena_alloc_array(self->ui_ctx->render_frame_allocator, u64, n);
            *out_count = (u32)selection_set_copy_ids(self->set, buf, n);
            return buf;
        }
    }
    u64* one = arena_alloc_array(self->ui_ctx->render_frame_allocator, u64, 1);
    one[0] = press_id;
    *out_count = 1;
    return one;
}

// Draw the insertion line: a thin floating bar spanning the viewport width at the candidate drop boundary (the
// centre of the gap before row ins), clamped to the viewport. Window-space (physical) pixels in; the floating
// offset/sizing are logical, so divide by ui_scale (the marquee overlay precedent).
static void list_drag_draw_insertion(FlListContext* self, f32 vp_top, f32 vp_left, f32 vp_right, f32 vp_bottom,
                                     f32 scroll, u32 ins, f32 stride, f32 gap) {
    f32 line_content_y = (f32)ins * stride - gap * 0.5f; // middle of the gap preceding row ins
    f32 screen_y = clamp_f32_impl(vp_top + (line_content_y - scroll), vp_top, vp_bottom);

    const FlThemeColors* theme = fl_theme_colors();
    list_draw_floating_rect(self, LIST_DRAG_LINE_FLOAT_SEED, LIST_DRAG_LINE_FILL_SEED, UI_Z_OVERLAY + 1, vp_left,
                            screen_y - LIST_DRAG_LINE_THICKNESS * 0.5f, vp_right - vp_left, LIST_DRAG_LINE_THICKNESS,
                            theme->interactive.base, (FlLayoutBorder) { 0 });
}

// Draw the drag ghost: a translucent, row-height, viewport-width wash following the cursor vertically (centred on
// it), so the dragged row visibly floats. Window-space pixels in; floating offset/sizing are logical.
static void list_drag_draw_ghost(FlListContext* self, f32 vp_left, f32 vp_right, f32 mouse_y) {
    f32 height = self->current_row_height;
    f32 top = mouse_y - height * 0.5f;

    const FlThemeColors* theme = fl_theme_colors();
    FlColor fill = theme->surface.selected;
    fill.a = LIST_DRAG_GHOST_ALPHA;

    list_draw_floating_rect(self, LIST_DRAG_GHOST_FLOAT_SEED, LIST_DRAG_GHOST_FILL_SEED, UI_Z_OVERLAY + 2, vp_left, top,
                            vp_right - vp_left, height, fill, fl_border(theme->interactive.base, 1));
}

static void list_drag_update(FlListContext* self, ListState* st) {
    if (!st->drag_pressed) {
        return;
    }
    FlUiCtx* ui = self->ui_ctx;
    const FlString name = self->current_id.name;
    bool released = ui->mouse_released_this_frame || !ui->input_state.mouse.buttons[FlMouseButton_Left];

    // Armed but the threshold was never crossed: a release here was a plain click (handled in item_begin) - just
    // disarm. A sub-threshold hold keeps waiting.
    if (!st->drag_active) {
        if (released) {
            st->drag_pressed = false;
        }
        return;
    }

    f32 vp_top, vp_left, vp_right, vp_bottom;
    if (!list_get_viewport_rect(self, &vp_top, &vp_left, &vp_right, &vp_bottom)) {
        st->drag_pressed = false;
        st->drag_active = false;
        return;
    }

    f32 stride = self->current_row_height + self->current_gap;
    f32 scroll = list_uniform_scroll_offset(ui->list_uniform_ctx, name);
    f32 mouse_y = ui->input_state.mouse.y;

    // Candidate insertion from the (viewport-clamped) cursor content Y, mapped to a target id + relationship. The
    // clamp pins the candidate at the edge while edge auto-scroll advances scroll underneath it.
    f32 cur_content_y = (clamp_f32_impl(mouse_y, vp_top, vp_bottom) - vp_top) + scroll;
    u32 ins = list_drag_insertion_index(cur_content_y, stride, self->current_row_height, self->ordered_count);

    u64 target_id = 0;
    FlDropRelationship rel = FlDropRelationship_Before;
    if (self->ordered_ids != nullptr && self->ordered_count > 0) {
        if (ins == 0) {
            target_id = self->ordered_ids[0];
            rel = FlDropRelationship_Before;
        } else {
            target_id = self->ordered_ids[ins - 1];
            rel = FlDropRelationship_After;
        }
    }

    u32 from_count = 0;
    const u64* from_ids = list_drag_collect_from_ids(self, st->drag_press_id, &from_count);

    // Publish the payload - shared between the live (candidate) and committed (reorder request) phases.
    self->drag_from_ids = from_ids;
    self->drag_from_count = from_count;
    self->drag_target_id = target_id;
    self->drag_rel = rel;

    if (released) {
        // Drop: emit the reorder request and end the gesture. drag_active stays false on the result this frame.
        self->reorder_emitted = (from_count > 0 && target_id != 0);
        st->drag_pressed = false;
        st->drag_active = false;
        return;
    }

    // Still dragging: expose the live state and draw the default affordances (unless the caller overrides them).
    self->drag_active_out = true;
    if (!self->suppress_drag_visuals) {
        list_drag_draw_insertion(self, vp_top, vp_left, vp_right, vp_bottom, scroll, ins, stride, self->current_gap);
        list_drag_draw_ghost(self, vp_left, vp_right, mouse_y);
    }

    // A drag can reach off-screen positions the same way the marquee does.
    list_edge_autoscroll(self, mouse_y, vp_top, vp_bottom, stride);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlListResult list_end_impl(FlListContext* self) {
    list_uniform_end(self->ui_ctx->list_uniform_ctx);

    ListState* st = list_state_get(self, self->current_clay_id);

    // Drag-reorder gesture: runs after the rows are emitted (the press row is armed, the threshold may have crossed)
    // and before the marquee. A drag and the marquee are mutually exclusive - a drag arms on a row press, the
    // marquee on empty space - so an engaged drag (armed or live this frame) suppresses the marquee for the frame.
    bool drag_engaged = st->drag_pressed;
    list_drag_update(self, st);

    // Marquee gesture: runs after the rows are emitted (frame_row_pressed is settled) and before auto-apply, so a
    // covered span this frame folds into the set this frame. Never co-occurs with a click - a row press would
    // have set frame_row_pressed and disarmed the marquee. A single-selection list never rubber-band-selects.
    if (!drag_engaged && !self->single_select) {
        list_marquee_update(self, st);
    }

    // Auto-apply (the default): fold the emitted requests into the caller's set, forwarding the FULL ordered
    // slice so SetRange / SetAll resolve across rows virtualized away. A NULL set applies nothing - the app
    // owns apply/highlight via result.requests.
    if (self->set != nullptr && self->req_count > 0) {
        selection_set_apply(self->set, self->requests, self->req_count, self->ordered_ids, self->ordered_count);
    }

    // List-level focus: a press landing inside this list keeps focus here; a press elsewhere this frame blurs it
    // (so keyboard nav + Ctrl+A only fire while the list is the active surface). "Inside" covers both a row press
    // (frame_row_pressed) and a press on empty list space inside the viewport - the press that arms the marquee -
    // so starting a rubber-band gesture does not blur the very list being swept.
    if (self->ui_ctx->mouse_pressed_this_frame) {
        bool focused = self->frame_row_pressed;
        if (!focused) {
            // Same viewport rect + containment test the marquee arm uses.
            f32 vp_top, vp_left, vp_right, vp_bottom;
            if (list_get_viewport_rect(self, &vp_top, &vp_left, &vp_right, &vp_bottom)) {
                f32 mx = self->ui_ctx->input_state.mouse.x;
                f32 my = self->ui_ctx->input_state.mouse.y;
                focused = list_rect_contains(mx, my, vp_left, vp_right, vp_top, vp_bottom);
            }
            if (focused) {
                // An empty-space press takes focus into the global ring too, exactly like a row press - other
                // widgets blur, and begin's another-widget-owns-focus check won't blur the list back next frame.
                self->ui_ctx->focused_id = self->current_clay_id;
            }
        }
        st->has_focus = focused;
    }

    return (FlListResult) {
        .activated_id = self->activated_id,
        .focused_id = self->focused_id,
        .context_menu_id = self->context_menu_id,
        .requests = self->requests,
        .request_count = self->req_count,
        .nav_up_edge = self->nav_up_edge,
        .nav_down_edge = self->nav_down_edge,
        .drag_active = self->drag_active_out,
        .reorder_emitted = self->reorder_emitted,
        .drag_from_ids = self->drag_from_ids,
        .drag_target_id = self->drag_target_id,
        .drag_from_count = self->drag_from_count,
        .drag_rel = self->drag_rel,
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Declarative face - CONTEXT.md `## Lists`. One call runs the whole list on the shared core (begin/item/end): it
// splits the array-of-descriptors into the parallel (id, label) slices the core's begin consumes and loops the
// visible window emitting an icon + label per row.

static FlListResult list_decl_impl(FlListContext* self, FlLayoutId id, const FlListItem* items, u32 count,
                                   FlSelectionSet* set, FlListOpts opts) {
    // A NULL item array is treated as empty regardless of count - never trust a count without the backing
    // array (the loop below dereferences items[i], so a non-zero count with NULL items would fault). n is the
    // effective length forwarded to the core; the begin call and the row loop both key off it, not the raw count.
    u32 n = (items != nullptr) ? count : 0;

    // Fold the descriptors into the two parallel slices the shared core takes - the ordered stable ids (also the
    // virtualization total) and the type-ahead labels - into the frame arena.
    u64* ids = nullptr;
    FlString* labels = nullptr;
    if (n > 0) {
        ids = arena_alloc_array(self->ui_ctx->render_frame_allocator, u64, n);
        labels = arena_alloc_array(self->ui_ctx->render_frame_allocator, FlString, n);
        for (u32 i = 0; i < n; i++) {
            ids[i] = items[i].id;
            labels[i] = items[i].label;
        }
    }

    FlListConfig config = {
        .row_height = opts.row_height,
        .gap = opts.gap,
        .labels = labels,
        .suppress_drag_visuals = opts.suppress_drag_visuals,
    };

    // multi_select / reorderable are positive-sense opts; the core takes the inverted policy toggles.
    FlListRange r = list_begin_ex_impl(self, id, ids, n, set, config, !opts.multi_select, !opts.reorderable);

    f32 row_height = self->current_row_height; // resolved (config 0 -> default) inside begin
    for (u32 i = r.start; i < r.end; i++) {
        const FlListItem* it = &items[i];
        list_item_begin_impl(self, it->id);

        // Row content: an optional leading icon then the label, over the List's own selection-highlight fill
        // (drawn on the row wrapper). The row wrapper is grow×fit, so this content has the fixed row height.
        fl_layout_start_auto();
        fl_layout_configure((FlLayoutConfig) {
            .sizing = { sizing_grow(), sizing_fixed(row_height) },
            .padding = { LIST_DECL_ROW_PAD_X, LIST_DECL_ROW_PAD_X, 0, 0 },
            .child_gap = LIST_DECL_ICON_GAP,
            .direction = LayoutDirection_LeftToRight,
            .child_alignment = { .y = LayoutAlignmentY_Center },
        });
        if (it->icon != FL_IMAGE_HANDLE_INVALID) {
            // A fixed square sized to the row so the icon scales uniformly; ui_image_impl draws nothing for a
            // not-ready / invalid handle, so a still-loading icon just leaves the box empty (no layout jump).
            f32 icon_side = row_height - LIST_DECL_ICON_INSET * 2.0f;
            if (icon_side < 1.0f) {
                icon_side = row_height;
            }
            fl_layout_start_auto();
            fl_layout_configure((FlLayoutConfig) {
                .sizing = { sizing_fixed(icon_side), sizing_fixed(icon_side) },
            });
            ui_image_impl((ImageHandle)it->icon, (FlImageOpts) { .fill_container = true });
            fl_layout_end();
        }
        ui_text(it->label);
        fl_layout_end();

        list_item_end_impl(self);
    }
    return list_end_impl(self);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scoped face - forward to the core with the global UI context's list state.

FlListRange ui_list_begin(FlLayoutId id, const u64* ordered_ids, u32 count, FlSelectionSet* set, FlListConfig config) {
    return list_begin_impl(ui_get_ctx()->ui_list_ctx, id, ordered_ids, count, set, config);
}

FlSignal ui_list_item_begin(u64 stable_id) {
    return list_item_begin_impl(ui_get_ctx()->ui_list_ctx, stable_id);
}

void ui_list_item_end(void) {
    list_item_end_impl(ui_get_ctx()->ui_list_ctx);
}

FlListResult ui_list_end(void) {
    return list_end_impl(ui_get_ctx()->ui_list_ctx);
}

FlListResult ui_list(FlLayoutId id, const FlListItem* items, u32 count, FlSelectionSet* set, FlListOpts opts) {
    return list_decl_impl(ui_get_ctx()->ui_list_ctx, id, items, count, set, opts);
}

void ui_list_request_focus(FlLayoutId id, u64 cursor_id) {
    FlListContext* self = ui_get_ctx()->ui_list_ctx;
    u32 clay_id = ui_layout_id_to_clay_id(id);
    ListState* st = list_state_get(self, clay_id);

    // Re-arm guard: only a call where the list does not ALREADY hold focus seeds the cursor, so a stray
    // every-frame call cannot yank a roving cursor back. "Already holds focus" tests both has_focus AND ownership
    // of the global ring: has_focus is never cleared while a popup is closed (the list isn't emitted, so begin
    // never blurs it), so a plain !has_focus guard would read a genuine reopen as redundant. The seed is
    // validated against the ordered slice in begin (0 / absent -> first row).
    bool already_focused = st->has_focus && self->ui_ctx->focused_id == clay_id;
    if (!already_focused) {
        st->focus_cursor_id = cursor_id;
        st->focus_seed_pending = true;
    }

    // Engage the list's own keyboard handling AND claim the global focus ring together: setting has_focus without
    // the ring is dead one frame later (begin blurs it when another widget - the just-clicked trigger - owns the
    // ring), and claiming the ring without has_focus never runs the list's keyboard state machine.
    st->has_focus = true;
    self->ui_ctx->focused_id = clay_id;
}
