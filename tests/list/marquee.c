// ui_list marquee (rubber-band) selection tests

#include "list/common.h"

// The marquee (rubber-band) gesture. A drag begun on empty list space sweeps a rectangle and selects the rows it
// covers. Dragging past the bottom edge auto-scrolls and keeps extending the band, so the covered span reaches
// rows that were not instantiated on the press frame. Coverage is computed as model positions from content Y and
// never from per-item state.
//
// The case uses an app-owned context and the real input layer every frame (ui_input_update), so the click-vs-drag
// threshold and the empty-space hit test are the real ones. fl_app_destroy tears down the global UI context, so
// a render-fixture case must not follow it. That is why it sits with the other fl_app cases.

// Stable id of position p in this fixture's model (1-based, as in the model's ids).
#define MARQUEE_ID_AT(p) ((u64)((p) + 1))

// The press point, screen y = 30, is in the gap between row 0 ([0,24]) and row 1 ([36,60]). That is empty space,
// so the gesture arms as a marquee and the anchor lands just below row 0.
#define MARQUEE_PRESS_X 150.0f
#define MARQUEE_PRESS_Y 30.0f

typedef struct MarqueeCase {
    ListApp app;
    ListModel model;
} MarqueeCase;

// A 100-row List in a fixed 120px viewport at the window origin. row_height 24 + gap 12 gives a 36px stride, so
// the gap after row 0 is screen y [24, 36], empty space a press can land in.
static void marquee_case_init(MarqueeCase* c, FlLayoutId list) {
    list_model_init(&c->model, list, 100, 0.0f);
    c->model.config.gap = 12.0f;
    c->model.set = selection_set_create(64);
}

static FlListRange marquee_step_with(FlUiCtx* ctx, const ListModel* m, ListBoxedStep step) {
    step.root = ID("marquee_root");
    return list_boxed_step(ctx, m, &step);
}

static FlListRange marquee_step(FlUiCtx* ctx, const ListModel* m, ListPointer pointer) {
    return marquee_step_with(ctx, m, (ListBoxedStep) { .pointer = pointer });
}

static FlListRange marquee_ctrl_step(FlUiCtx* ctx, const ListModel* m, ListPointer pointer) {
    return marquee_step_with(ctx, m, (ListBoxedStep) { .pointer = pointer, .modifiers = FlKeyModifier_Control });
}

// Runs the steps in order and returns the visible range of the last one.
static FlListRange marquee_play(FlUiCtx* ctx, const ListModel* m, const ListPointer* steps, size_t count) {
    FlListRange range = { 0, 0, 0 };
    for (size_t i = 0; i < count; i++) {
        range = marquee_step(ctx, m, steps[i]);
    }
    return range;
}

// Warm-up with the button up. Frame 1 lets list_uniform measure the container. Frame 2 emits the real visible
// window and resolves the container rect the marquee reads. The visible range shows that later rows are off
// screen. Then (1) Click-vs-drag threshold: a press, a 2px move (under the threshold) and a release on empty space
// read as a click and leave the selection untouched.
static void marquee_phase_click_threshold(int* utest_result, MarqueeCase* c) {
    FlUiCtx* ctx = c->app.ctx;
    const ListModel* m = &c->model;
    const ListPointer warm_up[] = { list_ptr_hover(150.0f, 50.0f), list_ptr_hover(150.0f, 50.0f) };
    FlListRange visible = marquee_play(ctx, m, warm_up, sizeof_array(warm_up));
    ASSERT_TRUE_MSG(visible.end < 11, "viewport must virtualize: position 11 is off-screen at the start");

    const float y2 = MARQUEE_PRESS_Y + 2.0f; // 2px < thr
    const ListPointer tap[] = {
        list_ptr_press(MARQUEE_PRESS_X, MARQUEE_PRESS_Y),
        list_ptr_drag(MARQUEE_PRESS_X, y2),
        list_ptr_release(MARQUEE_PRESS_X, y2),
    };
    marquee_play(ctx, m, tap, sizeof_array(tap));
    ASSERT_EQ_MSG((u64)0, selection_set_len(m->set), "a sub-threshold empty-space press+release does not marquee");
}

// (2) A marquee drag: press on empty space, then drag well past the threshold but still inside the viewport. It
// selects only the rows the band sweeps. Row 0, above the anchor, stays unselected.
static void marquee_phase_drag_inside_viewport(int* utest_result, MarqueeCase* c) {
    const ListPointer drag[] = {
        list_ptr_press(MARQUEE_PRESS_X, MARQUEE_PRESS_Y), // arms
        list_ptr_drag(MARQUEE_PRESS_X, 100.0f),           // inside the viewport
    };
    marquee_play(c->app.ctx, &c->model, drag, sizeof_array(drag));
    const FlSelectionSet* set = c->model.set;
    ASSERT_TRUE_MSG(selection_set_contains(set, MARQUEE_ID_AT(1)), "the marquee covers row 1");
    ASSERT_TRUE_MSG(selection_set_contains(set, MARQUEE_ID_AT(2)), "the marquee covers row 2");
    ASSERT_FALSE_MSG(selection_set_contains(set, MARQUEE_ID_AT(0)), "row 0, above the anchor, is not covered");
    ASSERT_FALSE_MSG(selection_set_contains(set, MARQUEE_ID_AT(3)), "rows below the band are not covered yet");
}

// (3) Auto-scroll: hold the cursor well past the bottom edge for many frames. The list scrolls and the band keeps
// extending to rows that were not instantiated on the press frame.
static void marquee_phase_autoscroll(int* utest_result, MarqueeCase* c) {
    FlUiCtx* ctx = c->app.ctx;
    const FlSelectionSet* set = c->model.set;
    for (int f = 0; f < 60; f++) {
        marquee_step(ctx, &c->model, list_ptr_drag(MARQUEE_PRESS_X, 220.0f));
    }
    ASSERT_TRUE_MSG(selection_set_contains(set, MARQUEE_ID_AT(1)), "the anchor end stays selected through scroll");
    ASSERT_FALSE_MSG(selection_set_contains(set, MARQUEE_ID_AT(0)), "row 0 above the anchor stays unselected");
    ASSERT_TRUE_MSG(selection_set_contains(set, MARQUEE_ID_AT(11)),
                    "auto-scroll extends the band to an initially off-screen row (id resolved across virtualization)");

    u64 len_during = selection_set_len(set);
    ASSERT_TRUE_MSG(len_during >= 11, "the auto-scrolled band covers many rows");
    for (u64 p = 1; p <= 11; p++) {
        ASSERT_TRUE_MSG(selection_set_contains(set, MARQUEE_ID_AT(p)), "the covered span is contiguous");
    }

    marquee_step(ctx, &c->model, list_ptr_release(MARQUEE_PRESS_X, 220.0f));
    u64 len_after_release = selection_set_len(set);
    marquee_step(ctx, &c->model, list_ptr_hover(MARQUEE_PRESS_X, 220.0f));
    ASSERT_EQ_MSG(len_after_release, selection_set_len(set), "the selection is stable once the marquee is released");
}

// (4) Additive marquee: with Control held, the swept band is added to the existing selection. A new list id starts
// at scroll 0. A sentinel id far outside the band survives the drag, because the ClearAll of replace mode is
// skipped.
static void marquee_phase_additive(int* utest_result, MarqueeCase* c) {
    FlUiCtx* ctx = c->app.ctx;
    FlSelectionSet* set = c->model.set;
    c->model.list = ID("marquee_list2");
    const u64 sentinel = MARQUEE_ID_AT(98); // off-screen, far below the band
    selection_set_clear_all(set);
    selection_set_set(set, sentinel, true);

    // Warm up with the cursor already at the press point in the gap. If it hovered a row first, that row would
    // still be hot when the press lands (hot lags one frame) and the press would count as a row click.
    const ListPointer warm_up = list_ptr_hover(MARQUEE_PRESS_X, MARQUEE_PRESS_Y);
    marquee_step(ctx, &c->model, warm_up);
    marquee_step(ctx, &c->model, warm_up);
    marquee_ctrl_step(ctx, &c->model, list_ptr_press(MARQUEE_PRESS_X, MARQUEE_PRESS_Y));
    marquee_ctrl_step(ctx, &c->model, list_ptr_drag(MARQUEE_PRESS_X, 100.0f));
    ASSERT_TRUE_MSG(selection_set_contains(set, MARQUEE_ID_AT(1)), "additive marquee covers the swept rows");
    ASSERT_TRUE_MSG(selection_set_contains(set, MARQUEE_ID_AT(2)), "additive marquee covers the swept rows");
    ASSERT_TRUE_MSG(selection_set_contains(set, sentinel),
                    "additive (ctrl) marquee unions onto the existing selection — the sentinel id survives");
    marquee_ctrl_step(ctx, &c->model, list_ptr_release(MARQUEE_PRESS_X, 100.0f));
}

UTEST(list_marquee, rubber_band_with_autoscroll) {
    MarqueeCase c;
    ASSERT_TRUE_MSG(list_app_open(&c.app, FL_S("list_marquee")), "fl_app_create(headless) failed");
    marquee_case_init(&c, ID("marquee_list"));

    LIST_RUN_PHASE(marquee_phase_click_threshold(utest_result, &c));
    LIST_RUN_PHASE(marquee_phase_drag_inside_viewport(utest_result, &c));
    LIST_RUN_PHASE(marquee_phase_autoscroll(utest_result, &c));
    LIST_RUN_PHASE(marquee_phase_additive(utest_result, &c));

    selection_set_destroy(c.model.set);
    list_app_close(&c.app);
}

// The press that arms a marquee lands on empty list space, where no row consumes it, and does not blur the list's
// keyboard focus. Only a press outside the viewport blurs. Focus is observed through result.focused_id: the
// cursor moves on Nav-Down only while the list holds keyboard focus.

static FlListResult marquee_nav_down_frame(FlUiCtx* ctx, const ListModel* m, ListPointer pointer) {
    FlListResult result = { 0 };
    marquee_step_with(ctx, m, (ListBoxedStep) { .pointer = pointer, .nav_down = true, .out_result = &result });
    return result;
}

UTEST(list_marquee, press_keeps_keyboard_focus) {
    MarqueeCase c;
    ASSERT_TRUE_MSG(list_app_open(&c.app, FL_S("list_marquee_focus")), "fl_app_create(headless) failed");
    marquee_case_init(&c, ID("marquee_focus_list"));
    FlUiCtx* ctx = c.app.ctx;

    // Warm up hovering row 0 (screen y [0, 24]). Frame 1 measures the container. Frame 2 resolves the rect and
    // makes row 0 hot, so the press counts as a row click. Then click row 0. This focuses the list and puts the
    // keyboard cursor on id 1.
    const ListPointer click_row0[] = {
        list_ptr_hover(150.0f, 12.0f), list_ptr_hover(150.0f, 12.0f), list_ptr_press(150.0f, 12.0f),
        list_ptr_release(150.0f, 12.0f),
    };
    marquee_play(ctx, &c.model, click_row0, sizeof_array(click_row0));

    // Move to the gap between row 0 and row 1 and let the hot state settle, so the coming press arms the marquee
    // and does not click a row that is still hot. Then marquee: press on empty space, drag past the threshold,
    // release.
    const ListPointer sweep[] = {
        list_ptr_hover(150.0f, 30.0f), list_ptr_hover(150.0f, 30.0f), list_ptr_press(150.0f, 30.0f),
        list_ptr_drag(150.0f, 100.0f), list_ptr_release(150.0f, 100.0f),
    };
    marquee_play(ctx, &c.model, sweep, sizeof_array(sweep));
    ASSERT_TRUE_MSG(selection_set_contains(c.model.set, MARQUEE_ID_AT(1)), "the marquee gesture really ran (row 1 swept)");
    ASSERT_TRUE_MSG(selection_set_contains(c.model.set, MARQUEE_ID_AT(2)), "the marquee gesture really ran (row 2 swept)");

    // Park the cursor off the list without a press, so hover cannot hide the keyboard cursor, then Nav-Down. The
    // cursor moving from id 1 to id 2 shows the list still holds keyboard focus.
    const ListPointer parked = list_ptr_hover(350.0f, 300.0f);
    marquee_step(ctx, &c.model, parked);
    FlListResult res = marquee_nav_down_frame(ctx, &c.model, parked);
    ASSERT_EQ_MSG((u64)MARQUEE_ID_AT(1), res.focused_id,
                  "keyboard nav still works after a marquee gesture — the arming press kept the list's focus");

    // A press outside the viewport blurs the list, and a later Nav-Down is ignored.
    marquee_step(ctx, &c.model, list_ptr_press(350.0f, 300.0f));
    marquee_step(ctx, &c.model, list_ptr_release(350.0f, 300.0f));
    res = marquee_nav_down_frame(ctx, &c.model, parked);
    ASSERT_EQ_MSG((u64)0, res.focused_id, "a press outside the list's viewport still blurs its keyboard focus");

    selection_set_destroy(c.model.set);
    list_app_close(&c.app);
}
