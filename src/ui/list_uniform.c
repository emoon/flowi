// A virtualized scrollable list where all items have the same height. Callers render arbitrary content inside
// each item using a begin/end pattern.

#include "list_uniform.h"
#include <core/arena.h>
#include <core/math.h>
#include <core/hashmap.h>
#include <core/log.h>
#include "layout.h"
#include "pixel_snap.h"
#include "scroll_core.h"
#include "ui.h"
#include "ui_private.h"
#include "ui_scroll.h"
#include "wheel_scope.h"
#include <string.h>

#define LIST_UNIFORM_SCROLL_SPEED 12.0f // Smooth scroll lerp factor (per-caller override)
#define LIST_UNIFORM_DEFAULT_GAP 8.0f
#define LIST_UNIFORM_VISIBLE_BUFFER 2 // Extra items to render outside viewport

typedef struct ListUniformState {
    f64 scroll_offset;    // Current scroll position (pixels)
    f64 scroll_target;    // Target for smooth scrolling
    f32 container_height; // Actual container height from previous frame
} ListUniformState;

struct FlListUniformContext {
    FlArena* arena;
    FlUiCtx* ui_ctx;

    hashmap(FlString, ListUniformState) states;

    // Current list being rendered
    FlString current_id;
    u32 current_total_count;
    FlListUniformOpts current_opts;
    ListUniformState* current_state;

    u32 visible_start;
    u32 visible_end;

    f32 viewport_height;
    f64 content_height;
    bool scrollbar_engaged;

    // Current item state (during item_begin/item_end)
    bool in_item;

    // Layout ID for querying container size
    u32 current_layout_id;
};

FlListUniformContext* list_uniform_create(FlArena* arena) {
    FlListUniformContext* ctx = arena_alloc_zero(arena, FlListUniformContext);
    ctx->arena = arena;
    ctx->ui_ctx = ui_get_ctx();

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

// Total content height: every item plus its gap.

static f64 list_content_height(u32 count, f32 item_height, f32 gap) {
    return (f64)count * (f64)(item_height + gap);
}

static void update_scroll_for_selection(FlListUniformContext* self, i32 selected, f32 item_height, f32 gap) {
    f32 item_with_gap = item_height + gap;
    f64 item_top = (f64)selected * (f64)item_with_gap;

    // Always position the selected item at 1/3 from the top of the viewport.
    f32 target_position = self->viewport_height * 0.33f;
    f64 target = item_top - (f64)target_position;

    f64 max_scroll
        = scroll_max(list_content_height(self->current_total_count, item_height, gap), self->viewport_height);

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
    self->scrollbar_engaged = false;
    FlScrollbarDisplayMode mode = self->current_opts.scrollbar_mode;
    if (mode == FlScrollbarDisplayMode_Never) {
        return;
    }

    ListUniformState* state = self->current_state;
    ScrollExtent extent = {
        .viewport = self->viewport_height,
        .content = self->content_height,
        .bar_strip = ui_scroll_overlay_bar_reserve(),
    };
    f64 max_scroll = scroll_max(extent.content, extent.viewport);
    f32 shown = max_scroll > 0.0 ? (f32)(state->scroll_offset / max_scroll) : 0.0f;
    f32 normalized = shown;

    FlSignal signal = ui_scroll_overlay_bar(self->current_layout_id, mode, extent, &normalized);
    self->scrollbar_engaged = signal.hovered || signal.dragging;
    if (normalized != shown) {
        state->scroll_offset = (f64)normalized * max_scroll;
        state->scroll_target = state->scroll_offset;
    }
}

f32 list_uniform_row_px(const FlUiCtx* ctx, f32 row_height) {
    return row_height * ui_scale_or_one(ctx->ui_scale);
}

f32 list_uniform_gap_px(const FlUiCtx* ctx, f32 gap) {
    return ui_scale_and_snap(gap, ui_scale_or_one(ctx->ui_scale));
}

void list_uniform_begin_update(FlListUniformContext* self) {
    self->viewport_height = self->ui_ctx->layout_height;
}

FlListUniformRange list_uniform_begin(FlListUniformContext* self, FlString id, u32 total_count,
                                      FlListUniformOpts opts) {
    self->current_id = id;
    self->current_total_count = total_count;
    self->current_opts = opts;

    if (self->current_opts.gap <= 0.0f) {
        self->current_opts.gap = LIST_UNIFORM_DEFAULT_GAP;
    }

    self->current_state = get_or_create_state(self, id);
    ANALYZER_ASSUME_NONNULL(self->current_state);

    self->in_item = false;

    f32 item_height = list_uniform_row_px(self->ui_ctx, opts.height_hint > 0.0f ? opts.height_hint : 32.0f);
    f32 gap = list_uniform_gap_px(self->ui_ctx, self->current_opts.gap);

    // Until a height has been measured (first frame), size the viewport to ~5 items.
    f32 container_height = self->current_state->container_height;
    if (container_height <= 0.0f) {
        container_height = item_height * 5.0f + gap * 4.0f;
    }
    self->viewport_height = container_height;

    FlLayoutId layout_id = { .name = id, .id = 0 };
    self->current_layout_id = ui_layout_id_to_clay_id(layout_id);

    self->content_height = list_content_height(total_count, item_height, gap);
    f64 max_scroll = scroll_max(self->content_height, self->viewport_height);
    scroll_by_wheel(self, max_scroll);
    wheel_scope_begin(self->ui_ctx, self->current_layout_id, max_scroll > 0.0);

    if (opts.scroll_to >= 0 && opts.scroll_to < (i32)total_count) {
        update_scroll_for_selection(self, opts.scroll_to, item_height, gap);
    }

    self->current_state->scroll_offset
        = scroll_smooth_step(self->current_state->scroll_offset, self->current_state->scroll_target, max_scroll,
                             LIST_UNIFORM_SCROLL_SPEED, self->ui_ctx->delta_time);

    scroll_visible_range(self->current_state->scroll_offset, self->viewport_height, item_height + gap, total_count,
                         LIST_UNIFORM_VISIBLE_BUFFER, &self->visible_start, &self->visible_end);

    f32 item_with_gap = item_height + gap;
    f64 first_item_top = (f64)self->visible_start * (f64)item_with_gap;
    FlVec2 scroll_pos = { 0, (f32)(first_item_top - self->current_state->scroll_offset) };

    bool has_bar = opts.scrollbar_mode != FlScrollbarDisplayMode_Never && max_scroll > 0.0;
    f32 bar_reserve = has_bar ? ui_scroll_overlay_bar_reserve() : 0.0f;

    fl_layout_start(layout_id);
    // Track the container so list_uniform_end can read its measured height from item_state.
    ui_track_element(layout_id);
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_grow(), sizing_grow() },
        .padding = { .right = (u16)bar_reserve },
        .direction = LayoutDirection_TopToBottom,
        .child_gap = self->current_opts.gap,
        .clip = { .vertical = true, .manual_scroll = true, .scroll_pos = scroll_pos },
    });

    return (FlListUniformRange) {
        .start = self->visible_start,
        .end = self->visible_end,
        .total = total_count,
    };
}

void list_uniform_item_begin(FlListUniformContext* self, u32 index) {
    self->in_item = true;

    FlLayoutId item_id = { .name = self->current_id, .id = index + 1 };

    fl_layout_start(item_id);
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_grow(), sizing_fit() },
    });
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
}

bool list_uniform_scrollbar_engaged(const FlListUniformContext* self) {
    return self->scrollbar_engaged;
}

f64 list_uniform_scroll_offset(FlListUniformContext* self, FlString id) {
    ListUniformState* state = hashmap_string_get(&self->states, id);
    return state != nullptr ? state->scroll_offset : 0.0;
}

void list_uniform_nudge_scroll(FlListUniformContext* self, FlString id, f32 delta_px, f32 item_stride,
                               u32 total_count) {
    ListUniformState* state = hashmap_string_get(&self->states, id);
    if (state == nullptr) {
        return;
    }
    // Same extent policy as list_content_height (every row plus its gap == count * stride), clamped against the
    // measured viewport so a fully-scrolled list can't run past its end.
    f64 max_scroll = scroll_max((f64)total_count * (f64)item_stride, state->container_height);
    f64 next = clamp(state->scroll_offset + (f64)delta_px, 0.0, max_scroll);
    state->scroll_offset = next;
    state->scroll_target = next; // sticky: begin's smooth-step leaves an offset already at its target untouched
}
