// List drag-reorder - CONTEXT.md Drop Relationship.
//
// A press that landed on a row (armed in item_begin) and then crossed the click-vs-drag threshold begins an
// intra-list reorder. Dragging past the viewport edge auto-scrolls to extend the reach.

#include "list_private.h"
#include <core/arena.h>
#include "../ui.h"
#include "../ui_theme.h"

#include <math.h>

// Overlay seeds, derived from the list name so they never collide with rows or the marquee.
#define LIST_DRAG_GHOST_FLOAT_SEED 0x44475348 // 'DGSH'
#define LIST_DRAG_GHOST_FILL_SEED 0x44475300  // 'DGS\0'
#define LIST_DRAG_LINE_FLOAT_SEED 0x444c4e45  // 'DLNE'
#define LIST_DRAG_LINE_FILL_SEED 0x444c4e00   // 'DLN\0'
#define LIST_DRAG_GHOST_ALPHA 200             // ghost fill opacity (rows show through faintly)
#define LIST_DRAG_LINE_THICKNESS 2.0f         // insertion line height (logical px)

// The drop a drag resolves to: the dragged ids land next to target_id.
typedef struct DropTarget {
    u64 id;
    FlDropRelationship rel;
} DropTarget;

// Candidate insertion index in [0, count] for a reorder drag: the dragged item(s) land BEFORE row ins. The
// cursor's row and which half it falls in decide it (top half -> before that row, bottom half -> after), pure
// arithmetic on the uniform stride so an off-screen target resolves exactly like an on-screen one.
static u32 insertion_index(f32 cur_content_y, f32 stride, f32 row_height, u32 count) {
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

// The row an insertion index lands next to: before the first row for index 0, else after the row above it.
static DropTarget drop_target_for(const FlListContext* self, u32 ins) {
    DropTarget drop = { 0, FlDropRelationship_Before };
    if (!list_has_rows(self)) {
        return drop;
    }
    if (ins == 0) {
        drop.id = self->ordered_ids[0];
    } else {
        drop.id = self->ordered_ids[ins - 1];
        drop.rel = FlDropRelationship_After;
    }
    return drop;
}

// The dragged stable ids for this frame, into the frame arena: the WHOLE selection when the pressed row is
// selected (dragging any selected row drags all of them), else just the pressed row. Recomputed each frame - the
// selection cannot change while the drag holds (row clicks are suppressed), so this is stable across the gesture.
static const u64* collect_from_ids(FlListContext* self, u64 press_id, u32* out_count) {
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
// centre of the gap before row ins), clamped to the viewport.
static void draw_insertion(FlListContext* self, const ListViewport* vp, f32 scroll, u32 ins) {
    f32 line_content_y = (f32)ins * list_row_stride(self) - self->current_gap * 0.5f; // middle of the preceding gap
    f32 screen_y = clamp_f32_impl(vp->top + (line_content_y - scroll), vp->top, vp->bottom);

    list_draw_overlay(self, &(ListOverlay) {
                                .float_seed = LIST_DRAG_LINE_FLOAT_SEED,
                                .fill_seed = LIST_DRAG_LINE_FILL_SEED,
                                .z_index = UI_Z_OVERLAY + 1,
                                .x = vp->left,
                                .y = screen_y - LIST_DRAG_LINE_THICKNESS * 0.5f,
                                .w = vp->right - vp->left,
                                .h = LIST_DRAG_LINE_THICKNESS,
                                .fill = fl_theme_colors()->interactive.base,
                            });
}

// Draw the drag ghost: a translucent, row-height, viewport-width wash following the cursor vertically (centred on
// it), so the dragged row visibly floats.
static void draw_ghost(FlListContext* self, const ListViewport* vp) {
    f32 height = self->current_row_height;
    f32 top = self->ui_ctx->input_state.mouse.y - height * 0.5f;

    const FlThemeColors* theme = fl_theme_colors();
    FlColor fill = theme->surface.selected;
    fill.a = LIST_DRAG_GHOST_ALPHA;

    list_draw_overlay(self, &(ListOverlay) {
                                .float_seed = LIST_DRAG_GHOST_FLOAT_SEED,
                                .fill_seed = LIST_DRAG_GHOST_FILL_SEED,
                                .z_index = UI_Z_OVERLAY + 2,
                                .x = vp->left,
                                .y = top,
                                .w = vp->right - vp->left,
                                .h = height,
                                .fill = fill,
                                .border = fl_border(theme->interactive.base, 1),
                            });
}

// Still dragging: expose the live state and draw the default affordances (unless the caller overrides them). A drag
// can reach off-screen positions the same way the marquee does, so edge auto-scroll applies.
static void show_live_drag(FlListContext* self, const ListViewport* vp, f32 scroll, u32 ins) {
    self->drag_active_out = true;
    if (!self->suppress_drag_visuals) {
        draw_insertion(self, vp, scroll, ins);
        draw_ghost(self, vp);
    }
    list_edge_autoscroll(self, vp);
}

void list_drag_update(FlListContext* self, ListState* st) {
    if (!st->drag_pressed) {
        return;
    }
    FlUiCtx* ui = self->ui_ctx;
    bool released = ui->mouse_released_this_frame || !ui->input_state.mouse.buttons[FlMouseButton_Left];

    // Armed but the threshold was never crossed: a release here was a plain click (handled in item_begin) - just
    // disarm. A sub-threshold hold keeps waiting.
    if (!st->drag_active) {
        if (released) {
            st->drag_pressed = false;
        }
        return;
    }

    ListViewport vp;
    if (!list_get_viewport(self, &vp)) {
        st->drag_pressed = false;
        st->drag_active = false;
        return;
    }

    // Candidate insertion from the (viewport-clamped) cursor content Y. The clamp pins the candidate at the edge
    // while edge auto-scroll advances scroll underneath it.
    f32 scroll = list_scroll_offset(self);
    f32 cur_content_y = list_clamped_content_y(self, &vp, scroll);
    u32 ins = insertion_index(cur_content_y, list_row_stride(self), self->current_row_height, self->ordered_count);
    DropTarget drop = drop_target_for(self, ins);

    u32 from_count = 0;
    const u64* from_ids = collect_from_ids(self, st->drag_press_id, &from_count);

    // Publish the payload - shared between the live (candidate) and committed (reorder request) phases.
    self->drag_from_ids = from_ids;
    self->drag_from_count = from_count;
    self->drag_target_id = drop.id;
    self->drag_rel = drop.rel;

    if (released) {
        // Drop: the reorder request is emitted when there is something to move and somewhere to put it.
        self->reorder_emitted = (from_count > 0 && drop.id != 0);
        st->drag_pressed = false;
        st->drag_active = false;
        return;
    }

    show_live_drag(self, &vp, scroll, ins);
}
