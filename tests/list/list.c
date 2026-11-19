// ui_list selection tests

#include "list/common.h"

// The List's built-in multi-selection gestures: a plain click replaces, a ctrl or cmd click toggles, a shift click
// selects a range from the anchor, and Ctrl+A selects all. The List applies the Selection Requests it emits to
// the caller-owned FlSelectionSet and passes the full ordered id slice, so ranges and SetAll resolve across rows
// that are virtualized away. A nullptr set applies nothing but still returns the requests on the result. The
// cases use the render fixture, stamp a row's ItemState hot in place of hovering, and inject a fast tap through
// mouse_pressed_this_frame and mouse_released_this_frame plus modifiers or FlKey_A.

// Each case uses its own list id. The List's per-list state (gesture anchor and focus) lives in the shared
// FlListContext across UTEST cases, so a reused id would carry focus and anchor from one test into the next. Row
// ids are the container name seeded with {pos + 1}, as in list_item_begin, so a row's ItemState is keyed by
// ui_layout_id_to_clay_id({name, pos + 1}).

// Each plain click emits ClearAll + SetOne(id, true) and leaves exactly the clicked id in the set.
UTEST(list_selection, plain_click_replaces) {
    LIST_REQUIRE_FIXTURE(ctx);
    ListModel m;
    list_model_init(&m, ID("sel_plain"), 100, 240.0f);
    m.set = selection_set_create(16);
    list_sel_setup(ctx, &m);

    // Click position 0 (stable id 1): the set becomes exactly {1}.
    FlListResult r = list_sel_click_row(ctx, &m, 0, FlKeyModifier_None);
    ASSERT_EQ_MSG((u64)1, selection_set_len(m.set), "plain click selects exactly one row");
    ASSERT_TRUE_MSG(selection_set_contains(m.set, 1), "the clicked row's id is selected");
    ASSERT_EQ_MSG(2u, r.request_count, "plain click emits ClearAll + SetOne");
    const FlSelectionRequest* req = r.requests;
    ASSERT_EQ_MSG((int)FlSelectionRequestKind_ClearAll, (int)req[0].kind, "first request clears the set");
    ASSERT_EQ_MSG((int)FlSelectionRequestKind_SetOne, (int)req[1].kind, "second request selects the row");
    ASSERT_EQ_MSG((u64)1, req[1].id, "SetOne targets the clicked stable id");
    ASSERT_TRUE_MSG(req[1].selected, "SetOne selects (not deselects)");

    // Click position 4 (stable id 5): the earlier selection is replaced.
    list_sel_click_row(ctx, &m, 4, FlKeyModifier_None);
    ASSERT_EQ_MSG((u64)1, selection_set_len(m.set), "a second plain click still selects exactly one row");
    ASSERT_TRUE_MSG(selection_set_contains(m.set, 5), "the newly clicked row is selected");
    ASSERT_FALSE_MSG(selection_set_contains(m.set, 1), "the previously selected row is dropped (replace, not add)");

    selection_set_destroy(m.set);
}

// A ctrl or cmd click toggles only the clicked row and leaves the rest of the selection as it was.
UTEST(list_selection, ctrl_click_toggles) {
    LIST_REQUIRE_FIXTURE(ctx);
    ListModel m;
    list_model_init(&m, ID("sel_ctrl"), 100, 240.0f);
    m.set = selection_set_create(16);
    list_sel_setup(ctx, &m);

    // Plain click pos 0 -> {1}.
    list_sel_click_row(ctx, &m, 0, FlKeyModifier_None);
    ASSERT_TRUE_MSG(selection_set_contains(m.set, 1), "baseline: row 1 selected");

    // Ctrl-click pos 2 -> adds id 3 without dropping id 1: {1, 3}.
    FlListResult r = list_sel_click_row(ctx, &m, 2, FlKeyModifier_Control);
    ASSERT_EQ_MSG((u64)2, selection_set_len(m.set), "ctrl-click extends the selection");
    ASSERT_TRUE_MSG(selection_set_contains(m.set, 1) && selection_set_contains(m.set, 3), "both rows selected");
    ASSERT_EQ_MSG(1u, r.request_count, "ctrl-click emits a single SetOne (no ClearAll)");
    const FlSelectionRequest* added = r.requests;
    ASSERT_EQ_MSG((int)FlSelectionRequestKind_SetOne, (int)added->kind, "ctrl-click is a SetOne");
    ASSERT_EQ_MSG((u64)3, added->id, "SetOne targets the ctrl-clicked stable id");
    ASSERT_TRUE_MSG(added->selected, "toggling a not-selected row selects it");

    // Ctrl-click pos 2 again -> toggles id 3 back off: {1}.
    r = list_sel_click_row(ctx, &m, 2, FlKeyModifier_Control);
    ASSERT_EQ_MSG((u64)1, selection_set_len(m.set), "ctrl-click again toggles the row off");
    ASSERT_TRUE_MSG(selection_set_contains(m.set, 1), "the other row stays selected");
    ASSERT_FALSE_MSG(selection_set_contains(m.set, 3), "the re-clicked row is deselected");
    const FlSelectionRequest* removed = r.requests;
    ASSERT_EQ_MSG((u64)3, removed->id, "the re-toggle SetOne still targets the same stable id");
    ASSERT_FALSE_MSG(removed->selected, "toggling a selected row deselects it");

    selection_set_destroy(m.set);
}

// A shift click selects the range from the anchor and replaces the earlier selection. The request holds only the
// endpoints, SetRange(anchor_id, target_id), and apply resolves every id between them against the ordered slice.
UTEST(list_selection, shift_click_ranges_from_anchor) {
    LIST_REQUIRE_FIXTURE(ctx);
    ListModel m;
    list_model_init(&m, ID("sel_shift"), 100, 240.0f);
    m.set = selection_set_create(16);
    list_sel_setup(ctx, &m);

    // Plain click pos 2 sets the anchor at stable id 3.
    list_sel_click_row(ctx, &m, 2, FlKeyModifier_None);

    // Shift-click pos 5 (stable id 6): selects the inclusive span 3..6 = {3, 4, 5, 6}.
    FlListResult r = list_sel_click_row(ctx, &m, 5, FlKeyModifier_Shift);
    ASSERT_EQ_MSG((u64)4, selection_set_len(m.set), "shift-range selects the 4-id inclusive span 3..6");
    for (u64 id = 3; id <= 6; id++) {
        ASSERT_TRUE_MSG(selection_set_contains(m.set, id), "every id in the anchor..target span is selected");
    }
    ASSERT_FALSE_MSG(selection_set_contains(m.set, 2), "ids outside the span are not selected");

    ASSERT_EQ_MSG(2u, r.request_count, "shift-click emits ClearAll + SetRange");
    const FlSelectionRequest* range = &r.requests[1];
    ASSERT_EQ_MSG((int)FlSelectionRequestKind_SetRange, (int)range->kind, "second request is a range");
    ASSERT_EQ_MSG((u64)3, range->id, "range carries the anchor id as its 'from' endpoint");
    ASSERT_EQ_MSG((u64)6, range->to_id, "range carries the target id as its 'to' endpoint");

    selection_set_destroy(m.set);
}

// A shift click on a list that has never been plain-clicked has no anchor to range from, so it acts as a plain
// replace: ClearAll + SetOne(target). This covers the shift && anchor_id != 0 check in the gesture code.
UTEST(list_selection, shift_click_without_anchor_falls_through_to_plain) {
    LIST_REQUIRE_FIXTURE(ctx);
    ListModel m;
    list_model_init(&m, ID("sel_shift_noanchor"), 100, 240.0f);
    m.set = selection_set_create(16);
    list_sel_setup(ctx, &m);

    // The first interaction is a shift click on pos 4 (stable id 5), while anchor_id is still 0.
    FlListResult r = list_sel_click_row(ctx, &m, 4, FlKeyModifier_Shift);

    ASSERT_EQ_MSG((u64)1, selection_set_len(m.set), "shift without an anchor selects exactly the clicked row");
    ASSERT_TRUE_MSG(selection_set_contains(m.set, 5), "the shift-clicked row is selected");
    ASSERT_EQ_MSG(2u, r.request_count, "falls through to ClearAll + SetOne");
    const FlSelectionRequest* req = r.requests;
    ASSERT_EQ_MSG((int)FlSelectionRequestKind_ClearAll, (int)req[0].kind, "first request clears the set");
    ASSERT_EQ_MSG((int)FlSelectionRequestKind_SetOne, (int)req[1].kind, "second request is SetOne, not SetRange");
    ASSERT_EQ_MSG((u64)5, req[1].id, "SetOne targets the shift-clicked id");

    selection_set_destroy(m.set);
}

// Ctrl+A selects every row in the full ordered slice, including rows virtualized away, and only when the list
// holds focus. The model has 100 rows and the viewport emits only a few, so id 100 can be selected only if the
// List passes the whole ordered slice to apply.
UTEST(list_selection, ctrl_a_selects_full_slice_and_requires_focus) {
    LIST_REQUIRE_FIXTURE(ctx);
    ListModel m;
    // A small viewport, so only a few of the 100 rows are ever emitted.
    list_model_init(&m, ID("sel_ctrla"), 100, 120.0f);
    m.set = selection_set_create(16);
    FlListRange range = list_sel_setup(ctx, &m);
    ASSERT_TRUE_MSG(range.end < m.count, "viewport must virtualize: not all rows are emitted");

    // Ctrl+A with no earlier click: the list has no focus, so nothing happens.
    const SelInput ctrl_a = { .modifiers = FlKeyModifier_Control, .key_a = true };
    FlListResult r = list_sel_frame(ctx, &m, ctrl_a);
    ASSERT_EQ_MSG((u64)0, selection_set_len(m.set), "Ctrl+A on an unfocused list selects nothing");
    ASSERT_EQ_MSG(0u, r.request_count, "Ctrl+A on an unfocused list emits no request");

    // Click a visible row to focus the list.
    list_sel_click_row(ctx, &m, 0, FlKeyModifier_None);

    list_sel_only_hot(ctx, &m, -1); // no row is hot, so this frame is only the keyboard shortcut
    r = list_sel_frame(ctx, &m, ctrl_a);
    ASSERT_EQ_MSG((u64)m.count, selection_set_len(m.set), "Ctrl+A selects every id in the ordered slice");
    ASSERT_TRUE_MSG(selection_set_contains(m.set, 100), "an id whose row was never emitted is still selected");
    ASSERT_EQ_MSG(1u, r.request_count, "Ctrl+A emits a single SetAll");
    const FlSelectionRequest* select_all = r.requests;
    ASSERT_EQ_MSG((int)FlSelectionRequestKind_SetAll, (int)select_all->kind, "the request is SetAll");

    selection_set_destroy(m.set);
}

// With a nullptr set the gesture still returns its requests on the result, so the app can drive its own model.
// Nothing is applied and the row's Signal.selected stays false.
UTEST(list_selection, null_set_emits_requests_without_applying) {
    LIST_REQUIRE_FIXTURE(ctx);
    ListModel m;
    list_model_init(&m, ID("sel_null"), 20, 240.0f);
    list_sel_setup(ctx, &m);

    FlSignal signals[20] = { 0 };
    list_sel_only_hot(ctx, &m, 0);
    FlListResult r = list_sel_frame(ctx, &m, (SelInput) { .click = true, .out_signals = signals });

    ASSERT_EQ_MSG(2u, r.request_count, "nullptr set still exposes the emitted requests");
    const FlSelectionRequest* req = r.requests;
    ASSERT_EQ_MSG((int)FlSelectionRequestKind_ClearAll, (int)req[0].kind, "first request is ClearAll");
    ASSERT_EQ_MSG((int)FlSelectionRequestKind_SetOne, (int)req[1].kind, "second request is SetOne");
    ASSERT_EQ_MSG((u64)1, req[1].id, "SetOne names the clicked id");
    ASSERT_FALSE_MSG(signals[0].selected, "with a nullptr set the clicked row's Signal.selected stays false");
}

// Signal.selected is the set membership of each visible row, looked up by id, so the app can style its own row
// content from it. The case fills the set, then reads the per-row signals on an idle frame.
UTEST(list_selection, signal_selected_reflects_membership) {
    LIST_REQUIRE_FIXTURE(ctx);
    ListModel m;
    list_model_init(&m, ID("sel_membership"), 20, 240.0f);
    m.set = selection_set_create(16);
    FlListRange range = list_sel_setup(ctx, &m);
    ASSERT_TRUE_MSG(range.end > 2, "row at position 2 must be visible for this check");

    // Select id 3 (position 2) directly, with no gesture.
    selection_set_set(m.set, 3, true);

    FlSignal signals[20] = { 0 };
    list_sel_only_hot(ctx, &m, -1);
    list_sel_frame(ctx, &m, (SelInput) { .out_signals = signals });
    ASSERT_TRUE_MSG(signals[2].selected, "the row whose id is in the set reports Signal.selected == true");
    ASSERT_FALSE_MSG(signals[0].selected, "a row not in the set reports Signal.selected == false");
    ASSERT_FALSE_MSG(signals[1].selected, "another unselected row reports Signal.selected == false");

    selection_set_destroy(m.set);
}

// The gesture anchor lives in list-level state keyed by the list id, so it persists across idle frames and a
// shift click ranges from an anchor set many frames earlier.
UTEST(list_selection, anchor_persists_across_idle_frames) {
    LIST_REQUIRE_FIXTURE(ctx);
    ListModel m;
    list_model_init(&m, ID("sel_anchor"), 100, 240.0f);
    m.set = selection_set_create(16);
    list_sel_setup(ctx, &m);

    // Set the anchor at position 1 (stable id 2) with a plain click.
    list_sel_click_row(ctx, &m, 1, FlKeyModifier_None);

    // Five idle frames with no clicks and no keys.
    for (int f = 0; f < 5; f++) {
        list_sel_only_hot(ctx, &m, -1);
        list_sel_frame(ctx, &m, (SelInput) { 0 });
    }

    // Shift-click position 4 (stable id 5): the range still starts at the anchor (id 2), giving {2,3,4,5}.
    FlListResult r = list_sel_click_row(ctx, &m, 4, FlKeyModifier_Shift);
    ASSERT_EQ_MSG((u64)4, selection_set_len(m.set), "shift-range from the persisted anchor spans 2..5");
    for (u64 id = 2; id <= 5; id++) {
        ASSERT_TRUE_MSG(selection_set_contains(m.set, id), "the span resolves against the surviving anchor");
    }
    ASSERT_EQ_MSG((u64)2, r.requests[1].id, "the range's 'from' endpoint is the anchor set five frames earlier");

    selection_set_destroy(m.set);
}
