// scroll area and wheel tests

#include "render/common.h"

// fl_floating_area, fl_floating_area_id and fl_scroll_area_id. Each opens exactly one logical container and
// closes it, so Clay's open-element id returns to the baseline after every macro. The fl_floating_area without an
// id shows that the FL_ID_INT(__LINE__) seed expands to a usable id.

UTEST(render, floating_scroll_area_macros) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });

    ui_update_begin(ctx);

    // The baseline open element is the root opened by ui_update_begin. Every macro below returns to it.
    const uint32_t base_open = Clay_GetOpenElementId();

    // Floating with no id: FL_ID_INT("_Floating", __LINE__) gives each call site its own id.
    uint32_t floating_open = base_open;
    fl_floating_area(.attach_to = FlFloatingAttachTo_Root, .z_index = 100,
                     .sizing = { sizing_fixed(120), sizing_fixed(120) }) {
        floating_open = Clay_GetOpenElementId();
    }
    ASSERT_NE_MSG(base_open, floating_open, "fl_floating_area must open its container");
    ASSERT_EQ_MSG(base_open, Clay_GetOpenElementId(), "fl_floating_area must close exactly what it opened");

    // Floating with an explicit id, closed by fl_layout_end.
    fl_floating_area_id(FL_ID("floating_dialog"), .attach_to = FlFloatingAttachTo_Root, .z_index = 101,
                        .sizing = { sizing_fixed(80), sizing_fixed(80) }) {}
    ASSERT_EQ_MSG(base_open, Clay_GetOpenElementId(), "fl_floating_area_id must close exactly what it opened");

    // The scroll area opens a clipping container and closes with fl_scroll_area_end.
    uint32_t scroll_open = base_open;
    fl_scroll_area_id(FL_ID("scroll_region"), .sizing = { sizing_grow(), sizing_grow() }, .show_scrollbar = true) {
        scroll_open = Clay_GetOpenElementId();
    }
    ASSERT_NE_MSG(base_open, scroll_open, "fl_scroll_area_id must open its container");
    ASSERT_EQ_MSG(base_open, Clay_GetOpenElementId(), "fl_scroll_area_id must close exactly what it opened");

    ui_update_end(ctx, 1.0f / 60.0f);
}

// ui_scroll's visibility, scroll-to-widget and content-extent paths read the scroll container's screen rect from
// item_state, which has it because fl_scroll_area_begin registers the container with ui_track_element. A fixed
// 200x80 viewport holds eight 60px buttons (480px of content), so only the top button is on screen.

// One frame: a grow-to-fill scroll area holding eight MD buttons stacked top to bottom.
static void scroll_vis_frame(FlUiCtx* ctx) {
    ui_update_begin(ctx);
    fl_scroll_area_id(FL_ID("scroll_vis_area"), .sizing = { sizing_grow(), sizing_grow() },
                      .direction = FlLayoutDirection_TopToBottom) {
        ui_button(FL_S("svb0"));
        ui_button(FL_S("svb1"));
        ui_button(FL_S("svb2"));
        ui_button(FL_S("svb3"));
        ui_button(FL_S("svb4"));
        ui_button(FL_S("svb5"));
        ui_button(FL_S("svb6"));
        ui_button(FL_S("svb7"));
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(render, scroll_widget_visibility_reroute) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    render_set_layout(ctx, 200.0f, 80.0f);

    // Two frames. Frame 1 lays out and fills item_state and the scroll content and viewport sizes. Frame 2 reads
    // them back, since the item_state reads lag one frame.
    scroll_vis_frame(ctx);
    scroll_vis_frame(ctx);

    // svb0 is at the top of the 80px viewport. svb7 is 420px down and fully clipped. Clay emits no command for
    // it, so it is absent from item_state and reports as not visible.
    ASSERT_TRUE_MSG(ui_is_widget_visible(ID("svb0")), "top child must be visible in the scroll viewport");
    ASSERT_FALSE_MSG(ui_is_widget_visible(ID("svb7")),
                     "last child is fully clipped below the viewport and must not be visible");

    // The content extent is the real content size, 480px > 80px, and not the viewport-size fallback.
    FlScrollContainerExtent extent = ui_get_scroll_container_extent(ui_layout_id_to_clay_id(FL_ID("scroll_vis_area")));
    ASSERT_TRUE_MSG(extent.found, "scroll container extent must be queryable after two frames");
    ASSERT_TRUE_MSG(extent.content_extent.y > extent.viewport_size.y,
                    "content extent must exceed the viewport (real content dims, not the viewport fallback)");

    // svb1 straddles the viewport's bottom edge (y 60..120), so it is partially emitted and present in
    // item_state, which a scroll target has to be. Immediate mode sets the offset now, and calling before the
    // next frame's scroll_area_begin lets that frame's clip use it.
    ui_scroll_to_widget_ex(ID("svb1"), (FlScrollToOptions) { .immediate = true });
    scroll_vis_frame(ctx);

    ASSERT_TRUE_MSG(ui_is_widget_visible(ID("svb1")), "after scroll-to-widget svb1 must be visible");
    ASSERT_FALSE_MSG(ui_is_widget_visible(ID("svb0")),
                     "after scrolling down, the former top child must no longer be visible");
}

// fl_scroll_area_begin applies the wheel tick only to the scroll area the cursor is over. Two sibling scroll
// areas are open in the same frame, and the shared cached wheel delta scrolls only one. The hover test reads the
// container's screen rect from the previous frame's layout (item_state), so the scene runs one warm-up frame
// before the wheel frame.

// One frame: two 100x80 vertical scroll areas, A left and B right, separated by a 100px spacer so their rects
// never touch. Each holds eight buttons, so the content is taller than the 80px viewport and can scroll.
static void scroll_wheel_frame(FlUiCtx* ctx, f32 mx, f32 my, f32 wheel_y) {
    ctx->input_state.mouse.x = mx;
    ctx->input_state.mouse.y = my;
    ctx->input_state.mouse.wheel_x_cached = 0.0f;
    ctx->input_state.mouse.wheel_y_cached = wheel_y;

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {
        fl_scroll_area_id(FL_ID("wheel_area_a"), .sizing = { sizing_fixed(100), sizing_fixed(80) },
                          .direction = FlLayoutDirection_TopToBottom) {
            ui_button(FL_S("wa0"));
            ui_button(FL_S("wa1"));
            ui_button(FL_S("wa2"));
            ui_button(FL_S("wa3"));
            ui_button(FL_S("wa4"));
            ui_button(FL_S("wa5"));
            ui_button(FL_S("wa6"));
            ui_button(FL_S("wa7"));
        }
        ui_layout_area(.sizing = { sizing_fixed(100), sizing_fixed(80) }) {}
        fl_scroll_area_id(FL_ID("wheel_area_b"), .sizing = { sizing_fixed(100), sizing_fixed(80) },
                          .direction = FlLayoutDirection_TopToBottom) {
            ui_button(FL_S("wb0"));
            ui_button(FL_S("wb1"));
            ui_button(FL_S("wb2"));
            ui_button(FL_S("wb3"));
            ui_button(FL_S("wb4"));
            ui_button(FL_S("wb5"));
            ui_button(FL_S("wb6"));
            ui_button(FL_S("wb7"));
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(render, scroll_wheel_hover_gate) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // ui_scale 1 at a fixed size makes logical == physical, so mouse coords compare directly with the container
    // rects.
    render_set_layout(ctx, 1920.0f, 1080.0f);

    const u32 id_a = ui_layout_id_to_clay_id(FL_ID("wheel_area_a"));
    const u32 id_b = ui_layout_id_to_clay_id(FL_ID("wheel_area_b"));

    // Warm-up: no wheel and the cursor parked away from both areas, so both containers' screen rects are in
    // item_state and their content and viewport sizes are recorded for the next frame's max scroll.
    scroll_wheel_frame(ctx, 1000.0f, 1000.0f, 0.0f);
    scroll_wheel_frame(ctx, 1000.0f, 1000.0f, 0.0f);

    ScrollAreaState* sa = hashmap_get(&ctx->shared->scroll_states, id_a);
    ScrollAreaState* sb = hashmap_get(&ctx->shared->scroll_states, id_b);
    ASSERT_TRUE_MSG(sa != nullptr && sb != nullptr, "both scroll areas must have persistent state after layout");
    ASSERT_NEAR_MSG(0.0, (double)sa->scroll_offset.y, 0.001, "area A starts unscrolled");
    ASSERT_NEAR_MSG(0.0, (double)sb->scroll_offset.y, 0.001, "area B starts unscrolled");

    // Wheel frame: the cursor is over area A, whose rect is [0..100]x[0..80] at the root origin, and a negative
    // wheel delta gives a positive scroll. Only A is hovered, so only A consumes the tick.
    scroll_wheel_frame(ctx, 50.0f, 40.0f, -2.0f);

    sa = hashmap_get(&ctx->shared->scroll_states, id_a);
    sb = hashmap_get(&ctx->shared->scroll_states, id_b);
    ASSERT_TRUE_MSG(sa->scroll_offset.y > 0.5f, "the hovered scroll area consumes the wheel and scrolls");
    ASSERT_NEAR_MSG(0.0, (double)sb->scroll_offset.y, 0.001,
                    "the unhovered scroll area must not move when the wheel is delivered to its sibling");
}

// The wheel goes only to the innermost hovered scroll area, and hover is cut by the clip chain the hit tester
// uses. The outer area (200x80) holds a header_h spacer, then an inner area (100x60 at y = header_h), then a
// tall spacer so the outer has room to scroll.

typedef struct NestedFrame {
    f32 mx;
    f32 my;
    f32 wheel_y;
    f32 header_h;
} NestedFrame;

static void scroll_nested_frame(FlUiCtx* ctx, const NestedFrame* in) {
    const f32 header_h = in->header_h;
    ctx->input_state.mouse.x = in->mx;
    ctx->input_state.mouse.y = in->my;
    ctx->input_state.mouse.wheel_x_cached = 0.0f;
    ctx->input_state.mouse.wheel_y_cached = in->wheel_y;

    ui_update_begin(ctx);
    fl_scroll_area_id(FL_ID("nest_outer"), .sizing = { sizing_fixed(200), sizing_fixed(80) },
                      .direction = FlLayoutDirection_TopToBottom) {
        ui_layout_area(.sizing = { sizing_fixed(100), sizing_fixed(header_h) }) {}
        fl_scroll_area_id(FL_ID("nest_inner"), .sizing = { sizing_fixed(100), sizing_fixed(60) },
                          .direction = FlLayoutDirection_TopToBottom) {
            ui_layout_area(.sizing = { sizing_fixed(100), sizing_fixed(400) }) {}
        }
        ui_layout_area(.sizing = { sizing_fixed(100), sizing_fixed(400) }) {}
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

static void scroll_nested_setup(FlUiCtx* ctx, f32 header_h) {
    render_set_layout(ctx, 1920.0f, 1080.0f);

    // Start both areas unscrolled.
    for_each_hashmap(&ctx->shared->scroll_states, id, state) {
        (void)id;
        state->scroll_offset = (FlVec2) { 0 };
        state->scroll_target = (FlVec2) { 0 };
    }

    // Two warm-up frames with the cursor parked away, so layout rects and content sizes are recorded.
    scroll_nested_frame(ctx, &(NestedFrame) { 1000.0f, 1000.0f, 0.0f, header_h });
    scroll_nested_frame(ctx, &(NestedFrame) { 1000.0f, 1000.0f, 0.0f, header_h });
}

static void scroll_nested_reset_input(FlUiCtx* ctx) {
    ctx->input_state.mouse.wheel_x_cached = 0.0f;
    ctx->input_state.mouse.wheel_y_cached = 0.0f;
    ctx->input_state.mouse.x = 0.0f;
    ctx->input_state.mouse.y = 0.0f;
}

UTEST(render, scroll_wheel_nested) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const u32 outer_id = ui_layout_id_to_clay_id(FL_ID("nest_outer"));
    const u32 inner_id = ui_layout_id_to_clay_id(FL_ID("nest_inner"));

    // The inner area is at y 10..70, fully inside the outer viewport (0..80).
    scroll_nested_setup(ctx, 10.0f);

    ScrollAreaState* outer = hashmap_get(&ctx->shared->scroll_states, outer_id);
    ScrollAreaState* inner = hashmap_get(&ctx->shared->scroll_states, inner_id);
    ASSERT_TRUE_MSG(outer != nullptr && inner != nullptr, "both scroll areas must have persistent state");
    ASSERT_NEAR_MSG(0.0, (double)outer->scroll_offset.y, 0.001, "outer starts unscrolled");
    ASSERT_NEAR_MSG(0.0, (double)inner->scroll_offset.y, 0.001, "inner starts unscrolled");

    // Over the inner area: only the inner scrolls.
    scroll_nested_frame(ctx, &(NestedFrame) { 50.0f, 40.0f, -2.0f, 10.0f });
    outer = hashmap_get(&ctx->shared->scroll_states, outer_id);
    inner = hashmap_get(&ctx->shared->scroll_states, inner_id);
    ASSERT_TRUE_MSG(inner->scroll_offset.y > 0.5f, "a wheel tick over the inner area scrolls the inner area");
    ASSERT_NEAR_MSG(0.0, (double)outer->scroll_offset.y, 0.001, "a wheel tick over the inner area leaves the outer");

    // Over the outer area but outside the inner one (the header strip), only the outer scrolls.
    f32 inner_before = inner->scroll_offset.y;
    scroll_nested_frame(ctx, &(NestedFrame) { 50.0f, 5.0f, -2.0f, 10.0f });
    outer = hashmap_get(&ctx->shared->scroll_states, outer_id);
    inner = hashmap_get(&ctx->shared->scroll_states, inner_id);
    ASSERT_TRUE_MSG(outer->scroll_offset.y > 0.5f, "a wheel tick over the outer-only region scrolls the outer");
    ASSERT_NEAR_MSG((double)inner_before, (double)inner->scroll_offset.y, 0.001,
                    "a wheel tick over the outer-only region leaves the inner");

    scroll_nested_reset_input(ctx);
}

// The part of a scroll area cut away by an enclosing clip does not take the wheel. The inner area (y 60..120)
// hangs below the outer viewport (0..80), and the cursor at y = 100 is inside the inner's own rect but outside
// every visible region.

UTEST(render, scroll_wheel_clipped_away) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const u32 outer_id = ui_layout_id_to_clay_id(FL_ID("nest_outer"));
    const u32 inner_id = ui_layout_id_to_clay_id(FL_ID("nest_inner"));

    scroll_nested_setup(ctx, 60.0f);
    ScrollAreaState* outer = hashmap_get(&ctx->shared->scroll_states, outer_id);
    ScrollAreaState* inner = hashmap_get(&ctx->shared->scroll_states, inner_id);
    ASSERT_TRUE_MSG(outer != nullptr && inner != nullptr, "both scroll areas must have persistent state");

    scroll_nested_frame(ctx, &(NestedFrame) { 50.0f, 100.0f, -2.0f, 60.0f });
    outer = hashmap_get(&ctx->shared->scroll_states, outer_id);
    inner = hashmap_get(&ctx->shared->scroll_states, inner_id);
    ASSERT_NEAR_MSG(0.0, (double)inner->scroll_offset.y, 0.001, "the clipped-away part of the inner takes no wheel");
    ASSERT_NEAR_MSG(0.0, (double)outer->scroll_offset.y, 0.001, "the outer takes no wheel outside its own rect");

    // The visible part of the inner (y 60..80) still scrolls it.
    scroll_nested_frame(ctx, &(NestedFrame) { 50.0f, 70.0f, -2.0f, 60.0f });
    inner = hashmap_get(&ctx->shared->scroll_states, inner_id);
    ASSERT_TRUE_MSG(inner->scroll_offset.y > 0.5f, "the visible part of the inner area scrolls it");

    scroll_nested_reset_input(ctx);
}

// When scroll-state allocation fails, fl_scroll_area_begin still pushes a stack entry with a null state, so the
// paired fl_scroll_area_end pops its own entry and not the enclosing scroll area's. The ui_scroll test seam
// forces the failure for the inner area only, and the outer area's entry and depth are unchanged afterwards.

UTEST(render, scroll_state_alloc_failure_stack_balance) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });

    const u32 outer_id = ui_layout_id_to_clay_id(FL_ID("balance_outer"));
    const u32 inner_id = ui_layout_id_to_clay_id(FL_ID("balance_inner"));

    ui_update_begin(ctx);

    fl_scroll_area_id(FL_ID("balance_outer"), .sizing = { sizing_fixed(200), sizing_fixed(160) },
                      .direction = FlLayoutDirection_TopToBottom) {
        ASSERT_EQ_MSG(1u, ctx->scroll_area_stack_depth, "outer scroll area must push one stack entry");
        ASSERT_EQ_MSG(outer_id, ctx->scroll_area_stack[0].scroll_area_id, "stack top is the outer area");
        ScrollAreaState* outer_state = ctx->scroll_area_stack[0].state;
        ASSERT_TRUE_MSG(outer_state != nullptr, "outer area has real scroll state");

        ui_scroll_test_force_state_alloc_failure(true);
        fl_scroll_area_id(FL_ID("balance_inner"), .sizing = { sizing_fixed(100), sizing_fixed(80) },
                          .direction = FlLayoutDirection_TopToBottom) {
            ASSERT_EQ_MSG(2u, ctx->scroll_area_stack_depth, "fallback begin must still push a stack entry");
            ASSERT_EQ_MSG(inner_id, ctx->scroll_area_stack[1].scroll_area_id, "fallback entry carries the inner id");
            ASSERT_TRUE_MSG(ctx->scroll_area_stack[1].state == nullptr, "fallback entry has no scroll state");
            // A widget inside the fallback area runs against a stack entry with no scroll state.
            ui_button(FL_S("balance_inner_btn"));
        }
        ui_scroll_test_force_state_alloc_failure(false);

        ASSERT_EQ_MSG(1u, ctx->scroll_area_stack_depth,
                      "inner begin/end pair must leave the enclosing depth untouched");
        ASSERT_EQ_MSG(outer_id, ctx->scroll_area_stack[0].scroll_area_id,
                      "outer stack entry survives the inner fallback scope");
        ASSERT_TRUE_MSG(ctx->scroll_area_stack[0].state == outer_state,
                        "outer stack entry still points at the outer scroll state");
        ui_button(FL_S("balance_outer_btn"));
    }
    ASSERT_EQ_MSG(0u, ctx->scroll_area_stack_depth, "all scroll scopes unwound");

    ui_update_end(ctx, 1.0f / 60.0f);
}

// Scroll areas nested past the stack capacity do not pop an enclosing area's entry when they end.

UTEST(render, scroll_area_overflow_stays_paired) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });

    const u32 total = SCROLL_AREA_STACK_MAX_DEPTH + 3;
    char names[SCROLL_AREA_STACK_MAX_DEPTH + 3][16];

    ui_update_begin(ctx);
    for (u32 i = 0; i < total; i++) {
        snprintf(names[i], sizeof(names[i]), "ovf_%u", i);
        fl_scroll_area_begin((FlLayoutId) { .name = string_from_cstr(names[i]) },
                             (FlScrollAreaConfig) { .sizing = { sizing_fixed(200), sizing_fixed(160) } });
    }
    ASSERT_EQ_MSG((u32)SCROLL_AREA_STACK_MAX_DEPTH, ctx->scroll_area_stack_depth, "stack depth is capped");
    ASSERT_EQ_MSG(3u, ctx->scroll_area_overflow, "areas past the cap are counted");

    for (u32 i = 0; i < total; i++) {
        fl_scroll_area_end();
        if (i < 3) {
            ASSERT_EQ_MSG((u32)SCROLL_AREA_STACK_MAX_DEPTH, ctx->scroll_area_stack_depth,
                          "ending an overflowed area must leave the tracked stack untouched");
        }
    }
    ASSERT_EQ_MSG(0u, ctx->scroll_area_stack_depth, "all tracked areas unwound");
    ASSERT_EQ_MSG(0u, ctx->scroll_area_overflow, "all overflowed areas retired");
    ui_update_end(ctx, 1.0f / 60.0f);
}

// A placed child is a layout element positioned at an explicit (x, y) in its parent's content coordinate space.
// It scrolls and clips with the container but does not advance the flex cursor or grow the content extent. The
// scene is a 400x300 manual_scroll clip box at the origin holding flowed siblings D and E and three placed
// children A, B and C. A and B overlap, and C straddles the right clip edge. Frame 1 (scroll 0) checks
// placement, overlap, independence from flow, and clipping. Frame 2 emits again with a non-zero scroll and
// checks that the placed children move by exactly the scroll delta.
UTEST(render, placed_child_scroll_clip) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    const FlColor clear_col = ui_color(12, 12, 14, 255);   // canvas_fixture_reset clear color (outside the clip box)
    const FlColor box_col = ui_color(50, 50, 60, 255);     // clip-box fill (inside the box, no child over it)
    const FlColor flow_col = ui_color(240, 200, 40, 255);  // D: a flowed sibling, emitted before the placed children
    const FlColor flow2_col = ui_color(200, 80, 220, 255); // E: a flowed sibling, emitted after the placed children
    const FlColor a_col = ui_color(40, 200, 120, 255);     // A: placed at (100, 100)
    const FlColor b_col = ui_color(220, 60, 60, 255);      // B: placed at (120, 110), overlapping A
    const FlColor c_col = ui_color(80, 140, 240, 255);     // C: placed at (380, 100), straddling the right clip edge

    // Frame 1: scroll_pos = (0, 0).
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        // A 400x300 clip box at the origin that owns its scroll (manual_scroll), the content viewport. With the
        // default LeftToRight the two flowed children D and E lay out as a row, and the three placed children
        // do not take part.
        ui_layout_area_id(
            ID("placed_clip"), .sizing = { sizing_fixed(400), sizing_fixed(300) }, .color = box_col,
            .clip = { .horizontal = true, .vertical = true, .manual_scroll = true, .scroll_pos = { 0.0f, 0.0f } }) {
            // Flowed sibling D: the first child that is not placed, at the flex-cursor origin (0,0).
            ui_layout_area(.sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = flow_col) {}
            ui_layout_area(.sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = a_col,
                           .placed = { .enabled = true, .offset = { 100.0f, 100.0f } }) {}
            ui_layout_area(.sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = b_col,
                           .placed = { .enabled = true, .offset = { 120.0f, 110.0f } }) {}
            ui_layout_area(.sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = c_col,
                           .placed = { .enabled = true, .offset = { 380.0f, 100.0f } }) {}
            // Flowed sibling E, emitted after the placed children. Placed children do not advance the cursor, so
            // E lands right after D at x=40..80. If they advanced it, E would be far to the right.
            ui_layout_area(.sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = flow2_col) {}
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");

    // A renders at its explicit offset. (105,105) is inside A (100..140) but left of B (x<120), so only A.
    ASSERT_TRUE_MSG(px_near(&buf, 105, 105, a_col, 2), "placed child A renders at its content-space offset");
    // The clip box shows where no child covers it, so the placed children are its descendants.
    ASSERT_TRUE_MSG(px_near(&buf, 300, 250, box_col, 2), "clip box renders its own area under the placed children");
    // D is at the cursor origin (x=0..40), and E, emitted after the three placed children, is right after D at
    // x=40..80. A cursor advanced by the placed children would put E near x=160.
    ASSERT_TRUE_MSG(px_near(&buf, 20, 20, flow_col, 2), "first flowed sibling D is at the cursor origin");
    ASSERT_TRUE_MSG(px_near(&buf, 60, 20, flow2_col, 2), "flowed sibling after placed children is not displaced");
    ASSERT_FALSE_MSG(px_near(&buf, 160, 20, flow2_col, 2), "placed children did not advance the flex cursor");
    // Overlap: the A-only pixel is A, the B-only pixel is B, and the overlap shows B, the later sibling.
    ASSERT_TRUE_MSG(px_near(&buf, 150, 145, b_col, 2), "placed child B renders where it does not overlap A");
    ASSERT_TRUE_MSG(px_near(&buf, 130, 130, b_col, 2), "overlapping placed children both render (later draws on top)");
    // C straddles the right edge. Its inside half is drawn and its outside half is cut at the clip area.
    ASSERT_TRUE_MSG(px_near(&buf, 390, 120, c_col, 2), "placed child C renders inside the clip area");
    ASSERT_FALSE_MSG(px_near(&buf, 410, 120, c_col, 2), "placed child is clipped to the container's active area");
    ASSERT_TRUE_MSG(px_near(&buf, 410, 120, clear_col, 2),
                    "outside the clip box is the clear color, not leaked content");

    // Frame 2: scroll_pos = (-40, -30), so the content scrolls up and left.
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        ui_layout_area_id(
            ID("placed_clip"), .sizing = { sizing_fixed(400), sizing_fixed(300) }, .color = box_col,
            .clip = { .horizontal = true, .vertical = true, .manual_scroll = true, .scroll_pos = { -40.0f, -30.0f } }) {
            ui_layout_area(.sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = flow_col) {}
            ui_layout_area(.sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = a_col,
                           .placed = { .enabled = true, .offset = { 100.0f, 100.0f } }) {}
            ui_layout_area(.sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = b_col,
                           .placed = { .enabled = true, .offset = { 120.0f, 110.0f } }) {}
            ui_layout_area(.sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = c_col,
                           .placed = { .enabled = true, .offset = { 380.0f, 100.0f } }) {}
            ui_layout_area(.sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = flow2_col) {}
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf2;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf2), "read_pixels failed");

    // The placed child scrolls with the container: A's A-only pixel moved by exactly the scroll delta (-40,-30),
    // from (105,105) to (65,75), and is gone from its old position.
    ASSERT_TRUE_MSG(px_near(&buf2, 65, 75, a_col, 2), "placed child scrolls with the container by the scroll delta");
    ASSERT_FALSE_MSG(px_near(&buf2, 105, 105, a_col, 2), "placed child no longer sits at its un-scrolled position");

    // The clip container's content size counts only the flowed children (D and E, 40px wide each, in a row, so
    // 80px) and never the placed children. It is queried after the second frame, because Clay fills contentSize
    // once the scroll container exists and it is still zero on the first frame.
    Clay_ScrollContainerData scd
        = Clay_GetScrollContainerData((Clay_ElementId) { .id = ui_layout_id_to_clay_id(ID("placed_clip")) });
    ASSERT_TRUE_MSG(scd.found, "clip container scroll data is queryable");
    ASSERT_TRUE_MSG(scd.contentDimensions.width <= 80.5f,
                    "placed children do not grow the container's content extent (flowed D+E only)");
}

// Two ui_scrollbar widgets in one frame without an id_override do not collide. The automatic id is seeded from the
// bound value pointer, which is stable per instance, so each scrollbar gets its own element id. The case has two
// vertical scrollbars side by side, bound to different float*. One is hovered and tapped and the other is
// checked to be untouched.

typedef struct TwoScrollbars {
    FlSignal a, b;
} TwoScrollbars;

// One frame: scrollbar A (bound to va) in the left box and scrollbar B (bound to vb) in the right box, with the
// cursor at (mx, my) and an optional press and release in the same frame. Neither passes id_override, so both
// get automatic ids.
typedef struct ScrollbarsFrame {
    float* va;
    float* vb;
    f32 mx;
    f32 my;
    bool click;
} ScrollbarsFrame;

static TwoScrollbars two_scrollbars_frame(FlUiCtx* ctx, const ScrollbarsFrame* in) {
    float* va = in->va;
    float* vb = in->vb;
    ctx->input_state.mouse.x = in->mx;
    ctx->input_state.mouse.y = in->my;
    ctx->mouse_pressed_this_frame = in->click;
    ctx->mouse_released_this_frame = in->click;

    TwoScrollbars r = { 0 };
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        // The left box [0..40]x[0..300] holds scrollbar A. A 160px spacer separates it from the right box, so
        // hovering A never touches B.
        ui_layout_area_id(ID("sb_box_a"), .sizing = { sizing_fixed(40), sizing_fixed(300) }) {
            r.a = ui_scrollbar(va, .min = 0.0f, .max = 100.0f, .width = 40.0f);
        }
        ui_layout_area(.sizing = { sizing_fixed(160), sizing_fixed(300) }) {}
        ui_layout_area_id(ID("sb_box_b"), .sizing = { sizing_fixed(40), sizing_fixed(300) }) {
            r.b = ui_scrollbar(vb, .min = 0.0f, .max = 100.0f, .width = 40.0f);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    return r;
}

UTEST(render, scrollbar_auto_id_no_collision) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    float va = 0.0f;
    float vb = 25.0f;

    // Hover over scrollbar A's track for a few frames so A's hot state propagates before the tap. x=20 is inside
    // the scrollbar in the left box, [0..40].
    TwoScrollbars r = { 0 };
    for (int f = 0; f < 5; f++) {
        r = two_scrollbars_frame(ctx, &(ScrollbarsFrame) { &va, &vb, 20.0f, 150.0f, false });
    }
    ASSERT_TRUE_MSG(r.a.hovered, "scrollbar A reports hovered while the cursor is over its track");
    ASSERT_FALSE_MSG(r.b.hovered, "scrollbar B (distinct id) does not report hovered — no shared hot state");

    // Tap the middle of A's track. A's value jumps toward mid-range, and B's value is untouched, since B has its
    // own id and no drag state is shared.
    r = two_scrollbars_frame(ctx, &(ScrollbarsFrame) { &va, &vb, 20.0f, 150.0f, true });
    ASSERT_TRUE_MSG(va > 1.0f, "tapping scrollbar A moves its bound value");
    ASSERT_EQ_MSG(vb, 25.0f, "scrollbar B's value is unchanged — the two scrollbars do not share drag state");
}

// With viewport_ratio 1.0 the thumb fills the track, so the drag path's usable_track is exactly 0. Dividing by it
// gives 0/0, a NaN, and both of clamp's comparisons fail on NaN, so a NaN would pass straight into the bound
// value.
UTEST(render, scrollbar_full_viewport_ratio_no_nan) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    float value = 25.0f;

    for (int f = 0; f < 3; f++) {
        ctx->input_state.mouse.x = 20.0f;
        ctx->input_state.mouse.y = 150.0f;
        ctx->mouse_pressed_this_frame = (f == 2);
        ctx->mouse_released_this_frame = (f == 2);

        ui_update_begin(ctx);
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
            ui_layout_area_id(ID("sb_full_box"), .sizing = { sizing_fixed(40), sizing_fixed(300) }) {
                ui_scrollbar(&value, .min = 0.0f, .max = 100.0f, .width = 40.0f, .viewport_ratio = 1.0f,
                             .widget.id_override = FL_S("sb_full"));
            }
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }

    ASSERT_TRUE_MSG(value == value, "tapping a full-viewport scrollbar must not write NaN into the bound value");
    ASSERT_TRUE_MSG(value >= 0.0f && value <= 100.0f, "the bound value must stay inside [min, max]");
}

// A painter 1px line inside an element whose origin has a fractional offset renders crisp through the software
// backend. A floating element's offset is scaled but not snapped (layout.c), and ui_scroll applies a scroll
// offset as an unsnapped element translation in the same way, so at scale 1.0 the floating offset stands in for
// a fractional scroll. The splice snaps the absolute device position, which puts the line on a whole row.
UTEST(render, snapped_line_crisp_at_fractional_scroll) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx); // scale 1.0, so the fractional origin is the only fraction involved

    const FlColor line_col = ui_color(90, 200, 230, 255);

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        // A floating canvas at the root with a fractional y offset (10.4), the unsnapped element origin a
        // fractional scroll produces. The hairline at local y=0 is at device y=10.4, which snaps to row 10. Local
        // x 0..100 with the x offset of 200 covers device x 200..300, so x=250 is in the middle of the line.
        ui_floating_area_id(ID("scrolled_canvas"), .attach_to = FlFloatingAttachTo_Root, .offset = { 200.0f, 10.4f },
                            .sizing = { sizing_fixed(200), sizing_fixed(200) }) {
            painter_hline(g_ui_ctx->painter, 0.0f, 0.0f, 100.0f, line_col);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(px_near(&buf, 250, 10, line_col, 2), "the snapped line row (10) is solid — no blur");
    ASSERT_FALSE_MSG(px_near(&buf, 250, 9, line_col, 2), "the row above the line (9) is pure background");
    ASSERT_FALSE_MSG(px_near(&buf, 250, 11, line_col, 2), "the row below the line (11) is pure background");

    canvas_fixture_reset(ctx);
}

// The wheel end to end. It is injected with fl_app_inject_wheel over a full-window Scroll Target, the scoped delta
// accumulates into an app-owned offset, and a column of bars is painted shifted by it. The frame before the
// wheel (offset 0) and the frame after differ.

typedef struct WheelScrollState {
    FlUiCtx* ctx;
    f32 offset_y; // cumulative scroll offset owned by the app. The framework holds no absolute wheel value
} WheelScrollState;

#define WHEEL_SCROLL_W 200

#define WHEEL_SCROLL_H 160

#define WHEEL_SCROLL_BARS 12

#define WHEEL_SCROLL_PITCH 28.0f

#define WHEEL_SCROLL_BAR_H 20.0f

// The content is taller than the viewport (12 * 28 = 336 > 160), so there is range to scroll.
#define WHEEL_SCROLL_MAX_OFFSET ((float)(WHEEL_SCROLL_BARS) * WHEEL_SCROLL_PITCH - (float)(WHEEL_SCROLL_H))

static const FlLayoutId k_wheel_scroll_id
    = { .name = { .data = "wheel_scroll", .length = 12, .is_static = 1 }, .id = 0 };

// The wheel over the target accumulates into offset_y, clamped to the content range. The frame holds only solid
// painter rects and no text, so its bytes are deterministic on the software renderer.
static void wheel_scroll_frame(void* user_data) {
    WheelScrollState* st = (WheelScrollState*)user_data;
    st->ctx = g_ui_ctx;

    ui_layout_area_id(k_wheel_scroll_id, .sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_scroll_target(k_wheel_scroll_id);
        FlVec2 wheel = ui_mouse_wheel_delta(k_wheel_scroll_id);
        st->offset_y -= wheel.y; // positive dy scrolls the content up, so the offset grows
        if (st->offset_y < 0.0f) {
            st->offset_y = 0.0f;
        }
        if (st->offset_y > WHEEL_SCROLL_MAX_OFFSET) {
            st->offset_y = WHEEL_SCROLL_MAX_OFFSET;
        }

        Painter* p = st->ctx->painter;
        painter_rect(p, (FlRect) { .x = 0.0f, .y = 0.0f, .w = (float)WHEEL_SCROLL_W, .h = (float)WHEEL_SCROLL_H },
                     ui_color(18, 20, 26, 255));
        for (int i = 0; i < WHEEL_SCROLL_BARS; i++) {
            float y = (float)i * WHEEL_SCROLL_PITCH - st->offset_y;
            FlColor color = ui_color((u8)(30 + i * 18), (u8)(210 - i * 14), (u8)(120 + i * 8), 255);
            painter_rect(p, (FlRect) { .x = 0.0f, .y = y, .w = (float)WHEEL_SCROLL_W, .h = WHEEL_SCROLL_BAR_H }, color);
        }
    }
}

UTEST(render, wheel_scroll) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings
        = { .window_title = FL_S("wheel_scroll"), .width = WHEEL_SCROLL_W, .height = WHEEL_SCROLL_H, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    WheelScrollState st = { 0 };

    // Let the pointer settle over the Scroll Target so it becomes hot and receives the wheel.
    fl_app_inject_mouse_move(app, WHEEL_SCROLL_W / 2, WHEEL_SCROLL_H / 2);
    for (int i = 0; i < 4; i++) {
        fl_app_tick(app, &st, wheel_scroll_frame);
    }
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    ASSERT_EQ_MSG(0.0, (double)st.offset_y, "no wheel injected yet: offset is at the top");

    FlPixelReadback before;
    ASSERT_TRUE_MSG(st.ctx->shared->renderer.read_pixels != nullptr, "backend has no pixel-readback capability");
    ASSERT_TRUE_MSG(render_seam_read_pixels(&st.ctx->shared->renderer, st.ctx->surface, &before), "read_pixels failed");
    ASSERT_TRUE_MSG(before.width == WHEEL_SCROLL_W && before.height == WHEEL_SCROLL_H, "unexpected before-frame size");
    uint64_t before_hash = fnv1a_64(before.pixels, (size_t)before.width * (size_t)before.height * 3u);
    dump_frame("wheel_before", &before);

    // The cached wheel_y is unscaled, since input caches the accumulated delta as it is, so one inject of
    // dy = -WHEEL_SCROLL_MAX_OFFSET moves the offset exactly to the clamped bottom.
    fl_app_inject_wheel(app, 0.0f, -WHEEL_SCROLL_MAX_OFFSET);
    fl_app_tick(app, &st, wheel_scroll_frame);
    ASSERT_NEAR_MSG((double)WHEEL_SCROLL_MAX_OFFSET, (double)st.offset_y, 0.001,
                    "wheel over the hot Scroll Target routed the scoped delta into the app offset");

    FlPixelReadback after;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&st.ctx->shared->renderer, st.ctx->surface, &after), "read_pixels failed");
    uint64_t after_hash = fnv1a_64(after.pixels, (size_t)after.width * (size_t)after.height * 3u);
    dump_frame("wheel_after", &after);

    ASSERT_NE_MSG((long long)before_hash, (long long)after_hash, "wheel scroll must change the rendered frame");

    fl_app_destroy(app);
}
