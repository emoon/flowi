// UI widget, input and selection tests

#include "unit_tests_common.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// selection: FlSelectionSet membership + the Selection Request apply protocol.

UTEST(selection, set_membership) {
    FlSelectionSet* s = selection_set_create(0);
    EXPECT_FALSE(selection_set_contains(s, 42));
    selection_set_set(s, 42, true);
    EXPECT_TRUE(selection_set_contains(s, 42));
    EXPECT_EQ((int)selection_set_len(s), 1);
    selection_set_set(s, 42, true);
    EXPECT_EQ((int)selection_set_len(s), 1);
    selection_set_set(s, 42, false);
    EXPECT_FALSE(selection_set_contains(s, 42));
    EXPECT_EQ((int)selection_set_len(s), 0);
    selection_set_set(s, 7, false);
    EXPECT_EQ((int)selection_set_len(s), 0);
    selection_set_destroy(s);
}

UTEST(selection, clear_all_and_set_all_from_iter) {
    FlSelectionSet* s = selection_set_create(0);
    const u64 ids[] = { 10, 20, 30 };
    selection_set_set_all(s, ids, 3);
    EXPECT_EQ((int)selection_set_len(s), 3);
    EXPECT_TRUE(selection_set_contains(s, 10));
    EXPECT_TRUE(selection_set_contains(s, 30));
    const u64 ids2[] = { 99 }; // set_all replaces (clear-then-insert-all), not unions
    selection_set_set_all(s, ids2, 1);
    EXPECT_EQ((int)selection_set_len(s), 1);
    EXPECT_TRUE(selection_set_contains(s, 99));
    EXPECT_FALSE(selection_set_contains(s, 10));
    selection_set_clear_all(s);
    EXPECT_EQ((int)selection_set_len(s), 0);
    selection_set_destroy(s);
}

UTEST(selection, copy_ids_snapshot) {
    FlSelectionSet* s = selection_set_create(0);
    const u64 ids[] = { 1, 2, 3, 4 };
    selection_set_set_all(s, ids, 4);
    u64 out[8] = { 0 };
    u64 n = selection_set_copy_ids(s, out, 8);
    EXPECT_EQ((int)n, 4);
    u64 sum = 0; // order is unspecified, so check the multiset by sum
    for (u64 i = 0; i < n; i++) {
        sum += out[i];
    }
    EXPECT_EQ((int)sum, 10);
    EXPECT_EQ((int)selection_set_copy_ids(s, out, 2), 2);
    EXPECT_EQ((int)selection_set_copy_ids(s, NULL, 0), 0); // size-first / no-buffer path
    selection_set_destroy(s);
}

UTEST(selection, apply_set_one_no_enumeration_needed) {
    FlSelectionSet* s = selection_set_create(0);
    FlSelectionRequest reqs[] = {
        { .kind = FlSelectionRequestKind_SetOne, .id = 5, .selected = true },
        { .kind = FlSelectionRequestKind_SetOne, .id = 6, .selected = true },
        { .kind = FlSelectionRequestKind_SetOne, .id = 5, .selected = false },
    };
    selection_set_apply(s, reqs, 3, NULL, 0); // SetOne/ClearAll never need the ordered list
    EXPECT_FALSE(selection_set_contains(s, 5));
    EXPECT_TRUE(selection_set_contains(s, 6));
    EXPECT_EQ((int)selection_set_len(s), 1);
    selection_set_destroy(s);
}

UTEST(selection, apply_sequence_is_semantics) {
    FlSelectionSet* s = selection_set_create(0);
    selection_set_set(s, 100, true);
    selection_set_set(s, 200, true);
    FlSelectionRequest reqs[] = {
        { .kind = FlSelectionRequestKind_ClearAll },
        { .kind = FlSelectionRequestKind_SetOne, .id = 7, .selected = true },
    };
    selection_set_apply(s, reqs, 2, NULL, 0);
    EXPECT_EQ((int)selection_set_len(s), 1);
    EXPECT_TRUE(selection_set_contains(s, 7));
    selection_set_destroy(s);
}

UTEST(selection, apply_set_all_resolves_from_ordered_ids) {
    FlSelectionSet* s = selection_set_create(0);
    const u64 model[] = { 11, 22, 33 };
    FlSelectionRequest sel_all = { .kind = FlSelectionRequestKind_SetAll, .selected = true };
    selection_set_apply(s, &sel_all, 1, model, 3);
    EXPECT_EQ((int)selection_set_len(s), 3);
    EXPECT_TRUE(selection_set_contains(s, 22));
    FlSelectionRequest desel_all = { .kind = FlSelectionRequestKind_SetAll, .selected = false };
    selection_set_apply(s, &desel_all, 1, model, 3);
    EXPECT_EQ((int)selection_set_len(s), 0);
    selection_set_destroy(s);
}

UTEST(selection, apply_range_spans_offscreen_ids) {
    // apply walks the full ordered model, not the visible window, so a range spans rows a virtualized List
    // never emitted.
    FlSelectionSet* s = selection_set_create(0);
    const u64 model[] = { 10, 20, 30, 40, 50 };
    FlSelectionRequest r = { .kind = FlSelectionRequestKind_SetRange, .id = 20, .to_id = 40, .selected = true };
    selection_set_apply(s, &r, 1, model, 5);
    EXPECT_FALSE(selection_set_contains(s, 10));
    EXPECT_TRUE(selection_set_contains(s, 20));
    EXPECT_TRUE(selection_set_contains(s, 30));
    EXPECT_TRUE(selection_set_contains(s, 40));
    EXPECT_FALSE(selection_set_contains(s, 50));
    EXPECT_EQ((int)selection_set_len(s), 3);
    selection_set_destroy(s);
}

UTEST(selection, apply_range_is_direction_agnostic) {
    FlSelectionSet* s = selection_set_create(0);
    const u64 model[] = { 10, 20, 30, 40, 50 };
    FlSelectionRequest r = { .kind = FlSelectionRequestKind_SetRange, .id = 40, .to_id = 20, .selected = true };
    selection_set_apply(s, &r, 1, model, 5);
    EXPECT_EQ((int)selection_set_len(s), 3);
    EXPECT_TRUE(selection_set_contains(s, 20));
    EXPECT_TRUE(selection_set_contains(s, 30));
    EXPECT_TRUE(selection_set_contains(s, 40));
    selection_set_destroy(s);
}

UTEST(selection, apply_range_deselect_and_single_endpoint) {
    FlSelectionSet* s = selection_set_create(0);
    const u64 model[] = { 1, 2, 3, 4, 5 };
    selection_set_set_all(s, model, 5);
    FlSelectionRequest deselect = { .kind = FlSelectionRequestKind_SetRange, .id = 2, .to_id = 4, .selected = false };
    selection_set_apply(s, &deselect, 1, model, 5);
    EXPECT_TRUE(selection_set_contains(s, 1));
    EXPECT_FALSE(selection_set_contains(s, 2));
    EXPECT_FALSE(selection_set_contains(s, 3));
    EXPECT_FALSE(selection_set_contains(s, 4));
    EXPECT_TRUE(selection_set_contains(s, 5));
    FlSelectionRequest one = { .kind = FlSelectionRequestKind_SetRange, .id = 3, .to_id = 3, .selected = true };
    selection_set_apply(s, &one, 1, model, 5);
    EXPECT_TRUE(selection_set_contains(s, 3));
    EXPECT_EQ((int)selection_set_len(s), 3); // {1, 3, 5}
    selection_set_destroy(s);
}

#ifdef NDEBUG
UTEST(selection, apply_range_missing_endpoint_is_noop_in_release) {
    // A missing endpoint is an app bug -> debug-assert in debug builds; in release it applies nothing.
    FlSelectionSet* s = selection_set_create(0);
    const u64 model[] = { 1, 2, 3 };
    FlSelectionRequest r = { .kind = FlSelectionRequestKind_SetRange, .id = 2, .to_id = 99, .selected = true };
    selection_set_apply(s, &r, 1, model, 3);
    EXPECT_EQ((int)selection_set_len(s), 0);
    selection_set_destroy(s);
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// scroll_math: the canonical scroll/virtualization kernel (ui/scroll_core.c), shared by ui_scroll,
// list_uniform and content_view.

UTEST(scroll_math, max_never_negative) {
    EXPECT_NEAR(scroll_max(1000.0f, 300.0f), 700.0f, 0.001f);
    EXPECT_EQ(scroll_max(200.0f, 300.0f), 0.0f);
    EXPECT_EQ(scroll_max(300.0f, 300.0f), 0.0f);
    EXPECT_EQ(scroll_max(0.0f, 300.0f), 0.0f);
}

UTEST(scroll_math, smooth_step_eases_then_snaps_and_clamps) {
    f32 dt = 1.0f / 60.0f;
    f32 speed = 8.0f;

    f32 o = scroll_smooth_step(0.0f, 100.0f, 1000.0f, speed, dt);
    EXPECT_NEAR(o, 100.0f * (speed * dt), 0.01f); // ~13.33px after one 60fps frame
    EXPECT_GT(o, 0.0f);
    EXPECT_LT(o, 100.0f);

    // Within half a pixel of the target it snaps exactly...
    EXPECT_EQ(scroll_smooth_step(99.7f, 100.0f, 1000.0f, speed, dt), 100.0f);
    // ...but just beyond the snap epsilon it eases: diff = 0.6 > 0.5.
    f32 near = scroll_smooth_step(100.0f, 100.6f, 1000.0f, speed, dt);
    EXPECT_NEAR(near, 100.0f + 0.6f * (speed * dt), 0.01f);
    EXPECT_GT(near, 100.0f);
    EXPECT_LT(near, 100.6f);

    EXPECT_EQ(scroll_smooth_step(40.0f, 500.0f, 50.0f, speed, dt), 50.0f); // eased past max -> clamped
    EXPECT_EQ(scroll_smooth_step(10.0f, -100.0f, 50.0f, speed, dt), 0.0f); // negative target -> clamped to 0
}

UTEST(scroll_math, smooth_step_is_frame_rate_independent) {
    f32 speed = 8.0f;
    f32 target = 600.0f, max = 1000.0f;

    // From a standstill (linear regime) a single step's progress is exactly target * speed * dt.
    f32 step_120 = scroll_smooth_step(0.0f, target, max, speed, 1.0f / 120.0f);
    f32 step_60 = scroll_smooth_step(0.0f, target, max, speed, 1.0f / 60.0f);
    EXPECT_NEAR(step_120, target * speed / 120.0f, 0.01f); // 40px
    EXPECT_NEAR(step_60, target * speed / 60.0f, 0.01f);   // 80px
    EXPECT_NEAR(step_60, step_120 * 2.0f, 0.01f);

    // Over a fixed wall-clock duration both frame rates converge on the target.
    f32 lo = 0.0f, hi = 0.0f;
    for (int i = 0; i < 60; i++) {
        lo = scroll_smooth_step(lo, target, max, speed, 1.0f / 60.0f); // 1s at 60fps
    }
    for (int i = 0; i < 30; i++) {
        hi = scroll_smooth_step(hi, target, max, speed, 1.0f / 30.0f); // 1s at 30fps
    }
    EXPECT_EQ(lo, target);
    EXPECT_EQ(hi, target);

    // A degenerate dt of 0 makes no progress; a huge dt clamps the lerp factor to 1 (snap to target).
    EXPECT_EQ(scroll_smooth_step(10.0f, 200.0f, 1000.0f, speed, 0.0f), 10.0f);
    EXPECT_EQ(scroll_smooth_step(10.0f, 200.0f, 1000.0f, speed, 10.0f), 200.0f);
}

UTEST(scroll_math, visible_range_virtualization_window) {
    u32 start = 0, end = 0;
    f32 stride = 50.0f; // item height + gap
    f32 viewport = 500.0f;
    u32 buffer = 2;
    u32 total = 100;

    // Scrolled to the top: buffer can't push the window below 0.
    scroll_visible_range(0.0f, viewport, stride, total, buffer, &start, &end);
    EXPECT_EQ(start, 0u);
    // floor(500/50) + 1 + 2*2 = 10 + 1 + 4 = 15 items from index 0.
    EXPECT_EQ(end, 15u);

    // Scrolled into the middle: first visible = floor(1000/50) - 2 = 18.
    scroll_visible_range(1000.0f, viewport, stride, total, buffer, &start, &end);
    EXPECT_EQ(start, 18u);
    EXPECT_EQ(end, 33u);

    scroll_visible_range(4800.0f, viewport, stride, total, buffer, &start, &end);
    EXPECT_EQ(end, total);
    EXPECT_LE(start, end);

    // Degenerate stride must not divide by zero - yields an empty window.
    scroll_visible_range(100.0f, viewport, 0.0f, total, buffer, &start, &end);
    EXPECT_EQ(start, 0u);
    EXPECT_EQ(end, 0u);

    scroll_visible_range(0.0f, viewport, stride, 0, buffer, &start, &end);
    EXPECT_EQ(start, 0u);
    EXPECT_EQ(end, 0u);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// input_repeat: ui_action_repeated (ui/input_actions.c). It reads only the keyboard state + delta_time, so a
// zeroed FlUiCtx drives it deterministically. Actions sharing a physical key but differing by modifier
// (Left vs Ctrl+Left) must never both repeat on the same frame.

UTEST(input_repeat, modifier_disambiguation_no_double_fire) {
    FlUiCtx* ctx = arena_alloc(s_arena, FlUiCtx);
    *ctx = (FlUiCtx) { 0 };
    ctx->delta_time = 1.0f / 60.0f;

    KeyboardState* kb = &ctx->input_state.keyboard;
    kb->keys[FlKey_Left] = true;
    kb->keys[FlKey_Right] = true;
    // Held just past the repeat delay: time_since_delay is a fraction of a frame, so a repeat is due this frame.
    kb->key_hold_duration[FlKey_Left] = UI_NAV_REPEAT_DELAY + ctx->delta_time * 0.5f;
    kb->key_hold_duration[FlKey_Right] = UI_NAV_REPEAT_DELAY + ctx->delta_time * 0.5f;

    kb->modifiers = 0;
    EXPECT_TRUE(ui_action_repeated(ctx, FlAction_Text_Move_Left));
    EXPECT_FALSE(ui_action_repeated(ctx, FlAction_Text_Move_Word_Left));
    EXPECT_TRUE(ui_action_repeated(ctx, FlAction_Text_Move_Right));
    EXPECT_FALSE(ui_action_repeated(ctx, FlAction_Text_Move_Word_Right));

    kb->modifiers = FlKeyModifier_Control;
    EXPECT_FALSE(ui_action_repeated(ctx, FlAction_Text_Move_Left));
    EXPECT_TRUE(ui_action_repeated(ctx, FlAction_Text_Move_Word_Left));
    EXPECT_FALSE(ui_action_repeated(ctx, FlAction_Text_Move_Right));
    EXPECT_TRUE(ui_action_repeated(ctx, FlAction_Text_Move_Word_Right));

    // A key that is not held never repeats.
    EXPECT_FALSE(ui_action_repeated(ctx, FlAction_Text_Backspace));

    // Held but still inside the initial delay window: no repeat yet.
    kb->modifiers = 0;
    kb->key_hold_duration[FlKey_Left] = UI_NAV_REPEAT_DELAY * 0.5f;
    EXPECT_FALSE(ui_action_repeated(ctx, FlAction_Text_Move_Left));
}

// Key codes outside [0, FlKey_Count) must be dropped before any keyboard-state array is indexed: the arrays
// are FlKey_Count long, so an unchecked index corrupts the adjacent KeyboardState fields.
UTEST(input_key_event, out_of_range_key_is_ignored) {
    FlUiCtx* ctx = arena_alloc(s_arena, FlUiCtx);
    *ctx = (FlUiCtx) { 0 };
    KeyboardState* kb = &ctx->input_state.keyboard;

    const FlKey bad_keys[] = { (FlKey)FlKey_Count, (FlKey)(FlKey_Count + 1000), (FlKey)-1 };
    const FlKeyEventType types[] = { FlKeyEvent_Down, FlKeyEvent_Up, FlKeyEvent_Repeat };
    for (u32 k = 0; k < sizeof_array(bad_keys); k++) {
        for (u32 t = 0; t < sizeof_array(types); t++) {
            FlKeyEvent event = { .type = types[t], .key = bad_keys[k], .modifiers = FlKeyModifier_Shift };
            fl_platform_input_push_key_event(ctx, event);
        }
    }

    KeyboardState zero = { 0 };
    EXPECT_EQ(0, memcmp(kb, &zero, sizeof(KeyboardState)));

    FlKeyEvent good = { .type = FlKeyEvent_Down, .key = FlKey_A, .modifiers = FlKeyModifier_Shift };
    fl_platform_input_push_key_event(ctx, good);
    EXPECT_TRUE(kb->keys[FlKey_A]);
    EXPECT_TRUE(kb->keys_pressed[FlKey_A]);
    EXPECT_EQ((u32)FlKeyModifier_Shift, kb->modifiers);
}

UTEST(input, out_of_range_mouse_button_is_ignored) {
    FlUiCtx* ctx = arena_alloc(s_arena, FlUiCtx);
    *ctx = (FlUiCtx) { 0 };
    MouseState* mouse = &ctx->input_state.mouse;

    const FlMouseButton bad[]
        = { (FlMouseButton)FlMouseButton_Count, (FlMouseButton)(FlMouseButton_Count + 1000), (FlMouseButton)-1 };
    const FlMouseEventType types[] = { FlMouseEvent_Button_Down, FlMouseEvent_Button_Up };
    for (u32 b = 0; b < sizeof_array(bad); b++) {
        for (u32 t = 0; t < sizeof_array(types); t++) {
            FlMouseEvent event = { .type = types[t], .button = { .button = bad[b] } };
            fl_platform_input_push_mouse_event(ctx, event);
        }
    }

    MouseState zero = { 0 };
    EXPECT_EQ(0, memcmp(mouse, &zero, sizeof(MouseState)));

    FlMouseEvent good = { .type = FlMouseEvent_Button_Down, .button = { .button = FlMouseButton_Right } };
    fl_platform_input_push_mouse_event(ctx, good);
    EXPECT_TRUE(mouse->buttons[FlMouseButton_Right]);
    EXPECT_TRUE(mouse->buttons_pressed[FlMouseButton_Right]);
}

UTEST(input, out_of_range_gamepad_button_or_axis_is_ignored) {
    FlUiCtx* ctx = arena_alloc(s_arena, FlUiCtx);
    *ctx = (FlUiCtx) { 0 };
    GamepadState* gamepad = &ctx->input_state.gamepads[0];

    const FlGamepadButton bad_buttons[] = { (FlGamepadButton)FlGamepadButton_Count,
                                            (FlGamepadButton)(FlGamepadButton_Count + 1000), (FlGamepadButton)-1 };
    const FlGamepadEventType types[] = { FlGamepadEvent_Button_Down, FlGamepadEvent_Button_Up };
    for (u32 b = 0; b < sizeof_array(bad_buttons); b++) {
        for (u32 t = 0; t < sizeof_array(types); t++) {
            FlGamepadEvent event = { .type = types[t], .gamepad_id = 0, .button = { .button = bad_buttons[b] } };
            fl_platform_input_push_gamepad_event(ctx, event);
        }
    }

    const FlGamepadAxis bad_axes[]
        = { (FlGamepadAxis)FlGamepadAxis_Count, (FlGamepadAxis)(FlGamepadAxis_Count + 1000), (FlGamepadAxis)-1 };
    for (u32 a = 0; a < sizeof_array(bad_axes); a++) {
        FlGamepadEvent event
            = { .type = FlGamepadEvent_Axis_Motion, .gamepad_id = 0, .axis = { .axis = bad_axes[a], .value = 1.0f } };
        fl_platform_input_push_gamepad_event(ctx, event);
    }

    GamepadState zero = { 0 };
    EXPECT_EQ(0, memcmp(gamepad, &zero, sizeof(GamepadState)));

    FlGamepadEvent good_button
        = { .type = FlGamepadEvent_Button_Down, .gamepad_id = 0, .button = { .button = FlGamepadButton_Start } };
    fl_platform_input_push_gamepad_event(ctx, good_button);
    EXPECT_TRUE(gamepad->buttons[FlGamepadButton_Start]);

    FlGamepadEvent good_axis = { .type = FlGamepadEvent_Axis_Motion,
                                 .gamepad_id = 0,
                                 .axis = { .axis = FlGamepadAxis_Left_X, .value = 0.5f } };
    fl_platform_input_push_gamepad_event(ctx, good_axis);
    EXPECT_EQ(0.5f, gamepad->axes[FlGamepadAxis_Left_X]);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// text_buffer: shared character filter / grow helpers (ui/ui_text_buffer.h), used by both the text-input widget
// and the virtual keyboard so their newline policy cannot drift apart.

UTEST(text_buffer, empty_filter_permits_control_whitespace) {
    // With no allow-list the control-whitespace trio is permitted.
    FlString no_filter = (FlString) { .data = NULL, .length = 0 };
    EXPECT_TRUE(text_buffer_filter_char('\n', no_filter));
    EXPECT_TRUE(text_buffer_filter_char('\r', no_filter));
    EXPECT_TRUE(text_buffer_filter_char('\t', no_filter));

    EXPECT_TRUE(text_buffer_filter_char('A', no_filter));
    EXPECT_TRUE(text_buffer_filter_char(' ', no_filter));
    EXPECT_FALSE(text_buffer_filter_char('\b', no_filter));
    EXPECT_FALSE(text_buffer_filter_char((char)0x1b, no_filter)); // ESC
}

UTEST(text_buffer, explicit_allow_list_is_exact) {
    // A non-empty allow-list is an exact set: the newline rule does not re-admit '\n'/'\r'/'\t'.
    FlString digits = (FlString) { .data = "0123456789", .length = 10 };
    EXPECT_TRUE(text_buffer_filter_char('5', digits));
    EXPECT_FALSE(text_buffer_filter_char('a', digits));
    EXPECT_FALSE(text_buffer_filter_char('\n', digits));
    EXPECT_FALSE(text_buffer_filter_char('\t', digits));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(text_buffer, grow_doubles_with_floor_and_preserves) {
    FlArena* arena = arena_new();

    char* buffer = arena_alloc_array(arena, char, 4);
    buffer[0] = 'h';
    buffer[1] = 'i';
    u32 capacity = 4;
    u32 length = 2;

    // Requesting beyond the current capacity grows using a 256-byte floor (doubling 4 would still be tiny).
    text_buffer_grow(arena, &buffer, &capacity, length, capacity + 1);
    EXPECT_EQ(capacity, 256u);
    EXPECT_EQ(buffer[0], 'h');
    EXPECT_EQ(buffer[1], 'i');

    char* before = buffer;
    text_buffer_grow(arena, &buffer, &capacity, length, 100);
    EXPECT_EQ(capacity, 256u);
    EXPECT_EQ_MSG(buffer, before, "grow must be a no-op when capacity already suffices");

    text_buffer_grow(arena, &buffer, &capacity, length, 1000);
    EXPECT_EQ(capacity, 1000u);
    EXPECT_EQ(buffer[0], 'h');
    EXPECT_EQ(buffer[1], 'i');

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// slider_format: ui_slider_format_value (ui/ui.c) - the seam that turns a slider's value + printf format into its
// display string. The int path must apply the format to an integer, so a %d-style format never reaches a float
// formatter. NULL falls back to a bare integer, not "5.00".

UTEST(slider_format, int_arbitrary_formats) {
    FlString a = ui_slider_format_value(s_arena, "%d", 5.0f, true);
    ASSERT_TRUE(a.length == 1 && a.data[0] == '5');

    // Width/padding format - feeding this float-formatted would be undefined behavior.
    FlString b = ui_slider_format_value(s_arena, "%3d", 5.0f, true);
    ASSERT_TRUE(b.length == 3 && b.data[0] == ' ' && b.data[1] == ' ' && b.data[2] == '5');

    FlString c = ui_slider_format_value(s_arena, "%d ms", 42.0f, true);
    ASSERT_TRUE(c.length == 5 && c.data[0] == '4' && c.data[1] == '2' && c.data[2] == ' ' && c.data[3] == 'm'
                && c.data[4] == 's');

    // The value is rounded to the nearest integer before formatting.
    FlString d = ui_slider_format_value(s_arena, "%d", 2.6f, true);
    ASSERT_TRUE(d.length == 1 && d.data[0] == '3');
}

UTEST(slider_format, int_null_and_empty_format) {
    FlString n = ui_slider_format_value(s_arena, nullptr, 5.0f, true);
    ASSERT_TRUE(n.length == 1 && n.data[0] == '5');

    // An empty format yields an empty string.
    FlString e = ui_slider_format_value(s_arena, "", 5.0f, true);
    ASSERT_TRUE(e.length == 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// feedback.* - the semantic UI feedback-event seam. flowi reports what happened in the interaction layer (focus
// moved, a nav edge was hit, a widget was activated) through one app-installed hook. These cases record the
// callback headlessly and assert the emitted kind / from / to / dir.

#define FEEDBACK_MAX_EVENTS 16

typedef struct FeedbackRecorder {
    FlUiFeedbackEvent events[FEEDBACK_MAX_EVENTS];
    int count;
} FeedbackRecorder;

static void feedback_record(const FlUiFeedbackEvent* ev, void* user_data) {
    FeedbackRecorder* r = (FeedbackRecorder*)user_data;
    if (r->count < FEEDBACK_MAX_EVENTS) {
        r->events[r->count] = *ev;
    }
    r->count++;
}

// Lay out two stacked focusable areas (A above B) and run one frame: positions register in item_state, so the
// next frame's spatial nav has real bounding boxes to reason about.
static void feedback_emit_two_focusables(FlUiCtx* ctx, FlLayoutId id_a, FlLayoutId id_b) {
    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .direction = FlLayoutDirection_TopToBottom) {
        ui_layout_area_id(id_a, .sizing = { sizing_fixed(40), sizing_fixed(20) }, .color = ui_color(40, 40, 60, 255)) {
            fl_item_behavior(id_a, FlItemFlags_focusable);
        }
        ui_layout_area_id(id_b, .sizing = { sizing_fixed(40), sizing_fixed(20) }, .color = ui_color(60, 40, 40, 255)) {
            fl_item_behavior(id_b, FlItemFlags_focusable);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

// Lay out a single focusable area and run one frame, returning its interaction signal. Used to warm a widget's
// focus state up (across frames) and to drive the activation frame.
static FlSignal feedback_emit_one_focusable(FlUiCtx* ctx, FlLayoutId id) {
    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });

    ui_update_begin(ctx);
    FlSignal sig = { 0 };
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_layout_area_id(id, .sizing = { sizing_fixed(40), sizing_fixed(20) }, .color = ui_color(40, 40, 60, 255)) {
            sig = fl_item_behavior(id, FlItemFlags_focusable);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    return sig;
}

// feedback.nav_moved_spatial - arrow/D-pad spatial nav from A down to B emits exactly one NavMoved with the
// from/to widget ids and the pressed direction.
UTEST(feedback, nav_moved_spatial) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ctx->focused_id = 0;
    ctx->last_focused_id = 0;

    const FlLayoutId id_a = ID("fb_nav_a");
    const FlLayoutId id_b = ID("fb_nav_b");
    const u32 clay_a = ui_layout_id_to_clay_id(id_a);
    const u32 clay_b = ui_layout_id_to_clay_id(id_b);

    // Frame 1 establishes positions and focuses A; no hook yet, so nothing is recorded.
    feedback_emit_two_focusables(ctx, id_a, id_b);
    ctx->focused_id = clay_a;

    FeedbackRecorder rec = { 0 };
    fl_ui_set_feedback_hook(feedback_record, &rec);
    ctx->nav_frame.nav_down = true;
    feedback_emit_two_focusables(ctx, id_a, id_b);
    ctx->nav_frame.nav_down = false;
    fl_ui_set_feedback_hook(nullptr, nullptr);

    ASSERT_EQ_MSG(rec.count, 1, "spatial nav down emits exactly one feedback event");
    ASSERT_EQ_MSG((int)rec.events[0].kind, (int)FlUiFeedbackKind_NavMoved, "the event is a NavMoved");
    ASSERT_EQ_MSG(rec.events[0].from, clay_a, "from is the previously focused widget (A)");
    ASSERT_EQ_MSG(rec.events[0].to, clay_b, "to is the newly focused widget (B)");
    ASSERT_EQ_MSG((int)rec.events[0].dir, (int)FlUiFeedbackDir_Down, "dir is the pressed direction (Down)");
    ASSERT_EQ_MSG(ctx->focused_id, clay_b, "focus actually moved to B");
}

// feedback.nav_moved_tab_order - Tab/Shift+Tab focus movement is a distinct emit branch from spatial nav: it
// runs through ui_navigate_tab_order and reports dir == None (no spatial direction). A is declared before B, so
// Tab-next from A lands on B.
UTEST(feedback, nav_moved_tab_order) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ctx->focused_id = 0;
    ctx->last_focused_id = 0;

    const FlLayoutId id_a = ID("fb_tab_a");
    const FlLayoutId id_b = ID("fb_tab_b");
    const u32 clay_a = ui_layout_id_to_clay_id(id_a);
    const u32 clay_b = ui_layout_id_to_clay_id(id_b);

    // Frame 1 establishes the focus order and focuses A.
    feedback_emit_two_focusables(ctx, id_a, id_b);
    ctx->focused_id = clay_a;

    FeedbackRecorder rec = { 0 };
    fl_ui_set_feedback_hook(feedback_record, &rec);
    ctx->nav_next_pressed = true;
    feedback_emit_two_focusables(ctx, id_a, id_b);
    ctx->nav_next_pressed = false;
    fl_ui_set_feedback_hook(nullptr, nullptr);

    ASSERT_EQ_MSG(rec.count, 1, "tab-order move emits exactly one feedback event");
    ASSERT_EQ_MSG((int)rec.events[0].kind, (int)FlUiFeedbackKind_NavMoved, "the event is a NavMoved");
    ASSERT_EQ_MSG(rec.events[0].from, clay_a, "from is the previously focused widget (A)");
    ASSERT_EQ_MSG(rec.events[0].to, clay_b, "to is the next widget in tab order (B)");
    ASSERT_EQ_MSG((int)rec.events[0].dir, (int)FlUiFeedbackDir_None, "tab-order carries no spatial direction");
    ASSERT_EQ_MSG(ctx->focused_id, clay_b, "focus actually moved to B");
}

// feedback.nav_blocked_edge - pressing a direction with no candidate (up from the topmost widget) emits
// NavBlocked, focus stays put, and to is 0.
UTEST(feedback, nav_blocked_edge) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ctx->focused_id = 0;
    ctx->last_focused_id = 0;

    const FlLayoutId id_a = ID("fb_edge_a");
    const FlLayoutId id_b = ID("fb_edge_b");
    const u32 clay_a = ui_layout_id_to_clay_id(id_a);

    feedback_emit_two_focusables(ctx, id_a, id_b);
    ctx->focused_id = clay_a; // A is the topmost widget: nothing above it.

    FeedbackRecorder rec = { 0 };
    fl_ui_set_feedback_hook(feedback_record, &rec);
    ctx->nav_frame.nav_up = true;
    feedback_emit_two_focusables(ctx, id_a, id_b);
    ctx->nav_frame.nav_up = false;
    fl_ui_set_feedback_hook(nullptr, nullptr);

    ASSERT_EQ_MSG(rec.count, 1, "an edge hit emits exactly one feedback event");
    ASSERT_EQ_MSG((int)rec.events[0].kind, (int)FlUiFeedbackKind_NavBlocked, "the event is a NavBlocked");
    ASSERT_EQ_MSG(rec.events[0].from, clay_a, "from is the widget that stayed focused (A)");
    ASSERT_EQ_MSG(rec.events[0].to, 0u, "to is 0 — no widget was moved to");
    ASSERT_EQ_MSG((int)rec.events[0].dir, (int)FlUiFeedbackDir_Up, "dir is the pressed direction (Up)");
    ASSERT_EQ_MSG(ctx->focused_id, clay_a, "focus stayed on A");
}

// feedback.activated - activating the focused widget (Enter/Space/Gamepad-A) emits an Activated event naming the
// widget.
UTEST(feedback, activated) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ctx->focused_id = 0;
    ctx->last_focused_id = 0;

    const FlLayoutId id = ID("fb_act");
    const u32 clay_id = ui_layout_id_to_clay_id(id);

    // Warm-up frames: keep the widget focused with no activate press so its focused state settles past the
    // just-gained-focus guard.
    for (int frame = 0; frame < 3; ++frame) {
        ctx->nav_activate_pressed = false;
        feedback_emit_one_focusable(ctx, id);
        ctx->focused_id = clay_id;
    }

    FeedbackRecorder rec = { 0 };
    fl_ui_set_feedback_hook(feedback_record, &rec);
    ctx->nav_activate_pressed = true;
    FlSignal sig = feedback_emit_one_focusable(ctx, id);
    ctx->nav_activate_pressed = false;
    fl_ui_set_feedback_hook(nullptr, nullptr);

    ASSERT_TRUE_MSG(sig.activated, "the focused widget reports activated on the activate press");
    ASSERT_EQ_MSG(rec.count, 1, "activation emits exactly one feedback event");
    ASSERT_EQ_MSG((int)rec.events[0].kind, (int)FlUiFeedbackKind_Activated, "the event is an Activated");
    ASSERT_EQ_MSG(rec.events[0].to, clay_id, "to is the activated widget");
    ASSERT_EQ_MSG((int)rec.events[0].dir, (int)FlUiFeedbackDir_None, "Activated carries no direction");
}

// feedback.null_hook_no_emit - with no hook installed (the default), driving a real nav move records nothing:
// the seam is inert until an app opts in.
UTEST(feedback, null_hook_no_emit) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ctx->focused_id = 0;
    ctx->last_focused_id = 0;

    const FlLayoutId id_a = ID("fb_null_a");
    const FlLayoutId id_b = ID("fb_null_b");
    const u32 clay_a = ui_layout_id_to_clay_id(id_a);
    const u32 clay_b = ui_layout_id_to_clay_id(id_b);

    feedback_emit_two_focusables(ctx, id_a, id_b);
    ctx->focused_id = clay_a;

    fl_ui_set_feedback_hook(nullptr, nullptr);
    ctx->nav_frame.nav_down = true;
    feedback_emit_two_focusables(ctx, id_a, id_b);
    ctx->nav_frame.nav_down = false;

    ASSERT_EQ_MSG(ctx->focused_id, clay_b, "the move still happened (nav is independent of the hook)");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// widget_state_colors.variant_base_derivation - the variant->color base derivation: Solid pulls
// interactive.base / text.primary, Outline its surface.base background, Ghost the secondary text, Soft the
// accent text.

UTEST(widget_state_colors, variant_base_derivation) {
    static const FlThemeColors theme = {
        .interactive.base = { 200, 30, 40, 255 },
        .surface.base = { 18, 20, 24, 255 },
        .text.primary = { 240, 240, 240, 255 },
        .text.secondary = { 150, 150, 150, 255 },
        .text.accent = { 200, 30, 40, 255 },
    };

    ASSERT_TRUE_MSG(color_eq(ui_get_variant_bg(FlWidgetVariant_Solid, &theme), theme.interactive.base),
                    "Solid bg derives from interactive.base");
    ASSERT_TRUE_MSG(color_eq(ui_get_variant_bg(FlWidgetVariant_Outline, &theme), theme.surface.base),
                    "Outline bg derives from surface.base");
    ASSERT_TRUE_MSG(color_eq(ui_get_variant_text(FlWidgetVariant_Solid, &theme), theme.text.primary),
                    "Solid text derives from text.primary");
    ASSERT_TRUE_MSG(color_eq(ui_get_variant_text(FlWidgetVariant_Ghost, &theme), theme.text.secondary),
                    "Ghost text derives from text.secondary");
    ASSERT_TRUE_MSG(color_eq(ui_get_variant_text(FlWidgetVariant_Soft, &theme), theme.text.accent),
                    "Soft text derives from text.accent");
}

// Modifier readback: ui_keyboard_modifiers() returns the raw keyboard modifier bitmask the input layer holds
// (Shift=1, Ctrl=2, Alt=4, Super=8). The bare render fixture has no key pump, so the bits are driven straight
// onto the context.
UTEST(input, modifiers_readback) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    // ui_keyboard_modifiers() reads the global context; one frame binds s_ctx to this fixture (it survives end).
    ui_update_begin(ctx);
    ui_update_end(ctx, 1.0f / 60.0f);

    ctx->input_state.keyboard.modifiers = FlKeyModifier_Shift;
    ASSERT_TRUE_MSG((ui_keyboard_modifiers() & FlKeyModifier_Shift) != 0, "Shift bit reads back");
    ASSERT_EQ_MSG((int)(ui_keyboard_modifiers() & FlKeyModifier_Control), 0, "Ctrl absent when only Shift is held");

    ctx->input_state.keyboard.modifiers = FlKeyModifier_Control;
    ASSERT_TRUE_MSG((ui_keyboard_modifiers() & FlKeyModifier_Control) != 0, "Ctrl bit reads back");
    ASSERT_EQ_MSG((int)(ui_keyboard_modifiers() & FlKeyModifier_Shift), 0, "Shift absent when only Ctrl is held");

    ctx->input_state.keyboard.modifiers = FlKeyModifier_Shift | FlKeyModifier_Control;
    u32 mods = ui_keyboard_modifiers();
    ASSERT_TRUE_MSG((mods & FlKeyModifier_Shift) && (mods & FlKeyModifier_Control), "Shift and Ctrl both read back");
    ASSERT_EQ_MSG((int)mods, (int)(FlKeyModifier_Shift | FlKeyModifier_Control), "exact union, no stray modifier bits");

    ctx->input_state.keyboard.modifiers = FlKeyModifier_None;
    canvas_fixture_reset(ctx);
}

// Shift+wheel axis swap: fl_platform_input_update swaps the frame's accumulated wheel axes when Shift is
// held, and the swap must land in the cached values consumers actually read (Clay scroll, ui_mouse_wheel,
// hover signals) - the live accumulators are cleared each frame. The Scroll_Up/Scroll_Down actions derive
// from the same cached value, so they fire under the real tick order, swapped and unswapped alike.
UTEST(input, shift_wheel_axis_swap_lands_in_cache) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    FlInputState* in = &ctx->input_state;

    in->keyboard.modifiers = FlKeyModifier_None;
    in->mouse.wheel_x = 3.0f;
    in->mouse.wheel_y = 7.0f;
    fl_platform_input_update(ctx, 1.0 / 60.0);
    ASSERT_FALSE_MSG(in->mouse.wheel_axis_swap_requested, "no swap requested without Shift");
    ASSERT_NEAR_MSG(3.0, (double)in->mouse.wheel_x_cached, 0.001, "x caches unswapped without Shift");
    ASSERT_NEAR_MSG(7.0, (double)in->mouse.wheel_y_cached, 0.001, "y caches unswapped without Shift");
    ASSERT_TRUE_MSG(ui_action_pressed(ctx, FlAction_Scroll_Up), "Scroll_Up fires from positive wheel y");
    ASSERT_FALSE_MSG(ui_action_pressed(ctx, FlAction_Scroll_Down), "Scroll_Down stays off");

    // Under Shift the scroll actions follow the swapped y: accumulated x is positive, so Scroll_Up fires, and
    // the negative accumulated y lands on x.
    in->keyboard.modifiers = FlKeyModifier_Shift;
    in->mouse.wheel_x = 5.0f;
    in->mouse.wheel_y = -2.0f;
    fl_platform_input_update(ctx, 1.0 / 60.0);
    ASSERT_TRUE_MSG(in->mouse.wheel_axis_swap_requested, "swap requested while Shift held");
    ASSERT_NEAR_MSG(-2.0, (double)in->mouse.wheel_x_cached, 0.001, "x caches the accumulated y under Shift");
    ASSERT_NEAR_MSG(5.0, (double)in->mouse.wheel_y_cached, 0.001, "y caches the accumulated x under Shift");
    ASSERT_TRUE_MSG(ui_action_pressed(ctx, FlAction_Scroll_Up), "Scroll_Up fires from the swapped positive y");
    ASSERT_FALSE_MSG(ui_action_pressed(ctx, FlAction_Scroll_Down), "Scroll_Down stays off after swap");

    // One quiet update: the cached wheel, swap flag, and scroll-action state clear.
    in->keyboard.modifiers = FlKeyModifier_None;
    fl_platform_input_update(ctx, 1.0 / 60.0);
    ASSERT_FALSE_MSG(ui_action_pressed(ctx, FlAction_Scroll_Up), "scroll action clears on a wheel-quiet frame");
}

// Click-run counting: the double-click window is the gap between consecutive clicks, not a budget for the
// whole run, so a triple-click is no harder to perform than a double-click.
UTEST(input, click_count_window_is_per_gap) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    FlInputState* in = &ctx->input_state;
    MouseState* mouse = &in->mouse;

    // Each gap sits inside the window but three of them exceed it, so a whole-run budget would reset the
    // count on the third click.
    const double gap = UI_MOUSE_DOUBLE_CLICK_TIME * 0.7;
    ASSERT_TRUE_MSG(3.0 * gap > UI_MOUSE_DOUBLE_CLICK_TIME, "gap must distinguish per-gap from whole-run");

    mouse->x = 100.0f;
    mouse->y = 100.0f;
    mouse->last_click_time[FlMouseButton_Left] = 0;
    mouse->click_count[FlMouseButton_Left] = 0;

    for (u32 expected = 1; expected <= 4; expected++) {
        mouse->buttons_pressed[FlMouseButton_Left] = true;
        fl_platform_input_update(ctx, gap);
        ASSERT_EQ_MSG((int)expected, (int)mouse->click_count[FlMouseButton_Left], "click run keeps accumulating");
    }

    // A gap wider than the window still starts a fresh run.
    mouse->buttons_pressed[FlMouseButton_Left] = true;
    fl_platform_input_update(ctx, UI_MOUSE_DOUBLE_CLICK_TIME * 1.5);
    ASSERT_EQ_MSG(1, (int)mouse->click_count[FlMouseButton_Left], "a gap past the window resets the run");

    // A click far from its predecessor starts a fresh run even inside the window.
    mouse->x = 100.0f + UI_MOUSE_DOUBLE_CLICK_MAX_DIST * 2.0f;
    mouse->buttons_pressed[FlMouseButton_Left] = true;
    fl_platform_input_update(ctx, gap);
    ASSERT_EQ_MSG(1, (int)mouse->click_count[FlMouseButton_Left], "a click past the distance limit resets the run");

    mouse->click_count[FlMouseButton_Left] = 0;
    mouse->last_click_time[FlMouseButton_Left] = 0;
    canvas_fixture_reset(ctx);
}

// Mouse-button readback: ui_mouse_pressed/released read the context's per-frame edge snapshot
// (mouse_buttons_*_this_frame, captured before fl_platform_input_update clears the raw buttons_pressed/released
// arrays), and ui_mouse_down reads the surviving input_state.mouse.buttons[] level. Buttons are global and
// unrouted, so the bare render fixture drives the context fields straight.
UTEST(input, mouse_button_readback) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    // ui_mouse_* read the global context; one frame binds s_ctx to this fixture (it survives end).
    ui_update_begin(ctx);
    ui_update_end(ctx, 1.0f / 60.0f);

    MouseState* mouse = &ctx->input_state.mouse;
    memset(mouse->buttons, 0, sizeof(mouse->buttons));
    memset(ctx->mouse_buttons_pressed_this_frame, 0, sizeof(ctx->mouse_buttons_pressed_this_frame));
    memset(ctx->mouse_buttons_released_this_frame, 0, sizeof(ctx->mouse_buttons_released_this_frame));

    ctx->mouse_buttons_pressed_this_frame[FlMouseButton_Left] = true;
    mouse->buttons[FlMouseButton_Left] = true;
    ASSERT_TRUE_MSG(ui_mouse_pressed(FlMouseButton_Left), "Left press edge reads back");
    ASSERT_TRUE_MSG(ui_mouse_down(FlMouseButton_Left), "Left held level reads back");
    ASSERT_FALSE_MSG(ui_mouse_released(FlMouseButton_Left), "Left not released on the press frame");

    ASSERT_FALSE_MSG(ui_mouse_pressed(FlMouseButton_Right), "Right press absent when only Left is pressed");
    ASSERT_FALSE_MSG(ui_mouse_down(FlMouseButton_Right), "Right held absent when only Left is held");

    ctx->mouse_buttons_pressed_this_frame[FlMouseButton_Left] = false;
    ASSERT_FALSE_MSG(ui_mouse_pressed(FlMouseButton_Left), "press edge is one frame only");
    ASSERT_TRUE_MSG(ui_mouse_down(FlMouseButton_Left), "Left stays held after the press edge");

    mouse->buttons[FlMouseButton_Left] = false;
    ctx->mouse_buttons_released_this_frame[FlMouseButton_Left] = true;
    ASSERT_TRUE_MSG(ui_mouse_released(FlMouseButton_Left), "Left release edge reads back");
    ASSERT_FALSE_MSG(ui_mouse_down(FlMouseButton_Left), "Left no longer held after release");

    // Same-frame tap: pressed and released both set in one frame (a fast click).
    memset(mouse->buttons, 0, sizeof(mouse->buttons));
    memset(ctx->mouse_buttons_pressed_this_frame, 0, sizeof(ctx->mouse_buttons_pressed_this_frame));
    memset(ctx->mouse_buttons_released_this_frame, 0, sizeof(ctx->mouse_buttons_released_this_frame));
    ctx->mouse_buttons_pressed_this_frame[FlMouseButton_Middle] = true;
    ctx->mouse_buttons_released_this_frame[FlMouseButton_Middle] = true;
    ASSERT_TRUE_MSG(ui_mouse_pressed(FlMouseButton_Middle), "tap: pressed set");
    ASSERT_TRUE_MSG(ui_mouse_released(FlMouseButton_Middle), "tap: released set in the same frame");

    ASSERT_FALSE_MSG(ui_mouse_pressed((FlMouseButton)FlMouseButton_Count), "out-of-range button reads false");
    ASSERT_FALSE_MSG(ui_mouse_down((FlMouseButton)-1), "negative button index reads false");

    canvas_fixture_reset(ctx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Gamepad readback through the public fl_input_* queries, driven the way a backend does: push events, then
// fl_platform_input_update closes the frame. Slot 3 keeps clear of the pad 0 the nav cases drive, and the slot
// is handed back exactly as found.
UTEST(input, gamepad_edges_hold_duration_and_axis_read_back_per_slot) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    // fl_input_* read the global context; one frame binds it to this fixture.
    ui_update_begin(ctx);
    ui_update_end(ctx, 1.0f / 60.0f);

    const u32 slot = 3;
    const GamepadState saved = ctx->input_state.gamepads[slot];
    const f64 dt = 0.25;

    const FlGamepadEvent b_down
        = { .type = FlGamepadEvent_Button_Down, .gamepad_id = slot, .button = { FlGamepadButton_B } };
    fl_platform_input_push_gamepad_event(ctx,
                                         (FlGamepadEvent) { .type = FlGamepadEvent_Connected, .gamepad_id = slot });
    fl_platform_input_push_gamepad_event(ctx, b_down);
    fl_platform_input_push_gamepad_event(ctx, (FlGamepadEvent) { .type = FlGamepadEvent_Axis_Motion,
                                                                 .gamepad_id = slot,
                                                                 .axis = { FlGamepadAxis_Left_Trigger, 0.75f } });
    // Inside the default dead zone: the read is the raw value, not a dead-zoned 0.
    fl_platform_input_push_gamepad_event(ctx, (FlGamepadEvent) { .type = FlGamepadEvent_Axis_Motion,
                                                                 .gamepad_id = slot,
                                                                 .axis = { FlGamepadAxis_Right_X, -0.1f } });
    fl_platform_input_update(ctx, dt);

    EXPECT_TRUE_MSG(fl_input_gamepad_button_pressed(slot, FlGamepadButton_B), "press edge reads on the press frame");
    EXPECT_FALSE_MSG(fl_input_gamepad_button_released(slot, FlGamepadButton_B), "no release on the press frame");
    EXPECT_FALSE_MSG(fl_input_gamepad_button_pressed(slot, FlGamepadButton_A), "an untouched button has no edge");
    EXPECT_FALSE_MSG(fl_input_gamepad_button_pressed(slot - 1, FlGamepadButton_B), "the edge is the slot's own");
    EXPECT_EQ(0.25f, fl_input_gamepad_button_hold_duration(slot, FlGamepadButton_B));
    EXPECT_EQ(0.0f, fl_input_gamepad_button_hold_duration(slot, FlGamepadButton_A));
    EXPECT_EQ(0.75f, fl_input_gamepad_axis(slot, FlGamepadAxis_Left_Trigger));
    EXPECT_EQ(-0.1f, fl_input_gamepad_axis(slot, FlGamepadAxis_Right_X));
    EXPECT_EQ(0.0f, fl_input_gamepad_axis(slot, FlGamepadAxis_Left_X));

    for_count(i, 3) {
        fl_platform_input_update(ctx, dt);
    }
    EXPECT_FALSE_MSG(fl_input_gamepad_button_pressed(slot, FlGamepadButton_B), "the press edge lasts one frame");
    EXPECT_TRUE_MSG(fl_input_gamepad_button_down(slot, FlGamepadButton_B), "the button is still held");
    EXPECT_EQ(1.0f, fl_input_gamepad_button_hold_duration(slot, FlGamepadButton_B));
    EXPECT_EQ(0.75f, fl_input_gamepad_axis(slot, FlGamepadAxis_Left_Trigger));

    const FlGamepadEvent b_up
        = { .type = FlGamepadEvent_Button_Up, .gamepad_id = slot, .button = { FlGamepadButton_B } };
    fl_platform_input_push_gamepad_event(ctx, b_up);
    fl_platform_input_update(ctx, dt);
    EXPECT_TRUE_MSG(fl_input_gamepad_button_released(slot, FlGamepadButton_B), "release edge reads on release");
    EXPECT_FALSE_MSG(fl_input_gamepad_button_pressed(slot, FlGamepadButton_B), "no press on the release frame");
    EXPECT_EQ(0.0f, fl_input_gamepad_button_hold_duration(slot, FlGamepadButton_B));

    fl_platform_input_update(ctx, dt);
    EXPECT_FALSE_MSG(fl_input_gamepad_button_released(slot, FlGamepadButton_B), "the release edge lasts one frame");

    // Same-frame tap: down and up land before one update, so both edges read and nothing is held.
    const FlGamepadEvent x_down
        = { .type = FlGamepadEvent_Button_Down, .gamepad_id = slot, .button = { FlGamepadButton_X } };
    const FlGamepadEvent x_up
        = { .type = FlGamepadEvent_Button_Up, .gamepad_id = slot, .button = { FlGamepadButton_X } };
    fl_platform_input_push_gamepad_event(ctx, x_down);
    fl_platform_input_push_gamepad_event(ctx, x_up);
    fl_platform_input_update(ctx, dt);
    EXPECT_TRUE_MSG(fl_input_gamepad_button_pressed(slot, FlGamepadButton_X), "tap: pressed set");
    EXPECT_TRUE_MSG(fl_input_gamepad_button_released(slot, FlGamepadButton_X), "tap: released set in the same frame");
    EXPECT_EQ(0.0f, fl_input_gamepad_button_hold_duration(slot, FlGamepadButton_X));

    // Out-of-range buttons and axes on a connected slot read as false/0.
    const FlGamepadButton bad_buttons[] = { (FlGamepadButton)FlGamepadButton_Count, (FlGamepadButton)-1 };
    for_count(b, sizeof_array(bad_buttons)) {
        EXPECT_FALSE(fl_input_gamepad_button_pressed(slot, bad_buttons[b]));
        EXPECT_FALSE(fl_input_gamepad_button_released(slot, bad_buttons[b]));
        EXPECT_EQ(0.0f, fl_input_gamepad_button_hold_duration(slot, bad_buttons[b]));
    }
    const FlGamepadAxis bad_axes[] = { (FlGamepadAxis)FlGamepadAxis_Count, (FlGamepadAxis)-1 };
    for_count(a, sizeof_array(bad_axes)) {
        EXPECT_EQ(0.0f, fl_input_gamepad_axis(slot, bad_axes[a]));
    }

    // A pad unplugged mid-press reads as an empty slot: no edge, no hold, every axis at 0.
    const FlGamepadEvent y_down
        = { .type = FlGamepadEvent_Button_Down, .gamepad_id = slot, .button = { FlGamepadButton_Y } };
    fl_platform_input_push_gamepad_event(ctx, y_down);
    fl_platform_input_update(ctx, dt);
    fl_platform_input_push_gamepad_event(ctx,
                                         (FlGamepadEvent) { .type = FlGamepadEvent_Disconnected, .gamepad_id = slot });
    EXPECT_FALSE_MSG(fl_input_gamepad_button_pressed(slot, FlGamepadButton_Y), "an empty slot has no press edge");
    EXPECT_EQ(0.0f, fl_input_gamepad_button_hold_duration(slot, FlGamepadButton_Y));
    EXPECT_EQ(0.0f, fl_input_gamepad_axis(slot, FlGamepadAxis_Left_Trigger));

    EXPECT_FALSE(fl_input_gamepad_button_pressed(UI_INPUT_MAX_GAMEPADS, FlGamepadButton_A));
    EXPECT_FALSE(fl_input_gamepad_button_released(UI_INPUT_MAX_GAMEPADS, FlGamepadButton_A));
    EXPECT_EQ(0.0f, fl_input_gamepad_button_hold_duration(UI_INPUT_MAX_GAMEPADS, FlGamepadButton_A));
    EXPECT_EQ(0.0f, fl_input_gamepad_axis(UI_INPUT_MAX_GAMEPADS, FlGamepadAxis_Left_X));

    ctx->input_state.gamepads[slot] = saved;
    fl_platform_input_update(ctx, dt);
    canvas_fixture_reset(ctx);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// cursor - the core's per-frame cursor request is last-writer-wins and resets to the arrow when nobody asks:
// ui_update_begin resets the request, fl_cursor_set overwrites it during the frame, then the resolved value
// (fl_cursor_get_requested) is flushed to the headless backend, which records it.

UTEST(cursor, per_frame_last_writer_wins_and_arrow_reset) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlWindowBackend wb = headless_window_backend_create(s_arena);
    ASSERT_TRUE_MSG(wb.create_window != nullptr, "headless window backend creation failed");
    FlWindowConfig cfg = { .title = FL_S("cursor"), .width = 320, .height = 240 };
    FlWindow* w = wb.create_window(wb.user_data, &cfg);
    ASSERT_TRUE_MSG(w != nullptr, "headless window creation failed");

    const float dt = 1.0f / 60.0f;

    // Frame 1: the last writer (Hand) wins over the earlier Crosshair request.
    ui_update_begin(ctx);
    fl_cursor_set((FlCursor) { .shape = FlCursorShape_Crosshair });
    fl_cursor_set((FlCursor) { .shape = FlCursorShape_Hand });
    ui_update_end(ctx, dt);
    FlCursor frame1 = fl_cursor_get_requested();
    wb.set_cursor(wb.user_data, w, (FlCursor) { .shape = (FlCursorShape)frame1.shape });
    ASSERT_EQ_MSG(window_backend_headless_last_cursor(&wb).shape, FlCursorShape_Hand,
                  "the last cursor->set of the frame wins and is flushed to the backend");

    // Frame 2: nobody requests a cursor.
    ui_update_begin(ctx);
    ui_update_end(ctx, dt);
    FlCursor frame2 = fl_cursor_get_requested();
    wb.set_cursor(wb.user_data, w, (FlCursor) { .shape = (FlCursorShape)frame2.shape });
    ASSERT_EQ_MSG(window_backend_headless_last_cursor(&wb).shape, FlCursorShape_Arrow,
                  "a frame with no request resets the cursor to the arrow");

    // Visibility is a sticky toggle, flushed independently of the per-frame shape.
    fl_cursor_set_visible(false);
    wb.set_cursor_visible(wb.user_data, w, fl_cursor_get_visible());
    ASSERT_FALSE_MSG(window_backend_headless_cursor_visible(&wb), "set_visible(false) hides the cursor");
    fl_cursor_set_visible(true);
    wb.set_cursor_visible(wb.user_data, w, fl_cursor_get_visible());
    ASSERT_TRUE_MSG(window_backend_headless_cursor_visible(&wb), "set_visible(true) shows it again");

    wb.destroy_window(wb.user_data, w);
    wb.destroy(wb.user_data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// widget_state_colors.perceptual_state_contract - the state->colour wiring of the widget prologue at the value
// level: fl_resolve_widget_colors must lighten the background on hover, darken it on press, and ramp the border
// onto interactive.focus under focus - all in OKLCH.
UTEST(widget_state_colors, perceptual_state_contract) {
    FlThemeColors theme = { 0 };
    theme.interactive.base = (FlColor) { 128, 128, 128, 255 }; // mid-lightness so lighten/darken are unambiguous
    theme.interactive.focus = (FlColor) { 40, 120, 240, 255 };

    const float l_base = color_rgb_to_oklch(theme.interactive.base).l;

    ItemState rest = { 0 };
    FlWidgetColors c_rest = fl_resolve_widget_colors(FlWidgetVariant_Solid, (FlColor) { 0 }, (FlColor) { 0 }, &rest,
                                                     &theme, /*animate_text=*/false);
    ASSERT_NEAR_MSG((double)color_rgb_to_oklch(c_rest.bg).l, (double)l_base, 1e-4,
                    "at rest the Solid background is the unmodified variant base");

    ItemState hover = { 0 };
    hover.state.hot_t = 1.0f;
    FlWidgetColors c_hover
        = fl_resolve_widget_colors(FlWidgetVariant_Solid, (FlColor) { 0 }, (FlColor) { 0 }, &hover, &theme, false);
    ASSERT_GT_MSG((double)color_rgb_to_oklch(c_hover.bg).l, (double)l_base,
                  "hover raises the OKLCH lightness of the background");

    ItemState press = { 0 };
    press.state.active_t = 1.0f;
    FlWidgetColors c_press
        = fl_resolve_widget_colors(FlWidgetVariant_Solid, (FlColor) { 0 }, (FlColor) { 0 }, &press, &theme, false);
    ASSERT_LT_MSG((double)color_rgb_to_oklch(c_press.bg).l, (double)l_base,
                  "press lowers the OKLCH lightness of the background");

    // The Solid border has a transparent base, so at focused_t = 1 it ramps exactly onto interactive.focus.
    ItemState focus = { 0 };
    focus.state.focused_t = 1.0f;
    FlWidgetColors c_focus
        = fl_resolve_widget_colors(FlWidgetVariant_Solid, (FlColor) { 0 }, (FlColor) { 0 }, &focus, &theme, false);
    ASSERT_TRUE_MSG(color_eq(c_focus.border, theme.interactive.focus),
                    "focus drives the derived border onto interactive.focus");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// widget_state_colors.state_color_derivation - ui_apply_state_color is base-color derivation only: hover lightens
// by 0.15 * hot_t, press then darkens that result by 0.15 * active_t, and a null or resting item returns the base.
// Every variant's resolved background and text go through the same derivation.
UTEST(widget_state_colors, state_color_derivation) {
    const FlColor base = { 120, 90, 200, 255 };

    ASSERT_TRUE_MSG(color_eq(ui_apply_state_color(base, nullptr), base), "null item returns the base");

    ItemState rest = { 0 };
    ASSERT_TRUE_MSG(color_eq(ui_apply_state_color(base, &rest), base), "resting item returns the base");

    ItemState hover = { 0 };
    hover.state.hot_t = 0.5f;
    ASSERT_TRUE_MSG(color_eq(ui_apply_state_color(base, &hover), color_lighten_perceptual(base, 0.15f * 0.5f)),
                    "hover lightens by 0.15 * hot_t");

    ItemState press = { 0 };
    press.state.active_t = 0.5f;
    ASSERT_TRUE_MSG(color_eq(ui_apply_state_color(base, &press), color_darken_perceptual(base, 0.15f * 0.5f)),
                    "press darkens by 0.15 * active_t");

    ItemState both = { 0 };
    both.state.hot_t = 1.0f;
    both.state.active_t = 1.0f;
    ASSERT_TRUE_MSG(color_eq(ui_apply_state_color(base, &both),
                             color_darken_perceptual(color_lighten_perceptual(base, 0.15f), 0.15f)),
                    "press darkens on top of the hover lighten");

    FlThemeColors theme = { 0 };
    theme.interactive.base = (FlColor) { 200, 30, 40, 255 };
    theme.surface.base = (FlColor) { 18, 20, 24, 255 };
    theme.text.primary = (FlColor) { 240, 240, 240, 255 };
    theme.text.secondary = (FlColor) { 150, 150, 150, 255 };
    theme.text.accent = (FlColor) { 200, 30, 40, 255 };

    const FlWidgetVariant variants[]
        = { FlWidgetVariant_Solid, FlWidgetVariant_Outline, FlWidgetVariant_Ghost, FlWidgetVariant_Soft };
    for (size_t i = 0; i < sizeof(variants) / sizeof(variants[0]); ++i) {
        FlWidgetColors c = fl_resolve_widget_colors(variants[i], (FlColor) { 0 }, (FlColor) { 0 }, &both, &theme, true);
        ASSERT_TRUE_MSG(color_eq(c.bg, ui_apply_state_color(ui_get_variant_bg(variants[i], &theme), &both)),
                        "variant bg is the derived state color of its base");
        ASSERT_TRUE_MSG(color_eq(c.text, ui_apply_state_color(ui_get_variant_text(variants[i], &theme), &both)),
                        "variant text is the derived state color of its base");
    }
}

// ui_switch.toggles_and_animates - the switch shares the checkbox click/activate behavior (toggles a bool*) and
// drives a 0..1 knob animation that eases toward the on/off target. Direct-FlUiCtx path with a controlled dt,
// asserting the toggle, the returned signal, and that the knob animation transitions toward its target.
UTEST(ui_switch, toggles_and_animates) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // Identity scale (ui_scale == 1.0); a fixed dt makes the animation deterministic.
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    const float dt = 1.0f / 60.0f;
    ctx->delta_time = dt;

    const FlLayoutId sw_id = { .name = FL_S("test_switch"), .id = 0 };
    const u32 sw_clay = ui_layout_id_to_clay_id(sw_id);

    bool value = false;

#define SWITCH_FRAME(sig_out)                                        \
    do {                                                             \
        ui_update_begin(ctx);                                        \
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) { \
            (sig_out) = ui_switch(FL_S("test_switch"), &value);      \
        }                                                            \
        ui_update_end(ctx, dt);                                      \
    } while (0)

    FlSignal sig = { 0 };

    // Frame 1: lay out the switch once so its interactive ItemState exists and the knob starts off (0.0).
    ctx->mouse_pressed_this_frame = false;
    ctx->mouse_released_this_frame = false;
    SWITCH_FRAME(sig);

    ItemState* item = hashmap_get(&ctx->shared->item_state, sw_clay);
    ASSERT_TRUE_MSG(item != nullptr, "switch registered an interactive item");
    ASSERT_NEAR_MSG((double)item->check_anim_t, 0.0, 1e-6, "knob starts at the off position");

    // Mark the switch hot so the fast tap in the next frame registers as a click, shortcutting the multi-frame
    // hover -> hot_id -> state.hot propagation.
    item->state.hot = 1;

    // Frame 2: a fast tap (pressed + released in the same frame) on the hot switch toggles the bool on.
    ctx->mouse_pressed_this_frame = true;
    ctx->mouse_released_this_frame = true;
    SWITCH_FRAME(sig);
    ASSERT_TRUE_MSG(sig.clicked, "fast tap on the hot switch returns a clicked signal");
    ASSERT_TRUE_MSG(value, "click toggled the bool on");

    float prev = -1.0f;
    for (int i = 0; i < 90; i++) {
        ctx->mouse_pressed_this_frame = false;
        ctx->mouse_released_this_frame = false;
        SWITCH_FRAME(sig);
        item = hashmap_get(&ctx->shared->item_state, sw_clay);
        ASSERT_GE_MSG((double)item->check_anim_t, (double)prev, "knob animation moves toward on, never backward");
        prev = item->check_anim_t;
    }
    ASSERT_NEAR_MSG((double)item->check_anim_t, 1.0, 1e-3, "knob settles at the on position");

    // A second fast tap toggles the bool back off.
    item = hashmap_get(&ctx->shared->item_state, sw_clay);
    item->state.hot = 1;
    ctx->mouse_pressed_this_frame = true;
    ctx->mouse_released_this_frame = true;
    SWITCH_FRAME(sig);
    ASSERT_TRUE_MSG(sig.clicked, "second fast tap returns a clicked signal");
    ASSERT_FALSE_MSG(value, "second click toggled the bool off");

    item = hashmap_get(&ctx->shared->item_state, sw_clay);
    const float off_start = item->check_anim_t;
    ASSERT_GT_MSG((double)off_start, 0.5, "knob is still near the on position right after toggling off");
    for (int i = 0; i < 90; i++) {
        ctx->mouse_pressed_this_frame = false;
        ctx->mouse_released_this_frame = false;
        SWITCH_FRAME(sig);
    }
    item = hashmap_get(&ctx->shared->item_state, sw_clay);
    ASSERT_LT_MSG((double)item->check_anim_t, (double)off_start, "knob moved back toward off");
    ASSERT_NEAR_MSG((double)item->check_anim_t, 0.0, 1e-3, "knob settles at the off position");

    ctx->mouse_pressed_this_frame = false;
    ctx->mouse_released_this_frame = false;

#undef SWITCH_FRAME
}

// One frame's solid rects, captured off the render seam. A switch draws exactly three: the hit-test
// container, the track, then the knob (parent before child, in Clay's render order).
typedef struct SwitchRects {
    FlRect rect[8];
    int count;
} SwitchRects;

static SwitchRects s_switch_rects;

// The renderer the fixture installed.
static void (*s_switch_rects_next)(void*, FlSurface*, const FlRenderFrame*);

static void switch_rects_capture(void* user_data, FlSurface* surface, const FlRenderFrame* frame) {
    s_switch_rects.count = 0;
    for (const u8* p = (const u8*)frame->commands; p < (const u8*)frame->commands + frame->bytes;) {
        const FlCmdHeader* h = (const FlCmdHeader*)p;
        if (h->kind == FlCmd_FillRect && s_switch_rects.count < 8) {
            s_switch_rects.rect[s_switch_rects.count++] = ((const FlCmdFillRect*)(h + 1))->rect;
        }
        p += h->byte_size;
    }
    s_switch_rects_next(user_data, surface, frame);
}

// ui_switch.geometry_follows_config - the track is FlSwitchConfig's track_width x track_height, the knob the
// circle that fits inside knob_inset, and zero on any of the three keeps the built-in 48x24 pill with its 20px
// knob. Asserted off the emitted fill rects rather than pixels so the numbers in the assertions ARE the geometry.
UTEST(ui_switch, geometry_follows_config) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // Identity scale so the emitted physical-px rects are the logical geometry the config asks for.
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    const float dt = 1.0f / 60.0f;

    // ui_shared_create copied the backend, so the seam to intercept is the one the core holds, not the
    // fixture's own struct.
    s_switch_rects_next = ctx->shared->renderer.render;
    ctx->shared->renderer.render = switch_rects_capture;

    // Renders one switch with cfg and value, leaving its three rects in s_switch_rects. Two frames per case:
    // the first registers the widget's ItemState (stamping only works once the item exists), the second runs
    // with its knob animation stamped to an exact 0 or 1 rather than whatever fraction one dt of easing gives.
    bool value = false;
#define SWITCH_GEOM_FRAME(label_str, ...)                                                                     \
    do {                                                                                                      \
        const FlLayoutId geom_id = { .name = FL_S(label_str), .id = 0 };                                      \
        for (int geom_pass = 0; geom_pass < 2; geom_pass++) {                                                 \
            ItemState* it = hashmap_get(&ctx->shared->item_state, ui_layout_id_to_clay_id(geom_id));          \
            if (it != nullptr) {                                                                              \
                it->check_anim_t = value ? 1.0f : 0.0f;                                                       \
            }                                                                                                 \
            ui_update_begin(ctx);                                                                             \
            ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {                                      \
                ui_switch(FL_S(label_str), &value, __VA_ARGS__);                                              \
            }                                                                                                 \
            ui_update_end(ctx, dt);                                                                           \
        }                                                                                                     \
        ASSERT_EQ_MSG(s_switch_rects.count, 3, "a switch draws the hit-test container, the track, the knob"); \
    } while (0)

    // Default geometry: a 48x24 pill, knob 24 - 2*2 = 20 across, sitting 2px in from the track's left edge.
    SWITCH_GEOM_FRAME("geom_default", .track_width = 0.0f);
    FlRect track = s_switch_rects.rect[1];
    FlRect knob = s_switch_rects.rect[2];
    ASSERT_NEAR_MSG((double)track.w, 48.0, 1e-4, "the default track is twice its height wide");
    ASSERT_NEAR_MSG((double)track.h, 24.0, 1e-4, "the default track is FlSpacing_LG tall");
    ASSERT_NEAR_MSG((double)knob.w, 20.0, 1e-4, "the default knob is the track height less both insets");
    ASSERT_NEAR_MSG((double)knob.h, 20.0, 1e-4, "the knob is square (a circle at full radius)");
    ASSERT_NEAR_MSG((double)(knob.x - track.x), 2.0, 1e-4, "the off knob sits one inset in from the left");

    // track_height alone still derives the width, so the pill proportion survives a height-only skin.
    SWITCH_GEOM_FRAME("geom_height_only", .track_height = 13.0f);
    ASSERT_NEAR_MSG((double)s_switch_rects.rect[1].w, 26.0, 1e-4, "width still derives from the skinned height");
    ASSERT_NEAR_MSG((double)s_switch_rects.rect[1].h, 13.0, 1e-4, "the skinned height applies");

    // the compact skin, off: a 26x13 track with a 9px knob.
    SWITCH_GEOM_FRAME("geom_skin", .track_width = 26.0f, .track_height = 13.0f);
    track = s_switch_rects.rect[1];
    knob = s_switch_rects.rect[2];
    ASSERT_NEAR_MSG((double)track.w, 26.0, 1e-4, "the skinned track width applies");
    ASSERT_NEAR_MSG((double)track.h, 13.0, 1e-4, "the skinned track height applies");
    ASSERT_NEAR_MSG((double)knob.w, 9.0, 1e-4, "the knob derives as track_height - 2 * knob_inset");
    ASSERT_NEAR_MSG((double)(knob.x - track.x), 2.0, 1e-4, "the off knob sits one inset in from the left");

    // Same skin, fully on: the knob has travelled to x = 15 within the track (26 - 9 - 2).
    value = true;
    SWITCH_GEOM_FRAME("geom_skin", .track_width = 26.0f, .track_height = 13.0f);
    track = s_switch_rects.rect[1];
    knob = s_switch_rects.rect[2];
    ASSERT_NEAR_MSG((double)(knob.x - track.x), 15.0, 1e-4, "the on knob stops one inset short of the right edge");

    value = false;

    // An explicit knob_inset drives the knob size directly: 13 - 2*4 = 5.
    SWITCH_GEOM_FRAME("geom_inset", .track_height = 13.0f, .knob_inset = 4.0f);
    track = s_switch_rects.rect[1];
    knob = s_switch_rects.rect[2];
    ASSERT_NEAR_MSG((double)knob.w, 5.0, 1e-4, "an explicit inset eats into the derived knob size");
    ASSERT_NEAR_MSG((double)(knob.x - track.x), 4.0, 1e-4, "the off knob sits one explicit inset in from the left");

    // An inset past half the track height leaves no knob rather than a negative one.
    SWITCH_GEOM_FRAME("geom_fat_inset", .track_height = 13.0f, .knob_inset = 10.0f);
    knob = s_switch_rects.rect[2];
    ASSERT_NEAR_MSG((double)knob.w, 0.0, 1e-4, "an over-large inset collapses the knob instead of inverting it");
    ASSERT_NEAR_MSG((double)knob.h, 0.0, 1e-4, "an over-large inset collapses the knob instead of inverting it");

    // A track narrower than its own knob leaves no travel at all. Unclamped this would be negative, and the
    // knob's left padding is an unsigned pixel count - the knob would wrap to the far side of the screen.
    value = true;
    SWITCH_GEOM_FRAME("geom_narrow", .track_width = 8.0f, .track_height = 12.0f);
    track = s_switch_rects.rect[1];
    knob = s_switch_rects.rect[2];
    ASSERT_NEAR_MSG((double)(knob.x - track.x), 2.0, 1e-4, "a track with no room to travel pins the knob at rest");

#undef SWITCH_GEOM_FRAME
}

// One frame's solid rects for a slider, captured off the render seam. A slider draws exactly four: the
// hit-test row, then the filled segment, the thumb and the unfilled segment (parent before children, in
// Clay's render order).
static SwitchRects s_slider_rects;

static void (*s_slider_rects_next)(void*, FlSurface*, const FlRenderFrame*);

static void slider_rects_capture(void* user_data, FlSurface* surface, const FlRenderFrame* frame) {
    s_slider_rects.count = 0;
    for (const u8* p = (const u8*)frame->commands; p < (const u8*)frame->commands + frame->bytes;) {
        const FlCmdHeader* h = (const FlCmdHeader*)p;
        if (h->kind == FlCmd_FillRect && s_slider_rects.count < 8) {
            s_slider_rects.rect[s_slider_rects.count++] = ((const FlCmdFillRect*)(h + 1))->rect;
        }
        p += h->byte_size;
    }
    s_slider_rects_next(user_data, surface, frame);
}

// ui_slider.geometry_follows_config - the thumb is FlSliderConfig's thumb_size square, the track bar is
// track_height thick and centered against that thumb, and zero on either keeps the built-in 24px thumb over an
// 8px bar. Asserted off the emitted fill rects rather than pixels so the numbers in the assertions ARE the
// geometry.
UTEST(ui_slider, geometry_follows_config) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // Identity scale so the emitted physical-px rects are the logical geometry the config asks for.
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    const float dt = 1.0f / 60.0f;

    // ui_shared_create copied the backend, so the seam to intercept is the one the core holds, not the
    // fixture's own struct.
    s_slider_rects_next = ctx->shared->renderer.render;
    ctx->shared->renderer.render = slider_rects_capture;

    // Renders one slider with cfg, leaving its four rects in s_slider_rects. Two frames per case: the filled
    // and unfilled widths are derived from the row width the item carried over from the previous frame, so the
    // first frame lays out against a zero-width row and only the second reflects the value.
    float value = 0.5f;
#define SLIDER_GEOM_FRAME(label_str, ...)                                          \
    do {                                                                           \
        for (int geom_pass = 0; geom_pass < 2; geom_pass++) {                      \
            ui_update_begin(ctx);                                                  \
            ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {           \
                ui_slider_float(FL_S(label_str), &value, 0.0f, 1.0f, __VA_ARGS__); \
            }                                                                      \
            ui_update_end(ctx, dt);                                                \
        }                                                                          \
        ASSERT_EQ_MSG(s_slider_rects.count, 4,                                     \
                      "a slider draws the row, the filled bar, the thumb, the "    \
                      "unfilled bar");                                             \
    } while (0)

    // Default geometry: a 24px thumb over an 8px bar, the bar centered in the row ((24 - 8) / 2 = 8 down).
    SLIDER_GEOM_FRAME("slider_geom_default", .thumb_size = 0.0f);
    FlRect row = s_slider_rects.rect[0];
    FlRect filled = s_slider_rects.rect[1];
    FlRect thumb = s_slider_rects.rect[2];
    FlRect unfilled = s_slider_rects.rect[3];
    ASSERT_NEAR_MSG((double)thumb.w, 24.0, 1e-4, "the default thumb is UI_SLIDER_THUMB_WIDTH across");
    ASSERT_NEAR_MSG((double)thumb.h, 24.0, 1e-4, "the thumb is square (a circle at full radius)");
    ASSERT_NEAR_MSG((double)row.h, 24.0, 1e-4, "the row is as tall as the thumb");
    ASSERT_NEAR_MSG((double)filled.h, 8.0, 1e-4, "the default track bar is 8px thick");
    ASSERT_NEAR_MSG((double)unfilled.h, 8.0, 1e-4, "both track segments share the bar thickness");
    ASSERT_NEAR_MSG((double)(filled.y - row.y), 8.0, 1e-4, "the thin bar centers vertically against the thumb");
    // At 0.5 the thumb sits mid-travel, so the two segments split the row's width less the thumb evenly.
    ASSERT_NEAR_MSG((double)filled.w, (double)unfilled.w, 1e-4, "a half-value slider splits its travel evenly");
    ASSERT_NEAR_MSG((double)(filled.w + thumb.w + unfilled.w), (double)row.w, 1e-4,
                    "the three children fill the row exactly");

    // the compact skin: a 3px bar under a 13px thumb, the bar centered at (13 - 3) / 2 = 5 down.
    SLIDER_GEOM_FRAME("slider_geom_skin", .track_height = 3.0f, .thumb_size = 13.0f);
    row = s_slider_rects.rect[0];
    filled = s_slider_rects.rect[1];
    thumb = s_slider_rects.rect[2];
    ASSERT_NEAR_MSG((double)thumb.w, 13.0, 1e-4, "the skinned thumb size applies");
    ASSERT_NEAR_MSG((double)thumb.h, 13.0, 1e-4, "the skinned thumb stays square");
    ASSERT_NEAR_MSG((double)row.h, 13.0, 1e-4, "the row height follows the skinned thumb, not the bar");
    ASSERT_NEAR_MSG((double)filled.h, 3.0, 1e-4, "the skinned track thickness applies");
    ASSERT_NEAR_MSG((double)(filled.y - row.y), 5.0, 1e-4, "the 3px bar centers against the 13px thumb");

    // Either field alone keeps the other's default, so a thickness-only or thumb-only skin is well-defined.
    SLIDER_GEOM_FRAME("slider_geom_track_only", .track_height = 3.0f);
    ASSERT_NEAR_MSG((double)s_slider_rects.rect[2].w, 24.0, 1e-4, "the thumb keeps its default beside a thin bar");
    ASSERT_NEAR_MSG((double)s_slider_rects.rect[1].h, 3.0, 1e-4, "the skinned thickness applies on its own");
    SLIDER_GEOM_FRAME("slider_geom_thumb_only", .thumb_size = 13.0f);
    ASSERT_NEAR_MSG((double)s_slider_rects.rect[2].w, 13.0, 1e-4, "the skinned thumb applies on its own");
    ASSERT_NEAR_MSG((double)s_slider_rects.rect[1].h, 8.0, 1e-4, "the bar keeps its default under a small thumb");

#undef SLIDER_GEOM_FRAME
}

// One frame of a synthetic gesture over a slider: place the cursor, set the left-button state, run the real
// input layer, then lay the slider out and let it consume the click. Going through fl_platform_input_update
// rather than poking the context means signal.clicked/hovered come from the genuine hit-testing path.
static void slider_click_frame(FlUiCtx* ctx, FlString label, float thumb_size, float* value, float mx, float my,
                               bool held, bool pressed, bool released) {
    FlInputState* in = &ctx->input_state;
    fl_platform_input_set_mouse_position(ctx, mx, my);
    in->mouse.buttons[FlMouseButton_Left] = held;
    in->mouse.buttons_pressed[FlMouseButton_Left] = pressed; // fl_platform_input_update consumes + clears this edge
    in->mouse.buttons_released[FlMouseButton_Left] = released;
    fl_platform_input_update(ctx, 1.0f / 60.0f);

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {
        ui_slider_float(label, value, 0.0f, 1.0f, .thumb_size = thumb_size);
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

// Clicks click_x pixels in from the slider row's left edge and returns the value the slider wrote, reporting
// the row's width through out_row_w so the caller can state the expected mapping in terms of the geometry
// rather than a magic number. label doubles as the widget id, so each thumb size needs its own.
static float slider_click_value(FlUiCtx* ctx, FlString label, float thumb_size, float click_x, float* out_row_w) {
    float value = 0.0f;

    // Two frames with the cursor parked off-window: the first lays the row out (the drag math divides by the
    // width it carried over from the previous frame), the second settles it before any hover.
    slider_click_frame(ctx, label, thumb_size, &value, -100.0f, -100.0f, false, false, false);
    slider_click_frame(ctx, label, thumb_size, &value, -100.0f, -100.0f, false, false, false);

    const ItemState* row
        = hashmap_get(&ctx->shared->item_state, ui_layout_id_to_clay_id((FlLayoutId) { .name = label, .id = 0 }));
    if (row == nullptr) {
        *out_row_w = 0.0f;
        return value;
    }
    *out_row_w = row->state.size.x;
    const float mx = row->state.pos.x + click_x;
    const float my = row->state.pos.y + row->state.size.y / 2.0f;

    // Hover (two frames: the row becomes hot_id at the end of the first and its state.hot flips on the second,
    // and a press needs signal.hovered), then press, then release - the release frame is where clicked fires.
    slider_click_frame(ctx, label, thumb_size, &value, mx, my, false, false, false);
    slider_click_frame(ctx, label, thumb_size, &value, mx, my, false, false, false);
    slider_click_frame(ctx, label, thumb_size, &value, mx, my, true, true, false);
    slider_click_frame(ctx, label, thumb_size, &value, mx, my, false, false, true);
    return value;
}

// ui_slider.hit_math_follows_thumb_size - clicking a point on the track maps to a value through the configured
// thumb size, not the built-in 24px one. The expected values are written as the mapping itself - mouse-x less
// half a thumb, over the travel the thumb has - so the assertion states the formula, not a precomputed constant.
UTEST(ui_slider, hit_math_follows_thumb_size) {
    FlAppSettings settings = { .window_title = FL_S("slider_hit"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = ui_get_ctx();
    ctx->ui_scale = 1.0f; // logical == physical, so an injected window pixel is the logical x the slider sees
    ctx->layout_width = 400.0f;
    ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });

    // The same click point on both sliders. Well inside the track, so neither mapping clamps at an end.
    const float click_x = 100.0f;

    float row_w = 0.0f;
    const float got_default = slider_click_value(ctx, FL_S("hit_default"), 0.0f, click_x, &row_w);
    ASSERT_TRUE_MSG(row_w > 0.0f, "the default slider's row laid out");
    const float want_default = (click_x - UI_SLIDER_THUMB_WIDTH / 2.0f) / (row_w - UI_SLIDER_THUMB_WIDTH);
    ASSERT_NEAR_MSG((double)got_default, (double)want_default, 1e-4,
                    "a zero thumb_size keeps the built-in 24px thumb in the click mapping");

    const float thumb = 13.0f;
    const float got_skin = slider_click_value(ctx, FL_S("hit_skin"), thumb, click_x, &row_w);
    ASSERT_TRUE_MSG(row_w > 0.0f, "the skinned slider's row laid out");
    const float want_skin = (click_x - thumb / 2.0f) / (row_w - thumb);
    ASSERT_NEAR_MSG((double)got_skin, (double)want_skin, 1e-4,
                    "the configured thumb_size drives the click mapping, not UI_SLIDER_THUMB_WIDTH");

    // The two mappings really do differ, so the assertions above are not both satisfied by the default math.
    ASSERT_TRUE_MSG(got_skin != got_default, "a smaller thumb maps the same click to a different value");

    fl_app_destroy(app);
}

// ui_tab_bar.geometry_follows_config - FlTabBarConfig's item/strip knobs steer the tab geometry, and a zeroed
// config leaves the strip at its built-in geometry (the dock leaf header passes a zeroed one). Asserted off the
// debug render-command snapshot rather than pixels, so the numbers in the assertions ARE the geometry.
UTEST(ui_tab_bar, geometry_follows_config) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // Identity scale, so the snapshot's logical rects are the pixels the config asked for.
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    const float dt = 1.0f / 60.0f;

    // The tabs are fit-sized around their labels, so the strip needs real text metrics. Built-in 8x8 font.
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 1));

    FlString tabs[] = { FL_S("One"), FL_S("Two"), FL_S("Three") };
    int selected = 0;
    UiRenderCommand rects[8];
    int count = 0;

    // Renders one tab bar with the given config and leaves its rectangles in rects/count. Two frames per case:
    // the first registers each tab's ItemState so the hover/focus animation has somewhere to land, and only the
    // second is measured.
#define TAB_GEOM_FRAME(...)                                              \
    do {                                                                 \
        for (int geom_pass = 0; geom_pass < 2; geom_pass++) {            \
            ui_update_begin(ctx);                                        \
            ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) { \
                ui_tab_bar(tabs, 3, &selected, __VA_ARGS__);             \
            }                                                            \
            ui_update_end(ctx, dt);                                      \
        }                                                                \
        count = render_cmd_collect(UiRenderCmd_Rectangle, rects, 8);     \
    } while (0)

    TAB_GEOM_FRAME(.vertical = false);
    ASSERT_EQ_MSG(count, 3, "a zeroed strip paints one rectangle per tab and no pill behind them");
    ASSERT_NEAR_MSG((double)rects[0].height, (double)FlWidgetHeight_MD, 1e-4,
                    "a zeroed config keeps the fixed tab height");
    ASSERT_NEAR_MSG((double)rects[0].corner_radius.tl, (double)FlRadius_MD, 1e-4, "and the default tab radius");

    TAB_GEOM_FRAME(.item_padding_h = 18.0f, .item_padding_v = 6.0f, .item_radius = 6.0f, .container_padding = 3.0f,
                   .container_radius = 8.0f);
    ASSERT_EQ_MSG(count, 4, "asking for strip padding/radius paints a pill behind the three tabs");
    const UiRenderCommand pill = rects[0];
    const float skin_h = rects[1].height;
    const float skin_w = rects[1].width;
    ASSERT_NEAR_MSG((double)pill.corner_radius.tl, 8.0, 1e-4, "the pill takes container_radius");
    ASSERT_NEAR_MSG((double)rects[1].corner_radius.tl, 6.0, 1e-4, "the tabs take item_radius");
    ASSERT_NEAR_MSG((double)(rects[1].x - pill.x), 3.0, 1e-4, "container_padding insets the first tab");
    ASSERT_NEAR_MSG((double)(pill.height - skin_h), 6.0, 1e-4, "the pill hugs the tabs, padded top and bottom");
    ASSERT_NEAR_MSG((double)pill.width, (double)(rects[3].x + rects[3].width + 3.0f - pill.x), 1e-4,
                    "and hugs them across too, rather than stretching the row like the bare strip does");
    ASSERT_LT_MSG((double)skin_h, (double)FlWidgetHeight_MD, "item padding takes the tab off the fixed height");

    // Padding-driven on both axes: 10px more padding per side is 20px more tab. Stated as a delta against the
    // case above so no font metric is baked into the expected numbers.
    TAB_GEOM_FRAME(.item_padding_h = 18.0f, .item_padding_v = 16.0f, .item_radius = 6.0f, .container_padding = 3.0f,
                   .container_radius = 8.0f);
    ASSERT_NEAR_MSG((double)(rects[1].height - skin_h), 20.0, 1e-4, "vertical padding drives the tab height");
    TAB_GEOM_FRAME(.item_padding_h = 28.0f, .item_padding_v = 6.0f, .item_radius = 6.0f, .container_padding = 3.0f,
                   .container_radius = 8.0f);
    ASSERT_NEAR_MSG((double)(rects[1].width - skin_w), 20.0, 1e-4, "horizontal padding drives the tab width");

    // child_gap steers the space between tabs, stated as a delta against the 4px default so no font metric is
    // baked in: the strip's first tab never moves, the last one moves by two extra gaps. A negative value lands
    // the second tab flush against the first, which 0 cannot ask for.
    TAB_GEOM_FRAME(.item_padding_h = 18.0f, .item_padding_v = 6.0f, .container_padding = 3.0f);
    const float gap_default_last_x = rects[3].x;
    const float gap_default_pill_w = rects[0].width;
    ASSERT_NEAR_MSG((double)(rects[2].x - (rects[1].x + rects[1].width)), 4.0, 1e-4,
                    "a zeroed child_gap keeps the built-in FlSpacing_XS between tabs");
    TAB_GEOM_FRAME(.item_padding_h = 18.0f, .item_padding_v = 6.0f, .container_padding = 3.0f, .child_gap = 12.0f);
    ASSERT_NEAR_MSG((double)(rects[3].x - gap_default_last_x), 16.0, 1e-4,
                    "child_gap widens each of the two gaps ahead of the last tab by 8px");
    ASSERT_NEAR_MSG((double)(rects[0].width - gap_default_pill_w), 16.0, 1e-4, "and the pill grows with them");
    TAB_GEOM_FRAME(.item_padding_h = 18.0f, .item_padding_v = 6.0f, .container_padding = 3.0f, .child_gap = -1.0f);
    ASSERT_NEAR_MSG((double)(rects[2].x - (rects[1].x + rects[1].width)), 0.0, 1e-4,
                    "a negative child_gap makes the tabs flush, which 0 cannot ask for");
    ASSERT_NEAR_MSG((double)(gap_default_pill_w - rects[0].width), 8.0, 1e-4,
                    "so the strip loses both gaps off its pinned width");

    // Either strip knob alone is enough to paint the pill, and each item knob stands on its own default.
    TAB_GEOM_FRAME(.container_radius = 8.0f);
    ASSERT_EQ_MSG(count, 4, "container_radius alone paints the pill");
    ASSERT_NEAR_MSG((double)rects[1].height, (double)FlWidgetHeight_MD, 1e-4,
                    "a pill without item padding leaves the tabs at the fixed height");
    TAB_GEOM_FRAME(.item_radius = 6.0f);
    ASSERT_EQ_MSG(count, 3, "item_radius alone paints no pill");
    ASSERT_NEAR_MSG((double)rects[0].corner_radius.tl, 6.0, 1e-4, "the tab radius applies on its own");
    ASSERT_NEAR_MSG((double)rects[0].height, (double)FlWidgetHeight_MD, 1e-4, "and leaves the height alone");

    // font_size lands on the run and the snapshot carries it directly; letter_spacing has no snapshot field, so
    // it is read off the fit-sized tab's width instead. "One" has three glyphs, so two gaps.
    TAB_GEOM_FRAME(.font_size = 10);
    UiRenderCommand texts[8];
    ASSERT_EQ_MSG(render_cmd_collect(UiRenderCmd_Text, texts, 8), 3, "one label per tab");
    ASSERT_EQ_MSG((int)texts[0].font_size, 10, "the config's font_size reaches the label");
    const float unspaced_w = rects[0].width;
    TAB_GEOM_FRAME(.font_size = 10, .letter_spacing = 4);
    ASSERT_NEAR_MSG((double)(rects[0].width - unspaced_w), 8.0, 1e-4,
                    "letter_spacing widens the label by one gap per glyph pair");

#undef TAB_GEOM_FRAME
}

// Tab colors come out of the shared variant prologue: Solid for the selected tab, Ghost for the rest.
UTEST(ui_tab_bar, colors_follow_theme) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    const float dt = 1.0f / 60.0f;

    // Pin the theme these colors resolve through.
    static const FlThemeColors base = {
        .interactive.base = { 200, 30, 40, 255 }, // selected tab fill (Solid)
        .interactive.focus = { 255, 255, 255, 255 },
        .surface.base = { 18, 20, 24, 255 }, // unselected tab fill (Ghost)
        .surface.elevated = { 36, 38, 44, 255 },
        .border.default_color = { 50, 52, 60, 255 },
        .text.primary = { 240, 240, 240, 255 },   // selected label
        .text.secondary = { 150, 150, 150, 255 }, // unselected label
        .text.accent = { 200, 30, 40, 255 },
    };
    ui_set_theme(ctx, &base);

    // Park the cursor off the strip: hover lightens both fill and label perceptually, which would pull every
    // color below off its exact theme value.
    ctx->input_state.mouse.x = -1000.0f;
    ctx->input_state.mouse.y = -1000.0f;

    FlString tabs[] = { FL_S("colors_a"), FL_S("colors_b") };
    int selected = 0;
    UiRenderCommand rects[8];
    UiRenderCommand texts[8];

#define TAB_COLOR_FRAME(...)                                                                   \
    do {                                                                                       \
        for (int color_pass = 0; color_pass < 2; color_pass++) {                               \
            ui_update_begin(ctx);                                                              \
            ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {                       \
                ui_tab_bar(tabs, 2, &selected, __VA_ARGS__);                                   \
            }                                                                                  \
            ui_update_end(ctx, dt);                                                            \
        }                                                                                      \
        ASSERT_EQ_MSG(render_cmd_collect(UiRenderCmd_Rectangle, rects, 8), 2,                  \
                      "two tabs, both painted (the resting Ghost fill "                        \
                      "is opaque surface.base)");                                              \
        ASSERT_EQ_MSG(render_cmd_collect(UiRenderCmd_Text, texts, 8), 2, "one label per tab"); \
    } while (0)

    TAB_COLOR_FRAME(.vertical = false);
    ASSERT_TRUE_MSG(tab_color_eq(rects[0].bg_color, base.interactive.base),
                    "the selected tab fills with the theme's accent");
    ASSERT_TRUE_MSG(tab_color_eq(rects[1].bg_color, base.surface.base),
                    "an unselected tab fills from the theme, not a hardcoded transparent");
    ASSERT_TRUE_MSG(tab_color_eq(texts[0].text_color, base.text.primary),
                    "the selected label follows the theme, not a hardcoded black");
    ASSERT_TRUE_MSG(tab_color_eq(texts[1].text_color, base.text.secondary), "an unselected label is secondary text");

    // Per-widget override as a named local: a compound literal inside the ui_tab_bar() macro would split on its
    // internal commas (the preprocessor only protects parentheses, not braces).
    const FlColor I1 = { 20, 60, 200, 255 };
    const FlColor T1 = { 10, 250, 90, 255 };
    const FlThemeOverride ov = {
        .slots = FlThemeSlot_InteractiveBase | FlThemeSlot_TextPrimary,
        .colors = { .interactive.base = I1, .text.primary = T1 },
    };
    TAB_COLOR_FRAME(.widget.theme_override = &ov);
    ASSERT_TRUE_MSG(tab_color_eq(rects[0].bg_color, I1), "a per-widget override reaches the selected tab's fill");
    ASSERT_TRUE_MSG(tab_color_eq(texts[0].text_color, T1), "and its label");
    ASSERT_TRUE_MSG(tab_color_eq(rects[1].bg_color, base.surface.base),
                    "slots the override does not flag stay on the base theme");

    // FlWidgetConfig::color is deliberately NOT forwarded as this widget's background override, unlike every
    // other caller of fl_resolve_widget_colors: one override spans both tab states, so honouring it would paint
    // the selected and unselected tabs the same and erase the selection cue.
    TAB_COLOR_FRAME(.widget.color = { 250, 5, 250, 255 });
    ASSERT_TRUE_MSG(tab_color_eq(rects[0].bg_color, base.interactive.base),
                    "a widget color override leaves the selected tab on the theme's accent");
    ASSERT_TRUE_MSG(tab_color_eq(rects[1].bg_color, base.surface.base), "and the unselected tab on its surface");

#undef TAB_COLOR_FRAME
}

// A checkbox and a switch driven toward the same target with the same dt share anim_toggle_toward, so their
// check_anim_t must match every frame.
UTEST(ui_checkbox, fill_eases_like_switch) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    const float dt = 1.0f / 60.0f;
    ctx->delta_time = dt;

    const FlLayoutId cb_id = { .name = FL_S("test_checkbox"), .id = 0 };
    const FlLayoutId sw_id = { .name = FL_S("test_switch_cmp"), .id = 0 };
    const u32 cb_clay = ui_layout_id_to_clay_id(cb_id);
    const u32 sw_clay = ui_layout_id_to_clay_id(sw_id);

    // No clicks needed: the fill animation reads the bound bool directly, so identical bools yield identical curves.
    bool checked = true;
    bool switched = true;

#define BOTH_FRAME()                                                 \
    do {                                                             \
        ui_update_begin(ctx);                                        \
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) { \
            ui_checkbox(FL_S("test_checkbox"), &checked);            \
            ui_switch(FL_S("test_switch_cmp"), &switched);           \
        }                                                            \
        ui_update_end(ctx, dt);                                      \
    } while (0)

    ctx->mouse_pressed_this_frame = false;
    ctx->mouse_released_this_frame = false;

    BOTH_FRAME();
    ItemState* cb = hashmap_get(&ctx->shared->item_state, cb_clay);
    ItemState* sw = hashmap_get(&ctx->shared->item_state, sw_clay);
    ASSERT_TRUE_MSG(cb != nullptr && sw != nullptr, "checkbox and switch registered interactive items");
    ASSERT_GT_MSG((double)cb->check_anim_t, 0.0, "checkbox fill advances on the first frame");
    ASSERT_LT_MSG((double)cb->check_anim_t, 1.0, "checkbox fill eases (no one-frame linear snap to fully checked)");
    ASSERT_NEAR_MSG((double)cb->check_anim_t, (double)sw->check_anim_t, 1e-6,
                    "checkbox fill matches the switch knob from the first frame (shared anim_toggle_toward)");

    for (int i = 0; i < 90; i++) {
        BOTH_FRAME();
        cb = hashmap_get(&ctx->shared->item_state, cb_clay);
        sw = hashmap_get(&ctx->shared->item_state, sw_clay);
        ASSERT_NEAR_MSG((double)cb->check_anim_t, (double)sw->check_anim_t, 1e-6,
                        "checkbox fill eases identically to the switch knob (BB closed)");
    }
    ASSERT_NEAR_MSG((double)cb->check_anim_t, 1.0, 1e-3, "checkbox fill settles fully checked");

    ctx->mouse_pressed_this_frame = false;
    ctx->mouse_released_this_frame = false;

#undef BOTH_FRAME
}

// Each dismissal route returns a one-shot FlDialogResult and clears the caller-owned open bool. Frame 1 opens
// the dialog (which returns None on the opening frame by design); the dismissal lands on frame 2.
UTEST(ui_message_box, result_paths) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    const float dt = 1.0f / 60.0f;
    ctx->delta_time = dt;

    const u32 ok_clay = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("##msgbox_ok"), .id = 0 });
    const u32 cancel_clay = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("##msgbox_cancel"), .id = 0 });

    FlDialogResult result = FlDialogResult_None;

#define MB_FRAME(open_ptr)                                                                                   \
    do {                                                                                                     \
        ui_update_begin(ctx);                                                                                \
        result = ui_message_box(FL_S("Title"), FL_S("Message"), (open_ptr), .cancel_label = FL_S("Cancel")); \
        ui_update_end(ctx, dt);                                                                              \
    } while (0)

    // --- OK confirms ---
    {
        bool open = true;
        ctx->mouse_pressed_this_frame = false;
        ctx->mouse_released_this_frame = false;
        MB_FRAME(&open);
        ASSERT_EQ_MSG((int)result, (int)FlDialogResult_None, "opening frame returns None (no decision yet)");
        ASSERT_TRUE_MSG(open, "dialog stays open on the opening frame");

        ItemState* ok = hashmap_get(&ctx->shared->item_state, ok_clay);
        ASSERT_TRUE_MSG(ok != nullptr, "OK button registered an interactive item");
        ok->state.hot = 1; // shortcut hover->hot propagation, like the switch case

        ctx->mouse_pressed_this_frame = true;
        ctx->mouse_released_this_frame = true;
        MB_FRAME(&open);
        ASSERT_EQ_MSG((int)result, (int)FlDialogResult_Confirmed, "fast tap on OK returns Confirmed");
        ASSERT_FALSE_MSG(open, "confirming clears the caller-owned open bool");
    }

    // --- Cancel button cancels ---
    {
        bool open = true;
        ctx->mouse_pressed_this_frame = false;
        ctx->mouse_released_this_frame = false;
        MB_FRAME(&open);
        ASSERT_EQ_MSG((int)result, (int)FlDialogResult_None, "opening frame returns None");

        ItemState* cancel = hashmap_get(&ctx->shared->item_state, cancel_clay);
        ASSERT_TRUE_MSG(cancel != nullptr, "Cancel button registered an interactive item");
        cancel->state.hot = 1;

        ctx->mouse_pressed_this_frame = true;
        ctx->mouse_released_this_frame = true;
        MB_FRAME(&open);
        ASSERT_EQ_MSG((int)result, (int)FlDialogResult_Cancelled, "fast tap on Cancel returns Cancelled");
        ASSERT_FALSE_MSG(open, "cancelling clears the caller-owned open bool");
    }

    // --- ESC cancels ---
    {
        bool open = true;
        ctx->mouse_pressed_this_frame = false;
        ctx->mouse_released_this_frame = false;
        MB_FRAME(&open);
        ASSERT_EQ_MSG((int)result, (int)FlDialogResult_None, "opening frame returns None");

        // ui_key_pressed reads the per-frame edge snapshot, normally captured by fl_platform_input_update, which
        // this direct-FlUiCtx fixture never runs.
        ctx->keys_pressed_this_frame[FlKey_Escape] = true;
        MB_FRAME(&open);
        ctx->keys_pressed_this_frame[FlKey_Escape] = false;
        ASSERT_EQ_MSG((int)result, (int)FlDialogResult_Cancelled, "ESC returns Cancelled");
        ASSERT_FALSE_MSG(open, "ESC clears the caller-owned open bool");
    }

    // --- ESC cancels a single-button alert too (empty cancel_label) ---
    {
        bool open = true;
        ctx->mouse_pressed_this_frame = false;
        ctx->mouse_released_this_frame = false;
        ui_update_begin(ctx);
        result = ui_message_box(FL_S("Title"), FL_S("Message"), &open, .confirm_label = FL_S("OK"));
        ui_update_end(ctx, dt);
        ASSERT_EQ_MSG((int)result, (int)FlDialogResult_None, "alert opening frame returns None");

        ctx->keys_pressed_this_frame[FlKey_Escape] = true; // edge-snapshot field, as above
        ui_update_begin(ctx);
        result = ui_message_box(FL_S("Title"), FL_S("Message"), &open, .confirm_label = FL_S("OK"));
        ui_update_end(ctx, dt);
        ctx->keys_pressed_this_frame[FlKey_Escape] = false;
        ASSERT_EQ_MSG((int)result, (int)FlDialogResult_Cancelled, "ESC cancels a single-button alert");
        ASSERT_FALSE_MSG(open, "ESC clears open for a single-button alert");
    }

#undef MB_FRAME
}

// While the dialog is open the modal scope blocks input to widgets emitted outside it; closing it is the positive
// control that shows the same click is otherwise live.
UTEST(ui_message_box, blocks_input_behind) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    const float dt = 1.0f / 60.0f;
    ctx->delta_time = dt;

    const u32 bg_clay = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("mb_bg_button"), .id = 0 });
    bool open = true;
    FlSignal bg = { 0 };

    // The dialog is emitted before the background content (the documented ordering), so the opening frame already
    // blocks the button behind it.
#define BLOCK_FRAME()                                                                          \
    do {                                                                                       \
        ui_update_begin(ctx);                                                                  \
        ui_message_box(FL_S("Title"), FL_S("Message"), &open, .cancel_label = FL_S("Cancel")); \
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {                           \
            bg = ui_button(FL_S("mb_bg_button"));                                              \
        }                                                                                      \
        ui_update_end(ctx, dt);                                                                \
    } while (0)

    // Opening frame: lay the button out so its interactive item exists.
    ctx->mouse_pressed_this_frame = false;
    ctx->mouse_released_this_frame = false;
    BLOCK_FRAME();

    ItemState* bgi = hashmap_get(&ctx->shared->item_state, bg_clay);
    ASSERT_TRUE_MSG(bgi != nullptr, "background button registered an interactive item");
    bgi->state.hot = 1;
    ctx->mouse_pressed_this_frame = true;
    ctx->mouse_released_this_frame = true;
    BLOCK_FRAME();
    ASSERT_FALSE_MSG(bg.clicked, "the dialog blocks input to the button behind it");

    // Positive control.
    open = false;
    bgi = hashmap_get(&ctx->shared->item_state, bg_clay);
    ASSERT_TRUE_MSG(bgi != nullptr, "the background button is still registered on the control frame");
    bgi->state.hot = 1;
    ctx->mouse_pressed_this_frame = true;
    ctx->mouse_released_this_frame = true;
    BLOCK_FRAME();
    ASSERT_TRUE_MSG(bg.clicked, "with the dialog closed the same click reaches the button");

    ctx->mouse_pressed_this_frame = false;
    ctx->mouse_released_this_frame = false;

#undef BLOCK_FRAME
}

// Exclusive single-choice selection and index clamping, driven through the real headless fl_app loop so selection
// rides the genuine hover -> hot -> click path rather than poked state.
typedef struct RadioPressState {
    int choice;
    FlUiCtx* ctx; // captured during the tick so we can read laid-out option geometry afterwards
    bool last_changed;
} RadioPressState;

static void radio_press_frame(void* user_data) {
    RadioPressState* st = (RadioPressState*)user_data;
    st->ctx = ui_get_ctx();
    FlString options[3] = { FL_S("A"), FL_S("B"), FL_S("C") };
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        st->last_changed = ui_radio_group(options, (int)(sizeof(options) / sizeof(options[0])), &st->choice).clicked;
    }
}

// The id mirrors ui_radio_group_impl: instance id = the selected_index pointer, folded with the option index via
// ui_item_id_seed.
static bool radio_option_center(RadioPressState* st, int index, i32* out_cx, i32* out_cy) {
    u32 opt_id = ui_layout_id_to_clay_id(
        (FlLayoutId) { .name = FL_S("radio"), .id = ui_item_id_seed((u32)(uintptr_t)&st->choice, (u32)index) });
    ItemState* item = hashmap_get(&st->ctx->shared->item_state, opt_id);
    if (!item || item->state.size.x <= 0.0f) {
        return false;
    }
    *out_cx = (i32)(item->state.pos.x + item->state.size.x * 0.5f);
    *out_cy = (i32)(item->state.pos.y + item->state.size.y * 0.5f);
    return true;
}

UTEST(ui_radio_group, exclusive_selection_and_clamping) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("radio"), .width = 400, .height = 300, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    RadioPressState st = { .choice = 0, .ctx = nullptr };

    st.choice = -5;
    fl_app_tick(app, &st, radio_press_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    ASSERT_EQ_MSG(st.choice, 0, "negative selected_index clamps to 0");

    st.choice = 99;
    fl_app_tick(app, &st, radio_press_frame);
    ASSERT_EQ_MSG(st.choice, 0, "out-of-range selected_index clamps to 0");

    // Hover option 2 for a couple of ticks so hot state propagates (next_hot_id -> hot_id -> state.hot).
    i32 cx = 0, cy = 0;
    ASSERT_TRUE_MSG(radio_option_center(&st, 2, &cx, &cy), "option 2 was not laid out");
    fl_app_inject_mouse_move(app, cx, cy);
    fl_app_tick(app, &st, radio_press_frame);
    fl_app_tick(app, &st, radio_press_frame);
    ASSERT_EQ_MSG(st.choice, 0, "hovering an option must not change the selection");

    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, &st, radio_press_frame);
    ASSERT_EQ_MSG(st.choice, 0, "radio selects on release, not on press");

    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    fl_app_tick(app, &st, radio_press_frame);
    ASSERT_EQ_MSG(st.choice, 2, "releasing over option 2 wrote its index back to selected_index");
    ASSERT_TRUE_MSG(st.last_changed, "selecting a new option reports a selection change");

    ASSERT_TRUE_MSG(radio_option_center(&st, 0, &cx, &cy), "option 0 was not laid out");
    fl_app_inject_mouse_move(app, cx, cy);
    fl_app_tick(app, &st, radio_press_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, &st, radio_press_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    fl_app_tick(app, &st, radio_press_frame);
    ASSERT_EQ_MSG(st.choice, 0, "selection moved to option 0 (exclusive — option 2 cleared)");
    ASSERT_TRUE_MSG(st.last_changed, "moving the selection reports a change");

    // Re-select the option that is already current.
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, &st, radio_press_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    fl_app_tick(app, &st, radio_press_frame);
    ASSERT_EQ_MSG(st.choice, 0, "re-selecting the current option leaves the index unchanged");
    ASSERT_FALSE_MSG(st.last_changed, "re-selecting the current option reports no selection change");

    fl_app_destroy(app);
}

// Menu cases drive the real headless fl_app loop so the menu rides the genuine right-click -> hover -> hot -> click
// path rather than poked state.
typedef struct MenuTestState {
    FlUiCtx* ctx;
    bool menu_open; // ui_menu_is_open snapshot AFTER the menu block (post-dismissal) on the last tick
    bool cut_clicked;
    bool stay_clicked;
    bool disabled_clicked;
    bool delete_clicked;
} MenuTestState;

static const FlLayoutId k_menu_region_id = { .name = { .data = "ctx_region", .length = 10, .is_static = 1 }, .id = 0 };

static const FlLayoutId k_menu_id = { .name = { .data = "ctx_menu", .length = 8, .is_static = 1 }, .id = 0 };

static void menu_frame(void* user_data) {
    MenuTestState* st = (MenuTestState*)user_data;
    st->ctx = ui_get_ctx();

    // The region is the top-level element; Clay grows it to fill the window.
    ui_menu_region(k_menu_region_id, k_menu_id) {}

    ui_menu(k_menu_id, .min_width = 200) {
        if (ui_menu_item(FL_S("Cut"), .shortcut = FL_S("Ctrl+X")).clicked) {
            st->cut_clicked = true;
        }
        ui_menu_item(FL_S("Copy"), .shortcut = FL_S("Ctrl+C"));
        if (ui_menu_item(FL_S("Paste"), .disabled = true).clicked) {
            st->disabled_clicked = true;
        }
        if (ui_menu_item(FL_S("Stay"), .close_policy = FlMenuClosePolicy_KeepOpen).clicked) {
            st->stay_clicked = true;
        }
    }

    // Snapshot AFTER the block so an item-close / dismissal that fires inside ui_menu_end is reflected this tick.
    st->menu_open = ui_menu_is_open_impl(k_menu_id);
}

// Mirrors menu_emit_row's id derivation, which folds the level into the seed so rows at different levels never
// collide (ui_menu_row_id_seed).
static bool menu_item_center(MenuTestState* st, FlString label, int level, int index, i32* out_cx, i32* out_cy) {
    u32 id = ui_layout_id_to_clay_id((FlLayoutId) { .name = label, .id = ui_menu_row_id_seed(level, index) });
    ItemState* item = hashmap_get(&st->ctx->shared->item_state, id);
    if (!item || item->state.size.x <= 0.0f) {
        return false;
    }
    *out_cx = (i32)(item->state.pos.x + item->state.size.x * 0.5f);
    *out_cy = (i32)(item->state.pos.y + item->state.size.y * 0.5f);
    return true;
}

static void menu_open_at(FlApp* app, MenuTestState* st, i32 x, i32 y) {
    fl_app_inject_mouse_move(app, x, y);
    fl_app_tick(app, st, menu_frame);
    fl_app_tick(app, st, menu_frame); // let hover settle into hot state on the region
    fl_app_inject_mouse_button(app, FlMouseButton_Right, true);
    fl_app_tick(app, st, menu_frame); // right-press edge opens the menu at the cursor
    fl_app_inject_mouse_button(app, FlMouseButton_Right, false);
    fl_app_tick(app, st, menu_frame);
}

// Hovers first so hot state propagates before the click.
static void menu_click_at(FlApp* app, MenuTestState* st, i32 x, i32 y) {
    fl_app_inject_mouse_move(app, x, y);
    fl_app_tick(app, st, menu_frame);
    fl_app_tick(app, st, menu_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, st, menu_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    fl_app_tick(app, st, menu_frame);
}

// The press tick carries the action press edge into the frame body (begin() reads it and moves/activates the
// highlight); the release tick clears it. Mouse stays put so keyboard owns the highlight.
static void menu_key_tap(FlApp* app, MenuTestState* st, FlKey key) {
    fl_app_inject_key(app, key, true);
    fl_app_tick(app, st, menu_frame);
    fl_app_inject_key(app, key, false);
    fl_app_tick(app, st, menu_frame);
}

// Settles hover into a hovered signal: the move feeds next_hot_id one frame, hot_id the next, state.hot the frame
// after. Returns false if the item was not laid out.
static bool menu_hover_item(FlApp* app, MenuTestState* st, FlString label, int level, int index) {
    i32 cx = 0, cy = 0;
    if (!menu_item_center(st, label, level, index, &cx, &cy)) {
        return false;
    }
    fl_app_inject_mouse_move(app, cx, cy);
    fl_app_tick(app, st, menu_frame);
    fl_app_tick(app, st, menu_frame);
    fl_app_tick(app, st, menu_frame);
    return true;
}

UTEST(ui_menu, context_menu_tracer) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    // A roomy window so the menu opens fully downward (no flip/clamp) - geometry stays simple for the click paths.
    FlAppSettings settings = { .window_title = FL_S("menu"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    MenuTestState st = { 0 };

    fl_app_tick(app, &st, menu_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    ASSERT_FALSE_MSG(st.menu_open, "menu starts closed");

    menu_open_at(app, &st, 120, 120);
    ASSERT_TRUE_MSG(st.menu_open, "right-click in the region opened the menu");

    i32 cx = 0, cy = 0;
    ASSERT_TRUE_MSG(menu_item_center(&st, FL_S("Cut"), 0, 0, &cx, &cy), "Cut item was not laid out");

    // Three ticks let the hover settle into a hovered signal during item emission: the move feeds next_hot_id one
    // frame, hot_id picks it up the next, and state.hot (read while the item is emitted) lands the frame after that.
    i32 copyx = 0, copyy = 0;
    ASSERT_TRUE_MSG(menu_item_center(&st, FL_S("Copy"), 0, 1, &copyx, &copyy), "Copy item was not laid out");
    fl_app_inject_mouse_move(app, copyx, copyy);
    fl_app_tick(app, &st, menu_frame);
    fl_app_tick(app, &st, menu_frame);
    fl_app_tick(app, &st, menu_frame);
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 1,
                  "hovering Copy highlights index 1 at level 0");
    fl_app_inject_mouse_move(app, cx, cy); // move back over Cut (index 0)
    fl_app_tick(app, &st, menu_frame);
    fl_app_tick(app, &st, menu_frame);
    fl_app_tick(app, &st, menu_frame);
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 0,
                  "moving to Cut moves the highlight to index 0");

    // Hovering the disabled Paste item still lights it - availability is the greyed label's job, not the highlight's.
    ASSERT_TRUE_MSG(menu_hover_item(app, &st, FL_S("Paste"), 0, 2), "Paste item was not laid out");
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 2,
                  "hovering a disabled row moves the highlight onto it");

    menu_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 3,
                  "Down from a hover-highlighted disabled row lands on the next navigable row");

    i32 dx = 0, dy = 0;
    ASSERT_TRUE_MSG(menu_item_center(&st, FL_S("Paste"), 0, 2, &dx, &dy), "Paste item was not laid out");
    menu_click_at(app, &st, dx, dy);
    ASSERT_FALSE_MSG(st.disabled_clicked, "a disabled item never reports clicked");
    ASSERT_TRUE_MSG(st.menu_open, "clicking a disabled item leaves the menu open");

    i32 sx = 0, sy = 0;
    ASSERT_TRUE_MSG(menu_item_center(&st, FL_S("Stay"), 0, 3, &sx, &sy), "Stay item was not laid out");
    menu_click_at(app, &st, sx, sy);
    ASSERT_TRUE_MSG(st.stay_clicked, "keep-open item reports clicked");
    ASSERT_TRUE_MSG(st.menu_open, "keep-open item does not close the menu");

    ASSERT_TRUE_MSG(menu_item_center(&st, FL_S("Cut"), 0, 0, &cx, &cy), "Cut item geometry");
    menu_click_at(app, &st, cx, cy);
    ASSERT_TRUE_MSG(st.cut_clicked, "default action item reports clicked");
    ASSERT_FALSE_MSG(st.menu_open, "default close policy tore the menu down on activation");

    menu_open_at(app, &st, 120, 120);
    ASSERT_TRUE_MSG(st.menu_open, "reopened the menu");
    fl_app_inject_key(app, FlKey_Escape, true);
    fl_app_tick(app, &st, menu_frame);
    fl_app_inject_key(app, FlKey_Escape, false);
    fl_app_tick(app, &st, menu_frame);
    ASSERT_FALSE_MSG(st.menu_open, "ESC dismisses the menu");

    // The top-left corner is well clear of a menu opened at 120,120.
    menu_open_at(app, &st, 120, 120);
    ASSERT_TRUE_MSG(st.menu_open, "reopened the menu for the click-outside case");
    menu_click_at(app, &st, 5, 5);
    ASSERT_FALSE_MSG(st.menu_open, "a click outside the menu rects dismisses it");

    fl_app_destroy(app);
}

// Keyboard as the second driver of the per-level highlight, arbitrated last-input-wins against hover. Gamepad A/B
// share CONFIRM/CANCEL with Enter/ESC, so these paths exercise the pad plumbing too (headless has no pad injection).
UTEST(ui_menu, keyboard_nav) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menu_kbd"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    MenuTestState st = { 0 };
    fl_app_tick(app, &st, menu_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");

    // Items are Cut(0), Copy(1), Paste(2, disabled), Stay(3, keep-open).
    menu_open_at(app, &st, 120, 120);
    ASSERT_TRUE_MSG(st.menu_open, "menu opened for keyboard nav");

    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 0,
                  "open highlights the first navigable item (Cut)");

    menu_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 1, "Down: Cut -> Copy");
    menu_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 3,
                  "Down: Copy -> Stay (disabled Paste skipped)");

    menu_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 0, "Down wraps Stay -> Cut");

    menu_key_tap(app, &st, FlKey_Up);
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 3, "Up wraps Cut -> Stay");
    menu_key_tap(app, &st, FlKey_Up);
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 1, "Up: Stay -> Copy (disabled Paste skipped)");

    ASSERT_TRUE_MSG(menu_hover_item(app, &st, FL_S("Stay"), 0, 3), "Stay item was not laid out for hover");
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 3,
                  "hover reclaims the highlight (mouse moved last)");

    menu_key_tap(app, &st, FlKey_Up);
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 1,
                  "keyboard reclaims the highlight from a stationary hover (Stay -> Copy)");

    // Copy has no handler in menu_frame, so move to Cut (which sets cut_clicked) before activating.
    menu_key_tap(app, &st, FlKey_Up);
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[0].highlight_index, 0, "Up: Copy -> Cut before activate");
    fl_app_inject_key(app, FlKey_Enter, true);
    fl_app_tick(app, &st, menu_frame);
    ASSERT_TRUE_MSG(st.cut_clicked, "Enter activates the highlighted item (Cut reported clicked)");
    ASSERT_FALSE_MSG(st.menu_open, "activating a default-close item tore the menu down");
    fl_app_inject_key(app, FlKey_Enter, false);
    fl_app_tick(app, &st, menu_frame);

    menu_open_at(app, &st, 120, 120);
    ASSERT_TRUE_MSG(st.menu_open, "reopened the menu for the ESC-close case");
    fl_app_inject_key(app, FlKey_Escape, true);
    fl_app_tick(app, &st, menu_frame);
    fl_app_inject_key(app, FlKey_Escape, false);
    fl_app_tick(app, &st, menu_frame);
    ASSERT_FALSE_MSG(st.menu_open, "ESC (CANCEL) closes the menu");

    fl_app_destroy(app);
}

// inject_text queues the character into the headless backend; the next tick's pump drains it into
// text_input_this_frame and the menu's begin() folds it into the type-ahead prefix. The mouse stays put across the
// tick so type-ahead owns the shared highlight (last-input-wins).
static void menu_type_char(FlApp* app, MenuTestState* st, FlString ch) {
    fl_app_inject_text(app, ch);
    fl_app_tick(app, st, menu_frame);
}

// Forces the accumulated type-ahead prefix stale so the next character starts fresh, without depending on the
// headless frame delta to cross the timeout: any positive input-clock time is then "past" it.
static void menu_typeahead_reset(MenuTestState* st) {
    st->ctx->shared->menu.typeahead.deadline = 0.0;
}

// Ticks with no input so the input clock advances - lets the type-ahead timeout elapse in real time (with a pinned
// dt) rather than via the whitebox reset above.
static void menu_idle_ticks(FlApp* app, MenuTestState* st, int n) {
    for (int i = 0; i < n; i++) {
        fl_app_tick(app, st, menu_frame);
    }
}

// The menu's type-ahead prefix is bounded at UI_MENU_TYPEAHEAD_CAP bytes: two rows sharing their first 24 bytes both
// match a longer typed prefix, so the cursor row stays put.
UTEST(ui_menu, type_ahead_prefix_cap) {
    const char* alphabet = "abcdefghijklmnopqrstuvwxyzab";
    FlString labels[2]
        = { { .data = "abcdefghijklmnopqrstuvwxQQQQ", .length = 28 }, { .data = alphabet, .length = 28 } };
    TypeAheadState state = { 0 };
    i32 idx = type_ahead_match(&state, labels, 2, 0, (FlString) { .data = alphabet, .length = 28 }, 1.0,
                               UI_MENU_TYPEAHEAD_CAP);
    ASSERT_EQ_MSG((int)idx, 0, "a prefix past the cap truncates to the cap and keeps the current row");
    ASSERT_EQ_MSG((int)state.len, UI_MENU_TYPEAHEAD_CAP, "prefix length is bounded by the menu cap");
}

// The trail highlight truncates each channel of the float blend; a rounding or reordered lerp shifts the result.
UTEST(ui_menu, blend_truncates_channels) {
    FlColor a = { 5, 5, 5, 255 };
    FlColor b = { 0, 0, 0, 255 };
    FlColor c = ui_menu_blend(a, b, 0.6f);
    ASSERT_EQ_MSG((int)c.r, 1, "5 -> 0 at t=0.6 truncates to 1");
    ASSERT_EQ_MSG((int)c.a, 255, "an equal alpha stays put");
}

// A printable character jumps the deepest level's highlight to the next item whose label starts with it - the third
// driver of the per-level highlight, alongside hover and the arrow keys.
UTEST(ui_menu, type_ahead) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menu_type"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    fl_app_set_test_dt(app, 0.1f); // pinned 0.1s/tick so the ~1.0s type-ahead timeout below is reproducible

    MenuTestState st = { 0 };
    fl_app_tick(app, &st, menu_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    UiMenuState* m = &st.ctx->shared->menu;

    // Items are Cut(0), Copy(1), Paste(2, disabled), Stay(3).
    menu_open_at(app, &st, 120, 120);
    ASSERT_TRUE_MSG(st.menu_open, "menu opened for type-ahead");
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 0, "open highlights Cut");

    menu_type_char(app, &st, FL_S("c"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "type 'c': Cut -> Copy");
    ASSERT_TRUE_MSG(st.menu_open, "type-ahead leaves the menu open");

    menu_type_char(app, &st, FL_S("c"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 0, "type 'c' again cycles Copy -> Cut (wrapped)");

    menu_typeahead_reset(&st);
    menu_type_char(app, &st, FL_S("s"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 3, "type 's': -> Stay");

    menu_typeahead_reset(&st);
    menu_type_char(app, &st, FL_S("z"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 3, "no-match keystroke leaves the highlight put");

    // 'p' matches only the disabled Paste.
    menu_typeahead_reset(&st);
    menu_type_char(app, &st, FL_S("p"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 3, "disabled Paste is not a type-ahead target");

    // From Stay, hover Cut; then 'c' cycles to Copy though the pointer still rests on Cut.
    ASSERT_TRUE_MSG(menu_hover_item(app, &st, FL_S("Cut"), 0, 0), "Cut item was not laid out for hover");
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 0, "hover reclaims the highlight from type-ahead (Stay -> Cut)");
    menu_typeahead_reset(&st);
    menu_type_char(app, &st, FL_S("c"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "type reclaims from a stationary hover (Cut -> Copy)");

    // Timeout stale-out with real elapsed time (not the whitebox reset): after the idle, 'u' is a fresh no-match
    // rather than an accumulated "cu" (which would narrow to Cut).
    menu_click_at(app, &st, 5, 5);
    menu_open_at(app, &st, 120, 120);
    menu_type_char(app, &st, FL_S("c"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "timeout setup: 'c' Cut -> Copy");
    menu_idle_ticks(app, &st, 12); // 12 * 0.1s = 1.2s > TYPE_AHEAD_TIMEOUT - the prefix goes stale
    menu_type_char(app, &st, FL_S("u"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "after a >1s pause 'u' is a fresh no-match, not 'cu'");

    // Multi-byte prefix WITHOUT a pause: 'c' then 'u' accumulates "cu", which only Cut matches - the second byte
    // narrows rather than cycling.
    menu_click_at(app, &st, 5, 5);
    ASSERT_FALSE_MSG(st.menu_open, "click-outside closed the menu before the multi-char case");
    menu_open_at(app, &st, 120, 120);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 0, "reopen highlights Cut");
    menu_type_char(app, &st, FL_S("c"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "multi-char step 1: 'c' Cut -> Copy");
    menu_type_char(app, &st, FL_S("u"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 0, "multi-char step 2: 'cu' narrows to Cut");

    fl_app_destroy(app);
}

// Submenus extend the open path to depth > 1 under the same focus scope. Driven with a pinned delta-time so the
// hover-dwell and branch-switch grace timers are reproducible.
//
// Submenu tree (level.index):
//   L0: Item0(0)  Sub(1, submenu)  Item2(2)
//   L1 (under Sub): S0(0)  Sub2(1, submenu)  S1(2)
//   L2 (under Sub2): D0(0)  D1(1)
static void submenu_frame(void* user_data) {
    MenuTestState* st = (MenuTestState*)user_data;
    st->ctx = ui_get_ctx();

    ui_menu_region(k_menu_region_id, k_menu_id) {}

    ui_menu(k_menu_id, .min_width = 200) {
        ui_menu_item(FL_S("Item0"));
        ui_submenu(FL_S("Sub")) {
            ui_menu_item(FL_S("S0"));
            ui_submenu(FL_S("Sub2")) {
                ui_menu_item(FL_S("D0"));
                ui_menu_item(FL_S("D1"));
            }
            ui_menu_item(FL_S("S1"));
        }
        ui_menu_item(FL_S("Item2"));
        ui_submenu(FL_S("DisSub"), .disabled = true) { // index 3: a disabled submenu must never open
            ui_menu_item(FL_S("X"));
        }
    }

    st->menu_open = ui_menu_is_open_impl(k_menu_id);
}

static void s3_ticks(FlApp* app, MenuTestState* st, int n) {
    for (int i = 0; i < n; i++) {
        fl_app_tick(app, st, submenu_frame);
    }
}

// (100,100) is the top-left anchor, over Item0 - not over the Sub row - so opening never accidentally dwells a
// submenu.
static void s3_open(FlApp* app, MenuTestState* st) {
    fl_app_inject_mouse_move(app, 100, 100);
    s3_ticks(app, st, 2);
    fl_app_inject_mouse_button(app, FlMouseButton_Right, true);
    s3_ticks(app, st, 1);
    fl_app_inject_mouse_button(app, FlMouseButton_Right, false);
    s3_ticks(app, st, 1);
}

// Click outside the chain to close it fully (sticky empty-space would NOT close, so use an actual press).
static void s3_close(FlApp* app, MenuTestState* st) {
    fl_app_inject_mouse_move(app, 5, 5);
    s3_ticks(app, st, 2);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    s3_ticks(app, st, 1);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    s3_ticks(app, st, 1);
}

static void s3_key_tap(FlApp* app, MenuTestState* st, FlKey key) {
    fl_app_inject_key(app, key, true);
    fl_app_tick(app, st, submenu_frame);
    fl_app_inject_key(app, key, false);
    fl_app_tick(app, st, submenu_frame);
}

static void s3_type_char(FlApp* app, MenuTestState* st, FlString ch) {
    fl_app_inject_text(app, ch);
    fl_app_tick(app, st, submenu_frame);
}

UTEST(ui_menu, submenu_open_path) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    // 800x600 so submenus open rightward with room (no flip): L0 at x=100 w=200, L1 at x=300, L2 at x=500.
    FlAppSettings settings = { .window_title = FL_S("submenu"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    fl_app_set_test_dt(app, 0.1f); // pinned 0.1s/tick so the dwell (~0.3s) and grace (~0.25s) timers are reproducible

    MenuTestState st = { 0 };
    fl_app_tick(app, &st, submenu_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");

    UiMenuState* m = &st.ctx->shared->menu;

    // --- Keyboard descend / ascend, including depth 3 ---
    s3_open(app, &st);
    ASSERT_TRUE_MSG(st.menu_open, "right-click opened the menu");
    ASSERT_EQ_MSG((int)m->depth, 1, "fresh menu is a single level");

    s3_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "Down highlights the Sub row");
    s3_key_tap(app, &st, FlKey_Right);
    ASSERT_EQ_MSG((int)m->depth, 2, "Right descends into the highlighted submenu (depth 2)");
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 0, "the new deepest level highlights its first navigable row");

    s3_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 1, "Down moves the deepest highlight to Sub2");
    s3_key_tap(app, &st, FlKey_Right);
    ASSERT_EQ_MSG((int)m->depth, 3, "Right descends again (depth 3, open path > 1)");

    s3_key_tap(app, &st, FlKey_Left);
    ASSERT_EQ_MSG((int)m->depth, 2, "Left ascends one level");
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 1, "ascending keeps the parent highlight on the row we came from");
    s3_key_tap(app, &st, FlKey_Left);
    ASSERT_EQ_MSG((int)m->depth, 1, "Left ascends back to the root");

    // --- ESC pops one level, closing only at the root ---
    s3_close(app, &st); // reopen fresh so the highlight resets to the first row
    s3_open(app, &st);
    s3_key_tap(app, &st, FlKey_Down);
    s3_key_tap(app, &st, FlKey_Right);
    ASSERT_EQ_MSG((int)m->depth, 2, "set up depth 2 for the ESC-pop case");
    s3_key_tap(app, &st, FlKey_Escape);
    ASSERT_EQ_MSG((int)m->depth, 1, "ESC pops one level (does not close the chain)");
    ASSERT_TRUE_MSG(st.menu_open, "ESC at depth 2 left the chain open");
    s3_key_tap(app, &st, FlKey_Escape);
    ASSERT_FALSE_MSG(st.menu_open, "ESC at the root closes the chain");

    // --- Cold-open hover-dwell: sub-threshold stays closed, crossing ~0.3s opens ---
    s3_open(app, &st);
    i32 sx = 0, sy = 0;
    ASSERT_TRUE_MSG(menu_item_center(&st, FL_S("Sub"), 0, 1, &sx, &sy), "Sub row was not laid out");
    fl_app_inject_mouse_move(app, sx, sy);
    s3_ticks(app, &st, 2); // hover settles onto Sub; accumulated dwell <= ~0.1s
    ASSERT_EQ_MSG((int)m->depth, 1, "a brief hover (< dwell threshold) does not open the submenu");
    s3_ticks(app, &st, 6); // keep resting on Sub → cross the ~0.3s dwell
    ASSERT_EQ_MSG((int)m->depth, 2, "resting past the dwell threshold opens the submenu");
    ASSERT_EQ_MSG((int)m->levels[0].expanded_child_id,
                  (int)ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Sub"), .id = 1 }),
                  "the open path is identity-keyed to the Sub widget id");

    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "the parent level keeps a trail highlight on the Sub row");
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 0, "the deepest level has its own active highlight (S0)");

    // --- Diagonal INTO the open submenu survives (no collapse) ---
    i32 s0x = 0, s0y = 0;
    ASSERT_TRUE_MSG(menu_item_center(&st, FL_S("S0"), 1, 0, &s0x, &s0y), "S0 (level 1) was not laid out");
    fl_app_inject_mouse_move(app, s0x, s0y);
    s3_ticks(app, &st, 3);
    ASSERT_EQ_MSG((int)m->depth, 2, "moving into the open submenu keeps it open");
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 0, "hover drives the deepest-level highlight onto S0");

    // --- Branch-switch grace: a stationary sibling collapses the deeper path only after ~0.25s ---
    i32 i0x = 0, i0y = 0;
    ASSERT_TRUE_MSG(menu_item_center(&st, FL_S("Item0"), 0, 0, &i0x, &i0y), "Item0 was not laid out");
    fl_app_inject_mouse_move(app, i0x, i0y);
    s3_ticks(app, &st, 2); // grace not yet elapsed (<= ~0.1s)
    ASSERT_EQ_MSG((int)m->depth, 2, "a sibling hover within the grace window keeps the deeper path open");
    s3_ticks(app, &st, 4); // cross the ~0.25s grace
    ASSERT_EQ_MSG((int)m->depth, 1, "a sibling-leaf hover past the grace collapses the deeper path");

    // --- Early-cancel: moving AWAY from the open submenu collapses immediately (before the grace elapses) ---
    fl_app_inject_mouse_move(app, sx, sy);
    s3_ticks(app, &st, 8);
    ASSERT_EQ_MSG((int)m->depth, 2, "re-opened Sub for the early-cancel case");
    fl_app_inject_mouse_move(app, i0x, i0y); // land on the Item0 sibling (approach from the right, not 'away')
    s3_ticks(app, &st, 1);
    ASSERT_EQ_MSG((int)m->depth, 2, "just landed on the sibling — grace has not elapsed yet");
    fl_app_inject_mouse_move(app, i0x - 60, i0y); // a leftward move = away from the right-opened submenu
    s3_ticks(app, &st, 1);
    ASSERT_EQ_MSG((int)m->depth, 1, "moving away from the open submenu cancels the grace and collapses immediately");

    // --- Click opens a submenu instantly (no dwell) ---
    // The short dt keeps the dwell timer far from its threshold, so the open is attributable to the click.
    fl_app_set_test_dt(app, 0.02f);
    s3_close(app, &st);
    s3_open(app, &st);
    ASSERT_TRUE_MSG(menu_item_center(&st, FL_S("Sub"), 0, 1, &sx, &sy), "Sub row was not laid out for the click case");
    fl_app_inject_mouse_move(app, sx, sy);
    s3_ticks(app, &st, 2);
    ASSERT_EQ_MSG((int)m->depth, 1, "a brief hover before the click has not dwelled the submenu open");
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    s3_ticks(app, &st, 1);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    s3_ticks(app, &st, 1);
    ASSERT_EQ_MSG((int)m->depth, 2, "clicking a submenu parent opens it instantly");

    // --- A disabled submenu never opens, even resting well past the dwell threshold ---
    fl_app_set_test_dt(app, 0.1f);
    s3_close(app, &st);
    s3_open(app, &st);
    i32 dsx = 0, dsy = 0;
    ASSERT_TRUE_MSG(menu_item_center(&st, FL_S("DisSub"), 0, 3, &dsx, &dsy), "DisSub row was not laid out");
    fl_app_inject_mouse_move(app, dsx, dsy);
    s3_ticks(app, &st, 8); // ~0.8s, well past the ~0.3s dwell
    ASSERT_EQ_MSG((int)m->depth, 1, "a disabled submenu never opens on hover-dwell");

    // --- Sticky: moving the cursor into empty space keeps the chain open (only click-outside / ESC / action close) ---
    fl_app_inject_mouse_move(app, 600, 300); // empty space, clear of every menu rect
    s3_ticks(app, &st, 2);
    ASSERT_TRUE_MSG(st.menu_open, "moving into empty space does not close the chain (sticky)");

    s3_close(app, &st);
    ASSERT_FALSE_MSG(st.menu_open, "click-outside closed the chain");

    fl_app_destroy(app);
}

// Type-ahead operates on the deepest open level only, not the whole open path.
UTEST(ui_menu, type_ahead_submenu) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menu_type_sub"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    fl_app_set_test_dt(app, 0.1f);

    MenuTestState st = { 0 };
    fl_app_tick(app, &st, submenu_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    UiMenuState* m = &st.ctx->shared->menu;

    // L0: Item0(0) Sub(1, submenu) Item2(2) DisSub(3). Descend into Sub -> L1: S0(0) Sub2(1, submenu) S1(2).
    s3_open(app, &st);
    s3_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "L0 highlight on Sub");
    s3_key_tap(app, &st, FlKey_Right);
    ASSERT_EQ_MSG((int)m->depth, 2, "the submenu is open (depth 2)");
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 0, "the child level highlights its first item (S0)");

    // S0, Sub2, S1 all begin with 'S', so typing 's' at the deepest level cycles among them (a submenu parent is a
    // navigable type-ahead target too).
    s3_type_char(app, &st, FL_S("s"));
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 1, "type 's' at L1: S0 -> Sub2");
    s3_type_char(app, &st, FL_S("s"));
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 2, "type 's' again: Sub2 -> S1");
    s3_type_char(app, &st, FL_S("s"));
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 0, "type 's' again wraps: S1 -> S0");

    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "the parent level's highlight stayed on Sub throughout");
    ASSERT_EQ_MSG((int)m->depth, 2, "typing did not open or collapse a level (still depth 2)");

    s3_close(app, &st);
    fl_app_destroy(app);
}

// A menu row's id folds its build level in alongside its in-level index (ui_menu_row_id_seed), so a parent-level row
// and one of its open submenu's rows that share BOTH a label and an in-level index still resolve to distinct ids.
//
// Layout (level.index):
//   L0: Branch(0, submenu)  Open(1)
//   L1 (under Branch): First(0)  Open(1)   <- same label "Open" AND same in-level index (1) as the L0 row
typedef struct MenuCollideState {
    FlUiCtx* ctx;
    bool menu_open;
    bool parent_open_clicked;
    bool sub_open_clicked;
} MenuCollideState;

static void collide_frame(void* user_data) {
    MenuCollideState* st = (MenuCollideState*)user_data;
    st->ctx = ui_get_ctx();

    ui_menu_region(k_menu_region_id, k_menu_id) {}

    ui_menu(k_menu_id, .min_width = 200) {
        ui_submenu(FL_S("Branch")) {
            ui_menu_item(FL_S("First"));
            if (ui_menu_item(FL_S("Open")).clicked) {
                st->sub_open_clicked = true;
            }
        }
        if (ui_menu_item(FL_S("Open")).clicked) {
            st->parent_open_clicked = true;
        }
    }

    st->menu_open = ui_menu_is_open_impl(k_menu_id);
}

static bool collide_center(MenuCollideState* st, FlString label, int level, int index, i32* out_cx, i32* out_cy) {
    u32 id = ui_layout_id_to_clay_id((FlLayoutId) { .name = label, .id = ui_menu_row_id_seed(level, index) });
    ItemState* item = hashmap_get(&st->ctx->shared->item_state, id);
    if (!item || item->state.size.x <= 0.0f) {
        return false;
    }
    *out_cx = (i32)(item->state.pos.x + item->state.size.x * 0.5f);
    *out_cy = (i32)(item->state.pos.y + item->state.size.y * 0.5f);
    return true;
}

static void collide_ticks(FlApp* app, MenuCollideState* st, int n) {
    for (int i = 0; i < n; i++) {
        fl_app_tick(app, st, collide_frame);
    }
}

static void collide_open(FlApp* app, MenuCollideState* st) {
    fl_app_inject_mouse_move(app, 100, 100);
    collide_ticks(app, st, 2);
    fl_app_inject_mouse_button(app, FlMouseButton_Right, true);
    collide_ticks(app, st, 1);
    fl_app_inject_mouse_button(app, FlMouseButton_Right, false);
    collide_ticks(app, st, 1);
}

static void collide_key_tap(FlApp* app, MenuCollideState* st, FlKey key) {
    fl_app_inject_key(app, key, true);
    fl_app_tick(app, st, collide_frame);
    fl_app_inject_key(app, key, false);
    fl_app_tick(app, st, collide_frame);
}

static void collide_click_at(FlApp* app, MenuCollideState* st, i32 x, i32 y) {
    fl_app_inject_mouse_move(app, x, y);
    collide_ticks(app, st, 3); // settle hover -> hot on the target row
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    collide_ticks(app, st, 1);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    collide_ticks(app, st, 1);
}

UTEST(ui_menu, submenu_id_collision) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menu_collide"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    fl_app_set_test_dt(app, 0.02f); // short dt so a stray hover never dwells a submenu open on its own

    MenuCollideState st = { 0 };
    fl_app_tick(app, &st, collide_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    UiMenuState* m = &st.ctx->shared->menu;

    // Descend into Branch with the keyboard so the submenu's "Open" (level 1, index 1) lays out.
    collide_open(app, &st);
    ASSERT_TRUE_MSG(st.menu_open, "right-click opened the menu");
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 0, "open highlights the first navigable row (Branch)");
    collide_key_tap(app, &st, FlKey_Right);
    ASSERT_EQ_MSG((int)m->depth, 2, "Right descended into the Branch submenu (depth 2)");

    const u32 parent_open_id
        = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Open"), .id = ui_menu_row_id_seed(0, 1) });
    const u32 sub_open_id
        = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Open"), .id = ui_menu_row_id_seed(1, 1) });
    ASSERT_NE_MSG((unsigned)parent_open_id, (unsigned)sub_open_id,
                  "the parent-level and submenu-level Open rows must resolve to distinct ids");

    i32 pox = 0, poy = 0, sox = 0, soy = 0;
    ASSERT_TRUE_MSG(collide_center(&st, FL_S("Open"), 0, 1, &pox, &poy), "parent-level Open row was not laid out");
    ASSERT_TRUE_MSG(collide_center(&st, FL_S("Open"), 1, 1, &sox, &soy), "submenu-level Open row was not laid out");
    ASSERT_TRUE_MSG(sox != pox || soy != poy, "the two Open rows occupy different screen positions");

    // With colliding ids the shared row state would set the parent level's highlight too.
    fl_app_inject_mouse_move(app, sox, soy);
    collide_ticks(app, &st, 3);
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 1, "hovering the submenu Open highlights it at level 1");
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 0,
                  "the parent level's highlight stays on the Branch trail (hover did not leak to the parent Open)");

    collide_click_at(app, &st, sox, soy);
    ASSERT_TRUE_MSG(st.sub_open_clicked, "clicking the submenu Open activates the submenu row");
    ASSERT_FALSE_MSG(st.parent_open_clicked, "the parent-level Open (same label + index) must not activate");

    fl_app_destroy(app);
}

// The non-stateful item kinds: a section header, a separator, the disabled flag, and right-aligned shortcut text.
// Header / separator / disabled rows lay out yet are skipped by keyboard navigation and never activate, and a click
// on a header or separator stays inside the menu (they are part of the surface, not click-outside). Shortcut text is
// display-only - no key dispatch is wired anywhere.
//
// Menu layout (per-level index in parens; navigable rows are the action items only):
//   0 header "Edit"            (non-nav)
//   1 item   "Cut"  Ctrl+X
//   2 item   "Copy"
//   3 separator                (non-nav)
//   4 item   "Paste" disabled  (non-nav)
//   5 item   "Delete"
static const FlString k_menu_sep_name = { .data = "##menu_sep", .length = 10, .is_static = 1 };

static void static_kinds_frame(void* user_data) {
    MenuTestState* st = (MenuTestState*)user_data;
    st->ctx = ui_get_ctx();

    ui_menu_region(k_menu_region_id, k_menu_id) {}

    ui_menu(k_menu_id, .min_width = 200) {
        ui_menu_header(FL_S("Edit"));
        ui_menu_item(FL_S("Cut"), .shortcut = FL_S("Ctrl+X"));
        ui_menu_item(FL_S("Copy"));
        ui_menu_separator();
        if (ui_menu_item(FL_S("Paste"), .disabled = true).clicked) {
            st->disabled_clicked = true;
        }
        if (ui_menu_item(FL_S("Delete")).clicked) {
            st->delete_clicked = true;
        }
    }

    st->menu_open = ui_menu_is_open_impl(k_menu_id);
}

static void s4a_open(FlApp* app, MenuTestState* st, i32 x, i32 y) {
    fl_app_inject_mouse_move(app, x, y);
    fl_app_tick(app, st, static_kinds_frame);
    fl_app_tick(app, st, static_kinds_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Right, true);
    fl_app_tick(app, st, static_kinds_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Right, false);
    fl_app_tick(app, st, static_kinds_frame);
}

static void s4a_click(FlApp* app, MenuTestState* st, i32 x, i32 y) {
    fl_app_inject_mouse_move(app, x, y);
    fl_app_tick(app, st, static_kinds_frame);
    fl_app_tick(app, st, static_kinds_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, st, static_kinds_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    fl_app_tick(app, st, static_kinds_frame);
}

static void s4a_key_tap(FlApp* app, MenuTestState* st, FlKey key) {
    fl_app_inject_key(app, key, true);
    fl_app_tick(app, st, static_kinds_frame);
    fl_app_inject_key(app, key, false);
    fl_app_tick(app, st, static_kinds_frame);
}

static void s4a_type_char(FlApp* app, MenuTestState* st, FlString ch) {
    fl_app_inject_text(app, ch);
    fl_app_tick(app, st, static_kinds_frame);
}

UTEST(ui_menu, static_item_kinds) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menu_static"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    MenuTestState st = { 0 };
    fl_app_tick(app, &st, static_kinds_frame);

    s4a_open(app, &st, 100, 100);
    ASSERT_TRUE_MSG(st.menu_open, "right-click opened the static-kinds menu");
    UiMenuState* m = &st.ctx->shared->menu;

    i32 hx = 0, hy = 0, spx = 0, spy = 0;
    ASSERT_TRUE_MSG(menu_item_center(&st, FL_S("Edit"), 0, 0, &hx, &hy), "header row did not lay out");
    ASSERT_TRUE_MSG(menu_item_center(&st, k_menu_sep_name, 0, 3, &spx, &spy), "separator row did not lay out");

    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1,
                  "open highlights the first navigable item (Cut), skipping the header");

    s4a_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 2, "Down: Cut -> Copy");
    s4a_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 5, "Down: Copy -> Delete (separator + disabled Paste skipped)");
    s4a_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "Down wraps Delete -> Cut (past the header)");
    s4a_key_tap(app, &st, FlKey_Up);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 5, "Up wraps Cut -> Delete (past the header)");

    // 'e' matches only the non-navigable header "Edit". The separator records a zero-length label, so it is never a
    // match target.
    s4a_type_char(app, &st, FL_S("e"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 5, "type 'e' does not land on the non-navigable header");
    menu_typeahead_reset(&st); // 'c' is an independent jump, not an accumulated "ec" prefix
    s4a_type_char(app, &st, FL_S("c"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "type 'c' finds Cut, scanning past the skipped header");

    s4a_click(app, &st, spx, spy);
    ASSERT_TRUE_MSG(st.menu_open, "clicking a separator does not dismiss the menu");

    s4a_click(app, &st, hx, hy);
    ASSERT_TRUE_MSG(st.menu_open, "clicking a header does not dismiss the menu");

    i32 px = 0, py = 0;
    ASSERT_TRUE_MSG(menu_item_center(&st, FL_S("Paste"), 0, 4, &px, &py), "disabled Paste row did not lay out");
    s4a_click(app, &st, px, py);
    ASSERT_FALSE_MSG(st.disabled_clicked, "a disabled item never reports .clicked");
    ASSERT_TRUE_MSG(st.menu_open, "clicking a disabled item leaves the menu open");

    // Hand control to the keyboard (one Down), then walk to Delete (index 5) deterministically before confirming.
    s4a_key_tap(app, &st, FlKey_Down);
    while (m->levels[0].highlight_index != 5) {
        s4a_key_tap(app, &st, FlKey_Down);
    }
    s4a_key_tap(app, &st, FlKey_Enter);
    ASSERT_TRUE_MSG(st.delete_clicked, "Enter activates the highlighted Delete action");
    ASSERT_FALSE_MSG(st.menu_open, "activating Delete (default close policy) tore the menu down");

    fl_app_destroy(app);
}

// The stateful leaf kinds and their keep-open behavior, plus a confirmation that arbitrary custom content composes
// between items.
typedef struct MenuStatefulState {
    FlUiCtx* ctx;
    bool menu_open; // ui_menu_is_open snapshot AFTER the menu block on the last tick
    bool chk;
    bool chk_close;
    i32 radio_sel;
    bool custom_hit;
} MenuStatefulState;

static const FlLayoutId k_menu_sf_region_id = { .name = { .data = "sf_region", .length = 9, .is_static = 1 }, .id = 0 };

static const FlLayoutId k_menu_sf_id = { .name = { .data = "sf_menu", .length = 7, .is_static = 1 }, .id = 0 };

static void menu_stateful_frame(void* user_data) {
    MenuStatefulState* st = (MenuStatefulState*)user_data;
    st->ctx = ui_get_ctx();

    ui_menu_region(k_menu_sf_region_id, k_menu_sf_id) {}

    ui_menu(k_menu_sf_id, .min_width = 220) {
        ui_menu_check(FL_S("Chk"), &st->chk); // 0: keep-open checkable
        ui_menu_check(FL_S("ChkClose"), &st->chk_close, .close_policy = FlMenuClosePolicy_Close); // 1: forced to close
        ui_menu_radio(FL_S("R"), &st->radio_sel, 0);                                              // 2
        ui_menu_radio(FL_S("R"), &st->radio_sel, 1);                                              // 3
        ui_menu_radio(FL_S("R"), &st->radio_sel, 2);                                              // 4
        // Custom content between items: a plain button, no menu API.
        if (ui_button(FL_S("Custom")).clicked) {
            st->custom_hit = true;
        }
    }

    st->menu_open = ui_menu_is_open_impl(k_menu_sf_id);
}

// ui_menu_item and ui_button derive their ids the same way - name + index, index 0 for a button.
static bool menu_sf_center(MenuStatefulState* st, FlString label, int index, i32* out_cx, i32* out_cy) {
    u32 id = ui_layout_id_to_clay_id((FlLayoutId) { .name = label, .id = index });
    ItemState* item = hashmap_get(&st->ctx->shared->item_state, id);
    if (!item || item->state.size.x <= 0.0f) {
        return false;
    }
    *out_cx = (i32)(item->state.pos.x + item->state.size.x * 0.5f);
    *out_cy = (i32)(item->state.pos.y + item->state.size.y * 0.5f);
    return true;
}

static void menu_sf_open_at(FlApp* app, MenuStatefulState* st, i32 x, i32 y) {
    fl_app_inject_mouse_move(app, x, y);
    fl_app_tick(app, st, menu_stateful_frame);
    fl_app_tick(app, st, menu_stateful_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Right, true);
    fl_app_tick(app, st, menu_stateful_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Right, false);
    fl_app_tick(app, st, menu_stateful_frame);
}

static void menu_sf_click_at(FlApp* app, MenuStatefulState* st, i32 x, i32 y) {
    fl_app_inject_mouse_move(app, x, y);
    fl_app_tick(app, st, menu_stateful_frame);
    fl_app_tick(app, st, menu_stateful_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, st, menu_stateful_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    fl_app_tick(app, st, menu_stateful_frame);
}

static bool menu_sf_click_item(FlApp* app, MenuStatefulState* st, FlString label, int index) {
    i32 cx = 0, cy = 0;
    if (!menu_sf_center(st, label, index, &cx, &cy)) {
        return false;
    }
    menu_sf_click_at(app, st, cx, cy);
    return true;
}

UTEST(ui_menu, stateful_item_kinds) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menu_sf"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    MenuStatefulState st = { 0 };
    fl_app_tick(app, &st, menu_stateful_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");

    menu_sf_open_at(app, &st, 120, 120);
    ASSERT_TRUE_MSG(st.menu_open, "right-click opened the stateful menu");

    ASSERT_FALSE_MSG(st.chk, "checkable starts unchecked");
    ASSERT_TRUE_MSG(menu_sf_click_item(app, &st, FL_S("Chk"), 0), "checkable row did not lay out");
    ASSERT_TRUE_MSG(st.chk, "clicking the checkable toggled its bool* on");
    ASSERT_TRUE_MSG(st.menu_open, "a checkable keeps the menu open by default");
    ASSERT_TRUE_MSG(menu_sf_click_item(app, &st, FL_S("Chk"), 0), "checkable row did not lay out (2)");
    ASSERT_FALSE_MSG(st.chk, "clicking the checkable again toggled it back off");
    ASSERT_TRUE_MSG(st.menu_open, "the menu is still open after a second toggle");

    ASSERT_EQ_MSG(st.radio_sel, 0, "radio group starts on value 0");
    ASSERT_TRUE_MSG(menu_sf_click_item(app, &st, FL_S("R"), 3), "radio (value 1) did not lay out"); // value 1
    ASSERT_EQ_MSG(st.radio_sel, 1, "picking the second radio moved the shared selection to 1");
    ASSERT_TRUE_MSG(st.menu_open, "a radio item keeps the menu open by default");
    ASSERT_TRUE_MSG(menu_sf_click_item(app, &st, FL_S("R"), 4), "radio (value 2) did not lay out"); // value 2
    ASSERT_EQ_MSG(st.radio_sel, 2, "picking the third radio moved the shared selection to 2 (exclusive)");
    ASSERT_TRUE_MSG(st.menu_open, "still open after a second radio pick");

    // The custom button is registered in the menu's popup scope, so it is part of the menu surface.
    i32 bx = 0, by = 0;
    ASSERT_TRUE_MSG(menu_sf_center(&st, FL_S("Custom"), 0, &bx, &by),
                    "the custom button between items did not lay out");
    menu_sf_click_at(app, &st, bx, by);
    ASSERT_TRUE_MSG(st.custom_hit, "the custom button between items is hittable (reported .clicked)");
    ASSERT_TRUE_MSG(st.menu_open, "clicking custom content inside the menu does not dismiss the chain");

    ASSERT_TRUE_MSG(menu_sf_click_item(app, &st, FL_S("ChkClose"), 1), "force-close checkable did not lay out");
    ASSERT_TRUE_MSG(st.chk_close, "the force-close checkable still toggled its bool*");
    ASSERT_FALSE_MSG(st.menu_open, "FlMenuClosePolicy_Close forced the checkable to dismiss the menu");

    fl_app_destroy(app);
}

// The menu-bar shell reuses the menu core's builder, highlight and open-path while supplying its own open trigger:
// a click, not a hover-dwell.
typedef struct MenuBarState {
    FlUiCtx* ctx;
    bool open_clicked;
} MenuBarState;

static const FlLayoutId k_menubar_id = { .name = { .data = "appbar", .length = 6, .is_static = 1 }, .id = 0 };

static void menubar_frame(void* user_data) {
    MenuBarState* st = (MenuBarState*)user_data;
    st->ctx = ui_get_ctx();

    ui_menubar(k_menubar_id) {
        ui_menubar_menu(FL_S("File")) {
            ui_menu_item(FL_S("New"));
            if (ui_menu_item(FL_S("Open")).clicked) {
                st->open_clicked = true;
            }
        }
        ui_menubar_menu(FL_S("Edit")) {
            ui_menu_item(FL_S("Undo"));
            ui_menu_item(FL_S("Redo"));
        }
        ui_menubar_menu(FL_S("View")) {
            ui_menu_item(FL_S("Zoom"));
        }
    }
}

// Mirrors menu_emit_row's level-folded id derivation. Tops are level-0 items (File=0, Edit=1, View=2); dropdown items
// are level-1 items indexed within their dropdown.
static bool menubar_center(FlUiCtx* ctx, FlString label, int level, int index, i32* out_cx, i32* out_cy) {
    u32 id = ui_layout_id_to_clay_id((FlLayoutId) { .name = label, .id = ui_menu_row_id_seed(level, index) });
    ItemState* item = hashmap_get(&ctx->shared->item_state, id);
    if (!item || item->state.size.x <= 0.0f) {
        return false;
    }
    *out_cx = (i32)(item->state.pos.x + item->state.size.x * 0.5f);
    *out_cy = (i32)(item->state.pos.y + item->state.size.y * 0.5f);
    return true;
}

static void mb_ticks(FlApp* app, MenuBarState* st, int n) {
    for (int i = 0; i < n; i++) {
        fl_app_tick(app, st, menubar_frame);
    }
}

// Settles hover into hot state: the move feeds next_hot_id one frame, hot_id the next.
static void mb_hover(FlApp* app, MenuBarState* st, i32 x, i32 y) {
    fl_app_inject_mouse_move(app, x, y);
    mb_ticks(app, st, 3);
}

// Hovers first so hot state propagates before the click.
static void mb_click(FlApp* app, MenuBarState* st, i32 x, i32 y) {
    fl_app_inject_mouse_move(app, x, y);
    mb_ticks(app, st, 2);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    mb_ticks(app, st, 1);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    mb_ticks(app, st, 1);
}

static void mb_key_tap(FlApp* app, MenuBarState* st, FlKey key) {
    fl_app_inject_key(app, key, true);
    fl_app_tick(app, st, menubar_frame);
    fl_app_inject_key(app, key, false);
    fl_app_tick(app, st, menubar_frame);
}

static void mb_type_char(FlApp* app, MenuBarState* st, FlString ch) {
    fl_app_inject_text(app, ch);
    fl_app_tick(app, st, menubar_frame);
}

// The bar shares the one app-wide type-ahead accumulator; staling it makes the next character an independent jump.
static void mb_typeahead_reset(MenuBarState* st) {
    st->ctx->shared->menu.typeahead.deadline = 0.0;
}

UTEST(ui_menu, menubar) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menubar"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    MenuBarState st = { 0 };
    fl_app_tick(app, &st, menubar_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    ASSERT_FALSE_MSG(ui_menubar_is_open_impl(k_menubar_id), "the bar starts with no dropdown open");

    UiMenuState* m = &st.ctx->shared->menu;
    const u32 file_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("File"), .id = 0 });
    const u32 edit_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Edit"), .id = 1 });

    i32 fx = 0, fy = 0, ex = 0, ey = 0;
    ASSERT_TRUE_MSG(menubar_center(st.ctx, FL_S("File"), 0, 0, &fx, &fy), "File top was not laid out");
    ASSERT_TRUE_MSG(menubar_center(st.ctx, FL_S("Edit"), 0, 1, &ex, &ey), "Edit top was not laid out");

    mb_hover(app, &st, fx, fy);
    ASSERT_FALSE_MSG(ui_menubar_is_open_impl(k_menubar_id), "a cold hover over a top must not open it");

    mb_click(app, &st, fx, fy);
    ASSERT_TRUE_MSG(ui_menubar_is_open_impl(k_menubar_id), "clicking a top opens its menu");
    ASSERT_EQ_MSG((int)m->depth, 2, "an open dropdown is level 1 over the bar's level 0 (depth 2)");
    ASSERT_EQ_MSG((unsigned)m->levels[0].expanded_child_id, (unsigned)file_id, "File is the open root");

    i32 ox = 0, oy = 0;
    ASSERT_TRUE_MSG(menubar_center(st.ctx, FL_S("Open"), 1, 1, &ox, &oy), "File's Open item was not laid out");
    ASSERT_TRUE_MSG(oy > fy, "the dropdown opened below the bar button (element-anchored under the top)");

    mb_hover(app, &st, ex, ey);
    ASSERT_EQ_MSG((int)m->depth, 2, "switching root keeps a dropdown open (still depth 2)");
    ASSERT_EQ_MSG((unsigned)m->levels[0].expanded_child_id, (unsigned)edit_id,
                  "hovering Edit while open switched the open root to Edit");

    mb_key_tap(app, &st, FlKey_Escape);
    ASSERT_EQ_MSG((int)m->depth, 1, "ESC collapses the dropdown back to the bar (depth 1)");
    ASSERT_TRUE_MSG(ui_menubar_is_open_impl(k_menubar_id), "the bar stays focused after collapsing the dropdown");

    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "the bar-level highlight is on Edit after the hover-switch");
    mb_key_tap(app, &st, FlKey_Right);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 2, "Right moves the highlight Edit -> View");
    mb_key_tap(app, &st, FlKey_Left);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "Left moves the highlight View -> Edit");

    mb_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)m->depth, 2, "Down opens the highlighted top (descends to the dropdown)");
    ASSERT_EQ_MSG((unsigned)m->levels[0].expanded_child_id, (unsigned)edit_id,
                  "Down opened Edit (the highlighted top)");

    // Close the chain completely so the next open is a fresh click rather than a hover-switch onto an open neighbour.
    mb_key_tap(app, &st, FlKey_Escape); // dropdown -> focused strip
    mb_key_tap(app, &st, FlKey_Escape); // focused strip -> closed
    ASSERT_FALSE_MSG(ui_menubar_is_open_impl(k_menubar_id), "a second ESC closes the focused bar entirely");

    mb_click(app, &st, fx, fy);
    ASSERT_EQ_MSG((unsigned)m->levels[0].expanded_child_id, (unsigned)file_id, "clicking File re-opens it as the root");
    i32 nx = 0, ny = 0;
    ASSERT_TRUE_MSG(menubar_center(st.ctx, FL_S("Open"), 1, 1, &nx, &ny), "File's Open item re-laid out");
    mb_click(app, &st, nx, ny);
    ASSERT_TRUE_MSG(st.open_clicked, "activating a dropdown action item reports .clicked");
    ASSERT_FALSE_MSG(ui_menubar_is_open_impl(k_menubar_id), "an action item tore the whole bar chain down");

    fl_app_destroy(app);
}

static bool menu_row_bg(u32 id, UiColor4* out) {
    for (i32 i = 0, n = ui_get_render_command_count(); i < n; i++) {
        UiRenderCommand cmd = { 0 };
        if (ui_get_render_command(i, &cmd) && cmd.id == id && cmd.type == UiRenderCmd_Rectangle) {
            *out = cmd.bg_color;
            return true;
        }
    }
    return false;
}

// A disabled row tracks the pointer, but at a fraction of an available row's highlight strength.
UTEST(ui_menu, disabled_highlight_is_dimmer) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menu_dim"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    // menu_frame's items are Cut(0), Copy(1), Paste(2, disabled), Stay(3).
    MenuTestState st = { 0 };
    fl_app_tick(app, &st, menu_frame);
    menu_open_at(app, &st, 120, 120);
    ASSERT_TRUE_MSG(st.menu_open, "menu opened");

    const FlThemeColors* theme = ui_get_theme_colors(st.ctx);
    float resting = (float)theme->surface.elevated.r;
    float selected = (float)theme->surface.selected.r;
    ASSERT_NE_MSG((int)resting, (int)selected, "the theme must distinguish the resting and selected surfaces");

    UiColor4 live = { 0 }, dead = { 0 };
    ASSERT_TRUE_MSG(menu_hover_item(app, &st, FL_S("Copy"), 0, 1), "Copy was not laid out");
    ASSERT_TRUE_MSG(
        menu_row_bg(ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Copy"), .id = ui_menu_row_id_seed(0, 1) }),
                    &live),
        "the hovered available row painted no background");

    ASSERT_TRUE_MSG(menu_hover_item(app, &st, FL_S("Paste"), 0, 2), "Paste was not laid out");
    ASSERT_TRUE_MSG(
        menu_row_bg(ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Paste"), .id = ui_menu_row_id_seed(0, 2) }),
                    &dead),
        "the hovered disabled row painted no background");

    // Stated as a fraction of the available row's travel so it holds whichever direction the theme's selected
    // surface sits.
    float live_travel = live.r - resting;
    float dead_travel = dead.r - resting;
    ASSERT_TRUE_MSG(dead_travel != 0.0f, "a hovered disabled row is still visibly highlighted");
    ASSERT_TRUE_MSG(live_travel != 0.0f, "sanity: a hovered available row is highlighted");
    ASSERT_TRUE_MSG(dead_travel / live_travel > 0.0f && dead_travel / live_travel < 0.5f,
                    "a disabled row's highlight is well under an available row's");

    fl_app_destroy(app);
}

static void bar_over_content_frame(void* user_data) {
    MenuBarState* st = (MenuBarState*)user_data;
    st->ctx = ui_get_ctx();

    ui_menubar(k_menubar_id) {
        ui_menubar_menu(FL_S("File")) {
            if (ui_menu_item(FL_S("Open")).clicked) {
                st->open_clicked = true;
            }
        }
        ui_menubar_menu(FL_S("Edit")) {
            ui_menu_item(FL_S("Undo"));
        }
    }
    // Content drawn after the bar must NOT inherit the bar's popup scope, or a click on it reads as a click inside
    // the menu and the chain never dismisses.
    ui_button(FL_S("under_the_bar"));
}

// The two mouse ways out of an open menu bar: a click on app content outside the menu dismisses the whole chain, and
// re-pressing the open top releases it rather than collapsing to a focused strip that still owns input.
UTEST(ui_menu, menubar_dismissal) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("bar_dismiss"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    MenuBarState st = { 0 };
    fl_app_tick(app, &st, bar_over_content_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");

    i32 fx = 0, fy = 0;
    ASSERT_TRUE_MSG(menubar_center(st.ctx, FL_S("File"), 0, 0, &fx, &fy), "File top was not laid out");
    // Membership in the bar's popup scope is exactly what the click-outside test reads, so this asserts on the scope
    // rather than on a click, which would need the content placed clear of the dropdown's rect.
    mb_click(app, &st, fx, fy);
    ASSERT_TRUE_MSG(ui_menubar_is_open_impl(k_menubar_id), "clicking the File top opened its dropdown");

    fl_app_tick(app, &st, bar_over_content_frame); // settle: the opening frame re-lays the content out
    u32 bar_scope = ui_layout_id_to_clay_id(k_menubar_id);
    ItemState* under = hashmap_get(&st.ctx->shared->item_state,
                                   ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("under_the_bar"), .id = 0 }));
    ASSERT_TRUE_MSG(under != nullptr, "the content button was not registered");
    ASSERT_NE_MSG((unsigned)under->popup_scope_id, (unsigned)bar_scope,
                  "content drawn after the bar must not inherit its popup scope");

    mb_click(app, &st, fx, fy);
    ASSERT_FALSE_MSG(ui_menubar_is_open_impl(k_menubar_id), "re-pressing the open top releases the whole chain");
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.depth, 0, "a released bar owns no levels");

    fl_app_destroy(app);
}

// A menu bar on screen next to a popup menu opened from app content, emitted in either order within the frame.
typedef struct BarAndPopupState {
    FlUiCtx* ctx;
    bool popup_first;
    bool open_requested;
    int clicked; // index into k_popup_labels of the item that reported .clicked, -1 for none
} BarAndPopupState;

static const FlLayoutId k_popup_id = { .name = { .data = "addmenu", .length = 7, .is_static = 1 }, .id = 0 };

static const char* const k_popup_labels[] = { "Oscillator", "Sample", "Noise", "Filter" };

static void bar_and_popup_bar(void) {
    ui_menubar(k_menubar_id) {
        ui_menubar_menu(FL_S("File")) {
            ui_menu_item(FL_S("New"));
        }
        ui_menubar_menu(FL_S("Edit")) {
            ui_menu_item(FL_S("Undo"));
        }
        ui_menubar_menu(FL_S("View")) {
            ui_menu_item(FL_S("Zoom"));
        }
        ui_menubar_menu(FL_S("Navigate")) {
            ui_menu_item(FL_S("Next"));
        }
    }
}

static void bar_and_popup_menu(BarAndPopupState* st) {
    if (st->open_requested) {
        st->open_requested = false;
        ui_menu_open_impl(k_popup_id, (FlVec2) { 100.0f, 200.0f });
    }
    ui_menu(k_popup_id, .min_width = 200) {
        // The header takes row 0, so item i sits at row i + 1 - the same index as the bar top it used to alias.
        ui_menu_header(FL_S("Generators"));
        for (int i = 0; i < 4; i++) {
            FlString label = string_from_cstr_len(k_popup_labels[i], strlen(k_popup_labels[i]));
            if (ui_menu_item(label).clicked) {
                st->clicked = i;
            }
        }
    }
}

static void bar_and_popup_frame(void* user_data) {
    BarAndPopupState* st = (BarAndPopupState*)user_data;
    st->ctx = ui_get_ctx();
    if (st->popup_first) {
        bar_and_popup_menu(st);
        bar_and_popup_bar();
    } else {
        bar_and_popup_bar();
        bar_and_popup_menu(st);
    }
}

static void bp_ticks(FlApp* app, BarAndPopupState* st, int n) {
    for (int i = 0; i < n; i++) {
        fl_app_tick(app, st, bar_and_popup_frame);
    }
}

static void bp_click(FlApp* app, BarAndPopupState* st, i32 x, i32 y) {
    fl_app_inject_mouse_move(app, x, y);
    bp_ticks(app, st, 2);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    bp_ticks(app, st, 1);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    bp_ticks(app, st, 1);
}

// A passive menu bar keeps its tops out of the popup's level-0 rows: clicking popup row i must activate that row, not
// the bar top that shares index i, and must leave the bar closed. The bar still opens normally once the popup is gone.
UTEST(ui_menu, menubar_beside_popup) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    for (int order = 0; order < 2; order++) {
        FlAppSettings settings = { .window_title = FL_S("bar_popup"), .width = 800, .height = 600, .headless = true };
        FlApp* app = fl_app_create(&settings);
        ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

        BarAndPopupState st = { .popup_first = order == 1, .clicked = -1 };
        bp_ticks(app, &st, 1);
        ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
        UiMenuState* m = &st.ctx->shared->menu;

        st.open_requested = true;
        bp_ticks(app, &st, 2);
        ASSERT_TRUE_MSG(m->active && m->owner_scope_id == ui_layout_id_to_clay_id(k_popup_id), "the popup opened");
        ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1,
                      "the popup's first navigable row (after its header) claims the initial highlight");

        i32 fx = 0, fy = 0;
        ASSERT_TRUE_MSG(menubar_center(st.ctx, FL_S("Filter"), 0, 4, &fx, &fy), "Filter row was not laid out");
        bp_click(app, &st, fx, fy);
        ASSERT_EQ_MSG(st.clicked, 3, "clicking the popup's Filter row reports Filter as clicked");
        ASSERT_FALSE_MSG(ui_menubar_is_open_impl(k_menubar_id), "a click inside the popup leaves the bar closed");
        ASSERT_FALSE_MSG(m->active, "the action item closed the popup");

        i32 tx = 0, ty = 0;
        ASSERT_TRUE_MSG(menubar_center(st.ctx, FL_S("File"), 0, 0, &tx, &ty), "File top was not laid out");
        bp_click(app, &st, tx, ty);
        ASSERT_TRUE_MSG(ui_menubar_is_open_impl(k_menubar_id), "the bar opens on a click after the popup closed");
        ASSERT_EQ_MSG((unsigned)m->levels[0].expanded_child_id,
                      (unsigned)ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("File"), .id = 0 }),
                      "File is the open root");

        fl_app_destroy(app);
    }
}

// A dropdown whose widest row carries both a long label and a long shortcut, so the row exceeds the panel's minimum
// width.
static void widerow_frame(void* user_data) {
    MenuBarState* st = (MenuBarState*)user_data;
    st->ctx = ui_get_ctx();

    ui_menubar(k_menubar_id) {
        ui_menubar_menu(FL_S("Edit")) {
            ui_menu_item(FL_S("Cut"), .shortcut = FL_S("Ctrl X"));
            if (ui_menu_item(FL_S("Duplicate Time"), .shortcut = FL_S("Ctrl Shift D")).clicked) {
                st->open_clicked = true;
            }
        }
    }
}

// A menu panel takes its min_width as a MINIMUM, not as a fixed width, so a row wider than it widens the panel
// instead of drawing its trailing shortcut outside the painted surface. Run at a HiDPI content scale so the
// min_width -> physical multiply is covered too.
UTEST(ui_menu, panel_fits_wide_rows) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("widerow"), .width = 1200, .height = 900, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    fl_app_set_window_content_scale_for(app, fl_app_main_window(app), 1.5f);

    MenuBarState st = { 0 };
    for (int i = 0; i < 2; i++) {
        fl_app_tick(app, &st, widerow_frame);
    }
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");

    i32 ex = 0, ey = 0;
    ASSERT_TRUE_MSG(menubar_center(st.ctx, FL_S("Edit"), 0, 0, &ex, &ey), "Edit top was not laid out");
    fl_app_inject_mouse_move(app, ex, ey);
    for (int i = 0; i < 2; i++) {
        fl_app_tick(app, &st, widerow_frame);
    }
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, &st, widerow_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    fl_app_tick(app, &st, widerow_frame);
    ASSERT_TRUE_MSG(ui_menubar_is_open_impl(k_menubar_id), "clicking the Edit top opened its dropdown");

    // The dropdown's floating panel: ui_popup_begin keys it by (anchor label, the bar's popup scope id).
    u32 bar_scope = ui_layout_id_to_clay_id(k_menubar_id);
    u32 panel_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Edit"), .id = bar_scope });
    Clay_ElementData panel = Clay_GetElementData((Clay_ElementId) { .id = panel_id });
    ASSERT_TRUE_MSG(panel.found, "the dropdown panel was not laid out");

    u32 row_id
        = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Duplicate Time"), .id = ui_menu_row_id_seed(1, 1) });
    ItemState* row = hashmap_get(&st.ctx->shared->item_state, row_id);
    ASSERT_TRUE_MSG(row != nullptr && row->state.size.x > 0.0f, "the wide row was not laid out");

    ASSERT_TRUE_MSG(row->state.pos.x >= panel.boundingBox.x
                        && row->state.pos.x + row->state.size.x <= panel.boundingBox.x + panel.boundingBox.width + 0.5f,
                    "a row wider than min_width must widen the panel, not overflow it");

    i32 cx = (i32)(row->state.pos.x + row->state.size.x * 0.5f);
    i32 cy = (i32)(row->state.pos.y + row->state.size.y * 0.5f);
    fl_app_inject_mouse_move(app, cx, cy);
    for (int i = 0; i < 3; i++) {
        fl_app_tick(app, &st, widerow_frame);
    }
    ASSERT_EQ_MSG((int)st.ctx->shared->menu.levels[1].highlight_index, 1, "hovering the wide row highlights it");
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, &st, widerow_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    fl_app_tick(app, &st, widerow_frame);
    ASSERT_TRUE_MSG(st.open_clicked, "clicking the wide row dispatches its action");

    fl_app_destroy(app);
}

// The menu-bar surface routes type-ahead through the same deepest-level path as the context menu, within an open
// dropdown and across the bar's own tops.
UTEST(ui_menu, type_ahead_menubar) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menubar_type"), .width = 800, .height = 600, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    MenuBarState st = { 0 };
    fl_app_tick(app, &st, menubar_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    UiMenuState* m = &st.ctx->shared->menu;

    // Tops File(0) Edit(1) View(2); File's dropdown is New(0) Open(1).
    i32 fx = 0, fy = 0;
    ASSERT_TRUE_MSG(menubar_center(st.ctx, FL_S("File"), 0, 0, &fx, &fy), "File top was not laid out");
    mb_click(app, &st, fx, fy);
    ASSERT_TRUE_MSG(ui_menubar_is_open_impl(k_menubar_id), "clicking File opened its dropdown");
    ASSERT_EQ_MSG((int)m->depth, 2, "an open dropdown is depth 2");

    // Each is an independent single-letter jump, so stale the prefix between them.
    mb_type_char(app, &st, FL_S("o"));
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 1, "dropdown type 'o': -> Open");
    mb_typeahead_reset(&st);
    mb_type_char(app, &st, FL_S("n"));
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, 0, "dropdown type 'n': -> New");

    // With the dropdown collapsed, type-ahead runs across the bar's own tops, so their labels are recorded even while
    // their dropdowns are closed. The strip highlight was on File.
    mb_key_tap(app, &st, FlKey_Escape);
    ASSERT_EQ_MSG((int)m->depth, 1, "ESC collapsed the dropdown to the focused strip");
    mb_typeahead_reset(&st);
    mb_type_char(app, &st, FL_S("v"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 2, "bar-strip type 'v': File -> View top");
    mb_typeahead_reset(&st);
    mb_type_char(app, &st, FL_S("e"));
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 1, "bar-strip type 'e': View -> Edit top (wrapped)");

    fl_app_destroy(app);
}

// ── Menu: scrollable over-tall levels ───────────────────────────────────────────────────────────────────────────
// A menu level whose natural item list is taller than the viewport is height-capped and scrolls: the mouse wheel over
// the hovered level, and keyboard scroll-into-view when the highlight moves past the visible edge. The state asserted
// is the per-level scroll_offset the panel's clip rides on; a level that fits keeps offset 0.

#define MENU_TALL_COUNT 15

// Distinct, stable labels for the root leaf rows and the submenu rows (FL_S_() gives the brace-init form for an array).
// Multi-char names keep the derived widget ids clear of the submenu panel id (which is hashed from the parent row id).
static const FlString k_tall_root[MENU_TALL_COUNT] = {
    FL_S_("root_00"), FL_S_("root_01"), FL_S_("root_02"), FL_S_("root_03"), FL_S_("root_04"),
    FL_S_("root_05"), FL_S_("root_06"), FL_S_("root_07"), FL_S_("root_08"), FL_S_("root_09"),
    FL_S_("root_10"), FL_S_("root_11"), FL_S_("root_12"), FL_S_("root_13"), FL_S_("root_14"),
};

static const FlString k_tall_sub[MENU_TALL_COUNT] = {
    FL_S_("sub_00"), FL_S_("sub_01"), FL_S_("sub_02"), FL_S_("sub_03"), FL_S_("sub_04"),
    FL_S_("sub_05"), FL_S_("sub_06"), FL_S_("sub_07"), FL_S_("sub_08"), FL_S_("sub_09"),
    FL_S_("sub_10"), FL_S_("sub_11"), FL_S_("sub_12"), FL_S_("sub_13"), FL_S_("sub_14"),
};

// An over-tall submenu parent ("More") at index 0, then MENU_TALL_COUNT leaf rows (indices 1..MENU_TALL_COUNT). Both
// the root level and the submenu overflow the short test window, so each is scrollable.
static void menu_tall_frame(void* user_data) {
    MenuTestState* st = (MenuTestState*)user_data;
    st->ctx = ui_get_ctx();

    ui_menu_region(k_menu_region_id, k_menu_id) {}

    ui_menu(k_menu_id, .min_width = 140) {
        ui_submenu(FL_S("More")) {
            for (int i = 0; i < MENU_TALL_COUNT; i++) {
                ui_menu_item(k_tall_sub[i]);
            }
        }
        for (int i = 0; i < MENU_TALL_COUNT; i++) {
            ui_menu_item(k_tall_root[i]);
        }
    }

    st->menu_open = ui_menu_is_open_impl(k_menu_id);
}

// A right-click opens the chain without dwelling a submenu (dwell needs a stationary hover, which this does not do).
static void menu_tall_open(FlApp* app, MenuTestState* st) {
    fl_app_inject_mouse_move(app, 20, 20);
    fl_app_tick(app, st, menu_tall_frame);
    fl_app_tick(app, st, menu_tall_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Right, true);
    fl_app_tick(app, st, menu_tall_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Right, false);
    fl_app_tick(app, st, menu_tall_frame);
}

static void menu_tall_key_tap(FlApp* app, MenuTestState* st, FlKey key) {
    fl_app_inject_key(app, key, true);
    fl_app_tick(app, st, menu_tall_frame);
    fl_app_inject_key(app, key, false);
    fl_app_tick(app, st, menu_tall_frame);
}

// Settle the pointer over a laid-out row so the wheel (and Clay's scroll-target arbitration) targets that level.
static bool menu_tall_hover(FlApp* app, MenuTestState* st, FlString label, int level, int index) {
    i32 cx = 0, cy = 0;
    if (!menu_item_center(st, label, level, index, &cx, &cy)) {
        return false;
    }
    fl_app_inject_mouse_move(app, cx, cy);
    fl_app_tick(app, st, menu_tall_frame);
    fl_app_tick(app, st, menu_tall_frame);
    fl_app_tick(app, st, menu_tall_frame);
    return true;
}

// Inject one wheel delta and let it land (observed on the following tick). Positive dy scrolls up.
static void menu_tall_wheel(FlApp* app, MenuTestState* st, f32 dy) {
    fl_app_inject_wheel(app, 0.0f, dy);
    fl_app_tick(app, st, menu_tall_frame);
}

// The mouse wheel over an over-tall level scrolls that level's contents and clamps at both ends.
UTEST(ui_menu, overtall_wheel) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    // A deliberately short window so the ~16-row menu overflows and the root level becomes scrollable.
    FlAppSettings settings = { .window_title = FL_S("menu_tall_wheel"), .width = 400, .height = 240, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    MenuTestState st = { 0 };
    fl_app_tick(app, &st, menu_tall_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    UiMenuState* m = &st.ctx->shared->menu;

    menu_tall_open(app, &st);
    ASSERT_TRUE_MSG(st.menu_open, "tall menu opened");
    ASSERT_EQ_MSG(0.0, (double)m->levels[0].scroll_offset, "a freshly opened level starts scrolled to the top");

    // natural_height is last frame's recorded total.
    ASSERT_GT_MSG((double)st.ctx->menu_frame.levels[0].natural_height, (double)st.ctx->layout_height,
                  "the root item list is taller than the whole viewport (scroll must engage)");

    // Hover a row so the wheel targets this level.
    ASSERT_TRUE_MSG(menu_tall_hover(app, &st, FL_S("More"), 0, 0), "More row was not laid out");
    menu_tall_wheel(app, &st, -3.0f);
    double after_one = (double)m->levels[0].scroll_offset;
    ASSERT_GT_MSG(after_one, 0.0, "wheel down scrolls the hovered level (offset grows from the top)");

    for (int i = 0; i < 40; i++) {
        menu_tall_wheel(app, &st, -5.0f);
    }
    double saturated = (double)m->levels[0].scroll_offset;
    ASSERT_GT_MSG(saturated, after_one, "sustained wheel-down scrolls further than a single notch");
    menu_tall_wheel(app, &st, -5.0f);
    ASSERT_EQ_MSG(saturated, (double)m->levels[0].scroll_offset, "offset clamps at the bottom (no over-scroll)");

    for (int i = 0; i < 60; i++) {
        menu_tall_wheel(app, &st, 5.0f);
    }
    ASSERT_EQ_MSG(0.0, (double)m->levels[0].scroll_offset, "wheel up returns to the top and clamps at 0");

    // The wheel scrolls the hovered level only, leaving the still-open (also over-tall) parent at the top.
    ASSERT_TRUE_MSG(menu_tall_hover(app, &st, FL_S("More"), 0, 0), "More row for the descend was not laid out");
    menu_tall_key_tap(app, &st, FlKey_Right); // highlight is on More → descend into its submenu (level 1)
    ASSERT_EQ_MSG((int)m->depth, 2, "descended into the More submenu for the wheel-isolation check");
    ASSERT_TRUE_MSG(menu_tall_hover(app, &st, k_tall_sub[0], 1, 0), "first submenu row was not laid out");
    menu_tall_wheel(app, &st, -4.0f);
    ASSERT_GT_MSG((double)m->levels[1].scroll_offset, 0.0, "wheel over the submenu scrolls the submenu level");
    ASSERT_EQ_MSG(0.0, (double)m->levels[0].scroll_offset, "wheel over the submenu leaves the parent root untouched");

    fl_app_destroy(app);
}

// Arrow-key navigation past the visible edge scrolls the highlight into view in BOTH directions, and an over-tall
// submenu scrolls independently of its (also over-tall) parent level.
UTEST(ui_menu, overtall_keyboard) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("menu_tall_kbd"), .width = 400, .height = 240, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    MenuTestState st = { 0 };
    fl_app_tick(app, &st, menu_tall_frame);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    UiMenuState* m = &st.ctx->shared->menu;

    menu_tall_open(app, &st);
    ASSERT_TRUE_MSG(st.menu_open, "tall menu opened");
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 0, "open highlights the first row (the More submenu parent)");
    ASSERT_EQ_MSG(0.0, (double)m->levels[0].scroll_offset, "root starts at the top");

    menu_tall_key_tap(app, &st, FlKey_Up);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, MENU_TALL_COUNT, "Up wraps to the last row");
    ASSERT_GT_MSG((double)m->levels[0].scroll_offset, 0.0, "wrapping to the last row scrolls the highlight into view");

    menu_tall_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((int)m->levels[0].highlight_index, 0, "Down wraps back to the first row");
    ASSERT_EQ_MSG(0.0, (double)m->levels[0].scroll_offset,
                  "wrapping back to the first row scrolls the root to the top");

    // --- Per-level independence ---
    // The highlight sits on "More" (index 0). Right descends into its submenu (level 1).
    menu_tall_key_tap(app, &st, FlKey_Right);
    ASSERT_EQ_MSG((int)m->depth, 2, "Right descends into the More submenu (depth 2)");
    // A couple of ticks so the just-opened submenu records its geometry before we navigate it.
    fl_app_tick(app, &st, menu_tall_frame);
    fl_app_tick(app, &st, menu_tall_frame);
    ASSERT_EQ_MSG(0.0, (double)m->levels[1].scroll_offset, "the freshly opened submenu starts at the top");

    menu_tall_key_tap(app, &st, FlKey_Up);
    ASSERT_EQ_MSG((int)m->levels[1].highlight_index, MENU_TALL_COUNT - 1, "Up wraps to the submenu's last row");
    ASSERT_GT_MSG((double)m->levels[1].scroll_offset, 0.0, "the submenu scrolls to reveal its last row");
    ASSERT_EQ_MSG(0.0, (double)m->levels[0].scroll_offset, "scrolling the submenu leaves the PARENT level untouched");

    menu_tall_key_tap(app, &st, FlKey_Left);
    ASSERT_EQ_MSG((int)m->depth, 1, "Left ascends back to the root");
    ASSERT_EQ_MSG(0.0, (double)m->levels[1].scroll_offset, "collapsing the submenu resets its scroll offset");
    menu_tall_key_tap(app, &st, FlKey_Right);
    ASSERT_EQ_MSG((int)m->depth, 2, "re-descended into the submenu");
    ASSERT_EQ_MSG(0.0, (double)m->levels[1].scroll_offset, "a reopened level starts at the top, not mid-scroll");

    fl_app_destroy(app);
}

// The progress bar is pure paint: a track plus a fill sized to a normalized fraction in [0,1]. The widget has no id,
// so each rect is identified by its theme color (track = surface.elevated, fill = interactive.base); the fill width
// as a fraction of the track must equal the requested fraction, and out-of-range input clamps rather than wraps.

// Distinctive track/fill colors so each progress rect is identifiable by color alone in the snapshot.
static const FlColor PROGRESS_TRACK_COLOR = { 40, 40, 44, 255 };

static const FlColor PROGRESS_FILL_COLOR = { 10, 200, 80, 255 };

typedef struct ProgressFrameState {
    float fraction;
} ProgressFrameState;

static void progress_frame(void* user_data) {
    ProgressFrameState* st = (ProgressFrameState*)user_data;
    static const FlThemeColors theme = {
        .surface.elevated = { 40, 40, 44, 255 },  // track slot (PROGRESS_TRACK_COLOR)
        .interactive.base = { 10, 200, 80, 255 }, // fill slot (PROGRESS_FILL_COLOR)
        .text.primary = { 240, 240, 240, 255 },
    };
    ui_set_theme(ui_get_ctx(), &theme);
    // A padded full-window column holding only the bar, so the track grows to a single determinate width.
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }, .padding = padding_all(20)) {
        ui_progress_bar(st->fraction);
    }
}

// Widest rectangle painted in want, or -1 if none was emitted.
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Two nested Scroll Targets register via ui_scroll_target(): the wheel goes only to the inner (hot) one and the
// outer reads {0,0} (consume-takes-all). A pure Scroll Target with no Clickable/Draggable still reports wheel (the
// read gates on item->state.hot, never signal.hovered), and a press over it still claims active_id.

UTEST(wheel_input, inner_shadows_outer) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    // Drive an app-owned context, not the shared render fixture: fl_app_create()/ui_create() set the global UI
    // context fresh (an earlier fl_app teardown nulls the global). Headless inject_wheel is deferred, so the wheel
    // cache is set directly and ui_update_begin/end pumped here rather than through fl_app_tick, which would reset
    // the wheel each frame.
    FlAppSettings settings = { .window_title = FL_S("wheel_input"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = ui_get_ctx();
    ASSERT_TRUE_MSG(ctx != nullptr, "fl_app_create did not establish a global UI context");

    const int layout_w = 400;
    const int layout_h = 400;
    ctx->ui_scale = 1.0f; // sizing_fixed() multiplies by ui_scale; keep logical == physical for exact rects.
    ctx->layout_width = (float)layout_w;
    ctx->layout_height = (float)layout_h;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = (float)layout_w, .height = (float)layout_h });

    // outer is the full 400x400 area; inner is a 200x200 child at its top-left. (100,100) lands inside both, so
    // both are under the cursor and the deeper (inner, rendered last) wins arbitration.
    const FlColor outer_color = ui_color(40, 40, 60, 255);
    const FlColor inner_color = ui_color(60, 90, 60, 255);

    // A pure Scroll Target carries NO Clickable/Draggable - the only signal it gets is the wheel, via state.hot.
    const u32 inner_clay_id = ui_layout_id_to_clay_id(ID("wheel_inner"));
    const u32 outer_clay_id = ui_layout_id_to_clay_id(ID("wheel_outer"));

    // Frame 1: cursor over inner, no wheel yet. Registers both targets and lays them out; hot_id resolves to
    // inner at end-of-frame but state.hot does not flip until the next frame's update.
    ctx->input_state.mouse.x = 100.0f;
    ctx->input_state.mouse.y = 100.0f;
    ctx->input_state.mouse.wheel_x_cached = 0.0f;
    ctx->input_state.mouse.wheel_y_cached = 0.0f;
    ctx->mouse_pressed_this_frame = false;
    ctx->mouse_released_this_frame = false;

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

    ASSERT_EQ_MSG(inner_clay_id, ctx->hot_id, "after frame 1 the inner Scroll Target must be the hot id");

    // Frame 2: inject a downward wheel tick. ui_update_begin sees the previous frame's hot id is a ScrollTarget,
    // so it withholds the wheel from Clay's scroll containers and leaves the cache intact for the scoped read.
    ctx->input_state.mouse.wheel_y_cached = 120.0f;

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

    FlVec2 inner_wheel = ui_mouse_wheel_delta(ID("wheel_inner"));
    FlVec2 outer_wheel = ui_mouse_wheel_delta(ID("wheel_outer"));
    ASSERT_NEAR_MSG(120.0, (double)inner_wheel.y, 0.001, "inner (hot) Scroll Target must read the wheel delta");
    ASSERT_NEAR_MSG(0.0, (double)inner_wheel.x, 0.001, "no horizontal wheel was injected");
    ASSERT_NEAR_MSG(0.0, (double)outer_wheel.y, 0.001, "outer Scroll Target is shadowed by the inner — reads {0,0}");
    ASSERT_NEAR_MSG(0.0, (double)outer_wheel.x, 0.001, "outer Scroll Target is shadowed by the inner — reads {0,0}");

    // The read does NOT depend on signal.hovered: this element never declared Clickable or Draggable, so
    // signal.hovered is always false for it, yet the scoped read returned the wheel.
    ItemState* inner_item = hashmap_get(&ctx->shared->item_state, inner_clay_id);
    ASSERT_TRUE_MSG(inner_item != nullptr, "inner Scroll Target must be present in item_state");
    ASSERT_TRUE_MSG(inner_item->state.hot, "the scoped read keys off item->state.hot, which must be set");
    ASSERT_FALSE_MSG(inner_item->last_signal.hovered,
                     "a pure Scroll Target never sets signal.hovered, yet still reports wheel via the scoped read");

    // A press over a pure Scroll Target still claims active_id, because the generic hot->active promotion in
    // ui_update_end keys off the flag-agnostic hot_id, not any Clickable behavior.
    ctx->mouse_pressed_this_frame = true;
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
    ASSERT_EQ_MSG(inner_clay_id, ctx->active_id, "a press over a pure Scroll Target still sets active_id");
    (void)outer_clay_id;

    fl_app_destroy(app);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// An unflagged plain layout region must keep an unconsumed wheel for the frame: after a frame over a plain region
// that never declares itself a Scroll Target, the injected wheel still surfaces via the cache.

UTEST(wheel_input, unflagged_region_keeps_unconsumed_wheel) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("wheel_unflagged"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = ui_get_ctx();
    ASSERT_TRUE_MSG(ctx != nullptr, "fl_app_create did not establish a global UI context");

    ctx->ui_scale = 1.0f;
    ctx->layout_width = 400.0f;
    ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });

    // Cursor over a plain layout area that declares no Scroll Target and no clip - nothing consumes the wheel.
    ctx->input_state.mouse.x = 100.0f;
    ctx->input_state.mouse.y = 100.0f;
    ctx->input_state.mouse.wheel_x_cached = 0.0f;
    ctx->input_state.mouse.wheel_y_cached = 120.0f;
    ctx->mouse_pressed_this_frame = false;
    ctx->mouse_released_this_frame = false;

    // hot_id is not a Scroll Target, so ui_update_begin takes the Clay branch; the cache must survive it untouched.
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_fixed(400), .height = sizing_fixed(400) },
                   .color = ui_color(30, 30, 40, 255)) {}
    ui_update_end(ctx, 1.0f / 60.0f);

    // Observed via the unscoped ui_get_mouse_wheel() / cached fields, NOT mouse_wheel_delta(id) - the scoped query
    // self-gates to a hot Scroll Target and would read zero for this unflagged region.
    FlVec2 wheel = ui_get_mouse_wheel();
    ASSERT_NEAR_MSG(120.0, (double)wheel.y, 0.001, "an unconsumed wheel over a plain region survives the frame");
    ASSERT_NEAR_MSG(0.0, (double)wheel.x, 0.001, "no horizontal wheel was injected");
    ASSERT_NEAR_MSG(120.0, (double)ctx->input_state.mouse.wheel_y_cached, 0.001,
                    "the wheel cache itself is left intact by ui_update_begin");

    fl_app_destroy(app);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A Clay-managed scroll container with a nested Scroll Target widget (the slider carries
// Clickable|Draggable|ScrollTarget) must route the wheel to exactly ONE arbiter, never both. When the widget is hot,
// ui_update_begin withholds the wheel from Clay and the widget reads it; the container must not ALSO scroll.

// A 400x400 vertical clip container whose content is taller than it (a 200x40 slider at the top plus an 800px
// filler), so Clay CAN scroll it. The slider is the deeper element under the cursor, so it wins hot arbitration.
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
    FlUiCtx* ctx = ui_get_ctx();
    ASSERT_TRUE_MSG(ctx != nullptr, "fl_app_create did not establish a global UI context");

    ctx->ui_scale = 1.0f;
    ctx->layout_width = 400.0f;
    ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });

    const u32 container_clay_id = ui_layout_id_to_clay_id(ID("wheel_scroll_box"));
    const u32 slider_clay_id = ui_layout_id_to_clay_id(ID("wheel_slider"));

    // Cursor at (100,20) lands on the slider. No wheel yet.
    ctx->input_state.mouse.x = 100.0f;
    ctx->input_state.mouse.y = 20.0f;
    ctx->input_state.mouse.wheel_x_cached = 0.0f;
    ctx->input_state.mouse.wheel_y_cached = 0.0f;
    ctx->mouse_pressed_this_frame = false;
    ctx->mouse_released_this_frame = false;

    // Frames 1-2 settle the hot state. hot_id resolves to the slider at the end of frame 1, but signal.hovered keys
    // off item->state.hot, which item_state_update fills from the PRIOR frame's hot_id - so it only flips true on
    // frame 3. Two warm-up frames let the signal.wheel_delta path (the actual double-read vector) engage.
    wheel_dbl_frame(ctx);
    ASSERT_EQ_MSG(slider_clay_id, ctx->hot_id, "after frame 1 the nested Scroll Target slider must be the hot id");
    wheel_dbl_frame(ctx);

    // Frame 3: inject a downward wheel. hot_id is the slider (a Scroll Target), so ui_update_begin gives Clay a zero
    // delta and leaves the cache for the widget. The slider is now hovered, so it reads via signal.wheel_delta.
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Click-vs-drag threshold (FlItem_DragThreshold): an item that opts into the flag only starts dragging once the
// cursor has moved a few pixels from the press point, so a press-and-release in place reads as a click rather than
// a one-pixel drag. These cases drive the real input layer (fl_platform_input_update) every frame, so signal.dragging
// is gated by the genuine drag-distance tracking - not a value poked straight into the context.

typedef struct DragGesture {
    FlSignal move;    // signal captured on the frame the cursor moved while the button was held
    FlSignal release; // signal captured on the release frame
} DragGesture;

static void drag_threshold_frame(FlUiCtx* ctx, float x, float y, bool held, bool pressed, bool released,
                                 FlSignal* out_sig) {
    FlInputState* in = &ctx->input_state;
    fl_platform_input_set_mouse_position(ctx, x, y);
    in->mouse.buttons[FlMouseButton_Left] = held;
    in->mouse.buttons_pressed[FlMouseButton_Left] = pressed; // fl_platform_input_update consumes + clears this edge
    in->mouse.buttons_released[FlMouseButton_Left] = released;
    fl_platform_input_update(ctx, 1.0f / 60.0f);

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

// Press at the item centre (100,100), move by move_dx while held, then release.
static DragGesture drag_threshold_run(FlUiCtx* ctx, float move_dx) {
    // Two warm-up frames: the item becomes hot_id at the end of frame 1 and its state.hot flips on frame 2. A press
    // needs signal.hovered, which keys off state.hot.
    drag_threshold_frame(ctx, 100.0f, 100.0f, false, false, false, nullptr);
    drag_threshold_frame(ctx, 100.0f, 100.0f, false, false, false, nullptr);
    // Press in place. active_id latches at end-of-frame, so dragging cannot fire on the press frame itself.
    drag_threshold_frame(ctx, 100.0f, 100.0f, true, true, false, nullptr);

    DragGesture g = { 0 };
    drag_threshold_frame(ctx, 100.0f + move_dx, 100.0f, true, false, false, &g.move);
    drag_threshold_frame(ctx, 100.0f + move_dx, 100.0f, false, false, true, &g.release);
    return g;
}

// Both scenarios share one app (a single fl_app per case, like wheel_input) and the same item id; each gesture's
// warm-up frames re-establish hot state and the press resets the drag-distance tracking, so they don't bleed.
UTEST(drag_threshold, click_vs_drag) {
    FlAppSettings settings = { .window_title = FL_S("drag_threshold"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = ui_get_ctx();
    ctx->ui_scale = 1.0f; // sizing_fixed() multiplies by ui_scale; keep logical == physical so the item lands at 0,0.
    ctx->layout_width = 400.0f;
    ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });

    // 2px of travel - under UI_MOUSE_DRAG_THRESHOLD (4px): the gesture stays a click.
    DragGesture click = drag_threshold_run(ctx, 2.0f);
    ASSERT_FALSE_MSG(click.move.dragging, "a sub-threshold move must not latch dragging");
    ASSERT_NEAR_MSG(0.0, (double)click.move.drag_delta.x, 0.001, "no drag means no drag_delta");
    ASSERT_FALSE_MSG(click.release.dragging, "a sub-threshold release still must not drag");
    ASSERT_TRUE_MSG(click.release.clicked, "a press-and-release under the threshold reads as a click");

    // 10px of travel - well past the threshold.
    DragGesture drag = drag_threshold_run(ctx, 10.0f);
    ASSERT_TRUE_MSG(drag.move.dragging, "a move past the threshold latches dragging");
    ASSERT_NEAR_MSG(10.0, (double)drag.move.drag_delta.x, 0.001, "drag_delta tracks movement from the press point");

    fl_app_destroy(app);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The dropdown popup is a virtualized ui_list of label rows: clicking a row commits (1-based stable id ->
// *selected_index, index = id - 1) and closes; max_visible_rows bounds the viewport height (min(count, max) *
// row_stride) and the List virtualizes the rest; the substring filter feeds only the surviving subset to the List.
//
// Row layout ids: list_item_begin_impl keys each row on {list-container name, emit_pos + 1}. The container name
// is "<label>_dropdown_list" (button id is the label verbatim; see resolve_widget_id), and emit_pos is the
// absolute index into the (filtered) item array, so row at filtered position p has clay id {name, p + 1}.

typedef struct DropdownState {
    bool is_open;
    int choice;   // *selected_index, written on commit
    FlUiCtx* ctx; // captured during the tick so we can read laid-out row/container geometry afterwards
    FlString* options;
    int option_count;
    int max_visible_rows; // DropdownConfig.max_visible_rows (0 = default)
    bool searchable;
    FlTextInputState search; // auto-initialized by the dropdown on the first open frame
} DropdownState;

static void dropdown_frame(void* user_data) {
    DropdownState* st = (DropdownState*)user_data;
    st->ctx = ui_get_ctx();
    FlDropdownConfig cfg = {
        .show_cancel = false, // keep the popup to just the list so the geometry reads are unambiguous
        .min_width = 200.0f,
        .max_visible_rows = st->max_visible_rows,
        .searchable = st->searchable,
        .search_state = st->searchable ? &st->search : nullptr,
    };
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_dropdown_impl(FL_S("Pick"), &st->is_open, st->options, st->option_count, &st->choice, cfg);
    }
}

// The list container is emitted under {name "Pick_dropdown_list", id 0}; its laid-out height is the bounded
// viewport (min(count, max_visible_rows) * (row_height + gap)). Returns -1 if not laid out yet.
static float dropdown_list_height(DropdownState* st) {
    u32 cid = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Pick_dropdown_list"), .id = 0 });
    ItemState* it = hashmap_get(&st->ctx->shared->item_state, cid);
    return (it != nullptr && it->state.size.y > 0.0f) ? it->state.size.y : -1.0f;
}

// Center of the option row at filtered position pos (its clay id is {name, pos + 1}). False if not laid out
// (e.g. virtualized away) - that absence is itself the virtualization signal the clamp case asserts on.
static bool dropdown_row_center(DropdownState* st, int pos, i32* out_cx, i32* out_cy) {
    u32 rid = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Pick_dropdown_list"), .id = (i32)(pos + 1) });
    ItemState* it = hashmap_get(&st->ctx->shared->item_state, rid);
    if (it == nullptr || it->state.size.x <= 0.0f) {
        return false;
    }
    *out_cx = (i32)(it->state.pos.x + it->state.size.x * 0.5f);
    *out_cy = (i32)(it->state.pos.y + it->state.size.y * 0.5f);
    return true;
}

// True if the frame drew a rectangle in the theme's surface.selected colour - i.e. the List drew the
// selection-highlight fill on the row seeded from *selected_index. (Unselected rows use a transparent fill that
// emits no rectangle, so any surface.selected rect is the current-selection highlight.)
static bool dropdown_selection_highlight_drawn(void) {
    FlColor want = fl_theme_colors()->surface.selected;
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (!ui_get_render_command(i, &cmd) || cmd.type != UiRenderCmd_Rectangle) {
            continue;
        }
        if (fabsf(cmd.bg_color.r - want.r) < 1.0f && fabsf(cmd.bg_color.g - want.g) < 1.0f
            && fabsf(cmd.bg_color.b - want.b) < 1.0f && want.a > 0.0f) {
            return true;
        }
    }
    return false;
}

// FlDropdownConfig's padding_h/padding_v/corner_radius steer the closed trigger's box; a zeroed config falls back
// to the default metrics. Asserted off the debug render-command snapshot rather than pixels, so the numbers in the
// assertions ARE the geometry. The dropdown stays closed throughout; the popup keeps its own metrics.
UTEST(dropdown, trigger_geometry_follows_config) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // Identity scale, so the snapshot's logical rects are the pixels the config asked for.
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    const float dt = 1.0f / 60.0f;

    // A padding-driven trigger fits around its label, so the strip needs real text metrics. Built-in 8x8 font.
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 1));

    FlString opts[2] = { FL_S("Sine"), FL_S("Saw") };
    bool is_open = false;
    int selected = 0;
    UiRenderCommand rects[4];
    UiRenderCommand texts[4];

    // Two frames per case: the first registers the trigger's ItemState so the hover/focus animation has somewhere
    // to land, and only the second is measured. Each case passes its own label because a label is also the widget's
    // id, and the fixture's ItemState map outlives a case.
#define DROPDOWN_GEOM_FRAME(label_str, ...)                                                    \
    do {                                                                                       \
        for (int geom_pass = 0; geom_pass < 2; geom_pass++) {                                  \
            ui_update_begin(ctx);                                                              \
            ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {                       \
                ui_dropdown(FL_S(label_str), &is_open, opts, 2, &selected, __VA_ARGS__);       \
            }                                                                                  \
            ui_update_end(ctx, dt);                                                            \
        }                                                                                      \
        ASSERT_EQ_MSG(render_cmd_collect(UiRenderCmd_Rectangle, rects, 4), 1,                  \
                      "a closed dropdown paints exactly one rectangle: its trigger");          \
        ASSERT_EQ_MSG(render_cmd_collect(UiRenderCmd_Text, texts, 4), 1, "and one label run"); \
    } while (0)

    // Zeroed: the fixed FlWidgetHeight_MD box with FlRadius_MD corners and the FlSpacing_MD/SM padding pair.
    DROPDOWN_GEOM_FRAME("dropdown_geom_default", .show_cancel = true);
    ASSERT_NEAR_MSG((double)rects[0].height, (double)FlWidgetHeight_MD, 1e-4,
                    "a zeroed config keeps the fixed trigger height");
    ASSERT_NEAR_MSG((double)rects[0].corner_radius.tl, (double)FlRadius_MD, 1e-4, "and the default trigger radius");
    ASSERT_NEAR_MSG((double)(texts[0].x - rects[0].x), (double)FlSpacing_MD, 1e-4,
                    "and the default horizontal padding, read as the label's inset");

    // the compact pill: 12x7 padding at radius 8. The radius matches FlRadius_MD, so the height and the inset are
    // what separate this from the default case; the radius is pinned on its own below.
    DROPDOWN_GEOM_FRAME("dropdown_geom_pill", .padding_h = 12.0f, .padding_v = 7.0f, .corner_radius = 8.0f);
    const float pill_h = rects[0].height;
    ASSERT_LT_MSG((double)pill_h, (double)FlWidgetHeight_MD, "padding_v takes the trigger off the fixed height");
    ASSERT_NEAR_MSG((double)(texts[0].x - rects[0].x), 12.0, 1e-4, "padding_h insets the label");

    // The switched-on sizing really is padding-driven: 10px more padding per side is 20px more trigger. Stated
    // as a delta against the case above so no font metric is baked into the expected number.
    DROPDOWN_GEOM_FRAME("dropdown_geom_taller", .padding_h = 12.0f, .padding_v = 17.0f, .corner_radius = 8.0f);
    ASSERT_NEAR_MSG((double)(rects[0].height - pill_h), 20.0, 1e-4, "padding_v drives the trigger height");

    // Each knob stands on its own default. padding_h has no say in the sizing switch - the trigger grows to its
    // row horizontally either way - so it insets the label and leaves the fixed height alone.
    DROPDOWN_GEOM_FRAME("dropdown_geom_radius_only", .corner_radius = 4.0f);
    ASSERT_NEAR_MSG((double)rects[0].corner_radius.tl, 4.0, 1e-4, "corner_radius applies on its own");
    ASSERT_NEAR_MSG((double)rects[0].height, (double)FlWidgetHeight_MD, 1e-4, "and leaves the height alone");
    DROPDOWN_GEOM_FRAME("dropdown_geom_padding_h_only", .padding_h = 12.0f);
    ASSERT_NEAR_MSG((double)(texts[0].x - rects[0].x), 12.0, 1e-4, "padding_h applies on its own");
    ASSERT_NEAR_MSG((double)rects[0].height, (double)FlWidgetHeight_MD, 1e-4,
                    "and does not switch the trigger off its fixed height");
    ASSERT_NEAR_MSG((double)rects[0].corner_radius.tl, (double)FlRadius_MD, 1e-4, "nor move its radius");
    DROPDOWN_GEOM_FRAME("dropdown_geom_padding_v_only", .padding_v = 7.0f);
    ASSERT_NEAR_MSG((double)rects[0].height, (double)pill_h, 1e-4,
                    "padding_v drives the height on its own — the pill's height owes nothing to padding_h");
    ASSERT_NEAR_MSG((double)(texts[0].x - rects[0].x), (double)FlSpacing_MD, 1e-4,
                    "and leaves the horizontal inset at its default");

#undef DROPDOWN_GEOM_FRAME
}

// Clicking an option row commits its index to *selected_index (1-based id -> index = id - 1) and closes the popup.
UTEST(dropdown, click_commits_and_closes) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[4] = { FL_S("Red"), FL_S("Green"), FL_S("Blue"), FL_S("Alpha") };
    DropdownState st = { .is_open = true, .choice = 2, .options = opts, .option_count = 4 };

    // Settle: the list needs one frame to establish the container height for virtualization and another to emit
    // the visible rows so their ItemStates exist. The popup stays open (no click yet).
    for (int f = 0; f < 4; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    ASSERT_TRUE_MSG(st.is_open, "popup stays open with no click");
    ASSERT_EQ_MSG(st.choice, 2, "selection unchanged before any click");

    // The current selection (choice 2) is highlighted: the transient set seeded from *selected_index makes the
    // List draw its surface.selected fill on that row before any click happens.
    ASSERT_TRUE_MSG(dropdown_selection_highlight_drawn(), "the current selection's row is highlighted while open");

    // Click option 0 ("Red"): hover to propagate hot, then press + release.
    i32 cx = 0, cy = 0;
    ASSERT_TRUE_MSG(dropdown_row_center(&st, 0, &cx, &cy), "option row 0 was not laid out");
    fl_app_inject_mouse_move(app, cx, cy);
    fl_app_tick(app, &st, dropdown_frame);
    fl_app_tick(app, &st, dropdown_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, &st, dropdown_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    fl_app_tick(app, &st, dropdown_frame);

    ASSERT_EQ_MSG(st.choice, 0, "clicking option 0 (stable id 1) commits index 0");
    ASSERT_FALSE_MSG(st.is_open, "committing a selection closes the popup");

    fl_app_destroy(app);
}

// max_visible_rows bounds the popup viewport: a long list clamps to max_visible_rows and virtualizes the rest; a
// short list sizes to its content (fewer than max_visible_rows).
UTEST(dropdown, max_visible_rows_clamps_and_virtualizes) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    // Matches the file-local UI_DROPDOWN_ROW_HEIGHT (28) + UI_DROPDOWN_ROW_GAP (2) in ui_dropdown.c.
    const float row_stride = 28.0f + 2.0f;

    // Long list (100 options) clamped to 5 visible rows.
    FlString many[100];
    static char many_labels[100][8];
    for (int i = 0; i < 100; i++) {
        snprintf(many_labels[i], sizeof(many_labels[i]), "opt%d", i);
        many[i] = string_from_cstr_len(many_labels[i], strlen(many_labels[i]));
    }
    DropdownState st = { .is_open = true, .choice = 0, .options = many, .option_count = 100, .max_visible_rows = 5 };
    for (int f = 0; f < 4; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }
    ASSERT_NEAR(dropdown_list_height(&st), 5.0f * row_stride, 1.5f);

    // Virtualization: the top rows are laid out, a row well past the viewport is not emitted at all.
    i32 cx = 0, cy = 0;
    ASSERT_TRUE_MSG(dropdown_row_center(&st, 0, &cx, &cy), "top row is laid out");
    ASSERT_FALSE_MSG(dropdown_row_center(&st, 60, &cx, &cy), "a row far past the viewport is virtualized away");

    // Short list (3 options) with the same cap sizes to content, not to the cap.
    FlString few[3] = { FL_S("A"), FL_S("B"), FL_S("C") };
    DropdownState st2 = { .is_open = true, .choice = 0, .options = few, .option_count = 3, .max_visible_rows = 5 };
    for (int f = 0; f < 4; f++) {
        fl_app_tick(app, &st2, dropdown_frame);
    }
    ASSERT_NEAR(dropdown_list_height(&st2), 3.0f * row_stride, 1.5f);

    // max_visible_rows left at 0 resolves to the default cap (UI_DROPDOWN_DEFAULT_MAX_ROWS = 10): a 20-option
    // list clamps to 10 rows, distinguishing "default cap" from "no cap at all".
    DropdownState st3 = { .is_open = true, .choice = 0, .options = many, .option_count = 20, .max_visible_rows = 0 };
    for (int f = 0; f < 4; f++) {
        fl_app_tick(app, &st3, dropdown_frame);
    }
    ASSERT_NEAR(dropdown_list_height(&st3), 10.0f * row_stride, 1.5f);

    fl_app_destroy(app);
}

// When the substring filter matches nothing, the popup shows an empty list: no rows, no crash, selection
// unchanged, popup still open.
UTEST(dropdown, empty_when_all_filtered_out) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[3] = { FL_S("Red"), FL_S("Green"), FL_S("Blue") };
    DropdownState st = { .is_open = true, .choice = 1, .options = opts, .option_count = 3, .searchable = true };

    // First frame lets the dropdown auto-initialize the search buffer; then poke a query that matches no option.
    fl_app_tick(app, &st, dropdown_frame);
    ASSERT_TRUE_MSG(st.search.buffer != nullptr, "searchable dropdown initialized its search buffer");
    memcpy(st.search.buffer, "zzz", 3);
    st.search.buffer_length = 3;

    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }

    i32 cx = 0, cy = 0;
    ASSERT_FALSE_MSG(dropdown_row_center(&st, 0, &cx, &cy), "all options filtered out: no option row is laid out");
    ASSERT_EQ_MSG(st.choice, 1, "an empty filtered popup leaves the selection unchanged");
    ASSERT_TRUE_MSG(st.is_open, "an empty filtered popup stays open (nothing to commit)");
    ASSERT_TRUE_MSG(ui_get_render_command_count() > 0, "the popup panel still renders (no crash on an empty list)");

    fl_app_destroy(app);
}

// Clicking a row of a FILTERED list commits the ORIGINAL option index, not the filtered position: stable id =
// orig_index + 1 is baked into the descriptor during the filter, so index = id - 1 resolves past the filtered-out
// rows. Options "zero".."four"; the substring "o" keeps {zero(0), one(1), two(2), four(4)} - "three" has no 'o'.
UTEST(dropdown, searchable_filter_click_commits_original_index) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[5] = { FL_S("zero"), FL_S("one"), FL_S("two"), FL_S("three"), FL_S("four") };
    DropdownState st = { .is_open = true, .choice = 0, .options = opts, .option_count = 5, .searchable = true };

    // Let the dropdown auto-initialize the search buffer, then poke the query "o".
    fl_app_tick(app, &st, dropdown_frame);
    ASSERT_TRUE_MSG(st.search.buffer != nullptr, "searchable dropdown initialized its search buffer");
    memcpy(st.search.buffer, "o", 1);
    st.search.buffer_length = 1;
    for (int f = 0; f < 4; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }

    // The filter feeds only the surviving subset to the List: 4 rows survive, so a 5th filtered position does not.
    i32 cx = 0, cy = 0;
    ASSERT_TRUE_MSG(dropdown_row_center(&st, 3, &cx, &cy), "the 4th surviving row (filtered position 3) is laid out");
    ASSERT_FALSE_MSG(dropdown_row_center(&st, 4, &cx, &cy), "only 4 options survive the filter — no 5th row");

    // Click filtered position 3 ("four"): its stable id is orig_index + 1 = 5, so the commit maps to index 4.
    fl_app_inject_mouse_move(app, cx, cy);
    fl_app_tick(app, &st, dropdown_frame);
    fl_app_tick(app, &st, dropdown_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, &st, dropdown_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    fl_app_tick(app, &st, dropdown_frame);

    ASSERT_EQ_MSG(st.choice, 4, "clicking the filtered 4th row commits original index 4 (\"four\"), not position 3");
    ASSERT_FALSE_MSG(st.is_open, "committing a filtered selection closes the popup");

    fl_app_destroy(app);
}

// Center of the trigger button ("Pick" - the label verbatim, no override; resolve_widget_id gives {name, 0}), from
// its laid-out ItemState. False if not laid out yet.
static bool dropdown_trigger_center(DropdownState* st, i32* out_cx, i32* out_cy) {
    u32 bid = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Pick"), .id = 0 });
    ItemState* it = hashmap_get(&st->ctx->shared->item_state, bid);
    if (it == nullptr || it->state.size.x <= 0.0f) {
        return false;
    }
    *out_cx = (i32)(it->state.pos.x + it->state.size.x * 0.5f);
    *out_cy = (i32)(it->state.pos.y + it->state.size.y * 0.5f);
    return true;
}

// Returns false if the trigger was not laid out.
static bool dropdown_click_trigger(FlApp* app, DropdownState* st) {
    i32 bx = 0, by = 0;
    if (!dropdown_trigger_center(st, &bx, &by)) {
        return false;
    }
    fl_app_inject_mouse_move(app, bx, by);
    fl_app_tick(app, st, dropdown_frame);
    fl_app_tick(app, st, dropdown_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, st, dropdown_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    fl_app_tick(app, st, dropdown_frame);
    return true;
}

// Tap a key through the real app loop (press tick + release tick); the pointer stays parked off the rows so the
// keyboard, not hover, owns the list's focus cursor.
static void dropdown_key_tap(FlApp* app, DropdownState* st, FlKey key) {
    fl_app_inject_key(app, key, true);
    fl_app_tick(app, st, dropdown_frame);
    fl_app_inject_key(app, key, false);
    fl_app_tick(app, st, dropdown_frame);
}

// A non-searchable dropdown opens with the list focused: the focus ring is on the current-value row immediately,
// arrow keys navigate without a first click, and Enter and Space commit the focused option (mapping the List
// activation back to *selected_index and closing), matching the click-commit path.
UTEST(dropdown, keyboard_opens_focused_and_enter_space_commit) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[5] = { FL_S("Red"), FL_S("Green"), FL_S("Blue"), FL_S("Alpha"), FL_S("Cyan") };
    DropdownState st = { .is_open = false, .choice = 1, .options = opts, .option_count = 5 };

    // Warm up with the popup closed so the trigger button lays out; it stays closed until clicked.
    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    ASSERT_FALSE_MSG(st.is_open, "popup stays closed until the trigger is clicked");

    // Click the trigger: the false->true edge requests list focus (non-searchable), seeded on the current value.
    ASSERT_TRUE_MSG(dropdown_click_trigger(app, &st), "trigger button was not laid out");
    ASSERT_TRUE_MSG(st.is_open, "clicking the trigger opens the popup");

    // Park the pointer off every row so the keyboard owns the cursor, then settle the popup list.
    fl_app_inject_mouse_move(app, 5, 5);
    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }

    // Focus-on-open: the list is focused and its ring is drawn on the seeded (current-value) row before any click.
    ASSERT_TRUE_MSG(focus_ring_drawn(), "the freshly-opened non-searchable dropdown draws the list focus ring");
    ASSERT_EQ_MSG(st.choice, 1, "opening the popup does not change the selection");

    // Arrow Down moves the cursor to option 2 (id 3); Enter commits index 2 and closes - no row click needed.
    dropdown_key_tap(app, &st, FlKey_Down);
    dropdown_key_tap(app, &st, FlKey_Enter);
    ASSERT_EQ_MSG(st.choice, 2, "Down then Enter commits the roved option (index 2)");
    ASSERT_FALSE_MSG(st.is_open, "Enter-commit closes the popup");

    // Reopen (now on choice 2), rove Down to option 3 (id 4), and commit with Space this time.
    ASSERT_TRUE_MSG(dropdown_click_trigger(app, &st), "trigger button was not laid out on reopen");
    ASSERT_TRUE_MSG(st.is_open, "clicking the trigger reopens the popup");
    fl_app_inject_mouse_move(app, 5, 5);
    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }
    dropdown_key_tap(app, &st, FlKey_Down);
    dropdown_key_tap(app, &st, FlKey_Space);
    ASSERT_EQ_MSG(st.choice, 3, "Down then Space commits the roved option (index 3)");
    ASSERT_FALSE_MSG(st.is_open, "Space-commit closes the popup");

    fl_app_destroy(app);
}

// Reopening a non-searchable dropdown reseeds the focus cursor onto the CURRENT value, even after a previous open
// session left the cursor roved to a different row and closed WITHOUT committing. The List's has_focus is never
// cleared while the popup is closed, so the re-arm guard also checks global-ring ownership.
UTEST(dropdown, reopen_reseeds_cursor_to_current_value) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[5] = { FL_S("Red"), FL_S("Green"), FL_S("Blue"), FL_S("Alpha"), FL_S("Cyan") };
    DropdownState st = { .is_open = false, .choice = 1, .options = opts, .option_count = 5 };

    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }

    // Open (current value = choice 1, seeded on id 2), then rove Down to option 2 (id 3) WITHOUT committing.
    ASSERT_TRUE_MSG(dropdown_click_trigger(app, &st), "trigger button was not laid out");
    ASSERT_TRUE_MSG(st.is_open, "clicking the trigger opens the popup");
    fl_app_inject_mouse_move(app, 5, 5);
    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }
    dropdown_key_tap(app, &st, FlKey_Down);

    // Close without committing (caller-owned is_open): the selection stays choice 1. While closed the List is not
    // emitted, so its has_focus stays stuck true and the roved cursor (id 3) lingers in the persisted state.
    st.is_open = false;
    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }
    ASSERT_EQ_MSG(st.choice, 1, "closing without committing leaves the selection unchanged");

    // Reopen and immediately commit with Enter - no arrow. A correct reseed lands the cursor back on the current
    // value (choice 1, id 2), so Enter commits index 1; a stale cursor (id 3) would wrongly commit index 2.
    ASSERT_TRUE_MSG(dropdown_click_trigger(app, &st), "trigger button was not laid out on reopen");
    ASSERT_TRUE_MSG(st.is_open, "clicking the trigger reopens the popup");
    fl_app_inject_mouse_move(app, 5, 5);
    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }
    dropdown_key_tap(app, &st, FlKey_Enter);
    ASSERT_EQ_MSG(st.choice, 1, "reopen reseeds the cursor onto the current value: Enter commits index 1, not 2");
    ASSERT_FALSE_MSG(st.is_open, "Enter-commit closes the popup");

    fl_app_destroy(app);
}

// Clay id of the searchable dropdown's visible search input ({name "Pick_dropdown_search", id 0}) and its
// containing list. Keyboard ownership is routed by which of these holds the global focus ring.
static u32 dropdown_search_clay_id(void) {
    return ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Pick_dropdown_search"), .id = 0 });
}

static u32 dropdown_list_clay_id(void) {
    return ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Pick_dropdown_list"), .id = 0 });
}

// A SEARCHABLE dropdown renders a real visible text input at the top of the popup, and keyboard focus starts
// THERE on open (not in the list): the input is the first focusable widget in the popup scope, so the popup's
// initial-focus lands on it. The list therefore holds no focus and draws no ring until the user hands off to it.
UTEST(dropdown, searchable_renders_input_and_focuses_it_on_open) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[5] = { FL_S("Red"), FL_S("Green"), FL_S("Blue"), FL_S("Alpha"), FL_S("Cyan") };
    DropdownState st = { .is_open = false, .choice = 1, .options = opts, .option_count = 5, .searchable = true };

    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }

    ASSERT_TRUE_MSG(dropdown_click_trigger(app, &st), "trigger button was not laid out");
    ASSERT_TRUE_MSG(st.is_open, "clicking the trigger opens the searchable popup");
    fl_app_inject_mouse_move(app, 5, 5);
    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }

    ItemState* search_item = hashmap_get(&st.ctx->shared->item_state, dropdown_search_clay_id());
    ASSERT_TRUE_MSG(search_item != nullptr && search_item->state.size.y > 0.0f,
                    "the searchable dropdown renders a visible search input at the top of the popup");

    ASSERT_EQ_MSG((u64)dropdown_search_clay_id(), (u64)st.ctx->focused_id,
                  "a freshly-opened searchable dropdown puts keyboard focus in the search input");
    ASSERT_FALSE_MSG(focus_ring_drawn(), "the list holds no focus on open, so it draws no focus ring");

    fl_app_destroy(app);
}

// Typing into the visible search input drives the filter: the buffer text (not a hidden keystroke buffer) selects
// the surviving option subset. Options "zero".."four", typing "o" keeps {zero, one, two, four} - "three" has no 'o'.
UTEST(dropdown, searchable_typing_visible_input_filters_list) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[5] = { FL_S("zero"), FL_S("one"), FL_S("two"), FL_S("three"), FL_S("four") };
    DropdownState st = { .is_open = false, .choice = 0, .options = opts, .option_count = 5, .searchable = true };

    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }

    // Open and settle so the search input is focused (it consumes typed text only while focused).
    ASSERT_TRUE_MSG(dropdown_click_trigger(app, &st), "trigger button was not laid out");
    ASSERT_TRUE_MSG(st.is_open, "clicking the trigger opens the searchable popup");
    fl_app_inject_mouse_move(app, 5, 5);
    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }
    ASSERT_EQ_MSG((u64)dropdown_search_clay_id(), (u64)st.ctx->focused_id,
                  "focus is in the search input before typing");

    // Type "o" as a real keystroke (queued into the headless backend; the next tick drains it into the input).
    fl_app_inject_text(app, FL_S("o"));
    for (int f = 0; f < 4; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }

    FlString query = ui_text_input_get_text(&st.search);
    ASSERT_TRUE_MSG(query.length == 1 && query.data[0] == 'o', "the typed character reached the visible search input");

    i32 cx = 0, cy = 0;
    ASSERT_TRUE_MSG(dropdown_row_center(&st, 3, &cx, &cy), "typing 'o' keeps 4 options: the 4th survivor is laid out");
    ASSERT_FALSE_MSG(dropdown_row_center(&st, 4, &cx, &cy), "only 4 options survive the 'o' filter — no 5th row");

    fl_app_destroy(app);
}

// Focus is routed by hand-off: Down from the search input moves focus INTO the list (its ring appears on the first
// row), and Up on the list's first row (nav_up_edge) returns focus to the input (its ring disappears). Both edges
// are driven through the real app loop.
UTEST(dropdown, searchable_focus_handoff_input_list_input) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[5] = { FL_S("Red"), FL_S("Green"), FL_S("Blue"), FL_S("Alpha"), FL_S("Cyan") };
    DropdownState st = { .is_open = false, .choice = 0, .options = opts, .option_count = 5, .searchable = true };

    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }

    ASSERT_TRUE_MSG(dropdown_click_trigger(app, &st), "trigger button was not laid out");
    ASSERT_TRUE_MSG(st.is_open, "clicking the trigger opens the searchable popup");
    fl_app_inject_mouse_move(app, 5, 5);
    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }
    ASSERT_EQ_MSG((u64)dropdown_search_clay_id(), (u64)st.ctx->focused_id, "focus starts in the search input");
    ASSERT_FALSE_MSG(focus_ring_drawn(), "the list has no focus ring while the input holds focus");

    dropdown_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((u64)dropdown_list_clay_id(), (u64)st.ctx->focused_id,
                  "Down from the input moves focus into the list");
    ASSERT_TRUE_MSG(focus_ring_drawn(), "the list draws its focus ring once focus is handed to it");

    // The seeded cursor is already on the list's first row, so Up hands focus back to the input.
    dropdown_key_tap(app, &st, FlKey_Up);
    ASSERT_EQ_MSG((u64)dropdown_search_clay_id(), (u64)st.ctx->focused_id,
                  "Up on the list's first row returns focus to the search input");
    ASSERT_FALSE_MSG(focus_ring_drawn(), "the list drops its focus ring once focus returns to the input");
    ASSERT_TRUE_MSG(st.is_open, "focus routing does not close the popup or change the selection");
    ASSERT_EQ_MSG(st.choice, 0, "focus routing alone commits nothing");

    fl_app_destroy(app);
}

// After Down hands focus into the list, arrow keys and type-ahead navigate the filtered subset and Enter commits -
// exactly the List keyboard path the non-searchable dropdown uses, now reached through the search input.
UTEST(dropdown, searchable_into_list_arrow_typeahead_commit) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[5] = { FL_S("Red"), FL_S("Green"), FL_S("Blue"), FL_S("Alpha"), FL_S("Cyan") };
    DropdownState st = { .is_open = false, .choice = 0, .options = opts, .option_count = 5, .searchable = true };

    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }

    // Hand focus into the list, seeded on row 0 ("Red").
    ASSERT_TRUE_MSG(dropdown_click_trigger(app, &st), "trigger button was not laid out");
    fl_app_inject_mouse_move(app, 5, 5);
    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }
    dropdown_key_tap(app, &st, FlKey_Down); // input -> list, cursor on row 0
    ASSERT_EQ_MSG((u64)dropdown_list_clay_id(), (u64)st.ctx->focused_id, "Down handed focus to the list");

    dropdown_key_tap(app, &st, FlKey_Down);
    dropdown_key_tap(app, &st, FlKey_Enter);
    ASSERT_EQ_MSG(st.choice, 1, "arrow-nav within the filtered list then Enter commits the roved option (index 1)");
    ASSERT_FALSE_MSG(st.is_open, "Enter-commit closes the popup");

    // Reopen (now on choice 1); hand into the list, then type-ahead: 'c' jumps to "Cyan" (index 4). Enter commits.
    ASSERT_TRUE_MSG(dropdown_click_trigger(app, &st), "trigger button was not laid out on reopen");
    fl_app_inject_mouse_move(app, 5, 5);
    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }
    dropdown_key_tap(app, &st, FlKey_Down); // input -> list
    fl_app_inject_text(app, FL_S("c"));     // type-ahead within the list (input is no longer focused)
    for (int f = 0; f < 2; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }
    dropdown_key_tap(app, &st, FlKey_Enter);
    ASSERT_EQ_MSG(st.choice, 4, "type-ahead in the handed-off list jumps to \"Cyan\" and Enter commits index 4");
    ASSERT_FALSE_MSG(st.is_open, "Enter-commit closes the popup");

    fl_app_destroy(app);
}

// The Down hand-off seeds the list cursor from the FILTERED subset, not the original option array: when a query
// removes leading options the cursor lands on the first SURVIVING option, and arrow-nav within the narrowed set
// commits the correct ORIGINAL index across the filtered-out gap. Options "zero".."four"; typing "t" keeps
// {two(2), three(3)} - "zero"/"one"/"four" have no 't'. So the first surviving row is "two" (original index 2),
// not "zero" (index 0), and the row below it is "three" (original index 3), proving the seed and the id mapping.
UTEST(dropdown, searchable_filtered_handoff_commits_original_index) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[5] = { FL_S("zero"), FL_S("one"), FL_S("two"), FL_S("three"), FL_S("four") };
    DropdownState st = { .is_open = false, .choice = 0, .options = opts, .option_count = 5, .searchable = true };

    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }

    // Open, settle, type "t": only "two" and "three" survive (a 2-row filtered subset that drops leading options).
    ASSERT_TRUE_MSG(dropdown_click_trigger(app, &st), "trigger button was not laid out");
    fl_app_inject_mouse_move(app, 5, 5);
    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }
    fl_app_inject_text(app, FL_S("t"));
    for (int f = 0; f < 4; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }
    i32 cx = 0, cy = 0;
    ASSERT_TRUE_MSG(dropdown_row_center(&st, 1, &cx, &cy),
                    "the 't' filter keeps 2 options: the 2nd survivor is laid out");
    ASSERT_FALSE_MSG(dropdown_row_center(&st, 2, &cx, &cy), "only 2 options survive the 't' filter — no 3rd row");

    // Down seeds the list cursor on the first SURVIVING row ("two", original index 2). Enter with no arrow commits
    // index 2 - if the seed had wrongly landed on original option 0 ("zero", filtered out), it would commit 0.
    dropdown_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((u64)dropdown_list_clay_id(), (u64)st.ctx->focused_id, "Down hands focus to the filtered list");
    dropdown_key_tap(app, &st, FlKey_Enter);
    ASSERT_EQ_MSG(st.choice, 2,
                  "the hand-off seeds the first surviving filtered row (\"two\"), so Enter commits index 2");
    ASSERT_FALSE_MSG(st.is_open, "Enter-commit closes the popup");

    // Reopen (choice now 2, search cleared): re-filter to {two, three}, hand off, arrow Down to the 2nd survivor
    // ("three", original index 3), Enter. This proves arrow-nav within the filtered subset maps back past the gap.
    ASSERT_TRUE_MSG(dropdown_click_trigger(app, &st), "trigger button was not laid out on reopen");
    fl_app_inject_mouse_move(app, 5, 5);
    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }
    fl_app_inject_text(app, FL_S("t"));
    for (int f = 0; f < 4; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }
    dropdown_key_tap(app, &st, FlKey_Down); // input -> list, cursor on "two"
    dropdown_key_tap(app, &st, FlKey_Down); // "two" -> "three" within the filtered subset
    dropdown_key_tap(app, &st, FlKey_Enter);
    ASSERT_EQ_MSG(st.choice, 3, "arrow-nav within the filtered subset then Enter commits original index 3 (\"three\")");
    ASSERT_FALSE_MSG(st.is_open, "Enter-commit closes the popup");

    fl_app_destroy(app);
}

// Pressing Down in the search input when the filter matched NOTHING is a no-op: there is no list to enter (and an
// empty list can never raise nav_up_edge to hand focus back), so focus stays in the input rather than stranding on
// the empty list. This is the `filtered_count > 0` condition on the hand-off.
UTEST(dropdown, searchable_down_with_empty_filter_keeps_focus_in_input) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[3] = { FL_S("zero"), FL_S("one"), FL_S("two") };
    DropdownState st = { .is_open = false, .choice = 0, .options = opts, .option_count = 3, .searchable = true };

    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }

    // Type a query that matches nothing.
    ASSERT_TRUE_MSG(dropdown_click_trigger(app, &st), "trigger button was not laid out");
    fl_app_inject_mouse_move(app, 5, 5);
    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }
    fl_app_inject_text(app, FL_S("zzz"));
    for (int f = 0; f < 4; f++) {
        fl_app_tick(app, &st, dropdown_frame);
    }
    i32 cx = 0, cy = 0;
    ASSERT_FALSE_MSG(dropdown_row_center(&st, 0, &cx, &cy), "the 'zzz' filter removes every option — no rows laid out");
    ASSERT_EQ_MSG((u64)dropdown_search_clay_id(), (u64)st.ctx->focused_id,
                  "focus is in the input before the Down press");

    dropdown_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((u64)dropdown_search_clay_id(), (u64)st.ctx->focused_id,
                  "Down over an empty filtered list keeps focus in the search input (no focus trap)");
    ASSERT_TRUE_MSG(st.is_open, "the popup stays open");
    ASSERT_EQ_MSG(st.choice, 0, "nothing is committed");
    ASSERT_TRUE_MSG(ui_get_render_command_count() > 0, "the popup still renders (no crash on the empty-list hand-off)");

    fl_app_destroy(app);
}

UTEST(direct_ui_calls, calls_reach_real_bodies_in_a_live_frame) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(1.0f, 1.0f);
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 3));

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        fl_painter_rect(0.0f, 0.0f, 16.0f, 16.0f, ui_color(200, 30, 40, 255));
        FlSignal sig = fl_button_impl(FL_S("ok"), (FlWidgetConfig) { 0 });
        ASSERT_TRUE_MSG(!sig.clicked, "no input was injected, so the button must not report a click");
        bool checked = false;
        fl_checkbox_impl(FL_S("on"), &checked, (FlCheckboxConfig) { 0 });
        bool toggled = false;
        fl_switch_impl(FL_S("en"), &toggled, (FlSwitchConfig) { 0 });
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

// The public measurement entry points: font_id 0 resolves to the active font, and a missing context is a
// safe zeroed-metrics no-op.
UTEST(direct_ui_calls, text_measure_resolves_the_active_font) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(1.0f, 1.0f);
    FlFontId font = font_cache_load_default_font(ctx->shared->font_cache, 2);
    ui_font_set(font);

    ui_update_begin(ctx);
    FlTextMetrics by_active = fl_text_measure(FL_S("AA"), (FlTextStyle) { 0 });
    FlTextMetrics by_id = fl_text_measure(FL_S("AA"), (FlTextStyle) { .font_id = font });
    ASSERT_NEAR_MSG(by_active.width, 32.0f, 0.0001f, "two 8x8 cells at scale 2 through the public entry point");
    ASSERT_NEAR_MSG(by_id.width, by_active.width, 0.0001f, "font_id 0 resolves to the active font");
    FlFontMetrics fm = fl_font_metrics(0, 0.0f);
    ASSERT_NEAR_MSG(fm.line_height, 16.0f, 0.0001f, "font metrics resolve font id 0 to the active font");
    ui_update_end(ctx, 1.0f / 60.0f);

    FlUiCtx* active = g_ui_ctx;
    g_ui_ctx = nullptr;
    FlTextMetrics none = fl_text_measure(FL_S("AA"), (FlTextStyle) { 0 });
    FlFontMetrics none_fm = fl_font_metrics(0, 0.0f);
    g_ui_ctx = active;
    ASSERT_TRUE_MSG(none.width == 0.0f && none.height == 0.0f, "no-context measure returns zeroed metrics");
    ASSERT_TRUE_MSG(none_fm.line_height == 0.0f, "no-context font metrics returns zeroed metrics");
}

// Two sides of one decision, which is why they are stated together: a real frame carries the context's
// text_scale into the painter, and the public measure API deliberately does not apply it.
//
// An app authors in logical pixels and feeds the measured results back into layout, which applies the scale
// itself - scaling here too would apply it twice.
UTEST(direct_ui_calls, a_frame_scales_painter_text_but_not_the_measure_api) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(1.0f, 1.0f);

    // A FreeType face, not the built-in default: the default font reports the same metrics whatever size it
    // is asked for, so it cannot tell a scaled query from an unscaled one.
    const u16 authored = 16;
    FlFontId font
        = font_cache_load_font_memory(ctx->shared->font_cache, k_glyph_test_ttf, k_glyph_test_ttf_size, authored);
    ASSERT_TRUE_MSG(font != FONT_ID_INVALID, "the embedded test font loads");
    ui_font_set(font);

    ui_update_begin(ctx);
    FlTextMetrics measured_at_1 = fl_text_measure(FL_S("AA"), (FlTextStyle) { .font_id = font, .font_size = 12.0f });
    FlFontMetrics vmetrics_at_1 = fl_font_metrics(font, 12.0f);
    ui_update_end(ctx, 1.0f / 60.0f);
    ASSERT_TRUE_MSG(measured_at_1.width > 0.0f && vmetrics_at_1.line_height > 0.0f,
                    "both measurements are non-zero, so comparing them means something");

    ui_set_scale(2.0f, 2.0f);

    const FlPaintSpan* drawn = nullptr;
    ui_update_begin(ctx);
    FlTextMetrics measured_at_2 = fl_text_measure(FL_S("AA"), (FlTextStyle) { .font_id = font, .font_size = 12.0f });
    FlFontMetrics vmetrics_at_2 = fl_font_metrics(font, 12.0f);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        struct Painter* p = ui_get_painter();
        ASSERT_TRUE_MSG(p != nullptr, "a painter is available inside the layout scope");
        void* slot = nullptr;
        painter_scope_push(p, &slot);
        // font_size 0 takes the authored load size, the path that most depends on the scale arriving here.
        fl_painter_text_styled(4.0f, 4.0f, FL_S("AA"), (FlTextStyle) { .font_id = font }, ui_color(255, 255, 255, 255));
        painter_scope_pop(p);
        drawn = (const FlPaintSpan*)slot;
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_TRUE_MSG(drawn != nullptr && drawn->first != nullptr, "the styled draw attached a paint span");
    const FlCmdTextRun* run = (const FlCmdTextRun*)((const FlCmdHeader*)drawn->first->ir + 1);
    ASSERT_EQ_MSG((int)run->size, (int)font_cache_quantize_size_q4((float)authored * 2.0f),
                  "ui_update_begin hands the context's text_scale to the painter, so the run rasterizes at 2x");

    ASSERT_NEAR_MSG(measured_at_2.width, measured_at_1.width, 0.0001f,
                    "fl_text_measure stays in logical space; layout applies the scale, and doing it here too "
                    "would apply it twice");
    ASSERT_NEAR_MSG(vmetrics_at_2.line_height, vmetrics_at_1.line_height, 0.0001f,
                    "fl_font_metrics stays logical for the same reason");
}

// The painter styled-draw entry point resolves font_id 0 to the active font - the same rule fl_text_measure
// applies, so draw and measure agree on which font a zero meant - and a missing context is a safe no-op.
UTEST(direct_ui_calls, painter_text_styled_resolves_the_active_font) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(1.0f, 1.0f);
    FlFontId font = font_cache_load_default_font(ctx->shared->font_cache, 2);
    ui_font_set(font);

    ui_update_begin(ctx);
    const FlPaintSpan* by_active = nullptr;
    const FlPaintSpan* by_id = nullptr;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        struct Painter* p = ui_get_painter();
        ASSERT_TRUE_MSG(p != nullptr, "a painter is available inside the layout scope");
        void* slot_active = nullptr;
        painter_scope_push(p, &slot_active);
        fl_painter_text_styled(4.0f, 4.0f, FL_S("AA"), (FlTextStyle) { 0 }, ui_color(255, 255, 255, 255));
        painter_scope_pop(p);
        void* slot_id = nullptr;
        painter_scope_push(p, &slot_id);
        fl_painter_text_styled(4.0f, 4.0f, FL_S("AA"), (FlTextStyle) { .font_id = font }, ui_color(255, 255, 255, 255));
        painter_scope_pop(p);
        // The plain painter text entry point honors the same rule: font 0 draws with the active font.
        void* slot_plain = nullptr;
        painter_scope_push(p, &slot_plain);
        fl_painter_text(4.0f, 4.0f, FL_S("AA"), 0, ui_color(255, 255, 255, 255));
        painter_scope_pop(p);
        const FlPaintSpan* by_plain = (const FlPaintSpan*)slot_plain;
        ASSERT_TRUE_MSG(by_plain != nullptr && by_plain->first != nullptr, "plain text with font 0 draws");
        const FlCmdTextRun* run_plain = (const FlCmdTextRun*)((const FlCmdHeader*)by_plain->first->ir + 1);
        ASSERT_EQ_MSG((int)run_plain->font, (int)font, "fl_painter_text resolves font 0 to the active font");
        by_active = (const FlPaintSpan*)slot_active;
        by_id = (const FlPaintSpan*)slot_id;
    }

    ASSERT_TRUE_MSG(by_active != nullptr && by_active->first != nullptr, "font_id 0 draws through the active font");
    ASSERT_TRUE_MSG(by_id != nullptr && by_id->first != nullptr, "an explicit font id draws");
    const FlCmdTextRun* run_active = (const FlCmdTextRun*)((const FlCmdHeader*)by_active->first->ir + 1);
    const FlCmdTextRun* run_id = (const FlCmdTextRun*)((const FlCmdHeader*)by_id->first->ir + 1);
    ASSERT_EQ_MSG((int)run_active->font, (int)font, "font_id 0 resolves to the active font");
    ASSERT_EQ_MSG((int)run_id->font, (int)run_active->font, "both spellings resolve to the same font");
    const FlGlyph* g_active = (const FlGlyph*)(run_active + 1);
    const FlGlyph* g_id = (const FlGlyph*)(run_id + 1);
    ASSERT_TRUE_MSG(g_active[0].x == g_id[0].x && g_active[0].y == g_id[0].y && g_active[1].x == g_id[1].x,
                    "both spellings lay the run out identically");
    ui_update_end(ctx, 1.0f / 60.0f);

    FlUiCtx* active = g_ui_ctx;
    g_ui_ctx = nullptr;
    fl_painter_text_styled(0.0f, 0.0f, FL_S("AA"), (FlTextStyle) { 0 }, ui_color(255, 255, 255, 255));
    g_ui_ctx = active;
}

// The public effect entry point reaches the painter body inside a live frame, and is a safe no-op with no
// context (hence no open scope).
UTEST(direct_ui_calls, painter_set_effect_reaches_the_body) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(1.0f, 1.0f);

    ui_update_begin(ctx);
    const FlPaintSpan* painted = nullptr;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        struct Painter* p = ui_get_painter();
        ASSERT_TRUE_MSG(p != nullptr, "a painter is available inside the layout scope");
        void* slot = nullptr;
        painter_scope_push(p, &slot);
        fl_painter_set_effect(
            (FlEffect) { .kind = FlEffectKind_DropShadow, .color = { 0, 0, 0, 255 }, .off_y = 24.0f, .blur = 70.0f });
        fl_painter_rect(0.0f, 0.0f, 8.0f, 8.0f, ui_color(255, 255, 255, 255));
        painter_scope_pop(p);
        painted = (const FlPaintSpan*)slot;
    }

    ASSERT_TRUE_MSG(painted != nullptr && painted->first != nullptr, "the public painter calls drew");
    const FlCmdHeader* h = (const FlCmdHeader*)painted->first->ir;
    ASSERT_TRUE_MSG(h->kind == FlCmd_FillRect && (h->flags & FL_HAS_EFFECT) != 0,
                    "fl_painter_set_effect reaches the body: the later rect carries the effect");
    const FlEffect* fx = (const FlEffect*)((const FlCmdFillRect*)(h + 1) + 1);
    ASSERT_TRUE_MSG(fx->kind == FlEffectKind_DropShadow && fx->off_y == 24.0f && fx->blur == 70.0f,
                    "the effect crosses the public seam intact");
    ui_update_end(ctx, 1.0f / 60.0f);

    FlUiCtx* active = g_ui_ctx;
    g_ui_ctx = nullptr;
    fl_painter_set_effect((FlEffect) { .kind = FlEffectKind_Glow, .blur = 1.0f });
    g_ui_ctx = active;
}

// The public bar-row entry point reaches the painter body inside a live frame: the whole row crosses the
// pointer+count seam into one batched record, and it is a safe no-op with no context.
UTEST(direct_ui_calls, painter_bars_reaches_the_body) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ui_set_scale(1.0f, 1.0f);

    ui_update_begin(ctx);
    const FlPaintSpan* painted = nullptr;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        struct Painter* p = ui_get_painter();
        ASSERT_TRUE_MSG(p != nullptr, "a painter is available inside the layout scope");
        void* slot = nullptr;
        painter_scope_push(p, &slot);
        FlBar bars[3] = { { 0.0f, 2.0f, 10.0f }, { 3.0f, 2.0f, -4.0f }, { 6.0f, 2.0f, 7.0f } };
        fl_painter_bars(100.0f, bars, 3, ui_color(200, 100, 50, 255));
        painter_scope_pop(p);
        painted = (const FlPaintSpan*)slot;
    }

    ASSERT_TRUE_MSG(painted != nullptr && painted->first != nullptr, "the public bar row drew");
    const FlCmdHeader* h = (const FlCmdHeader*)painted->first->ir;
    ASSERT_TRUE_MSG(h->kind == FlCmd_Bar, "one batched bar-set record carries the whole row");
    const FlCmdBarSet* bar_set = (const FlCmdBarSet*)(h + 1);
    ASSERT_TRUE_MSG(bar_set->baseline_y == 100.0f, "the baseline crosses the public seam");
    ASSERT_EQ_MSG(bar_set->bar_count, 3u, "the slice length becomes the record's bar count");
    const FlBar* rbars = (const FlBar*)(bar_set + 1);
    ASSERT_TRUE_MSG(rbars[2].x == 6.0f && rbars[2].w == 2.0f && rbars[2].h == 7.0f,
                    "the last bar survives the pointer+count seam, so the whole row does");
    ui_update_end(ctx, 1.0f / 60.0f);

    FlUiCtx* active = g_ui_ctx;
    g_ui_ctx = nullptr;
    FlBar one = { 0.0f, 1.0f, 1.0f };
    fl_painter_bars(0.0f, &one, 1, ui_color(255, 255, 255, 255));
    g_ui_ctx = active;
}

// The get-or-bail preamble every context-touching forwarder in ui_api.c shares. With no active context these
// public entry points must be safe no-ops handing back the value their contract documents as "nothing" - not a
// crash, and not a zeroed struct whose fields read as real data: the cursor is visible by default (so a zeroed
// false would invert it).
UTEST(direct_ui_calls, no_context_forwarders_return_documented_defaults) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // --- live context: each forwarder reaches its body ---

    fl_cursor_set((FlCursor) { .shape = FlCursorShape_Hand });
    ASSERT_EQ_MSG((int)fl_cursor_get_requested().shape, (int)FlCursorShape_Hand,
                  "the requested shape reaches the context");
    fl_cursor_set_visible(false);
    ASSERT_FALSE_MSG(fl_cursor_get_visible(), "the visibility toggle reaches the context");

    fl_input_push_mouse_event((FlMouseEvent) { .type = FlMouseEvent_Move, .move = { .x = 42.0f, .y = 24.0f } });
    ASSERT_TRUE_MSG(ctx->input_state.mouse.x == 42.0f && ctx->input_state.mouse.y == 24.0f,
                    "the pushed event reaches the context's input state");

    // Snapshot the two fields the payload-gated pushes below would write, so the no-context half can be shown
    // not to have landed.
    const uint32_t text_len_before = ctx->input_state.keyboard.text_input.length;
    const bool focus_gained_before = ctx->input_state.window_events.focus_gained;
    const bool key0_before = ctx->input_state.keyboard.keys[0];
    const bool pad0_button0_before = ctx->input_state.gamepads[0].buttons[0];

    FlListUniformRange live_range = { 0, 0, 0 };
    char live_fmt[8] = { 0 };
    u32 live_fmt_len = 0;
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(240) }, .direction = FlLayoutDirection_TopToBottom) {
        live_range = fl_list_uniform_begin(FL_S("uniform_bail_probe"), 100,
                                           (FlListUniformOpts) { .height_hint = 24.0f, .scroll_to = -1 });
        for (u32 i = live_range.start; i < live_range.end; i++) {
            fl_list_uniform_item_begin(i);
            ui_layout_area(.sizing = { sizing_grow(), sizing_fixed(24) }) {}
            fl_list_uniform_item_end();
        }
        fl_list_uniform_end();

        // The frame arena the variadic formatter writes into is only live inside the frame, so copy out here.
        FlString formatted = fl_format_frame_string("%d-%s", 7, "ok");
        live_fmt_len = formatted.length;
        if (formatted.length > 0 && formatted.length < sizeof(live_fmt)) {
            memcpy(live_fmt, formatted.data, formatted.length);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ASSERT_EQ_MSG(live_range.total, 100u, "the uniform-list forwarder reaches the body and echoes the row count");
    ASSERT_TRUE_MSG(live_range.end > live_range.start, "the live call emits a non-empty visible window");
    ASSERT_EQ_MSG(live_fmt_len, 4u, "the variadic formatter reaches the frame arena");
    ASSERT_TRUE_MSG(memcmp(live_fmt, "7-ok", 4) == 0, "the formatted frame string crosses the seam intact");

    // --- no context: every one of them bails, none of them crash ---

    FlUiCtx* active = g_ui_ctx;
    g_ui_ctx = nullptr;

    fl_cursor_set((FlCursor) { .shape = FlCursorShape_Crosshair });
    fl_cursor_set_visible(true);
    ASSERT_EQ_MSG((int)fl_cursor_get_requested().shape, (int)FlCursorShape_Arrow,
                  "no context resolves to the arrow, the documented fallback shape");
    ASSERT_TRUE_MSG(fl_cursor_get_visible(), "no context reports the default-visible cursor, not a zeroed false");

    // begin and end must bail as a pair: each half opens/closes a layout element, so a guard on only one of
    // them would leave the layout stack unbalanced rather than merely returning the wrong value.
    FlListUniformRange range = fl_list_uniform_begin(FL_S("uniform_bail_probe"), 100,
                                                     (FlListUniformOpts) { .height_hint = 24.0f, .scroll_to = -1 });
    ASSERT_TRUE_MSG(range.start == 0 && range.end == 0 && range.total == 0,
                    "no context yields an empty visible window");
    fl_list_uniform_item_begin(0);
    fl_list_uniform_item_end();
    fl_list_uniform_end();

    FlString bailed = fl_format_frame_string("%d", 1);
    ASSERT_TRUE_MSG(bailed.length == 0, "no context yields an empty frame string");

    // The push surface guards here rather than in the callee: fl_platform_input_push_* opens with
    // `&ctx->input_state` and takes a non-null context as a precondition, so an unguarded forwarder would
    // write through null instead of dropping the event. Every payload below is chosen to reach a branch that
    // actually writes - a zero-valued FlTextInputEvent (length 0) and a zero-valued FlWindowEvent
    // (FlWindowEvent_None) are both no-ops in the callee, so they would pass whether or not the guard exists.
    fl_input_push_key_event((FlKeyEvent) { 0 });                              // key 0 is in range -> keys[0] write
    fl_input_push_text_input_event((FlTextInputEvent) { .text = FL_S("x") }); // length > 0 -> text buffer write
    fl_input_push_mouse_event((FlMouseEvent) { .type = FlMouseEvent_Move, .move = { .x = 1.0f, .y = 1.0f } });
    fl_input_push_gamepad_event((FlGamepadEvent) { 0 }); // slot 0 is in range -> buttons write
    fl_input_push_window_event((FlWindowEvent) { .type = FlWindowEvent_Focus_Gained });

    g_ui_ctx = active;
    ASSERT_TRUE_MSG(ctx->input_state.mouse.x == 42.0f,
                    "the no-context mouse push dropped its event rather than reaching the live context");
    ASSERT_TRUE_MSG(ctx->input_state.keyboard.text_input.length == text_len_before,
                    "the no-context text-input push dropped its event rather than reaching the live context");
    ASSERT_TRUE_MSG(ctx->input_state.window_events.focus_gained == focus_gained_before,
                    "the no-context window push dropped its event rather than reaching the live context");
    ASSERT_TRUE_MSG(ctx->input_state.keyboard.keys[0] == key0_before,
                    "the no-context key push dropped its event rather than reaching the live context");
    ASSERT_TRUE_MSG(ctx->input_state.gamepads[0].buttons[0] == pad0_button0_before,
                    "the no-context gamepad push dropped its event rather than reaching the live context");

    fl_cursor_set_visible(true);
    ASSERT_TRUE_MSG(fl_cursor_get_visible(), "the restored context is reachable again");
}

UTEST(frame_api, setter_updates_persistent_frame_envelope_alpha) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ASSERT_EQ_MSG(ctx->global_alpha, 255, "frame opacity defaults to fully opaque");

    fl_frame_set_alpha(173);
    ASSERT_EQ_MSG(ctx->global_alpha, 173, "direct frame setter updates the envelope source");

    fl_frame_set_alpha(91);
    ASSERT_EQ_MSG(ctx->global_alpha, 91, "frame setter reaches the body");

    ui_update_begin(ctx);
    ui_update_end(ctx, 1.0f / 60.0f);
    ASSERT_EQ_MSG(s_render.submitted_global_alpha, 91, "submitted frame envelope captures the requested opacity");
    ASSERT_EQ_MSG(ctx->global_alpha, 91, "frame opacity persists across submission until changed");
    ASSERT_EQ_MSG(s_render.submitted_background.alpha, 255,
                  "frame opacity does not modify background composition opacity");

    FlUiCtx* active = g_ui_ctx;
    g_ui_ctx = nullptr;
    fl_frame_set_alpha(0);
    g_ui_ctx = active;
    ASSERT_EQ_MSG(ctx->global_alpha, 91, "no-context frame setter is a safe no-op");

    fl_frame_set_alpha(255);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// widget_id - ui_item_id_seed: per-item id seeding for multi-item widgets (radio group, tab strip).
//
// ui_item_id_seed decorrelates the instance base so the item ranges of two nearby instance keys do not overlap,
// while staying a bijection in index for a fixed key (items within one widget never alias each other).

UTEST(widget_id, seed_is_bijection_within_one_instance) {
    // For a fixed instance key, distinct item indices must map to distinct seeds - otherwise two items in the
    // same widget would share per-item state and trip the duplicate-id assert.
    const u32 key = 0x00400000u;
    enum { N = 512 };
    static u32 seeds[N];
    for (u32 i = 0; i < N; i++) {
        seeds[i] = ui_item_id_seed(key, i);
    }
    for (int i = 0; i < N; i++) {
        for (int j = i + 1; j < N; j++) {
            ASSERT_NE_MSG(seeds[i], seeds[j], "per-item seeds within one instance must be unique");
        }
    }
}

UTEST(widget_id, adjacent_instance_keys_do_not_collide) {
    // Cover the small gaps that arise from adjacent 4- and 8-byte fields, with item counts well past any real
    // radio group / tab bar.
    const u32 base = 0x7ffe0100u;
    const u32 gaps[] = { 1, 2, 4, 8, 12, 16 };
    enum { ITEMS = 64 };
    for (size_t g = 0; g < sizeof(gaps) / sizeof(gaps[0]); g++) {
        const u32 key_a = base;
        const u32 key_b = base + gaps[g];
        static u32 both[2 * ITEMS];
        int n = 0;
        for (u32 i = 0; i < ITEMS; i++) {
            both[n++] = ui_item_id_seed(key_a, i);
        }
        for (u32 i = 0; i < ITEMS; i++) {
            both[n++] = ui_item_id_seed(key_b, i);
        }
        for (int i = 0; i < n; i++) {
            for (int j = i + 1; j < n; j++) {
                ASSERT_NE_MSG(both[i], both[j], "per-item seeds must not collide across adjacent instance keys");
            }
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The virtual keyboard lives across many frames while the caller's title/allowed_chars string storage may not,
// so ui_virtual_keyboard_create copies the caller strings into its dedicated, rewound-per-cycle arena.

UTEST(virtual_keyboard, create_owns_strings_and_does_not_leak) {
    FlArena* arena = arena_new();
    ASSERT_TRUE(arena != nullptr);

    // Non-static (dynamic) caller strings backed by mutable stack buffers.
    char title_buf[] = "Enter name";
    char chars_buf[] = "abc";
    VirtualKeyboardConfig config = { .max_length = 0,
                                     .allowed_chars = string_from_cstr_len(chars_buf, sizeof(chars_buf) - 1),
                                     .title = string_from_cstr_len(title_buf, sizeof(title_buf) - 1),
                                     .initial_text = string_empty(),
                                     .show_character_count = false,
                                     .initial_page = VK_PAGE_LOWERCASE };

    VirtualKeyboard* vk = ui_virtual_keyboard_create(arena, config);
    // Copied, not aliased.
    ASSERT_TRUE(vk->config.title.data != title_buf);
    ASSERT_TRUE(vk->config.allowed_chars.data != chars_buf);
    ASSERT_EQ((u64)vk->config.title.length, (u64)(sizeof(title_buf) - 1));
    ASSERT_EQ((u64)vk->config.allowed_chars.length, (u64)(sizeof(chars_buf) - 1));

    // Clobber the caller buffers; the keyboard's owned copies must be unaffected.
    memory_set(title_buf, sizeof(title_buf), 'X', sizeof(title_buf));
    memory_set(chars_buf, sizeof(chars_buf), 'X', sizeof(chars_buf));
    ASSERT_EQ(memory_compare(vk->config.title.data, "Enter name", 10), 0);
    ASSERT_EQ(memory_compare(vk->config.allowed_chars.data, "abc", 3), 0);

    // Static (FL_S("...")) strings are already stable storage: fl_string_copy short-circuits and returns them
    // unchanged (no allocation).
    VirtualKeyboardConfig static_cfg = config;
    static_cfg.title = FL_S("Static title");
    VirtualKeyboard* vk_static = ui_virtual_keyboard_create(arena, static_cfg);
    ASSERT_TRUE(vk_static->config.title.data == FL_S("Static title").data);

    // No per-cycle leak: with a rewind before each create (as the focus-gain path does), the arena position
    // after N cycles equals the position after a single create - growth is O(1), not O(N).
    fl_arena_rewind(arena);
    ui_virtual_keyboard_create(arena, config);
    void* pos_after_one = fl_arena_current_pos(arena);
    for (int i = 0; i < 50; ++i) {
        fl_arena_rewind(arena);
        ui_virtual_keyboard_create(arena, config);
    }
    ASSERT_TRUE(fl_arena_current_pos(arena) == pos_after_one);

    fl_arena_destroy(arena);
}

// Drive a real ui_text_input through the focus-gain path and confirm the keyboard is allocated from
// ctx->vk_arena (not the persistent arena) and that repeated focus-gain cycles do not grow it.
UTEST(virtual_keyboard, focus_gain_uses_vk_arena_and_does_not_grow) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ASSERT_TRUE_MSG(ctx->vk_arena != nullptr, "ui_create wired ctx->vk_arena");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    const float dt = 1.0f / 60.0f;

    // No physical keyboard => focus-gain on a text input spawns the virtual keyboard.
    ctx->physical_keyboard_available = false;
    ctx->active_virtual_keyboard = nullptr;
    ctx->vk_target_text_input = nullptr;

    FlTextInputState state = { 0 };
    const u32 ti_clay = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("vk_focus_input"), .id = 0 });

#define TI_FRAME()                                                                    \
    do {                                                                              \
        ctx->mouse_pressed_this_frame = false;                                        \
        ctx->mouse_released_this_frame = false;                                       \
        ui_update_begin(ctx);                                                         \
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {                  \
            ui_text_input(FL_S("vk_focus_input"), &state, (FlTextInputConfig) { 0 }); \
        }                                                                             \
        ui_update_end(ctx, dt);                                                       \
    } while (0)

    // Focus takes effect one frame after focused_id is set (item->state.focused is written from focused_id in the
    // command pass), and focus_gained needs a prior unfocused frame - a short bounded loop reaches it.
    ctx->focused_id = ti_clay;
    for (int i = 0; i < 8 && ctx->active_virtual_keyboard == nullptr; i++) {
        TI_FRAME();
    }
    ASSERT_TRUE_MSG(ctx->active_virtual_keyboard != nullptr, "focus-gain with no physical keyboard spawns a VK");

    // The keyboard must live in vk_arena, not the persistent arena.
    const u8* vk_lo = (const u8*)ctx->vk_arena->ptr;
    const u8* vk_hi = (const u8*)fl_arena_current_pos(ctx->vk_arena);
    const u8* vk_ptr = (const u8*)ctx->active_virtual_keyboard;
    ASSERT_TRUE_MSG(vk_ptr >= vk_lo && vk_ptr < vk_hi, "VK allocated from ctx->vk_arena, not ctx->persistant");

    void* pos_cycle1 = fl_arena_current_pos(ctx->vk_arena);

    // Dismiss and run a second full focus-gain cycle; the per-cycle fl_arena_rewind must keep growth at zero.
    ctx->active_virtual_keyboard = nullptr;
    ctx->vk_target_text_input = nullptr;
    ctx->focused_id = 0;
    for (int i = 0; i < 8 && state.was_focused_last_frame; i++) { // blur until the input reports unfocused
        TI_FRAME();
    }
    ctx->focused_id = ti_clay;
    for (int i = 0; i < 8 && ctx->active_virtual_keyboard == nullptr; i++) {
        TI_FRAME();
    }
    ASSERT_TRUE_MSG(ctx->active_virtual_keyboard != nullptr, "second focus-gain re-spawns a VK");
    ASSERT_TRUE_MSG(fl_arena_current_pos(ctx->vk_arena) == pos_cycle1, "vk_arena does not grow across focus cycles");

#undef TI_FRAME
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A keyboard that closed itself is retired by the shared close-out both ui_update_end and
// ui_virtual_keyboard_update_and_render run: a submit commits its buffer to the target input, a cancel discards it,
// and either way both context slots clear.

UTEST(virtual_keyboard, close_out_commits_on_submit_and_discards_on_cancel) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    const float dt = 1.0f / 60.0f;

    ctx->physical_keyboard_available = false;
    ctx->active_virtual_keyboard = nullptr;
    ctx->vk_target_text_input = nullptr;

    FlTextInputState state = { 0 };
    const u32 ti_clay = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("vk_closeout_input"), .id = 0 });

#define VK_TI_FRAME()                                                                    \
    do {                                                                                 \
        ctx->mouse_pressed_this_frame = false;                                           \
        ctx->mouse_released_this_frame = false;                                          \
        ui_update_begin(ctx);                                                            \
        ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {                     \
            ui_text_input(FL_S("vk_closeout_input"), &state, (FlTextInputConfig) { 0 }); \
        }                                                                                \
        ui_update_end(ctx, dt);                                                          \
    } while (0)

    // Submit: the keyboard's buffer lands in the target input.
    ctx->focused_id = ti_clay;
    for (int i = 0; i < 8 && ctx->active_virtual_keyboard == nullptr; i++) {
        VK_TI_FRAME();
    }
    ASSERT_TRUE_MSG(ctx->active_virtual_keyboard != nullptr, "focus-gain spawns a VK to close out");

    VirtualKeyboard* vk = ctx->active_virtual_keyboard;
    ASSERT_TRUE_MSG(vk->buffer_capacity >= 6, "VK buffer holds the test string");
    memcpy(vk->buffer, "hello", 5);
    vk->buffer_length = 5;
    vk->submitted = true;
    vk->cancelled = false;
    vk->active = false; // the keyboard closed itself this frame

    VK_TI_FRAME();

    FlString committed = ui_text_input_get_text(&state);
    ASSERT_EQ_MSG(5u, (u32)committed.length, "submitted text length reaches the input");
    ASSERT_TRUE_MSG(memcmp(committed.data, "hello", 5) == 0, "submitted text content reaches the input");
    ASSERT_TRUE_MSG(ctx->active_virtual_keyboard == nullptr, "submit clears active_virtual_keyboard");
    ASSERT_TRUE_MSG(ctx->vk_target_text_input == nullptr, "submit clears vk_target_text_input");

    // Cancel: the input keeps the text the submit above committed.
    ctx->focused_id = 0;
    for (int i = 0; i < 8 && state.was_focused_last_frame; i++) {
        VK_TI_FRAME();
    }
    ctx->focused_id = ti_clay;
    for (int i = 0; i < 8 && ctx->active_virtual_keyboard == nullptr; i++) {
        VK_TI_FRAME();
    }
    ASSERT_TRUE_MSG(ctx->active_virtual_keyboard != nullptr, "second focus-gain re-spawns a VK");

    vk = ctx->active_virtual_keyboard;
    memcpy(vk->buffer, "DISCARD", 7);
    vk->buffer_length = 7;
    vk->submitted = false;
    vk->cancelled = true;
    vk->active = false;

    VK_TI_FRAME();

    FlString after_cancel = ui_text_input_get_text(&state);
    ASSERT_EQ_MSG(5u, (u32)after_cancel.length, "cancel leaves the input text untouched");
    ASSERT_TRUE_MSG(memcmp(after_cancel.data, "hello", 5) == 0, "cancel discards the keyboard buffer");
    ASSERT_TRUE_MSG(ctx->active_virtual_keyboard == nullptr, "cancel clears active_virtual_keyboard");
    ASSERT_TRUE_MSG(ctx->vk_target_text_input == nullptr, "cancel clears vk_target_text_input");

#undef VK_TI_FRAME
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Typed text must survive the real fl_app tick order (pump -> fl_platform_input_update -> frame body).
// fl_platform_input_update snapshots kb->text_input into ctx->text_input_this_frame before clearing the raw field,
// and ui_text_input reads that snapshot - so a character
// injected before the tick reaches the widget's buffer.

typedef struct TypedTextState {
    FlUiCtx* ctx;
    FlTextInputState input;
} TypedTextState;

static void typed_text_frame(void* user_data) {
    TypedTextState* st = (TypedTextState*)user_data;
    st->ctx = ui_get_ctx();
    // Hold the field focused every frame (a physical keyboard, so no virtual keyboard steals the path) - focus
    // survives the tick, but re-asserting it is immune to any focus auto-restore reshuffle.
    st->ctx->physical_keyboard_available = true;
    st->ctx->focused_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("typed_input"), .id = 0 });
    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {
        ui_text_input(FL_S("typed_input"), &st->input, (FlTextInputConfig) { 0 });
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Injected modifier keys hold a mask, so a headless test can drive a chord.

typedef struct ChordState {
    u32 modifiers;
    bool r_pressed;
} ChordState;

static void chord_frame(void* user_data) {
    ChordState* st = (ChordState*)user_data;
    st->modifiers = fl_input_keyboard_modifiers();
    st->r_pressed = fl_input_key_pressed(FlKey_R);
}

UTEST(input, an_injected_modifier_is_held_across_the_keys_pressed_under_it) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("chord"), .width = 200, .height = 200, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    ChordState st = { 0 };
    fl_app_tick(app, &st, chord_frame);
    ASSERT_EQ_MSG((int)st.modifiers, 0, "no modifier is held before one is injected");

    fl_app_inject_key(app, FlKey_Left_Control, true);
    fl_app_inject_key(app, FlKey_R, true);
    fl_app_tick(app, &st, chord_frame);
    ASSERT_EQ_MSG((int)st.modifiers, (int)FlKeyModifier_Control, "Ctrl is held under the R press");
    ASSERT_TRUE_MSG(st.r_pressed, "the R press edge lands on the same frame");

    fl_app_inject_key(app, FlKey_R, false);
    fl_app_inject_key(app, FlKey_Left_Control, false);
    fl_app_tick(app, &st, chord_frame);
    ASSERT_EQ_MSG((int)st.modifiers, 0, "releasing the modifier clears the mask");

    fl_app_destroy(app);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// OS file drops: what the frame body reads back from an injected drop, and that the reads are per-frame.

typedef struct DroppedFilesState {
    u32 count;
    FlString first;
    FlVec2 first_position;
    FlString second;
} DroppedFilesState;

static void dropped_files_frame(void* user_data) {
    DroppedFilesState* st = (DroppedFilesState*)user_data;
    st->count = fl_input_dropped_file_count();
    st->first = fl_input_dropped_file(0);
    st->first_position = fl_input_dropped_file_position(0);
    st->second = fl_input_dropped_file(1);
}

UTEST(input, dropped_files_reach_the_frame_and_clear_after_it) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("drops"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    DroppedFilesState st = { 0 };
    fl_app_tick(app, &st, dropped_files_frame);
    ASSERT_EQ_MSG((int)st.count, 0, "a frame with no drop reads no files");

    fl_app_inject_drop_file(app, FL_S("/tmp/kick.wav"), 120.0f, 64.0f);
    fl_app_inject_drop_file(app, FL_S("/tmp/snare.wav"), 120.0f, 64.0f);
    fl_app_tick(app, &st, dropped_files_frame);

    ASSERT_EQ_MSG((int)st.count, 2, "both dropped files reach the frame");
    ASSERT_EQ_MSG((int)st.first.length, 13, "the first path arrives whole");
    EXPECT_EQ(0, memcmp(st.first.data, "/tmp/kick.wav", 13));
    EXPECT_EQ(0, memcmp(st.second.data, "/tmp/snare.wav", 14));
    EXPECT_EQ(120.0f, st.first_position.x);
    EXPECT_EQ(64.0f, st.first_position.y);

    fl_app_tick(app, &st, dropped_files_frame);
    ASSERT_EQ_MSG((int)st.count, 0, "the drop belongs to one frame only");
    EXPECT_TRUE(st.first.data == nullptr);

    fl_app_destroy(app);
}

UTEST(input, typed_text_reaches_widget_under_app_tick_order) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("typed"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    TypedTextState st = { 0 };

    // Settle: let the widget register and focus propagate (signal.focused lags focused_id by a frame).
    for (int f = 0; f < 4; f++) {
        fl_app_tick(app, &st, typed_text_frame);
    }
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");

    // Type one character. inject_text queues it into the headless backend; the NEXT tick's pump drains it into
    // kb->text_input BEFORE fl_platform_input_update runs.
    fl_app_inject_text(app, FL_S("a"));
    fl_app_tick(app, &st, typed_text_frame);

    FlString got = ui_text_input_get_text(&st.input);
    ASSERT_EQ_MSG((int)got.length, 1, "the typed character reached the text input buffer");
    ASSERT_TRUE_MSG(got.length == 1 && got.data[0] == 'a', "buffered character is what was typed");

    fl_app_destroy(app);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ui_text_input reports text-changed, submit and cancel as one-frame edges on the returned signal.

typedef struct TextInputEdgesState {
    FlTextInputState input;
    FlSignal signal;
} TextInputEdgesState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void text_input_edges_frame(void* user_data) {
    TextInputEdgesState* st = (TextInputEdgesState*)user_data;
    FlUiCtx* ctx = ui_get_ctx();
    ctx->physical_keyboard_available = true;
    ctx->focused_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("edges_input"), .id = 0 });
    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {
        st->signal = ui_text_input(FL_S("edges_input"), &st->input, (FlTextInputConfig) { 0 });
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlApp* text_input_edges_app(TextInputEdgesState* st) {
    FlAppSettings settings = { .window_title = FL_S("edges"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    if (app) {
        for (int f = 0; f < 4; f++) {
            fl_app_tick(app, st, text_input_edges_frame);
        }
    }
    return app;
}

UTEST(input, text_input_reports_text_changed_on_the_edit_frame_only) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    TextInputEdgesState st = { 0 };
    FlApp* app = text_input_edges_app(&st);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    ASSERT_TRUE_MSG(st.signal.focused, "the field is focused after settling");
    EXPECT_FALSE(st.signal.text_changed);

    fl_app_inject_text(app, FL_S("a"));
    fl_app_tick(app, &st, text_input_edges_frame);
    EXPECT_TRUE(st.signal.text_changed);
    EXPECT_FALSE(st.signal.submitted);
    EXPECT_FALSE(st.signal.cancelled);

    fl_app_tick(app, &st, text_input_edges_frame);
    EXPECT_FALSE(st.signal.text_changed);

    fl_app_destroy(app);
}

UTEST(input, text_input_reports_submit_on_the_enter_frame_only) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    TextInputEdgesState st = { 0 };
    FlApp* app = text_input_edges_app(&st);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    fl_app_inject_text(app, FL_S("a"));
    fl_app_tick(app, &st, text_input_edges_frame);

    fl_app_inject_key(app, FlKey_Enter, true);
    fl_app_tick(app, &st, text_input_edges_frame);
    EXPECT_TRUE(st.signal.submitted);
    EXPECT_FALSE(st.signal.cancelled);
    EXPECT_FALSE(st.signal.text_changed);

    fl_app_tick(app, &st, text_input_edges_frame);
    EXPECT_FALSE(st.signal.submitted); // key still held

    fl_app_inject_key(app, FlKey_Enter, false);
    fl_app_tick(app, &st, text_input_edges_frame);
    EXPECT_FALSE(st.signal.submitted);

    fl_app_tick(app, &st, text_input_edges_frame);
    EXPECT_FALSE(st.signal.submitted);

    fl_app_destroy(app);
}

UTEST(input, text_input_typing_a_space_edits_the_text_and_does_not_submit) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    TextInputEdgesState st = { 0 };
    FlApp* app = text_input_edges_app(&st);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    fl_app_inject_key(app, FlKey_Space, true);
    fl_app_inject_text(app, FL_S(" "));
    fl_app_tick(app, &st, text_input_edges_frame);
    EXPECT_TRUE(st.signal.text_changed);
    EXPECT_FALSE(st.signal.submitted);
    EXPECT_FALSE(st.signal.cancelled);
    EXPECT_EQ((int)ui_text_input_get_text(&st.input).length, 1);

    fl_app_destroy(app);
}

UTEST(input, text_input_reports_cancel_on_the_escape_frame_only) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    TextInputEdgesState st = { 0 };
    FlApp* app = text_input_edges_app(&st);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    fl_app_inject_key(app, FlKey_Escape, true);
    fl_app_tick(app, &st, text_input_edges_frame);
    EXPECT_TRUE(st.signal.cancelled);
    EXPECT_FALSE(st.signal.submitted);

    fl_app_tick(app, &st, text_input_edges_frame);
    EXPECT_FALSE(st.signal.cancelled); // key still held

    fl_app_inject_key(app, FlKey_Escape, false);
    fl_app_tick(app, &st, text_input_edges_frame);
    EXPECT_FALSE(st.signal.cancelled);

    fl_app_tick(app, &st, text_input_edges_frame);
    EXPECT_FALSE(st.signal.cancelled);

    fl_app_destroy(app);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The same field, never aimed at: it is the frame's first focusable widget, so it is exactly what a focus
// auto-restore that seeds on an idle frame would hand the keyboard to.

typedef struct UnaimedTextState {
    FlUiCtx* ctx;
    FlTextInputState input;
} UnaimedTextState;

static void unaimed_text_frame(void* user_data) {
    UnaimedTextState* st = (UnaimedTextState*)user_data;
    st->ctx = ui_get_ctx();
    st->ctx->physical_keyboard_available = true;
    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {
        ui_text_input(FL_S("unaimed_input"), &st->input, (FlTextInputConfig) { 0 });
    }
}

UTEST(input, an_unaimed_text_input_consumes_nothing) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("unaimed"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    UnaimedTextState st = { 0 };

    for (int f = 0; f < 4; f++) {
        fl_app_tick(app, &st, unaimed_text_frame);
    }
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    ASSERT_EQ_MSG(st.ctx->focused_id, 0u, "an unaimed text input must not hold the keyboard ring");

    fl_app_inject_text(app, FL_S("a"));
    fl_app_tick(app, &st, unaimed_text_frame);

    FlString got = ui_text_input_get_text(&st.input);
    ASSERT_EQ_MSG((int)got.length, 0, "an unfocused text input must not swallow the typed character");

    fl_app_destroy(app);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fl_input_text_input_active reads whether a field had keyboard focus in the frame that ended last, the frame an app
// polling after the tick has just seen, and lets go of it once the field loses focus.

typedef struct EditingTextState {
    FlUiCtx* ctx;
    FlTextInputState input;
    bool aimed;
} EditingTextState;

static void editing_text_frame(void* user_data) {
    EditingTextState* st = (EditingTextState*)user_data;
    st->ctx = ui_get_ctx();
    st->ctx->physical_keyboard_available = true;
    if (st->aimed) {
        st->ctx->focused_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("editing_input"), .id = 0 });
    }
    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {
        ui_text_input(FL_S("editing_input"), &st->input, (FlTextInputConfig) { 0 });
    }
}

UTEST(input, text_input_active_follows_the_fields_keyboard_focus) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("editing"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    EditingTextState st = { 0 };
    for (int f = 0; f < 4; f++) {
        fl_app_tick(app, &st, editing_text_frame);
    }
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    ASSERT_FALSE_MSG(fl_input_text_input_active(), "an unaimed field is not being edited");

    st.aimed = true;
    for (int f = 0; f < 4; f++) {
        fl_app_tick(app, &st, editing_text_frame);
    }
    ASSERT_TRUE_MSG(fl_input_text_input_active(), "a focused field is being edited");

    st.aimed = false;
    st.ctx->focused_id = 0;
    for (int f = 0; f < 4; f++) {
        fl_app_tick(app, &st, editing_text_frame);
    }
    ASSERT_FALSE_MSG(fl_input_text_input_active(), "the flag outlived the field's focus");

    st.ctx->focused_id = 0;
    fl_app_destroy(app);
}

typedef struct PublicTextInputState {
    FlUiCtx* ctx;
    void* input;
    FlSignal signal;
} PublicTextInputState;

static void public_text_input_frame(void* user_data) {
    PublicTextInputState* st = (PublicTextInputState*)user_data;
    st->ctx = ui_get_ctx();
    st->ctx->physical_keyboard_available = true;
    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {
        st->signal = fl_text_input_show(FL_S("configured_input"), st->input, .placeholder = FL_S("type here"),
                                        .min_width = 150.0f, .max_length = 2,
                                        .widget = { .focusable = FlFocusable_Yes, .auto_focus = true });
        fl_button(FL_S("other_focus_target"), .focusable = FlFocusable_Yes);
    }
}

// The caret's position is measured through the active font, so this frame installs a real FreeType face:
// the built-in bitmap default reports the same metrics whatever size it is asked for, which would make the
// scale invisible here for reasons that have nothing to do with the code under test.
static FlFontId s_caret_font;

static float s_caret_min_width = 300.0f; // narrowed by the scrolling case so the text overflows its box

static void scaled_caret_frame(void* user_data) {
    PublicTextInputState* st = (PublicTextInputState*)user_data;
    st->ctx = ui_get_ctx();
    st->ctx->physical_keyboard_available = true;
    if (s_caret_font == 0) {
        s_caret_font
            = font_cache_load_font_memory(st->ctx->shared->font_cache, k_glyph_test_ttf, k_glyph_test_ttf_size, 16);
    }
    ui_font_set(s_caret_font);
    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {
        st->signal
            = fl_text_input_show(FL_S("caret_input"), st->input, .min_width = s_caret_min_width, .max_length = 64,
                                 .widget = { .focusable = FlFocusable_Yes, .auto_focus = true });
    }
}

typedef struct CaretRect {
    float x, y, height;
} CaretRect;

// The caret is the only rectangle in this frame two authored px wide, so its width identifies it. The blink
// hides it half the time; the caller ticks until it appears rather than trying to predict the phase. Returns
// false while it is hidden.
static bool find_text_caret(float expected_width, CaretRect* out) {
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (!ui_get_render_command(i, &cmd) || cmd.type != UiRenderCmd_Rectangle) {
            continue;
        }
        if (fabsf(cmd.width - expected_width) < 0.51f) {
            *out = (CaretRect) { .x = cmd.x, .y = cmd.y, .height = cmd.height };
            return true;
        }
    }
    return false;
}

static bool tick_to_caret(FlApp* app, PublicTextInputState* st, float expected_width, CaretRect* out) {
    for (int f = 0; f < 40; f++) {
        fl_app_tick(app, st, scaled_caret_frame);
        if (find_text_caret(expected_width, out)) {
            return true;
        }
    }
    return false;
}

// The text-input caret is a floating element configured straight through Clay rather than through
// fl_floating_configure, so it is the one spot in the widget where the scale has to be applied by hand - and
// it also measures how far along the string the caret sits, using a font size that is authored rather than
// pre-scaled. The scale arrives through fl_app_set_ui_zoom, the way a real app's does - pushing ui_set_scale
// directly would be overwritten by the tick loop's own composition.
UTEST(input, text_input_caret_follows_the_scale_with_the_text) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("caret scale"), .width = 800, .height = 200, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    s_caret_font = 0; // the frame callback installs the face on its first run
    s_caret_min_width = 300.0f;
    PublicTextInputState st = { .input = fl_text_input_create(64) };
    ASSERT_TRUE_MSG(st.input != nullptr, "opaque text-input state creation failed");
    // Only 'A' and 'o' are mapped in the embedded test face; unmapped glyphs measure zero and would
    // leave the caret sitting on the padding, where the assertion below would hold for free.
    fl_text_input_set_text(st.input, FL_S("AAAAAAAA"));

    for (int f = 0; f < 4; f++) {
        fl_app_tick(app, &st, scaled_caret_frame);
    }
    ASSERT_TRUE_MSG(st.signal.focused, "the caret only renders on a focused input");

    CaretRect at_1 = { 0 };
    ASSERT_TRUE_MSG(tick_to_caret(app, &st, 2.0f, &at_1), "the caret must render at identity scale");
    const float caret_at_1 = at_1.x;
    // Well past the padding: a zero-width measurement (unmapped glyphs, or a caret still at index 0) would
    // make the doubling assertion below pass on the padding alone, testing nothing.
    ASSERT_TRUE_MSG(caret_at_1 > 50.0f, "the caret sits several characters into the string, not on the padding");

    fl_app_set_ui_zoom(app, 2.0f);
    for (int f = 0; f < 4; f++) {
        fl_app_tick(app, &st, scaled_caret_frame);
    }

    CaretRect at_2 = { 0 };
    ASSERT_TRUE_MSG(tick_to_caret(app, &st, 4.0f, &at_2),
                    "the caret must still render at double scale, and at double its authored width");
    const float caret_at_2 = at_2.x;

    // Stated as a band, not an equality: glyph advances re-hint at the larger ppem, so eight of them at 32px
    // are not exactly twice eight at 16px (this face lands near 1.8x). The band still separates the two
    // outcomes cleanly - a caret measured at the authored size instead of the scaled one barely moves at all,
    // landing near 1.0x, which is nowhere close to the lower bound.
    ASSERT_TRUE_MSG(caret_at_2 > caret_at_1 * 1.6f,
                    "the caret's distance along the text grows with the scale, like the glyphs it follows");
    ASSERT_TRUE_MSG(caret_at_2 < caret_at_1 * 2.2f, "and does not overshoot, which would mean a double-scale");

    // The caret's own box, not just where it sits. Its height is the text height (type, re-hinted at the
    // larger ppem, hence a band again) plus an authored 4px nudge (chrome); its y is the authored vertical
    // centring inside the box. Both come from the same hand-applied scaling as the offset, and an unscaled
    // one leaves a 2x caret drawn at 1x proportions inside doubled chrome.
    ASSERT_TRUE_MSG(at_2.height > at_1.height * 1.6f, "the caret grows in height with the type it marks");
    ASSERT_TRUE_MSG(at_2.height < at_1.height * 2.3f, "without overshooting");
    ASSERT_TRUE_MSG(at_1.y > 0.0f, "the caret is centred inside the box, not pinned to its top edge");
    ASSERT_TRUE_MSG(at_2.y > at_1.y * 1.5f, "and its vertical centring tracks the scaled box");

    fl_app_set_ui_zoom(app, 1.0f);
    fl_text_input_destroy(st.input);
    fl_app_destroy(app);
}

// The scrolled regime, which the case above never enters: once the string outruns its box, the caret's
// position stops being "how far along the text am I" and becomes "where does the viewport end", because the
// scroll offset is clamped to text width - viewport. That makes this the branch where the viewport width
// and the 10px trigger margin have to be scaled - they are authored lengths being compared against a
// measurement already in screen coordinates. It also makes the assertion exact rather than a band: with the
// clamp saturated the caret lands on `inner_padding * scale + viewport * scale`, all layout constants, so
// the glyph re-hinting that forced a band above cancels out entirely.
UTEST(input, text_input_caret_stays_in_the_box_when_the_text_scrolls) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("caret scroll"), .width = 800, .height = 200, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    s_caret_font = 0;
    s_caret_min_width = 90.0f; // narrow enough that the string below overflows it at either scale
    PublicTextInputState st = { .input = fl_text_input_create(64) };
    ASSERT_TRUE_MSG(st.input != nullptr, "opaque text-input state creation failed");
    fl_text_input_set_text(st.input, FL_S("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")); // 30 mapped glyphs

    // The scroll offset eases toward its target (0.3 per frame), so let it settle before reading the caret;
    // a caret sampled mid-ease would drift with tick count rather than with the scale.
    for (int f = 0; f < 40; f++) {
        fl_app_tick(app, &st, scaled_caret_frame);
    }
    ASSERT_TRUE_MSG(st.signal.focused, "the caret only renders on a focused input");

    CaretRect scrolled_1 = { 0 };
    ASSERT_TRUE_MSG(tick_to_caret(app, &st, 2.0f, &scrolled_1), "the caret must render while scrolled");
    const float caret_at_1 = scrolled_1.x;
    ASSERT_TRUE_MSG(caret_at_1 > 0.0f && caret_at_1 <= 90.0f,
                    "a scrolled caret stays inside its own box rather than running off the end of the text");

    fl_app_set_ui_zoom(app, 2.0f);
    for (int f = 0; f < 40; f++) {
        fl_app_tick(app, &st, scaled_caret_frame);
    }

    CaretRect scrolled_2 = { 0 };
    ASSERT_TRUE_MSG(tick_to_caret(app, &st, 4.0f, &scrolled_2), "the caret must still render while scrolled at 2x");
    const float caret_at_2 = scrolled_2.x;
    ASSERT_TRUE_MSG(caret_at_2 > 0.0f && caret_at_2 <= 180.0f, "and stay inside the doubled box");

    // An unscaled viewport would clamp the scroll against roughly half the box it should, parking the caret
    // near its 1x position (about 1.1x) instead of tracking the box out to 2x.
    ASSERT_NEAR_MSG(caret_at_2, caret_at_1 * 2.0f, caret_at_1 * 2.0f * 0.05f,
                    "the scrolled caret tracks the scaled viewport edge, not the authored one");

    fl_app_set_ui_zoom(app, 1.0f);
    s_caret_min_width = 300.0f;
    fl_text_input_destroy(st.input);
    fl_app_destroy(app);
}

UTEST(input, public_configured_opaque_text_input) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings
        = { .window_title = FL_S("configured text input"), .width = 400, .height = 200, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    PublicTextInputState st = { .input = fl_text_input_create(8) };
    ASSERT_TRUE_MSG(st.input != nullptr, "opaque text-input state creation failed");
    fl_text_input_set_text(st.input, FL_S("x"));
    ASSERT_EQ_MSG((int)fl_text_input_get_text(st.input).length, 1, "opaque state set/get round trip");
    fl_text_input_clear(st.input);
    ASSERT_EQ_MSG((int)fl_text_input_get_text(st.input).length, 0, "opaque state clear round trip");

    for (int f = 0; f < 4; f++) {
        fl_app_tick(app, &st, public_text_input_frame);
    }
    ASSERT_TRUE_MSG(st.signal.focused, "configured auto-focus reaches the text-input widget");
    FlElementState element = ui_get_element_state((FlLayoutId) { .name = FL_S("configured_input"), .id = 0 });
    ASSERT_TRUE_MSG(element.found, "configured text input is present in layout state");
    ASSERT_TRUE_MSG(element.size.x > 140.0f && element.size.x <= 150.0f,
                    "configured minimum width reaches text-input layout");
    bool found_placeholder = false;
    for (int i = 0; i < ui_get_render_command_count(); i++) {
        UiRenderCommand cmd;
        if (ui_get_render_command(i, &cmd) && cmd.type == UiRenderCmd_Text && cmd.text.length == 9
            && memory_compare(cmd.text.data, "type here", 9) == 0) {
            found_placeholder = true;
            break;
        }
    }
    ASSERT_TRUE_MSG(found_placeholder, "configured placeholder reaches text rendering");

    st.ctx->focused_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("other_focus_target"), .id = 0 });
    for (int f = 0; f < 3; f++) {
        fl_app_tick(app, &st, public_text_input_frame);
    }
    ASSERT_FALSE_MSG(st.signal.focused, "configured auto-focus does not steal focus after its one-shot claim");

    st.ctx->focused_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("configured_input"), .id = 0 });
    for (int f = 0; f < 2; f++) {
        fl_app_tick(app, &st, public_text_input_frame);
    }
    fl_app_inject_text(app, FL_S("abc"));
    fl_app_tick(app, &st, public_text_input_frame);
    FlString got = fl_text_input_get_text(st.input);
    ASSERT_EQ_MSG((int)got.length, 2, "configured maximum length caps typed text");
    ASSERT_TRUE_MSG(got.data[0] == 'a' && got.data[1] == 'b', "maximum-length behavior preserves accepted prefix");

    fl_text_input_destroy(st.input);
    fl_app_destroy(app);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// ui_text_input_paste filters and truncates pasted text the way typed text is handled, and a selection it replaces
// frees its own capacity.

static void paste_text_state(FlTextInputState* state, const char* text, int sel_start, int sel_end) {
    ui_text_input_init(state, s_arena);
    ui_text_input_set_text(state, string_from_cstr_len(text, strlen(text)));
    STB_TexteditState* stb = (STB_TexteditState*)state->stb_state;
    stb->select_start = sel_start;
    stb->select_end = sel_end;
    stb->cursor = sel_end;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool paste_equals(const FlTextInputState* state, const char* expected) {
    FlString got = ui_text_input_get_text(state);
    return got.length == strlen(expected) && memcmp(got.data, expected, got.length) == 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(text_input, paste_drops_disallowed_characters) {
    FlTextInputState state;
    paste_text_state(&state, "", 0, 0);
    FlTextInputConfig cfg = { .allowed_chars = FL_S("0123456789") };

    ASSERT_TRUE(ui_text_input_paste(&state, &cfg, FL_S("1a2-3 b4")));
    ASSERT_TRUE_MSG(paste_equals(&state, "1234"), "only allow-listed characters are pasted");

    ASSERT_FALSE_MSG(ui_text_input_paste(&state, &cfg, FL_S("xyz")), "nothing allowed means nothing pasted");
    ASSERT_TRUE(paste_equals(&state, "1234"));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(text_input, paste_truncates_to_remaining_capacity) {
    FlTextInputState state;
    paste_text_state(&state, "ab", 2, 2);
    FlTextInputConfig cfg = { .max_length = 5 };

    ASSERT_TRUE(ui_text_input_paste(&state, &cfg, FL_S("123456789")));
    ASSERT_TRUE_MSG(paste_equals(&state, "ab123"), "the paste is cut to the room left, not dropped");

    ASSERT_FALSE_MSG(ui_text_input_paste(&state, &cfg, FL_S("zz")), "a full field takes nothing");
    ASSERT_TRUE(paste_equals(&state, "ab123"));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(text_input, paste_drops_non_ascii_bytes_like_typed_text) {
    FlTextInputState state;
    paste_text_state(&state, "", 0, 0);
    FlTextInputConfig cfg = { .max_length = 4 };

    ASSERT_TRUE(ui_text_input_paste(&state, &cfg,
                                    FL_S("ab\xC3\xA9\xC3\xA9"
                                         "cd")));
    ASSERT_TRUE_MSG(paste_equals(&state, "abcd"), "default filtering admits printable ASCII only");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(text_input, paste_replaces_the_selection_and_counts_its_room) {
    FlTextInputState state;
    paste_text_state(&state, "abcde", 1, 4); // "bcd" selected
    FlTextInputConfig cfg = { .max_length = 5, .allowed_chars = FL_S("0123456789") };

    ASSERT_TRUE(ui_text_input_paste(&state, &cfg, FL_S("1x2y3z4567")));
    ASSERT_TRUE_MSG(paste_equals(&state, "a123e"), "filtered, cut to the selection's room, and replacing it");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Hit testing is clipped. Clay culls against the screen, not against the clip rect, so an element that overflows
// its clip container still reaches the hit test carrying its full, uncut bounding box.

// 60x120 placed at y=140 inside a 200x200 clip box: 60px of it survive the clip, 60px are cut away.
static void hit_clip_emit(FlUiCtx* ctx) {
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() },
                   .padding = { .left = 40, .top = 30 }) {
        ui_layout_area_id(ID("hc_clip"), .sizing = { sizing_fixed(200), sizing_fixed(200) },
                          .color = ui_color(50, 50, 60, 255),
                          .clip = { .horizontal = true, .vertical = true, .manual_scroll = true }) {
            ui_layout_area_id(ID("hc_item"), .sizing = { sizing_fixed(60), sizing_fixed(120) },
                              .color = ui_color(40, 200, 120, 255),
                              .placed = { .enabled = true, .offset = { 20.0f, 140.0f } }) {
                ui_item_behavior(ID("hc_item"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
            // Cut away by the clip box entirely, while its own rect stays well inside the 400x400 window.
            ui_layout_area_id(ID("hc_hidden"), .sizing = { sizing_fixed(60), sizing_fixed(60) },
                              .color = ui_color(200, 60, 40, 255),
                              .placed = { .enabled = true, .offset = { 20.0f, 260.0f } }) {
                ui_item_behavior(ID("hc_hidden"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hit_clip, an_overflowing_element_takes_input_only_where_it_paints) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("hitclip"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = ui_get_ctx();
    ASSERT_TRUE_MSG(ctx != nullptr, "fl_app_create did not establish a global UI context");

    ctx->ui_scale = 1.0f; // sizing_fixed() multiplies by ui_scale; keep logical == physical for exact rects.
    ctx->layout_width = 400.0f;
    ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });

    hit_clip_emit(ctx);

    const u32 item_id = ui_layout_id_to_clay_id(ID("hc_item"));
    Clay_ElementData box = Clay_GetElementData((Clay_ElementId) { .id = ui_layout_id_to_clay_id(ID("hc_clip")) });
    Clay_ElementData item = Clay_GetElementData((Clay_ElementId) { .id = item_id });
    ASSERT_TRUE_MSG(box.found && item.found, "the clip container and its child must lay out");

    const f32 clip_bottom = box.boundingBox.y + box.boundingBox.height;
    const f32 item_bottom = item.boundingBox.y + item.boundingBox.height;
    ASSERT_GT_MSG(item_bottom, clip_bottom, "the child must overflow its clip container for this to test anything");
    ASSERT_LT_MSG(item.boundingBox.y, clip_bottom, "and part of it must survive the clip");

    const f32 x = item.boundingBox.x + item.boundingBox.width * 0.5f;

    ctx->input_state.mouse.x = x;
    ctx->input_state.mouse.y = (item.boundingBox.y + clip_bottom) * 0.5f;
    hit_clip_emit(ctx);
    ASSERT_EQ_MSG(item_id, ctx->hot_id, "over the part that survives the clip, the element is hot");

    ctx->input_state.mouse.y = (clip_bottom + item_bottom) * 0.5f;
    hit_clip_emit(ctx);
    ASSERT_NE_MSG(item_id, ctx->hot_id, "past the clip it paints nothing, so it must take no input either");

    // FlElementState.visible reads the same clip chain: "within the visible/clip region", not merely on screen.
    Clay_ElementData hidden_box
        = Clay_GetElementData((Clay_ElementId) { .id = ui_layout_id_to_clay_id(ID("hc_hidden")) });
    ASSERT_TRUE_MSG(hidden_box.found, "the fully clipped element must still lay out");
    ASSERT_GT_MSG(hidden_box.boundingBox.y, clip_bottom, "it must sit entirely past the clip");
    ASSERT_LT_MSG(hidden_box.boundingBox.y + hidden_box.boundingBox.height, ctx->layout_height,
                  "while its own rect stays on screen, where the screen test alone would call it visible");

    FlElementState hidden = ui_get_element_state(ID("hc_hidden"));
    FlElementState shown = ui_get_element_state(ID("hc_item"));
    ASSERT_TRUE_MSG(hidden.found && shown.found, "both elements must report state");
    ASSERT_FALSE_MSG(hidden.visible, "an element the clip chain cuts away entirely is not visible");
    ASSERT_TRUE_MSG(shown.visible, "an element that survives the clip in part stays visible");

    fl_app_destroy(app);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// An element with both a background and a clip gets two render commands under one id; its hit-test state must
// still advance once per frame, so the focus-gained edge survives.

static void hit_once_emit(FlUiCtx* ctx) {
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_layout_area_id(ID("ho_item"), .sizing = { sizing_fixed(100), sizing_fixed(100) },
                          .color = ui_color(50, 50, 60, 255),
                          .clip = { .horizontal = true, .vertical = true, .manual_scroll = true }) {
            ui_item_behavior(ID("ho_item"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hit_clip, a_background_and_clip_update_the_item_once_per_frame) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("hitonce"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = ui_get_ctx();
    ASSERT_TRUE_MSG(ctx != nullptr, "fl_app_create did not establish a global UI context");

    ctx->ui_scale = 1.0f;
    ctx->layout_width = 400.0f;
    ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });

    hit_once_emit(ctx);
    const u32 item_id = ui_layout_id_to_clay_id(ID("ho_item"));
    ItemState* item = hashmap_get(&ctx->shared->item_state, item_id);
    ASSERT_TRUE_MSG(item != nullptr, "the behavior call must create the item");
    ASSERT_FALSE(item->state.focused);

    const u64 pass_before = ctx->shared->hit_test_pass;
    ctx->focused_id = item_id;
    hit_once_emit(ctx);
    ASSERT_EQ_MSG(ctx->shared->hit_test_pass, pass_before + 1, "one hit-test pass per frame");
    ASSERT_EQ_MSG(item->hit_pass, ctx->shared->hit_test_pass, "the item was updated in that pass");
    ASSERT_TRUE_MSG(item->state.focused, "focus is picked up in the frame it is set");
    ASSERT_FALSE_MSG(item->was_focused, "the focus-gained edge must survive the element's second command");

    hit_once_emit(ctx);
    ASSERT_TRUE_MSG(item->was_focused, "the edge is consumed by the next frame's update");

    ctx->focused_id = 0;
    fl_app_destroy(app);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clay emits an overlay-color marker under the element's id with a zero box ahead of the element's own commands. The
// marker carries no geometry, so it must not stand in for the element's hit-test update.

static void hit_overlay_emit(FlUiCtx* ctx) {
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        fl_layout_start(ID("hov_item"));
        Clay__ConfigureOpenElement((Clay_ElementDeclaration) {
            .layout.sizing = { .width = CLAY_SIZING_FIXED(100), .height = CLAY_SIZING_FIXED(100) },
            .backgroundColor = { 50, 50, 60, 255 },
            .overlayColor = { 0, 0, 0, 100 },
        });
        ui_item_behavior(ID("hov_item"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
        fl_layout_end();
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(hit_clip, an_element_with_an_overlay_color_still_takes_hover) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("hitoverlay"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = ui_get_ctx();
    ASSERT_TRUE_MSG(ctx != nullptr, "fl_app_create did not establish a global UI context");

    ctx->ui_scale = 1.0f;
    ctx->layout_width = 400.0f;
    ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });

    hit_overlay_emit(ctx);
    const u32 item_id = ui_layout_id_to_clay_id(ID("hov_item"));
    Clay_ElementData box = Clay_GetElementData((Clay_ElementId) { .id = item_id });
    ASSERT_TRUE_MSG(box.found, "the overlaid element must lay out");

    ctx->input_state.mouse.x = box.boundingBox.x + box.boundingBox.width * 0.5f;
    ctx->input_state.mouse.y = box.boundingBox.y + box.boundingBox.height * 0.5f;
    hit_overlay_emit(ctx);
    ASSERT_EQ_MSG(item_id, ctx->hot_id, "the pointer over an overlaid element makes it hot");

    ctx->input_state.mouse.x = -10.0f;
    ctx->input_state.mouse.y = -10.0f;
    fl_app_destroy(app);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Opens levels unrestricting clip containers, then one that restricts to 200x100. Inside it: a further clip
// container, which is the push past the walk's stack capacity, and - after that one closes - an item tall enough to
// reach past the restricting clip's bottom edge.

static void hit_clip_deep_nest(int levels) {
    if (levels > 0) {
        ui_layout_area_id(ID_INT("hc_deep", (u32)levels), .sizing = { sizing_fixed(200), sizing_fixed(200) },
                          .clip = { .horizontal = true, .vertical = true, .manual_scroll = true }) {
            hit_clip_deep_nest(levels - 1);
        }
        return;
    }

    ui_layout_area_id(ID("hc_deep_limit"), .sizing = { sizing_fixed(200), sizing_fixed(100) },
                      .clip = { .horizontal = true, .vertical = true, .manual_scroll = true }) {
        ui_layout_area_id(ID("hc_deep_over"), .sizing = { sizing_fixed(200), sizing_fixed(200) },
                          .clip = { .horizontal = true, .vertical = true, .manual_scroll = true },
                          .placed = { .enabled = true, .offset = { 0.0f, 0.0f } }) {}
        ui_layout_area_id(ID("hc_deep_item"), .sizing = { sizing_fixed(60), sizing_fixed(120) },
                          .color = ui_color(40, 200, 120, 255),
                          .placed = { .enabled = true, .offset = { 20.0f, 40.0f } }) {
            ui_item_behavior(ID("hc_deep_item"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void hit_clip_deep_emit(FlUiCtx* ctx) {
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        hit_clip_deep_nest(31); // 31 + the restricting one == UI_HIT_CLIP_STACK_MAX, so the one inside it is dropped
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Past the clip stack's capacity a push is dropped and its matching pop cancels the drop rather than popping a clip
// that is still in force - the same balance the software renderer's scissor stack keeps
// (render.scissor_stack_deep_nesting_balanced).

UTEST(hit_clip, a_push_past_the_stack_capacity_does_not_pop_a_live_clip) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlAppSettings settings = { .window_title = FL_S("hitclipdeep"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    FlUiCtx* ctx = ui_get_ctx();
    ASSERT_TRUE_MSG(ctx != nullptr, "fl_app_create did not establish a global UI context");

    ctx->ui_scale = 1.0f;
    ctx->layout_width = 400.0f;
    ctx->layout_height = 400.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 400.0f, .height = 400.0f });

    hit_clip_deep_emit(ctx);

    const u32 item_id = ui_layout_id_to_clay_id(ID("hc_deep_item"));
    Clay_ElementData limit
        = Clay_GetElementData((Clay_ElementId) { .id = ui_layout_id_to_clay_id(ID("hc_deep_limit")) });
    Clay_ElementData item = Clay_GetElementData((Clay_ElementId) { .id = item_id });
    ASSERT_TRUE_MSG(limit.found && item.found, "the deep nest must lay out");

    const f32 limit_bottom = limit.boundingBox.y + limit.boundingBox.height;
    const f32 item_bottom = item.boundingBox.y + item.boundingBox.height;
    ASSERT_GT_MSG(item_bottom, limit_bottom, "the item must overflow the restricting clip");
    ASSERT_LT_MSG(item.boundingBox.y, limit_bottom, "and part of it must survive it");

    const f32 x = item.boundingBox.x + item.boundingBox.width * 0.5f;

    ctx->input_state.mouse.x = x;
    ctx->input_state.mouse.y = (item.boundingBox.y + limit_bottom) * 0.5f;
    hit_clip_deep_emit(ctx);
    ASSERT_EQ_MSG(item_id, ctx->hot_id, "control: inside the restricting clip the item is hot at this depth");

    ctx->input_state.mouse.y = (limit_bottom + item_bottom) * 0.5f;
    hit_clip_deep_emit(ctx);
    ASSERT_NE_MSG(item_id, ctx->hot_id,
                  "the dropped over-capacity push must not take the restricting clip off the stack with it");

    fl_app_destroy(app);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// localization: SID_ display text resolved against the default language's string table, loaded from the
// checked-in example manifest under examples/data/localization.

#include "ui/localization.h"
#include <flowi/ui/localization.h>

static FlVfsMount* localization_mount(void) {
    fl_vfs_init(s_arena);
    FlVfsMountResult mount = vfs_mount(FL_S(FLOWI_TEST_DATA_DIR "/localization"));
    if (mount.status != FlVfsMountErrorStatus_Success) {
        return nullptr;
    }
    vfs_wait_all();
    return mount.mount;
}

// Drive frame boundaries until nothing is in flight, the way an app tick loop does. Every read is built on a VFS
// worker, so a case that wants a loaded table has to reach it by ticking rather than by one call.
static void localization_settle(struct Localization* loc) {
    for (u32 i = 0; i < 100000; i++) {
        localization_frame_begin(loc);
        if (!localization_is_loading(loc)) {
            return;
        }
        sleep_us(200);
    }
}

// Init localization against the example data and tick until the default table is up.
static FlUiCtx* localization_fixture(FlVfsMount** out_mount, FlString manifest_path) {
    FlUiCtx* ctx = render_fixture_get();
    if (ctx == nullptr) {
        return nullptr;
    }
    *out_mount = localization_mount();
    if (*out_mount == nullptr) {
        return nullptr;
    }
    fl_localization_init(*out_mount, manifest_path, false);
    localization_settle(ctx->shared->localization);
    return ctx;
}

static void localization_fixture_release(FlUiCtx* ctx, FlVfsMount* mount) {
    localization_destroy(ctx->shared->localization);
    ctx->shared->localization = nullptr;
    if (mount != nullptr) {
        fl_vfs_mount_close(mount);
    }
}

// Emit one frame holding label as text inside an element identified by id_source, then serialize the tree.
// Returns the size the snapshot needed, so a caller can tell a truncated buffer from an absent string.
static int32_t localization_emit(FlUiCtx* ctx, FlString id_source, FlString label, char* snapshot,
                                 int32_t snapshot_size) {
    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        // A background colour is what makes the area emit a render command, which is what the inspector reads.
        ui_layout_area_id(resolve_widget_id(id_source, string_empty()),
                          .sizing = { sizing_fixed(200), sizing_fixed(40) }, .color = ui_color(40, 200, 60, 255)) {
            ui_text(label);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    return fl_inspector_snapshot(snapshot, snapshot_size);
}

UTEST(localization, sid_renders_the_default_table_translation) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    char snapshot[8192];
    localization_emit(ctx, FL_S("loc_probe"), FL_S("SID_settings_title"), snapshot, (int32_t)sizeof(snapshot));
    ASSERT_TRUE_MSG(strstr(snapshot, "Settings") != nullptr, "the SID must render as its default-table text");

    // Non-SID text is never looked up, so it reaches the tree byte-for-byte.
    localization_emit(ctx, FL_S("loc_probe"), FL_S("Plain label"), snapshot, (int32_t)sizeof(snapshot));
    ASSERT_TRUE_MSG(strstr(snapshot, "Plain label") != nullptr, "non-SID text passes through untouched");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, missing_key_renders_the_sid_literally) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(ctx->shared->localization, FL_S("SID_not_in_the_table")),
                                     FL_S("SID_not_in_the_table")),
                    "a key with no entry resolves to the key itself");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, a_plural_key_on_the_implicit_path_resolves_the_other_variant) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    // No count reaches the implicit path, so the key resolves to its "other" variant with the placeholder left
    // visible rather than to the SID literal.
    ASSERT_TRUE_MSG(
        fl_string_equals(localization_lookup(ctx->shared->localization, FL_S("SID_file_count")), FL_S("{0} files")),
        "a plural key with no count in hand renders its other variant");

    char snapshot[8192];
    localization_emit(ctx, FL_S("loc_probe"), FL_S("SID_file_count"), snapshot, (int32_t)sizeof(snapshot));
    ASSERT_TRUE_MSG(strstr(snapshot, "{0} files") != nullptr, "the widget path renders the same other variant");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, unreadable_manifest_disables_localization) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("no_such_manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    char snapshot[8192];
    localization_emit(ctx, FL_S("loc_probe"), FL_S("SID_settings_title"), snapshot, (int32_t)sizeof(snapshot));
    ASSERT_TRUE_MSG(strstr(snapshot, "SID_settings_title") != nullptr, "with no manifest every SID renders as itself");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, widget_identity_is_the_raw_sid) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    char snapshot[8192];
    localization_emit(ctx, FL_S("SID_settings_title"), FL_S("SID_settings_title"), snapshot, (int32_t)sizeof(snapshot));

    FlInspectorHit hit;
    ASSERT_TRUE_MSG(fl_inspector_hit_test_name(FL_S("SID_settings_title"), &hit),
                    "the element is still named by the raw SID after its text was translated");

    const u32 sid_id = ui_layout_id_to_clay_id(resolve_widget_id(FL_S("SID_settings_title"), string_empty()));
    const u32 translated_id = ui_layout_id_to_clay_id(resolve_widget_id(FL_S("Settings"), string_empty()));
    ASSERT_NE_MSG(sid_id, translated_id, "the id must not follow the display text");
    ASSERT_EQ_MSG(sid_id, hit.id, "the emitted element carries the raw SID's id, not the translation's");

    localization_fixture_release(ctx, mount);
}

// ui_app_frame_begin is where the startup barrier runs, so this case drives fl_app_tick.
static void localization_barrier_frame(void* user_data) {
    (void)user_data;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_text(FL_S("SID_settings_title"));
    }
}

UTEST(localization, startup_never_blocks_and_text_resolves_once_the_table_lands) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlVfsMount* mount = localization_mount();
    ASSERT_TRUE_MSG(mount != nullptr, "mounting the localization example data failed");

    FlAppSettings settings = { .window_title = FL_S("loc"), .width = 256, .height = 256, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlUiCtx* ctx = ui_get_ctx();
    fl_localization_init(mount, FL_S("manifest.json"), false);

    fl_app_tick(app, nullptr, localization_barrier_frame);

    // The tree is captured when the frame ends, so it is read here rather than inside the callback.
    char snapshot[8192];
    int32_t needed = fl_inspector_snapshot(snapshot, (int32_t)sizeof(snapshot));

    // The snapshot is read for an absence, so a truncated buffer would pass that check for the wrong reason.
    ASSERT_TRUE_MSG(needed > 0 && needed < (int32_t)sizeof(snapshot), "snapshot fits the buffer");
    // Whether the manifest and table have landed by the first frame is a race with the async read,
    // so the frame is only required to have rendered the SID one way or the other.
    ASSERT_TRUE_MSG(strstr(snapshot, "SID_settings_title") != nullptr || strstr(snapshot, "Settings") != nullptr,
                    "the first frame renders without waiting on the manifest or table read");

    bool resolved = false;
    for (u32 i = 0; i < 1000 && !resolved; i++) {
        fl_app_tick(app, nullptr, localization_barrier_frame);
        needed = fl_inspector_snapshot(snapshot, (int32_t)sizeof(snapshot));
        ASSERT_TRUE_MSG(needed > 0 && needed < (int32_t)sizeof(snapshot), "snapshot fits the buffer");
        resolved = strstr(snapshot, "Settings") != nullptr;
        ASSERT_TRUE_MSG(resolved || strstr(snapshot, "SID_settings_title") != nullptr,
                        "until the table lands the key keeps rendering as itself");
    }
    ASSERT_TRUE_MSG(resolved, "the table lands within a bounded number of frames and the text resolves");
    ASSERT_TRUE_MSG(strstr(snapshot, "SID_settings_title") == nullptr,
                    "the frame the table lands on shows the translation, not the key");

    localization_fixture_release(ctx, mount);
    fl_app_destroy(app);
}

UTEST(localization, unparseable_manifest_disables_localization) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // A manifest that reads fine but is not JSON takes the parse-failure branch, distinct from the
    // file-not-found branch above.
    FlArena* arena = arena_new();
    FlString dir_path = file_build_path_os(arena, get_current_directory_os(arena), FL_S("unit_test_localization_tmp"));
    file_remove_directory_recursive_os(dir_path); // clean any leftover from an aborted run
    ASSERT_TRUE(file_create_directory_os(dir_path));

    const u8 broken[] = "{ this is not json";
    FlString manifest_path = file_build_path_os(arena, dir_path, FL_S("manifest.json"));
    ASSERT_EQ((int)sizeof(broken) - 1, (int)file_write_os(manifest_path, broken, (i64)sizeof(broken) - 1));

    fl_vfs_init(s_arena);
    FlVfsMountResult mount = vfs_mount(dir_path);
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "the scratch directory should mount through LocalFS");
    vfs_wait_all();

    fl_localization_init(mount.mount, FL_S("manifest.json"), false);
    localization_settle(ctx->shared->localization);

    char snapshot[8192];
    localization_emit(ctx, FL_S("loc_probe"), FL_S("SID_settings_title"), snapshot, (int32_t)sizeof(snapshot));
    ASSERT_TRUE_MSG(strstr(snapshot, "SID_settings_title") != nullptr,
                    "an unparseable manifest must leave every SID rendering as itself");

    localization_fixture_release(ctx, mount.mount);
    file_remove_directory_recursive_os(dir_path);
    fl_arena_destroy(arena);
}

// Requests a Language Switch from between two SID emissions, so a frame that applied the switch while rendering
// would come out with one key in each language.
static bool s_localization_switch_requested;

static void localization_switch_frame(void* user_data) {
    (void)user_data;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_text(FL_S("SID_settings_title"));
        if (!s_localization_switch_requested) {
            fl_localization_set_language(FL_S("de"));
            s_localization_switch_requested = true;
        }
        ui_text(FL_S("SID_greeting"));
    }
}

UTEST(localization, a_switch_applies_at_the_frame_boundary) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlVfsMount* mount = localization_mount();
    ASSERT_TRUE_MSG(mount != nullptr, "mounting the localization example data failed");

    FlAppSettings settings = { .window_title = FL_S("locswitch"), .width = 256, .height = 256, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlUiCtx* ctx = ui_get_ctx();
    fl_localization_init(mount, FL_S("manifest.json"), false);
    localization_settle(ctx->shared->localization);
    s_localization_switch_requested = false;

    char snapshot[8192];
    fl_app_tick(app, nullptr, localization_switch_frame);
    int32_t needed = fl_inspector_snapshot(snapshot, (int32_t)sizeof(snapshot));
    ASSERT_TRUE_MSG(needed > 0 && needed < (int32_t)sizeof(snapshot), "snapshot fits the buffer");
    ASSERT_TRUE_MSG(strstr(snapshot, "Settings") != nullptr && strstr(snapshot, "Hello,") != nullptr,
                    "the frame that requested the switch renders entirely in the old language");
    ASSERT_TRUE_MSG(strstr(snapshot, "Einstellungen") == nullptr && strstr(snapshot, "Hallo,") == nullptr,
                    "no part of that frame may already be in the new language");

    // The table loads on a worker, so the swap lands at whichever later boundary finds it ready. Every frame up
    // to that one is entirely in the old language, and the one that flips is entirely in the new.
    bool switched = false;
    for (u32 i = 0; i < 1000 && !switched; i++) {
        fl_app_tick(app, nullptr, localization_switch_frame);
        needed = fl_inspector_snapshot(snapshot, (int32_t)sizeof(snapshot));
        ASSERT_TRUE_MSG(needed > 0 && needed < (int32_t)sizeof(snapshot), "snapshot fits the buffer");

        switched = strstr(snapshot, "Einstellungen") != nullptr;
        if (switched) {
            ASSERT_TRUE_MSG(strstr(snapshot, "Hallo,") != nullptr,
                            "the frame that switches renders entirely in the new language");
            ASSERT_TRUE_MSG(strstr(snapshot, "Settings") == nullptr && strstr(snapshot, "Hello,") == nullptr,
                            "nothing is left over from the old language");
        } else {
            ASSERT_TRUE_MSG(strstr(snapshot, "Settings") != nullptr && strstr(snapshot, "Hello,") != nullptr
                                && strstr(snapshot, "Hallo,") == nullptr,
                            "a frame with the switch still in flight renders entirely in the old language");
        }
    }
    ASSERT_TRUE_MSG(switched, "the switch must land within a bounded number of frames");

    localization_fixture_release(ctx, mount);
    fl_app_destroy(app);
}

UTEST(localization, set_language_during_setup_selects_the_initial_language) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    FlVfsMount* mount = localization_mount();
    ASSERT_TRUE_MSG(mount != nullptr, "mounting the localization example data failed");

    FlAppSettings settings = { .window_title = FL_S("locinit"), .width = 256, .height = 256, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlUiCtx* ctx = ui_get_ctx();
    fl_localization_init(mount, FL_S("manifest.json"), false);
    fl_localization_set_language(FL_S("de"));

    // The tag is recorded before localization is even ready, so it waits for the manifest and default table and
    // then applies; the ticks in between show the key itself or the default language, never a mix.
    char snapshot[8192];
    bool selected = false;
    for (u32 i = 0; i < 1000 && !selected; i++) {
        fl_app_tick(app, nullptr, localization_barrier_frame);
        const int32_t needed = fl_inspector_snapshot(snapshot, (int32_t)sizeof(snapshot));
        ASSERT_TRUE_MSG(needed > 0 && needed < (int32_t)sizeof(snapshot), "snapshot fits the buffer");
        selected = strstr(snapshot, "Einstellungen") != nullptr;
    }
    ASSERT_TRUE_MSG(selected, "a tag set before the first tick selects the initial language once it lands");
    ASSERT_TRUE_MSG(strstr(snapshot, "Settings") == nullptr && strstr(snapshot, "SID_settings_title") == nullptr,
                    "the frame it lands on is entirely in the requested language");

    localization_fixture_release(ctx, mount);
    fl_app_destroy(app);
}

UTEST(localization, a_key_missing_from_the_active_table_falls_back_to_the_default) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    struct Localization* loc = ctx->shared->localization;
    fl_localization_set_language(FL_S("de"));
    localization_settle(loc);

    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S_UTF8("Einstellungen")),
                    "a key the active table carries resolves from it");
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_untranslated")), FL_S("Untranslated")),
                    "a key only the default table carries resolves from the fallback layer");
    ASSERT_TRUE_MSG(
        fl_string_equals(localization_lookup(loc, FL_S("SID_not_in_the_table")), FL_S("SID_not_in_the_table")),
        "a key neither table carries renders as the key itself");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, a_switch_retires_the_replaced_table_and_keeps_the_default_resident) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    struct Localization* loc = ctx->shared->localization;
    ASSERT_EQ_MSG((u32)1, localization_resident_table_count(loc), "the default language alone is resident at startup");

    fl_localization_set_language(FL_S("de"));
    localization_settle(loc);
    ASSERT_EQ_MSG((u32)2, localization_resident_table_count(loc),
                  "a non-default language adds its table beside the default");

    fl_localization_set_language(FL_S("pt-br"));
    localization_settle(loc);
    ASSERT_EQ_MSG((u32)2, localization_resident_table_count(loc),
                  "the table the switch replaced is retired rather than accumulated");
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S_UTF8("Configurações")),
                    "the newly loaded table is the one lookups resolve against");

    // Also covers case-insensitive tag matching: the manifest spells this one "en".
    fl_localization_set_language(FL_S("EN"));
    localization_settle(loc);
    ASSERT_EQ_MSG((u32)1, localization_resident_table_count(loc),
                  "switching back to the default leaves it as the only resident table");
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S("Settings")),
                    "the default is active again");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, a_failed_switch_keeps_the_current_language) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    struct Localization* loc = ctx->shared->localization;
    fl_localization_set_language(FL_S("de"));
    localization_settle(loc);

    fl_localization_set_language(FL_S("zz-not-a-language"));
    localization_settle(loc);

    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S_UTF8("Einstellungen")),
                    "a tag the manifest does not list leaves the active language untouched");
    ASSERT_EQ_MSG((u32)2, localization_resident_table_count(loc), "the failed load leaves no table behind");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, an_in_flight_switch_keeps_the_old_language_until_it_lands) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    struct Localization* loc = ctx->shared->localization;
    fl_localization_set_language(FL_S("de"));

    u32 frames = 0;
    for (; frames < 100000; frames++) {
        localization_frame_begin(loc);
        if (!localization_is_loading(loc)) {
            break;
        }
        ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S("Settings")),
                        "a frame with the table still loading resolves entirely against the old language");
        ASSERT_EQ_MSG((u32)1, localization_resident_table_count(loc),
                      "a table still being built on the worker is not resident");
        sleep_us(200);
    }

    ASSERT_TRUE_MSG(frames > 0, "the read is started at a frame boundary, so at least one frame sees it in flight");
    ASSERT_TRUE_MSG(frames < 100000, "the switch must land within a bounded number of frames");
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S_UTF8("Einstellungen")),
                    "the boundary that finds the table ready is the one that swaps it in");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, a_tag_set_while_a_switch_is_in_flight_is_applied_after_it) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    struct Localization* loc = ctx->shared->localization;
    fl_localization_set_language(FL_S("de"));
    localization_frame_begin(loc);
    ASSERT_TRUE_MSG(localization_is_loading(loc), "the boundary that starts the read leaves it in flight");

    fl_localization_set_language(FL_S("pt-br"));

    bool landed = false;
    for (u32 i = 0; i < 100000 && !landed; i++) {
        localization_frame_begin(loc);
        const FlString text = localization_lookup(loc, FL_S("SID_settings_title"));
        ASSERT_TRUE_MSG(!fl_string_equals(text, FL_S_UTF8("Configurações")),
                        "a tag set during a switch never overtakes the table already loading");
        landed = fl_string_equals(text, FL_S_UTF8("Einstellungen"));
        sleep_us(200);
    }
    ASSERT_TRUE_MSG(landed, "the switch that was in flight is the one that lands first");

    localization_settle(loc);
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S_UTF8("Configurações")),
                    "the tag queued behind it is applied once the first switch is done, not dropped");
    ASSERT_EQ_MSG((u32)2, localization_resident_table_count(loc),
                  "the table the queued switch replaced is retired rather than accumulated");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, a_switch_still_in_flight_at_teardown_is_waited_out) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    struct Localization* loc = ctx->shared->localization;
    fl_localization_set_language(FL_S("de"));
    localization_frame_begin(loc);
    ASSERT_TRUE_MSG(localization_is_loading(loc), "the boundary that starts the read leaves it in flight");

    // Teardown has to wait out the worker before retiring the arena it may still be writing into, so this case
    // fails as a hang or a use-after-free rather than an assertion.
    localization_fixture_release(ctx, mount);
}

UTEST(localization, a_table_the_worker_cannot_build_keeps_the_current_language) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // Both worker-side failure modes behind a manifest that is itself fine: a table file that is not JSON, and one
    // the manifest names but that does not exist.
    FlArena* arena = arena_new();
    FlString dir_path
        = file_build_path_os(arena, get_current_directory_os(arena), FL_S("unit_test_localization_badtable"));
    file_remove_directory_recursive_os(dir_path); // clean any leftover from an aborted run
    ASSERT_TRUE(file_create_directory_os(dir_path));

    const u8 manifest[] = "{\"version\":1,\"default\":\"en\","
                          "\"languages\":[{\"tag\":\"en\",\"file\":\"en.json\"},"
                          "{\"tag\":\"broken\",\"file\":\"broken.json\"},"
                          "{\"tag\":\"gone\",\"file\":\"gone.json\"}]}";
    FlString manifest_path = file_build_path_os(arena, dir_path, FL_S("manifest.json"));
    ASSERT_EQ((int)sizeof(manifest) - 1, (int)file_write_os(manifest_path, manifest, (i64)sizeof(manifest) - 1));

    const u8 en[] = "{\"version\":1,\"strings\":{\"SID_settings_title\":\"Settings\"}}";
    FlString en_path = file_build_path_os(arena, dir_path, FL_S("en.json"));
    ASSERT_EQ((int)sizeof(en) - 1, (int)file_write_os(en_path, en, (i64)sizeof(en) - 1));

    const u8 broken[] = "{ this is not json";
    FlString broken_path = file_build_path_os(arena, dir_path, FL_S("broken.json"));
    ASSERT_EQ((int)sizeof(broken) - 1, (int)file_write_os(broken_path, broken, (i64)sizeof(broken) - 1));

    fl_vfs_init(s_arena);
    FlVfsMountResult mount = vfs_mount(dir_path);
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "the scratch directory should mount through LocalFS");
    vfs_wait_all();

    fl_localization_init(mount.mount, FL_S("manifest.json"), false);
    struct Localization* loc = ctx->shared->localization;
    localization_frame_begin(loc);

    fl_localization_set_language(FL_S("broken"));
    localization_settle(loc);
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S("Settings")),
                    "a table the worker cannot parse leaves the active language untouched");
    ASSERT_EQ_MSG((u32)1, localization_resident_table_count(loc), "the half-built table is retired, not kept");

    fl_localization_set_language(FL_S("gone"));
    localization_settle(loc);
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S("Settings")),
                    "a table file the manifest names but that is missing leaves the active language untouched");
    ASSERT_EQ_MSG((u32)1, localization_resident_table_count(loc), "a failed read leaves no table behind");

    localization_fixture_release(ctx, mount.mount);
    file_remove_directory_recursive_os(dir_path);
    fl_arena_destroy(arena);
}

// One clickable, focusable element labelled by a SID, sized and placed so the mouse the caller parks at (100, 20)
// lands inside it.
static void localization_interactive_emit(FlUiCtx* ctx) {
    ctx->layout_width = 256.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 256.0f, .height = 256.0f });

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_layout_area_id(resolve_widget_id(FL_S("SID_settings_title"), string_empty()),
                          .sizing = { sizing_fixed(200), sizing_fixed(40) }, .color = ui_color(40, 200, 60, 255)) {
            ui_item_behavior(resolve_widget_id(FL_S("SID_settings_title"), string_empty()),
                             FlItemFlags_clickable | FlItemFlags_focusable, (FlWidgetConfig) { 0 });
            ui_text(FL_S("SID_settings_title"));
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(localization, hover_and_focus_survive_a_language_switch) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    const u32 sid_id = ui_layout_id_to_clay_id(resolve_widget_id(FL_S("SID_settings_title"), string_empty()));
    ctx->input_state.mouse.x = 100.0f;
    ctx->input_state.mouse.y = 20.0f;

    localization_interactive_emit(ctx);
    fl_set_focus_id(sid_id);
    localization_interactive_emit(ctx);
    ASSERT_EQ_MSG(sid_id, ctx->hot_id, "the element under the cursor is hot before the switch");
    ASSERT_EQ_MSG(sid_id, fl_focused_id(), "the element holds focus before the switch");

    fl_localization_set_language(FL_S("de"));
    localization_settle(ctx->shared->localization);
    localization_interactive_emit(ctx);

    char snapshot[8192];
    fl_inspector_snapshot(snapshot, (int32_t)sizeof(snapshot));
    ASSERT_TRUE_MSG(strstr(snapshot, "Einstellungen") != nullptr, "the switch did reach the display text");
    ASSERT_EQ_MSG(sid_id, ctx->hot_id, "the id hashes the raw SID, so hover outlives the switch");
    ASSERT_EQ_MSG(sid_id, fl_focused_id(), "and so does focus: the focus ring never sees the translation");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, an_unusable_tag_is_rejected_and_the_last_tag_set_wins) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    struct Localization* loc = ctx->shared->localization;
    fl_localization_set_language(string_empty());
    // 64 characters, one over what the pending-tag buffer holds.
    fl_localization_set_language(FL_S("0123456789012345678901234567890123456789012345678901234567890123"));
    localization_settle(loc);
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S("Settings")),
                    "a tag that is empty or too long is rejected outright, never recorded as pending");
    ASSERT_EQ_MSG((u32)1, localization_resident_table_count(loc), "a rejected tag loads nothing");

    // One pending slot, not a queue: only the last tag set before the boundary is applied.
    fl_localization_set_language(FL_S("pt-br"));
    fl_localization_set_language(FL_S("de"));
    localization_settle(loc);
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S_UTF8("Einstellungen")),
                    "the last tag set before the frame boundary is the one that takes effect");
    ASSERT_EQ_MSG((u32)2, localization_resident_table_count(loc), "the tag it superseded was never loaded");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, a_default_table_that_fails_to_load_leaves_no_fallback_layer) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // A manifest that parses fine but names a default whose file is absent. The de table beside it is real, so the
    // run can still put an active language on top of the missing fallback layer.
    FlArena* arena = arena_new();
    FlString dir_path
        = file_build_path_os(arena, get_current_directory_os(arena), FL_S("unit_test_localization_nodef"));
    file_remove_directory_recursive_os(dir_path); // clean any leftover from an aborted run
    ASSERT_TRUE(file_create_directory_os(dir_path));

    const u8 manifest[]
        = "{\"version\":1,\"default\":\"en\","
          "\"languages\":[{\"tag\":\"en\",\"file\":\"en.json\"},{\"tag\":\"de\",\"file\":\"de.json\"}]}";
    FlString manifest_path = file_build_path_os(arena, dir_path, FL_S("manifest.json"));
    ASSERT_EQ((int)sizeof(manifest) - 1, (int)file_write_os(manifest_path, manifest, (i64)sizeof(manifest) - 1));

    const u8 de[] = "{\"version\":1,\"strings\":{\"SID_settings_title\":\"Einstellungen\"}}";
    FlString de_path = file_build_path_os(arena, dir_path, FL_S("de.json"));
    ASSERT_EQ((int)sizeof(de) - 1, (int)file_write_os(de_path, de, (i64)sizeof(de) - 1));

    fl_vfs_init(s_arena);
    FlVfsMountResult mount = vfs_mount(dir_path);
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "the scratch directory should mount through LocalFS");
    vfs_wait_all();

    fl_localization_init(mount.mount, FL_S("manifest.json"), false);
    struct Localization* loc = ctx->shared->localization;
    localization_settle(loc);

    ASSERT_EQ_MSG((u32)0, localization_resident_table_count(loc),
                  "a default table that failed to load is not resident");
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S("SID_settings_title")),
                    "with no fallback layer every SID renders as itself rather than crashing");

    fl_localization_set_language(FL_S("de"));
    localization_settle(loc);
    ASSERT_EQ_MSG((u32)1, localization_resident_table_count(loc),
                  "a language switched in afterwards is the only resident table");
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S_UTF8("Einstellungen")),
                    "the active language still works without a fallback layer beneath it");

    localization_fixture_release(ctx, mount.mount);
    file_remove_directory_recursive_os(dir_path);
    fl_arena_destroy(arena);
}

UTEST(localization, a_default_tag_the_manifest_does_not_list_never_starts_a_table_read) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // The default names a tag absent from the language list, so no table read is ever started and localization
    // reaches its ready state without passing through a default-table load at all.
    FlArena* arena = arena_new();
    FlString dir_path
        = file_build_path_os(arena, get_current_directory_os(arena), FL_S("unit_test_localization_notag"));
    file_remove_directory_recursive_os(dir_path); // clean any leftover from an aborted run
    ASSERT_TRUE(file_create_directory_os(dir_path));

    const u8 manifest[] = "{\"version\":1,\"default\":\"en\","
                          "\"languages\":[{\"tag\":\"de\",\"file\":\"de.json\"}]}";
    FlString manifest_path = file_build_path_os(arena, dir_path, FL_S("manifest.json"));
    ASSERT_EQ((int)sizeof(manifest) - 1, (int)file_write_os(manifest_path, manifest, (i64)sizeof(manifest) - 1));

    const u8 de[] = "{\"version\":1,\"strings\":{\"SID_settings_title\":\"Einstellungen\"}}";
    FlString de_path = file_build_path_os(arena, dir_path, FL_S("de.json"));
    ASSERT_EQ((int)sizeof(de) - 1, (int)file_write_os(de_path, de, (i64)sizeof(de) - 1));

    fl_vfs_init(s_arena);
    FlVfsMountResult mount = vfs_mount(dir_path);
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "the scratch directory should mount through LocalFS");
    vfs_wait_all();

    fl_localization_init(mount.mount, FL_S("manifest.json"), false);
    struct Localization* loc = ctx->shared->localization;
    localization_settle(loc);

    ASSERT_EQ_MSG((u32)0, localization_resident_table_count(loc), "an unlistable default resolves to no table");
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S("SID_settings_title")),
                    "every SID renders as itself while no table is resident");

    fl_localization_set_language(FL_S("de"));
    localization_settle(loc);
    ASSERT_TRUE_MSG(fl_string_equals(localization_lookup(loc, FL_S("SID_settings_title")), FL_S_UTF8("Einstellungen")),
                    "localization is ready enough for a switch even though the default never loaded");

    localization_fixture_release(ctx, mount.mount);
    file_remove_directory_recursive_os(dir_path);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fl_localize_fmt: lookup plus indexed-placeholder substitution.

static FlFmtArg fmt_int(i64 value) {
    return (FlFmtArg) { .value.int_value = value, .kind = FlFmtArgKind_Int };
}

static FlFmtArg fmt_float(f64 value) {
    return (FlFmtArg) { .value.float_value = value, .kind = FlFmtArgKind_Float };
}

static FlFmtArg fmt_str(FlString value) {
    return (FlFmtArg) { .value.string_value = value, .kind = FlFmtArgKind_Str };
}

UTEST(localization, fmt_substitutes_indexed_placeholders) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    const FlFmtArg one[] = { fmt_str(FL_S("Ada")) };
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_fmt(FL_S("SID_greeting"), one, 1), FL_S("Hello, Ada!")),
                    "a single in-order slot takes the first argument");

    const FlFmtArg two[] = { fmt_str(FL_S("first")), fmt_str(FL_S("second")) };
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_fmt(FL_S("SID_fmt_reordered"), two, 2), FL_S("second then first")),
                    "slots address arguments by index, in whatever order the translation puts them");

    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_fmt(FL_S("SID_fmt_repeated"), two, 2), FL_S("first, first and first")),
                    "a repeated index substitutes at every occurrence");

    // An argument no slot names is simply unused, not an error.
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_fmt(FL_S("SID_greeting"), two, 2), FL_S("Hello, first!")),
                    "arguments past the slots the text uses are ignored");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, fmt_renders_doubled_braces_as_literals) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    const FlFmtArg args[] = { fmt_str(FL_S("the value")) };
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_fmt(FL_S("SID_fmt_escaped"), args, 1), FL_S("{0} shows the value")),
                    "{{ and }} escape to one brace each and never open a slot");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, fmt_leaves_a_placeholder_with_no_argument_visible) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    const FlFmtArg args[] = { fmt_str(FL_S("two")) };
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_fmt(FL_S("SID_fmt_two_slots"), args, 1), FL_S("have two of {1}")),
                    "a slot past the end of the argument list renders as itself");
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_fmt(FL_S("SID_greeting"), nullptr, 0), FL_S("Hello, {0}!")),
                    "a parameterized key called with no arguments keeps every placeholder visible");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, fmt_formats_numbers_c_style) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    const FlFmtArg args[] = { fmt_int(-1000000), fmt_float(2.5) };
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_fmt(FL_S("SID_fmt_two_slots"), args, 2), FL_S("have -1000000 of 2.5")),
                    "integers and floats format plainly: a . decimal point and no digit grouping");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, fmt_returns_unparameterized_text_without_copying_it) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    const FlString first = fl_localize_fmt(FL_S("SID_settings_title"), nullptr, 0);
    const FlString second = fl_localize_fmt(FL_S("SID_settings_title"), nullptr, 0);
    ASSERT_TRUE_MSG(fl_string_equals(first, FL_S("Settings")), "text with no placeholders resolves like any other key");
    ASSERT_TRUE_MSG(first.data == second.data,
                    "and is returned as the table's own bytes, so no scan or frame-arena copy happened");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, a_parameterized_key_on_the_implicit_path_shows_its_placeholders) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    char snapshot[8192];
    const int32_t needed
        = localization_emit(ctx, FL_S("loc_probe"), FL_S("SID_greeting"), snapshot, (int32_t)sizeof(snapshot));
    ASSERT_TRUE_MSG(needed > 0 && needed < (int32_t)sizeof(snapshot), "snapshot fits the buffer");
    ASSERT_TRUE_MSG(strstr(snapshot, "Hello, {0}!") != nullptr,
                    "a widget reaching a parameterized key without arguments renders the placeholders");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, fmt_leaves_text_it_cannot_substitute_as_written) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_fmt(FL_S("SID_fmt_stray_braces"), nullptr, 0), FL_S("a } b {c")),
                    "a closing brace that opened nothing, and a slot nothing closes, are both literal text");

    const FlFmtArg unknown[] = { { .value.int_value = 7, .kind = (FlFmtArgKind)99 } };
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_fmt(FL_S("SID_greeting"), unknown, 1), FL_S("Hello, {0}!")),
                    "an argument whose kind names no arm of the union counts as no argument at all");

    localization_fixture_release(ctx, mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fl_localize_plural: built-in variant selection, then the same substitution with the count as {0}.

UTEST(localization, plural_rules_select_per_language_family) {
    // Germanic and Romance: 1 alone is "one".
    ASSERT_EQ((int)LocalizationPlural_One, (int)localization_plural_category(FL_S("en"), 1));
    ASSERT_EQ((int)LocalizationPlural_Other, (int)localization_plural_category(FL_S("en"), 0));
    ASSERT_EQ((int)LocalizationPlural_Other, (int)localization_plural_category(FL_S("de"), 2));
    ASSERT_EQ((int)LocalizationPlural_Other, (int)localization_plural_category(FL_S("sv"), 21));

    // French-style: 0 joins 1 in "one".
    ASSERT_EQ((int)LocalizationPlural_One, (int)localization_plural_category(FL_S("fr"), 0));
    ASSERT_EQ((int)LocalizationPlural_One, (int)localization_plural_category(FL_S("fr"), 1));
    ASSERT_EQ((int)LocalizationPlural_Other, (int)localization_plural_category(FL_S("fr"), 2));

    // Only the primary subtag decides, so a region carries its language's rules.
    ASSERT_EQ((int)LocalizationPlural_One, (int)localization_plural_category(FL_S("pt-br"), 0));
    ASSERT_EQ((int)LocalizationPlural_One, (int)localization_plural_category(FL_S("PT-BR"), 0));

    // Slavic: the last digit picks the category and the 11-14 band overrides it.
    ASSERT_EQ((int)LocalizationPlural_One, (int)localization_plural_category(FL_S("ru"), 1));
    ASSERT_EQ((int)LocalizationPlural_One, (int)localization_plural_category(FL_S("ru"), 21));
    ASSERT_EQ((int)LocalizationPlural_One, (int)localization_plural_category(FL_S("ru"), 101));
    ASSERT_EQ((int)LocalizationPlural_Few, (int)localization_plural_category(FL_S("ru"), 2));
    ASSERT_EQ((int)LocalizationPlural_Few, (int)localization_plural_category(FL_S("ru"), 4));
    ASSERT_EQ((int)LocalizationPlural_Few, (int)localization_plural_category(FL_S("uk"), 22));
    ASSERT_EQ((int)LocalizationPlural_Many, (int)localization_plural_category(FL_S("ru"), 0));
    ASSERT_EQ((int)LocalizationPlural_Many, (int)localization_plural_category(FL_S("ru"), 5));
    ASSERT_EQ((int)LocalizationPlural_Many, (int)localization_plural_category(FL_S("ru"), 11));
    ASSERT_EQ((int)LocalizationPlural_Many, (int)localization_plural_category(FL_S("ru"), 12));
    ASSERT_EQ((int)LocalizationPlural_Many, (int)localization_plural_category(FL_S("ru"), 14));
    ASSERT_EQ((int)LocalizationPlural_Many, (int)localization_plural_category(FL_S("ru"), 111));

    // CJK: every count is "other".
    ASSERT_EQ((int)LocalizationPlural_Other, (int)localization_plural_category(FL_S("ja"), 0));
    ASSERT_EQ((int)LocalizationPlural_Other, (int)localization_plural_category(FL_S("zh"), 1));
    ASSERT_EQ((int)LocalizationPlural_Other, (int)localization_plural_category(FL_S("ko"), 2));

    // A language with no built-in rules selects "other" the same way, rather than borrowing a neighbour's.
    ASSERT_EQ((int)LocalizationPlural_Other, (int)localization_plural_category(FL_S("ar"), 1));
    ASSERT_EQ((int)LocalizationPlural_Other, (int)localization_plural_category(FL_S("pl"), 2));
    ASSERT_EQ((int)LocalizationPlural_Other, (int)localization_plural_category(string_empty(), 1));

    // A negative count selects as its magnitude does.
    ASSERT_EQ((int)LocalizationPlural_One, (int)localization_plural_category(FL_S("en"), -1));
    ASSERT_EQ((int)LocalizationPlural_Few, (int)localization_plural_category(FL_S("ru"), -22));
    ASSERT_EQ((int)LocalizationPlural_Other, (int)localization_plural_category(FL_S("en"), INT64_MIN));
}

UTEST(localization, plural_selects_a_variant_and_exposes_the_count_as_the_first_argument) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_plural(FL_S("SID_file_count"), 1, nullptr, 0), FL_S("1 file")),
                    "a count of one selects the one variant and substitutes itself into {0}");
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_plural(FL_S("SID_file_count"), 3, nullptr, 0), FL_S("3 files")),
                    "any other count selects the other variant");
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_plural(FL_S("SID_file_count"), 0, nullptr, 0), FL_S("0 files")),
                    "English puts zero in the other variant");

    // The caller's own arguments start at {1}, so the count cannot be passed twice by mistake.
    const FlFmtArg args[] = { fmt_str(FL_S("Documents")) };
    ASSERT_TRUE_MSG(
        fl_string_equals(fl_localize_plural(FL_S("SID_plural_args"), 1, args, 1), FL_S("1 file in Documents")),
        "the first caller argument fills {1}");
    ASSERT_TRUE_MSG(
        fl_string_equals(fl_localize_plural(FL_S("SID_plural_args"), 7, args, 1), FL_S("7 files in Documents")),
        "the shift holds across variants");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, plural_degrades_to_other_and_then_through_the_fallback_chain) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    // The key carries no "one" variant, so the count that selects it lands on "other".
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_plural(FL_S("SID_plural_other_only"), 1, nullptr, 0), FL_S("1 items")),
                    "a variant the key does not carry degrades to other");

    // A plain string is its own answer whatever the count says.
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_plural(FL_S("SID_settings_title"), 2, nullptr, 0), FL_S("Settings")),
                    "a key that is a plain string resolves to it");

    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_plural(FL_S("SID_absent"), 2, nullptr, 0), FL_S("SID_absent")),
                    "a key no table carries renders as the key itself");

    // pt-br carries the key, so the switch also puts Portuguese's rules behind it: zero counts as one there.
    fl_localization_set_language(FL_S("pt-br"));
    localization_settle(ctx->shared->localization);
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_plural(FL_S("SID_file_count"), 0, nullptr, 0), FL_S("0 arquivo")),
                    "the active language's rules put zero in the one variant");
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_plural(FL_S("SID_file_count"), 2, nullptr, 0), FL_S("2 arquivos")),
                    "and everything above one in the other variant");

    // A key the active language lacks falls back to the default table, rules and all.
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_plural(FL_S("SID_plural_args"), 1, nullptr, 0), FL_S("1 file in {1}")),
                    "a missing key falls back to the default language");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, a_plural_key_through_the_plain_fmt_call_renders_the_other_variant) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    const FlFmtArg args[] = { fmt_int(1) };
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_fmt(FL_S("SID_file_count"), args, 1), FL_S("1 files")),
                    "the count-less call resolves the other variant rather than guessing at one");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, malformed_plural_entries_are_skipped_per_key) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlArena* arena = arena_new();
    FlString dir_path
        = file_build_path_os(arena, get_current_directory_os(arena), FL_S("unit_test_localization_plural"));
    file_remove_directory_recursive_os(dir_path); // clean any leftover from an aborted run
    ASSERT_TRUE(file_create_directory_os(dir_path));

    const u8 manifest[] = "{\"version\":1,\"default\":\"en\",\"languages\":[{\"tag\":\"en\",\"file\":\"en.json\"}]}";
    FlString manifest_path = file_build_path_os(arena, dir_path, FL_S("manifest.json"));
    ASSERT_EQ((int)sizeof(manifest) - 1, (int)file_write_os(manifest_path, manifest, (i64)sizeof(manifest) - 1));

    const u8 en[] = "{\"version\":1,\"strings\":{"
                    "\"SID_ok\":{\"one\":\"{0} thing\",\"other\":\"{0} things\"},"
                    "\"SID_no_other\":{\"one\":\"{0} thing\"},"
                    "\"SID_unknown_variant\":{\"lots\":\"{0} lots\",\"other\":\"{0} others\"},"
                    "\"SID_non_string_variant\":{\"one\":5,\"other\":\"{0} others\"},"
                    "\"SID_not_a_value\":5}}";
    FlString en_path = file_build_path_os(arena, dir_path, FL_S("en.json"));
    ASSERT_EQ((int)sizeof(en) - 1, (int)file_write_os(en_path, en, (i64)sizeof(en) - 1));

    fl_vfs_init(s_arena);
    FlVfsMountResult mount = vfs_mount(dir_path);
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "the scratch directory should mount through LocalFS");
    vfs_wait_all();

    fl_localization_init(mount.mount, FL_S("manifest.json"), false);
    localization_settle(ctx->shared->localization);

    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_plural(FL_S("SID_ok"), 1, nullptr, 0), FL_S("1 thing")),
                    "a well-formed variant object resolves");
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_plural(FL_S("SID_no_other"), 1, nullptr, 0), FL_S("SID_no_other")),
                    "a variant object with no other variant is skipped and falls through");
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_plural(FL_S("SID_unknown_variant"), 2, nullptr, 0), FL_S("2 others")),
                    "an unknown variant name is ignored and the rest of the key survives");
    ASSERT_TRUE_MSG(
        fl_string_equals(fl_localize_plural(FL_S("SID_non_string_variant"), 1, nullptr, 0), FL_S("1 others")),
        "a non-string variant is ignored and its category degrades to other");
    ASSERT_TRUE_MSG(
        fl_string_equals(fl_localize_plural(FL_S("SID_not_a_value"), 1, nullptr, 0), FL_S("SID_not_a_value")),
        "a value that is neither a string nor an object is skipped");

    localization_fixture_release(ctx, mount.mount);
    file_remove_directory_recursive_os(dir_path);
    fl_arena_destroy(arena);
}

UTEST(localization, a_language_with_no_built_in_rules_selects_the_other_variant) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlArena* arena = arena_new();
    FlString dir_path
        = file_build_path_os(arena, get_current_directory_os(arena), FL_S("unit_test_localization_norule"));
    file_remove_directory_recursive_os(dir_path); // clean any leftover from an aborted run
    ASSERT_TRUE(file_create_directory_os(dir_path));

    const u8 manifest[] = "{\"version\":1,\"default\":\"xx\",\"languages\":[{\"tag\":\"xx\",\"file\":\"xx.json\"}]}";
    FlString manifest_path = file_build_path_os(arena, dir_path, FL_S("manifest.json"));
    ASSERT_EQ((int)sizeof(manifest) - 1, (int)file_write_os(manifest_path, manifest, (i64)sizeof(manifest) - 1));

    const u8 xx[] = "{\"version\":1,\"strings\":{\"SID_count\":{\"one\":\"{0} thing\",\"other\":\"{0} things\"}}}";
    FlString xx_path = file_build_path_os(arena, dir_path, FL_S("xx.json"));
    ASSERT_EQ((int)sizeof(xx) - 1, (int)file_write_os(xx_path, xx, (i64)sizeof(xx) - 1));

    fl_vfs_init(s_arena);
    FlVfsMountResult mount = vfs_mount(dir_path);
    ASSERT_EQ_MSG(mount.status, FlVfsMountErrorStatus_Success, "the scratch directory should mount through LocalFS");
    vfs_wait_all();

    fl_localization_init(mount.mount, FL_S("manifest.json"), false);
    localization_settle(ctx->shared->localization);

    // The count that would select "one" in a language flowi knows the rules for selects "other" here, and it warns
    // once rather than once per lookup.
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_plural(FL_S("SID_count"), 1, nullptr, 0), FL_S("1 things")),
                    "a language with no built-in rules selects the other variant for every count");
    ASSERT_TRUE_MSG(fl_string_equals(fl_localize_plural(FL_S("SID_count"), 1, nullptr, 0), FL_S("1 things")),
                    "and keeps doing so once it has warned");

    localization_fixture_release(ctx, mount.mount);
    file_remove_directory_recursive_os(dir_path);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FL_FMT / FL_PLURAL: the C call-site macros that tag bare arguments and build the FlFmtArg array at the call.

#include <flowi/ui/localization_fmt.h>

UTEST(localization, fmt_macro_tags_bare_arguments_by_type) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    ASSERT_TRUE_MSG(fl_string_equals(FL_FMT("SID_greeting", "Ada"), FL_S("Hello, Ada!")),
                    "a C string argument tags as Str");
    ASSERT_TRUE_MSG(fl_string_equals(FL_FMT("SID_greeting", FL_S("Grace")), FL_S("Hello, Grace!")),
                    "an FlString argument tags as Str");

    const i32 files = -1000000;
    const f32 ratio = 2.5f;
    ASSERT_TRUE_MSG(fl_string_equals(FL_FMT("SID_fmt_two_slots", files, ratio), FL_S("have -1000000 of 2.5")),
                    "integer and float arguments tag as Int and Float and format C-style");

    ASSERT_TRUE_MSG(
        fl_string_equals(FL_FMT("SID_fmt_two_slots", FL_ARG_INT(7), FL_ARG_FLOAT(0.5)), FL_S("have 7 of 0.5")),
        "the explicit constructors sit in the same argument list as bare values");
    ASSERT_TRUE_MSG(fl_string_equals(FL_FMT("SID_greeting", FL_ARG_STR(FL_S("Ada"))), FL_S("Hello, Ada!")),
                    "an explicitly tagged string reaches the same slot a bare one would");

    ASSERT_TRUE_MSG(fl_string_equals(FL_FMT("SID_settings_title"), FL_S("Settings")),
                    "a key with no placeholders takes no arguments at all");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, fmt_macro_takes_a_full_eight_arguments) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    ASSERT_TRUE_MSG(fl_string_equals(FL_FMT("SID_fmt_eight", 1, 2.5, "three", FL_S("four"), (i64)5, 6.5f, "seven", 8),
                                     FL_S("1 2.5 three four 5 6.5 seven 8")),
                    "eight mixed-type arguments each land in their own slot");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, plural_macro_selects_the_variant_and_tags_the_rest) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    ASSERT_TRUE_MSG(fl_string_equals(FL_PLURAL("SID_file_count", 1), FL_S("1 file")),
                    "the count alone selects the variant and fills {0}");
    ASSERT_TRUE_MSG(fl_string_equals(FL_PLURAL("SID_file_count", 3), FL_S("3 files")),
                    "and a count the rules put in another category selects that one");

    const u32 many = 7;
    ASSERT_TRUE_MSG(fl_string_equals(FL_PLURAL("SID_plural_args", many, "Documents"), FL_S("7 files in Documents")),
                    "arguments after the count shift to {1} onwards and tag by type");
    ASSERT_TRUE_MSG(fl_string_equals(FL_PLURAL("SID_plural_args", 1, FL_S("Photos")), FL_S("1 file in Photos")),
                    "an FlString argument after the count tags as Str");

    localization_fixture_release(ctx, mount);
}

UTEST(localization, a_macro_result_is_a_widget_text_argument) {
    FlVfsMount* mount = nullptr;
    FlUiCtx* ctx = localization_fixture(&mount, FL_S("manifest.json"));
    ASSERT_TRUE_MSG(ctx != nullptr, "localization fixture creation failed");

    // Wide enough that neither line wraps: the two texts share the row, and a wrapped line would reach the
    // snapshot as several text elements rather than one.
    ctx->layout_width = 1024.0f;
    ctx->layout_height = 256.0f;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1024.0f, .height = 256.0f });

    ui_update_begin(ctx);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_layout_area_id(resolve_widget_id(FL_S("loc_macro_probe"), string_empty()),
                          .sizing = { sizing_fixed(1000), sizing_fixed(80) }, .color = ui_color(40, 200, 60, 255)) {
            ui_text(FL_FMT("SID_greeting", "Ada"));
            ui_text(FL_PLURAL("SID_file_count", 2));
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    char snapshot[8192];
    fl_inspector_snapshot(snapshot, (int32_t)sizeof(snapshot));
    ASSERT_TRUE_MSG(strstr(snapshot, "Hello, Ada!") != nullptr, "FL_FMT passes inline as the text of a widget");
    ASSERT_TRUE_MSG(strstr(snapshot, "2 files") != nullptr, "FL_PLURAL passes inline as the text of a widget");

    localization_fixture_release(ctx, mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Hot reload: with the opt-in flag on, editing a resident table or the manifest on disk re-runs the switch
// machinery, so a reload lands at a frame boundary and a file that stops parsing leaves what is loaded in place.

static const char k_hot_manifest[] = "{ \"version\": 1, \"default\": \"en\", \"languages\": ["
                                     "{ \"tag\": \"en\", \"file\": \"en.json\" },"
                                     "{ \"tag\": \"de\", \"file\": \"de.json\" } ] }";

static const char k_hot_en[] = "{ \"version\": 1, \"strings\": {"
                               "\"SID_hot\": \"before\", \"SID_only_default\": \"fallback\" } }";

static const char k_hot_de[] = "{ \"version\": 1, \"strings\": { \"SID_hot\": \"vorher\" } }";

#define HOT_RELOAD_WAIT_ITERATIONS 5000

typedef struct LocalizationHotReload {
    FlArena* arena;
    FlString dir;
    FlVfsMount* mount;
    FlUiCtx* ctx;
} LocalizationHotReload;

static bool hot_reload_write(const LocalizationHotReload* fixture, FlString name, const char* text) {
    FlString path = file_build_path_os(fixture->arena, fixture->dir, name);
    const i64 length = (i64)strlen(text);
    return file_write_os(path, (const u8*)text, length) == length;
}

// A scratch localization directory the case can rewrite mid-run, mounted with file watching on so hot reload has
// something to hear, with the default language already up.
static bool hot_reload_fixture_create(LocalizationHotReload* fixture, FlString dir_name, bool hot_reload) {
    fixture->arena = arena_new();
    fixture->dir = file_build_path_os(fixture->arena, get_current_directory_os(fixture->arena), dir_name);
    file_remove_directory_recursive_os(fixture->dir); // clean any leftover from an aborted run
    if (!file_create_directory_os(fixture->dir)) {
        return false;
    }

    if (!hot_reload_write(fixture, FL_S("manifest.json"), k_hot_manifest)
        || !hot_reload_write(fixture, FL_S("en.json"), k_hot_en)
        || !hot_reload_write(fixture, FL_S("de.json"), k_hot_de)) {
        return false;
    }

    fixture->ctx = render_fixture_get();
    if (fixture->ctx == nullptr) {
        return false;
    }

    fl_vfs_init(s_arena);
    FlVfsMountResult mount = vfs_mount(fixture->dir);
    if (mount.status != FlVfsMountErrorStatus_Success) {
        return false;
    }
    fixture->mount = mount.mount;
    vfs_wait_all();

    fl_localization_init(fixture->mount, FL_S("manifest.json"), hot_reload);
    localization_settle(fixture->ctx->shared->localization);
    return true;
}

static void hot_reload_fixture_release(LocalizationHotReload* fixture) {
    localization_fixture_release(fixture->ctx, fixture->mount);
    file_remove_directory_recursive_os(fixture->dir);
    fl_arena_destroy(fixture->arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pump the frame boundary the way an app tick loop does until the watcher has asked for a reload beyond
// requests_before and the work it started has landed. False if no request arrives within the deadline.
static bool hot_reload_wait_for_request(const LocalizationHotReload* fixture, u64 requests_before) {
    struct Localization* loc = fixture->ctx->shared->localization;

    for (u32 i = 0; i < HOT_RELOAD_WAIT_ITERATIONS; i++) {
        localization_frame_begin(loc);
        if (localization_hot_reload_request_count(loc) > requests_before && !localization_is_loading(loc)) {
            return true;
        }
        sleep_us(1000);
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static u64 hot_reload_requests(const LocalizationHotReload* fixture) {
    return localization_hot_reload_request_count(fixture->ctx->shared->localization);
}

// Pump until sid resolves to expected, false if it never does within iterations milliseconds.
static bool hot_reload_wait_for(const LocalizationHotReload* fixture, FlString sid, FlString expected, u32 iterations) {
    struct Localization* loc = fixture->ctx->shared->localization;

    for (u32 i = 0; i < iterations; i++) {
        localization_frame_begin(loc);
        if (!localization_is_loading(loc) && fl_string_equals(localization_lookup(loc, sid), expected)) {
            return true;
        }
        sleep_us(1000);
    }

    return false;
}

static FlString hot_reload_lookup(const LocalizationHotReload* fixture, FlString sid) {
    return localization_lookup(fixture->ctx->shared->localization, sid);
}

UTEST(localization, hot_reload_swaps_in_an_edited_table) {
    LocalizationHotReload fixture = { 0 };
    ASSERT_TRUE_MSG(hot_reload_fixture_create(&fixture, FL_S("unit_test_loc_hot_active_tmp"), true),
                    "hot reload fixture creation failed");
    ASSERT_TRUE(fl_string_equals(hot_reload_lookup(&fixture, FL_S("SID_hot")), FL_S("before")));

    ASSERT_TRUE(
        hot_reload_write(&fixture, FL_S("en.json"), "{ \"version\": 1, \"strings\": { \"SID_hot\": \"after\" } }"));
    ASSERT_TRUE_MSG(hot_reload_wait_for(&fixture, FL_S("SID_hot"), FL_S("after"), 3000),
                    "an edited table must swap its new strings in");

    hot_reload_fixture_release(&fixture);
}

UTEST(localization, hot_reload_of_the_default_table_refreshes_the_fallback_layer) {
    LocalizationHotReload fixture = { 0 };
    ASSERT_TRUE_MSG(hot_reload_fixture_create(&fixture, FL_S("unit_test_loc_hot_default_tmp"), true),
                    "hot reload fixture creation failed");

    fl_localization_set_language(FL_S("de"));
    localization_settle(fixture.ctx->shared->localization);
    ASSERT_TRUE(fl_string_equals(hot_reload_lookup(&fixture, FL_S("SID_hot")), FL_S("vorher")));
    ASSERT_TRUE(fl_string_equals(hot_reload_lookup(&fixture, FL_S("SID_only_default")), FL_S("fallback")));

    ASSERT_TRUE(hot_reload_write(&fixture, FL_S("en.json"),
                                 "{ \"version\": 1, \"strings\": { \"SID_only_default\": \"edited fallback\" } }"));
    ASSERT_TRUE_MSG(hot_reload_wait_for(&fixture, FL_S("SID_only_default"), FL_S("edited fallback"), 3000),
                    "the reloaded default table must become the fallback layer");
    ASSERT_TRUE_MSG(fl_string_equals(hot_reload_lookup(&fixture, FL_S("SID_hot")), FL_S("vorher")),
                    "reloading the default table must leave the active language where it is");
    ASSERT_EQ_MSG(2u, localization_resident_table_count(fixture.ctx->shared->localization),
                  "the reload must replace the fallback layer rather than add a third table");

    hot_reload_fixture_release(&fixture);
}

UTEST(localization, a_batch_that_edits_both_resident_tables_reloads_both) {
    LocalizationHotReload fixture = { 0 };
    ASSERT_TRUE_MSG(hot_reload_fixture_create(&fixture, FL_S("unit_test_loc_hot_both_tmp"), true),
                    "hot reload fixture creation failed");

    fl_localization_set_language(FL_S("de"));
    localization_settle(fixture.ctx->shared->localization);
    ASSERT_TRUE(fl_string_equals(hot_reload_lookup(&fixture, FL_S("SID_hot")), FL_S("vorher")));

    // Both resident tables edited before a single drain, so both changes arrive in one batch. Only one switch runs
    // at a time and a watcher reports a change once, so the one that cannot start immediately has to be held.
    ASSERT_TRUE(
        hot_reload_write(&fixture, FL_S("de.json"), "{ \"version\": 1, \"strings\": { \"SID_hot\": \"nachher\" } }"));
    ASSERT_TRUE(hot_reload_write(&fixture, FL_S("en.json"),
                                 "{ \"version\": 1, \"strings\": { \"SID_only_default\": \"edited fallback\" } }"));

    ASSERT_TRUE_MSG(hot_reload_wait_for(&fixture, FL_S("SID_only_default"), FL_S("edited fallback"), 3000),
                    "the second table's reload must be held, not dropped, while the first one runs");
    ASSERT_TRUE_MSG(fl_string_equals(hot_reload_lookup(&fixture, FL_S("SID_hot")), FL_S("nachher")),
                    "the active table's own reload must land as well");
    ASSERT_EQ_MSG(2u, localization_resident_table_count(fixture.ctx->shared->localization),
                  "reloading both tables must replace them rather than add more");

    hot_reload_fixture_release(&fixture);
}

UTEST(localization, a_hot_reload_that_fails_to_parse_keeps_the_current_table) {
    LocalizationHotReload fixture = { 0 };
    ASSERT_TRUE_MSG(hot_reload_fixture_create(&fixture, FL_S("unit_test_loc_hot_broken_tmp"), true),
                    "hot reload fixture creation failed");

    const u64 requests = hot_reload_requests(&fixture);
    ASSERT_TRUE(hot_reload_write(&fixture, FL_S("en.json"), "{ this is not json"));
    ASSERT_TRUE_MSG(hot_reload_wait_for_request(&fixture, requests), "the edit never reached the watcher");

    ASSERT_TRUE_MSG(fl_string_equals(hot_reload_lookup(&fixture, FL_S("SID_hot")), FL_S("before")),
                    "a table that stops parsing must leave the loaded one in place");

    hot_reload_fixture_release(&fixture);
}

UTEST(localization, a_default_table_reload_that_fails_to_parse_keeps_the_fallback_layer) {
    LocalizationHotReload fixture = { 0 };
    ASSERT_TRUE_MSG(hot_reload_fixture_create(&fixture, FL_S("unit_test_loc_hot_broken_default_tmp"), true),
                    "hot reload fixture creation failed");

    fl_localization_set_language(FL_S("de"));
    localization_settle(fixture.ctx->shared->localization);
    ASSERT_TRUE(fl_string_equals(hot_reload_lookup(&fixture, FL_S("SID_only_default")), FL_S("fallback")));

    const u64 requests = hot_reload_requests(&fixture);
    ASSERT_TRUE(hot_reload_write(&fixture, FL_S("en.json"), "{ this is not json"));
    ASSERT_TRUE_MSG(hot_reload_wait_for_request(&fixture, requests), "the edit never reached the watcher");

    ASSERT_TRUE_MSG(fl_string_equals(hot_reload_lookup(&fixture, FL_S("SID_only_default")), FL_S("fallback")),
                    "a default table that stops parsing must leave the fallback layer in place");
    ASSERT_TRUE_MSG(fl_string_equals(hot_reload_lookup(&fixture, FL_S("SID_hot")), FL_S("vorher")),
                    "a failed default-table reload must leave the active language alone");
    ASSERT_EQ_MSG(2u, localization_resident_table_count(fixture.ctx->shared->localization),
                  "a failed reload must not leave a third table resident");

    hot_reload_fixture_release(&fixture);
}

UTEST(localization, hot_reload_picks_up_a_manifest_edit) {
    LocalizationHotReload fixture = { 0 };
    ASSERT_TRUE_MSG(hot_reload_fixture_create(&fixture, FL_S("unit_test_loc_hot_manifest_tmp"), true),
                    "hot reload fixture creation failed");

    const u64 requests = hot_reload_requests(&fixture);
    ASSERT_TRUE(
        hot_reload_write(&fixture, FL_S("de2.json"), "{ \"version\": 1, \"strings\": { \"SID_hot\": \"zwei\" } }"));
    ASSERT_TRUE(hot_reload_write(&fixture, FL_S("manifest.json"),
                                 "{ \"version\": 1, \"default\": \"en\", \"languages\": ["
                                 "{ \"tag\": \"en\", \"file\": \"en.json\" },"
                                 "{ \"tag\": \"de\", \"file\": \"de2.json\" } ] }"));
    ASSERT_TRUE_MSG(hot_reload_wait_for_request(&fixture, requests), "the manifest edit never reached the watcher");

    fl_localization_set_language(FL_S("de"));
    localization_settle(fixture.ctx->shared->localization);
    ASSERT_TRUE_MSG(fl_string_equals(hot_reload_lookup(&fixture, FL_S("SID_hot")), FL_S("zwei")),
                    "a switch after a manifest edit must use the file the new language list names");

    hot_reload_fixture_release(&fixture);
}

UTEST(localization, an_unparseable_manifest_reload_keeps_the_last_good_language_list) {
    LocalizationHotReload fixture = { 0 };
    ASSERT_TRUE_MSG(hot_reload_fixture_create(&fixture, FL_S("unit_test_loc_hot_bad_manifest_tmp"), true),
                    "hot reload fixture creation failed");

    const u64 requests = hot_reload_requests(&fixture);
    ASSERT_TRUE(hot_reload_write(&fixture, FL_S("manifest.json"), "{ this is not json"));
    ASSERT_TRUE_MSG(hot_reload_wait_for_request(&fixture, requests), "the manifest edit never reached the watcher");

    fl_localization_set_language(FL_S("de"));
    localization_settle(fixture.ctx->shared->localization);
    ASSERT_TRUE_MSG(fl_string_equals(hot_reload_lookup(&fixture, FL_S("SID_hot")), FL_S("vorher")),
                    "a manifest that stops parsing must leave the last good language list in force");

    hot_reload_fixture_release(&fixture);
}

UTEST(localization, hot_reload_off_never_reloads) {
    LocalizationHotReload fixture = { 0 };
    ASSERT_TRUE_MSG(hot_reload_fixture_create(&fixture, FL_S("unit_test_loc_hot_off_tmp"), false),
                    "hot reload fixture creation failed");

    ASSERT_FALSE_MSG(localization_is_watching(fixture.ctx->shared->localization),
                     "a run with hot reload off registers no watch");
    ASSERT_TRUE(
        hot_reload_write(&fixture, FL_S("en.json"), "{ \"version\": 1, \"strings\": { \"SID_hot\": \"after\" } }"));
    localization_frame_begin(fixture.ctx->shared->localization);
    ASSERT_TRUE_MSG(fl_string_equals(hot_reload_lookup(&fixture, FL_S("SID_hot")), FL_S("before")),
                    "a run with hot reload off keeps the table it loaded");

    hot_reload_fixture_release(&fixture);
}

UTEST(localization, a_file_change_does_not_drop_a_language_switch_that_has_not_been_applied) {
    LocalizationHotReload fixture = { 0 };
    ASSERT_TRUE_MSG(hot_reload_fixture_create(&fixture, FL_S("unit_test_loc_hot_race_tmp"), true),
                    "hot reload fixture creation failed");

    // Queue the edit and wait until the kernel has delivered it to an independent watcher before recording the
    // switch, so the frame boundary drains a watcher that already has the change while the switch is still only
    // recorded.
    const FlFileWatcherConfig barrier_config
        = { .recursive = true, .watch_files = true, .watch_directories = false, .max_changes = 0 };
    const FlFileWatcherHandle barrier = file_watcher_start(fixture.dir, barrier_config);
    ASSERT_NE(barrier, FL_FILE_WATCHER_INVALID);
    const bool written
        = hot_reload_write(&fixture, FL_S("en.json"), "{ \"version\": 1, \"strings\": { \"SID_hot\": \"after\" } }");
    bool delivered = false;
    for (u32 i = 0; i < HOT_RELOAD_WAIT_ITERATIONS && !delivered; i++) {
        delivered = file_watcher_has_changes(barrier);
        if (!delivered) {
            sleep_us(1000);
        }
    }
    file_watcher_stop(barrier);
    ASSERT_TRUE(written);
    ASSERT_TRUE_MSG(delivered, "the edit never reached the independent watcher");

    fl_localization_set_language(FL_S("de"));
    localization_settle(fixture.ctx->shared->localization);
    ASSERT_TRUE_MSG(fl_string_equals(hot_reload_lookup(&fixture, FL_S("SID_hot")), FL_S("vorher")),
                    "a file change must not overwrite a language switch that is still pending");

    hot_reload_fixture_release(&fixture);
}
