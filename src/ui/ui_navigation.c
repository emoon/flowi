///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Keyboard/gamepad focus movement - tab order, spatial navigation, and focus repair.
//
// Both walks are restricted to the current focus scope (the active popup's items when one is open, otherwise
// items matching popup_focus_scope_id), so a menu or dialog cannot be navigated out of. Runs after hit testing
// so the rects spatial navigation scores are this frame's.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "ui_navigation.h"
#include <core/arena.h>
#include <core/hashmap.h>
#include <core/types.h>
#include <float.h>
#include <math.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline float calculate_overlap_1d(float a_min, float a_max, float b_min, float b_max) {
    float overlap_min = fmaxf(a_min, b_min);
    float overlap_max = fminf(a_max, b_max);
    return overlap_max - overlap_min;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Is this item reachable by navigation right now? With a popup active, only items in the active
// popup; otherwise items matching the current focus scope.
static inline bool item_in_scope(const FlUiCtx* self, const ItemState* item, bool popup_active) {
    if (popup_active) {
        return item->in_active_popup;
    }
    return item->popup_scope_id == self->popup_focus_scope_id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline bool widget_navigable(FlUiCtx* self, u32 widget_id, bool popup_active) {
    ItemState* item = hashmap_get(&self->shared->item_state, widget_id);
    if (item == nullptr)
        return false;

    return item_in_scope(self, item, popup_active);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ui_restore_focus_if_needed(FlUiCtx* self) {
    bool popup_active = (self->popup_stack_count > 0);

    bool focused_valid = false;
    for_count(i, self->focus_order_count) {
        if (self->focus_order[i] == self->focused_id) {
            focused_valid = widget_navigable(self, self->focused_id, popup_active);
            break;
        }
    }

    // A zero focused_id means nobody holds the ring; only a NON-zero id that fell out of this frame's focus order
    // is a vanished widget to repair.
    //
    // Only auto-restore focus while the window is OS-focused. A deactivated window keeps focused_id at
    // 0 (cleared on the focus-lost edge) so it shows no focus ring; focus-gained restores its stashed widget. A
    // single-window app never receives activation events, so window_focused stays true and this is a no-op gate.
    if (!focused_valid && self->focused_id != 0 && self->focus_order_count > 0 && self->input_state.window_focused) {
        bool restored = false;

        for_count(i, self->focus_order_count) {
            if (self->focus_order[i] == self->last_focused_id
                && widget_navigable(self, self->last_focused_id, popup_active)) {
                self->focused_id = self->last_focused_id;
                restored = true;
                break;
            }
        }

        // Fall back to the first focusable widget in scope.
        if (!restored) {
            for_count(i, self->focus_order_count) {
                if (widget_navigable(self, self->focus_order[i], popup_active)) {
                    self->focused_id = self->focus_order[i];
                    restored = true;
                    break;
                }
            }
        }

        if (!restored) {
            self->focused_id = 0;
        }
    }

    self->prev_focused_id = self->focused_id;
    if (self->focused_id != 0) {
        self->last_focused_id = self->focused_id;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef enum { NavDirection_Up, NavDirection_Down, NavDirection_Left, NavDirection_Right } NavDirection;

// A NavDirection is an (axis, sign) pair. axis 0 = horizontal (x), axis 1 = vertical (y).
// In-direction test is sign*delta[axis] > 0; primary span is delta[axis], secondary is delta[1-axis].
typedef struct {
    u8 axis;
    float sign;
} NavAxisSign;

static const NavAxisSign NAV_AXIS_SIGN[4] = {
    [NavDirection_Up] = { 1, -1.0f },
    [NavDirection_Down] = { 1, +1.0f },
    [NavDirection_Left] = { 0, -1.0f },
    [NavDirection_Right] = { 0, +1.0f },
};

// Map the internal spatial NavDirection to the public feedback direction reported in FlUiFeedbackEvent.
static const FlUiFeedbackDir NAV_FEEDBACK_DIR[4] = {
    [NavDirection_Up] = FlUiFeedbackDir_Up,
    [NavDirection_Down] = FlUiFeedbackDir_Down,
    [NavDirection_Left] = FlUiFeedbackDir_Left,
    [NavDirection_Right] = FlUiFeedbackDir_Right,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Weighted navigation distance between two items; INFINITY if the candidate is not in the given direction.

static float calculate_navigation_distance(ItemState* from, ItemState* to, NavDirection dir, FlUiCtx* self) {
    // Skipping candidates whose center is outside the viewport keeps navigation out of the scrolled-away
    // regions of content views.
    float cx = ITEM_CENTER_X(to);
    float cy = ITEM_CENTER_Y(to);
    float viewport_w = self->layout_width;
    float viewport_h = self->layout_height;

    if (cx < 0 || cx > viewport_w || cy < 0 || cy > viewport_h) {
        return INFINITY;
    }

    float focused_cx = ITEM_CENTER_X(from);
    float focused_cy = ITEM_CENTER_Y(from);

    // delta indexed by axis: [0] = horizontal (x), [1] = vertical (y)
    float delta[2] = { cx - focused_cx, cy - focused_cy };
    u8 axis = NAV_AXIS_SIGN[dir].axis;
    float sign = NAV_AXIS_SIGN[dir].sign;

    // Candidate must lie on the directed side of the focused item along the primary axis
    if (sign * delta[axis] <= 0.0f) {
        return INFINITY;
    }

    // Alignment is overlap in the perpendicular axis.
    bool is_aligned = false;
    float alignment_overlap;

    if (axis == 1) {
        alignment_overlap = calculate_overlap_1d(ITEM_LEFT(from), ITEM_RIGHT(from), ITEM_LEFT(to), ITEM_RIGHT(to));
    } else {
        alignment_overlap = calculate_overlap_1d(ITEM_TOP(from), ITEM_BOTTOM(from), ITEM_TOP(to), ITEM_BOTTOM(to));
    }

    if (alignment_overlap > 0.0f) {
        is_aligned = true;
    }

    float primary_distance = fabsf(delta[axis]);
    float secondary_distance = fabsf(delta[1 - axis]);

    // Aligned: primary distance dominates, secondary is only a tiebreaker. Unaligned candidates take a penalty
    // large enough that any aligned candidate wins.
    float distance;
    if (is_aligned) {
        distance = primary_distance + secondary_distance * 0.1f;
    } else {
        distance = primary_distance + secondary_distance * 3.0f + 10000.0f;
    }

    return distance;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static u32 find_best_navigation_candidate(FlUiCtx* self, ItemState* focused, NavDirection dir) {
    u32 best_id = 0;
    float best_distance = FLT_MAX;

    bool popup_active = (self->popup_stack_count > 0);

    for_count(i, self->focus_order_count) {
        u32 candidate_id = self->focus_order[i];

        if (candidate_id == self->focused_id)
            continue;

        ItemState* candidate = hashmap_get(&self->shared->item_state, candidate_id);
        if (candidate == nullptr)
            continue;

        if (!item_in_scope(self, candidate, popup_active))
            continue;

        float distance = calculate_navigation_distance(focused, candidate, dir, self);

        if (distance < best_distance) {
            best_distance = distance;
            best_id = candidate_id;
        }
    }

    return best_id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Navigate focus using tab order (Tab/Shift+Tab)

static void ui_navigate_tab_order(FlUiCtx* self, bool forward) {
    bool popup_active = (self->popup_stack_count > 0);

    u32* scoped_focus_order = arena_alloc_array(self->render_frame_allocator, u32, self->focus_order_count);
    u32 scoped_count = 0;

    for_count(i, self->focus_order_count) {
        u32 widget_id = self->focus_order[i];
        ItemState* item = hashmap_get(&self->shared->item_state, widget_id);
        if (item == nullptr)
            continue;

        if (item_in_scope(self, item, popup_active)) {
            scoped_focus_order[scoped_count++] = widget_id;
        }
    }

    i32 current_index = -1;
    for_count(i, scoped_count) {
        if (scoped_focus_order[i] == self->focused_id) {
            current_index = (i32)i;
            break;
        }
    }

    if (scoped_count > 0) {
        i32 next_index;
        if (forward) {
            if (current_index < 0) {
                next_index = 0;
            } else {
                next_index = (current_index + 1) % (i32)scoped_count;
            }
        } else {
            if (current_index < 0) {
                next_index = (i32)scoped_count - 1;
            } else {
                next_index = (current_index - 1 + (i32)scoped_count) % (i32)scoped_count;
            }
        }

        self->focused_id = scoped_focus_order[next_index];
        self->should_scroll_to_focused = true;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_navigation_update(FlUiCtx* self) {
    if (self->nav_next_pressed || self->nav_prev_pressed) {
        u32 nav_from = self->focused_id;
        ui_navigate_tab_order(self, self->nav_next_pressed);
        if (self->focused_id != nav_from) {
            // Tab order has no spatial direction - report None.
            ui_emit_feedback(self, FlUiFeedbackKind_NavMoved, nav_from, self->focused_id, FlUiFeedbackDir_None);
        }
    }

    // Spatial navigation (arrow keys / D-pad), skipped while a virtual keyboard owns all navigation input.
    bool virtual_keyboard_active = (self->active_virtual_keyboard != nullptr);
    FlNavFrame* nav = &self->nav_frame;
    if (!virtual_keyboard_active && (nav->nav_up || nav->nav_down || nav->nav_left || nav->nav_right)) {
        ItemState* focused_item = hashmap_get(&self->shared->item_state, self->focused_id);

        if (focused_item != nullptr) {
            NavDirection dir;

            if (nav->nav_up) {
                dir = NavDirection_Up;
            } else if (nav->nav_down) {
                dir = NavDirection_Down;
            } else if (nav->nav_left) {
                dir = NavDirection_Left;
            } else { // nav_right
                dir = NavDirection_Right;
            }

            u32 nav_from = self->focused_id;
            u32 best_id = find_best_navigation_candidate(self, focused_item, dir);

            // No candidate means the direction hit an edge.
            if (best_id != 0) {
                self->focused_id = best_id;
                self->should_scroll_to_focused = true;
                ui_emit_feedback(self, FlUiFeedbackKind_NavMoved, nav_from, best_id, NAV_FEEDBACK_DIR[dir]);
            } else {
                ui_emit_feedback(self, FlUiFeedbackKind_NavBlocked, nav_from, 0, NAV_FEEDBACK_DIR[dir]);
            }
        }
    }

    ui_restore_focus_if_needed(self);
}
