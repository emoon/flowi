#include "ui.h"
#include <core/hashmap.h>
#include <core/math.h>
#include "external/clay/clay.h"
#include "ui_navigation.h"
#include "ui_private.h"
#include "ui_widget_internal.h"

// One behavior function for every widget family, controlled by flags. Declared in ui.h for use by
// custom widgets (like text input).

FlLayoutId resolve_widget_id(FlString fallback, FlString id_override) {
    FlString id_string = id_override.length > 0 ? id_override : fallback;
    return (FlLayoutId) { .name = id_string, .id = 0 };
}

u32 ui_layout_id_to_clay_id(FlLayoutId layout_id) {
    Clay_ElementId clay_id = Clay__HashString(
        (Clay_String) { .chars = layout_id.name.data, .length = (u32)layout_id.name.length }, layout_id.id);
    return clay_id.id;
}

ItemState* get_item_state(FlLayoutId layout_id) {
    u32 clay_id = ui_layout_id_to_clay_id(layout_id);
    return hashmap_get(&g_ui_ctx->shared->item_state, clay_id);
}

// What one ui_item_behavior call knows about the item, shared by the signal steps below.
typedef struct ItemInput {
    FlUiCtx* ui;
    ItemState* item;
    u32 widget_id;
    FlItemFlags flags;
    bool blocked; // a popup is active and this item is outside its scope
} ItemInput;

static void register_in_scroll_area(const ItemInput* in) {
    FlUiCtx* self = in->ui;
    // Keyed on containment, not focusability: an unfocusable item in a scroll area is still clipped by it.
    if (self->scroll_area_stack_depth == 0) {
        return;
    }
    ScrollAreaStackEntry* current_scroll = &self->scroll_area_stack[self->scroll_area_stack_depth - 1];
    WidgetScrollInfo info = {
        .scroll_area_id = current_scroll->scroll_area_id,
        .pos_in_content = { 0, 0 }, // queried via Clay_GetElementData when needed
        .size = { 0, 0 },
    };
    hashmap_insert(&self->shared->widget_scroll_map, in->widget_id, info);
}

static void claim_focus(const ItemInput* in, bool auto_focus) {
    FlUiCtx* self = in->ui;
    ItemState* item = in->item;

    if (self->focus_order != nullptr && self->focus_order_count < self->focus_order_capacity) {
        self->focus_order[self->focus_order_count++] = in->widget_id;
    }

    // Continued immediate-mode emission must not steal focus back after navigation moves it elsewhere.
    if (auto_focus && !item->auto_focus_consumed) {
        self->focused_id = in->widget_id;
        item->auto_focus_consumed = true;
    }

    // Popup auto-focus lands on the first focusable widget in the popup.
    if (self->popup_needs_initial_focus && item->popup_scope_id == self->popup_focus_scope_id) {
        self->focused_id = in->widget_id;
        self->popup_needs_initial_focus = false;
    }
}

static void apply_focus_signals(const ItemInput* in, bool auto_focus, FlSignal* signal) {
    FlUiCtx* self = in->ui;
    const ItemState* item = in->item;

    claim_focus(in, auto_focus);

    signal->focused = item->state.focused && !in->blocked;

    // A widget that just gained focus does not activate on the same frame.
    bool just_gained_focus = item->state.focused && !item->was_focused;
    signal->activated = item->state.focused && self->nav_activate_pressed && !in->blocked && !just_gained_focus;

    if (signal->activated) {
        ui_emit_feedback(self, FlUiFeedbackKind_Activated, 0, in->widget_id, FlUiFeedbackDir_None);
    }
}

// Pressed and released in the same frame, where active_id has not been updated yet (that happens in
// ui_update_end, after widget rendering).
static bool is_fast_tap(const FlUiCtx* self, bool hovered) {
    return self->mouse_pressed_this_frame && self->mouse_released_this_frame && hovered;
}

static void apply_click_signals(const ItemInput* in, FlSignal* signal) {
    const FlUiCtx* self = in->ui;
    bool is_active = (self->active_id == in->widget_id);
    signal->pressed = signal->hovered && self->mouse_pressed_this_frame;
    signal->released = is_active && self->mouse_released_this_frame;

    signal->clicked = is_active && self->mouse_released_this_frame && signal->hovered;

    if (is_fast_tap(self, signal->hovered)) {
        signal->clicked = true;
    }

    // Keyboard activation counts as a click.
    if (signal->activated) {
        signal->clicked = true;
    }
}

static void apply_double_click(const ItemInput* in, FlSignal* signal) {
    if (!signal->clicked) {
        return;
    }
    ItemState* item = in->item;
    f64 now = in->ui->input_state.time;
    if (now - item->last_click_time < UI_MOUSE_DOUBLE_CLICK_TIME) {
        signal->double_clicked = true;
        item->click_count++;
    } else {
        item->click_count = 1;
    }
    item->last_click_time = now;
}

// Edge-triggered: fires once on the right-button press over the hovered item.
static void apply_right_click(const ItemInput* in, FlSignal* signal) {
    if (signal->hovered && in->ui->mouse_buttons_pressed_this_frame[FlMouseButton_Right]) {
        signal->right_clicked = true;
    }
}

// Items that opt into FlItem_DragThreshold only start dragging once the cursor has moved a few pixels
// from the press point; that's how a plain click (press + release in place) stays distinct from a
// drag. The distance is the per-button max travelled, tracked by the input layer since the press frame.
// Items without the flag drag immediately.
static bool drag_past_threshold(const ItemInput* in) {
    float max_distance_sqr = in->ui->input_state.mouse.drag_max_distance_sqr[FlMouseButton_Left];
    return !(in->flags & FlItem_DragThreshold) || max_distance_sqr > UI_MOUSE_DRAG_THRESHOLD * UI_MOUSE_DRAG_THRESHOLD;
}

static bool drag_in_progress(const ItemInput* in, bool is_active) {
    return is_active && in->item->is_dragging && drag_past_threshold(in);
}

static void apply_drag_signals(const ItemInput* in, FlSignal* signal) {
    const FlUiCtx* self = in->ui;
    ItemState* item = in->item;
    bool is_active = (self->active_id == in->widget_id);
    Vec2 mouse = { self->input_state.mouse.x, self->input_state.mouse.y };

    if (signal->hovered && self->mouse_pressed_this_frame) {
        item->is_dragging = true;
        item->drag_start_mouse = mouse;
    }

    if (drag_in_progress(in, is_active)) {
        signal->dragging = true;
        signal->drag_delta.x = mouse.x - item->drag_start_mouse.x;
        signal->drag_delta.y = mouse.y - item->drag_start_mouse.y;
    }

    if (self->mouse_released_this_frame && is_active) {
        item->is_dragging = false;
    }
}

static void apply_wheel_signal(const ItemInput* in, FlSignal* signal) {
    if (signal->hovered) {
        signal->wheel_delta.x = in->ui->input_state.mouse.wheel_x_cached;
        signal->wheel_delta.y = in->ui->input_state.mouse.wheel_y_cached;
    }
}

// Signals for the input kinds a widget opted into.
static void apply_pointer_signals(const ItemInput* in, FlSignal* signal) {
    FlItemFlags flags = in->flags;

    if (flags & FlItemFlags_clickable) {
        apply_click_signals(in, signal);
    }

    if (flags & FlItemFlags_double_click) {
        apply_double_click(in, signal);
    }

    if (flags & FlItemFlags_right_click) {
        apply_right_click(in, signal);
    }

    if (flags & FlItemFlags_draggable) {
        apply_drag_signals(in, signal);
    }

    if (flags & FlItemFlags_scroll_target) {
        apply_wheel_signal(in, signal);
    }
}

FlSignal ui_item_behavior(FlLayoutId lay_id, FlItemFlags flags, FlWidgetConfig cfg) {
    FlUiCtx* self = g_ui_ctx;

    // The flag says the widget family is focusable; cfg.focusable is the per-instance override on top of it.
    // Only an explicit No subtracts, since Default is also what a zeroed config carries. Yes is honoured by the
    // widget family instead, the only layer that knows what else focus costs it (the scrollbar pairs it with
    // focus_animation).
    if (cfg.focusable == FlFocusable_No) {
        flags &= (FlItemFlags)~FlItemFlags_focusable;
    }

    u32 widget_id = ui_layout_id_to_clay_id(lay_id);

    ItemState* item = get_or_create_key(self, widget_id);
    ANALYZER_ASSUME_NONNULL(item);
    item->flags = flags;

    // With a popup active, input to widgets outside its scope is blocked.
    ItemInput in = {
        .ui = self,
        .item = item,
        .widget_id = widget_id,
        .flags = flags,
        .blocked = self->popup_focus_scope_id != 0 && item->popup_scope_id != self->popup_focus_scope_id,
    };

    FlSignal signal = { 0 };

    if (flags & (FlItemFlags_clickable | FlItemFlags_draggable)) {
        signal.hovered = item->state.hot && !in.blocked;
    }

    // Records which scroll area contains this item, which is what ui_is_widget_visible and ui_scroll_to_widget read.
    register_in_scroll_area(&in);

    if (flags & FlItemFlags_focusable) {
        apply_focus_signals(&in, cfg.auto_focus, &signal);
    }

    if (!in.blocked) {
        apply_pointer_signals(&in, &signal);
    }

    signal.mouse_pos.x = self->input_state.mouse.x - ITEM_LEFT(item);
    signal.mouse_pos.y = self->input_state.mouse.y - ITEM_TOP(item);

    // The hot/active/focus animations run in item_state_update, off the flags stored above.

    // Kept for test inspection.
    item->last_signal = signal;

    return signal;
}

FlSignal ui_widget_behavior(FlLayoutId id, FlWidgetConfig cfg) {
    return ui_item_behavior(id, STANDARD_WIDGET_FLAGS, cfg);
}
