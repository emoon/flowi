// ui_list keyboard navigation, type-ahead and focus-request tests

#include "list/common.h"

// Keyboard navigation and focus. A focused List moves an internal focus cursor on arrows, Home, End and
// type-ahead, activates on Enter and Space, and extends the selection on Shift+arrows. It consumes the nav keys
// it handles so they do not also drive global focus navigation. The cases inject nav_frame directions, key edges
// and type-ahead text, as the list_selection cases inject clicks.

// Keyboard input injected for one List frame. Everything is off by default.
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

static void kb_nav_keys_set(FlUiCtx* ctx, const KbInput* in) {
    ctx->input_state.keyboard.modifiers = in->modifiers;
    ctx->input_state.time += 0.1; // small enough to keep a type-ahead burst inside its window
    ctx->nav_frame.nav_up = in->nav_up;
    ctx->nav_frame.nav_down = in->nav_down;
    ctx->keys_pressed_this_frame[FlKey_Home] = in->home;
    ctx->keys_pressed_this_frame[FlKey_End] = in->end;
    ctx->keys_pressed_this_frame[FlKey_Enter] = in->enter;
    ctx->keys_pressed_this_frame[FlKey_Space] = in->space;
}

// Clears the injected edges so they do not reach a later frame.
static void kb_nav_keys_clear(FlUiCtx* ctx) {
    ctx->nav_frame.nav_up = false;
    ctx->nav_frame.nav_down = false;
    ctx->keys_pressed_this_frame[FlKey_Home] = false;
    ctx->keys_pressed_this_frame[FlKey_End] = false;
    ctx->keys_pressed_this_frame[FlKey_Enter] = false;
    ctx->keys_pressed_this_frame[FlKey_Space] = false;
    ctx->input_state.keyboard.modifiers = FlKeyModifier_None;
}

// Emits one List frame with keyboard input injected and the cursor parked off the list, so no hover competes with
// the keyboard focus. out_nav, if non-null, receives the nav_frame after fl_list_end, so a test can check that
// the List consumed the directions it handled. A nullptr model labels disables type-ahead.
static FlListResult kb_frame(FlUiCtx* ctx, const ListModel* m, KbInput in, FlNavFrame* out_nav) {
    list_park_cursor(ctx);
    list_release_mouse(ctx); // a press here would blur the list's focus
    kb_nav_keys_set(ctx, &in);
    ctx->text_input_this_frame = in.text; // type-ahead reads the per-frame snapshot
    ctx->keys_pressed_this_frame[FlKey_A] = in.key_a;

    FlListResult result = list_emit_frame(ctx, m, &(ListFrameOpts) { .out_nav = out_nav });

    kb_nav_keys_clear(ctx);
    ctx->keys_pressed_this_frame[FlKey_A] = false;
    ctx->text_input_this_frame = (FlString) { 0 };
    return result;
}

// Clicks a visible row (parked cursor and stamped hot, as in the list_selection cases) to focus the list and put
// the keyboard cursor on it. The click also selects the row.
static void kb_focus_row(FlUiCtx* ctx, const ListModel* m, int row) {
    list_sel_click_row(ctx, m, row, FlKeyModifier_None);
    list_sel_only_hot(ctx, m, -1); // no row is hovered in the keyboard frames that follow
}

// Arrows, Home and End move the focus cursor, reported as result.focused_id. The List consumes the nav direction
// it handled so it does not also drive global spatial navigation. A list with no focus ignores them.
UTEST(list_keyboard, arrows_move_focus_cursor_and_consume) {
    LIST_REQUIRE_FIXTURE(ctx);
    ListModel m;
    list_model_init(&m, ID("kb_arrows"), 100, 120.0f); // most of the 100 rows are virtualized away
    m.set = selection_set_create(16);
    FlListRange range = list_sel_setup(ctx, &m);
    ASSERT_TRUE_MSG(range.end < m.count, "viewport must virtualize: not all rows are emitted");

    FlNavFrame nav = { 0 };
    FlListResult r = kb_frame(ctx, &m, (KbInput) { .nav_down = true }, &nav);
    ASSERT_EQ_MSG((u64)0, r.focused_id, "an unfocused list ignores arrow keys");
    ASSERT_TRUE_MSG(nav.nav_down, "an unfocused list does not consume the nav key");

    // Click row 0 to focus the list and put the cursor on stable id 1.
    kb_focus_row(ctx, &m, 0);

    r = kb_frame(ctx, &m, (KbInput) { .nav_down = true }, &nav);
    ASSERT_EQ_MSG((u64)2, r.focused_id, "Nav-Down moves the focus cursor to the next row");
    ASSERT_FALSE_MSG(nav.nav_down, "the focused List consumes Nav-Down so it does not also spatial-navigate");
    ASSERT_FALSE_MSG(r.nav_down_edge, "moving off row 0 is not a bottom edge");

    r = kb_frame(ctx, &m, (KbInput) { .nav_up = true }, &nav);
    ASSERT_EQ_MSG((u64)1, r.focused_id, "Nav-Up moves the cursor back one row");
    ASSERT_FALSE_MSG(nav.nav_up, "the focused List consumes Nav-Up too");

    // End jumps to the last row (id 100) even though it was virtualized away.
    r = kb_frame(ctx, &m, (KbInput) { .end = true }, nullptr);
    ASSERT_EQ_MSG((u64)100, r.focused_id, "End jumps the cursor to the last row");
    r = kb_frame(ctx, &m, (KbInput) { .home = true }, nullptr);
    ASSERT_EQ_MSG((u64)1, r.focused_id, "Home jumps the cursor to the first row");

    selection_set_destroy(m.set);
}

UTEST(list_keyboard, enter_space_activate_cursor) {
    LIST_REQUIRE_FIXTURE(ctx);
    ListModel m;
    list_model_init(&m, ID("kb_activate"), 20, 240.0f);
    m.set = selection_set_create(16);
    list_sel_setup(ctx, &m);
    kb_focus_row(ctx, &m, 0); // cursor on id 1

    // Move the cursor down twice to id 3, then Enter activates it.
    kb_frame(ctx, &m, (KbInput) { .nav_down = true }, nullptr);
    kb_frame(ctx, &m, (KbInput) { .nav_down = true }, nullptr);
    FlListResult r = kb_frame(ctx, &m, (KbInput) { .enter = true }, nullptr);
    ASSERT_EQ_MSG((u64)3, r.activated_id, "Enter activates the cursor row");

    r = kb_frame(ctx, &m, (KbInput) { .space = true }, nullptr);
    ASSERT_EQ_MSG((u64)3, r.activated_id, "Space activates the cursor row too");

    r = kb_frame(ctx, &m, (KbInput) { 0 }, nullptr);
    ASSERT_EQ_MSG((u64)0, r.activated_id, "no activation on an idle frame");

    selection_set_destroy(m.set);
}

// Shift+arrows extend the selection from the anchor, the row the cursor came from. The request holds only the
// endpoints, so the span resolves across virtualized rows. The anchor stays put as the cursor keeps moving.
UTEST(list_keyboard, shift_arrows_extend_from_anchor) {
    LIST_REQUIRE_FIXTURE(ctx);
    ListModel m;
    list_model_init(&m, ID("kb_shift"), 100, 120.0f);
    m.set = selection_set_create(16);
    list_sel_setup(ctx, &m);
    kb_focus_row(ctx, &m, 0); // cursor + anchor on id 1, selection {1}

    const KbInput shift_down = { .nav_down = true, .modifiers = FlKeyModifier_Shift };
    FlListResult r = kb_frame(ctx, &m, shift_down, nullptr);
    ASSERT_EQ_MSG((u64)2, r.focused_id, "Shift+Down still moves the cursor");
    ASSERT_EQ_MSG((u64)2, selection_set_len(m.set), "Shift+Down extends the selection to two rows");
    ASSERT_TRUE_MSG(selection_set_contains(m.set, 1) && selection_set_contains(m.set, 2), "the span is {1,2}");

    r = kb_frame(ctx, &m, shift_down, nullptr);
    ASSERT_EQ_MSG((u64)3, selection_set_len(m.set), "the anchor holds: the span grows to {1,2,3}");
    ASSERT_TRUE_MSG(selection_set_contains(m.set, 1) && selection_set_contains(m.set, 3), "the span is {1,2,3}");

    r = kb_frame(ctx, &m, (KbInput) { .key_a = true, .modifiers = FlKeyModifier_Control }, nullptr);
    ASSERT_EQ_MSG((u64)m.count, selection_set_len(m.set), "Ctrl+A selects every id in the ordered slice");

    selection_set_destroy(m.set);
}

// Navigating past the first or last row reports an edge flag and leaves the cursor at the boundary.
UTEST(list_keyboard, reports_edge_flags_at_bounds) {
    LIST_REQUIRE_FIXTURE(ctx);
    ListModel m;
    list_model_init(&m, ID("kb_edges"), 8, 240.0f); // all rows visible
    m.set = selection_set_create(16);
    list_sel_setup(ctx, &m);
    kb_focus_row(ctx, &m, 0); // cursor on id 1 (first row)

    FlListResult r = kb_frame(ctx, &m, (KbInput) { .nav_up = true }, nullptr);
    ASSERT_TRUE_MSG(r.nav_up_edge, "Nav-Up on the first row reports the top edge");
    ASSERT_FALSE_MSG(r.nav_down_edge, "no bottom edge here");
    ASSERT_EQ_MSG((u64)1, r.focused_id, "the cursor stays on the first row at the top edge");

    // Jump to the last row, then Down past it.
    kb_frame(ctx, &m, (KbInput) { .end = true }, nullptr);
    r = kb_frame(ctx, &m, (KbInput) { .nav_down = true }, nullptr);
    ASSERT_TRUE_MSG(r.nav_down_edge, "Nav-Down on the last row reports the bottom edge");
    ASSERT_FALSE_MSG(r.nav_up_edge, "no top edge here");
    ASSERT_EQ_MSG((u64)m.count, r.focused_id, "the cursor stays on the last row at the bottom edge");

    selection_set_destroy(m.set);
}

// Type-ahead moves the cursor to the next row whose label matches the typed prefix. Repeating a single letter
// cycles through the matches and wraps.
UTEST(list_keyboard, type_ahead_jumps_and_cycles) {
    LIST_REQUIRE_FIXTURE(ctx);
    ListModel m;
    list_model_init(&m, ID("kb_typeahead"), 4, 240.0f);
    m.set = selection_set_create(16);
    // Two rows start with 'b', so the single-letter cycle can be seen.
    const FlString labels[4] = { FL_S("apple"), FL_S("banana"), FL_S("blueberry"), FL_S("cherry") };
    m.config.labels = labels;
    list_sel_setup(ctx, &m);
    kb_focus_row(ctx, &m, 0); // cursor on apple (id 1)

    const KbInput type_b = { .text = FL_S("b") };
    FlListResult r = kb_frame(ctx, &m, type_b, nullptr);
    ASSERT_EQ_MSG((u64)2, r.focused_id, "typing 'b' jumps to the first matching label");

    r = kb_frame(ctx, &m, type_b, nullptr);
    ASSERT_EQ_MSG((u64)3, r.focused_id, "repeating the letter cycles to the next match");

    r = kb_frame(ctx, &m, type_b, nullptr);
    ASSERT_EQ_MSG((u64)2, r.focused_id, "the cycle wraps around");

    selection_set_destroy(m.set);
}

// Several characters typed in one frame match a longer prefix and skip rows that share only a shorter one.
UTEST(list_keyboard, type_ahead_matches_multibyte_prefix) {
    LIST_REQUIRE_FIXTURE(ctx);
    ListModel m;
    list_model_init(&m, ID("kb_prefix"), 3, 240.0f);
    m.set = selection_set_create(16);
    const FlString labels[3] = { FL_S("alpha"), FL_S("alpine"), FL_S("beta") };
    m.config.labels = labels;
    list_sel_setup(ctx, &m);
    kb_focus_row(ctx, &m, 2); // cursor on beta (id 3)

    // "alpi" skips alpha (only "alp" matches) and lands on alpine (id 2).
    FlListResult r = kb_frame(ctx, &m, (KbInput) { .text = FL_S("alpi") }, nullptr);
    ASSERT_EQ_MSG((u64)2, r.focused_id, "a multi-byte prefix narrows to the row that matches all of it");

    selection_set_destroy(m.set);
}

// One frame for the focus-request cases. When request is set it calls fl_list_request_focus(list, seed) just
// before the list begins, as a dropdown does when it opens, then emits the list over a nullptr set. The cursor is
// parked off the list and there are no clicks, so only the seed and the injected keys drive focus. *out_global,
// if non-null, receives the global focus ring id after the frame.
typedef struct FocusReq {
    bool request;
    u64 seed;
    KbInput in;
    u32* out_global;
} FocusReq;

static FlListResult focusreq_frame(FlUiCtx* ctx, const ListModel* m, FocusReq req) {
    list_park_cursor(ctx);
    list_release_mouse(ctx);
    kb_nav_keys_set(ctx, &req.in);

    const ListFrameOpts opts = {
        .out_global_focus = req.out_global,
        .request_focus = req.request,
        .focus_seed = req.seed,
    };
    FlListResult result = list_emit_frame(ctx, m, &opts);

    kb_nav_keys_clear(ctx);
    return result;
}

static FlListResult focusreq_keys(FlUiCtx* ctx, const ListModel* m, KbInput in) {
    return focusreq_frame(ctx, m, (FocusReq) { .in = in });
}

static FlListResult focusreq_request(FlUiCtx* ctx, const ListModel* m, u64 seed) {
    return focusreq_frame(ctx, m, (FocusReq) { .request = true, .seed = seed });
}

// Sizes the surface and runs two idle frames without a request, so the container height and the visible rows'
// ItemStates exist. Like list_sel_setup, but over a nullptr set.
static void focusreq_setup(FlUiCtx* ctx, const ListModel* m) {
    list_prepare_surface(ctx);
    focusreq_frame(ctx, m, (FocusReq) { 0 });
    focusreq_frame(ctx, m, (FocusReq) { 0 });
}

// fl_list_request_focus gives the list keyboard focus, claims the global focus ring and puts the cursor on the
// given row, so arrow keys navigate at once and the focus ring shows on that row. A list that is never asked
// stays unfocused.
UTEST(list_focus_request, seeds_cursor_engages_focus_and_claims_ring) {
    LIST_REQUIRE_FIXTURE(ctx);
    ListModel m;
    list_model_init(&m, ID("focusreq_seed"), 8, 240.0f); // all 8 rows are visible, so the seeded row's ring is on screen
    const u32 list_clay = ui_layout_id_to_clay_id(m.list);
    // The default theme has a border.focus color that is not transparent.
    ui_set_theme(ctx, nullptr);
    focusreq_setup(ctx, &m);

    // Without a request an idle frame does not give the list keyboard focus. has_focus is set only by a click or
    // Tab.
    u32 global = 0;
    FlListResult r = focusreq_frame(ctx, &m, (FocusReq) { .out_global = &global });
    ASSERT_EQ_MSG((u64)0, r.focused_id, "an un-requested list does not engage keyboard focus (no cursor)");
    ASSERT_FALSE_MSG(focus_ring_drawn(), "no focus ring is drawn before focus is requested");

    // Request focus seeded on the current value (row 2, stable id 3).
    r = focusreq_frame(ctx, &m, (FocusReq) { .request = true, .seed = 3, .out_global = &global });
    ASSERT_EQ_MSG((u64)3, r.focused_id, "request_focus seeds and reports the focus cursor on the given row");
    ASSERT_EQ_MSG((u64)list_clay, (u64)global, "request_focus claims the global focus ring so focus sticks");
    ASSERT_TRUE_MSG(focus_ring_drawn(), "the focus ring is drawn on the seeded (current-value) row");

    // Arrow keys navigate at once, with no earlier click.
    r = focusreq_keys(ctx, &m, (KbInput) { .nav_down = true });
    ASSERT_EQ_MSG((u64)4, r.focused_id, "Nav-Down moves the seeded cursor immediately after a focus request");
    r = focusreq_keys(ctx, &m, (KbInput) { .home = true });
    ASSERT_EQ_MSG((u64)1, r.focused_id, "Home jumps the cursor to the first row");
}

// Once the list holds focus, a repeated request (for example one made every frame) does not move the cursor back
// to the seed.
UTEST(list_focus_request, rearm_guard_does_not_stomp_roving_cursor) {
    LIST_REQUIRE_FIXTURE(ctx);
    ListModel m;
    list_model_init(&m, ID("focusreq_rearm"), 8, 240.0f);
    focusreq_setup(ctx, &m);

    // The request puts the cursor on row 0 (id 1), then two Down presses move it to id 3.
    focusreq_request(ctx, &m, 1);
    focusreq_keys(ctx, &m, (KbInput) { .nav_down = true });
    FlListResult r = focusreq_keys(ctx, &m, (KbInput) { .nav_down = true });
    ASSERT_EQ_MSG((u64)3, r.focused_id, "two Nav-Downs rove the cursor to id 3");

    // A second request, seeded at id 1 again, arrives while the list already holds focus.
    r = focusreq_request(ctx, &m, 1);
    ASSERT_EQ_MSG((u64)3, r.focused_id, "the re-arm guard keeps the roving cursor in place on a repeated request");
}

// begin validates the seed against the ordered slice. cursor_id 0 (unset) and an id that is not in the slice
// both fall back to the first row.
UTEST(list_focus_request, seed_validated_with_first_row_fallback) {
    LIST_REQUIRE_FIXTURE(ctx);
    ListModel m;
    list_model_init(&m, ID("focusreq_zero"), 5, 240.0f);
    list_model_scale_ids(&m, 10); // with gaps between ids, a seed outside the slice is unambiguous

    // cursor_id 0 means the first row, so the cursor lands on ids[0] (10).
    focusreq_setup(ctx, &m);
    FlListResult r = focusreq_request(ctx, &m, 0);
    ASSERT_EQ_MSG((u64)10, r.focused_id, "cursor_id 0 seeds the first row");

    m.list = ID("focusreq_bad");
    focusreq_setup(ctx, &m);
    r = focusreq_request(ctx, &m, 9999);
    ASSERT_EQ_MSG((u64)10, r.focused_id, "an out-of-slice cursor_id falls back to the first row");
}
