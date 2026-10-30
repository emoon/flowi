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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Constants

// Smooth scroll lerp factor - shared scroll_core default (higher = faster)
#define SCROLL_SMOOTH_LERP_SPEED SCROLL_CORE_DEFAULT_SMOOTH_SPEED
#define SCROLL_DEFAULT_SPEED 30.0f     // Default pixels per wheel tick
#define SCROLL_DEFAULT_MARGIN 20.0f    // Default margin when scrolling to widget
#define SCROLLBAR_WIDTH 12.0f          // Scrollbar track width
#define SCROLLBAR_MIN_THUMB_SIZE 40.0f // Minimum scrollbar thumb height
#define SCROLLBAR_PADDING 2.0f         // Padding around scrollbar

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void update_smooth_scroll(ScrollAreaState* state, FlVec2 max_scroll, f32 delta_time) {
    state->scroll_offset.x = scroll_smooth_step(state->scroll_offset.x, state->scroll_target.x, max_scroll.x,
                                                SCROLL_SMOOTH_LERP_SPEED, delta_time);
    state->scroll_offset.y = scroll_smooth_step(state->scroll_offset.y, state->scroll_target.y, max_scroll.y,
                                                SCROLL_SMOOTH_LERP_SPEED, delta_time);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlVec2 calculate_max_scroll(FlVec2 content_size, FlVec2 viewport_size) {
    return (FlVec2) {
        .x = scroll_max(content_size.x, viewport_size.x),
        .y = scroll_max(content_size.y, viewport_size.y),
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pointer is over the container's clip-cut rect from the previous frame's hit test. A scroll area not yet in
// item_state (first frame) is not hovered.

static bool scroll_area_hovered(FlUiCtx* ctx, u32 element_id) {
    ItemState* item = hashmap_get(&ctx->shared->item_state, element_id);
    if (item == nullptr || !item->state.found) {
        return false;
    }

    return ui_item_hit_contains(item, (Vec2) { ctx->input_state.mouse.x, ctx->input_state.mouse.y });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The wheel goes to the innermost hovered scroll area only: an area takes it when hovered and no scroll area nested
// inside it is also hovered.

static bool scroll_area_owns_wheel(FlUiCtx* ctx, u32 element_id) {
    if (!scroll_area_hovered(ctx, element_id)) {
        return false;
    }

    for_each_hashmap(&ctx->shared->scroll_states, other_id, other) {
        if (other->parent_id != 0 && scroll_area_hovered(ctx, *other_id)
            && scroll_area_is_ancestor(ctx, element_id, *other_id)) {
            return false;
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

        state->scroll_target.x = clamp_f32_impl(state->scroll_target.x, 0.0f, max_scroll.x);
        state->scroll_target.y = clamp_f32_impl(state->scroll_target.y, 0.0f, max_scroll.y);
    } else {
        state->scroll_offset.x += scroll_delta.x;
        state->scroll_offset.y += scroll_delta.y;

        state->scroll_offset.x = clamp_f32_impl(state->scroll_offset.x, 0.0f, max_scroll.x);
        state->scroll_offset.y = clamp_f32_impl(state->scroll_offset.y, 0.0f, max_scroll.y);

        state->scroll_target = state->scroll_offset;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlSignal ui_scrollbar_impl(float* value, FlScrollbarConfig cfg) {
    FlUiCtx* ctx = ui_get_ctx();
    FL_VALIDATE_RET(ctx != nullptr, (FlSignal) { 0 });
    FL_VALIDATE_RET(value != nullptr, (FlSignal) { 0 });

    f32 scrollbar_width = cfg.width > 0.0f ? cfg.width : (f32)FlSpacing_SM;
    f32 min_thumb_size = cfg.min_thumb_size > 0.0f ? cfg.min_thumb_size : SCROLLBAR_MIN_THUMB_SIZE;
    f32 viewport_ratio = cfg.viewport_ratio > 0.0f ? cfg.viewport_ratio : 0.2f;
    bool is_vertical = (cfg.orientation == FlScrollbarOrientation_Vertical);

    *value = clamp_f32_impl(*value, cfg.min, cfg.max);

    FlLayoutId scrollbar_id;
    if (cfg.widget.id_override.length > 0) {
        scrollbar_id = (FlLayoutId) { .name = cfg.widget.id_override };
    } else {
        // Auto-generate the ID from the bound value pointer, a stable per-instance key.
        scrollbar_id = ID_INT("_scrollbar", (int)(uintptr_t)value);
    }

    FlItemFlags flags
        = FlItemFlags_clickable | FlItemFlags_draggable | FlItemFlags_hot_animation | FlItemFlags_active_animation;
    // The scrollbar takes focus only on an explicit FlFocusable_Yes, never on Default.
    if (cfg.widget.focusable == FlFocusable_Yes) {
        flags |= FlItemFlags_focusable | FlItemFlags_focus_animation;
    }

    FlSignal signal = ui_item_behavior(scrollbar_id, flags, cfg.widget);

    ItemState* item = hashmap_get(&ctx->shared->item_state, ui_layout_id_to_clay_id(scrollbar_id));
    if (item == nullptr) {
        return signal;
    }

    f32 track_length = is_vertical ? item->state.size.y : item->state.size.x;
    if (track_length <= 0.0f) {
        track_length = 100.0f; // Fallback for first frame
    }

    f32 thumb_size = max_f32_impl(min_thumb_size, track_length * viewport_ratio);
    f32 usable_track = track_length - thumb_size;

    // Current normalized position [0, 1]
    f32 range = cfg.max - cfg.min;
    f32 normalized = range > 0.0f ? (*value - cfg.min) / range : 0.0f;

    bool value_modified = false;

    if (signal.dragging || (signal.clicked && signal.hovered)) {
        f32 mouse_pos;
        f32 track_start;
        if (is_vertical) {
            mouse_pos = ctx->input_state.mouse.y;
            track_start = item->state.pos.y;
        } else {
            mouse_pos = ctx->input_state.mouse.x;
            track_start = item->state.pos.x;
        }

        // Center the thumb on the mouse position. usable_track is 0 when the thumb fills the track.
        f32 relative_pos = mouse_pos - track_start - (thumb_size / 2.0f);
        normalized = usable_track > 0.0f ? clamp_f32_impl(relative_pos / usable_track, 0.0f, 1.0f) : 0.0f;
        value_modified = true;
    }

    if (value_modified) {
        *value = cfg.min + normalized * range;
        *value = clamp_f32_impl(*value, cfg.min, cfg.max);
    }

    f32 thumb_offset = normalized * usable_track;
    f32 space_after = usable_track - thumb_offset;
    if (thumb_offset < 0.0f) {
        thumb_offset = 0.0f;
    }
    if (space_after < 0.0f) {
        space_after = 0.0f;
    }

    const FlThemeColors* colors = ui_get_theme_colors(ctx);
    FlColor base_thumb = colors->text.secondary;
    FlColor thumb_color = ui_apply_state_color(base_thumb, item);

    f32 corner_rad = scrollbar_width / 2.0f;

    if (is_vertical) {
        // clang-format off
        ui_layout_area_id(scrollbar_id,
            .sizing = { sizing_fixed(scrollbar_width), sizing_grow(0) },
            .direction = LayoutDirection_TopToBottom,
            .color = UI_HIT_TEST_FILL(1))
        {
            if (thumb_offset > 0.0f) {
                ui_layout_area(.sizing = { sizing_grow(0), sizing_fixed(thumb_offset) }) {}
            }

            ui_layout_area(
                .sizing = { sizing_grow(0), sizing_fixed(thumb_size) },
                .color = thumb_color,
                .radius = corner_radius((u16)corner_rad)) {}

            if (space_after > 0.0f) {
                ui_layout_area(.sizing = { sizing_grow(0), sizing_fixed(space_after) }) {}
            }
        }
        // clang-format on
    } else {
        // clang-format off
        ui_layout_area_id(scrollbar_id,
            .sizing = { sizing_grow(0), sizing_fixed(scrollbar_width) },
            .direction = LayoutDirection_LeftToRight,
            .color = UI_HIT_TEST_FILL(1))
        {
            if (thumb_offset > 0.0f) {
                ui_layout_area(.sizing = { sizing_fixed(thumb_offset), sizing_grow(0) }) {}
            }

            ui_layout_area(
                .sizing = { sizing_fixed(thumb_size), sizing_grow(0) },
                .color = thumb_color,
                .radius = corner_radius((u16)corner_rad)) {}

            if (space_after > 0.0f) {
                ui_layout_area(.sizing = { sizing_fixed(space_after), sizing_grow(0) }) {}
            }
        }
        // clang-format on
    }

    return signal;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void render_vertical_scrollbar(ScrollAreaState* state, FlVec2 viewport_size, FlVec2 content_size,
                                      FlScrollbarDisplayMode scrollbar_mode, u32 scroll_area_id) {
    if (scrollbar_mode == FlScrollbarDisplayMode_Never || content_size.y <= viewport_size.y) {
        return;
    }

    if (scrollbar_mode == FlScrollbarDisplayMode_Auto && !state->is_hovered) {
        return;
    }

    f32 max_scroll = content_size.y - viewport_size.y;
    f32 viewport_ratio = viewport_size.y / content_size.y;

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
            .padding = { .left = (u16)SCROLLBAR_PADDING, .right = (u16)SCROLLBAR_PADDING,
                         .top = (u16)SCROLLBAR_PADDING, .bottom = (u16)SCROLLBAR_PADDING })
        {
            FlSignal scrollbar_signal = ui_scrollbar(&state->scroll_target.y,
                .orientation = FlScrollbarOrientation_Vertical,
                .min = 0.0f,
                .max = max_scroll,
                .viewport_ratio = viewport_ratio,
                .width = SCROLLBAR_WIDTH - SCROLLBAR_PADDING * 2,
                .widget = { .id_override = fl_sprintf_arena(ui_get_ctx()->render_frame_allocator,
                                                          "_sb_%u", scroll_area_id) });

            // If user dragged scrollbar, sync offset immediately for responsiveness
            if (scrollbar_signal.dragging || scrollbar_signal.clicked) {
                state->scroll_offset.y = state->scroll_target.y;
            }
        }
    }
    // clang-format on
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_scroll_area_begin(FlLayoutId id, FlScrollAreaConfig config) {
    FlUiCtx* ctx = ui_get_ctx();
    FL_VALIDATE(ctx != nullptr);

    u32 element_id = ui_layout_id_to_clay_id(id);

    ScrollAreaState* state = get_or_create_scroll_state(element_id);
    if (state == nullptr) {
        // Fallback: render without scroll. Still push a stack entry with a null state (every stack consumer treats
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
    f32 scroll_speed = config.scroll_speed > 0.0f ? config.scroll_speed : SCROLL_DEFAULT_SPEED;
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_scroll_area_end(void) {
    FlUiCtx* ctx = ui_get_ctx();

    if (ctx != nullptr && ctx->scroll_area_overflow > 0) {
        ctx->scroll_area_overflow--;
    } else if (ctx != nullptr && ctx->scroll_area_stack_depth > 0) {
        ctx->scroll_area_stack_depth--;

        ScrollAreaStackEntry* entry = &ctx->scroll_area_stack[ctx->scroll_area_stack_depth];

        // The extent query reads the previous frame's layout data.
        if (entry->state != nullptr) {
            FlScrollContainerExtent extent = ui_get_scroll_container_extent(entry->scroll_area_id);

            if (extent.found) {
                entry->state->viewport_size = extent.viewport_size;
                entry->state->content_size = extent.content_extent;
            } else {
                // Fallback to the container's element rect (size only)
                FlVec2 pos, size;
                if (get_element_bounds(entry->scroll_area_id, &pos, &size)) {
                    entry->state->viewport_size = size;
                }
            }
        }

        if (entry->config.show_scrollbar && entry->state != nullptr) {
            // If show_scrollbar is true but mode is Never (default 0), treat as Always
            FlScrollbarDisplayMode mode = entry->config.scrollbar_mode;
            if (mode == FlScrollbarDisplayMode_Never) {
                mode = FlScrollbarDisplayMode_Always;
            }
            render_vertical_scrollbar(entry->state, entry->state->viewport_size, entry->state->content_size, mode,
                                      entry->scroll_area_id);
        }
    }

    fl_layout_end();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool ui_is_widget_visible(FlLayoutId widget_id) {
    FlUiCtx* ctx = ui_get_ctx();
    if (ctx == nullptr) {
        return true;
    }

    u32 id = ui_layout_id_to_clay_id(widget_id);
    WidgetScrollInfo* info = hashmap_get(&ctx->shared->widget_scroll_map, id);
    if (info == nullptr || info->scroll_area_id == 0) {
        return true; // Not in a scroll area = always visible
    }

    FlVec2 viewport_pos, viewport_size;
    if (!get_element_bounds(info->scroll_area_id, &viewport_pos, &viewport_size)) {
        return true; // Can't determine the viewport - assume visible
    }

    // A widget absent from item_state this frame was culled out of the viewport (Clay emits no render command
    // for it), which counts as not visible.
    FlVec2 widget_pos, widget_size;
    if (!get_element_bounds(id, &widget_pos, &widget_size)) {
        return false; // Culled out of the viewport = not visible
    }

    // Check if widget bounds intersect visible viewport (in screen space)
    f32 widget_top = widget_pos.y;
    f32 widget_bottom = widget_top + widget_size.y;
    f32 viewport_top = viewport_pos.y;
    f32 viewport_bottom = viewport_top + viewport_size.y;

    return (widget_bottom > viewport_top && widget_top < viewport_bottom);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_scroll_to_widget(FlLayoutId widget_id) {
    ui_scroll_to_widget_ex(widget_id, (FlScrollToOptions) { .margin = SCROLL_DEFAULT_MARGIN });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Internal helper that works with raw Clay ID (used by ui_scroll_auto_to_focused)
static void scroll_to_widget_internal(u32 widget_clay_id, FlScrollToOptions opts) {
    FlUiCtx* ctx = ui_get_ctx();
    if (ctx == nullptr) {
        return;
    }

    WidgetScrollInfo* info = hashmap_get(&ctx->shared->widget_scroll_map, widget_clay_id);
    if (info == nullptr || info->scroll_area_id == 0) {
        return; // Not in a scroll area
    }

    ScrollAreaState* scroll = hashmap_get(&ctx->shared->scroll_states, info->scroll_area_id);
    if (scroll == nullptr) {
        return;
    }

    // item_state only contains elements emitted this frame; fully culled widgets have no bounds to scroll to.
    FlVec2 widget_pos, widget_size;
    if (!get_element_bounds(widget_clay_id, &widget_pos, &widget_size)) {
        return; // Culled out of the viewport - position unknown, cannot scroll to it
    }

    // Get scroll area viewport rect from item_state (screen space)
    FlVec2 viewport_pos, viewport_size;
    if (!get_element_bounds(info->scroll_area_id, &viewport_pos, &viewport_size)) {
        return; // Can't determine viewport position
    }

    f32 margin = opts.margin > 0.0f ? opts.margin : SCROLL_DEFAULT_MARGIN;

    // Convert widget position to content-relative coordinates
    // widget.y is the screen position, viewport.y is the scroll area's screen position
    // Widget position in content = (widget.y - viewport.y) + scroll_offset
    f32 widget_top_in_content = (widget_pos.y - viewport_pos.y) + scroll->scroll_offset.y;
    f32 widget_height = widget_size.y;
    f32 widget_bottom_in_content = widget_top_in_content + widget_height;

    f32 viewport_height = viewport_size.y;

    // Current visible range in content space
    f32 visible_top = scroll->scroll_offset.y;
    f32 visible_bottom = visible_top + viewport_height;

    f32 new_scroll_y = scroll->scroll_target.y;

    switch (opts.align) {
        case FlScrollAlign_Nearest:
            if (widget_top_in_content - margin < visible_top) {
                new_scroll_y = widget_top_in_content - margin;
            } else if (widget_bottom_in_content + margin > visible_bottom) {
                new_scroll_y = widget_bottom_in_content + margin - viewport_height;
            }
            break;
        case FlScrollAlign_Top:
            new_scroll_y = widget_top_in_content - margin;
            break;
        case FlScrollAlign_Center:
            new_scroll_y = widget_top_in_content + widget_height / 2.0f - viewport_height / 2.0f;
            break;
        case FlScrollAlign_Bottom:
            new_scroll_y = widget_bottom_in_content + margin - viewport_height;
            break;
    }

    f32 max_scroll_y = scroll->content_size.y - viewport_height;
    new_scroll_y = clamp_f32_impl(new_scroll_y, 0.0f, max_f32_impl(0.0f, max_scroll_y));

    if (opts.immediate) {
        scroll->scroll_offset.y = new_scroll_y;
        scroll->scroll_target.y = new_scroll_y;
    } else {
        scroll->scroll_target.y = new_scroll_y;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void ui_scroll_to_widget_ex(FlLayoutId widget_id, FlScrollToOptions opts) {
    u32 id = ui_layout_id_to_clay_id(widget_id);
    scroll_to_widget_internal(id, opts);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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
