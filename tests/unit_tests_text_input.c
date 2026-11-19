// UI text input and typed text tests

#include "widgets/common.h"
#include <math.h>

// One frame holding a single text input, with a physical keyboard so no virtual keyboard opens. An aimed field is
// given focus every frame, which makes the case independent of focus auto-restore.
static FlSignal text_input_field_frame(FlString name, FlTextInputState* input, bool aimed) {
    FlUiCtx* ctx = g_ui_ctx;
    ctx->physical_keyboard_available = true;
    if (aimed) {
        ctx->focused_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = name, .id = 0 });
    }
    FlSignal signal = { 0 };
    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {
        signal = ui_text_input(name, input, (FlTextInputConfig) { 0 });
    }
    return signal;
}

// Typed text survives the real fl_app tick order (pump -> ui_input_update -> frame body). ui_input_update copies
// kb->text_input into ctx->text_input_this_frame before clearing the raw field, and ui_text_input reads that
// copy, so a character injected before the tick reaches the widget's buffer.

typedef struct TypedTextState {
    FlUiCtx* ctx;
    FlTextInputState input;
} TypedTextState;

static void typed_text_frame(void* user_data) {
    TypedTextState* st = (TypedTextState*)user_data;
    st->ctx = g_ui_ctx;
    text_input_field_frame(FL_S("typed_input"), &st->input, true);
}

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

static void expect_vec2_eq(int* utest_result, FlVec2 got, f32 want_x, f32 want_y) {
    EXPECT_EQ(want_x, got.x);
    EXPECT_EQ(want_y, got.y);
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
    const char* want_paths[] = { "/tmp/kick.wav", "/tmp/snare.wav" };
    const FlString got_paths[] = { st.first, st.second };
    for (size_t i = 0; i < sizeof_array(want_paths); i++) {
        EXPECT_EQ(0, memcmp(got_paths[i].data, want_paths[i], strlen(want_paths[i])));
    }
    expect_vec2_eq(utest_result, st.first_position, 120.0f, 64.0f);

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

    // Let the widget register and focus propagate. signal.focused lags focused_id by a frame.
    for (int f = 0; f < 4; f++) {
        fl_app_tick(app, &st, typed_text_frame);
    }
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");

    // Type one character. inject_text queues it in the headless backend, and the next tick's pump drains it into
    // kb->text_input before ui_input_update runs.
    fl_app_inject_text(app, FL_S("a"));
    fl_app_tick(app, &st, typed_text_frame);

    FlString got = ui_text_input_get_text(&st.input);
    ASSERT_EQ_MSG((int)got.length, 1, "the typed character reached the text input buffer");
    ASSERT_TRUE_MSG(fl_string_equals(got, FL_S("a")), "buffered character is what was typed");

    fl_app_destroy(app);
}

// ui_text_input reports text-changed, submit and cancel as one-frame edges on the returned signal.

typedef struct TextInputEdgesState {
    FlTextInputState input;
    FlSignal signal;
} TextInputEdgesState;

static void text_input_edges_frame(void* user_data) {
    TextInputEdgesState* st = (TextInputEdgesState*)user_data;
    st->signal = text_input_field_frame(FL_S("edges_input"), &st->input, true);
}

// Creates the headless app and settles the field. A failed ASSERT_* leaves *out_app null, and callers end the case.
static void text_input_edges_start(int* utest_result, TextInputEdgesState* st, FlApp** out_app) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("edges"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");
    for (int f = 0; f < 4; f++) {
        fl_app_tick(app, st, text_input_edges_frame);
    }
    *out_app = app;
}

// The edge a held key raised on its press frame, which has to be gone on every frame after it.
typedef struct HeldKeyEdge {
    FlKey key;
    bool (*edge)(const FlSignal* signal);
} HeldKeyEdge;

static bool signal_submitted(const FlSignal* signal) {
    return signal->submitted;
}

static bool signal_cancelled(const FlSignal* signal) {
    return signal->cancelled;
}

// Run after the press frame: the edge stays down while the key is held, after its release and one frame later.
static void expect_edge_is_one_frame(int* utest_result, FlApp* app, TextInputEdgesState* st, HeldKeyEdge held) {
    fl_app_tick(app, st, text_input_edges_frame);
    EXPECT_FALSE(held.edge(&st->signal)); // key still held

    fl_app_inject_key(app, held.key, false);
    fl_app_tick(app, st, text_input_edges_frame);
    EXPECT_FALSE(held.edge(&st->signal));

    fl_app_tick(app, st, text_input_edges_frame);
    EXPECT_FALSE(held.edge(&st->signal));
}

UTEST(input, text_input_reports_text_changed_on_the_edit_frame_only) {
    TextInputEdgesState st = { 0 };
    FlApp* app = nullptr;
    text_input_edges_start(utest_result, &st, &app);
    RETURN_IF_TEST_FAILED();
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
    TextInputEdgesState st = { 0 };
    FlApp* app = nullptr;
    text_input_edges_start(utest_result, &st, &app);
    RETURN_IF_TEST_FAILED();

    fl_app_inject_text(app, FL_S("a"));
    fl_app_tick(app, &st, text_input_edges_frame);

    fl_app_inject_key(app, FlKey_Enter, true);
    fl_app_tick(app, &st, text_input_edges_frame);
    EXPECT_TRUE(st.signal.submitted);
    EXPECT_FALSE(st.signal.cancelled);
    EXPECT_FALSE(st.signal.text_changed);

    expect_edge_is_one_frame(utest_result, app, &st, (HeldKeyEdge) { FlKey_Enter, signal_submitted });

    fl_app_destroy(app);
}

UTEST(input, text_input_typing_a_space_edits_the_text_and_does_not_submit) {
    TextInputEdgesState st = { 0 };
    FlApp* app = nullptr;
    text_input_edges_start(utest_result, &st, &app);
    RETURN_IF_TEST_FAILED();

    fl_app_inject_key(app, FlKey_Space, true);
    fl_app_inject_text(app, FL_S(" "));
    fl_app_tick(app, &st, text_input_edges_frame);
    EXPECT_TRUE(st.signal.text_changed);
    EXPECT_FALSE(st.signal.submitted);
    EXPECT_FALSE(st.signal.cancelled);
    const int typed_length = (int)ui_text_input_get_text(&st.input).length;
    EXPECT_EQ(typed_length, 1);

    fl_app_destroy(app);
}

UTEST(input, text_input_reports_cancel_on_the_escape_frame_only) {
    TextInputEdgesState st = { 0 };
    FlApp* app = nullptr;
    text_input_edges_start(utest_result, &st, &app);
    RETURN_IF_TEST_FAILED();

    fl_app_inject_key(app, FlKey_Escape, true);
    fl_app_tick(app, &st, text_input_edges_frame);
    EXPECT_TRUE(st.signal.cancelled);
    EXPECT_FALSE(st.signal.submitted);

    expect_edge_is_one_frame(utest_result, app, &st, (HeldKeyEdge) { FlKey_Escape, signal_cancelled });

    fl_app_destroy(app);
}

// Shift held with a cursor key extends the selection from where the cursor was.

static void text_input_tap(FlApp* app, TextInputEdgesState* st, FlKey key) {
    fl_app_inject_key(app, key, true);
    fl_app_tick(app, st, text_input_edges_frame);
    fl_app_inject_key(app, key, false);
    fl_app_tick(app, st, text_input_edges_frame);
}

// Number of selected characters, signed: negative when the cursor end is left of the anchor.
static int text_input_selected(const TextInputEdgesState* st) {
    const STB_TexteditState* stb = (const STB_TexteditState*)st->input.stb_state;
    return stb->select_end - stb->select_start;
}

static void text_input_shift_start(int* utest_result, TextInputEdgesState* st, FlApp** out_app) {
    FlApp* app = nullptr;
    text_input_edges_start(utest_result, st, &app);
    RETURN_IF_TEST_FAILED();
    fl_app_inject_text(app, FL_S("ab cd"));
    fl_app_tick(app, st, text_input_edges_frame);
    fl_app_inject_key(app, FlKey_Left_Shift, true);
    *out_app = app;
}

// One key tap and the signed selection length it has to leave behind.
typedef struct SelectionStep {
    FlKey key;
    int selected;
} SelectionStep;

typedef struct SelectionSteps {
    const SelectionStep* steps;
    size_t count;
} SelectionSteps;

#define SELECTION_STEPS(...)                                              \
    ((SelectionSteps) { .steps = (const SelectionStep[]) { __VA_ARGS__ }, \
                        .count = sizeof((const SelectionStep[]) { __VA_ARGS__ }) / sizeof(SelectionStep) })

static void expect_selection_steps(int* utest_result, FlApp* app, TextInputEdgesState* st, SelectionSteps run) {
    for (size_t i = 0; i < run.count; i++) {
        text_input_tap(app, st, run.steps[i].key);
        EXPECT_EQ(text_input_selected(st), run.steps[i].selected);
    }
}

UTEST(input, text_input_shift_arrows_extend_the_selection) {
    TextInputEdgesState st = { 0 };
    FlApp* app = nullptr;
    text_input_shift_start(utest_result, &st, &app);
    RETURN_IF_TEST_FAILED();

    expect_selection_steps(utest_result, app, &st,
                           SELECTION_STEPS({ FlKey_Left, -1 }, { FlKey_Left, -2 }, { FlKey_Right, -1 }));

    fl_app_destroy(app);
}

UTEST(input, text_input_shift_home_and_end_select_to_the_line_ends) {
    TextInputEdgesState st = { 0 };
    FlApp* app = nullptr;
    text_input_shift_start(utest_result, &st, &app);
    RETURN_IF_TEST_FAILED();

    expect_selection_steps(utest_result, app, &st, SELECTION_STEPS({ FlKey_Home, -5 }, { FlKey_End, 0 }));

    fl_app_destroy(app);
}

UTEST(input, text_input_ctrl_shift_arrows_select_by_word) {
    TextInputEdgesState st = { 0 };
    FlApp* app = nullptr;
    text_input_shift_start(utest_result, &st, &app);
    RETURN_IF_TEST_FAILED();

    fl_app_inject_key(app, FlKey_Left_Control, true);
    // Word-right lands on the start of the next word.
    expect_selection_steps(utest_result, app, &st,
                           SELECTION_STEPS({ FlKey_Left, -2 }, { FlKey_Left, -5 }, { FlKey_Right, -2 }));

    fl_app_destroy(app);
}

UTEST(input, text_input_typing_replaces_a_shift_selection) {
    TextInputEdgesState st = { 0 };
    FlApp* app = nullptr;
    text_input_shift_start(utest_result, &st, &app);
    RETURN_IF_TEST_FAILED();

    text_input_tap(app, &st, FlKey_Left);
    text_input_tap(app, &st, FlKey_Left);
    fl_app_inject_key(app, FlKey_Left_Shift, false);
    fl_app_inject_text(app, FL_S("X"));
    fl_app_tick(app, &st, text_input_edges_frame);

    FlString got = ui_text_input_get_text(&st.input);
    EXPECT_TRUE_MSG(fl_string_equals(got, FL_S("ab X")), "the two selected characters are replaced");

    fl_app_destroy(app);
}

// The same field, never given focus. It is the frame's first focusable widget, so a focus auto-restore that
// seeded on an idle frame would hand the keyboard to it.

typedef struct UnaimedTextState {
    FlUiCtx* ctx;
    FlTextInputState input;
} UnaimedTextState;

static void unaimed_text_frame(void* user_data) {
    UnaimedTextState* st = (UnaimedTextState*)user_data;
    st->ctx = g_ui_ctx;
    text_input_field_frame(FL_S("unaimed_input"), &st->input, false);
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

// fl_input_text_input_active reports whether a field had keyboard focus in the frame that ended last, which is the
// frame an app polling after the tick has just seen. It turns false once the field loses focus.

typedef struct EditingTextState {
    FlUiCtx* ctx;
    FlTextInputState input;
    bool aimed;
} EditingTextState;

static void editing_text_frame(void* user_data) {
    EditingTextState* st = (EditingTextState*)user_data;
    st->ctx = g_ui_ctx;
    text_input_field_frame(FL_S("editing_input"), &st->input, st->aimed);
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
    st->ctx = g_ui_ctx;
    st->ctx->physical_keyboard_available = true;
    ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {
        st->signal = fl_text_input_show(FL_S("configured_input"), st->input, .placeholder = FL_S("type here"),
                                        .min_width = 150.0f, .max_length = 2,
                                        .widget = { .focusable = FlFocusable_Yes, .auto_focus = true });
        fl_button(FL_S("other_focus_target"), .focusable = FlFocusable_Yes);
    }
}

// The caret's position is measured through the active font, so this frame installs a real FreeType face. The
// built-in bitmap default reports the same metrics at every size, which would hide the scale.
static FlFontId s_caret_font;

static float s_caret_min_width = 300.0f; // narrowed by the scrolling case so the text overflows its box

static void scaled_caret_frame(void* user_data) {
    PublicTextInputState* st = (PublicTextInputState*)user_data;
    st->ctx = g_ui_ctx;
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

// The caret is the only rectangle in this frame that is two authored px wide, so its width identifies it. The
// blink hides it half the time, and the caller ticks until it appears. Returns false while it is hidden.
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

// The text-input caret is a floating element configured directly through Clay and not through
// fl_floating_configure, so the widget applies the scale to it by hand. It also measures how far along the
// string the caret is, with a font size that is authored and not yet scaled. The scale is set with
// fl_app_set_ui_zoom, as in a real app. A direct ui_set_scale would be overwritten by the tick loop.
UTEST(input, text_input_caret_follows_the_scale_with_the_text) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("caret scale"), .width = 800, .height = 200, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    s_caret_font = 0; // the frame callback installs the face on its first run
    s_caret_min_width = 300.0f;
    PublicTextInputState st = { .input = fl_text_input_create(64) };
    ASSERT_TRUE_MSG(st.input != nullptr, "opaque text-input state creation failed");
    // Only 'A' and 'o' are mapped in the embedded test face. Unmapped glyphs measure zero and would leave the
    // caret on the padding, where the assertion below holds trivially.
    fl_text_input_set_text(st.input, FL_S("AAAAAAAA"));

    for (int f = 0; f < 4; f++) {
        fl_app_tick(app, &st, scaled_caret_frame);
    }
    ASSERT_TRUE_MSG(st.signal.focused, "the caret only renders on a focused input");

    CaretRect at_1 = { 0 };
    ASSERT_TRUE_MSG(tick_to_caret(app, &st, 2.0f, &at_1), "the caret must render at identity scale");
    const float caret_at_1 = at_1.x;
    // Well past the padding. With a zero-width measurement (unmapped glyphs, or a caret still at index 0) the
    // doubling assertion below would pass on the padding alone.
    ASSERT_TRUE_MSG(caret_at_1 > 50.0f, "the caret sits several characters into the string, not on the padding");

    fl_app_set_ui_zoom(app, 2.0f);
    for (int f = 0; f < 4; f++) {
        fl_app_tick(app, &st, scaled_caret_frame);
    }

    CaretRect at_2 = { 0 };
    ASSERT_TRUE_MSG(tick_to_caret(app, &st, 4.0f, &at_2),
                    "the caret must still render at double scale, and at double its authored width");
    const float caret_at_2 = at_2.x;

    // A band and not an equality: glyph advances are hinted again at the larger ppem, so eight of them at 32px
    // are not exactly twice eight at 16px (this face gives about 1.8x). A caret measured at the authored size
    // and not the scaled one barely moves, about 1.0x, far below the lower bound.
    // The caret's own box. Its height is the text height (hinted again at the larger ppem, so a band again) plus
    // an authored 4px, and its y is the authored vertical centring inside the box. Both use the same hand-applied
    // scaling as the offset. Without it a 2x caret would be drawn at 1x proportions inside doubled chrome.
    ASSERT_TRUE_CHECKS(
        TRUE_CHECK(caret_at_2 > caret_at_1 * 1.6f,
                   "the caret's distance along the text grows with the scale, like the glyphs it follows"),
        TRUE_CHECK(caret_at_2 < caret_at_1 * 2.2f, "and does not overshoot, which would mean a double-scale"),
        TRUE_CHECK(at_2.height > at_1.height * 1.6f, "the caret grows in height with the type it marks"),
        TRUE_CHECK(at_2.height < at_1.height * 2.3f, "without overshooting"),
        TRUE_CHECK(at_1.y > 0.0f, "the caret is centred inside the box, not pinned to its top edge"),
        TRUE_CHECK(at_2.y > at_1.y * 1.5f, "and its vertical centring tracks the scaled box"));

    fl_app_set_ui_zoom(app, 1.0f);
    fl_text_input_destroy(st.input);
    fl_app_destroy(app);
}

// The scrolled case. Once the string is wider than its box, the scroll offset is clamped to text width -
// viewport, so the caret's position is where the viewport ends. Here the viewport width and the 10px trigger
// margin have to be scaled, because they are authored lengths compared with a measurement already in screen
// coordinates. The assertion is exact: with the clamp saturated the caret is at `inner_padding * scale +
// viewport * scale`, all layout constants, so the glyph hinting that needed a band above does not enter.
UTEST(input, text_input_caret_stays_in_the_box_when_the_text_scrolls) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("caret scroll"), .width = 800, .height = 200, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    s_caret_font = 0;
    s_caret_min_width = 90.0f; // narrow enough that the string below overflows it at both scales
    PublicTextInputState st = { .input = fl_text_input_create(64) };
    ASSERT_TRUE_MSG(st.input != nullptr, "opaque text-input state creation failed");
    fl_text_input_set_text(st.input, FL_S("AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")); // 30 mapped glyphs

    // The scroll offset eases toward its target (0.3 per frame), so let it settle before reading the caret. A
    // caret read mid-ease would depend on the tick count.
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

    // An unscaled viewport would clamp the scroll against about half the box, which leaves the caret near its 1x
    // position (about 1.1x) when it should follow the box out to 2x.
    ASSERT_NEAR_MSG(caret_at_2, caret_at_1 * 2.0f, caret_at_1 * 2.0f * 0.05f,
                    "the scrolled caret tracks the scaled viewport edge, not the authored one");

    fl_app_set_ui_zoom(app, 1.0f);
    s_caret_min_width = 300.0f;
    fl_text_input_destroy(st.input);
    fl_app_destroy(app);
}

static bool is_text_run(const UiRenderCommand* cmd, const char* text, size_t length) {
    return cmd->type == UiRenderCmd_Text && cmd->text.length == length
           && memory_compare(cmd->text.data, text, length) == 0;
}

// True if the frame drew a text run holding exactly text.
static bool text_run_rendered(const char* text) {
    const size_t length = strlen(text);
    for (int i = 0; i < ui_get_render_command_count(); i++) {
        UiRenderCommand cmd;
        if (ui_get_render_command(i, &cmd) && is_text_run(&cmd, text, length)) {
            return true;
        }
    }
    return false;
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
    ASSERT_TRUE_MSG(text_run_rendered("type here"), "configured placeholder reaches text rendering");

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

// ui_text_input_paste filters and truncates pasted text as typed text is, and the selection it replaces frees its
// own capacity.

static void paste_text_state(FlTextInputState* state, const char* text, int sel_start, int sel_end) {
    ui_text_input_init(state, s_arena);
    ui_text_input_set_text(state, string_from_cstr_len(text, strlen(text)));
    STB_TexteditState* stb = (STB_TexteditState*)state->stb_state;
    stb->select_start = sel_start;
    stb->select_end = sel_end;
    stb->cursor = sel_end;
}

static bool paste_equals(const FlTextInputState* state, const char* expected) {
    FlString got = ui_text_input_get_text(state);
    return got.length == strlen(expected) && memcmp(got.data, expected, got.length) == 0;
}

UTEST(text_input, paste_drops_disallowed_characters) {
    FlTextInputState state;
    paste_text_state(&state, "", 0, 0);
    FlTextInputConfig cfg = { .allowed_chars = FL_S("0123456789") };

    ASSERT_TRUE(ui_text_input_paste(&state, &cfg, FL_S("1a2-3 b4")));
    ASSERT_TRUE_MSG(paste_equals(&state, "1234"), "only allow-listed characters are pasted");

    const bool pasted_disallowed = ui_text_input_paste(&state, &cfg, FL_S("xyz"));
    ASSERT_FALSE_MSG(pasted_disallowed, "nothing allowed means nothing pasted");
    ASSERT_TRUE(paste_equals(&state, "1234"));
}

UTEST(text_input, paste_truncates_to_remaining_capacity) {
    FlTextInputState state;
    paste_text_state(&state, "ab", 2, 2);
    FlTextInputConfig cfg = { .max_length = 5 };

    ASSERT_TRUE(ui_text_input_paste(&state, &cfg, FL_S("123456789")));
    ASSERT_TRUE_MSG(paste_equals(&state, "ab123"), "the paste is cut to the room left, not dropped");

    const bool pasted_into_full_field = ui_text_input_paste(&state, &cfg, FL_S("zz"));
    ASSERT_FALSE_MSG(pasted_into_full_field, "a full field takes nothing");
    ASSERT_TRUE(paste_equals(&state, "ab123"));
}

UTEST(text_input, paste_drops_non_ascii_bytes_like_typed_text) {
    FlTextInputState state;
    paste_text_state(&state, "", 0, 0);
    FlTextInputConfig cfg = { .max_length = 4 };

    ASSERT_TRUE(ui_text_input_paste(&state, &cfg,
                                    FL_S("ab\xC3\xA9\xC3\xA9"
                                         "cd")));
    ASSERT_TRUE_MSG(paste_equals(&state, "abcd"), "default filtering admits printable ASCII only");
}

UTEST(text_input, paste_replaces_the_selection_and_counts_its_room) {
    FlTextInputState state;
    paste_text_state(&state, "abcde", 1, 4); // "bcd" selected
    FlTextInputConfig cfg = { .max_length = 5, .allowed_chars = FL_S("0123456789") };

    ASSERT_TRUE(ui_text_input_paste(&state, &cfg, FL_S("1x2y3z4567")));
    ASSERT_TRUE_MSG(paste_equals(&state, "a123e"), "filtered, cut to the selection's room, and replacing it");
}
