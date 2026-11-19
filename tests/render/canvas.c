// canvas, placed child and tooltip tests

#include "render/common.h"

static int canvas_custom_command_count(void) {
    int count = 0;
    int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (ui_get_render_command(i, &cmd) && cmd.type == UiRenderCmd_Custom) {
            count++;
        }
    }
    return count;
}

// Paint takes part in z-order (it is under children) and is bracketed by the live scissor stack, and many
// primitives in one scope produce exactly one CUSTOM span: one marker per scope, never one per primitive.
UTEST(render, canvas_paint_z_clip_and_one_span) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    const FlColor canvas_col = ui_color(40, 200, 120, 255); // opaque paint fill
    const FlColor child_col = ui_color(230, 90, 60, 255);   // opaque child box over the paint

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        // The clip container (120x120 at the origin) clips its oversized canvas child, so an ancestor scissor
        // brackets the paint. The canvas also clips to its own 200x200 rect, but the ancestor clip is tighter
        // and is the one that applies here.
        ui_layout_area_id(ID("clipbox"), .sizing = { sizing_fixed(120), sizing_fixed(120) },
                          .clip = { .horizontal = true, .vertical = true }) {
            ui_layout_area_id(ID("canvas"), .sizing = { sizing_fixed(200), sizing_fixed(200) }) {
                Painter* p = g_ui_ctx->painter;
                // Many primitives in one scope, which become a single CUSTOM span.
                painter_rect(p, (FlRect) { .x = 0.0f, .y = 0.0f, .w = 200.0f, .h = 200.0f }, canvas_col);
                painter_rect(p, (FlRect) { .x = 4.0f, .y = 4.0f, .w = 8.0f, .h = 8.0f }, canvas_col);
                painter_circle(p, (FlVec2) { .x = 100.0f, .y = 100.0f }, 6.0f, canvas_col);
                painter_rect(p, (FlRect) { .x = 20.0f, .y = 20.0f, .w = 6.0f, .h = 6.0f }, canvas_col);

                // A child element laid out at the canvas origin, which draws over the paint.
                ui_layout_area(.sizing = { sizing_fixed(60), sizing_fixed(60) }, .color = child_col) {}
            }
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");

    // (30,30) is under the child box (0..60), so it has the child's color. (90,90) is inside the clip (0..120) and
    // the canvas paint but not under the child, so it has the fill. (150,150) is within the canvas paint region
    // (0..200) but outside the clip (>120), so the scissor cut it.
    const PxProbe probes[] = {
        { 30, 30, child_col, 2, true, "child element draws over the paint (paint at element z)" },
        { 90, 90, canvas_col, 2, true, "paint is visible at the element's z, inside the scissor" },
        { 150, 150, canvas_col, 2, false, "paint is bracketed by the live layout scissor stack" },
    };
    check_px_probes(utest_result, &buf, probes, sizeof_array(probes));

    ASSERT_EQ_MSG(canvas_custom_command_count(), 1, "one painting scope emits exactly one CUSTOM span");
}

// A placed child is interactive like any other element: hit testing, hover, press and click, the element-local
// mouse_pos, and topmost-wins arbitration all use the placed (offset) rect and not the flex position it skipped.
// The scene is a 400x300 clip box at root origin (40, 30) with two overlapping interactive placed children, A
// (offset 100,100) and B (offset 120,110).

typedef struct PlacedHit {
    FlSignal a, b;
    FlElementState sa, sb;
} PlacedHit;

// Emits one frame of the scene with the cursor at (mx, my). click injects a press and release in the same frame.
// A is emitted before B, so B is the topmost sibling, drawn later. Both are clickable and have a color, so each
// emits a RECTANGLE that gives the hit test its rect.
static PlacedHit placed_interaction_frame(FlUiCtx* ctx, f32 mx, f32 my, bool click) {
    ctx->input_state.mouse.x = mx;
    ctx->input_state.mouse.y = my;
    ctx->mouse_pressed_this_frame = click;
    ctx->mouse_released_this_frame = click;

    PlacedHit r = { 0 };
    ui_update_begin(ctx);
    // Root padding puts the clip box at (40, 30), so the placed rect's origin is not zero.
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .padding = { .left = 40, .top = 30 }) {
        ui_layout_area_id(
            ID("pi_clip"), .sizing = { sizing_fixed(400), sizing_fixed(300) }, .color = ui_color(50, 50, 60, 255),
            .clip = { .horizontal = true, .vertical = true, .manual_scroll = true, .scroll_pos = { 0.0f, 0.0f } }) {
            ui_layout_area_id(ID("pi_a"), .sizing = { sizing_fixed(40), sizing_fixed(40) },
                              .color = ui_color(40, 200, 120, 255),
                              .placed = { .enabled = true, .offset = { 100.0f, 100.0f } }) {
                r.a = ui_item_behavior(ID("pi_a"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
            ui_layout_area_id(ID("pi_b"), .sizing = { sizing_fixed(40), sizing_fixed(40) },
                              .color = ui_color(220, 60, 60, 255),
                              .placed = { .enabled = true, .offset = { 120.0f, 110.0f } }) {
                r.b = ui_item_behavior(ID("pi_b"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    r.sa = ui_get_element_state(ID("pi_a"));
    r.sb = ui_get_element_state(ID("pi_b"));
    return r;
}

UTEST(render, placed_child_interaction) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx); // 1920x1080 at scale 1, so logical == physical

    // Geometry (root padding origin (40,30) plus the placed offset, all at scale 1.0):
    //   A: content [100..140]x[100..140], screen [140..180]x[130..170]
    //   B: content [120..160]x[110..150], screen [160..200]x[140..180]
    // The A-only point (150,135) is inside A and left of and above B. The overlap point (170,155) is inside both.
    const f32 a_only_x = 150.0f, a_only_y = 135.0f;
    const f32 overlap_x = 170.0f, overlap_y = 155.0f;

    // Phase 1: hover A alone. Hot propagates over two frames (next_hot_id -> hot_id -> state.hot, then the Signal
    // the behavior reads the following frame), so stay a few frames before asserting hovered.
    PlacedHit r = { 0 };
    for (int f = 0; f < 4; f++) {
        r = placed_interaction_frame(ctx, a_only_x, a_only_y, false);
    }
    ASSERT_TRUE_MSG(r.a.hovered, "item_behavior on a placed child reports hovered at its placed rect");
    ASSERT_FALSE_MSG(r.b.hovered, "the non-hovered overlapping placed child does not report hovered");

    // element_state returns the placed rect (root origin + placed offset) and not the skipped flex position, and
    // Signal.mouse_pos is local to the placed rect: cursor - placed origin = (150-140, 135-130).
    ASSERT_TRUE_MSG(r.sa.found, "the placed child is hit-tested as an ordinary laid-out element");
    const double placed_got[] = { r.sa.pos.x, r.sa.pos.y, r.sa.size.x, r.sa.size.y, r.a.mouse_pos.x, r.a.mouse_pos.y };
    const double placed_want[] = { 140.0, 130.0, 40.0, 40.0, 10.0, 5.0 };
    for (size_t i = 0; i < sizeof_array(placed_got); i++) {
        ASSERT_NEAR(placed_got[i], placed_want[i], 0.5);
    }

    // Phase 2: a fast tap (press and release in the same frame) on the hovered placed child clicks it.
    r = placed_interaction_frame(ctx, a_only_x, a_only_y, true);
    ASSERT_TRUE_MSG(r.a.pressed, "a press over the placed rect reports pressed");
    ASSERT_TRUE_MSG(r.a.clicked, "a tap over the placed rect reports clicked");

    // Phase 3: topmost wins. With the cursor over the overlap of A and B, B takes the hover and A does not,
    // because B is emitted last and drawn on top. Stay a few frames so the hot arbitration settles on B.
    for (int f = 0; f < 4; f++) {
        r = placed_interaction_frame(ctx, overlap_x, overlap_y, false);
    }
    const BoolCase topmost[] = {
        { r.b.hovered, true, "the topmost overlapping placed child (later sibling) wins the hover" },
        { r.a.hovered, false, "the shadowed overlapping placed child does not also report hovered" },
        { r.sb.hot, true, "element_state hot reflects the topmost placed child" },
        { r.sa.hot, false, "element_state hot is not set on the shadowed placed child" },
    };
    check_bool_cases(utest_result, topmost, sizeof_array(topmost));
}

// One frame of a single placed child A at content offset (100,100) in a manual_scroll clip box at root origin
// (40,30), with the box scrolled to (scroll_x, scroll_y).
static void emit_placed_state_frame(FlUiCtx* ctx, f32 scroll_x, f32 scroll_y) {
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .padding = { .left = 40, .top = 30 }) {
        ui_layout_area_id(
            ID("pes_clip"), .sizing = { sizing_fixed(400), sizing_fixed(300) }, .color = ui_color(50, 50, 60, 255),
            .clip
            = { .horizontal = true, .vertical = true, .manual_scroll = true, .scroll_pos = { scroll_x, scroll_y } }) {
            ui_layout_area_id(ID("pes_a"), .sizing = { sizing_fixed(40), sizing_fixed(40) },
                              .color = ui_color(40, 200, 120, 255),
                              .placed = { .enabled = true, .offset = { 100.0f, 100.0f } }) {
                ui_item_behavior(ID("pes_a"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

// A placed child's element_state origin follows the container scroll, since it scrolls and clips like a flowed
// child, and the element-local mouse_position stays in logical authoring space when the UI scale is not 1. The
// scene is a single placed child A at content offset (100,100) in a manual_scroll clip box at root origin
// (40,30).
UTEST(render, placed_child_element_state) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    // Scroll tracking at scale 1. Frame 1, scroll (0,0): the placed rect is at origin + offset = (140, 130).
    ctx->input_state.mouse.x = 0.0f;
    ctx->input_state.mouse.y = 0.0f;
    emit_placed_state_frame(ctx, 0.0f, 0.0f);

    FlElementState s0 = ui_get_element_state(ID("pes_a"));
    ASSERT_TRUE_MSG(s0.found, "placed child element_state is found");
    ASSERT_NEAR(s0.pos.x, 140.0f, 0.5f);
    ASSERT_NEAR(s0.pos.y, 130.0f, 0.5f);

    // Frame 2, scroll (-40,-30): the placed rect moves with the container by the scroll delta, to (100,100).
    emit_placed_state_frame(ctx, -40.0f, -30.0f);

    FlElementState s1 = ui_get_element_state(ID("pes_a"));
    ASSERT_NEAR(s1.pos.x, 100.0f, 0.5f); // 140 + (-40)
    ASSERT_NEAR(s1.pos.y, 100.0f, 0.5f); // 130 + (-30)

    // The pointer in logical space at a UI scale other than 1. At ui_scale 0.5 the placed offset (100,100
    // logical) scales to (50,50) physical and the root padding (40,30 logical) to (20,15) physical, so A's
    // physical origin is (70, 65). A cursor 10 physical px inside is 20 logical px inside, so the placed rect
    // goes through the logical-space mouse query like any other.
    ctx->layout_width = 960.0f;
    ctx->layout_height = 540.0f;
    UiScalePair fit_540 = ui_reference_fit(540);
    ui_set_scale(fit_540.ui_scale, fit_540.text_scale);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 960.0f, .height = 540.0f });

    ctx->input_state.mouse.x = 0.0f;
    ctx->input_state.mouse.y = 0.0f;
    emit_placed_state_frame(ctx, 0.0f, 0.0f);

    FlElementState sh = ui_get_element_state(ID("pes_a"));
    ASSERT_TRUE_MSG(sh.found, "placed child is found at 0.5 scale");
    ASSERT_NEAR(sh.pos.x, 70.0f, 0.5f); // (40+100) logical * 0.5 = 70 physical
    ASSERT_NEAR(sh.pos.y, 65.0f, 0.5f); // (30+100) logical * 0.5 = 65 physical

    ctx->input_state.mouse.x = 80.0f; // 10 physical px inside on x (origin 70)
    ctx->input_state.mouse.y = 75.0f; // 10 physical px inside on y (origin 65)
    FlVec2 local = ui_mouse_position(ID("pes_a"));
    ASSERT_NEAR(local.x, 20.0f, 0.5f); // 10 physical / 0.5 = 20 logical
    ASSERT_NEAR(local.y, 20.0f, 0.5f);
}

// An offscreen placed child is culled by the engine like any flowed child: it is not drawn, emits no render
// command and never becomes interactive. That gives the app's own visible-range virtualization a fallback. The
// scene holds one placed child on screen as the control and one placed far past the screen bounds, both
// registered as interactive.
UTEST(render, placed_child_culling) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx); // 1920x1080

    const FlColor onscreen_col = ui_color(40, 200, 120, 255); // control: placed inside the viewport
    const FlColor offscreen_col = ui_color(230, 90, 30, 255); // placed far past the 1920x1080 bounds

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_layout_area_id(
            ID("cull_clip"), .sizing = { sizing_fixed(400), sizing_fixed(300) }, .color = ui_color(50, 50, 60, 255),
            .clip = { .horizontal = true, .vertical = true, .manual_scroll = true, .scroll_pos = { 0.0f, 0.0f } }) {
            ui_layout_area_id(ID("cull_on"), .sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = onscreen_col,
                              .placed = { .enabled = true, .offset = { 100.0f, 100.0f } }) {
                ui_item_behavior(ID("cull_on"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
            // Placed at (3000, 3000), far past the 1920x1080 screen, so the engine culls it.
            ui_layout_area_id(ID("cull_off"), .sizing = { sizing_fixed(40), sizing_fixed(40) }, .color = offscreen_col,
                              .placed = { .enabled = true, .offset = { 3000.0f, 3000.0f } }) {
                ui_item_behavior(ID("cull_off"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_TRUE_MSG(render_has_rect_rgb(onscreen_col), "the on-screen placed child is drawn (control)");
    ASSERT_FALSE_MSG(render_has_rect_rgb(offscreen_col), "the offscreen placed child is culled — not drawn downstream");

    // The culled child is registered as interactive (item_behavior ran) but never gets a rect, so it is not
    // found and not laid out for interaction. The on-screen control is found, so the scene itself works.
    FlElementState on = ui_get_element_state(ID("cull_on"));
    FlElementState off = ui_get_element_state(ID("cull_off"));
    ASSERT_TRUE_MSG(on.found, "the on-screen placed child is laid out and interactive");
    ASSERT_FALSE_MSG(off.found, "the culled placed child is not laid out (no interaction downstream)");
    ASSERT_FALSE_MSG(off.hot, "the culled placed child never becomes hot");
}

// An element that sets both a background color and a paint span renders the paint over the fill. The order per
// element is background -> own scissor -> paint span -> children -> border. The paint span is clipped to the
// element's own rect, so overshoot is cut even with no ancestor clip.
UTEST(render, canvas_paint_over_fill_and_self_clip) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    const FlColor fill_col = ui_color(30, 30, 120, 255);    // opaque color: background
    const FlColor paint_col = ui_color(40, 200, 120, 255);  // opaque paint, drawn over the fill
    const FlColor child_col = ui_color(230, 90, 60, 255);   // opaque child, drawn over the paint
    const FlColor border_col = ui_color(250, 230, 40, 255); // opaque border, drawn over the paint
    const FlColor clear_col = ui_color(12, 12, 14, 255);    // canvas_fixture_reset clear color

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        // A 100x100 canvas at the origin with a solid color fill and a 4px border. Neither it nor any ancestor
        // clips (the root grow area is unclipped), so only the canvas's own scissor can cut the paint.
        ui_layout_area_id(ID("fillcanvas"), .sizing = { sizing_fixed(100), sizing_fixed(100) }, .color = fill_col,
                          .border = fl_border(border_col, 4)) {
            Painter* p = g_ui_ctx->painter;
            // A paint band that overshoots the element to the right: x 0..200, and the element is 100 wide.
            painter_rect(p, (FlRect) { .x = 0.0f, .y = 0.0f, .w = 200.0f, .h = 50.0f }, paint_col);
            // A child box at the canvas origin, which draws over the paint.
            ui_layout_area(.sizing = { sizing_fixed(30), sizing_fixed(30) }, .color = child_col) {}
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");

    // (50,25) is inside the canvas, under the paint band and not under the child. (80,80) is inside the canvas but
    // below the paint band (y>50) and not under the child. (15,15) is under the child box (0..30). (60,2) is on the
    // canvas's own top border (y 0..4), over the paint band (y<50) and clear of the child (x>30), and the border
    // is drawn last for the element. (150,25) is within the paint band's x extent (0..200) but outside the
    // canvas's own 100px rect, where the canvas's own scissor cuts the overshoot.
    const PxProbe probes[] = {
        { 50, 25, paint_col, 2, true, "paint composes over the element's own color: background" },
        { 80, 80, fill_col, 2, true, "the color: fill is a valid backdrop where paint does not cover" },
        { 15, 15, child_col, 2, true, "children compose over the paint span" },
        { 60, 2, border_col, 2, true, "the element's border composes over the paint span" },
        { 150, 25, paint_col, 2, false, "paint is clipped to the element's own rect, not bled past it" },
        { 150, 25, clear_col, 2, true, "the overshoot region shows the clear color, not bled paint" },
    };
    check_px_probes(utest_result, &buf, probes, sizeof_array(probes));

    ASSERT_EQ_MSG(canvas_custom_command_count(), 1, "fill + paint + child still emits exactly one CUSTOM span");
}

// A floating element declared after the canvas composes over the paint.
UTEST(render, canvas_under_popup) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    const FlColor canvas_col = ui_color(40, 200, 120, 255);
    const FlColor popup_col = ui_color(90, 120, 230, 255);

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        ui_layout_area_id(ID("canvas3"), .sizing = { sizing_fixed(200), sizing_fixed(200) }) {
            painter_rect(g_ui_ctx->painter, (FlRect) { .x = 0.0f, .y = 0.0f, .w = 200.0f, .h = 200.0f }, canvas_col);
        }
        // The popup is a floating element attached to the root at the origin and declared after the canvas, so it
        // composes over the paint.
        ui_floating_area_id(ID("popup"), .attach_to = FlFloatingAttachTo_Root, .z_index = 1,
                            .sizing = { sizing_fixed(100), sizing_fixed(100) }) {
            ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .color = popup_col) {}
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");

    // (50,50) is under the popup (0..100), which composes over the canvas paint.
    ASSERT_TRUE_MSG(px_near(&buf, 50, 50, popup_col, 2), "a later floating element draws over the canvas paint");
    // (150,150) is canvas paint outside the popup and still has the painted fill.
    ASSERT_TRUE_MSG(px_near(&buf, 150, 150, canvas_col, 2), "the canvas paint shows where the popup does not cover");
}

// A custom control is an ordinary layout element (interaction by id through ui_widget_behavior and
// ui_get_element_state) whose fill is painted inside its scope. The case also checks when painting scopes are open.
UTEST(render, canvas_custom_control) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    // Outside any frame or scope no painting scope is open, since paint has nothing to attach to.
    ASSERT_EQ_MSG(painter_scope_depth(g_ui_ctx->painter), 0u, "no painting scope is open outside a layout element");

    const FlColor ctrl_col = ui_color(200, 160, 40, 255);
    const FlLayoutId ctrl_id = ID("custom_control");
    bool painter_valid_in_scope = false;

    ui_update_begin(ctx);
    // A frame is open but no layout element is, so there is still no scope.
    ASSERT_EQ_MSG(painter_scope_depth(g_ui_ctx->painter), 0u, "no painting scope is open before any layout element");

    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_layout_area_id(ctrl_id, .sizing = { sizing_fixed(80), sizing_fixed(80) }) {
            FlSignal sig = ui_widget_behavior(ctrl_id, (FlWidgetConfig) { 0 });
            (void)sig;
            Painter* p = g_ui_ctx->painter;
            painter_valid_in_scope = painter_scope_depth(p) > 0;
            painter_rect(p, (FlRect) { .x = 0.0f, .y = 0.0f, .w = 80.0f, .h = 80.0f }, ctrl_col);
        }
    }
    ASSERT_EQ_MSG(painter_scope_depth(g_ui_ctx->painter), 0u, "no painting scope is open once the element closes");
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_TRUE_MSG(painter_valid_in_scope, "a painting scope was open inside the control's element");

    FlElementState st = ui_get_element_state(ctrl_id);
    ASSERT_TRUE_MSG(st.found, "the custom control is hit-tested as an ordinary layout element");
    ASSERT_EQ_MSG(canvas_custom_command_count(), 1, "painting inside the control emits one CUSTOM span");

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(px_near(&buf, 40, 40, ctrl_col, 2), "the control's painted fill rendered inside its element");
}

// A scope that paints nothing emits no CUSTOM command, and painting many primitives into a scope adds exactly one
// render command (the CUSTOM) and no extra layout element. The two frames render the same layout, so the paint
// is the only difference in command count.
UTEST(render, canvas_command_accounting) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    const FlColor box_col = ui_color(70, 70, 90, 255);
    const FlColor paint_col = ui_color(40, 200, 120, 255);

    // Frame A: the element opens a painting scope but paints nothing, so there is no CUSTOM command. This is the
    // baseline count.
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        ui_layout_area_id(ID("acct"), .sizing = { sizing_fixed(100), sizing_fixed(100) }, .color = box_col) {}
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    const int base_count = ui_get_render_command_count();
    ASSERT_EQ_MSG(canvas_custom_command_count(), 0, "a scope with no painter calls emits no CUSTOM command");

    // Frame B: the same layout, but the element paints several primitives.
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        ui_layout_area_id(ID("acct"), .sizing = { sizing_fixed(100), sizing_fixed(100) }, .color = box_col) {
            Painter* p = g_ui_ctx->painter;
            painter_rect(p, (FlRect) { .x = 0.0f, .y = 0.0f, .w = 100.0f, .h = 100.0f }, paint_col);
            painter_rect(p, (FlRect) { .x = 2.0f, .y = 2.0f, .w = 4.0f, .h = 4.0f }, paint_col);
            painter_circle(p, (FlVec2) { .x = 50.0f, .y = 50.0f }, 6.0f, paint_col);
            painter_rect(p, (FlRect) { .x = 8.0f, .y = 8.0f, .w = 4.0f, .h = 4.0f }, paint_col);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    ASSERT_EQ_MSG(canvas_custom_command_count(), 1, "many primitives in one scope emit exactly one CUSTOM command");
    // Painting added exactly three commands, the CUSTOM span plus the SCISSOR_START and SCISSOR_END that clip it
    // to the element's own rect, and no extra layout elements.
    ASSERT_EQ_MSG(ui_get_render_command_count(), base_count + 3,
                  "painting adds the CUSTOM span plus its own-rect scissor bracket, and zero extra layout elements");
}

// The splice translates element-local paint by the origin of the element's resolved rect. Root padding puts the
// canvas at a non-zero origin, and element-local (0,0) lands at that origin.
UTEST(render, canvas_paint_translated_by_origin) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    const FlColor canvas_col = ui_color(40, 200, 120, 255);

    ui_update_begin(ctx);
    // Root padding pushes its first child to origin (40, 30).
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom, .padding = { .left = 40, .top = 30 }) {
        ui_layout_area_id(ID("offset_canvas"), .sizing = { sizing_fixed(80), sizing_fixed(80) }) {
            painter_rect(g_ui_ctx->painter, (FlRect) { .x = 0.0f, .y = 0.0f, .w = 80.0f, .h = 80.0f },
                         canvas_col); // element-local (0,0)
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");

    // (50,40) is element-local (10,10) once the origin (40,30) is added, so it has the painted fill.
    ASSERT_TRUE_MSG(px_near(&buf, 50, 40, canvas_col, 2), "paint is translated by the element's rect origin");
    // (5,5) is in the root padding, above and left of the canvas, so element-local (0,0) is not at window (0,0).
    ASSERT_FALSE_MSG(px_near(&buf, 5, 5, canvas_col, 2), "the splice adds the origin, not a global (0,0) paste");
}

// ui_mouse_position returns (mouse - origin) / ui_scale in logical (authoring) coordinates, the space the painter
// draws in. It is checked at scale 1 and at scale 0.5, where an offset in physical pixels would differ.
UTEST(render, canvas_element_local_pointer_pos) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx); // ui_scale == 1.0

    // One frame resolves the element's rect, and the query reads the previous frame's rect. Root padding puts the
    // 80x80 canvas at (40, 30).
    ctx->input_state.mouse.x = 0.0f;
    ctx->input_state.mouse.y = 0.0f;
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .padding = { .left = 40, .top = 30 }) {
        ui_layout_area_id(ID("ptr_canvas"), .sizing = { sizing_fixed(80), sizing_fixed(80) }) {
            // A canvas registers by id as an ordinary interactive element, which records its resolved rect. The
            // paint emits a command carrying the element id, and that marks the rect as resolved (found).
            ui_widget_behavior(ID("ptr_canvas"), (FlWidgetConfig) { 0 });
            painter_rect(g_ui_ctx->painter, (FlRect) { .x = 0.0f, .y = 0.0f, .w = 80.0f, .h = 80.0f },
                         ui_color(40, 200, 120, 255));
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlElementState st = ui_get_element_state(ID("ptr_canvas"));
    ASSERT_TRUE_MSG(st.found, "the canvas element is in the layout after one frame");
    ASSERT_NEAR(st.pos.x, 40.0f, 0.5f);
    ASSERT_NEAR(st.pos.y, 30.0f, 0.5f);

    // The pointer at window (50, 40) is element-local (10, 10). At scale 1.0 logical == physical.
    ctx->input_state.mouse.x = 50.0f;
    ctx->input_state.mouse.y = 40.0f;
    FlVec2 local = ui_mouse_position(ID("ptr_canvas"));
    ASSERT_NEAR(local.x, 10.0f, 0.5f);
    ASSERT_NEAR(local.y, 10.0f, 0.5f);

    // An element that is not in the layout has no rect, and the result is {0, 0}.
    FlVec2 missing = ui_mouse_position(ID("ptr_canvas_absent"));
    ASSERT_EQ_MSG((int)missing.x, 0, "no rect → zero element-local x");
    ASSERT_EQ_MSG((int)missing.y, 0, "no rect → zero element-local y");

    // The same authored layout at half scale (540p, ui_scale 0.5). Padding (40,30) logical becomes (20,15)
    // physical, and the 80-logical element is 40 physical. A pointer 10 physical px inside the element is 20
    // logical px inside, because the query divides by ui_scale.
    ctx->layout_width = 960.0f;
    ctx->layout_height = 540.0f;
    UiScalePair fit_540 = ui_reference_fit(540); // the 1080p reference policy: ui_scale 0.5, with the PAL text boost
    ui_set_scale(fit_540.ui_scale, fit_540.text_scale);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 960.0f, .height = 540.0f });

    ctx->input_state.mouse.x = 0.0f;
    ctx->input_state.mouse.y = 0.0f;
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .padding = { .left = 40, .top = 30 }) {
        ui_layout_area_id(ID("ptr_canvas"), .sizing = { sizing_fixed(80), sizing_fixed(80) }) {
            ui_widget_behavior(ID("ptr_canvas"), (FlWidgetConfig) { 0 });
            painter_rect(g_ui_ctx->painter, (FlRect) { .x = 0.0f, .y = 0.0f, .w = 80.0f, .h = 80.0f },
                         ui_color(40, 200, 120, 255));
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlElementState st_half = ui_get_element_state(ID("ptr_canvas"));
    ASSERT_TRUE_MSG(st_half.found, "the canvas element is in the layout at 0.5 scale");
    ASSERT_NEAR(st_half.pos.x, 20.0f, 0.5f); // physical origin
    ASSERT_NEAR(st_half.pos.y, 15.0f, 0.5f);

    ctx->input_state.mouse.x = 30.0f; // 10 physical px inside the element on x
    ctx->input_state.mouse.y = 25.0f; // 10 physical px inside the element on y
    FlVec2 local_half = ui_mouse_position(ID("ptr_canvas"));
    ASSERT_NEAR(local_half.x, 20.0f, 0.5f); // 10 physical / 0.5 == 20 logical
    ASSERT_NEAR(local_half.y, 20.0f, 0.5f);
}

// A painted canvas highlights the half under the cursor, and the highlight follows the pointer as it moves. The
// mouse input is written straight into the context, because the bare render fixture has no input pump. The 80x80
// canvas is at (40, 30), and its halves split at element-local x == 40, which is window x == 80.
UTEST(render, canvas_hover_highlight_tracks_pointer) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx); // ui_scale == 1.0, so element-local logical coordinates are window pixels

    const FlColor base = ui_color(40, 200, 120, 255); // half without the highlight
    const FlColor hl = ui_color(220, 80, 60, 255);    // highlighted half, under the cursor

    // Frame 1 resolves the canvas rect so the next frame's query sees it. The pointer is far away, so nothing is
    // highlighted yet.
    ctx->input_state.mouse.x = 0.0f;
    ctx->input_state.mouse.y = 0.0f;
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .padding = { .left = 40, .top = 30 }) {
        ui_layout_area_id(ID("hover_canvas"), .sizing = { sizing_fixed(80), sizing_fixed(80) }) {
            ui_widget_behavior(ID("hover_canvas"), (FlWidgetConfig) { 0 });
            Painter* p = g_ui_ctx->painter;
            painter_rect(p, (FlRect) { .x = 0.0f, .y = 0.0f, .w = 40.0f, .h = 80.0f }, base);
            painter_rect(p, (FlRect) { .x = 40.0f, .y = 0.0f, .w = 40.0f, .h = 80.0f }, base);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    // Frame 2: the pointer is over the right half (window x 100 is element-local x 60, and 60 >= 40), which
    // lights up.
    ctx->input_state.mouse.x = 100.0f;
    ctx->input_state.mouse.y = 70.0f;
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .padding = { .left = 40, .top = 30 }) {
        ui_layout_area_id(ID("hover_canvas"), .sizing = { sizing_fixed(80), sizing_fixed(80) }) {
            ui_widget_behavior(ID("hover_canvas"), (FlWidgetConfig) { 0 });
            FlVec2 local = ui_mouse_position(ID("hover_canvas"));
            bool right = local.x >= 40.0f;
            Painter* p = g_ui_ctx->painter;
            painter_rect(p, (FlRect) { .x = 0.0f, .y = 0.0f, .w = 40.0f, .h = 80.0f }, right ? base : hl);
            painter_rect(p, (FlRect) { .x = 40.0f, .y = 0.0f, .w = 40.0f, .h = 80.0f }, right ? hl : base);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    ASSERT_TRUE_MSG(px_near(&buf, 100, 70, hl, 2), "right half (under cursor) is highlighted");
    ASSERT_TRUE_MSG(px_near(&buf, 60, 70, base, 2), "left half (away from cursor) stays base color");

    // Frame 3: the pointer moves to the left half (window x 60 is element-local x 20), and the highlight follows.
    ctx->input_state.mouse.x = 60.0f;
    ctx->input_state.mouse.y = 70.0f;
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .padding = { .left = 40, .top = 30 }) {
        ui_layout_area_id(ID("hover_canvas"), .sizing = { sizing_fixed(80), sizing_fixed(80) }) {
            ui_widget_behavior(ID("hover_canvas"), (FlWidgetConfig) { 0 });
            FlVec2 local = ui_mouse_position(ID("hover_canvas"));
            bool right = local.x >= 40.0f;
            Painter* p = g_ui_ctx->painter;
            painter_rect(p, (FlRect) { .x = 0.0f, .y = 0.0f, .w = 40.0f, .h = 80.0f }, right ? base : hl);
            painter_rect(p, (FlRect) { .x = 40.0f, .y = 0.0f, .w = 40.0f, .h = 80.0f }, right ? hl : base);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf),
                    "read_pixels failed (frame 3)");
    ASSERT_TRUE_MSG(px_near(&buf, 60, 70, hl, 2), "highlight tracked the pointer to the left half");
    ASSERT_TRUE_MSG(px_near(&buf, 100, 70, base, 2), "right half reverts to base once the cursor leaves it");
}

// The tooltip's floating label is shown only after the pointer has rested on the anchor, timed by the real
// stationary timer and not by the eased hover value. It hides as soon as the anchor is no longer hovered. The
// case checks the tooltip's floating element with Clay_GetElementData and compares no pixels.

// One tooltip frame: an 80x80 anchor at window (40,30), with ui_tooltip emitted right after it. The caller sets
// the mouse position and the dwell timer before calling.
static void tooltip_emit_frame(FlUiCtx* ctx, FlLayoutId anchor_id, FlString text) {
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .padding = { .left = 40, .top = 30 }) {
        // The background colour makes the anchor emit a render command, so item-state hit testing runs on it. An
        // empty layout area emits none and would never become hot.
        ui_layout_area_id(anchor_id, .sizing = { sizing_fixed(80), sizing_fixed(80) },
                          .color = ui_color(40, 200, 120, 255)) {
            ui_widget_behavior(anchor_id, (FlWidgetConfig) { 0 });
        }
        ui_tooltip(anchor_id, text);
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

// True if the tooltip's floating element is in the layout just produced. The tip id is derived as in
// ui_tooltip_impl (anchor name, with the anchor hash as sub-id), and Clay reports whether it was laid out this
// frame.
static bool tooltip_present(FlLayoutId anchor_id) {
    u32 anchor_clay_id = ui_layout_id_to_clay_id(anchor_id);
    FlLayoutId tip_id = { .name = anchor_id.name, .id = (int)anchor_clay_id };
    Clay_ElementData data = Clay_GetElementData((Clay_ElementId) { .id = ui_layout_id_to_clay_id(tip_id) });
    return data.found;
}

UTEST(render, tooltip_reveals_after_dwell_hides_on_unhover) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    FlLayoutId anchor = ID("tooltip_anchor");
    FlString text = FL_S("Tooltip text");

    // Let hover settle with the dwell timer held at 0. The pointer is on the anchor (rect 40..120 x 30..110), but
    // no dwell has elapsed, so the tooltip does not show. Hot propagates next_hot_id -> hot_id -> state.hot, so
    // it takes a few frames before hot reads 1 during emit.
    ctx->input_state.mouse.x = 80.0f;
    ctx->input_state.mouse.y = 70.0f;
    for (int i = 0; i < 3; i++) {
        ctx->input_state.mouse.stationary_timer = 0.0f;
        tooltip_emit_frame(ctx, anchor, text);
    }
    ASSERT_TRUE_MSG(ui_get_element_state(anchor).hot, "anchor is hovered while the pointer rests on it");
    ASSERT_FALSE_MSG(tooltip_present(anchor), "no tooltip before the dwell threshold is reached");

    // With the same hover and the timer past the 0.5s default, the tooltip is shown.
    ctx->input_state.mouse.stationary_timer = 0.6f;
    tooltip_emit_frame(ctx, anchor, text);
    ASSERT_TRUE_MSG(tooltip_present(anchor), "tooltip appears once the pointer has dwelled past the threshold");

    // Move the pointer off the anchor. hot trails the pointer by two frames (next_hot_id -> hot_id ->
    // state.hot), so a few frames let it settle. The dwell timer stays high, so the tooltip hides because hover
    // was lost and not because of the timer.
    ctx->input_state.mouse.x = 600.0f;
    ctx->input_state.mouse.y = 500.0f;
    ctx->input_state.mouse.stationary_timer = 5.0f;
    for (int i = 0; i < 3; i++) {
        tooltip_emit_frame(ctx, anchor, text);
    }
    ASSERT_FALSE_MSG(ui_get_element_state(anchor).hot, "anchor is no longer hot after the pointer leaves");
    ASSERT_FALSE_MSG(tooltip_present(anchor), "tooltip hides once the anchor is no longer hovered");
}

// A painter 1px line renders crisp through the software backend at a fractional UI scale. The snap puts it on a
// whole device row, so that row is solid and its neighbours are pure background. An unsnapped line would straddle
// two rows at partial coverage and fail both neighbour checks.
UTEST(render, snapped_line_crisp_at_fractional_dpi) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);
    ui_set_scale(1.25f, 1.25f);
    ASSERT_TRUE_MSG(ctx->ui_scale == 1.25f, "composed scale is 1.25 for the crispness render");
    ASSERT_TRUE_MSG(ctx->text_scale == 1.25f, "ui_set_scale stores the text scale verbatim too");

    const FlColor line_col = ui_color(230, 90, 60, 255);

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        // The canvas is at the device origin (0,0). A hairline at local y=9 is at device y = 9 * 1.25 = 11.25,
        // which snaps to row 11. Local x 0..100 covers device x 0..125, so x=50 is in the middle of the line.
        ui_layout_area_id(ID("canvas"), .sizing = { sizing_fixed(200), sizing_fixed(200) }) {
            painter_hline(g_ui_ctx->painter, 0.0f, 9.0f, 100.0f, line_col);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    FlPixelReadback buf;
    ASSERT_TRUE_MSG(render_seam_read_pixels(&ctx->shared->renderer, ctx->surface, &buf), "read_pixels failed");
    const PxProbe probes[] = {
        { 50, 11, line_col, 2, true, "the snapped line row (11) is solid — no blur" },
        { 50, 10, line_col, 2, false, "the row above the line (10) is pure background" },
        { 50, 12, line_col, 2, false, "the row below the line (12) is pure background" },
    };
    check_px_probes(utest_result, &buf, probes, sizeof_array(probes));
}
