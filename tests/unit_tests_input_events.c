// UI input event and readback tests

#include "unit_tests_common.h"
#include <math.h>

// ui_action_repeated (ui/input_actions.c) reads only the keyboard state and delta_time, so a zeroed FlUiCtx is
// enough to drive it. Actions that share a physical key but differ by modifier (Left and Ctrl+Left) never both
// repeat on the same frame.

typedef struct RepeatExpectation {
    FlAction action;
    bool repeated;
} RepeatExpectation;

#define REPEATS(...)                             \
    (const RepeatExpectation[]) { __VA_ARGS__ }, \
        (sizeof((const RepeatExpectation[]) { __VA_ARGS__ }) / sizeof(RepeatExpectation))

static void expect_actions_repeated(int* utest_result, FlUiCtx* ctx, const RepeatExpectation* want, size_t count) {
    for (size_t i = 0; i < count; i++) {
        EXPECT_EQ((int)want[i].repeated, (int)ui_action_repeated(ctx, want[i].action));
    }
}

UTEST(input_repeat, modifier_disambiguation_no_double_fire) {
    FlUiCtx* ctx = arena_alloc(s_arena, FlUiCtx);
    *ctx = (FlUiCtx) { 0 };
    ctx->delta_time = 1.0f / 60.0f;

    KeyboardState* kb = &ctx->input_state.keyboard;
    kb->keys[FlKey_Left] = true;
    kb->keys[FlKey_Right] = true;
    // Held just past the repeat delay. time_since_delay is a fraction of a frame, so a repeat is due this frame.
    kb->key_hold_duration[FlKey_Left] = UI_NAV_REPEAT_DELAY + ctx->delta_time * 0.5f;
    kb->key_hold_duration[FlKey_Right] = UI_NAV_REPEAT_DELAY + ctx->delta_time * 0.5f;

    kb->modifiers = 0;
    expect_actions_repeated(utest_result, ctx,
                            REPEATS({ FlAction_Text_Move_Left, true }, { FlAction_Text_Move_Word_Left, false },
                                    { FlAction_Text_Move_Right, true }, { FlAction_Text_Move_Word_Right, false }));

    kb->modifiers = FlKeyModifier_Control;
    expect_actions_repeated(utest_result, ctx,
                            REPEATS({ FlAction_Text_Move_Left, false }, { FlAction_Text_Move_Word_Left, true },
                                    { FlAction_Text_Move_Right, false }, { FlAction_Text_Move_Word_Right, true }));

    // A key that is not held never repeats.
    EXPECT_FALSE(ui_action_repeated(ctx, FlAction_Text_Backspace));

    // Held but still inside the initial delay, so no repeat yet.
    kb->modifiers = 0;
    kb->key_hold_duration[FlKey_Left] = UI_NAV_REPEAT_DELAY * 0.5f;
    EXPECT_FALSE(ui_action_repeated(ctx, FlAction_Text_Move_Left));
}

// Key codes outside [0, FlKey_Count) are dropped before any keyboard-state array is indexed. The arrays are
// FlKey_Count long, so an unchecked index would corrupt the adjacent KeyboardState fields.
UTEST(input_key_event, out_of_range_key_is_ignored) {
    FlUiCtx* ctx = arena_alloc(s_arena, FlUiCtx);
    *ctx = (FlUiCtx) { 0 };
    KeyboardState* kb = &ctx->input_state.keyboard;

    const FlKey bad_keys[] = { (FlKey)FlKey_Count, (FlKey)(FlKey_Count + 1000), (FlKey)-1 };
    const FlKeyEventType types[] = { FlKeyEvent_Down, FlKeyEvent_Up, FlKeyEvent_Repeat };
    for (u32 k = 0; k < sizeof_array(bad_keys); k++) {
        for (u32 t = 0; t < sizeof_array(types); t++) {
            FlKeyEvent event = { .type = types[t], .key = bad_keys[k], .modifiers = FlKeyModifier_Shift };
            platform_input_push_key_event(ctx, event);
        }
    }

    KeyboardState zero = { 0 };
    EXPECT_EQ(0, memcmp(kb, &zero, sizeof(KeyboardState)));

    FlKeyEvent good = { .type = FlKeyEvent_Down, .key = FlKey_A, .modifiers = FlKeyModifier_Shift };
    platform_input_push_key_event(ctx, good);
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
            platform_input_push_mouse_event(ctx, event);
        }
    }

    MouseState zero = { 0 };
    EXPECT_EQ(0, memcmp(mouse, &zero, sizeof(MouseState)));

    FlMouseEvent good = { .type = FlMouseEvent_Button_Down, .button = { .button = FlMouseButton_Right } };
    platform_input_push_mouse_event(ctx, good);
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
            platform_input_push_gamepad_event(ctx, event);
        }
    }

    const FlGamepadAxis bad_axes[]
        = { (FlGamepadAxis)FlGamepadAxis_Count, (FlGamepadAxis)(FlGamepadAxis_Count + 1000), (FlGamepadAxis)-1 };
    for (u32 a = 0; a < sizeof_array(bad_axes); a++) {
        FlGamepadEvent event
            = { .type = FlGamepadEvent_Axis_Motion, .gamepad_id = 0, .axis = { .axis = bad_axes[a], .value = 1.0f } };
        platform_input_push_gamepad_event(ctx, event);
    }

    GamepadState zero = { 0 };
    EXPECT_EQ(0, memcmp(gamepad, &zero, sizeof(GamepadState)));

    FlGamepadEvent good_button
        = { .type = FlGamepadEvent_Button_Down, .gamepad_id = 0, .button = { .button = FlGamepadButton_Start } };
    platform_input_push_gamepad_event(ctx, good_button);
    EXPECT_TRUE(gamepad->buttons[FlGamepadButton_Start]);

    FlGamepadEvent good_axis = { .type = FlGamepadEvent_Axis_Motion,
                                 .gamepad_id = 0,
                                 .axis = { .axis = FlGamepadAxis_Left_X, .value = 0.5f } };
    platform_input_push_gamepad_event(ctx, good_axis);
    EXPECT_EQ(0.5f, gamepad->axes[FlGamepadAxis_Left_X]);
}

// fl_input_gamepad_connected reads the slot table, and slots past the table read as disconnected. Slot 3 keeps
// clear of pad 0, which the nav cases use, and is restored to the state it was found in.
UTEST(input, gamepad_connected_reads_the_slot_and_out_of_range_slots_as_disconnected) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    // fl_input_* read the global context, and one frame binds it to this fixture.
    ui_update_begin(ctx);
    ui_update_end(ctx, 1.0f / 60.0f);

    const u32 slot = 3;
    const GamepadState saved = ctx->input_state.gamepads[slot];
    ctx->input_state.gamepads[slot].connected = false;

    EXPECT_FALSE(fl_input_gamepad_connected(slot));

    platform_input_push_gamepad_event(ctx, (FlGamepadEvent) { .type = FlGamepadEvent_Connected, .gamepad_id = slot });
    EXPECT_TRUE(fl_input_gamepad_connected(slot));
    EXPECT_FALSE_MSG(fl_input_gamepad_connected(slot - 1), "connecting one slot leaves its neighbour alone");

    const u32 beyond_the_table[] = { UI_INPUT_MAX_GAMEPADS, UI_INPUT_MAX_GAMEPADS + 1000, UINT32_MAX };
    for (u32 i = 0; i < sizeof_array(beyond_the_table); i++) {
        EXPECT_FALSE(fl_input_gamepad_connected(beyond_the_table[i]));
    }

    platform_input_push_gamepad_event(ctx,
                                      (FlGamepadEvent) { .type = FlGamepadEvent_Disconnected, .gamepad_id = slot });
    EXPECT_FALSE(fl_input_gamepad_connected(slot));

    ctx->input_state.gamepads[slot] = saved;
    canvas_fixture_reset(ctx);
}

// The delta is this frame's pointer position minus the last frame's. The case sets the pointer and lets
// ui_input_update close the frame, as a backend does.
UTEST(input, mouse_delta_follows_the_pointer_between_frames) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    ui_update_begin(ctx);
    ui_update_end(ctx, 1.0f / 60.0f);

    MouseState* mouse = &ctx->input_state.mouse;
    mouse->x = 10.0f;
    mouse->y = 20.0f;
    ui_input_update(ctx, 1.0 / 60.0);
    mouse->x = 10.0f;
    mouse->y = 20.0f;
    ui_input_update(ctx, 1.0 / 60.0);
    EXPECT_EQ(0.0f, fl_input_mouse_delta().x);
    EXPECT_EQ(0.0f, fl_input_mouse_delta().y);

    mouse->x = 40.0f;
    mouse->y = 15.0f;
    ui_input_update(ctx, 1.0 / 60.0);
    EXPECT_EQ(30.0f, fl_input_mouse_delta().x);
    EXPECT_EQ(-5.0f, fl_input_mouse_delta().y);

    // Holding still afterwards reads as no movement. The last step is not repeated.
    ui_input_update(ctx, 1.0 / 60.0);
    EXPECT_EQ(0.0f, fl_input_mouse_delta().x);
    EXPECT_EQ(0.0f, fl_input_mouse_delta().y);

    canvas_fixture_reset(ctx);
}

UTEST(input, new_context_gamepads_start_at_default_dead_zone) {
    FlUiCtx* fixture = render_fixture_get();
    ASSERT_TRUE_MSG(fixture != nullptr, "render fixture creation failed");

    FlUiCtx* ctx = ui_create(s_arena, fixture->shared);
    ASSERT_TRUE_MSG(ctx != nullptr, "ui_create failed");
    for (u32 slot = 0; slot < UI_INPUT_MAX_GAMEPADS; slot++) {
        EXPECT_EQ(UI_INPUT_GAMEPAD_DEFAULT_DEAD_ZONE, ctx->input_state.gamepads[slot].dead_zone);
    }
    EXPECT_TRUE(ctx->input_state.window_focused);
    ui_destroy(ctx);
}

// ui_keyboard_modifiers() returns the raw keyboard modifier bitmask the input layer holds (Shift=1, Ctrl=2,
// Alt=4, Super=8). The bare render fixture has no key pump, so the bits are written straight onto the context.
UTEST(input, modifiers_readback) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    // ui_keyboard_modifiers() reads the global context. One frame binds s_ctx to this fixture, and the binding
    // outlasts the frame.
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

// ui_input_update swaps the frame's accumulated wheel axes when Shift is held. The swap has to be in the cached
// values consumers read (Clay scroll, ui_mouse_wheel, hover signals), because the live accumulators are cleared
// each frame. The Scroll_Up and Scroll_Down actions derive from the same cached value, so they fire in the real
// tick order with and without the swap.
UTEST(input, shift_wheel_axis_swap_lands_in_cache) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    FlInputState* in = &ctx->input_state;

    in->keyboard.modifiers = FlKeyModifier_None;
    in->mouse.wheel_x = 3.0f;
    in->mouse.wheel_y = 7.0f;
    ui_input_update(ctx, 1.0 / 60.0);
    ASSERT_FALSE_MSG(in->mouse.wheel_axis_swap_requested, "no swap requested without Shift");
    ASSERT_NEAR_MSG(3.0, (double)in->mouse.wheel_x_cached, 0.001, "x caches unswapped without Shift");
    ASSERT_NEAR_MSG(7.0, (double)in->mouse.wheel_y_cached, 0.001, "y caches unswapped without Shift");
    const bool scroll_up = ui_action_pressed(ctx, FlAction_Scroll_Up);
    ASSERT_TRUE_MSG(scroll_up, "Scroll_Up fires from positive wheel y");
    ASSERT_FALSE_MSG(ui_action_pressed(ctx, FlAction_Scroll_Down), "Scroll_Down stays off");

    // Under Shift the scroll actions follow the swapped y. The accumulated x is positive, so Scroll_Up fires, and
    // the negative accumulated y ends up on x.
    in->keyboard.modifiers = FlKeyModifier_Shift;
    in->mouse.wheel_x = 5.0f;
    in->mouse.wheel_y = -2.0f;
    ui_input_update(ctx, 1.0 / 60.0);
    ASSERT_TRUE_MSG(in->mouse.wheel_axis_swap_requested, "swap requested while Shift held");
    ASSERT_NEAR_MSG(-2.0, (double)in->mouse.wheel_x_cached, 0.001, "x caches the accumulated y under Shift");
    ASSERT_NEAR_MSG(5.0, (double)in->mouse.wheel_y_cached, 0.001, "y caches the accumulated x under Shift");
    const bool swapped_scroll_up = ui_action_pressed(ctx, FlAction_Scroll_Up);
    ASSERT_TRUE_MSG(swapped_scroll_up, "Scroll_Up fires from the swapped positive y");
    ASSERT_FALSE_MSG(ui_action_pressed(ctx, FlAction_Scroll_Down), "Scroll_Down stays off after swap");

    // One quiet update clears the cached wheel, the swap flag and the scroll-action state.
    in->keyboard.modifiers = FlKeyModifier_None;
    ui_input_update(ctx, 1.0 / 60.0);
    ASSERT_FALSE_MSG(ui_action_pressed(ctx, FlAction_Scroll_Up), "scroll action clears on a wheel-quiet frame");
}

// The double-click window is the gap between consecutive clicks and not a budget for the whole run, so a
// triple-click is no harder to perform than a double-click.
UTEST(input, click_count_window_is_per_gap) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    FlInputState* in = &ctx->input_state;
    MouseState* mouse = &in->mouse;

    // Each gap is inside the window but three of them exceed it, so a budget for the whole run would reset the
    // count on the third click.
    const double gap = UI_MOUSE_DOUBLE_CLICK_TIME * 0.7;
    ASSERT_TRUE_MSG(3.0 * gap > UI_MOUSE_DOUBLE_CLICK_TIME, "gap must distinguish per-gap from whole-run");

    mouse->x = 100.0f;
    mouse->y = 100.0f;
    mouse->last_click_time[FlMouseButton_Left] = 0;
    mouse->click_count[FlMouseButton_Left] = 0;

    for (u32 expected = 1; expected <= 4; expected++) {
        mouse->buttons_pressed[FlMouseButton_Left] = true;
        ui_input_update(ctx, gap);
        ASSERT_EQ_MSG((int)expected, (int)mouse->click_count[FlMouseButton_Left], "click run keeps accumulating");
    }

    // A gap wider than the window starts a new run.
    mouse->buttons_pressed[FlMouseButton_Left] = true;
    ui_input_update(ctx, UI_MOUSE_DOUBLE_CLICK_TIME * 1.5);
    ASSERT_EQ_MSG(1, (int)mouse->click_count[FlMouseButton_Left], "a gap past the window resets the run");

    // A click far from the one before it starts a new run even inside the window.
    mouse->x = 100.0f + UI_MOUSE_DOUBLE_CLICK_MAX_DIST * 2.0f;
    mouse->buttons_pressed[FlMouseButton_Left] = true;
    ui_input_update(ctx, gap);
    ASSERT_EQ_MSG(1, (int)mouse->click_count[FlMouseButton_Left], "a click past the distance limit resets the run");

    mouse->click_count[FlMouseButton_Left] = 0;
    mouse->last_click_time[FlMouseButton_Left] = 0;
    canvas_fixture_reset(ctx);
}

// ui_mouse_pressed and ui_mouse_released read the context's per-frame edge snapshot
// (mouse_buttons_*_this_frame, captured before ui_input_update clears the raw buttons_pressed and
// buttons_released arrays). ui_mouse_down reads the level in input_state.mouse.buttons[]. Buttons are global and
// not routed, so the bare render fixture writes the context fields directly.
UTEST(input, mouse_button_readback) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    // ui_mouse_* read the global context. One frame binds s_ctx to this fixture, and the binding outlasts the
    // frame.
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

    const bool right_pressed = ui_mouse_pressed(FlMouseButton_Right);
    ASSERT_FALSE_MSG(right_pressed, "Right press absent when only Left is pressed");
    ASSERT_FALSE_MSG(ui_mouse_down(FlMouseButton_Right), "Right held absent when only Left is held");

    ctx->mouse_buttons_pressed_this_frame[FlMouseButton_Left] = false;
    ASSERT_FALSE_MSG(ui_mouse_pressed(FlMouseButton_Left), "press edge is one frame only");
    ASSERT_TRUE_MSG(ui_mouse_down(FlMouseButton_Left), "Left stays held after the press edge");

    mouse->buttons[FlMouseButton_Left] = false;
    ctx->mouse_buttons_released_this_frame[FlMouseButton_Left] = true;
    ASSERT_TRUE_MSG(ui_mouse_released(FlMouseButton_Left), "Left release edge reads back");
    ASSERT_FALSE_MSG(ui_mouse_down(FlMouseButton_Left), "Left no longer held after release");

    // A fast click: pressed and released both set in one frame.
    memset(mouse->buttons, 0, sizeof(mouse->buttons));
    memset(ctx->mouse_buttons_pressed_this_frame, 0, sizeof(ctx->mouse_buttons_pressed_this_frame));
    memset(ctx->mouse_buttons_released_this_frame, 0, sizeof(ctx->mouse_buttons_released_this_frame));
    ctx->mouse_buttons_pressed_this_frame[FlMouseButton_Middle] = true;
    ctx->mouse_buttons_released_this_frame[FlMouseButton_Middle] = true;
    ASSERT_TRUE_MSG(ui_mouse_pressed(FlMouseButton_Middle), "tap: pressed set");
    ASSERT_TRUE_MSG(ui_mouse_released(FlMouseButton_Middle), "tap: released set in the same frame");

    const bool count_button_pressed = ui_mouse_pressed((FlMouseButton)FlMouseButton_Count);
    ASSERT_FALSE_MSG(count_button_pressed, "out-of-range button reads false");
    ASSERT_FALSE_MSG(ui_mouse_down((FlMouseButton)-1), "negative button index reads false");

    canvas_fixture_reset(ctx);
}

// Gamepad readback through the public fl_input_* queries. The case pushes events and lets ui_input_update close
// the frame, as a backend does. Slot 3 keeps clear of pad 0, which the nav cases use, and is restored to the
// state it was found in.

static void push_gamepad_button(FlUiCtx* ctx, u32 slot, FlGamepadEventType type, FlGamepadButton button) {
    platform_input_push_gamepad_event(
        ctx, (FlGamepadEvent) { .type = type, .gamepad_id = slot, .button = { .button = button } });
}

static void push_gamepad_axis(FlUiCtx* ctx, u32 slot, FlGamepadAxis axis, f32 value) {
    platform_input_push_gamepad_event(
        ctx, (FlGamepadEvent) { .type = FlGamepadEvent_Axis_Motion, .gamepad_id = slot, .axis = { axis, value } });
}

static void expect_slot_reads_empty(int* utest_result, u32 slot, FlGamepadButton button, FlGamepadAxis axis) {
    EXPECT_FALSE(fl_input_gamepad_button_pressed(slot, button));
    EXPECT_FALSE(fl_input_gamepad_button_released(slot, button));
    const f32 hold = fl_input_gamepad_button_hold_duration(slot, button);
    EXPECT_EQ(0.0f, hold);
    EXPECT_EQ(0.0f, fl_input_gamepad_axis(slot, axis));
}

static void expect_out_of_range_reads_are_empty(int* utest_result, u32 slot) {
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
}

static void expect_press_frame_reads(int* utest_result, u32 slot) {
    EXPECT_TRUE_MSG(fl_input_gamepad_button_pressed(slot, FlGamepadButton_B), "press edge reads on the press frame");
    EXPECT_FALSE_MSG(fl_input_gamepad_button_released(slot, FlGamepadButton_B), "no release on the press frame");
    EXPECT_FALSE_MSG(fl_input_gamepad_button_pressed(slot, FlGamepadButton_A), "an untouched button has no edge");
    const bool other_slot_pressed = fl_input_gamepad_button_pressed(slot - 1, FlGamepadButton_B);
    EXPECT_FALSE_MSG(other_slot_pressed, "the edge is the slot's own");
    EXPECT_EQ(0.25f, fl_input_gamepad_button_hold_duration(slot, FlGamepadButton_B));
    EXPECT_EQ(0.0f, fl_input_gamepad_button_hold_duration(slot, FlGamepadButton_A));
    EXPECT_EQ(0.75f, fl_input_gamepad_axis(slot, FlGamepadAxis_Left_Trigger));
    const f32 right_x = fl_input_gamepad_axis(slot, FlGamepadAxis_Right_X);
    EXPECT_EQ(-0.1f, right_x);
    EXPECT_EQ(0.0f, fl_input_gamepad_axis(slot, FlGamepadAxis_Left_X));
}

static void expect_held_frames_reads(int* utest_result, u32 slot) {
    EXPECT_FALSE_MSG(fl_input_gamepad_button_pressed(slot, FlGamepadButton_B), "the press edge lasts one frame");
    EXPECT_TRUE_MSG(fl_input_gamepad_button_down(slot, FlGamepadButton_B), "the button is still held");
    EXPECT_EQ(1.0f, fl_input_gamepad_button_hold_duration(slot, FlGamepadButton_B));
    EXPECT_EQ(0.75f, fl_input_gamepad_axis(slot, FlGamepadAxis_Left_Trigger));
}

static void expect_release_frame_reads(int* utest_result, u32 slot) {
    EXPECT_TRUE_MSG(fl_input_gamepad_button_released(slot, FlGamepadButton_B), "release edge reads on release");
    EXPECT_FALSE_MSG(fl_input_gamepad_button_pressed(slot, FlGamepadButton_B), "no press on the release frame");
    EXPECT_EQ(0.0f, fl_input_gamepad_button_hold_duration(slot, FlGamepadButton_B));
}

static void expect_tap_reads(int* utest_result, u32 slot) {
    EXPECT_TRUE_MSG(fl_input_gamepad_button_pressed(slot, FlGamepadButton_X), "tap: pressed set");
    EXPECT_TRUE_MSG(fl_input_gamepad_button_released(slot, FlGamepadButton_X), "tap: released set in the same frame");
    EXPECT_EQ(0.0f, fl_input_gamepad_button_hold_duration(slot, FlGamepadButton_X));
}

UTEST(input, gamepad_edges_hold_duration_and_axis_read_back_per_slot) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    canvas_fixture_reset(ctx);

    // fl_input_* read the global context, and one frame binds it to this fixture.
    ui_update_begin(ctx);
    ui_update_end(ctx, 1.0f / 60.0f);

    const u32 slot = 3;
    const GamepadState saved = ctx->input_state.gamepads[slot];
    const f64 dt = 0.25;

    platform_input_push_gamepad_event(ctx, (FlGamepadEvent) { .type = FlGamepadEvent_Connected, .gamepad_id = slot });
    push_gamepad_button(ctx, slot, FlGamepadEvent_Button_Down, FlGamepadButton_B);
    push_gamepad_axis(ctx, slot, FlGamepadAxis_Left_Trigger, 0.75f);
    // Inside the default dead zone the read is still the raw value.
    push_gamepad_axis(ctx, slot, FlGamepadAxis_Right_X, -0.1f);
    ui_input_update(ctx, dt);
    expect_press_frame_reads(utest_result, slot);

    for_count(i, 3) {
        ui_input_update(ctx, dt);
    }
    expect_held_frames_reads(utest_result, slot);

    push_gamepad_button(ctx, slot, FlGamepadEvent_Button_Up, FlGamepadButton_B);
    ui_input_update(ctx, dt);
    expect_release_frame_reads(utest_result, slot);

    ui_input_update(ctx, dt);
    EXPECT_FALSE_MSG(fl_input_gamepad_button_released(slot, FlGamepadButton_B), "the release edge lasts one frame");

    // Down and up arrive before one update, so both edges read and nothing is held.
    push_gamepad_button(ctx, slot, FlGamepadEvent_Button_Down, FlGamepadButton_X);
    push_gamepad_button(ctx, slot, FlGamepadEvent_Button_Up, FlGamepadButton_X);
    ui_input_update(ctx, dt);
    expect_tap_reads(utest_result, slot);

    // Out-of-range buttons and axes on a connected slot read as false and 0.
    expect_out_of_range_reads_are_empty(utest_result, slot);

    // A pad unplugged while a button is down reads as an empty slot: no edge, no hold, every axis at 0.
    push_gamepad_button(ctx, slot, FlGamepadEvent_Button_Down, FlGamepadButton_Y);
    ui_input_update(ctx, dt);
    platform_input_push_gamepad_event(ctx,
                                      (FlGamepadEvent) { .type = FlGamepadEvent_Disconnected, .gamepad_id = slot });
    EXPECT_FALSE_MSG(fl_input_gamepad_button_pressed(slot, FlGamepadButton_Y), "an empty slot has no press edge");
    const f32 unplugged_hold = fl_input_gamepad_button_hold_duration(slot, FlGamepadButton_Y);
    EXPECT_EQ(0.0f, unplugged_hold);
    EXPECT_EQ(0.0f, fl_input_gamepad_axis(slot, FlGamepadAxis_Left_Trigger));

    expect_slot_reads_empty(utest_result, UI_INPUT_MAX_GAMEPADS, FlGamepadButton_A, FlGamepadAxis_Left_X);

    ctx->input_state.gamepads[slot] = saved;
    ui_input_update(ctx, dt);
    canvas_fixture_reset(ctx);
}
