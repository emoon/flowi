// List keyboard navigation: arrows / Home / End / type-ahead move the focus cursor, Shift extends the selection,
// Ctrl+A selects all and Enter / Space activate the cursor row.

#include "list_private.h"
#include "../ui.h"

// The row type-ahead should jump to, or -1 when there is nothing to jump to. cur is the current cursor index (-1 if
// unset); the matcher owns the cycle-vs-narrow and idle-reset behavior (see type_ahead.h).
static i32 typeahead_target(FlListContext* self, ListState* st, i32 cur) {
    if (self->labels == nullptr) {
        return -1;
    }
    FlString typed = self->ui_ctx->text_input_this_frame;
    f64 now = self->ui_ctx->input_state.time;
    return type_ahead_match(&st->type_ahead, self->labels, self->ordered_count, cur, typed, now, TYPE_AHEAD_CAP);
}

// Step the cursor one row in dir (+1 down, -1 up). From an unset cursor it lands on the first row going down and
// the last going up; at the end it stays put and *hit_edge is set.
static i32 step_cursor(i32 cur, i32 dir, i32 count, bool* hit_edge) {
    if (cur < 0) {
        return (dir > 0) ? 0 : count - 1;
    }
    i32 next = cur + dir;
    *hit_edge = (next < 0 || next >= count);
    return *hit_edge ? cur : next;
}

// Resolve the row the cursor moves to this frame from the nav keys (cur when nothing moves it). Consumes the nav
// directions it acts on so they do not also drive global focus navigation. *directional is set for the keys that
// Shift extends the selection with; type-ahead is not one of them.
static i32 resolve_target(FlListContext* self, ListState* st, i32 cur, bool* directional) {
    FlNavFrame nav = fl_get_nav_frame();
    i32 count = (i32)self->ordered_count;
    *directional = true;
    if (nav.nav_down) {
        fl_consume_nav_down(); // this list owns Down while focused, so it must not also spatial-nav away
        bool hit_edge = false;
        i32 target = step_cursor(cur, 1, count, &hit_edge);
        self->nav_down_edge |= hit_edge;
        return target;
    }
    if (nav.nav_up) {
        fl_consume_nav_up();
        bool hit_edge = false;
        i32 target = step_cursor(cur, -1, count, &hit_edge);
        self->nav_up_edge |= hit_edge;
        return target;
    }
    if (ui_key_pressed(FlKey_Home)) {
        return 0;
    }
    if (ui_key_pressed(FlKey_End)) {
        return count - 1;
    }

    *directional = false;
    i32 ta = typeahead_target(self, st, cur);
    return (ta >= 0) ? ta : cur;
}

static bool activation_pressed(void) {
    return ui_key_pressed(FlKey_Enter) || ui_key_pressed(FlKey_Kp_Enter) || ui_key_pressed(FlKey_Space);
}

// Extend the selection from the anchor to the new cursor (an endpoint-only range, which survives virtualization
// through selection_set_apply). The anchor stays put so a continued shift-arrow re-ranges from the same pivot.
static void extend_selection(FlListContext* self, ListState* st, i32 cur, u64 new_id) {
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
}

// Ctrl+A: select all, resolved against the full ordered slice (covers rows virtualized away). It does not move
// the cursor; emitted regardless of set, only auto-apply is gated on set != nullptr (see list_end_impl). A
// single-selection list has no "select all", so it is suppressed there.
static bool handle_select_all(FlListContext* self, bool ctrl) {
    bool pressed = ctrl && ui_key_pressed(FlKey_A);
    if (self->single_select || !pressed) {
        return false;
    }
    list_push_request(self, (FlSelectionRequest) { .kind = FlSelectionRequestKind_SetAll, .selected = true });
    return true;
}

// Shift on a directional key extends the selection from the anchor (not in a single-selection list).
static bool extends_selection(const FlListContext* self, u32 mods, bool directional) {
    return directional && (mods & FlKeyModifier_Shift) != 0 && !self->single_select;
}

// A cursor move from row cur to row target; extend = Shift-extend the selection with it.
typedef struct CursorMove {
    i32 cur;
    i32 target;
    bool extend;
} CursorMove;

// A plain move leaves the selection untouched (cursor is distinct from selection) and the anchor follows the cursor
// so a later Shift+arrow extends from where the cursor came to rest.
static void commit_move(FlListContext* self, ListState* st, CursorMove move) {
    u64 new_id = self->ordered_ids[move.target];
    if (move.extend) {
        extend_selection(self, st, move.cur, new_id);
    } else {
        st->anchor_id = new_id;
    }
    st->focus_cursor_id = new_id;
}

// Enter / Space activate the cursor row (committed id on the result), independent of any move this frame.
static void activate_row(FlListContext* self, i32 target) {
    if (activation_pressed() && target >= 0) {
        self->activated_id = self->ordered_ids[target];
    }
}

// Runs at begin (before list_uniform_begin so a cursor move can scroll the target into view this same frame).
i32 list_handle_keyboard(FlListContext* self, ListState* st, bool has_focus) {
    if (!has_focus || !list_has_rows(self)) {
        return -1;
    }

    u32 mods = ui_keyboard_modifiers();
    i32 cur = list_index_of_id(self, st->focus_cursor_id);
    if (handle_select_all(self, (mods & FlKeyModifier_Control) != 0)) {
        return -1;
    }

    bool directional;
    i32 target = resolve_target(self, st, cur, &directional);
    activate_row(self, target);
    if (target == cur) {
        return -1;
    }

    commit_move(self, st, (CursorMove) { cur, target, extends_selection(self, mods, directional) });
    return target;
}
