// List marquee (rubber-band) selection - CONTEXT.md `## Lists`.
//
// A drag begun on empty list space (a gap between rows or below the content, i.e. a press no row consumed) sweeps
// a selection rectangle and selects the rows it covers, emitting an endpoint-only SetRange like the click
// gestures. The anchor is held in content space, so edge auto-scroll mutating the visible range mid-gesture does
// not move it.
//
// The container is NOT registered as an interactive item: the empty-space test is "a press this frame that no row
// consumed, inside the container's viewport rect". Coverage works in boundingBox/mouse pixel space and assumes
// ui_scale == 1 (logical == physical); only the overlay's draw divides by scale.

#include "list_private.h"
#include <core/math.h>
#include "../ui.h"
#include "../ui_theme.h"

#include <math.h>

#define LIST_MARQUEE_FLOAT_SEED 0x4d515251 // 'MQRQ'
#define LIST_MARQUEE_FILL_SEED 0x4d515200  // 'MQR\0'
#define LIST_MARQUEE_FILL_ALPHA 64         // translucent wash so the rows show through

// A marquee band in content-space Y.
typedef struct Band {
    f32 lo;
    f32 hi;
} Band;

// The span of model positions a marquee band covers; empty when lo > hi.
typedef struct RowSpan {
    i64 lo;
    i64 hi;
} RowSpan;

// Map the content-space band to model positions: a row at position p occupies [p*stride, p*stride + row_height];
// it intersects [band_lo, band_hi] iff its bottom > band_lo and its top < band_hi. Solving for p gives a closed
// form - pure arithmetic on content Y, never a per-item lookup, so off-screen rows resolve identically.
static RowSpan covered_rows(const FlListContext* self, Band band) {
    f32 stride = list_row_stride(self);
    i64 count = (i64)self->ordered_count;
    i64 lo = (i64)ceilf((band.lo - self->current_row_height) / stride); // first row whose bottom > band_lo
    i64 hi = (i64)ceilf(band.hi / stride) - 1;                          // last row whose top < band_hi
    if (lo < 0) {
        lo = 0;
    }
    if (hi > count - 1) {
        hi = count - 1;
    }
    return (RowSpan) { lo, hi };
}

// The list has rows to cover and a usable row stride.
static bool has_model(const FlListContext* self) {
    return list_has_rows(self) && list_row_stride(self) > 0.0f;
}

// Emit the selection requests for the band. Replace mode clears first; additive (ctrl/⌘) unions the covered span
// onto the existing selection.
static void request_coverage(FlListContext* self, const ListState* st, Band band) {
    if (!has_model(self)) {
        return;
    }

    RowSpan span = covered_rows(self, band);
    bool covers_rows = span.lo <= span.hi;

    // A band that covers no row (e.g. a thin sweep inside a single gap) still empties the selection in replace mode.
    if (!st->marquee_additive) {
        list_push_request(self, (FlSelectionRequest) { .kind = FlSelectionRequestKind_ClearAll });
    }
    if (covers_rows) {
        list_push_request(self, (FlSelectionRequest) {
                                    .kind = FlSelectionRequestKind_SetRange,
                                    .id = self->ordered_ids[span.lo],
                                    .to_id = self->ordered_ids[span.hi],
                                    .selected = true,
                                });
    }
}

// Draw the marquee rectangle as a floating overlay (visual only - selection is resolved separately). The band is
// in content space; the x extent runs from the anchor to the cursor.
static void draw_band(FlListContext* self, const ListState* st, const ListViewport* vp, Band band) {
    f32 scroll = list_scroll_offset(self);
    f32 mouse_x = self->ui_ctx->input_state.mouse.x;
    f32 screen_top = clamp(vp->top + (band.lo - scroll), vp->top, vp->bottom);
    f32 screen_bottom = clamp(vp->top + (band.hi - scroll), vp->top, vp->bottom);
    f32 x0 = clamp(fminf(st->marquee_anchor_x, mouse_x), vp->left, vp->right);
    f32 x1 = clamp(fmaxf(st->marquee_anchor_x, mouse_x), vp->left, vp->right);

    f32 w = x1 - x0;
    f32 h = screen_bottom - screen_top;
    if (w <= 0.0f || h <= 0.0f) {
        return; // nothing visible on-screen (band fully scrolled out, or zero-width sweep)
    }

    const FlThemeColors* theme = fl_theme_colors();
    FlColor fill = theme->surface.selected;
    fill.a = LIST_MARQUEE_FILL_ALPHA;

    list_draw_overlay(self, &(ListOverlay) {
                                .float_seed = LIST_MARQUEE_FLOAT_SEED,
                                .fill_seed = LIST_MARQUEE_FILL_SEED,
                                .z_index = UI_Z_OVERLAY,
                                .x = x0,
                                .y = screen_top,
                                .w = w,
                                .h = h,
                                .fill = fill,
                                .border = fl_border(theme->interactive.base, 1),
                            });
}

static void disarm(ListState* st) {
    st->marquee_pressed = false;
    st->marquee_active = false;
}

static bool is_empty_space_press(const FlListContext* self, const ListViewport* vp) {
    const MouseState* mouse = &self->ui_ctx->input_state.mouse;
    return self->ui_ctx->mouse_pressed_this_frame && !self->frame_row_pressed
           && list_viewport_contains(vp, mouse->x, mouse->y);
}

// Arm on an empty-space press: a left press this frame, inside the viewport, that no row consumed. (A press on a
// row sets frame_row_pressed and runs the click gesture instead - the two are mutually exclusive.)
static void arm_on_empty_press(FlListContext* self, ListState* st, const ListViewport* vp) {
    f32 mouse_x = self->ui_ctx->input_state.mouse.x;
    f32 mouse_y = self->ui_ctx->input_state.mouse.y;
    if (!is_empty_space_press(self, vp)) {
        return;
    }

    st->marquee_pressed = true;
    st->marquee_active = false;
    st->marquee_additive = (ui_keyboard_modifiers() & FlKeyModifier_Control) != 0;
    st->marquee_anchor_content_y = (mouse_y - vp->top) + list_scroll_offset(self); // press point in content space
    st->marquee_anchor_x = mouse_x;
}

// Latch live only once past the movement threshold - so an empty-space press-and-release stays a (no-op) click,
// never a spurious one-pixel marquee.
static void latch_past_threshold(const FlListContext* self, ListState* st) {
    if (list_pointer_past_drag_threshold(self)) {
        st->marquee_active = true;
    }
}

void list_marquee_update(FlListContext* self, ListState* st) {
    ListViewport vp;
    if (!list_get_viewport(self, &vp)) {
        disarm(st);
        return;
    }

    arm_on_empty_press(self, st, &vp);

    // The gesture lives only while the button is held.
    if (!self->ui_ctx->input_state.mouse.buttons[FlMouseButton_Left]) {
        disarm(st);
        return;
    }
    if (!st->marquee_pressed) {
        return;
    }

    latch_past_threshold(self, st);
    if (!st->marquee_active) {
        return;
    }

    // The cursor's content-space Y is clamped to the viewport edges: a past-edge cursor pins the band edge at the
    // viewport and extends the band only as fast as auto-scroll advances scroll - the band doesn't teleport ahead
    // of the rows it covers.
    f32 scroll = list_scroll_offset(self);
    f32 cur_content_y = list_clamped_content_y(self, &vp, scroll);
    Band band = { .lo = fminf(st->marquee_anchor_content_y, cur_content_y),
                  .hi = fmaxf(st->marquee_anchor_content_y, cur_content_y) };

    request_coverage(self, st, band);
    list_edge_autoscroll(self, &vp);
    draw_band(self, st, &vp, band);
}
