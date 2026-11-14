#include "ui_scroll.h"
#include "scroll_core.h"
#include <core/arena.h>
#include <core/assert.h>
#include <core/math.h>
#include <core/hashmap.h>
#include <core/log.h>
#include <core/sprintf.h>
#include "layout.h"
#include "ui.h"
#include "ui_hit_test.h"
#include "ui_private.h"
#include "ui_tokens.h"
#include "ui_variant_helpers.h"
#include "ui_widget_internal.h"

// Smooth scroll lerp factor: the shared scroll_core default (higher = faster)
#define SCROLL_SMOOTH_LERP_SPEED SCROLL_CORE_DEFAULT_SMOOTH_SPEED
#define SCROLL_DEFAULT_SPEED 30.0f     // Default pixels per wheel tick
#define SCROLL_DEFAULT_MARGIN 20.0f    // Default margin when scrolling to widget
#define SCROLLBAR_WIDTH 12.0f          // Scrollbar track width
#define SCROLLBAR_MIN_THUMB_SIZE 40.0f // Minimum scrollbar thumb height
#define SCROLLBAR_PADDING 2.0f         // Padding around scrollbar

// Test-only: makes state creation report allocation failure so the begin-path fallback can be exercised.
// Only affects scroll areas without existing state; already-created states are returned as usual.
static bool g_force_state_alloc_failure = false;

void ui_scroll_test_force_state_alloc_failure(bool enable) {
    g_force_state_alloc_failure = enable;
}

static ScrollAreaState* get_or_create_scroll_state(u32 element_id) {
    FlUiCtx* ctx = ui_get_ctx();
    FL_VALIDATE_RET(ctx != nullptr, nullptr);

    ScrollAreaState* state = hashmap_get(&ctx->shared->scroll_states, element_id);
    if (state != nullptr) {
        state->last_frame = ctx->frame_counter;
        return state;
    }

    if (g_force_state_alloc_failure) {
        return nullptr;
    }

    ScrollAreaState new_state = {
        .scroll_offset = { 0, 0 },
        .scroll_target = { 0, 0 },
        .content_size = { 0, 0 },
        .viewport_size = { 0, 0 },
        .last_frame = ctx->frame_counter,
        .is_hovered = false,
    };

    hashmap_insert(&ctx->shared->scroll_states, element_id, new_state);
    return hashmap_get(&ctx->shared->scroll_states, element_id);
}

static void update_smooth_scroll(ScrollAreaState* state, FlVec2 max_scroll, f32 delta_time) {
    state->scroll_offset.x = scroll_smooth_step(state->scroll_offset.x, state->scroll_target.x, max_scroll.x,
                                                SCROLL_SMOOTH_LERP_SPEED, delta_time);
    state->scroll_offset.y = scroll_smooth_step(state->scroll_offset.y, state->scroll_target.y, max_scroll.y,
                                                SCROLL_SMOOTH_LERP_SPEED, delta_time);
}

static FlVec2 calculate_max_scroll(FlVec2 content_size, FlVec2 viewport_size) {
    return (FlVec2) {
        .x = scroll_max(content_size.x, viewport_size.x),
        .y = scroll_max(content_size.y, viewport_size.y),
    };
}

// Containers must be registered with ui_track_element for get_element_bounds to find them (scroll areas do so
// in _begin).

static bool get_element_bounds(u32 element_id, FlVec2* out_pos, FlVec2* out_size) {
    FlElementState state = ui_get_element_state_by_id(element_id);
    if (!state.found) {
        return false;
    }
    *out_pos = state.pos;
    *out_size = state.size;
    return true;
}

// Pointer is over the container's clip-cut rect from the previous frame's hit test. A scroll area not yet in
// item_state (first frame) is not hovered.

static bool scroll_area_hovered(FlUiCtx* ctx, u32 element_id) {
    ItemState* item = hashmap_get(&ctx->shared->item_state, element_id);
    if (item == nullptr || !item->state.found) {
        return false;
    }

    return ui_item_hit_contains(item, (Vec2) { ctx->input_state.mouse.x, ctx->input_state.mouse.y });
}

static bool scroll_area_is_ancestor(FlUiCtx* ctx, u32 ancestor_id, u32 area_id) {
    u32 parent_id = area_id;
    for_count(i, SCROLL_AREA_STACK_MAX_DEPTH) {
        ScrollAreaState* state = hashmap_get(&ctx->shared->scroll_states, parent_id);
        if (state == nullptr || state->parent_id == 0) {
            return false;
        }
        parent_id = state->parent_id;
        if (parent_id == ancestor_id) {
            return true;
        }
    }
    return false;
}

// The wheel goes to the innermost hovered scroll area only: an area takes it when hovered and no scroll area nested
// inside it is also hovered.

static bool scroll_area_has_hovered_descendant(FlUiCtx* ctx, u32 element_id) {
    for_each_hashmap(&ctx->shared->scroll_states, other_id, other) {
        (void)other;
        if (scroll_area_hovered(ctx, *other_id) && scroll_area_is_ancestor(ctx, element_id, *other_id)) {
            return true;
        }
    }
    return false;
}

static bool scroll_area_owns_wheel(FlUiCtx* ctx, u32 element_id) {
    return scroll_area_hovered(ctx, element_id) && !scroll_area_has_hovered_descendant(ctx, element_id);
}

static void handle_mouse_wheel(ScrollAreaState* state, FlVec2 max_scroll, f32 scroll_speed, bool smooth_scroll) {
    FlUiCtx* ctx = ui_get_ctx();
    if (ctx == nullptr) {
        return;
    }

    FlVec2 wheel = ui_get_mouse_wheel();
    if (wheel.x == 0.0f && wheel.y == 0.0f) {
        return;
    }

    // Apply the scroll-speed multiplier. wheel.y is positive when scrolling up, and scrolling content up
    // decreases the offset.
    FlVec2 scroll_delta = {
        .x = -wheel.x * scroll_speed,
        .y = -wheel.y * scroll_speed,
    };

    if (smooth_scroll) {
        state->scroll_target.x += scroll_delta.x;
        state->scroll_target.y += scroll_delta.y;

        state->scroll_target.x = clamp(state->scroll_target.x, 0.0f, max_scroll.x);
        state->scroll_target.y = clamp(state->scroll_target.y, 0.0f, max_scroll.y);
    } else {
        state->scroll_offset.x += scroll_delta.x;
        state->scroll_offset.y += scroll_delta.y;

        state->scroll_offset.x = clamp(state->scroll_offset.x, 0.0f, max_scroll.x);
        state->scroll_offset.y = clamp(state->scroll_offset.y, 0.0f, max_scroll.y);

        state->scroll_target = state->scroll_offset;
    }
}

typedef struct ScrollbarThumb {
    f32 offset;      // Track space before the thumb
    f32 size;        // Thumb length along the track
    f32 space_after; // Track space after the thumb
    FlColor color;
} ScrollbarThumb;

static f32 positive_or(f32 value, f32 fallback) {
    return value > 0.0f ? value : fallback;
}

static FlLayoutId scrollbar_layout_id(const FlScrollbarConfig* cfg, const float* value) {
    if (cfg->widget.id_override.length > 0) {
        return (FlLayoutId) { .name = cfg->widget.id_override };
    }
    // Auto-generate the ID from the bound value pointer, a stable per-instance key.
    return ID_INT("_scrollbar", (int)(uintptr_t)value);
}

static FlItemFlags scrollbar_item_flags(const FlScrollbarConfig* cfg) {
    FlItemFlags flags
        = FlItemFlags_clickable | FlItemFlags_draggable | FlItemFlags_hot_animation | FlItemFlags_active_animation;
    // The scrollbar takes focus only on an explicit FlFocusable_Yes, never on Default.
    if (cfg->widget.focusable == FlFocusable_Yes) {
        flags |= FlItemFlags_focusable | FlItemFlags_focus_animation;
    }
    return flags;
}

static f32 vec2_along(FlVec2 v, bool is_vertical) {
    return is_vertical ? v.y : v.x;
}

static f32 scrollbar_track_length(const ItemState* item, bool is_vertical) {
    return positive_or(vec2_along(item->state.size, is_vertical), 100.0f); // Fallback for first frame
}

// The thumb is grabbed while dragged, or on the click that lands on the track.
static bool scrollbar_grabbed(FlSignal signal) {
    return signal.dragging || (signal.clicked && signal.hovered);
}

// Thumb position in [0, 1] that centers the thumb on the mouse. usable_track is 0 when the thumb fills the track.
static f32 scrollbar_normalized_at_mouse(f32 mouse_pos, f32 track_start, f32 thumb_size, f32 usable_track) {
    f32 relative_pos = mouse_pos - track_start - (thumb_size / 2.0f);
    return usable_track > 0.0f ? clamp(relative_pos / usable_track, 0.0f, 1.0f) : 0.0f;
}

// Sizing from an extent across the track and one along it.
static FlLayoutSizing scrollbar_sizing(bool is_vertical, FlLayoutSizingAxis across, FlLayoutSizingAxis along) {
    return is_vertical ? (FlLayoutSizing) { .width = across, .height = along }
                       : (FlLayoutSizing) { .width = along, .height = across };
}

static void scrollbar_spacer(bool is_vertical, f32 length) {
    if (length > 0.0f) {
        ui_layout_area(.sizing = scrollbar_sizing(is_vertical, sizing_grow(0), sizing_fixed(length))) {}
    }
}

static void scrollbar_emit(FlLayoutId id, bool is_vertical, f32 width, ScrollbarThumb thumb) {
    u16 corner_rad = (u16)(width / 2.0f);
    FlLayoutDirection direction = is_vertical ? LayoutDirection_TopToBottom : LayoutDirection_LeftToRight;

    ui_layout_area_id(id, .sizing = scrollbar_sizing(is_vertical, sizing_fixed(width), sizing_grow(0)),
                      .direction = direction, .color = UI_HIT_TEST_FILL(1)) {
        scrollbar_spacer(is_vertical, thumb.offset);

        ui_layout_area(.sizing = scrollbar_sizing(is_vertical, sizing_grow(0), sizing_fixed(thumb.size)),
                       .color = thumb.color, .radius = corner_radius(corner_rad)) {}

        scrollbar_spacer(is_vertical, thumb.space_after);
    }
}

FlSignal ui_scrollbar_impl(float* value, FlScrollbarConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();
    FL_VALIDATE_RET(ctx != nullptr, (FlSignal) { 0 });
    FL_VALIDATE_RET(value != nullptr, (FlSignal) { 0 });

    f32 scrollbar_width = positive_or(cfg.width, (f32)FlSpacing_SM);
    f32 min_thumb_size = positive_or(cfg.min_thumb_size, SCROLLBAR_MIN_THUMB_SIZE);
    f32 viewport_ratio = positive_or(cfg.viewport_ratio, 0.2f);
    bool is_vertical = (cfg.orientation == FlScrollbarOrientation_Vertical);

    *value = clamp(*value, cfg.min, cfg.max);

    FlLayoutId scrollbar_id = scrollbar_layout_id(&cfg, value);
    FlSignal signal = ui_item_behavior(scrollbar_id, scrollbar_item_flags(&cfg), cfg.widget);

    ItemState* item = hashmap_get(&ctx->shared->item_state, ui_layout_id_to_clay_id(scrollbar_id));
    if (item == nullptr) {
        return signal;
    }

    f32 track_length = scrollbar_track_length(item, is_vertical);
    f32 thumb_size = max(min_thumb_size, track_length * viewport_ratio);
    f32 usable_track = track_length - thumb_size;

    // Current normalized position [0, 1]
    f32 range = cfg.max - cfg.min;
    f32 normalized = range > 0.0f ? (*value - cfg.min) / range : 0.0f;

    if (scrollbar_grabbed(signal)) {
        FlVec2 mouse = { ctx->input_state.mouse.x, ctx->input_state.mouse.y };
        normalized = scrollbar_normalized_at_mouse(vec2_along(mouse, is_vertical),
                                                   vec2_along(item->state.pos, is_vertical), thumb_size, usable_track);
        *value = clamp(cfg.min + normalized * range, cfg.min, cfg.max);
    }

    f32 thumb_offset = normalized * usable_track;
    ScrollbarThumb thumb = {
        .offset = max(0.0f, thumb_offset),
        .size = thumb_size,
        .space_after = max(0.0f, usable_track - thumb_offset),
        .color = ui_apply_state_color(ui_get_theme_colors(ctx)->text.secondary, item),
    };
    scrollbar_emit(scrollbar_id, is_vertical, scrollbar_width, thumb);

    return signal;
}

static void scroll_area_scrollbar_widget(ScrollAreaState* state, f32 viewport_ratio, f32 max_scroll,
                                         u32 scroll_area_id) {
    FlSignal scrollbar_signal = ui_scrollbar(
        &state->scroll_target.y, .orientation = FlScrollbarOrientation_Vertical, .min = 0.0f, .max = max_scroll,
        .viewport_ratio = viewport_ratio, .width = SCROLLBAR_WIDTH - SCROLLBAR_PADDING * 2,
        .widget = { .id_override = fl_sprintf_arena(ui_get_ctx()->render_frame_allocator, "_sb_%u", scroll_area_id) });

    // If user dragged scrollbar, sync offset immediately for responsiveness
    if (scrollbar_signal.dragging || scrollbar_signal.clicked) {
        state->scroll_offset.y = state->scroll_target.y;
    }
}

static void render_vertical_scrollbar(ScrollAreaState* state, FlScrollbarDisplayMode scrollbar_mode,
                                      u32 scroll_area_id) {
    FlVec2 viewport_size = state->viewport_size;
    FlVec2 content_size = state->content_size;
    if (scrollbar_mode == FlScrollbarDisplayMode_Never || content_size.y <= viewport_size.y) {
        return;
    }

    if (scrollbar_mode == FlScrollbarDisplayMode_Auto && !state->is_hovered) {
        return;
    }

    f32 max_scroll = content_size.y - viewport_size.y;
    f32 viewport_ratio = viewport_size.y / content_size.y;
    u16 padding = (u16)SCROLLBAR_PADDING;

    // clang-format off
    ui_floating_area_id(ID_INT("_scrollbar_float", (int)scroll_area_id),
        .attach_to = FlFloatingAttachTo_Parent,
        .parent_attach = FlFloatingAttachPoint_RightTop,
        .element_attach = FlFloatingAttachPoint_RightTop,
        .pointer_capture = FlFloatingPointerCapture_Passthrough,
        .sizing = { sizing_fixed(SCROLLBAR_WIDTH), sizing_fixed(viewport_size.y) })
    {
        ui_layout_area(
            .sizing = { sizing_grow(0), sizing_grow(0) },
            .padding = { .left = padding, .right = padding, .top = padding, .bottom = padding })
        {
            scroll_area_scrollbar_widget(state, viewport_ratio, max_scroll, scroll_area_id);
        }
    }
    // clang-format on
}

// Pushes one entry. Past SCROLL_AREA_STACK_MAX_DEPTH the area is counted in scroll_area_overflow instead, so
// the matching fl_scroll_area_end retires it without popping an enclosing area's entry.
static void scroll_stack_push(FlUiCtx* ctx, ScrollAreaStackEntry entry) {
    if (ctx->scroll_area_stack == nullptr) {
        return;
    }
    if (ctx->scroll_area_stack_depth >= SCROLL_AREA_STACK_MAX_DEPTH) {
        static bool s_overflow_logged;
        if (!s_overflow_logged) {
            s_overflow_logged = true;
            log_error("scroll area: more than %d nested scroll areas - inner areas do not track scrolling",
                      SCROLL_AREA_STACK_MAX_DEPTH);
        }
        ctx->scroll_area_overflow++;
        return;
    }
    ctx->scroll_area_stack[ctx->scroll_area_stack_depth++] = entry;
}

void fl_scroll_area_begin(FlLayoutId id, FlScrollAreaConfig config) {
    FlUiCtx* ctx = ui_get_ctx();
    FL_VALIDATE(ctx != nullptr);

    u32 element_id = ui_layout_id_to_clay_id(id);

    ScrollAreaState* state = get_or_create_scroll_state(element_id);
    if (state == nullptr) {
        // Fallback: render without scroll. Still push a stack entry with a nullptr state (every stack consumer treats
        // it as "no scroll tracking") so the paired fl_scroll_area_end pops this entry.
        scroll_stack_push(ctx, (ScrollAreaStackEntry) {
                                   .scroll_area_id = element_id,
                                   .state = nullptr,
                                   .viewport_pos = { 0, 0 },
                                   .viewport_size = { 0, 0 },
                                   .config = config,
                               });

        fl_layout_start(id);
        fl_layout_configure((FlLayoutConfig) {
            .sizing = config.sizing,
            .padding = config.padding,
            .child_gap = config.child_gap,
            .radius = config.corner_radius,
            .direction = config.direction,
            .color = config.background_color,
        });
        return;
    }

    // Calculate max scroll based on previous frame's content/viewport sizes
    FlVec2 max_scroll = calculate_max_scroll(state->content_size, state->viewport_size);

    // Wheel input goes to the innermost scroll area under the cursor.
    f32 scroll_speed = positive_or(config.scroll_speed, SCROLL_DEFAULT_SPEED);
    bool smooth = config.smooth_scroll;
    if (scroll_area_owns_wheel(ctx, element_id)) {
        handle_mouse_wheel(state, max_scroll, scroll_speed, smooth);
    }

    update_smooth_scroll(state, max_scroll, ctx->delta_time);

    state->parent_id = ctx->scroll_area_stack_depth > 0
                           ? ctx->scroll_area_stack[ctx->scroll_area_stack_depth - 1].scroll_area_id
                           : 0;

    scroll_stack_push(ctx, (ScrollAreaStackEntry) {
                               .scroll_area_id = element_id,
                               .state = state,
                               .viewport_pos = { 0, 0 },  // Will be updated after layout
                               .viewport_size = { 0, 0 }, // Will be updated after layout
                               .config = config,
                           });

    fl_layout_start(id);

    // Track the container in item_state so ui_is_widget_visible / ui_scroll_to_widget can read its screen rect.
    ui_track_element(id);

    // scroll_pos is negative: the content is offset up/left.
    FlVec2 scroll_pos = {
        .x = -state->scroll_offset.x,
        .y = -state->scroll_offset.y,
    };

    FlLayoutClip clip = {
        .horizontal = config.direction == FlLayoutDirection_LeftToRight,
        .vertical = config.direction == FlLayoutDirection_TopToBottom,
        .manual_scroll = true,
        .scroll_pos = scroll_pos,
    };

    // Add extra right padding when scrollbar is shown to prevent content overlapping
    FlLayoutPadding padding = config.padding;
    if (config.show_scrollbar) {
        u16 scrollbar_space = (u16)(SCROLLBAR_WIDTH + 4.0f);
        padding.right = (u16)(padding.right + scrollbar_space);
    }

    FlLayoutConfig layout_config = {
        .sizing = config.sizing,
        .padding = padding,
        .child_gap = config.child_gap,
        .radius = config.corner_radius,
        .direction = config.direction,
        .color = config.background_color,
        .clip = clip,
    };

    fl_layout_configure(layout_config);
}

// Retires the innermost open area: an overflowed area is uncounted, otherwise the top stack entry is popped. Returns
// the popped entry, or nullptr when there is none.
static ScrollAreaStackEntry* scroll_stack_pop(FlUiCtx* ctx) {
    if (ctx->scroll_area_overflow > 0) {
        ctx->scroll_area_overflow--;
        return nullptr;
    }
    if (ctx->scroll_area_stack_depth == 0) {
        return nullptr;
    }
    ctx->scroll_area_stack_depth--;
    return &ctx->scroll_area_stack[ctx->scroll_area_stack_depth];
}

// The extent query reads the previous frame's layout data.
static void scroll_area_update_extent(ScrollAreaState* state, u32 scroll_area_id) {
    FlScrollContainerExtent extent = ui_get_scroll_container_extent(scroll_area_id);
    if (extent.found) {
        state->viewport_size = extent.viewport_size;
        state->content_size = extent.content_extent;
        return;
    }

    // Fallback to the container's element rect (size only)
    FlVec2 pos, size;
    if (get_element_bounds(scroll_area_id, &pos, &size)) {
        state->viewport_size = size;
    }
}

void fl_scroll_area_end(void) {
    FlUiCtx* ctx = ui_get_ctx();
    ScrollAreaStackEntry* entry = ctx != nullptr ? scroll_stack_pop(ctx) : nullptr;

    if (entry != nullptr && entry->state != nullptr) {
        scroll_area_update_extent(entry->state, entry->scroll_area_id);

        if (entry->config.show_scrollbar) {
            // If show_scrollbar is true but mode is Never (default 0), treat as Always
            FlScrollbarDisplayMode mode = entry->config.scrollbar_mode;
            if (mode == FlScrollbarDisplayMode_Never) {
                mode = FlScrollbarDisplayMode_Always;
            }
            render_vertical_scrollbar(entry->state, mode, entry->scroll_area_id);
        }
    }

    fl_layout_end();
}

// The scroll area that contains the widget, or 0 when it is not in one.
static u32 widget_scroll_area_id(FlUiCtx* ctx, u32 widget_clay_id) {
    WidgetScrollInfo* info = hashmap_get(&ctx->shared->widget_scroll_map, widget_clay_id);
    return info != nullptr ? info->scroll_area_id : 0;
}

bool ui_is_widget_visible(FlLayoutId widget_id) {
    FlUiCtx* ctx = ui_get_ctx();
    if (ctx == nullptr) {
        return true;
    }

    u32 id = ui_layout_id_to_clay_id(widget_id);
    u32 scroll_area_id = widget_scroll_area_id(ctx, id);
    if (scroll_area_id == 0) {
        return true; // Not in a scroll area = always visible
    }

    FlVec2 viewport_pos, viewport_size;
    if (!get_element_bounds(scroll_area_id, &viewport_pos, &viewport_size)) {
        return true; // Can't determine the viewport; assume visible
    }

    // A widget absent from item_state this frame was culled out of the viewport (Clay emits no render command
    // for it), which counts as not visible.
    FlVec2 widget_pos, widget_size;
    if (!get_element_bounds(id, &widget_pos, &widget_size)) {
        return false;
    }

    // Check if widget bounds intersect visible viewport (in screen space)
    f32 widget_top = widget_pos.y;
    f32 widget_bottom = widget_top + widget_size.y;
    f32 viewport_top = viewport_pos.y;
    f32 viewport_bottom = viewport_top + viewport_size.y;

    return (widget_bottom > viewport_top && widget_top < viewport_bottom);
}

void ui_scroll_to_widget(FlLayoutId widget_id) {
    ui_scroll_to_widget_ex(widget_id, (FlScrollToOptions) { .margin = SCROLL_DEFAULT_MARGIN });
}

// Vertical extents in content space, plus the scroll position the area is heading for.
typedef struct ScrollToGeometry {
    f32 widget_top;
    f32 widget_height;
    f32 visible_top; // Current scroll offset
    f32 viewport_height;
    f32 current_target;
} ScrollToGeometry;

static f32 scroll_target_for_align(FlScrollAlignment align, f32 margin, ScrollToGeometry g) {
    f32 widget_bottom = g.widget_top + g.widget_height;
    f32 visible_bottom = g.visible_top + g.viewport_height;

    switch (align) {
        case FlScrollAlign_Nearest:
            if (g.widget_top - margin < g.visible_top) {
                return g.widget_top - margin;
            }
            if (widget_bottom + margin > visible_bottom) {
                return widget_bottom + margin - g.viewport_height;
            }
            break;
        case FlScrollAlign_Top:
            return g.widget_top - margin;
        case FlScrollAlign_Center:
            return g.widget_top + g.widget_height / 2.0f - g.viewport_height / 2.0f;
        case FlScrollAlign_Bottom:
            return widget_bottom + margin - g.viewport_height;
    }
    return g.current_target;
}

// Takes the raw Clay id; used by ui_scroll_auto_to_focused.
static void scroll_to_widget_internal(u32 widget_clay_id, FlScrollToOptions opts) {
    FlUiCtx* ctx = ui_get_ctx();
    if (ctx == nullptr) {
        return;
    }

    u32 scroll_area_id = widget_scroll_area_id(ctx, widget_clay_id);
    if (scroll_area_id == 0) {
        return; // Not in a scroll area
    }

    ScrollAreaState* scroll = hashmap_get(&ctx->shared->scroll_states, scroll_area_id);
    if (scroll == nullptr) {
        return;
    }

    // item_state only contains elements emitted this frame; fully culled widgets have no bounds to scroll to.
    FlVec2 widget_pos, widget_size;
    if (!get_element_bounds(widget_clay_id, &widget_pos, &widget_size)) {
        return;
    }

    // Get scroll area viewport rect from item_state (screen space)
    FlVec2 viewport_pos, viewport_size;
    if (!get_element_bounds(scroll_area_id, &viewport_pos, &viewport_size)) {
        return; // Can't determine viewport position
    }

    // Widget position in content = (widget.y - viewport.y) + scroll_offset, where both y values are screen space
    ScrollToGeometry geometry = {
        .widget_top = (widget_pos.y - viewport_pos.y) + scroll->scroll_offset.y,
        .widget_height = widget_size.y,
        .visible_top = scroll->scroll_offset.y,
        .viewport_height = viewport_size.y,
        .current_target = scroll->scroll_target.y,
    };
    f32 margin = positive_or(opts.margin, SCROLL_DEFAULT_MARGIN);
    f32 new_scroll_y = scroll_target_for_align(opts.align, margin, geometry);

    f32 max_scroll_y = scroll->content_size.y - viewport_size.y;
    new_scroll_y = clamp(new_scroll_y, 0.0f, max(0.0f, max_scroll_y));

    scroll->scroll_target.y = new_scroll_y;
    if (opts.immediate) {
        scroll->scroll_offset.y = new_scroll_y;
    }
}

void ui_scroll_to_widget_ex(FlLayoutId widget_id, FlScrollToOptions opts) {
    u32 id = ui_layout_id_to_clay_id(widget_id);
    scroll_to_widget_internal(id, opts);
}

void ui_scroll_auto_to_focused(void) {
    FlUiCtx* ctx = ui_get_ctx();
    if (ctx == nullptr) {
        return;
    }

    // Only scroll if focus changed this frame via navigation (keyboard/gamepad, not mouse click)
    // The should_scroll_to_focused flag is set by ui_navigate_tab_order and spatial navigation
    if (!ctx->should_scroll_to_focused) {
        return;
    }

    if (ctx->focused_id == 0) {
        return;
    }

    // ctx->focused_id is the Clay element hash, not an FlLayoutId.
    scroll_to_widget_internal(ctx->focused_id, (FlScrollToOptions) { .margin = SCROLL_DEFAULT_MARGIN });
}
