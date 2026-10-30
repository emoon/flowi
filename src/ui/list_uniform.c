///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Uniform List API Implementation
//
// A virtualized scrollable list where all items have the same height. Callers render arbitrary content inside
// each item using a begin/end pattern.

#include "list_uniform.h"
#include <core/arena.h>
#include <core/math.h>
#include <core/hashmap.h>
#include <core/log.h>
#include "layout.h"
#include "scroll_core.h"
#include "ui.h"
#include "ui_private.h"
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define LIST_UNIFORM_SCROLL_SPEED 12.0f // Smooth scroll lerp factor (per-caller override)
#define LIST_UNIFORM_DEFAULT_GAP 8.0f
#define LIST_UNIFORM_VISIBLE_BUFFER 2 // Extra items to render outside viewport

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ListUniformState {
    f32 scroll_offset;    // Current scroll position (pixels)
    f32 scroll_target;    // Target for smooth scrolling
    f32 container_height; // Actual container height from previous frame
} ListUniformState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

    // Current item state (during item_begin/item_end)
    bool in_item;

    // Layout ID for querying container size
    u32 current_layout_id;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlListUniformContext* list_uniform_create(FlArena* arena) {
    FlListUniformContext* ctx = arena_alloc_zero(arena, FlListUniformContext);
    ctx->arena = arena;
    ctx->ui_ctx = ui_get_ctx();

    hashmap_new(&ctx->states, arena, 16);

    log_info("Uniform list context created");
    return ctx;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void list_uniform_destroy(FlListUniformContext* self) {
    (void)self;
    // Arena-allocated, nothing to free
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static ListUniformState* get_or_create_state(FlListUniformContext* self, FlString id) {
    ListUniformState* state = hashmap_string_get(&self->states, id);
    if (state != nullptr) {
        return state;
    }
    ListUniformState new_state = {
        .scroll_offset = 0.0f,
        .scroll_target = 0.0f,
    };
    return hashmap_string_insert(&self->states, id, new_state);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Total content height - the list's extent policy is every item plus its gap.

static f32 list_content_height(u32 count, f32 item_height, f32 gap) {
    return (f32)count * (item_height + gap);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void update_scroll_for_selection(FlListUniformContext* self, i32 selected, f32 item_height, f32 gap) {
    f32 item_with_gap = item_height + gap;
    f32 item_top = (f32)selected * item_with_gap;

    // Always position the selected item at 1/3 from the top of the viewport.
    f32 target_position = self->viewport_height * 0.33f;
    f32 target = item_top - target_position;

    f32 max_scroll
        = scroll_max(list_content_height(self->current_total_count, item_height, gap), self->viewport_height);

    self->current_state->scroll_target = clamp_f32_impl(target, 0.0f, max_scroll);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void list_uniform_begin_update(FlListUniformContext* self) {
    self->viewport_height = self->ui_ctx->layout_height;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

    f32 item_height = opts.height_hint > 0.0f ? opts.height_hint : 32.0f;

    f32 gap = self->current_opts.gap;

    // Until a height has been measured (first frame), size the viewport to ~5 items.
    f32 container_height = self->current_state->container_height;
    if (container_height <= 0.0f) {
        container_height = item_height * 5.0f + gap * 4.0f;
    }
    self->viewport_height = container_height;

    if (opts.scroll_to >= 0 && opts.scroll_to < (i32)total_count) {
        update_scroll_for_selection(self, opts.scroll_to, item_height, gap);
    }

    f32 max_scroll = scroll_max(list_content_height(total_count, item_height, gap), self->viewport_height);
    self->current_state->scroll_offset
        = scroll_smooth_step(self->current_state->scroll_offset, self->current_state->scroll_target, max_scroll,
                             LIST_UNIFORM_SCROLL_SPEED, self->ui_ctx->delta_time);

    scroll_visible_range(self->current_state->scroll_offset, self->viewport_height, item_height + gap, total_count,
                         LIST_UNIFORM_VISIBLE_BUFFER, &self->visible_start, &self->visible_end);

    FlVec2 scroll_pos = { 0, -self->current_state->scroll_offset };

    f32 item_with_gap = item_height + gap;

    FlLayoutId layout_id = { .name = id, .id = 0 };
    self->current_layout_id = ui_layout_id_to_clay_id(layout_id);

    fl_layout_start(layout_id);
    // Track the container so list_uniform_end can read its measured height from item_state.
    ui_track_element(layout_id);
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_grow(), sizing_grow() },
        .direction = LayoutDirection_TopToBottom,
        .child_gap = gap,
        .clip = { .vertical = true, .manual_scroll = true, .scroll_pos = scroll_pos },
    });

    if (self->visible_start > 0) {
        f32 spacer_height = (f32)self->visible_start * item_with_gap - gap;
        ui_spacer(spacer_height);
    }

    return (FlListUniformRange) {
        .start = self->visible_start,
        .end = self->visible_end,
        .total = total_count,
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void list_uniform_item_begin(FlListUniformContext* self, u32 index) {
    self->in_item = true;

    FlLayoutId item_id = { .name = self->current_id, .id = index + 1 };

    fl_layout_start(item_id);
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_grow(), sizing_fit() },
    });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void list_uniform_item_end(FlListUniformContext* self) {
    if (!self->in_item) {
        return;
    }
    self->in_item = false;
    fl_layout_end();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void list_uniform_end(FlListUniformContext* self) {
    fl_layout_end();

    // Read the measured container height from item_state and store it for next frame.
    FlElementState state = ui_get_element_state_by_id(self->current_layout_id);
    if (state.found && state.size.y > 0.0f) {
        self->current_state->container_height = state.size.y;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scroll introspection / drive (see header).

f32 list_uniform_scroll_offset(FlListUniformContext* self, FlString id) {
    ListUniformState* state = hashmap_string_get(&self->states, id);
    return state != nullptr ? state->scroll_offset : 0.0f;
}

void list_uniform_nudge_scroll(FlListUniformContext* self, FlString id, f32 delta_px, f32 item_stride,
                               u32 total_count) {
    ListUniformState* state = hashmap_string_get(&self->states, id);
    if (state == nullptr) {
        return;
    }
    // Same extent policy as list_content_height (every row plus its gap == count * stride), clamped against the
    // measured viewport so a fully-scrolled list can't run past its end.
    f32 max_scroll = scroll_max((f32)total_count * item_stride, state->container_height);
    f32 next = clamp_f32_impl(state->scroll_offset + delta_px, 0.0f, max_scroll);
    state->scroll_offset = next;
    state->scroll_target = next; // sticky: begin's smooth-step leaves an offset already at its target untouched
}
