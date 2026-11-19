// UI progress bar, wheel input and drag threshold tests

#include "widgets/common.h"
#include <math.h>

// The progress bar only paints: a track plus a fill sized to a normalized fraction in [0,1]. The widget has no id,
// so each rect is found by its theme color (track = surface.elevated, fill = interactive.base). The fill width as
// a fraction of the track equals the requested fraction, and out-of-range input clamps.

// Track and fill colors that nothing else in the snapshot uses.
static const FlColor PROGRESS_TRACK_COLOR = { 40, 40, 44, 255 };

static const FlColor PROGRESS_FILL_COLOR = { 10, 200, 80, 255 };

typedef struct ProgressFrameState {
    float fraction;
} ProgressFrameState;

static void progress_frame(void* user_data) {
    ProgressFrameState* st = (ProgressFrameState*)user_data;
    static const FlThemeColors theme = {
        .surface.elevated = { 40, 40, 44, 255 },  // track (PROGRESS_TRACK_COLOR)
        .interactive.base = { 10, 200, 80, 255 }, // fill (PROGRESS_FILL_COLOR)
        .text.primary = { 240, 240, 240, 255 },
    };
    ui_set_theme(g_ui_ctx, &theme);
    // A padded full-window column holding only the bar, so the track grows to one known width.
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .padding = padding_all(20)) {
        ui_progress_bar(st->fraction);
    }
}

// Width of the widest rectangle painted in want, or -1 if none was emitted.
static f32 progress_rect_width(FlColor want) {
    f32 widest = -1.0f;
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (!ui_get_render_command(i, &cmd) || cmd.type != UiRenderCmd_Rectangle) {
            continue;
        }
        if (slot_override_color_eq(cmd.bg_color, want) && cmd.width > widest) {
            widest = cmd.width;
        }
    }
    return widest;
}

UTEST(ui_progress_bar, determinate_fill_and_clamping) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("progress"), .width = 400, .height = 300, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    ProgressFrameState st = { .fraction = 0.0f };

    st.fraction = 0.0f;
    fl_app_tick(app, &st, progress_frame);
    f32 track_w = progress_rect_width(PROGRESS_TRACK_COLOR);
    ASSERT_TRUE_MSG(track_w > 0.0f, "track is always painted");
    ASSERT_TRUE_MSG(progress_rect_width(PROGRESS_FILL_COLOR) < 0.0f, "a zero fraction paints no fill");

    st.fraction = 0.5f;
    fl_app_tick(app, &st, progress_frame);
    track_w = progress_rect_width(PROGRESS_TRACK_COLOR);
    f32 fill_w = progress_rect_width(PROGRESS_FILL_COLOR);
    ASSERT_TRUE_MSG(track_w > 0.0f && fill_w > 0.0f, "half: both track and fill paint");
    ASSERT_TRUE_MSG(fabsf(fill_w / track_w - 0.5f) < 0.03f, "half: fill spans ~50% of the track");

    st.fraction = 1.0f;
    fl_app_tick(app, &st, progress_frame);
    track_w = progress_rect_width(PROGRESS_TRACK_COLOR);
    fill_w = progress_rect_width(PROGRESS_FILL_COLOR);
    ASSERT_TRUE_MSG(fabsf(fill_w / track_w - 1.0f) < 0.03f, "full: fill spans ~100% of the track");

    st.fraction = 1.5f;
    fl_app_tick(app, &st, progress_frame);
    track_w = progress_rect_width(PROGRESS_TRACK_COLOR);
    fill_w = progress_rect_width(PROGRESS_FILL_COLOR);
    ASSERT_TRUE_MSG(fabsf(fill_w / track_w - 1.0f) < 0.03f, "over-range fraction clamps to a full fill");

    st.fraction = -0.5f;
    fl_app_tick(app, &st, progress_frame);
    ASSERT_TRUE_MSG(progress_rect_width(PROGRESS_FILL_COLOR) < 0.0f, "negative fraction clamps to an empty fill");

    fl_app_destroy(app);
}

// Two nested Scroll Targets register through ui_scroll_target(). The wheel goes only to the inner, hot one and the
// outer reads {0,0}. A Scroll Target with no Clickable or Draggable flag still reports the wheel, because the
// read checks item->state.hot and not signal.hovered, and a press over it still claims active_id.

// outer is the full 400x400 area and inner is a 200x200 child at its top-left. (100,100) is inside both, so both are
// under the cursor and the deeper one (inner, rendered last) wins arbitration.
static void wheel_nested_frame(FlUiCtx* ctx) {
    const FlColor outer_color = ui_color(40, 40, 60, 255);
    const FlColor inner_color = ui_color(60, 90, 60, 255);

    ui_update_begin(ctx);
    ui_layout_area_id(ID("wheel_outer"), .sizing = { .width = sizing_fixed(400), .height = sizing_fixed(400) },
                      .color = outer_color) {
        ui_scroll_target(ID("wheel_outer"));
        ui_layout_area_id(ID("wheel_inner"), .sizing = { .width = sizing_fixed(200), .height = sizing_fixed(200) },
                          .color = inner_color) {
            ui_scroll_target(ID("wheel_inner"));
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(wheel_input, inner_shadows_outer) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    // The case uses an app-owned context and not the shared render fixture: fl_app_create() and ui_create() set
    // the global UI context anew, and an earlier fl_app teardown clears it. Headless inject_wheel is deferred, so
    // the wheel cache is set directly and ui_update_begin/end are called here. fl_app_tick would reset the wheel
    // each frame.
    FlAppSettings settings = { .window_title = FL_S("wheel_input"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = g_ui_ctx;
    ASSERT_TRUE_MSG(ctx != nullptr, "fl_app_create did not establish a global UI context");

    const int layout_w = 400;
    const int layout_h = 400;
    ctx->ui_scale = 1.0f; // sizing_fixed() multiplies by ui_scale, so 1.0 keeps the rects exact
    ctx->layout_width = (float)layout_w;
    ctx->layout_height = (float)layout_h;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)layout_w, .height = (float)layout_h });

    // A Scroll Target with no Clickable or Draggable flag gets only the wheel, through state.hot.
    const u32 inner_clay_id = ui_layout_id_to_clay_id(ID("wheel_inner"));

    // Frame 1: cursor over inner, no wheel yet. This registers both targets and lays them out. hot_id resolves to
    // inner at the end of the frame, but state.hot is not set until the next frame's update.
    ctx->input_state.mouse.x = 100.0f;
    ctx->input_state.mouse.y = 100.0f;
    ctx->input_state.mouse.wheel_x_cached = 0.0f;
    ctx->input_state.mouse.wheel_y_cached = 0.0f;
    ctx->mouse_pressed_this_frame = false;
    ctx->mouse_released_this_frame = false;

    wheel_nested_frame(ctx);

    ASSERT_EQ_MSG(inner_clay_id, ctx->hot_id, "after frame 1 the inner Scroll Target must be the hot id");

    // Frame 2: inject a downward wheel tick. ui_update_begin sees that the previous frame's hot id is a
    // ScrollTarget, so it withholds the wheel from Clay's scroll containers and leaves the cache for the scoped
    // read.
    ctx->input_state.mouse.wheel_y_cached = 120.0f;

    wheel_nested_frame(ctx);

    FlVec2 inner_wheel = ui_mouse_wheel_delta(ID("wheel_inner"));
    FlVec2 outer_wheel = ui_mouse_wheel_delta(ID("wheel_outer"));
    ASSERT_NEAR_CHECKS(
        NEAR_CHECK_TOL(inner_wheel.y, 120.0, 0.001, "inner (hot) Scroll Target must read the wheel delta"),
        NEAR_CHECK_TOL(inner_wheel.x, 0.0, 0.001, "no horizontal wheel was injected"),
        NEAR_CHECK_TOL(outer_wheel.y, 0.0, 0.001, "outer Scroll Target is shadowed by the inner — reads {0,0}"),
        NEAR_CHECK_TOL(outer_wheel.x, 0.0, 0.001, "outer Scroll Target is shadowed by the inner — reads {0,0}"));

    // The read does not depend on signal.hovered. This element never declared Clickable or Draggable, so
    // signal.hovered is always false for it, and the scoped read still returned the wheel.
    ItemState* inner_item = hashmap_get(&ctx->shared->item_state, inner_clay_id);
    ASSERT_TRUE_MSG(inner_item != nullptr, "inner Scroll Target must be present in item_state");
    ASSERT_TRUE_MSG(inner_item->state.hot, "the scoped read keys off item->state.hot, which must be set");
    ASSERT_FALSE_MSG(inner_item->last_signal.hovered,
                     "a pure Scroll Target never sets signal.hovered, yet still reports wheel via the scoped read");

    // A press over such a Scroll Target still claims active_id, because the hot -> active promotion in
    // ui_update_end uses hot_id, whatever flags the item has.
    ctx->mouse_pressed_this_frame = true;
    wheel_nested_frame(ctx);
    ASSERT_EQ_MSG(inner_clay_id, ctx->active_id, "a press over a pure Scroll Target still sets active_id");

    fl_app_destroy(app);
}

// A plain layout region that never declares itself a Scroll Target leaves the wheel unconsumed for the frame, and
// the injected wheel is still readable from the cache afterwards.

UTEST(wheel_input, unflagged_region_keeps_unconsumed_wheel) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("wheel_unflagged"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = g_ui_ctx;
    ASSERT_TRUE_MSG(ctx != nullptr, "fl_app_create did not establish a global UI context");

    ctx->ui_scale = 1.0f;
    ctx->layout_width = 400.0f;
    ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });

    // The cursor is over a plain layout area that declares no Scroll Target and no clip, so nothing consumes the
    // wheel.
    ctx->input_state.mouse.x = 100.0f;
    ctx->input_state.mouse.y = 100.0f;
    ctx->input_state.mouse.wheel_x_cached = 0.0f;
    ctx->input_state.mouse.wheel_y_cached = 120.0f;
    ctx->mouse_pressed_this_frame = false;
    ctx->mouse_released_this_frame = false;

    // hot_id is not a Scroll Target, so ui_update_begin takes the Clay branch, which leaves the cache untouched.
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_fixed(400), .height = sizing_fixed(400) },
                   .color = ui_color(30, 30, 40, 255)) {}
    ui_update_end(ctx, 1.0f / 60.0f);

    // Read through the unscoped ui_get_mouse_wheel() and the cached fields. mouse_wheel_delta(id) answers only
    // for a hot Scroll Target and would read zero for this region.
    FlVec2 wheel = ui_get_mouse_wheel();
    ASSERT_NEAR_CHECKS(
        NEAR_CHECK_TOL(wheel.y, 120.0, 0.001, "an unconsumed wheel over a plain region survives the frame"),
        NEAR_CHECK_TOL(wheel.x, 0.0, 0.001, "no horizontal wheel was injected"),
        NEAR_CHECK_TOL(ctx->input_state.mouse.wheel_y_cached, 120.0, 0.001,
                       "the wheel cache itself is left intact by ui_update_begin"));

    fl_app_destroy(app);
}

// A Clay-managed scroll container with a nested Scroll Target widget (the slider has Clickable, Draggable and
// ScrollTarget) routes the wheel to exactly one of them. When the widget is hot, ui_update_begin withholds the
// wheel from Clay and the widget reads it, and the container does not scroll.

// A 400x400 vertical clip container whose content is taller than it (a 200x40 slider at the top plus an 800px
// filler), so Clay can scroll it. The slider is the deeper element under the cursor, so it wins hot arbitration.
static FlSignal wheel_dbl_frame(FlUiCtx* ctx) {
    FlSignal sig = { 0 };
    const FlItemFlags slider_flags = FlItemFlags_clickable | FlItemFlags_draggable | FlItemFlags_scroll_target;
    ui_update_begin(ctx);
    ui_layout_area_id(ID("wheel_scroll_box"), .sizing = { sizing_fixed(400), sizing_fixed(400) },
                      .color = ui_color(30, 30, 40, 255), .direction = FlLayoutDirection_TopToBottom,
                      .clip = { .vertical = true }) {
        ui_layout_area_id(ID("wheel_slider"), .sizing = { sizing_fixed(200), sizing_fixed(40) },
                          .color = ui_color(90, 90, 140, 255)) {
            sig = ui_item_behavior(ID("wheel_slider"), slider_flags, (FlWidgetConfig) { 0 });
        }
        ui_layout_area(.sizing = { sizing_fixed(200), sizing_fixed(800) }, .color = ui_color(50, 50, 70, 255)) {}
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    return sig;
}

UTEST(wheel_input, container_and_scroll_target_no_double_consume) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("wheel_dbl"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = g_ui_ctx;
    ASSERT_TRUE_MSG(ctx != nullptr, "fl_app_create did not establish a global UI context");

    ctx->ui_scale = 1.0f;
    ctx->layout_width = 400.0f;
    ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });

    const u32 container_clay_id = ui_layout_id_to_clay_id(ID("wheel_scroll_box"));
    const u32 slider_clay_id = ui_layout_id_to_clay_id(ID("wheel_slider"));

    // The cursor at (100,20) is on the slider. No wheel yet.
    ctx->input_state.mouse.x = 100.0f;
    ctx->input_state.mouse.y = 20.0f;
    ctx->input_state.mouse.wheel_x_cached = 0.0f;
    ctx->input_state.mouse.wheel_y_cached = 0.0f;
    ctx->mouse_pressed_this_frame = false;
    ctx->mouse_released_this_frame = false;

    // Frames 1 and 2 settle the hot state. hot_id resolves to the slider at the end of frame 1, but
    // signal.hovered reads item->state.hot, which item_state_update fills from the previous frame's hot_id, so
    // it becomes true only on frame 3. signal.wheel_delta, the second place the wheel could be read, needs that.
    wheel_dbl_frame(ctx);
    ASSERT_EQ_MSG(slider_clay_id, ctx->hot_id, "after frame 1 the nested Scroll Target slider must be the hot id");
    wheel_dbl_frame(ctx);

    // Frame 3: inject a downward wheel. hot_id is the slider, a Scroll Target, so ui_update_begin gives Clay a
    // zero delta and leaves the cache for the widget. The slider is hovered now and reads signal.wheel_delta.
    ctx->input_state.mouse.wheel_y_cached = 120.0f;
    FlSignal slider_sig = wheel_dbl_frame(ctx);

    ASSERT_NEAR_MSG(120.0, (double)slider_sig.wheel_delta.y, 0.001,
                    "the hot Scroll Target slider consumes the wheel via its signal");
    FlVec2 scoped = ui_mouse_wheel_delta(ID("wheel_slider"));
    ASSERT_NEAR_MSG(120.0, (double)scoped.y, 0.001, "the scoped read agrees the slider owns the wheel");

    Clay_ScrollContainerData scd = Clay_GetScrollContainerData((Clay_ElementId) { .id = container_clay_id });
    ASSERT_TRUE_MSG(scd.found, "the clip element is a queryable Clay scroll container");
    ASSERT_TRUE_MSG(scd.contentDimensions.height > scd.scrollContainerDimensions.height,
                    "the container content is taller than its viewport, so Clay COULD have scrolled it");
    ASSERT_NEAR_MSG(0.0, (double)scd.scrollPosition->y, 0.001,
                    "the container must NOT also scroll — the wheel routes to the slider only, no double-consume");

    fl_app_destroy(app);
}

// Click-vs-drag threshold (FlItem_DragThreshold). An item with the flag starts dragging only once the cursor has
// moved a few pixels from the press point, so a press and release in place is a click and not a one-pixel drag.
// The cases run the real input layer (ui_input_update) every frame, so signal.dragging depends on the real
// drag-distance tracking.

typedef struct DragGesture {
    FlSignal move;    // signal from the frame the cursor moved with the button held
    FlSignal release; // signal from the release frame
} DragGesture;

static void drag_threshold_frame(FlUiCtx* ctx, PointerFrame pointer, FlSignal* out_sig) {
    pointer_frame_apply(ctx, pointer);

    FlSignal sig = { 0 };
    ui_update_begin(ctx);
    ui_layout_area_id(ID("drag_item"), .sizing = { sizing_fixed(200), sizing_fixed(200) },
                      .color = ui_color(40, 200, 120, 255)) {
        sig = ui_item_behavior(ID("drag_item"), FlItemFlags_clickable | FlItemFlags_draggable | FlItem_DragThreshold,
                               (FlWidgetConfig) { 0 });
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    if (out_sig != nullptr) {
        *out_sig = sig;
    }
}

// Presses at the item centre (100,100), moves by move_dx while held, then releases.
static DragGesture drag_threshold_run(FlUiCtx* ctx, float move_dx) {
    const PointerFrame centre = { .x = 100.0f, .y = 100.0f };
    const PointerFrame moved = { .x = 100.0f + move_dx, .y = 100.0f };

    // Two warm-up frames: the item becomes hot_id at the end of frame 1 and its state.hot is set on frame 2. A
    // press needs signal.hovered, which reads state.hot.
    drag_threshold_frame(ctx, centre, nullptr);
    drag_threshold_frame(ctx, centre, nullptr);
    // Press in place. active_id is set at the end of the frame, so dragging cannot fire on the press frame.
    drag_threshold_frame(ctx, (PointerFrame) { .x = centre.x, .y = centre.y, .held = true, .pressed = true }, nullptr);

    DragGesture g = { 0 };
    drag_threshold_frame(ctx, (PointerFrame) { .x = moved.x, .y = moved.y, .held = true }, &g.move);
    drag_threshold_frame(ctx, (PointerFrame) { .x = moved.x, .y = moved.y, .released = true }, &g.release);
    return g;
}

// Both scenarios share one app and the same item id. Each gesture's warm-up frames set the hot state again and
// the press resets the drag-distance tracking, so nothing carries over.
UTEST(drag_threshold, click_vs_drag) {
    FlAppSettings settings = { .window_title = FL_S("drag_threshold"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = g_ui_ctx;
    ctx->ui_scale = 1.0f; // sizing_fixed() multiplies by ui_scale, so 1.0 puts the item at 0,0
    ctx->layout_width = 400.0f;
    ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });

    // 2px of travel is under UI_MOUSE_DRAG_THRESHOLD (4px), so the gesture stays a click.
    DragGesture click = drag_threshold_run(ctx, 2.0f);
    ASSERT_FALSE_MSG(click.move.dragging, "a sub-threshold move must not latch dragging");
    ASSERT_NEAR_MSG(0.0, (double)click.move.drag_delta.x, 0.001, "no drag means no drag_delta");
    ASSERT_FALSE_MSG(click.release.dragging, "a sub-threshold release still must not drag");
    ASSERT_TRUE_MSG(click.release.clicked, "a press-and-release under the threshold reads as a click");

    // 10px of travel is well past the threshold.
    DragGesture drag = drag_threshold_run(ctx, 10.0f);
    ASSERT_TRUE_MSG(drag.move.dragging, "a move past the threshold latches dragging");
    ASSERT_NEAR_MSG(10.0, (double)drag.move.drag_delta.x, 0.001, "drag_delta tracks movement from the press point");

    fl_app_destroy(app);
}
