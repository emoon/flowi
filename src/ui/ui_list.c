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

#include "list/list_private.h"
#include <core/arena.h>
#include <core/log.h>
#include "list_uniform.h"
#include <ui/ui_theme.h>
#include <flowi/ui/item_behavior.h>
#include "layout.h"
#include "ui.h"

// Constants

#define LIST_ROW_DEFAULT_HEIGHT 32.0f // Fallback row height when the config leaves it unset
#define LIST_ROW_DEFAULT_GAP 8.0f     // Fallback gap between rows

// Per-row interaction the scoped face opts into by default: clicks (also gates hover), double-click and
// right-click.
#define LIST_ITEM_FLAGS (FlItemFlags_clickable | FlItemFlags_double_click | FlItemFlags_right_click)

// Keyboard focus cursor: the ring drawn on the cursor row (theme border.focus), distinct from the selection
// highlight fill, so the cursor is visible even on an unselected row.
#define LIST_FOCUS_BORDER_WIDTH 2

// Declarative-face row content: the icon + label the List itself draws for ui_list rows.
#define LIST_DECL_ROW_PAD_X 6     // horizontal padding inside a declarative row (logical px)
#define LIST_DECL_ICON_GAP 8      // gap between the leading icon and the label (logical px)
#define LIST_DECL_ICON_INSET 4.0f // shrink the icon square this much from the row height, top and bottom

// The arguments of the begin core: the scoped face's begin call plus the declarative face's policy toggles.
typedef struct ListBeginArgs {
    FlLayoutId id;
    const u64* ordered_ids;
    u32 count;
    FlSelectionSet* set;
    FlListConfig config;
    bool single_select;
    bool disable_reorder;
} ListBeginArgs;

// Context Creation/Destruction

FlListContext* ui_list_create(FlArena* arena) {
    FlListContext* ctx = arena_alloc_zero(arena, FlListContext);
    ctx->arena = arena;
    ctx->ui_ctx = ui_get_ctx();
    hashmap_new(&ctx->list_states, arena, 8);

    log_info("List context created");
    return ctx;
}

// Promote an armed press (item_begin) to a live drag once the drag distance crosses the threshold, latching until
// release. Runs BEFORE any row is emitted so item_begin can suppress clicks for the whole list, and disarms on
// self->disable_reorder so a press armed on an earlier frame cannot promote after the flag flips.
static void list_resolve_drag_promotion(FlListContext* self, ListState* st) {
    if (self->disable_reorder) {
        st->drag_pressed = false; // drop any stale armed press so the marquee/click paths aren't suppressed
        st->drag_active = false;
        return;
    }

    if (!st->drag_pressed) {
        return;
    }

    bool held = self->ui_ctx->input_state.mouse.buttons[FlMouseButton_Left];
    if (held && list_pointer_past_drag_threshold(self)) {
        st->drag_active = true;
    }
    self->frame_drag_active = st->drag_active;
}

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

// Resolve a focus seed requested this frame (ui_list_request_focus). The caller chose cursor_id before the model
// was known, so validate it now against the ordered slice and fall back to the first row when it is 0 or absent.
// Only on the seed frame - a roving cursor is never re-snapped.
static void list_resolve_focus_seed(FlListContext* self, ListState* st) {
    if (!st->focus_seed_pending) {
        return;
    }

    st->focus_seed_pending = false;
    if (list_index_of_id(self, st->focus_cursor_id) < 0) {
        st->focus_cursor_id = list_has_rows(self) ? self->ordered_ids[0] : 0;
    }
}

// Core implementation (ctx explicit - the service API points here; the scoped face forwards with the global ctx)

// Reset every per-frame field and the request buffer for the list that begin is about to emit.
static void list_reset_frame(FlListContext* self, const ListBeginArgs* args) {
    self->single_select = args->single_select;
    self->disable_reorder = args->disable_reorder;
    self->context_menu_id = 0;
    self->current_id = args->id;
    self->current_clay_id = ui_layout_id_to_clay_id(args->id);
    self->ordered_ids = args->ordered_ids;
    self->ordered_count = args->count;
    self->labels = args->config.labels;
    self->set = args->set;
    self->activated_id = 0;
    self->focused_id = 0;
    self->frame_row_pressed = false;
    self->nav_up_edge = false;
    self->nav_down_edge = false;
    self->suppress_drag_visuals = args->config.suppress_drag_visuals;
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
}

// Expose the cursor: the result's focused_id reports it (hover may override per-row in item_begin), and
// item_begin draws the cursor ring from these two fields.
static void list_expose_cursor(FlListContext* self, const ListState* st, bool has_focus) {
    self->has_kb_focus = has_focus;
    self->frame_focus_cursor_id = has_focus ? st->focus_cursor_id : 0;
    if (has_focus && list_index_of_id(self, st->focus_cursor_id) >= 0) {
        self->focused_id = st->focus_cursor_id;
    }
}

// Begin core, with the declarative face's policy toggles threaded in. The scoped face leaves them off for its
// always-multi-select, always-reorderable behavior; ui_list forwards the FlListOpts choices.
static FlListRange list_begin_impl(FlListContext* self, ListBeginArgs args) {
    // The slice is the model the List forwards to selection_set_apply; it must be present whenever the List is
    // the one applying (non-NULL set). A no-selection caller (NULL set) need not owe the slice.
    FL_ASSERT_DEBUG((args.set == nullptr || args.ordered_ids != nullptr)
                    && "ui_list_begin: ordered_ids required when set is non-NULL");

    list_reset_frame(self, &args);

    ListState* st = list_state_get(self, self->current_clay_id);

    list_resolve_drag_promotion(self, st);
    bool has_focus = list_resolve_focus(self, st, args.id);
    list_resolve_focus_seed(self, st);

    // Keyboard navigation runs before list_uniform_begin so a cursor move scrolls its target into view this
    // frame. It may emit Ctrl+A / shift-range requests into the buffer above and returns the scroll target.
    i32 scroll_to = list_handle_keyboard(self, st, has_focus);
    list_expose_cursor(self, st, has_focus);

    f32 row_height = args.config.row_height > 0.0f ? args.config.row_height : LIST_ROW_DEFAULT_HEIGHT;
    f32 gap = args.config.gap > 0.0f ? args.config.gap : LIST_ROW_DEFAULT_GAP;
    self->current_row_height = row_height;
    self->current_gap = gap;

    // list_uniform provides the scroll engine and uniform-height layout container; the ordered-id count is the
    // virtualization total.
    FlListUniformRange r = list_uniform_begin(self->ui_ctx->list_uniform_ctx, args.id.name, args.count,
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

// Open the row wrapper inside list_uniform's virtualized container (same id seed list_uniform_item_begin uses:
// {list name, pos + 1}). The List draws the selection highlight here as the wrapper's background fill (the
// caller's content composes over it). A transparent fill (alpha 0) emits no rectangle, so an unselected row emits
// none. The clip makes Clay emit a render command bearing the row's id even for fill-less content, so the
// hit-test machinery (render-command-driven) can mark the row hot and the per-row signal stays live.
static void list_open_row_wrapper(const FlListContext* self, FlLayoutId item_id, u64 stable_id, bool selected) {
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
}

// Arm a potential drag-reorder on the pressed row (unless reorder is disabled). It stays a click until the cursor
// crosses the threshold (promoted in list_begin next frame); the dragged-id set is resolved later from this row's
// membership.
static void list_arm_drag(FlListContext* self, u64 stable_id) {
    if (self->disable_reorder) {
        return;
    }
    ListState* st = list_state_get(self, self->current_clay_id);
    st->drag_pressed = true;
    st->drag_active = false;
    st->drag_press_id = stable_id;
}

// Fold the row's interaction into the per-frame result. The result is keyed on the app-supplied stable id, never
// on dense position (CONTEXT Item Id). activated_id is the committed row (double-click / keyboard activate);
// focused_id tracks the row under the cursor.
static void list_record_signal(FlListContext* self, u64 stable_id, FlSignal signal) {
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
        list_arm_drag(self, stable_id);
    }
}

// Shift-click: range from the anchor, replacing the prior selection. Endpoint ids only, so the range survives
// virtualization. The anchor stays put, so a follow-up shift-click re-ranges from the same pivot.
static void list_click_range(FlListContext* self, const ListState* st, u64 stable_id) {
    list_push_request(self, (FlSelectionRequest) { .kind = FlSelectionRequestKind_ClearAll });
    list_push_request(self, (FlSelectionRequest) {
                                .kind = FlSelectionRequestKind_SetRange,
                                .id = st->anchor_id,
                                .to_id = stable_id,
                                .selected = true,
                            });
}

// Ctrl-click: toggle just this row relative to its current membership; the anchor moves here.
static void list_click_toggle(FlListContext* self, ListState* st, u64 stable_id, bool selected) {
    list_push_request(self, (FlSelectionRequest) {
                                .kind = FlSelectionRequestKind_SetOne,
                                .id = stable_id,
                                .selected = !selected,
                            });
    st->anchor_id = stable_id;
}

// Plain click: replace the selection with exactly this row; the anchor moves here.
static void list_click_replace(FlListContext* self, ListState* st, u64 stable_id) {
    list_push_request(self, (FlSelectionRequest) { .kind = FlSelectionRequestKind_ClearAll });
    list_push_request(self, (FlSelectionRequest) {
                                .kind = FlSelectionRequestKind_SetOne,
                                .id = stable_id,
                                .selected = true,
                            });
    st->anchor_id = stable_id;
}

// Selection gesture state machine - all internal; the app sees only the emitted Selection Requests (which
// auto-apply into set by default). Plain click replaces, ctrl/⌘ toggles, shift ranges from the anchor.
static void list_apply_click(FlListContext* self, u64 stable_id, bool selected) {
    ListState* st = list_state_get(self, self->current_clay_id);
    u32 mods = ui_keyboard_modifiers();
    // A single-selection list ignores the extend/toggle modifiers so every click is a plain replace.
    bool ctrl = !self->single_select && (mods & FlKeyModifier_Control) != 0;
    bool shift = !self->single_select && (mods & FlKeyModifier_Shift) != 0;

    if (shift && st->anchor_id != 0) {
        list_click_range(self, st, stable_id);
    } else if (ctrl) {
        list_click_toggle(self, st, stable_id, selected);
    } else {
        list_click_replace(self, st, stable_id);
    }
    st->has_focus = true;            // clicking the list focuses it (gates keyboard nav + Ctrl+A)
    st->focus_cursor_id = stable_id; // and lands the keyboard cursor on the clicked row
}

static FlSignal list_item_begin_impl(FlListContext* self, u64 stable_id) {
    FlLayoutId item_id = { .name = self->current_id.name, .id = (i32)(self->emit_pos + 1) };

    // Selected-ness is resolved purely by id membership in the caller's set (CONTEXT Selection Set), so a row
    // selected while off-screen highlights correctly once scrolled back into view. A NULL set is never selected -
    // the app owns its own highlight on that path.
    bool selected = (self->set != nullptr) && selection_set_contains(self->set, stable_id);

    list_open_row_wrapper(self, item_id, stable_id, selected);

    // Read this row's interaction off the wrapper's previous-frame rect (hover/click/double-click/right-click),
    // then stamp selected from membership so it flows out on the row's FlSignal.
    FlSignal signal = fl_item_behavior(item_id, LIST_ITEM_FLAGS);
    signal.selected = selected;
    list_record_signal(self, stable_id, signal);

    // A click that concluded a drag-reorder is suppressed (frame_drag_active) so the drop does not also re-select.
    if (signal.clicked && !self->frame_drag_active) {
        list_apply_click(self, stable_id, selected);
    }

    self->emit_pos++;
    return signal;
}

static void list_item_end_impl(FlListContext* self) {
    (void)self;
    fl_layout_end(); // close the row wrapper opened in list_item_begin_impl
}

// An empty-space press takes focus into the global ring too, exactly like a row press - other widgets blur, and
// begin's another-widget-owns-focus check won't blur the list back next frame.
static bool list_empty_space_press_focuses(FlListContext* self) {
    ListViewport vp;
    if (!list_get_viewport(self, &vp)) {
        return false;
    }
    FlUiCtx* ui = self->ui_ctx;
    bool inside = list_viewport_contains(&vp, ui->input_state.mouse.x, ui->input_state.mouse.y);
    if (inside) {
        ui->focused_id = self->current_clay_id;
    }
    return inside;
}

// List-level focus: a press landing inside this list keeps focus here; a press elsewhere this frame blurs it (so
// keyboard nav + Ctrl+A only fire while the list is the active surface). "Inside" covers both a row press
// (frame_row_pressed) and a press on empty list space inside the viewport - the press that arms the marquee - so
// starting a rubber-band gesture does not blur the very list being swept.
static void list_update_focus_from_press(FlListContext* self, ListState* st) {
    if (!self->ui_ctx->mouse_pressed_this_frame) {
        return;
    }
    st->has_focus = self->frame_row_pressed || list_empty_space_press_focuses(self);
}

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

    list_update_focus_from_press(self, st);

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

// Declarative face - CONTEXT.md `## Lists`. One call runs the whole list on the shared core (begin/item/end): it
// splits the array-of-descriptors into the parallel (id, label) slices the core's begin consumes and loops the
// visible window emitting an icon + label per row.

// The row's leading icon: a fixed square sized to the row so the icon scales uniformly; ui_image_impl draws nothing
// for a not-ready / invalid handle, so a still-loading icon just leaves the box empty (no layout jump).
static void list_decl_icon(const FlListItem* it, f32 row_height) {
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

// Row content: an optional leading icon then the label, over the List's own selection-highlight fill (drawn on the
// row wrapper). The row wrapper is grow×fit, so this content has the fixed row height.
static void list_decl_row_content(const FlListItem* it, f32 row_height) {
    fl_layout_start_auto();
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_grow(), sizing_fixed(row_height) },
        .padding = { LIST_DECL_ROW_PAD_X, LIST_DECL_ROW_PAD_X, 0, 0 },
        .child_gap = LIST_DECL_ICON_GAP,
        .direction = LayoutDirection_LeftToRight,
        .child_alignment = { .y = LayoutAlignmentY_Center },
    });
    if (it->icon != FL_IMAGE_HANDLE_INVALID) {
        list_decl_icon(it, row_height);
    }
    ui_text(it->label);
    fl_layout_end();
}

FlListResult ui_list(FlLayoutId id, const FlListItem* items, u32 count, FlSelectionSet* set, FlListOpts opts) {
    FlListContext* self = ui_get_ctx()->ui_list_ctx;
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

    // multi_select / reorderable are positive-sense opts; the core takes the inverted policy toggles.
    FlListRange r = list_begin_impl(self,
                                    (ListBeginArgs) {
                                        .id = id,
                                        .ordered_ids = ids,
                                        .count = n,
                                        .set = set,
                                        .config = {
                                            .row_height = opts.row_height,
                                            .gap = opts.gap,
                                            .labels = labels,
                                            .suppress_drag_visuals = opts.suppress_drag_visuals,
                                        },
                                        .single_select = !opts.multi_select,
                                        .disable_reorder = !opts.reorderable,
                                    });

    f32 row_height = self->current_row_height; // resolved (config 0 -> default) inside begin
    for (u32 i = r.start; i < r.end; i++) {
        list_item_begin_impl(self, items[i].id);
        list_decl_row_content(&items[i], row_height);
        list_item_end_impl(self);
    }
    return list_end_impl(self);
}

// Scoped face - forward to the core with the global UI context's list state.

FlListRange ui_list_begin(FlLayoutId id, const u64* ordered_ids, u32 count, FlSelectionSet* set, FlListConfig config) {
    return list_begin_impl(ui_get_ctx()->ui_list_ctx, (ListBeginArgs) {
                                                          .id = id,
                                                          .ordered_ids = ordered_ids,
                                                          .count = count,
                                                          .set = set,
                                                          .config = config,
                                                      });
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
