// List helpers shared by the keyboard, marquee and drag gestures: state lookup, the request buffer, the viewport
// geometry, edge auto-scroll and the floating overlay rectangles.

#include "list_private.h"
#include <core/math.h>
#include "../layout.h"
#include "../list_uniform.h"
#include <external/clay/clay.h>

#include <math.h>

// Edge auto-scroll while a gesture is held past the viewport: base speed plus a gain on how far past the edge the
// cursor is, the overshoot capped so a cursor parked at the window edge doesn't fling the list. Pixels/second,
// integrated by the real frame delta so the rate is frame-rate independent.
#define LIST_AUTOSCROLL_BASE_SPEED 140.0f
#define LIST_AUTOSCROLL_GAIN 9.0f
#define LIST_AUTOSCROLL_MAX_OVERSHOOT 48.0f

ListState* list_state_get(FlListContext* self, u32 clay_id) {
    ListState* st = hashmap_get(&self->list_states, clay_id);
    if (st == nullptr) {
        ListState fresh = { 0 };
        st = hashmap_insert(&self->list_states, clay_id, fresh);
    }

    FL_ASSERT(st != nullptr);
    return st;
}

void list_push_request(FlListContext* self, FlSelectionRequest req) {
    if (self->req_count < self->req_capacity) {
        self->requests[self->req_count++] = req;
    }
}

bool list_has_rows(const FlListContext* self) {
    return self->ordered_ids != nullptr && self->ordered_count > 0;
}

// Linear scan: the same enumeration selection_set_apply already walks, and the cursor/anchor mapping is at most
// once per nav keystroke, never per row.
i32 list_index_of_id(const FlListContext* self, u64 id) {
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

// Clay owns the container rect independent of the interaction item registry, so the container need not be a
// registered interactive item.
bool list_get_viewport(const FlListContext* self, ListViewport* out) {
    u32 container_clay_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = self->current_id.name, .id = 0 });
    Clay_ElementData data = Clay_GetElementData((Clay_ElementId) { .id = container_clay_id });
    if (!data.found) {
        return false;
    }

    out->top = data.boundingBox.y;
    out->left = data.boundingBox.x;
    out->right = out->left + data.boundingBox.width;
    out->bottom = out->top + data.boundingBox.height;
    return true;
}

bool list_viewport_contains(const ListViewport* vp, f32 x, f32 y) {
    return x >= vp->left && x <= vp->right && y >= vp->top && y <= vp->bottom;
}

f32 list_row_stride(const FlListContext* self) {
    return self->current_row_height + self->current_gap;
}

f32 list_scroll_offset(const FlListContext* self) {
    return list_uniform_scroll_offset(self->ui_ctx->list_uniform_ctx, self->current_id.name);
}

f32 list_clamped_content_y(const FlListContext* self, const ListViewport* vp, f32 scroll) {
    f32 mouse_y = self->ui_ctx->input_state.mouse.y;
    return (clamp_f32_impl(mouse_y, vp->top, vp->bottom) - vp->top) + scroll;
}

bool list_pointer_past_drag_threshold(const FlListContext* self) {
    const MouseState* mouse = &self->ui_ctx->input_state.mouse;
    return mouse->drag_max_distance_sqr[FlMouseButton_Left] > UI_MOUSE_DRAG_THRESHOLD * UI_MOUSE_DRAG_THRESHOLD;
}

// Signed distance the cursor is past the viewport, 0 when it is inside.
static f32 edge_overshoot(f32 mouse_y, const ListViewport* vp) {
    if (mouse_y > vp->bottom) {
        return mouse_y - vp->bottom;
    }
    if (mouse_y < vp->top) {
        return mouse_y - vp->top;
    }
    return 0.0f;
}

void list_edge_autoscroll(FlListContext* self, const ListViewport* vp) {
    FlUiCtx* ui = self->ui_ctx;
    f32 overshoot = edge_overshoot(ui->input_state.mouse.y, vp);
    if (overshoot == 0.0f) {
        return;
    }

    f32 dir = overshoot > 0.0f ? 1.0f : -1.0f;
    f32 mag = fminf(fabsf(overshoot), LIST_AUTOSCROLL_MAX_OVERSHOOT);
    f32 step = dir * (LIST_AUTOSCROLL_BASE_SPEED + mag * LIST_AUTOSCROLL_GAIN) * ui->delta_time;
    list_uniform_nudge_scroll(ui->list_uniform_ctx, self->current_id.name, step, list_row_stride(self),
                              self->ordered_count);
}

// A floating element is authored in logical pixels, so position and size are divided by ui_scale here.
void list_draw_overlay(FlListContext* self, const ListOverlay* rect) {
    f32 scale = self->ui_ctx->ui_scale > 0.0f ? self->ui_ctx->ui_scale : 1.0f;

    fl_floating_start((FlLayoutId) { .name = self->current_id.name, .id = rect->float_seed });
    fl_floating_configure((FlFloatingConfig) {
        .attach_to = FlFloatingAttachTo_Root,
        .pointer_capture = FlFloatingPointerCapture_Passthrough,
        .z_index = rect->z_index,
        .offset = { rect->x / scale, rect->y / scale },
        .sizing = { sizing_fixed(rect->w / scale), sizing_fixed(rect->h / scale) },
    });
    fl_layout_start((FlLayoutId) { .name = self->current_id.name, .id = rect->fill_seed });
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_grow(), sizing_grow() },
        .color = rect->fill,
        .border = rect->border,
    });
    fl_layout_end();
    fl_layout_end();
}
