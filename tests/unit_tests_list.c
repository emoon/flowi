// ui_list selection, keyboard and marquee tests

#include "unit_tests_common.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// list_selection.* - the List's built-in multi-selection gesture machine: plain click replaces, ctrl/⌘-click
// toggles, shift-click ranges from the anchor, Ctrl+A selects all. The List runs the gestures internally and
// auto-applies the emitted Selection Requests into the caller-owned FlSelectionSet (forwarding the FULL ordered
// id slice so ranges/SetAll resolve across rows virtualized away); a NULL set applies nothing but still exposes
// the requests on the result. These cases drive the render fixture directly, shortcut hover by stamping a row's
// ItemState hot, and inject a fast tap via mouse_pressed/_released_this_frame plus modifiers / FlKey_A.

// Each case emits under its OWN list id: the List's per-list state (gesture anchor + focus) persists in the
// shared FlListContext across UTEST cases, so reusing one id would bleed focus/anchor between tests. Row ids are
// the container name seeded with {pos + 1} (matching list_item_begin), so a row's interactive ItemState is keyed
// by ui_layout_id_to_clay_id({name, pos + 1}).

// Stamp exactly one visible row hot (so a same-frame fast tap clicks it) and clear every other row's hot, so a
// stale hot from a prior frame can't fire a second phantom click. `hot_pos < 0` clears all. Only rows that have
// been emitted at least once have an ItemState; absent ones are simply skipped.
static void sel_only_hot(FlUiCtx* ctx, FlLayoutId list_id, u32 count, int hot_pos) {
    for (u32 p = 0; p < count; p++) {
        const FlLayoutId row = { .name = list_id.name, .id = (i32)(p + 1) };
        ItemState* it = hashmap_get(&ctx->shared->item_state, ui_layout_id_to_clay_id(row));
        if (it != nullptr) {
            it->state.hot = (hot_pos >= 0 && (u32)hot_pos == p) ? 1 : 0;
        }
    }
}

// Emit one frame of the selection test list (cursor parked off-list; hover comes from the stamped hot). Injects
// modifiers (FlKeyModifier_*), the Ctrl+A key_a edge, and - when click - a same-frame press+release fast tap.
// out_signals, if non-NULL, captures each visible row's Signal at absolute index i (size it >= count).
static FlListResult sel_frame(FlUiCtx* ctx, FlLayoutId list_id, FlSelectionSet* set, const u64* ids, u32 count,
                              f32 viewport_h, u32 modifiers, bool key_a, bool click, FlSignal* out_signals) {
    ctx->input_state.mouse.x = -100.0f;
    ctx->input_state.mouse.y = -100.0f;
    ctx->input_state.keyboard.modifiers = modifiers;
    ctx->keys_pressed_this_frame[FlKey_A] = key_a;
    ctx->mouse_pressed_this_frame = click;
    ctx->mouse_released_this_frame = click;
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(12, 12, 14);

    FlListResult result = { 0 };
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(viewport_h) },
                   .direction = FlLayoutDirection_TopToBottom) {
        FlListRange r = ui_list_begin(list_id, ids, count, set, (FlListConfig) { .row_height = 24.0f, .gap = 4.0f });
        for (u32 i = r.start; i < r.end; i++) {
            FlSignal sig = ui_list_item_begin((u64)(i + 1));
            if (out_signals != nullptr) {
                out_signals[i] = sig;
            }
            ui_layout_area(.sizing = { sizing_grow(), sizing_fixed(24) }) {}
            ui_list_item_end();
        }
        result = ui_list_end();
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    return result;
}

// Shared fixture prologue: size the surface and run two idle frames. Frame 1 establishes the list container
// height list_uniform measures from the previous layout; frame 2 emits the true visible window so its rows'
// ItemStates exist for the hot-stamp shortcut. Returns the visible range from the second warm-up frame.
static FlListRange sel_setup(FlUiCtx* ctx, FlLayoutId list_id, FlSelectionSet* set, const u64* ids, u32 count,
                             f32 viewport_h) {
    ctx->layout_width = 320.0f;
    ctx->layout_height = 256.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 320.0f, .height = 256.0f });
    sel_frame(ctx, list_id, set, ids, count, viewport_h, FlKeyModifier_None, false, false, nullptr);
    FlListResult r2 = sel_frame(ctx, list_id, set, ids, count, viewport_h, FlKeyModifier_None, false, false, nullptr);
    (void)r2;
    // Re-emit once more only to read back the range.
    FlListRange range = { 0, 0, 0 };
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(viewport_h) },
                   .direction = FlLayoutDirection_TopToBottom) {
        range = ui_list_begin(list_id, ids, count, set, (FlListConfig) { .row_height = 24.0f, .gap = 4.0f });
        for (u32 i = range.start; i < range.end; i++) {
            ui_list_item_begin((u64)(i + 1));
            ui_layout_area(.sizing = { sizing_grow(), sizing_fixed(24) }) {}
            ui_list_item_end();
        }
        ui_list_end();
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    return range;
}

// Plain click replaces the whole selection with exactly the clicked row (and re-clicking another row replaces
// again). Each click emits ClearAll + SetOne(id, true) and lands one id in the set.
UTEST(list_selection, plain_click_replaces) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlSelectionSet* set = selection_set_create(16);
    const u32 count = 100;
    u64 ids[100];
    for (u32 i = 0; i < count; i++) {
        ids[i] = (u64)(i + 1);
    }

    const FlLayoutId list = ID("sel_plain");
    sel_setup(ctx, list, set, ids, count, 240.0f);

    // Click position 0 (stable id 1): the set becomes exactly {1}.
    sel_only_hot(ctx, list, count, 0);
    FlListResult r = sel_frame(ctx, list, set, ids, count, 240.0f, FlKeyModifier_None, false, true, nullptr);
    ASSERT_EQ_MSG((u64)1, selection_set_len(set), "plain click selects exactly one row");
    ASSERT_TRUE_MSG(selection_set_contains(set, 1), "the clicked row's id is selected");
    ASSERT_EQ_MSG(2u, r.request_count, "plain click emits ClearAll + SetOne");
    ASSERT_EQ_MSG((int)FlSelectionRequestKind_ClearAll, (int)r.requests[0].kind, "first request clears the set");
    ASSERT_EQ_MSG((int)FlSelectionRequestKind_SetOne, (int)r.requests[1].kind, "second request selects the row");
    ASSERT_EQ_MSG((u64)1, r.requests[1].id, "SetOne targets the clicked stable id");
    ASSERT_TRUE_MSG(r.requests[1].selected, "SetOne selects (not deselects)");

    // Click position 4 (stable id 5): the prior selection is replaced, not extended.
    sel_only_hot(ctx, list, count, 4);
    sel_frame(ctx, list, set, ids, count, 240.0f, FlKeyModifier_None, false, true, nullptr);
    ASSERT_EQ_MSG((u64)1, selection_set_len(set), "a second plain click still selects exactly one row");
    ASSERT_TRUE_MSG(selection_set_contains(set, 5), "the newly clicked row is selected");
    ASSERT_FALSE_MSG(selection_set_contains(set, 1), "the previously selected row is dropped (replace, not add)");

    selection_set_destroy(set);
}

// Ctrl/⌘-click toggles just the clicked row's membership, leaving the rest of the selection intact.
UTEST(list_selection, ctrl_click_toggles) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlSelectionSet* set = selection_set_create(16);
    const u32 count = 100;
    u64 ids[100];
    for (u32 i = 0; i < count; i++) {
        ids[i] = (u64)(i + 1);
    }
    const FlLayoutId list = ID("sel_ctrl");
    sel_setup(ctx, list, set, ids, count, 240.0f);

    // Plain click pos 0 -> {1}.
    sel_only_hot(ctx, list, count, 0);
    sel_frame(ctx, list, set, ids, count, 240.0f, FlKeyModifier_None, false, true, nullptr);
    ASSERT_TRUE_MSG(selection_set_contains(set, 1), "baseline: row 1 selected");

    // Ctrl-click pos 2 -> adds id 3 without dropping id 1: {1, 3}.
    sel_only_hot(ctx, list, count, 2);
    FlListResult r = sel_frame(ctx, list, set, ids, count, 240.0f, FlKeyModifier_Control, false, true, nullptr);
    ASSERT_EQ_MSG((u64)2, selection_set_len(set), "ctrl-click extends the selection");
    ASSERT_TRUE_MSG(selection_set_contains(set, 1) && selection_set_contains(set, 3), "both rows selected");
    ASSERT_EQ_MSG(1u, r.request_count, "ctrl-click emits a single SetOne (no ClearAll)");
    ASSERT_EQ_MSG((int)FlSelectionRequestKind_SetOne, (int)r.requests[0].kind, "ctrl-click is a SetOne");
    ASSERT_EQ_MSG((u64)3, r.requests[0].id, "SetOne targets the ctrl-clicked stable id");
    ASSERT_TRUE_MSG(r.requests[0].selected, "toggling a not-selected row selects it");

    // Ctrl-click pos 2 again -> toggles id 3 back off: {1}.
    sel_only_hot(ctx, list, count, 2);
    r = sel_frame(ctx, list, set, ids, count, 240.0f, FlKeyModifier_Control, false, true, nullptr);
    ASSERT_EQ_MSG((u64)1, selection_set_len(set), "ctrl-click again toggles the row off");
    ASSERT_TRUE_MSG(selection_set_contains(set, 1), "the other row stays selected");
    ASSERT_FALSE_MSG(selection_set_contains(set, 3), "the re-clicked row is deselected");
    ASSERT_EQ_MSG((u64)3, r.requests[0].id, "the re-toggle SetOne still targets the same stable id");
    ASSERT_FALSE_MSG(r.requests[0].selected, "toggling a selected row deselects it");

    selection_set_destroy(set);
}

// Shift-click ranges from the anchor, replacing the prior selection. The emitted request is endpoint-only
// (SetRange(anchor_id, target_id)) - never an enumerated span - yet apply resolves every id between the two
// endpoints against the ordered slice.
UTEST(list_selection, shift_click_ranges_from_anchor) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlSelectionSet* set = selection_set_create(16);
    const u32 count = 100;
    u64 ids[100];
    for (u32 i = 0; i < count; i++) {
        ids[i] = (u64)(i + 1);
    }
    const FlLayoutId list = ID("sel_shift");
    sel_setup(ctx, list, set, ids, count, 240.0f);

    // Plain click pos 2 sets the anchor at stable id 3.
    sel_only_hot(ctx, list, count, 2);
    sel_frame(ctx, list, set, ids, count, 240.0f, FlKeyModifier_None, false, true, nullptr);

    // Shift-click pos 5 (stable id 6): selects the inclusive span 3..6 = {3, 4, 5, 6}.
    sel_only_hot(ctx, list, count, 5);
    FlListResult r = sel_frame(ctx, list, set, ids, count, 240.0f, FlKeyModifier_Shift, false, true, nullptr);
    ASSERT_EQ_MSG((u64)4, selection_set_len(set), "shift-range selects the 4-id inclusive span 3..6");
    for (u64 id = 3; id <= 6; id++) {
        ASSERT_TRUE_MSG(selection_set_contains(set, id), "every id in the anchor..target span is selected");
    }
    ASSERT_FALSE_MSG(selection_set_contains(set, 2), "ids outside the span are not selected");

    ASSERT_EQ_MSG(2u, r.request_count, "shift-click emits ClearAll + SetRange");
    ASSERT_EQ_MSG((int)FlSelectionRequestKind_SetRange, (int)r.requests[1].kind, "second request is a range");
    ASSERT_EQ_MSG((u64)3, r.requests[1].id, "range carries the anchor id as its 'from' endpoint");
    ASSERT_EQ_MSG((u64)6, r.requests[1].to_id, "range carries the target id as its 'to' endpoint");

    selection_set_destroy(set);
}

// Shift-click with no anchor yet (the list has never been plain-clicked) has nothing to range from, so the
// gesture falls through to a plain replace - ClearAll + SetOne(target), not a SetRange. Pins the
// shift && anchor_id != 0 guard in the gesture machine.
UTEST(list_selection, shift_click_without_anchor_falls_through_to_plain) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlSelectionSet* set = selection_set_create(16);
    const u32 count = 100;
    u64 ids[100];
    for (u32 i = 0; i < count; i++) {
        ids[i] = (u64)(i + 1);
    }
    const FlLayoutId list = ID("sel_shift_noanchor");
    sel_setup(ctx, list, set, ids, count, 240.0f);

    // First-ever interaction is a shift-click on pos 4 (stable id 5); anchor_id is still 0.
    sel_only_hot(ctx, list, count, 4);
    FlListResult r = sel_frame(ctx, list, set, ids, count, 240.0f, FlKeyModifier_Shift, false, true, nullptr);

    ASSERT_EQ_MSG((u64)1, selection_set_len(set), "shift without an anchor selects exactly the clicked row");
    ASSERT_TRUE_MSG(selection_set_contains(set, 5), "the shift-clicked row is selected");
    ASSERT_EQ_MSG(2u, r.request_count, "falls through to ClearAll + SetOne");
    ASSERT_EQ_MSG((int)FlSelectionRequestKind_ClearAll, (int)r.requests[0].kind, "first request clears the set");
    ASSERT_EQ_MSG((int)FlSelectionRequestKind_SetOne, (int)r.requests[1].kind,
                  "second request is SetOne, not SetRange");
    ASSERT_EQ_MSG((u64)5, r.requests[1].id, "SetOne targets the shift-clicked id");

    selection_set_destroy(set);
}

// Ctrl+A selects every row in the FULL ordered slice - including rows virtualized away - and only when the list
// holds focus. With a 100-row model and a viewport that emits only a handful of rows, Ctrl+A must still select
// an id (e.g. 100) whose row was never laid out, which can only happen if the List forwards the whole ordered
// slice (not the visible window) to apply.
UTEST(list_selection, ctrl_a_selects_full_slice_and_requires_focus) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlSelectionSet* set = selection_set_create(16);
    const u32 count = 100;
    u64 ids[100];
    for (u32 i = 0; i < count; i++) {
        ids[i] = (u64)(i + 1);
    }
    // Small viewport so only a handful of the 100 rows are ever emitted.
    const f32 viewport = 120.0f;
    const FlLayoutId list = ID("sel_ctrla");
    FlListRange range = sel_setup(ctx, list, set, ids, count, viewport);
    ASSERT_TRUE_MSG(range.end < count, "viewport must virtualize: not all rows are emitted");

    // Ctrl+A with no prior click: the list has no focus, so it is a no-op.
    FlListResult r = sel_frame(ctx, list, set, ids, count, viewport, FlKeyModifier_Control, true, false, nullptr);
    ASSERT_EQ_MSG((u64)0, selection_set_len(set), "Ctrl+A on an unfocused list selects nothing");
    ASSERT_EQ_MSG(0u, r.request_count, "Ctrl+A on an unfocused list emits no request");

    // Click a visible row to focus the list.
    sel_only_hot(ctx, list, count, 0);
    sel_frame(ctx, list, set, ids, count, viewport, FlKeyModifier_None, false, true, nullptr);

    sel_only_hot(ctx, list, count, -1); // no row hot: this frame is the keyboard shortcut, not a click
    r = sel_frame(ctx, list, set, ids, count, viewport, FlKeyModifier_Control, true, false, nullptr);
    ASSERT_EQ_MSG((u64)count, selection_set_len(set), "Ctrl+A selects every id in the ordered slice");
    ASSERT_TRUE_MSG(selection_set_contains(set, 100), "an id whose row was never emitted is still selected");
    ASSERT_EQ_MSG(1u, r.request_count, "Ctrl+A emits a single SetAll");
    ASSERT_EQ_MSG((int)FlSelectionRequestKind_SetAll, (int)r.requests[0].kind, "the request is SetAll");

    selection_set_destroy(set);
}

// A NULL set opts out of auto-apply and highlight: the gesture still emits its requests on the result (so the app
// can drive its own model), but nothing is applied and the row's Signal.selected stays false.
UTEST(list_selection, null_set_emits_requests_without_applying) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const u32 count = 20;
    u64 ids[20];
    for (u32 i = 0; i < count; i++) {
        ids[i] = (u64)(i + 1);
    }
    const FlLayoutId list = ID("sel_null");
    sel_setup(ctx, list, nullptr, ids, count, 240.0f);

    FlSignal signals[20] = { 0 };
    sel_only_hot(ctx, list, count, 0);
    FlListResult r = sel_frame(ctx, list, nullptr, ids, count, 240.0f, FlKeyModifier_None, false, true, signals);

    ASSERT_EQ_MSG(2u, r.request_count, "NULL set still exposes the emitted requests");
    ASSERT_EQ_MSG((int)FlSelectionRequestKind_ClearAll, (int)r.requests[0].kind, "first request is ClearAll");
    ASSERT_EQ_MSG((int)FlSelectionRequestKind_SetOne, (int)r.requests[1].kind, "second request is SetOne");
    ASSERT_EQ_MSG((u64)1, r.requests[1].id, "SetOne names the clicked id");
    ASSERT_FALSE_MSG(signals[0].selected, "with a NULL set the clicked row's Signal.selected stays false");
}

// Signal.selected mirrors set membership for each visible row, resolved purely by id - so the app can style its
// own row content from it. Pre-populate the set, then read the per-row signals on an idle frame.
UTEST(list_selection, signal_selected_reflects_membership) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlSelectionSet* set = selection_set_create(16);
    const u32 count = 20;
    u64 ids[20];
    for (u32 i = 0; i < count; i++) {
        ids[i] = (u64)(i + 1);
    }
    const FlLayoutId list = ID("sel_membership");
    FlListRange range = sel_setup(ctx, list, set, ids, count, 240.0f);
    ASSERT_TRUE_MSG(range.end > 2, "row at position 2 must be visible for this check");

    // Select id 3 (position 2) directly, no gesture involved.
    selection_set_set(set, 3, true);

    FlSignal signals[20] = { 0 };
    sel_only_hot(ctx, list, count, -1);
    sel_frame(ctx, list, set, ids, count, 240.0f, FlKeyModifier_None, false, false, signals);
    ASSERT_TRUE_MSG(signals[2].selected, "the row whose id is in the set reports Signal.selected == true");
    ASSERT_FALSE_MSG(signals[0].selected, "a row not in the set reports Signal.selected == false");
    ASSERT_FALSE_MSG(signals[1].selected, "another unselected row reports Signal.selected == false");

    selection_set_destroy(set);
}

// The gesture anchor persists across idle frames (it lives in list-level state keyed by the list id, not on any
// row), so a shift-click ranges from an anchor set many frames earlier - the scroll-survival property in miniature.
UTEST(list_selection, anchor_persists_across_idle_frames) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlSelectionSet* set = selection_set_create(16);
    const u32 count = 100;
    u64 ids[100];
    for (u32 i = 0; i < count; i++) {
        ids[i] = (u64)(i + 1);
    }
    const FlLayoutId list = ID("sel_anchor");
    sel_setup(ctx, list, set, ids, count, 240.0f);

    // Set the anchor at position 1 (stable id 2) with a plain click.
    sel_only_hot(ctx, list, count, 1);
    sel_frame(ctx, list, set, ids, count, 240.0f, FlKeyModifier_None, false, true, nullptr);

    // Several idle frames pass (no clicks, no keys) - the anchor must survive them.
    for (int f = 0; f < 5; f++) {
        sel_only_hot(ctx, list, count, -1);
        sel_frame(ctx, list, set, ids, count, 240.0f, FlKeyModifier_None, false, false, nullptr);
    }

    // Shift-click position 4 (stable id 5): the range still pivots on the long-ago anchor (id 2) -> {2,3,4,5}.
    sel_only_hot(ctx, list, count, 4);
    FlListResult r = sel_frame(ctx, list, set, ids, count, 240.0f, FlKeyModifier_Shift, false, true, nullptr);
    ASSERT_EQ_MSG((u64)4, selection_set_len(set), "shift-range from the persisted anchor spans 2..5");
    for (u64 id = 2; id <= 5; id++) {
        ASSERT_TRUE_MSG(selection_set_contains(set, id), "the span resolves against the surviving anchor");
    }
    ASSERT_EQ_MSG((u64)2, r.requests[1].id, "the range's 'from' endpoint is the anchor set five frames earlier");

    selection_set_destroy(set);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// list_keyboard.* - keyboard navigation + focus participation. The List, once focused, moves an internal focus
// cursor on arrows / Home / End / type-ahead, activates on Enter / Space, extends the selection on Shift+arrows,
// and consumes the nav keys it handles so they do not also drive global focus navigation. These drive the
// synthetic keyboard channels (nav_frame directions, key edges, type-ahead text) the same way the list_selection
// cases drive synthetic clicks.

// Keyboard input injected for one List frame; everything off by default (designated init).
typedef struct KbInput {
    bool nav_up;
    bool nav_down;
    bool home;
    bool end;
    bool enter;
    bool space;
    bool key_a;
    u32 modifiers;
    FlString text; // type-ahead characters typed this frame (empty = none)
} KbInput;

// Emit one List frame with keyboard input injected and the cursor parked off-list (so no hover competes with the
// keyboard focus). Returns the List result; out_nav, if non-NULL, captures the nav_frame AFTER ui_list_end so a
// test can assert the List consumed the directions it handled. labels may be NULL (type-ahead disabled).
static FlListResult kb_frame(FlUiCtx* ctx, FlLayoutId list_id, FlSelectionSet* set, const u64* ids,
                             const FlString* labels, u32 count, f32 viewport_h, KbInput in, FlNavFrame* out_nav) {
    ctx->input_state.mouse.x = -100.0f;
    ctx->input_state.mouse.y = -100.0f;
    ctx->mouse_pressed_this_frame = false; // keyboard-only frame: no press to leak the list's focus blur
    ctx->mouse_released_this_frame = false;
    ctx->input_state.mouse.buttons[FlMouseButton_Left] = false;
    ctx->input_state.keyboard.modifiers = in.modifiers;
    ctx->text_input_this_frame = in.text; // type-ahead reads the per-frame snapshot, not the raw kb field
    ctx->input_state.time += 0.1; // advance the input clock a little (keeps a type-ahead burst inside its window)
    ctx->nav_frame.nav_up = in.nav_up;
    ctx->nav_frame.nav_down = in.nav_down;
    ctx->keys_pressed_this_frame[FlKey_Home] = in.home;
    ctx->keys_pressed_this_frame[FlKey_End] = in.end;
    ctx->keys_pressed_this_frame[FlKey_Enter] = in.enter;
    ctx->keys_pressed_this_frame[FlKey_Space] = in.space;
    ctx->keys_pressed_this_frame[FlKey_A] = in.key_a;
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(12, 12, 14);

    FlListResult result = { 0 };
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(viewport_h) },
                   .direction = FlLayoutDirection_TopToBottom) {
        FlListRange r = ui_list_begin(list_id, ids, count, set,
                                      (FlListConfig) { .row_height = 24.0f, .gap = 4.0f, .labels = labels });
        for (u32 i = r.start; i < r.end; i++) {
            ui_list_item_begin(ids[i]);
            ui_layout_area(.sizing = { sizing_grow(), sizing_fixed(24) }) {}
            ui_list_item_end();
        }
        result = ui_list_end();
    }
    if (out_nav != nullptr) {
        *out_nav = fl_get_nav_frame(); // post-list: a direction the List consumed reads back false
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    // Clear the injected edges so they never leak into a following frame.
    ctx->nav_frame.nav_up = false;
    ctx->nav_frame.nav_down = false;
    ctx->keys_pressed_this_frame[FlKey_Home] = false;
    ctx->keys_pressed_this_frame[FlKey_End] = false;
    ctx->keys_pressed_this_frame[FlKey_Enter] = false;
    ctx->keys_pressed_this_frame[FlKey_Space] = false;
    ctx->keys_pressed_this_frame[FlKey_A] = false;
    ctx->text_input_this_frame = (FlString) { 0 };
    ctx->input_state.keyboard.modifiers = FlKeyModifier_None;
    return result;
}

// Click a visible row (parked-cursor + stamped hot, as the list_selection cases do) to focus the list and land
// the keyboard cursor on it. The click selects the row too, which the callers tolerate.
static void kb_focus_row(FlUiCtx* ctx, FlLayoutId list, FlSelectionSet* set, const u64* ids, u32 count, f32 vp,
                         int row) {
    sel_only_hot(ctx, list, count, row);
    sel_frame(ctx, list, set, ids, count, vp, FlKeyModifier_None, false, true, nullptr);
    sel_only_hot(ctx, list, count, -1); // un-hover so later keyboard frames have no hover competing
}

// Arrows / Home / End move the focus cursor (reported as result.focused_id), and the List consumes the nav
// direction it handled so it does not also drive global spatial navigation. A list with no focus ignores them.
UTEST(list_keyboard, arrows_move_focus_cursor_and_consume) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlSelectionSet* set = selection_set_create(16);
    const u32 count = 100;
    u64 ids[100];
    for (u32 i = 0; i < count; i++) {
        ids[i] = (u64)(i + 1);
    }
    const f32 vp = 120.0f; // small viewport: most of the 100 rows are virtualized away
    const FlLayoutId list = ID("kb_arrows");
    FlListRange range = sel_setup(ctx, list, set, ids, count, vp);
    ASSERT_TRUE_MSG(range.end < count, "viewport must virtualize: not all rows are emitted");

    FlNavFrame nav = { 0 };
    FlListResult r = kb_frame(ctx, list, set, ids, nullptr, count, vp, (KbInput) { .nav_down = true }, &nav);
    ASSERT_EQ_MSG((u64)0, r.focused_id, "an unfocused list ignores arrow keys");
    ASSERT_TRUE_MSG(nav.nav_down, "an unfocused list does not consume the nav key");

    // Click row 0 to focus the list and seat the cursor on stable id 1.
    kb_focus_row(ctx, list, set, ids, count, vp, 0);

    r = kb_frame(ctx, list, set, ids, nullptr, count, vp, (KbInput) { .nav_down = true }, &nav);
    ASSERT_EQ_MSG((u64)2, r.focused_id, "Nav-Down moves the focus cursor to the next row");
    ASSERT_FALSE_MSG(nav.nav_down, "the focused List consumes Nav-Down so it does not also spatial-navigate");
    ASSERT_FALSE_MSG(r.nav_down_edge, "moving off row 0 is not a bottom edge");

    r = kb_frame(ctx, list, set, ids, nullptr, count, vp, (KbInput) { .nav_up = true }, &nav);
    ASSERT_EQ_MSG((u64)1, r.focused_id, "Nav-Up moves the cursor back one row");
    ASSERT_FALSE_MSG(nav.nav_up, "the focused List consumes Nav-Up too");

    // End jumps to the last row (id 100) even though it was virtualized away.
    r = kb_frame(ctx, list, set, ids, nullptr, count, vp, (KbInput) { .end = true }, nullptr);
    ASSERT_EQ_MSG((u64)100, r.focused_id, "End jumps the cursor to the last row");
    r = kb_frame(ctx, list, set, ids, nullptr, count, vp, (KbInput) { .home = true }, nullptr);
    ASSERT_EQ_MSG((u64)1, r.focused_id, "Home jumps the cursor to the first row");

    selection_set_destroy(set);
}

// Enter and Space activate the cursor row, reporting it on result.activated_id.
UTEST(list_keyboard, enter_space_activate_cursor) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlSelectionSet* set = selection_set_create(16);
    const u32 count = 20;
    u64 ids[20];
    for (u32 i = 0; i < count; i++) {
        ids[i] = (u64)(i + 1);
    }
    const f32 vp = 240.0f;
    const FlLayoutId list = ID("kb_activate");
    sel_setup(ctx, list, set, ids, count, vp);
    kb_focus_row(ctx, list, set, ids, count, vp, 0); // cursor on id 1

    // Move the cursor down twice to id 3, then Enter activates it.
    kb_frame(ctx, list, set, ids, nullptr, count, vp, (KbInput) { .nav_down = true }, nullptr);
    kb_frame(ctx, list, set, ids, nullptr, count, vp, (KbInput) { .nav_down = true }, nullptr);
    FlListResult r = kb_frame(ctx, list, set, ids, nullptr, count, vp, (KbInput) { .enter = true }, nullptr);
    ASSERT_EQ_MSG((u64)3, r.activated_id, "Enter activates the cursor row");

    r = kb_frame(ctx, list, set, ids, nullptr, count, vp, (KbInput) { .space = true }, nullptr);
    ASSERT_EQ_MSG((u64)3, r.activated_id, "Space activates the cursor row too");

    r = kb_frame(ctx, list, set, ids, nullptr, count, vp, (KbInput) { 0 }, nullptr);
    ASSERT_EQ_MSG((u64)0, r.activated_id, "no activation on an idle frame");

    selection_set_destroy(set);
}

// Shift+arrows extend the selection from the anchor (the row the cursor came from), endpoint-only so the span
// resolves across virtualized rows; the anchor stays put as the cursor keeps moving.
UTEST(list_keyboard, shift_arrows_extend_from_anchor) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlSelectionSet* set = selection_set_create(16);
    const u32 count = 100;
    u64 ids[100];
    for (u32 i = 0; i < count; i++) {
        ids[i] = (u64)(i + 1);
    }
    const f32 vp = 120.0f;
    const FlLayoutId list = ID("kb_shift");
    sel_setup(ctx, list, set, ids, count, vp);
    kb_focus_row(ctx, list, set, ids, count, vp, 0); // cursor + anchor on id 1, selection {1}

    FlListResult r = kb_frame(ctx, list, set, ids, nullptr, count, vp,
                              (KbInput) { .nav_down = true, .modifiers = FlKeyModifier_Shift }, nullptr);
    ASSERT_EQ_MSG((u64)2, r.focused_id, "Shift+Down still moves the cursor");
    ASSERT_EQ_MSG((u64)2, selection_set_len(set), "Shift+Down extends the selection to two rows");
    ASSERT_TRUE_MSG(selection_set_contains(set, 1) && selection_set_contains(set, 2), "the span is {1,2}");

    r = kb_frame(ctx, list, set, ids, nullptr, count, vp,
                 (KbInput) { .nav_down = true, .modifiers = FlKeyModifier_Shift }, nullptr);
    ASSERT_EQ_MSG((u64)3, selection_set_len(set), "the anchor holds: the span grows to {1,2,3}");
    ASSERT_TRUE_MSG(selection_set_contains(set, 1) && selection_set_contains(set, 3), "the span is {1,2,3}");

    r = kb_frame(ctx, list, set, ids, nullptr, count, vp,
                 (KbInput) { .key_a = true, .modifiers = FlKeyModifier_Control }, nullptr);
    ASSERT_EQ_MSG((u64)count, selection_set_len(set), "Ctrl+A selects every id in the ordered slice");

    selection_set_destroy(set);
}

// Navigating past the first/last row reports an edge flag and leaves the cursor pinned at the boundary.
UTEST(list_keyboard, reports_edge_flags_at_bounds) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlSelectionSet* set = selection_set_create(16);
    const u32 count = 8;
    u64 ids[8];
    for (u32 i = 0; i < count; i++) {
        ids[i] = (u64)(i + 1);
    }
    const f32 vp = 240.0f; // all rows visible
    const FlLayoutId list = ID("kb_edges");
    sel_setup(ctx, list, set, ids, count, vp);
    kb_focus_row(ctx, list, set, ids, count, vp, 0); // cursor on id 1 (first row)

    FlListResult r = kb_frame(ctx, list, set, ids, nullptr, count, vp, (KbInput) { .nav_up = true }, nullptr);
    ASSERT_TRUE_MSG(r.nav_up_edge, "Nav-Up on the first row reports the top edge");
    ASSERT_FALSE_MSG(r.nav_down_edge, "no bottom edge here");
    ASSERT_EQ_MSG((u64)1, r.focused_id, "the cursor stays on the first row at the top edge");

    // Jump to the last row, then Down past it.
    kb_frame(ctx, list, set, ids, nullptr, count, vp, (KbInput) { .end = true }, nullptr);
    r = kb_frame(ctx, list, set, ids, nullptr, count, vp, (KbInput) { .nav_down = true }, nullptr);
    ASSERT_TRUE_MSG(r.nav_down_edge, "Nav-Down on the last row reports the bottom edge");
    ASSERT_FALSE_MSG(r.nav_up_edge, "no top edge here");
    ASSERT_EQ_MSG((u64)count, r.focused_id, "the cursor stays on the last row at the bottom edge");

    selection_set_destroy(set);
}

// Type-ahead jumps the cursor to the next row whose label matches the typed prefix; repeating a single letter
// cycles through the matches (wrapping), and a multi-byte burst matches a longer prefix.
UTEST(list_keyboard, type_ahead_jumps_and_cycles) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlSelectionSet* set = selection_set_create(16);
    const u32 count = 4;
    u64 ids[4] = { 1, 2, 3, 4 };
    // apple / banana / blueberry / cherry: two 'b' rows so the single-letter cycle is observable.
    const FlString labels[4] = { FL_S("apple"), FL_S("banana"), FL_S("blueberry"), FL_S("cherry") };
    const f32 vp = 240.0f;
    const FlLayoutId list = ID("kb_typeahead");
    sel_setup(ctx, list, set, ids, count, vp);
    kb_focus_row(ctx, list, set, ids, count, vp, 0); // cursor on apple (id 1)

    FlListResult r = kb_frame(ctx, list, set, ids, labels, count, vp, (KbInput) { .text = FL_S("b") }, nullptr);
    ASSERT_EQ_MSG((u64)2, r.focused_id, "typing 'b' jumps to the first matching label");

    r = kb_frame(ctx, list, set, ids, labels, count, vp, (KbInput) { .text = FL_S("b") }, nullptr);
    ASSERT_EQ_MSG((u64)3, r.focused_id, "repeating the letter cycles to the next match");

    r = kb_frame(ctx, list, set, ids, labels, count, vp, (KbInput) { .text = FL_S("b") }, nullptr);
    ASSERT_EQ_MSG((u64)2, r.focused_id, "the cycle wraps around");

    selection_set_destroy(set);
}

// A multi-byte type-ahead burst matches a longer prefix, narrowing past rows that share only a shorter one.
UTEST(list_keyboard, type_ahead_matches_multibyte_prefix) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlSelectionSet* set = selection_set_create(16);
    const u32 count = 3;
    u64 ids[3] = { 1, 2, 3 };
    const FlString labels[3] = { FL_S("alpha"), FL_S("alpine"), FL_S("beta") };
    const f32 vp = 240.0f;
    const FlLayoutId list = ID("kb_prefix");
    sel_setup(ctx, list, set, ids, count, vp);
    kb_focus_row(ctx, list, set, ids, count, vp, 2); // cursor on beta (id 3)

    // "alpi" skips alpha (only "alp" matches) and lands on alpine (id 2).
    FlListResult r = kb_frame(ctx, list, set, ids, labels, count, vp, (KbInput) { .text = FL_S("alpi") }, nullptr);
    ASSERT_EQ_MSG((u64)2, r.focused_id, "a multi-byte prefix narrows to the row that matches all of it");

    selection_set_destroy(set);
}

// One frame for the focus-request cases: optionally call ui_list_request_focus(list, seed) just before the list
// begins (mirroring a dropdown's open-edge call), then emit the list over a NULL set. Cursor parked off-list and
// no clicks, so only the seed / injected keys drive focus. Captures the post-frame global focus ring id into
// *out_global if non-NULL.
static FlListResult focusreq_frame(FlUiCtx* ctx, FlLayoutId list_id, const u64* ids, u32 count, f32 vp, bool request,
                                   u64 seed, KbInput in, u32* out_global) {
    ctx->input_state.mouse.x = -100.0f;
    ctx->input_state.mouse.y = -100.0f;
    ctx->mouse_pressed_this_frame = false;
    ctx->mouse_released_this_frame = false;
    ctx->input_state.mouse.buttons[FlMouseButton_Left] = false;
    ctx->input_state.keyboard.modifiers = in.modifiers;
    ctx->input_state.time += 0.1;
    ctx->nav_frame.nav_up = in.nav_up;
    ctx->nav_frame.nav_down = in.nav_down;
    ctx->keys_pressed_this_frame[FlKey_Home] = in.home;
    ctx->keys_pressed_this_frame[FlKey_End] = in.end;
    ctx->keys_pressed_this_frame[FlKey_Enter] = in.enter;
    ctx->keys_pressed_this_frame[FlKey_Space] = in.space;
    ui_background_set(IMAGE_HANDLE_INVALID);
    ui_background_set_clear_color(12, 12, 14);

    FlListResult result = { 0 };
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(vp) }, .direction = FlLayoutDirection_TopToBottom) {
        if (request) {
            ui_list_request_focus(list_id, seed);
        }
        FlListRange r
            = ui_list_begin(list_id, ids, count, nullptr, (FlListConfig) { .row_height = 24.0f, .gap = 4.0f });
        for (u32 i = r.start; i < r.end; i++) {
            ui_list_item_begin(ids[i]);
            ui_layout_area(.sizing = { sizing_grow(), sizing_fixed(24) }) {}
            ui_list_item_end();
        }
        result = ui_list_end();
    }
    if (out_global != nullptr) {
        *out_global = ctx->focused_id;
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ctx->nav_frame.nav_up = false;
    ctx->nav_frame.nav_down = false;
    ctx->keys_pressed_this_frame[FlKey_Home] = false;
    ctx->keys_pressed_this_frame[FlKey_End] = false;
    ctx->keys_pressed_this_frame[FlKey_Enter] = false;
    ctx->keys_pressed_this_frame[FlKey_Space] = false;
    ctx->input_state.keyboard.modifiers = FlKeyModifier_None;
    return result;
}

// Fixture prologue for the focus-request cases: size the surface and run two idle (no-request) frames so the
// container height and the visible rows' ItemStates exist, exactly like sel_setup but over a NULL set.
static void focusreq_setup(FlUiCtx* ctx, FlLayoutId list_id, const u64* ids, u32 count, f32 vp) {
    ctx->layout_width = 320.0f;
    ctx->layout_height = 256.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 320.0f, .height = 256.0f });
    focusreq_frame(ctx, list_id, ids, count, vp, false, 0, (KbInput) { 0 }, nullptr);
    focusreq_frame(ctx, list_id, ids, count, vp, false, 0, (KbInput) { 0 }, nullptr);
}

// The open-edge call engages the list's keyboard focus, claims the global ring, and seeds the cursor on the given
// row - so arrow keys navigate immediately and the focus ring is visible on the seeded row. A list that is never
// asked stays unfocused.
UTEST(list_focus_request, seeds_cursor_engages_focus_and_claims_ring) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const u32 count = 8;
    u64 ids[8];
    for (u32 i = 0; i < count; i++) {
        ids[i] = (u64)(i + 1);
    }
    const f32 vp = 240.0f; // all 8 rows visible so the seeded row's ring is on-screen
    const FlLayoutId list = ID("focusreq_seed");
    const u32 list_clay = ui_layout_id_to_clay_id(list);
    // Pin the default theme so border.focus is the (non-transparent) default.
    ui_set_theme(ctx, nullptr);
    focusreq_setup(ctx, list, ids, count, vp);

    // With no request, an idle frame never ENGAGES the list's keyboard focus: has_focus is set only by a click
    // or Tab, never a bare emission.
    u32 global = 0;
    FlListResult r = focusreq_frame(ctx, list, ids, count, vp, false, 0, (KbInput) { 0 }, &global);
    ASSERT_EQ_MSG((u64)0, r.focused_id, "an un-requested list does not engage keyboard focus (no cursor)");
    ASSERT_FALSE_MSG(focus_ring_drawn(), "no focus ring is drawn before focus is requested");

    // Request focus seeded on the current value (row 2, stable id 3).
    r = focusreq_frame(ctx, list, ids, count, vp, true, 3, (KbInput) { 0 }, &global);
    ASSERT_EQ_MSG((u64)3, r.focused_id, "request_focus seeds and reports the focus cursor on the given row");
    ASSERT_EQ_MSG((u64)list_clay, (u64)global, "request_focus claims the global focus ring so focus sticks");
    ASSERT_TRUE_MSG(focus_ring_drawn(), "the focus ring is drawn on the seeded (current-value) row");

    // Arrow keys navigate immediately, with no prior click.
    r = focusreq_frame(ctx, list, ids, count, vp, false, 0, (KbInput) { .nav_down = true }, nullptr);
    ASSERT_EQ_MSG((u64)4, r.focused_id, "Nav-Down moves the seeded cursor immediately after a focus request");
    r = focusreq_frame(ctx, list, ids, count, vp, false, 0, (KbInput) { .home = true }, nullptr);
    ASSERT_EQ_MSG((u64)1, r.focused_id, "Home jumps the cursor to the first row");
}

// The re-arm guard: once the list holds focus, a repeated request (an accidental every-frame call) is redundant -
// it must not re-seed the cursor and yank it back mid-navigation.
UTEST(list_focus_request, rearm_guard_does_not_stomp_roving_cursor) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const u32 count = 8;
    u64 ids[8];
    for (u32 i = 0; i < count; i++) {
        ids[i] = (u64)(i + 1);
    }
    const f32 vp = 240.0f;
    const FlLayoutId list = ID("focusreq_rearm");
    focusreq_setup(ctx, list, ids, count, vp);

    // Open-edge request seeds the cursor on row 0 (id 1), then the user arrows down twice to id 3.
    focusreq_frame(ctx, list, ids, count, vp, true, 1, (KbInput) { 0 }, nullptr);
    focusreq_frame(ctx, list, ids, count, vp, false, 0, (KbInput) { .nav_down = true }, nullptr);
    FlListResult r = focusreq_frame(ctx, list, ids, count, vp, false, 0, (KbInput) { .nav_down = true }, nullptr);
    ASSERT_EQ_MSG((u64)3, r.focused_id, "two Nav-Downs rove the cursor to id 3");

    // A stray every-frame request (seeded back at id 1) fires while the list already holds focus.
    r = focusreq_frame(ctx, list, ids, count, vp, true, 1, (KbInput) { 0 }, nullptr);
    ASSERT_EQ_MSG((u64)3, r.focused_id, "the re-arm guard keeps the roving cursor in place on a repeated request");
}

// The seed is validated against the ordered slice in begin: cursor_id 0 (unset) and an id absent from the slice
// both fall back to the first row.
UTEST(list_focus_request, seed_validated_with_first_row_fallback) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const u32 count = 5;
    u64 ids[5] = { 10, 20, 30, 40, 50 }; // non-contiguous ids: an out-of-slice seed is unambiguous
    const f32 vp = 240.0f;

    // cursor_id 0 means "first row" -> seeds ids[0] (10).
    const FlLayoutId list_zero = ID("focusreq_zero");
    focusreq_setup(ctx, list_zero, ids, count, vp);
    FlListResult r = focusreq_frame(ctx, list_zero, ids, count, vp, true, 0, (KbInput) { 0 }, nullptr);
    ASSERT_EQ_MSG((u64)10, r.focused_id, "cursor_id 0 seeds the first row");

    const FlLayoutId list_bad = ID("focusreq_bad");
    focusreq_setup(ctx, list_bad, ids, count, vp);
    r = focusreq_frame(ctx, list_bad, ids, count, vp, true, 9999, (KbInput) { 0 }, nullptr);
    ASSERT_EQ_MSG((u64)10, r.focused_id, "an out-of-slice cursor_id falls back to the first row");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The marquee (rubber-band) gesture and its virtualization invariant. A drag begun on empty list space sweeps a
// rectangle and selects the rows it covers; dragging past the bottom edge auto-scrolls and keeps extending the band,
// so the covered span reaches rows that were never instantiated on the press frame. Coverage must resolve to MODEL
// POSITIONS (arithmetic on content Y), never per-item state.
//
// This drives an app-owned context via the REAL input layer every frame (fl_platform_input_update), so the
// click-vs-drag threshold and the empty-space hit-test are genuine. It is grouped with the other fl_app cases
// because fl_app_destroy tears down the global UI context - a render-fixture case must never follow it.

// Stable id of position p in this fixture's model (1-based, matching the ids[] below).
#define MARQUEE_ID_AT(p) ((u64)((p) + 1))

// Lays list_id out as a 100-row List in a fixed 120px viewport at the window origin. row_height 24 + gap 12 ->
// 36px stride, so the gap region after row 0 is screen y [24, 36] - empty space a press can land in. modifiers
// (FlKeyModifier_*) feeds ui_keyboard_modifiers; it - and nav_down, for the focus-retention case - is stamped
// AFTER fl_platform_input_update so the input layer can't clear it. out_result, if non-NULL, captures the List
// result so has_focus-gated outcomes (the roving cursor on result.focused_id) are observable.
static FlListRange marquee_frame_ex(FlUiCtx* ctx, FlLayoutId list_id, FlSelectionSet* set, const u64* ids, u32 count,
                                    u32 modifiers, float mx, float my, bool held, bool pressed, bool released,
                                    bool nav_down, FlListResult* out_result) {
    FlInputState* in = &ctx->input_state;
    fl_platform_input_set_mouse_position(ctx, mx, my);
    in->mouse.buttons[FlMouseButton_Left] = held;
    in->mouse.buttons_pressed[FlMouseButton_Left] = pressed;
    in->mouse.buttons_released[FlMouseButton_Left] = released;
    fl_platform_input_update(ctx, 1.0f / 60.0f);
    in->keyboard.modifiers = modifiers;
    ctx->nav_frame.nav_down = nav_down;

    FlListRange range = { 0, 0, 0 };
    ui_update_begin(ctx);
    ui_layout_area_id(ID("marquee_root"), .sizing = { sizing_fixed(300), sizing_fixed(120) },
                      .direction = LayoutDirection_TopToBottom) {
        range = ui_list_begin(list_id, ids, count, set, (FlListConfig) { .row_height = 24.0f, .gap = 12.0f });
        for (u32 i = range.start; i < range.end; i++) {
            ui_list_item_begin(ids[i]);
            ui_layout_area(.sizing = { sizing_grow(), sizing_fixed(24) }) {}
            ui_list_item_end();
        }
        FlListResult result = ui_list_end();
        if (out_result != nullptr) {
            *out_result = result;
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    ctx->nav_frame.nav_down = false; // never leak the injected edge into a following frame
    return range;
}

static FlListRange marquee_frame(FlUiCtx* ctx, FlLayoutId list_id, FlSelectionSet* set, const u64* ids, u32 count,
                                 u32 modifiers, float mx, float my, bool held, bool pressed, bool released) {
    return marquee_frame_ex(ctx, list_id, set, ids, count, modifiers, mx, my, held, pressed, released, false, nullptr);
}

UTEST(list_marquee, rubber_band_with_autoscroll) {
    FlAppSettings settings = { .window_title = FL_S("list_marquee"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = ui_get_ctx();
    ctx->ui_scale = 1.0f; // logical == physical, so the list lands at the window origin and content Y == screen Y
    ctx->layout_width = 400.0f;
    ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });

    const u32 count = 100;
    u64 ids[100];
    for (u32 i = 0; i < count; i++) {
        ids[i] = MARQUEE_ID_AT(i);
    }
    FlSelectionSet* set = selection_set_create(64);

    const FlLayoutId list = ID("marquee_list");

    // Warm-up (button up): frame 1 lets list_uniform measure the container; frame 2 emits the true visible window
    // and resolves the container rect the marquee reads. Capture the visible range to prove later rows are off-screen.
    marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, 150.0f, 50.0f, false, false, false);
    FlListRange visible
        = marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, 150.0f, 50.0f, false, false, false);
    ASSERT_TRUE_MSG(visible.end < 11, "viewport must virtualize: position 11 is off-screen at the start");

    // The press point: screen y = 30 sits in the gap between row 0 ([0,24]) and row 1 ([36,60]) - empty space,
    // so the gesture arms as a marquee rather than a row click. The anchor lands just below row 0.
    const float press_x = 150.0f, press_y = 30.0f;

    // (1) Click-vs-drag threshold: a press + sub-threshold (2px) move + release on empty space must NOT marquee -
    // it reads as a click, leaving the selection untouched.
    marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, press_x, press_y, true, true, false); // press
    marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, press_x, press_y + 2.0f, true, false,
                  false); // 2px < thr
    marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, press_x, press_y + 2.0f, false, false,
                  true); // release
    ASSERT_EQ_MSG((u64)0, selection_set_len(set), "a sub-threshold empty-space press+release does not marquee");

    // (2) A real marquee drag: press on empty space, then drag well past the threshold but still inside the
    // viewport. Covers the rows the band sweeps - and only those (row 0, above the anchor, stays unselected).
    marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, press_x, press_y, true, true, false); // press (arms)
    marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, press_x, 100.0f, true, false,
                  false); // drag (inside vp)
    ASSERT_TRUE_MSG(selection_set_contains(set, MARQUEE_ID_AT(1)), "the marquee covers row 1");
    ASSERT_TRUE_MSG(selection_set_contains(set, MARQUEE_ID_AT(2)), "the marquee covers row 2");
    ASSERT_FALSE_MSG(selection_set_contains(set, MARQUEE_ID_AT(0)), "row 0, above the anchor, is not covered");
    ASSERT_FALSE_MSG(selection_set_contains(set, MARQUEE_ID_AT(3)), "rows below the band are not covered yet");

    // (3) Auto-scroll: hold the cursor well past the bottom edge across many frames. The list scrolls and the band
    // keeps extending, pulling in rows that were never instantiated on the press frame.
    for (int f = 0; f < 60; f++) {
        marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, press_x, 220.0f, true, false, false);
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

    marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, press_x, 220.0f, false, false, true);
    u64 len_after_release = selection_set_len(set);
    marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, press_x, 220.0f, false, false, false);
    ASSERT_EQ_MSG(len_after_release, selection_set_len(set), "the selection is stable once the marquee is released");

    // (4) Additive (ctrl/⌘) marquee: with Control held, the swept band UNIONS onto the existing selection rather
    // than replacing it. A fresh list id starts at scroll 0; a sentinel id far outside the band must survive the
    // drag (the replace-mode ClearAll is skipped).
    const FlLayoutId list2 = ID("marquee_list2");
    const u64 sentinel = MARQUEE_ID_AT(98); // off-screen, far below the band
    selection_set_clear_all(set);
    selection_set_set(set, sentinel, true);
    // Warm up with the cursor already in the gap (press point): if it hovered a row first, that row would be hot
    // when the press lands and the one-frame hot lag would latch the row as a click instead of arming the marquee.
    marquee_frame(ctx, list2, set, ids, count, FlKeyModifier_None, press_x, press_y, false, false, false); // warm-up
    marquee_frame(ctx, list2, set, ids, count, FlKeyModifier_None, press_x, press_y, false, false, false); // warm-up
    marquee_frame(ctx, list2, set, ids, count, FlKeyModifier_Control, press_x, press_y, true, true,
                  false); // ctrl press
    marquee_frame(ctx, list2, set, ids, count, FlKeyModifier_Control, press_x, 100.0f, true, false, false); // ctrl drag
    ASSERT_TRUE_MSG(selection_set_contains(set, MARQUEE_ID_AT(1)), "additive marquee covers the swept rows");
    ASSERT_TRUE_MSG(selection_set_contains(set, MARQUEE_ID_AT(2)), "additive marquee covers the swept rows");
    ASSERT_TRUE_MSG(selection_set_contains(set, sentinel),
                    "additive (ctrl) marquee unions onto the existing selection — the sentinel id survives");
    marquee_frame(ctx, list2, set, ids, count, FlKeyModifier_Control, press_x, 100.0f, false, false, true); // release

    selection_set_destroy(set);
    fl_app_destroy(app);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The press that arms a marquee lands on empty list space (no row consumes it) but must not blur the list's
// keyboard focus; only a press genuinely outside the viewport blurs. Focus is observed through its effect: the
// roving cursor on result.focused_id moves on Nav-Down only while the list holds keyboard focus.

UTEST(list_marquee, press_keeps_keyboard_focus) {
    FlAppSettings settings
        = { .window_title = FL_S("list_marquee_focus"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = ui_get_ctx();
    ctx->ui_scale = 1.0f;
    ctx->layout_width = 400.0f;
    ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });

    const u32 count = 100;
    u64 ids[100];
    for (u32 i = 0; i < count; i++) {
        ids[i] = MARQUEE_ID_AT(i);
    }
    FlSelectionSet* set = selection_set_create(64);
    const FlLayoutId list = ID("marquee_focus_list");
    FlListResult res = { 0 };

    // Warm up hovering row 0 (screen y [0, 24]): frame 1 measures the container, frame 2 resolves the rect and
    // makes row 0 hot, so the press lands as a genuine row click (the one-frame hot lag).
    marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, 150.0f, 12.0f, false, false, false);
    marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, 150.0f, 12.0f, false, false, false);

    // Click row 0: focuses the list and seats the keyboard cursor on id 1.
    marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, 150.0f, 12.0f, true, true, false);  // press
    marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, 150.0f, 12.0f, false, false, true); // release

    // Move to the gap between row 0 and row 1 (empty space) and let the hot state settle so the coming press
    // arms the marquee instead of latching a stale-hot row click.
    marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, 150.0f, 30.0f, false, false, false);
    marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, 150.0f, 30.0f, false, false, false);

    // Marquee: press on empty space (this press must NOT blur the list), drag past the threshold, release.
    marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, 150.0f, 30.0f, true, true, false);   // press (arms)
    marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, 150.0f, 100.0f, true, false, false); // drag
    marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, 150.0f, 100.0f, false, false, true); // release
    ASSERT_TRUE_MSG(selection_set_contains(set, MARQUEE_ID_AT(1)), "the marquee gesture really ran (row 1 swept)");
    ASSERT_TRUE_MSG(selection_set_contains(set, MARQUEE_ID_AT(2)), "the marquee gesture really ran (row 2 swept)");

    // Park the cursor off the list (no press - parking never blurs) so hover can't mask the keyboard cursor,
    // then Nav-Down: the cursor moving from id 1 to id 2 proves the list still holds keyboard focus.
    marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, 350.0f, 300.0f, false, false, false);
    marquee_frame_ex(ctx, list, set, ids, count, FlKeyModifier_None, 350.0f, 300.0f, false, false, false, true, &res);
    ASSERT_EQ_MSG((u64)MARQUEE_ID_AT(1), res.focused_id,
                  "keyboard nav still works after a marquee gesture — the arming press kept the list's focus");

    // A press genuinely outside the viewport still blurs: subsequent Nav-Down is ignored (no roving cursor).
    marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, 350.0f, 300.0f, true, true, false);  // press
    marquee_frame(ctx, list, set, ids, count, FlKeyModifier_None, 350.0f, 300.0f, false, false, true); // release
    marquee_frame_ex(ctx, list, set, ids, count, FlKeyModifier_None, 350.0f, 300.0f, false, false, false, true, &res);
    ASSERT_EQ_MSG((u64)0, res.focused_id, "a press outside the list's viewport still blurs its keyboard focus");

    selection_set_destroy(set);
    fl_app_destroy(app);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A press on a row that crosses the click-vs-drag threshold begins a reorder; on release the List returns a reorder
// request { drag_from_ids, drag_target_id, drag_rel } describing where the drop landed (the app owns the actual
// model mutation). Dragging any row of a multi-selection drags the WHOLE selection. Drives the REAL input layer
// every frame, and is grouped with the other fl_app cases because fl_app_destroy tears down the global UI context.
//
// Fixture geometry mirrors the marquee case: row_height 24 + gap 12 -> 36px stride in a 120px viewport at the
// window origin (ui_scale 1, so content Y == screen Y at scroll 0). Stable ids are (position+1)*10, distinct from
// positions, to prove the request is keyed on identity. Row p occupies screen y [p*36, p*36+24]; its midpoint is
// p*36+12. The drop lands before row ins where ins is decided by which half of the cursor's row it sits in.

// Stable id of position p in this fixture (distinct from the position to prove identity-keying).
#define DRAG_ID_AT(p) ((u64)(((p) + 1) * 10))

static FlListResult drag_frame(FlUiCtx* ctx, FlLayoutId list_id, FlSelectionSet* set, const u64* ids, u32 count,
                               float mx, float my, bool held, bool pressed, bool released) {
    FlInputState* in = &ctx->input_state;
    fl_platform_input_set_mouse_position(ctx, mx, my);
    in->mouse.buttons[FlMouseButton_Left] = held;
    in->mouse.buttons_pressed[FlMouseButton_Left] = pressed;
    in->mouse.buttons_released[FlMouseButton_Left] = released;
    fl_platform_input_update(ctx, 1.0f / 60.0f);

    FlListResult lr = { 0 };
    ui_update_begin(ctx);
    ui_layout_area_id(ID("drag_root"), .sizing = { sizing_fixed(300), sizing_fixed(120) },
                      .direction = LayoutDirection_TopToBottom) {
        FlListRange range
            = ui_list_begin(list_id, ids, count, set, (FlListConfig) { .row_height = 24.0f, .gap = 12.0f });
        for (u32 i = range.start; i < range.end; i++) {
            ui_list_item_begin(ids[i]);
            ui_layout_area(.sizing = { sizing_grow(), sizing_fixed(24) }) {}
            ui_list_item_end();
        }
        lr = ui_list_end();
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    return lr;
}

UTEST(list_drag, reorder_request) {
    FlAppSettings settings = { .window_title = FL_S("list_drag"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = ui_get_ctx();
    ctx->ui_scale = 1.0f; // logical == physical, so the list lands at the window origin and content Y == screen Y
    ctx->layout_width = 400.0f;
    ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });

    const u32 count = 100;
    u64 ids[100];
    for (u32 i = 0; i < count; i++) {
        ids[i] = DRAG_ID_AT(i);
    }
    FlSelectionSet* set = selection_set_create(64);

    const float row0_y = 12.0f;  // row 0 midpoint  (row 0 spans [0, 24])
    const float row2_lo = 90.0f; // row 2 bottom half ([84, 96]) -> insert after row 2 (before row 3)

    // ---------------------------------------------------------------------------------------------------------------
    // (1) Sub-threshold press+release on a row stays a click, never a reorder. Two warm-up frames so row 0 becomes
    // hot (one-frame hot lag), then press + 2px move + release.
    const FlLayoutId list_a = ID("drag_list_a");
    drag_frame(ctx, list_a, set, ids, count, 150.0f, row0_y, false, false, false);
    drag_frame(ctx, list_a, set, ids, count, 150.0f, row0_y, false, false, false);
    drag_frame(ctx, list_a, set, ids, count, 150.0f, row0_y, true, true, false); // press row 0
    FlListResult sub = drag_frame(ctx, list_a, set, ids, count, 150.0f, row0_y + 2.0f, true, false, false); // 2px move
    ASSERT_FALSE_MSG(sub.drag_active, "a sub-threshold move must not begin a reorder drag");
    FlListResult sub_rel
        = drag_frame(ctx, list_a, set, ids, count, 150.0f, row0_y + 2.0f, false, false, true); // release
    ASSERT_FALSE_MSG(sub_rel.reorder_emitted, "a sub-threshold press+release emits no reorder request");
    ASSERT_TRUE_MSG(selection_set_contains(set, DRAG_ID_AT(0)), "the sub-threshold gesture read as a plain click");
    ASSERT_EQ_MSG((u64)1, selection_set_len(set), "a plain click selects exactly the clicked row");

    // ---------------------------------------------------------------------------------------------------------------
    // (2) Single-row drag. Press row 0, drag well past the threshold into row 2's bottom half, release. The
    // in-flight frame exposes the live drag state; the release frame returns the reorder request.
    const FlLayoutId list_b = ID("drag_list_b");
    selection_set_clear_all(set);
    drag_frame(ctx, list_b, set, ids, count, 150.0f, row0_y, false, false, false);
    drag_frame(ctx, list_b, set, ids, count, 150.0f, row0_y, false, false, false);
    drag_frame(ctx, list_b, set, ids, count, 150.0f, row0_y, true, true, false); // press row 0 (arms the drag)

    FlListResult mid = drag_frame(ctx, list_b, set, ids, count, 150.0f, row2_lo, true, false, false); // drag (live)
    ASSERT_TRUE_MSG(mid.drag_active, "crossing the threshold begins the reorder drag (live state exposed)");
    ASSERT_FALSE_MSG(mid.reorder_emitted, "no reorder request until release");
    ASSERT_EQ_MSG((u32)1, mid.drag_from_count, "a single dragged row reports one from-id");
    ASSERT_EQ_MSG(DRAG_ID_AT(0), mid.drag_from_ids[0], "the dragged id is the pressed row's stable id (identity)");
    ASSERT_NE_MSG((int)FlDropRelationship_Into, (int)mid.drag_rel, "v1 never produces Into");

    FlListResult drop = drag_frame(ctx, list_b, set, ids, count, 150.0f, row2_lo, false, false, true); // release (drop)
    ASSERT_TRUE_MSG(drop.reorder_emitted, "releasing an active drag emits a reorder request");
    ASSERT_FALSE_MSG(drop.drag_active, "the drop frame is not also an in-flight frame");
    ASSERT_EQ_MSG((u32)1, drop.drag_from_count, "single-row drag: one from-id");
    ASSERT_EQ_MSG(DRAG_ID_AT(0), drop.drag_from_ids[0], "single-row drag: the from-id is the pressed row");
    ASSERT_EQ_MSG(DRAG_ID_AT(2), drop.drag_target_id, "dropped in row 2's bottom half -> target is row 2");
    ASSERT_EQ_MSG((int)FlDropRelationship_After, (int)drop.drag_rel, "bottom half of a row -> drop After it");

    // A subsequent idle frame must not keep emitting (the gesture ended on release).
    FlListResult idle = drag_frame(ctx, list_b, set, ids, count, 150.0f, row2_lo, false, false, false);
    ASSERT_FALSE_MSG(idle.reorder_emitted, "the reorder request fires once, on release, not on later idle frames");
    ASSERT_FALSE_MSG(idle.drag_active, "no drag is in flight after release");

    // ---------------------------------------------------------------------------------------------------------------
    // (3) Multi-row drag: with rows 0 and 1 selected, pressing one of them drags the WHOLE selection. Preselect via
    // the set directly (membership is what the List reads), press row 0, drag into row 2's bottom half, release.
    const FlLayoutId list_c = ID("drag_list_c");
    selection_set_clear_all(set);
    selection_set_set(set, DRAG_ID_AT(0), true);
    selection_set_set(set, DRAG_ID_AT(1), true);
    drag_frame(ctx, list_c, set, ids, count, 150.0f, row0_y, false, false, false);
    drag_frame(ctx, list_c, set, ids, count, 150.0f, row0_y, false, false, false);
    drag_frame(ctx, list_c, set, ids, count, 150.0f, row0_y, true, true, false);   // press row 0 (a selected row)
    drag_frame(ctx, list_c, set, ids, count, 150.0f, row2_lo, true, false, false); // drag past threshold
    FlListResult multi = drag_frame(ctx, list_c, set, ids, count, 150.0f, row2_lo, false, false, true); // release
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

    selection_set_destroy(set);
    fl_app_destroy(app);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The declarative one-call face ui_list(id, items, count, set, opts): the widget takes an array of {id, label, icon}
// descriptors and the caller-owned set, runs the whole interaction internally (virtualization, selection, keyboard
// nav, marquee, drag-reorder, auto-apply), and the app reads only the policy outcomes off the result. Geometry
// mirrors the drag case: row_height 24 + gap 12 -> 36px stride in a 120px viewport at the window origin (ui_scale 1,
// content Y == screen Y at scroll 0). Stable ids are (position+1)*10, distinct from positions, so the result is
// proven keyed on identity rather than dense position.

#define DECL_ID_AT(p) ((u64)(((p) + 1) * 10))

static FlListResult decl_frame_ex(FlUiCtx* ctx, FlLayoutId list_id, const FlListItem* items, u32 count,
                                  FlSelectionSet* set, float mx, float my, bool held, bool pressed, bool released,
                                  bool right_held, bool enter, u32 mods, FlListOpts opts) {
    FlInputState* in = &ctx->input_state;
    fl_platform_input_set_mouse_position(ctx, mx, my);
    in->mouse.buttons[FlMouseButton_Left] = held;
    in->mouse.buttons_pressed[FlMouseButton_Left] = pressed;
    in->mouse.buttons_released[FlMouseButton_Left] = released;
    in->mouse.buttons[FlMouseButton_Right] = right_held;
    // right_clicked is edge-triggered (fires once on press), so synthesize the press edge too. The declarative
    // cases only ever pulse right for a single frame (a context-menu right-click), so right_held doubles as the edge.
    in->mouse.buttons_pressed[FlMouseButton_Right] = right_held;
    in->keyboard.keys_pressed[FlKey_Enter] = enter; // one-frame edge (fl_platform_input_update clears it afterward)
    in->keyboard.modifiers = mods;                  // a level (ctrl/shift held), read by the click gesture machine
    fl_platform_input_update(ctx, 1.0f / 60.0f);

    FlListResult lr = { 0 };
    ui_update_begin(ctx);
    ui_layout_area_id(ID("decl_root"), .sizing = { sizing_fixed(300), sizing_fixed(120) },
                      .direction = LayoutDirection_TopToBottom) {
        lr = ui_list(list_id, items, count, set, opts);
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    return lr;
}

// No modifiers, full-featured opts (multi_select + reorderable).
static FlListResult decl_frame(FlUiCtx* ctx, FlLayoutId list_id, const FlListItem* items, u32 count,
                               FlSelectionSet* set, float mx, float my, bool held, bool pressed, bool released,
                               bool right_held, bool enter) {
    return decl_frame_ex(ctx, list_id, items, count, set, mx, my, held, pressed, released, right_held, enter,
                         FlKeyModifier_None,
                         (FlListOpts) { .row_height = 24.0f, .gap = 12.0f, .multi_select = true, .reorderable = true });
}

UTEST(list_declarative, policy_outcomes) {
    FlAppSettings settings
        = { .window_title = FL_S("list_declarative"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = ui_get_ctx();
    ctx->ui_scale = 1.0f; // logical == physical, so the list lands at the window origin and content Y == screen Y
    ctx->layout_width = 400.0f;
    ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });

    const u32 count = 10;
    FlListItem items[10];
    char labels[10][8];
    for (u32 i = 0; i < count; i++) {
        snprintf(labels[i], sizeof(labels[i]), "row%u", i);
        items[i] = (FlListItem) {
            .id = DECL_ID_AT(i),
            .label = string_from_cstr_len(labels[i], strlen(labels[i])),
            .icon = FL_IMAGE_HANDLE_INVALID, // label-only rows (icon rendering self-skips a not-ready handle)
        };
    }
    FlSelectionSet* set = selection_set_create(64);

    const float row_x = 150.0f;  // list column midpoint (the list spans x [0, 300))
    const float row1_y = 48.0f;  // row 1 midpoint (row 1 spans content [36, 60])
    const float row2_y = 84.0f;  // row 2 midpoint (row 2 spans content [72, 96])
    const float row0_y = 12.0f;  // row 0 midpoint (row 0 spans content [0, 24])
    const float row2_lo = 90.0f; // row 2 bottom half ([84, 96]) -> drop After row 2

    // Settle hover on the row under the cursor (hover propagates over a couple of frames - see list_stable_id_split).
#define DECL_DWELL(_y)                                                                                \
    do {                                                                                              \
        for (int _f = 0; _f < 4; _f++) {                                                              \
            decl_frame(ctx, list, items, count, set, row_x, (_y), false, false, false, false, false); \
        }                                                                                             \
    } while (0)

    // ---------------------------------------------------------------------------------------------------------------
    // (1) Activation. Dwell so row 1 is hot, click it (press then release) to focus the list and land the keyboard
    // cursor on row 1, then Enter on the next frame activates the cursor row.
    const FlLayoutId list = ID("decl_list");
    DECL_DWELL(row1_y);
    decl_frame(ctx, list, items, count, set, row_x, row1_y, true, true, false, false, false);  // press row 1
    decl_frame(ctx, list, items, count, set, row_x, row1_y, false, false, true, false, false); // release -> click
    ASSERT_TRUE_MSG(selection_set_contains(set, DECL_ID_AT(1)), "a plain click auto-applies into the caller's set");
    ASSERT_EQ_MSG((u64)1, selection_set_len(set), "plain click selects exactly the clicked row");

    FlListResult act = decl_frame(ctx, list, items, count, set, row_x, row1_y, false, false, false, false, true);
    ASSERT_EQ_MSG(DECL_ID_AT(1), act.activated_id, "Enter activates the focused cursor row (the clicked row)");

    // ---------------------------------------------------------------------------------------------------------------
    // (2) Context-menu id - the List opens no menu itself. Move the cursor onto row 2, dwell for the hover lag,
    // then hold the right button.
    DECL_DWELL(row2_y);
    FlListResult menu = decl_frame(ctx, list, items, count, set, row_x, row2_y, false, false, false, true, false);
    ASSERT_EQ_MSG(DECL_ID_AT(2), menu.context_menu_id, "right-click reports the row's stable id as the menu target");
    ASSERT_EQ_MSG((u64)0, menu.activated_id, "a right-click is not an activation");

    // ---------------------------------------------------------------------------------------------------------------
    // (3) Reorder outcome. Press row 0, drag past the threshold into row 2's bottom half, release.
    DECL_DWELL(row0_y);
    decl_frame(ctx, list, items, count, set, row_x, row0_y, true, true, false, false, false); // press row 0 (arms)
    FlListResult mid = decl_frame(ctx, list, items, count, set, row_x, row2_lo, true, false, false, false, false);
    ASSERT_TRUE_MSG(mid.drag_active, "crossing the threshold begins the reorder drag");
    FlListResult drop = decl_frame(ctx, list, items, count, set, row_x, row2_lo, false, false, true, false, false);
    ASSERT_TRUE_MSG(drop.reorder_emitted, "releasing an active drag emits a reorder request");
    ASSERT_EQ_MSG((u32)1, drop.drag_from_count, "an unselected pressed row drags just itself");
    ASSERT_EQ_MSG(DECL_ID_AT(0), drop.drag_from_ids[0], "the dragged id is the pressed row's stable id");
    ASSERT_EQ_MSG(DECL_ID_AT(2), drop.drag_target_id, "dropped in row 2's bottom half -> target is row 2");
    ASSERT_EQ_MSG((int)FlDropRelationship_After, (int)drop.drag_rel, "bottom half of a row -> drop After it");

    // The remaining phases run on the SAME fl_app: fl_app_create/destroy double-mounts the VFS, so all declarative
    // phases share one app. Each phase uses a fresh list id so gesture anchor / focus never bleed between them.
    const FlListOpts single = { .row_height = 24.0f, .gap = 12.0f, .multi_select = false, .reorderable = true };
    const FlListOpts no_reorder = { .row_height = 24.0f, .gap = 12.0f, .multi_select = true, .reorderable = false };

    // ---------------------------------------------------------------------------------------------------------------
    // (4) multi_select = false: the extend/toggle modifiers are ignored, so a ctrl-click replaces rather than adds
    // (the selection never grows past one row). Click row 0, then ctrl-click row 2.
    selection_set_clear_all(set);
    const FlLayoutId list_s = ID("decl_single");
    for (int f = 0; f < 4; f++) {
        decl_frame_ex(ctx, list_s, items, count, set, row_x, row0_y, false, false, false, false, false,
                      FlKeyModifier_None, single);
    }
    decl_frame_ex(ctx, list_s, items, count, set, row_x, row0_y, true, true, false, false, false, FlKeyModifier_None,
                  single); // press row 0
    decl_frame_ex(ctx, list_s, items, count, set, row_x, row0_y, false, false, true, false, false, FlKeyModifier_None,
                  single); // release -> click row 0
    ASSERT_TRUE_MSG(selection_set_contains(set, DECL_ID_AT(0)), "single-select: plain click selects row 0");
    for (int f = 0; f < 4; f++) {
        decl_frame_ex(ctx, list_s, items, count, set, row_x, row2_y, false, false, false, false, false,
                      FlKeyModifier_Control, single);
    }
    decl_frame_ex(ctx, list_s, items, count, set, row_x, row2_y, true, true, false, false, false, FlKeyModifier_Control,
                  single); // ctrl-press row 2
    decl_frame_ex(ctx, list_s, items, count, set, row_x, row2_y, false, false, true, false, false,
                  FlKeyModifier_Control,
                  single); // ctrl-release -> ctrl-click row 2
    ASSERT_EQ_MSG((u64)1, selection_set_len(set), "single-select ignores ctrl: the selection never grows past one");
    ASSERT_TRUE_MSG(selection_set_contains(set, DECL_ID_AT(2)), "single-select ctrl-click replaces with row 2");
    ASSERT_FALSE_MSG(selection_set_contains(set, DECL_ID_AT(0)), "single-select drops the previously selected row");

    // ---------------------------------------------------------------------------------------------------------------
    // (5) reorderable = false: a press-and-drag past the threshold never begins a reorder (no drag arms).
    const FlLayoutId list_r = ID("decl_noreorder");
    selection_set_clear_all(set);
    for (int f = 0; f < 2; f++) {
        decl_frame_ex(ctx, list_r, items, count, set, row_x, row0_y, false, false, false, false, false,
                      FlKeyModifier_None, no_reorder);
    }
    decl_frame_ex(ctx, list_r, items, count, set, row_x, row0_y, true, true, false, false, false, FlKeyModifier_None,
                  no_reorder); // press row 0
    FlListResult nr_mid = decl_frame_ex(ctx, list_r, items, count, set, row_x, row2_lo, true, false, false, false,
                                        false, FlKeyModifier_None, no_reorder); // drag past threshold
    ASSERT_FALSE_MSG(nr_mid.drag_active, "reorderable=false: crossing the threshold does not begin a drag");
    FlListResult nr_drop = decl_frame_ex(ctx, list_r, items, count, set, row_x, row2_lo, false, false, true, false,
                                         false, FlKeyModifier_None, no_reorder); // release
    ASSERT_FALSE_MSG(nr_drop.reorder_emitted, "reorderable=false: releasing emits no reorder request");

    // ---------------------------------------------------------------------------------------------------------------
    // (6) Empty / NULL edges: count==0 and items==NULL both render a clean empty list (no crash, zero result). The
    // NULL-items case feeds a non-zero count to prove list_decl_impl ignores the count without its backing array.
    FlListResult empty = decl_frame_ex(ctx, ID("decl_empty"), items, 0, nullptr, -100.0f, -100.0f, false, false, false,
                                       false, false, FlKeyModifier_None, no_reorder);
    ASSERT_EQ_MSG((u64)0, empty.activated_id, "empty list: nothing activated");
    ASSERT_EQ_MSG((u64)0, empty.context_menu_id, "empty list: no context menu");
    ASSERT_FALSE_MSG(empty.reorder_emitted, "empty list: no reorder");
    FlListResult null_items = decl_frame_ex(ctx, ID("decl_null"), nullptr, count, nullptr, -100.0f, -100.0f, false,
                                            false, false, false, false, FlKeyModifier_None, no_reorder);
    ASSERT_EQ_MSG((u64)0, null_items.activated_id, "NULL items with a non-zero count is treated as empty (no crash)");

    selection_set_destroy(set);
    fl_app_destroy(app);
}
