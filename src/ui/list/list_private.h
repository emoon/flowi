#pragma once

// List internals shared by the List's source files: the persisted per-list state, the per-frame context, and the
// helpers the keyboard, marquee and drag gestures all use. Nothing outside src/ui/ui_list.c, src/ui/ui_tree.c and
// src/ui/list/ includes this.

#include "../list_rows.h"
#include "../ui_list.h"
#include "../type_ahead.h"
#include "../ui_private.h"
#include <core/hashmap.h>

// Upper bound on Selection Requests one frame can emit: a gesture is at most ClearAll + SetRange/SetOne (2),
// and Ctrl+A is one SetAll. The two never happen in the same frame, so 4 is above any real frame. The marquee
// also emits at most ClearAll + SetRange (2), and never co-occurs with a click or Ctrl+A in one frame.
#define LIST_MAX_REQUESTS_PER_FRAME 4

// Per-list state persisted across frames, keyed by the list's id (string-id derived). Holds only the gesture
// anchor and list-level focus. The resolved selection lives in the caller's FlSelectionSet.
typedef struct ListState {
    u64 anchor_id;  // Stable id of the gesture anchor (range pivot); 0 = none yet.
    bool has_focus; // List-level focus: a click in this list or a Tab into it; gates keyboard nav + Ctrl+A.

    // Keyboard focus cursor: the row the arrow keys move (CONTEXT.md "Lists"). Distinct from anchor_id: the
    // cursor roves on plain arrows without touching the selection, while the anchor is the shift-extend pivot.
    // A stable id (0 = unset), so it survives virtualization and re-sorts just like the selection ids do.
    u64 focus_cursor_id;

    // A focus seed was requested this frame (fl_list_request_focus) before the model was known: begin
    // validates focus_cursor_id against the ordered slice and falls back to the first row, then clears this.
    // Scoped to the seed frame so a roving cursor is never re-snapped on a later frame.
    bool focus_seed_pending;

    // Type-ahead prefix accumulated across frames while the list holds focus; the shared matcher owns the
    // accumulate/match/expire logic (type_ahead.h).
    TypeAheadState type_ahead;

    // Marquee (rubber-band) gesture. Armed by an empty-space press, latched live once the cursor crosses the
    // click-vs-drag threshold (the FlItem_DragThreshold substrate). The anchor is kept in content space (pixels
    // from the top of the content, not screen space) so it stays put while edge auto-scroll mutates the visible
    // range mid-gesture.
    bool marquee_pressed;         // a left press landed on empty list space; tracking toward the threshold
    bool marquee_active;          // threshold crossed; the marquee is live (selecting + drawn)
    bool marquee_additive;        // ctrl/⌘ held at gesture start: union with the existing selection
    f64 marquee_anchor_content_y; // anchor Y in content space (survives scroll)
    f32 marquee_anchor_x;         // anchor X in window space (drawing only; coverage is Y-only for full-width rows)

    // Drag-reorder gesture (CONTEXT.md Drop Relationship). Armed by a press that landed on a row, latched live
    // once the cursor crosses the click-vs-drag threshold. Only the pressed row id persists across frames. The
    // dragged-id set (whole selection vs the single row) is recomputed from live membership each frame, since the
    // selection cannot change while the drag holds (row clicks are suppressed).
    bool drag_pressed; // a left press landed on a row; tracking toward the threshold
    bool drag_active;  // threshold crossed; the reorder drag is live (ghost + insertion line drawn)
    u64 drag_press_id; // stable id of the row the press landed on (selected => drag the whole selection)

    // Header column resize. Armed by a press on a header edge: the column id and its resolved width (logical px)
    // at that moment, so the reported width is that plus the drag distance.
    u32 resize_column; // id of the column being resized (0 = none)
    f32 resize_start_width;
    bool resize_moved; // the mouse has moved since the press, so the drag reports a width
} ListState;

// The arguments of the begin core: the scoped face's begin call plus the declarative face's policy toggles.
typedef struct ListBeginArgs {
    FlLayoutId id;
    ListRows rows;
    FlSelectionSet* set;
    FlListConfig config;
    bool single_select;
    bool disable_reorder;
    i32 (*key_target_row)(i32 cursor_row);
} ListBeginArgs;

struct FlListContext {
    FlArena* arena;
    FlUiCtx* ui_ctx;

    hashmap(u32, ListState) list_states; // list clay id -> persisted anchor/focus

    // Current list being emitted (set in begin)
    FlLayoutId current_id;  // Identifies the list container; row ids derive from its name
    u32 current_clay_id;    // ui_layout_id_to_clay_id(current_id), the key into list_states
    ListRows rows;          // The list's rows, borrowed for this begin->end scope
    FlSelectionSet* set;    // Caller-owned selection storage (nullptr = opt out of auto-apply/highlight)
    u32 emit_pos;           // Auto-tracked emission position of the next row (absolute index)
    f32 current_row_height; // Resolved row height this frame, in logical pixels (for layout)
    f32 current_row_px;
    f32 current_gap_px;
    bool suppress_drag_visuals; // config.suppress_drag_visuals this frame (gates the built-in ghost/insertion line)

    // Policy toggles resolved at begin (default off = the scoped face's always-multi-select, always-reorderable
    // behavior). The declarative face (ui_list) sets them from FlListOpts; the scoped begin leaves them off.
    bool single_select;   // true: ignore ctrl/shift/marquee/Ctrl+A; plain click replace only (single-selection).
    bool disable_reorder; // true: a row press never arms a drag-reorder gesture.
    i32 (*key_target_row)(i32 cursor_row);

    // Per-frame Selection Requests (frame-arena backed; returned on the result and auto-applied at end).
    FlSelectionRequest* requests;
    u32 req_count;
    u32 req_capacity;

    bool frame_row_pressed; // Did a row of the current list take a press this frame? (focus tracking)

    // Columns for this begin->end scope (config.columns; column_count 0 = a plain list). column_widths holds the
    // resolved width of each column in logical pixels, in the frame arena.
    const FlColumnDef* columns;
    u32 column_count;
    f32* column_widths;
    u32 result_width_count;    // Entries of column_widths the result reports (0 until the row width is measured)
    f32 cell_cursor;           // How far into the current row the lead and cells drawn so far reach (logical px)
    u32 cell_next_index;       // Index of the first column the current row has not drawn a cell for
    bool frame_header_pressed; // Did the header of the current list take a press this frame? (focus tracking)
    u32 header_clicked_column; // Column whose header was clicked this frame (0 = none)
    u32 resized_column;        // Column being dragged to a new width this frame (0 = none)
    f32 resized_width;         // That column's new width in logical pixels
    u32 context_menu_cell;     // Column of the right-click behind context_menu_id (0 = none)

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
    bool drag_active_out;        // result.drag_active: live drag exposed for visual override
    bool reorder_emitted;        // result.reorder_emitted: a reorder request was produced this frame (on release)
    const u64* drag_from_ids;    // result.drag_from_ids: dragged stable ids (frame-arena backed)
    u32 drag_from_count;         // result.drag_from_count
    u64 drag_target_id;          // result.drag_target_id: candidate, then committed, drop target
    FlDropRelationship drag_rel; // result.drag_rel: Before/After (Into is never produced)
};

// The list container's viewport rect in window-space (physical) pixels.
typedef struct ListViewport {
    f32 top;
    f32 left;
    f32 right;
    f32 bottom;
} ListViewport;

// One of the list's floating overlay rectangles. x/y/w/h are window-space (physical) pixels.
typedef struct ListOverlay {
    i32 float_seed;
    i32 fill_seed;
    i16 z_index;
    f32 x;
    f32 y;
    f32 w;
    f32 h;
    FlColor fill;
    FlLayoutBorder border; // zeroed = no outline
} ListOverlay;

// Fetch (creating if absent) the persisted state for the list keyed by clay_id.
ListState* list_state_get(FlListContext* self, u32 clay_id);

// Append one Selection Request to this frame's array (capacity-guarded, see LIST_MAX_REQUESTS_PER_FRAME).
void list_push_request(FlListContext* self, FlSelectionRequest req);

// Row of stable id id, or -1 if absent (or id == 0, the "none" sentinel).
i32 list_index_of_id(const FlListContext* self, u64 id);

u64 list_row_id(const FlListContext* self, u32 row);

// The list has a model to act on: at least one row with an id.
bool list_has_rows(const FlListContext* self);

// The list container's viewport rect from the previous frame's resolved layout; false when the container has not
// been laid out yet.
bool list_get_viewport(const FlListContext* self, ListViewport* out);

bool list_viewport_contains(const ListViewport* vp, f32 x, f32 y);

// The mouse button went down this frame, inside the list, on the widget that has keyboard focus, such as a text
// input in a row. That press is the widget's: the list neither takes focus for it nor starts a marquee.
bool list_press_on_focused_widget(const FlListContext* self);

// Content-space Y of the cursor, clamped to the viewport edges so a past-edge cursor pins at the edge and moves
// only as fast as auto-scroll advances the scroll offset.
f64 list_clamped_content_y(const FlListContext* self, const ListViewport* vp, f64 scroll);

// Distance between the tops of two adjacent rows.
f32 list_row_stride(const FlListContext* self);

// Current scroll offset of this list's container.
f64 list_scroll_offset(const FlListContext* self);

// The held left button has moved farther than the click-vs-drag threshold (the FlItem_DragThreshold substrate).
bool list_pointer_past_drag_threshold(const FlListContext* self);

// Edge auto-scroll for a held gesture: scrolls for next frame when the real cursor is past a viewport edge.
void list_edge_autoscroll(FlListContext* self, const ListViewport* vp);

// Emit a root-attached, input-passthrough floating rectangle wrapping a single filled child.
void list_draw_overlay(FlListContext* self, const ListOverlay* rect);

// Keyboard navigation for a focused list; returns the index to scroll to (-1 = leave scrolling alone).
u32 list_handle_keyboard(FlListContext* self, ListState* st, bool has_focus);

// Marquee gesture; runs after the rows are emitted.
void list_marquee_update(FlListContext* self, ListState* st);

// Drag-reorder gesture; runs after the rows are emitted.
void list_drag_update(FlListContext* self, ListState* st);

// Columns (list_header.c). list_header_begin opens the outer wrapper and draws the header when the config has
// columns, and does nothing otherwise. list_header_end closes the wrapper. list_column_at names the column under an
// x offset from the row's left edge (logical px), 0 for none. list_row_cell_under_mouse names the column of the row
// the mouse is inside, 0 for none.
void list_header_begin(FlListContext* self, ListState* st, const FlListConfig* config, f32 row_height);
void list_header_end(FlListContext* self);
u32 list_column_at(const FlListContext* self, f32 x);
u32 list_row_cell_under_mouse(const FlListContext* self, FlLayoutId row_id);

FlListRange list_begin_impl(FlListContext* self, ListBeginArgs args);

FlSignal list_item_begin_impl(FlListContext* self, u64 stable_id);

void list_item_end_impl(FlListContext* self);

FlListResult list_end_impl(FlListContext* self);

void list_claim_press(FlListContext* self);
