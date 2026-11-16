// Keyboard/gamepad focus movement: tab order, spatial navigation, and focus repair.
//
// Both walks are restricted to the current focus scope (the active popup's items when one is open, otherwise
// items matching popup_focus_scope_id), so a menu or dialog cannot be navigated out of. Runs after hit testing
// so the rects spatial navigation scores are this frame's.

#include "ui_navigation.h"
#include <core/arena.h>
#include <core/hashmap.h>
#include <core/types.h>
#include <float.h>
#include <math.h>

static inline float calculate_overlap_1d(float a_min, float a_max, float b_min, float b_max) {
    float overlap_min = fmaxf(a_min, b_min);
    float overlap_max = fminf(a_max, b_max);
    return overlap_max - overlap_min;
}

// Is this item reachable by navigation right now? With a popup active, only items in the active
// popup; otherwise items matching the current focus scope.
static inline bool item_in_scope(const FlUiCtx* self, const ItemState* item, bool popup_active) {
    if (popup_active) {
        return item->in_active_popup;
    }
    return item->popup_scope_id == self->popup_focus_scope_id;
}

static inline bool widget_navigable(FlUiCtx* self, u32 widget_id, bool popup_active) {
    ItemState* item = hashmap_get(&self->shared->item_state, widget_id);
    if (item == nullptr)
        return false;

    return item_in_scope(self, item, popup_active);
}

static bool focus_order_contains(const FlUiCtx* self, u32 widget_id) {
    for_count(i, self->focus_order_count) {
        if (self->focus_order[i] == widget_id) {
            return true;
        }
    }
    return false;
}

static bool focus_order_navigable(FlUiCtx* self, u32 widget_id, bool popup_active) {
    return focus_order_contains(self, widget_id) && widget_navigable(self, widget_id, popup_active);
}

static u32 first_navigable_in_focus_order(FlUiCtx* self, bool popup_active) {
    for_count(i, self->focus_order_count) {
        if (widget_navigable(self, self->focus_order[i], popup_active)) {
            return self->focus_order[i];
        }
    }
    return 0;
}

static u32 focus_restore_target(FlUiCtx* self, bool popup_active) {
    if (focus_order_navigable(self, self->last_focused_id, popup_active)) {
        return self->last_focused_id;
    }
    return first_navigable_in_focus_order(self, popup_active);
}

static bool focus_ring_needs_repair(const FlUiCtx* self) {
    return self->focused_id != 0 && self->focus_order_count > 0;
}

static bool focus_repair_allowed(const FlUiCtx* self) {
    return focus_ring_needs_repair(self) && self->input_state.window_focused;
}

static void ui_restore_focus_if_needed(FlUiCtx* self) {
    bool popup_active = (self->popup_stack_count > 0);
    bool focused_valid = focus_order_navigable(self, self->focused_id, popup_active);

    // A zero focused_id means nobody holds the ring; only a non-zero id that fell out of this frame's focus order
    // is a vanished widget to repair.
    //
    // Only auto-restore focus while the window is OS-focused. A deactivated window keeps focused_id at
    // 0 (cleared on the focus-lost edge) so it shows no focus ring; focus-gained restores its stashed widget. A
    // single-window app never receives activation events, so window_focused stays true and this is a no-op gate.
    if (!focused_valid && focus_repair_allowed(self)) {
        self->focused_id = focus_restore_target(self, popup_active);
    }

    self->prev_focused_id = self->focused_id;
    if (self->focused_id != 0) {
        self->last_focused_id = self->focused_id;
    }
}

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

static bool center_in_viewport(const FlUiCtx* self, const ItemState* item) {
    float cx = ITEM_CENTER_X(item);
    float cy = ITEM_CENTER_Y(item);
    return !(cx < 0 || cx > self->layout_width || cy < 0 || cy > self->layout_height);
}

static bool items_aligned(const ItemState* from, const ItemState* to, u8 axis) {
    float overlap;
    if (axis == 1) {
        overlap = calculate_overlap_1d(ITEM_LEFT(from), ITEM_RIGHT(from), ITEM_LEFT(to), ITEM_RIGHT(to));
    } else {
        overlap = calculate_overlap_1d(ITEM_TOP(from), ITEM_BOTTOM(from), ITEM_TOP(to), ITEM_BOTTOM(to));
    }
    return overlap > 0.0f;
}

// Weighted navigation distance between two items; INFINITY if the candidate is not in the given direction.
static float calculate_navigation_distance(ItemState* from, ItemState* to, NavDirection dir, FlUiCtx* self) {
    // Skipping candidates whose center is outside the viewport keeps navigation out of the scrolled-away
    // regions of content views.
    if (!center_in_viewport(self, to)) {
        return INFINITY;
    }

    // delta indexed by axis: [0] = horizontal (x), [1] = vertical (y)
    float delta[2] = { ITEM_CENTER_X(to) - ITEM_CENTER_X(from), ITEM_CENTER_Y(to) - ITEM_CENTER_Y(from) };
    u8 axis = NAV_AXIS_SIGN[dir].axis;

    // Candidate must lie on the directed side of the focused item along the primary axis
    if (NAV_AXIS_SIGN[dir].sign * delta[axis] <= 0.0f) {
        return INFINITY;
    }

    float primary_distance = fabsf(delta[axis]);
    float secondary_distance = fabsf(delta[1 - axis]);

    // Aligned: primary distance dominates, secondary is only a tiebreaker. Unaligned candidates take a penalty
    // large enough that any aligned candidate wins.
    if (items_aligned(from, to, axis)) {
        return primary_distance + secondary_distance * 0.1f;
    }
    return primary_distance + secondary_distance * 3.0f + 10000.0f;
}

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

static u32 collect_scoped_focus_order(FlUiCtx* self, u32* scoped_focus_order) {
    bool popup_active = (self->popup_stack_count > 0);
    u32 scoped_count = 0;

    for_count(i, self->focus_order_count) {
        u32 widget_id = self->focus_order[i];
        if (widget_navigable(self, widget_id, popup_active)) {
            scoped_focus_order[scoped_count++] = widget_id;
        }
    }
    return scoped_count;
}

static i32 tab_order_next_index(i32 current_index, i32 count, bool forward) {
    if (current_index < 0) {
        return forward ? 0 : count - 1;
    }
    if (forward) {
        return (current_index + 1) % count;
    }
    return (current_index - 1 + count) % count;
}

// Navigate focus using tab order (Tab/Shift+Tab)

static void ui_navigate_tab_order(FlUiCtx* self, bool forward) {
    u32* scoped_focus_order = arena_alloc_array(self->render_frame_allocator, u32, self->focus_order_count);
    u32 scoped_count = collect_scoped_focus_order(self, scoped_focus_order);
    if (scoped_count == 0) {
        return;
    }

    i32 current_index = -1;
    for_count(i, scoped_count) {
        if (scoped_focus_order[i] == self->focused_id) {
            current_index = (i32)i;
            break;
        }
    }

    i32 next_index = tab_order_next_index(current_index, (i32)scoped_count, forward);
    self->focused_id = scoped_focus_order[next_index];
    self->should_scroll_to_focused = true;
}

static NavDirection nav_frame_direction(const FlNavFrame* nav) {
    if (nav->nav_up) {
        return NavDirection_Up;
    }
    if (nav->nav_down) {
        return NavDirection_Down;
    }
    if (nav->nav_left) {
        return NavDirection_Left;
    }
    return NavDirection_Right;
}

static void ui_navigate_tab_pressed(FlUiCtx* self) {
    u32 nav_from = self->focused_id;
    ui_navigate_tab_order(self, self->nav_next_pressed);
    if (self->focused_id != nav_from) {
        // Tab order has no spatial direction, so report None.
        ui_emit_feedback(self, FlUiFeedbackKind_NavMoved, nav_from, self->focused_id, FlUiFeedbackDir_None);
    }
}

static void ui_navigate_spatial(FlUiCtx* self, ItemState* focused_item, NavDirection dir) {
    u32 nav_from = self->focused_id;
    u32 best_id = find_best_navigation_candidate(self, focused_item, dir);

    // No candidate means the direction hit an edge.
    if (best_id == 0) {
        ui_emit_feedback(self, FlUiFeedbackKind_NavBlocked, nav_from, 0, NAV_FEEDBACK_DIR[dir]);
        return;
    }

    self->focused_id = best_id;
    self->should_scroll_to_focused = true;
    ui_emit_feedback(self, FlUiFeedbackKind_NavMoved, nav_from, best_id, NAV_FEEDBACK_DIR[dir]);
}

static bool nav_frame_has_direction(const FlNavFrame* nav) {
    return nav->nav_up || nav->nav_down || nav->nav_left || nav->nav_right;
}

void ui_navigation_update(FlUiCtx* self) {
    if (self->nav_next_pressed || self->nav_prev_pressed) {
        ui_navigate_tab_pressed(self);
    }

    // Spatial navigation (arrow keys / D-pad), skipped while a virtual keyboard owns all navigation input.
    bool virtual_keyboard_active = (self->active_virtual_keyboard != nullptr);
    if (!virtual_keyboard_active && nav_frame_has_direction(&self->nav_frame)) {
        ItemState* focused_item = hashmap_get(&self->shared->item_state, self->focused_id);
        if (focused_item != nullptr) {
            ui_navigate_spatial(self, focused_item, nav_frame_direction(&self->nav_frame));
        }
    }

    ui_restore_focus_if_needed(self);
}
