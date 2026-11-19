// ui_list drag-reorder tests

#include "list/common.h"

// A press on a row that crosses the click-vs-drag threshold begins a reorder. On release the List returns a
// reorder request { drag_from_ids, drag_target_id, drag_rel } that says where the drop landed. The app changes
// the model. Dragging any row of a multi-selection drags the whole selection. The case uses the real input layer
// every frame and sits with the other fl_app cases because fl_app_destroy tears down the global UI context.
//
// The geometry: row_height 24 + gap 12 gives a 36px stride in a 120px viewport at the window origin (ui_scale 1,
// so content Y == screen Y at scroll 0). Stable ids are (position+1)*10, so a request keyed on position would not
// match. Row p occupies screen y [p*36, p*36+24] and its midpoint is p*36+12. The half of the row the cursor is in
// decides whether the drop lands before or after it.

// Stable id of position p in this fixture.
#define DRAG_ID_AT(p) ((u64)(((p) + 1) * 10))

#define DRAG_ROW_X 150.0f
#define DRAG_ROW0_Y 12.0f  // row 0 midpoint  (row 0 spans [0, 24])
#define DRAG_ROW2_LO 90.0f // row 2 bottom half ([84, 96]) -> insert after row 2 (before row 3)

typedef struct DragCase {
    ListApp app;
    ListModel model;
} DragCase;

static void drag_case_init(DragCase* c) {
    list_model_init(&c->model, ID("drag_list_a"), 100, 0.0f);
    list_model_scale_ids(&c->model, 10);
    c->model.config.gap = 12.0f;
    c->model.set = selection_set_create(64);
}

static FlListResult drag_step(DragCase* c, ListPointer pointer) {
    FlListResult result = { 0 };
    list_boxed_step(c->app.ctx, &c->model, &(ListBoxedStep) { .root = ID("drag_root"), .pointer = pointer,
                                                              .out_result = &result });
    return result;
}

// Two warm-up frames make row 0 hot (hot lags one frame), then the press arms the drag on row 0.
static void drag_press_row0(DragCase* c) {
    drag_step(c, list_ptr_hover(DRAG_ROW_X, DRAG_ROW0_Y));
    drag_step(c, list_ptr_hover(DRAG_ROW_X, DRAG_ROW0_Y));
    drag_step(c, list_ptr_press(DRAG_ROW_X, DRAG_ROW0_Y));
}

// (1) A press and release on a row under the threshold stays a click: press, 2px move, release.
static void drag_phase_click_under_threshold(int* utest_result, DragCase* c) {
    drag_press_row0(c);
    FlListResult sub = drag_step(c, list_ptr_drag(DRAG_ROW_X, DRAG_ROW0_Y + 2.0f)); // 2px move
    ASSERT_FALSE_MSG(sub.drag_active, "a sub-threshold move must not begin a reorder drag");
    FlListResult sub_rel = drag_step(c, list_ptr_release(DRAG_ROW_X, DRAG_ROW0_Y + 2.0f));
    ASSERT_FALSE_MSG(sub_rel.reorder_emitted, "a sub-threshold press+release emits no reorder request");
    ASSERT_TRUE_MSG(selection_set_contains(c->model.set, DRAG_ID_AT(0)), "the sub-threshold gesture read as a plain click");
    ASSERT_EQ_MSG((u64)1, selection_set_len(c->model.set), "a plain click selects exactly the clicked row");
}

// (2) Single-row drag. Press row 0, drag well past the threshold into row 2's bottom half, release. The frame
// during the drag returns the live drag state and the release frame returns the reorder request.
static void drag_phase_single_row(int* utest_result, DragCase* c) {
    c->model.list = ID("drag_list_b");
    selection_set_clear_all(c->model.set);
    drag_press_row0(c);

    FlListResult mid = drag_step(c, list_ptr_drag(DRAG_ROW_X, DRAG_ROW2_LO)); // live drag
    ASSERT_TRUE_MSG(mid.drag_active, "crossing the threshold begins the reorder drag (live state exposed)");
    ASSERT_FALSE_MSG(mid.reorder_emitted, "no reorder request until release");
    ASSERT_EQ_MSG((u32)1, mid.drag_from_count, "a single dragged row reports one from-id");
    const u64 dragged_id = mid.drag_from_ids[0];
    ASSERT_EQ_MSG(DRAG_ID_AT(0), dragged_id, "the dragged id is the pressed row's stable id (identity)");
    ASSERT_NE_MSG((int)FlDropRelationship_Into, (int)mid.drag_rel, "v1 never produces Into");

    FlListResult drop = drag_step(c, list_ptr_release(DRAG_ROW_X, DRAG_ROW2_LO));
    ASSERT_TRUE_MSG(drop.reorder_emitted, "releasing an active drag emits a reorder request");
    ASSERT_FALSE_MSG(drop.drag_active, "the drop frame is not also an in-flight frame");
    ASSERT_EQ_MSG((u32)1, drop.drag_from_count, "single-row drag: one from-id");
    const u64 dropped_id = drop.drag_from_ids[0];
    ASSERT_EQ_MSG(DRAG_ID_AT(0), dropped_id, "single-row drag: the from-id is the pressed row");
    ASSERT_EQ_MSG(DRAG_ID_AT(2), drop.drag_target_id, "dropped in row 2's bottom half -> target is row 2");
    ASSERT_EQ_MSG((int)FlDropRelationship_After, (int)drop.drag_rel, "bottom half of a row -> drop After it");

    // The gesture ended on release, so a later idle frame emits nothing.
    FlListResult idle = drag_step(c, list_ptr_hover(DRAG_ROW_X, DRAG_ROW2_LO));
    ASSERT_FALSE_MSG(idle.reorder_emitted, "the reorder request fires once, on release, not on later idle frames");
    ASSERT_FALSE_MSG(idle.drag_active, "no drag is in flight after release");
}

// (3) Multi-row drag: with rows 0 and 1 selected, pressing one of them drags the whole selection. The rows are
// selected through the set directly, since membership is what the List reads. Press row 0, drag into row 2's bottom
// half, release.
static void drag_phase_multi_row(int* utest_result, DragCase* c) {
    c->model.list = ID("drag_list_c");
    selection_set_clear_all(c->model.set);
    selection_set_set(c->model.set, DRAG_ID_AT(0), true);
    selection_set_set(c->model.set, DRAG_ID_AT(1), true);
    drag_press_row0(c); // press row 0 (a selected row)
    drag_step(c, list_ptr_drag(DRAG_ROW_X, DRAG_ROW2_LO)); // drag past threshold
    FlListResult multi = drag_step(c, list_ptr_release(DRAG_ROW_X, DRAG_ROW2_LO));
    ASSERT_TRUE_MSG(multi.reorder_emitted, "multi-row drag emits a reorder request on release");
    ASSERT_EQ_MSG((u32)2, multi.drag_from_count, "dragging a selected row drags the whole selection (2 ids)");
    bool has0 = false, has1 = false;
    for (u32 i = 0; i < multi.drag_from_count; i++) {
        has0 |= (multi.drag_from_ids[i] == DRAG_ID_AT(0));
        has1 |= (multi.drag_from_ids[i] == DRAG_ID_AT(1));
    }
    ASSERT_TRUE_MSG(has0 && has1, "from_ids lists every selected id (order unspecified)");
    ASSERT_EQ_MSG(DRAG_ID_AT(2), multi.drag_target_id, "multi-row drop target is row 2 (an unselected row)");
    ASSERT_EQ_MSG((int)FlDropRelationship_After, (int)multi.drag_rel, "bottom half of row 2 -> drop After it");
}

UTEST(list_drag, reorder_request) {
    DragCase c;
    ASSERT_TRUE_MSG(list_app_open(&c.app, FL_S("list_drag")), "fl_app_create(headless) failed");
    drag_case_init(&c);

    LIST_RUN_PHASE(drag_phase_click_under_threshold(utest_result, &c));
    LIST_RUN_PHASE(drag_phase_single_row(utest_result, &c));
    LIST_RUN_PHASE(drag_phase_multi_row(utest_result, &c));

    selection_set_destroy(c.model.set);
    list_app_close(&c.app);
}
