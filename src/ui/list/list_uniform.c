// A virtualized scrollable list where all items have the same height. Callers render arbitrary content inside
// each item using a begin/end pattern.

#include "list_uniform.h"
#include <core/arena.h>
#include <core/math.h>
#include <core/hashmap.h>
#include <core/log.h>
#include "../layout.h"
#include "list_geometry.h"
#include "list_ids.h"
#include "../pixel_snap.h"
#include "../scroll_core.h"
#include "../ui.h"
#include "../ui_private.h"
#include "../ui_scroll.h"
#include "../wheel_scope.h"
#include <string.h>

#define LIST_UNIFORM_SCROLL_SPEED 12.0f // Smooth scroll lerp factor (per-caller override)
#define LIST_UNIFORM_VISIBLE_BUFFER 2   // Extra items to render outside viewport

typedef struct ListUniformState {
    f64 scroll_offset;    // Current scroll position (pixels)
    f64 scroll_target;    // Target for smooth scrolling
    f32 container_height; // Actual container height from previous frame
    f32 content_width;    // Width rows get (container width minus the scrollbar reserve) from previous frame
    ListRowGeometry rows; // Row Geometry the list was last begun with
    bool scrollbar_engaged;
} ListUniformState;

struct FlListUniformContext {
    FlArena* arena;
    FlUiCtx* ui_ctx;

    hashmap(FlString, ListUniformState) states;

    // Current list being rendered
    FlString current_id;
    ListRowGeometry current_rows;
    FlScrollbarDisplayMode current_scrollbar_mode;
    ListUniformState* current_state;

    u32 visible_start;
    u32 visible_end;

    f32 viewport_height;
    f64 content_height;
    f32 bar_reserve; // Right padding the container took this frame for the scrollbar

    // Current item state (during item_begin/item_end)
    bool in_item;

    // Layout ID for querying container size
    u32 current_layout_id;
};

FlListUniformContext* list_uniform_create(FlArena* arena) {
    FlListUniformContext* ctx = arena_alloc_zero(arena, FlListUniformContext);
    ctx->arena = arena;
    ctx->ui_ctx = g_ui_ctx;

    hashmap_new(&ctx->states, arena, 16);

    log_info("Uniform list context created");
    return ctx;
}

void list_uniform_destroy(FlListUniformContext* self) {
    (void)self;
    // Arena-allocated, nothing to free
}

static ListUniformState* get_or_create_state(FlListUniformContext* self, FlString id) {
    ListUniformState* state = hashmap_string_get(&self->states, id);
    if (state != nullptr) {
        return state;
    }
    ListUniformState new_state = {
        .scroll_offset = 0.0,
        .scroll_target = 0.0,
    };
    return hashmap_string_insert(&self->states, id, new_state);
}

static void update_scroll_for_selection(FlListUniformContext* self, u32 selected) {
    f64 item_top = list_geometry_row_top(self->current_rows, selected);

    // Always position the selected item at 1/3 from the top of the viewport.
    f32 target_position = self->viewport_height * 0.33f;
    f64 target = item_top - (f64)target_position;

    f64 max_scroll = scroll_max(list_geometry_content_height(self->current_rows), self->viewport_height);

    self->current_state->scroll_target = clamp(target, 0.0, max_scroll);
}

static void scroll_by_wheel(FlListUniformContext* self, f64 max_scroll) {
    f32 wheel_y = ui_get_mouse_wheel().y;
    if (wheel_y == 0.0f || !wheel_scope_owns_wheel(self->ui_ctx, self->current_layout_id)) {
        return;
    }
    f32 step = wheel_y * SCROLL_CORE_DEFAULT_WHEEL_SPEED * ui_scale_or_one(self->ui_ctx->ui_scale);
    f64 target = self->current_state->scroll_target - (f64)step;
    self->current_state->scroll_target = clamp(target, 0.0, max_scroll);
}

static void scrollbar_update(FlListUniformContext* self) {
    ListUniformState* state = self->current_state;
    state->scrollbar_engaged = false;
    FlScrollbarDisplayMode mode = self->current_scrollbar_mode;
    if (mode == FlScrollbarDisplayMode_Never) {
        return;
    }

    ScrollExtent extent = {
        .viewport = self->viewport_height,
        .content = self->content_height,
        .bar_strip = ui_scroll_overlay_bar_reserve(),
    };
    f64 max_scroll = scroll_max(extent.content, extent.viewport);
    f32 shown = max_scroll > 0.0 ? (f32)(state->scroll_offset / max_scroll) : 0.0f;
    f32 normalized = shown;

    FlSignal signal = ui_scroll_overlay_bar(self->current_layout_id, mode, extent, &normalized);
    state->scrollbar_engaged = signal.hovered || signal.dragging;
    if (normalized != shown) {
        state->scroll_offset = (f64)normalized * max_scroll;
        state->scroll_target = state->scroll_offset;
    }
}

ListRowGeometry list_uniform_geometry(const FlUiCtx* ctx, f32 row_height, f32 gap, u32 count) {
    return list_row_geometry(row_height, gap, ctx->ui_scale, count);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void list_uniform_begin_update(FlListUniformContext* self) {
    self->viewport_height = self->ui_ctx->layout_height;
}

FlListUniformRange list_uniform_begin_rows(FlListUniformContext* self, FlString id, ListRowGeometry rows,
                                           ListUniformScroll scroll) {
    self->current_id = id;
    self->current_rows = rows;
    self->current_scrollbar_mode = scroll.scrollbar_mode;

    self->current_state = get_or_create_state(self, id);
    ANALYZER_ASSUME_NONNULL(self->current_state);
    self->current_state->rows = rows;

    self->in_item = false;

    // Until a height has been measured (first frame), size the viewport to ~5 items.
    f32 container_height = self->current_state->container_height;
    if (container_height <= 0.0f) {
        container_height = list_geometry_span_height(rows, 5);
    }
    self->viewport_height = container_height;

    FlLayoutId layout_id = list_container_layout_id(id);
    self->current_layout_id = ui_layout_id_to_clay_id(layout_id);

    self->content_height = list_geometry_content_height(rows);
    f64 max_scroll = scroll_max(self->content_height, self->viewport_height);
    scroll_by_wheel(self, max_scroll);
    wheel_scope_begin(self->ui_ctx, self->current_layout_id, max_scroll > 0.0);

    if (scroll.scroll_to != 0 && scroll.scroll_to <= rows.count) {
        update_scroll_for_selection(self, scroll.scroll_to - 1);
    }

    self->current_state->scroll_offset
        = scroll_smooth_step(self->current_state->scroll_offset, self->current_state->scroll_target, max_scroll,
                             LIST_UNIFORM_SCROLL_SPEED, self->ui_ctx->delta_time);

    ScrollRange visible = list_geometry_visible_rows(rows, self->current_state->scroll_offset, self->viewport_height,
                                                     LIST_UNIFORM_VISIBLE_BUFFER);
    self->visible_start = visible.start;
    self->visible_end = visible.end;

    f64 first_item_top = list_geometry_row_top(rows, self->visible_start);
    FlVec2 scroll_pos = { 0, (f32)(first_item_top - self->current_state->scroll_offset) };

    bool has_bar = scroll.scrollbar_mode != FlScrollbarDisplayMode_Never && max_scroll > 0.0;
    f32 bar_reserve = has_bar ? ui_scroll_overlay_bar_reserve() : 0.0f;
    self->bar_reserve = bar_reserve;

    fl_layout_start(layout_id);
    // Track the container so list_uniform_end can read its measured height from item_state.
    ui_track_element(layout_id);
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_grow(), sizing_grow() },
        .padding = { .right = (u16)bar_reserve },
        .direction = LayoutDirection_TopToBottom,
        .child_gap = rows.gap,
        .clip = { .vertical = true, .manual_scroll = true, .scroll_pos = scroll_pos },
    });

    return (FlListUniformRange) {
        .start = self->visible_start,
        .end = self->visible_end,
        .total = rows.count,
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlListUniformRange list_uniform_begin(FlListUniformContext* self, FlString id, u32 total_count,
                                      FlListUniformOpts opts) {
    ListRowGeometry rows = list_uniform_geometry(self->ui_ctx, opts.height_hint, opts.gap, total_count);
    return list_uniform_begin_rows(self, id, rows,
                                   (ListUniformScroll) {
                                       .scroll_to = opts.scroll_to,
                                       .scrollbar_mode = opts.scrollbar_mode,
                                   });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlLayoutId list_uniform_row_begin(FlListUniformContext* self, u32 index, FlLayoutConfig decoration) {
    FlLayoutId row_id = list_row_layout_id(self->current_id, index);
    decoration.sizing = (FlLayoutSizing) { sizing_grow(), sizing_fit() };

    fl_layout_start(row_id);
    fl_layout_configure(decoration);
    return row_id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void list_uniform_item_begin(FlListUniformContext* self, u32 index) {
    self->in_item = true;
    list_uniform_row_begin(self, index, (FlLayoutConfig) { 0 });
}

void list_uniform_item_end(FlListUniformContext* self) {
    if (!self->in_item) {
        return;
    }
    self->in_item = false;
    fl_layout_end();
}

void list_uniform_end(FlListUniformContext* self) {
    scrollbar_update(self);
    fl_layout_end();
    wheel_scope_end(self->ui_ctx);

    // Read the measured container height from item_state and store it for next frame.
    FlElementState state = ui_get_element_state_by_id(self->current_layout_id);
    if (state.found && state.size.y > 0.0f) {
        self->current_state->container_height = state.size.y;
    }
    if (state.found && state.size.x > 0.0f) {
        self->current_state->content_width
            = state.size.x - ui_scale_and_snap(self->bar_reserve, self->ui_ctx->ui_scale);
    }
}

f32 list_uniform_content_width(FlListUniformContext* self, FlString id) {
    ListUniformState* state = hashmap_string_get(&self->states, id);
    return state != nullptr ? state->content_width : 0.0f;
}

bool list_uniform_scrollbar_engaged(FlListUniformContext* self, FlString id) {
    ListUniformState* state = hashmap_string_get(&self->states, id);
    return state != nullptr && state->scrollbar_engaged;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool list_uniform_viewport(const FlListUniformContext* self, FlString id, ListViewport* out) {
    (void)self;
    FlElementState state = ui_get_element_state(list_container_layout_id(id));
    if (!state.found) {
        return false;
    }
    out->left = state.pos.x;
    out->top = state.pos.y;
    out->right = state.pos.x + state.size.x;
    out->bottom = state.pos.y + state.size.y;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

f64 list_uniform_scroll_offset(FlListUniformContext* self, FlString id) {
    ListUniformState* state = hashmap_string_get(&self->states, id);
    return state != nullptr ? state->scroll_offset : 0.0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void list_uniform_nudge_scroll(FlListUniformContext* self, FlString id, f32 delta_px) {
    ListUniformState* state = hashmap_string_get(&self->states, id);
    if (state == nullptr) {
        return;
    }
    f64 max_scroll = scroll_max(list_geometry_content_height(state->rows), state->container_height);
    f64 next = clamp(state->scroll_offset + (f64)delta_px, 0.0, max_scroll);
    state->scroll_offset = next;
    state->scroll_target = next; // sticky: begin's smooth-step leaves an offset already at its target untouched
}
