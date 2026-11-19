// UI interaction feedback, widget state color and cursor tests

#include "widgets/common.h"
#include <math.h>

// flowi reports what happened in the interaction layer (focus moved, a nav edge was hit, a widget was activated)
// through one app-installed feedback hook. These cases record the callback headlessly and check the emitted
// kind, from, to and dir.

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

// Lays out two stacked focusable areas, A above B, and runs one frame. That registers their positions in
// item_state, so the next frame's spatial nav has bounding boxes to work with.
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

// Lays out a single focusable area, runs one frame and returns its interaction signal. Used to settle a widget's
// focus state across frames and to run the activation frame.
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

// Two stacked focusable widgets and the nav-request flag one scenario raises for its recorded frame.
typedef struct NavSetup {
    FlUiCtx* ctx;
    FlLayoutId id_a;
    FlLayoutId id_b;
    bool* nav_flag;
} NavSetup;

// Emits the first frame, which establishes positions and focus order, and focuses A. Then runs one frame with the
// nav flag raised and the recorder installed as the feedback hook.
static void feedback_run_nav(const NavSetup* nav, FeedbackRecorder* rec) {
    FlUiCtx* ctx = nav->ctx;
    ctx->focused_id = 0;
    ctx->last_focused_id = 0;
    feedback_emit_two_focusables(ctx, nav->id_a, nav->id_b);
    ctx->focused_id = ui_layout_id_to_clay_id(nav->id_a);

    fl_ui_set_feedback_hook(feedback_record, rec);
    *nav->nav_flag = true;
    feedback_emit_two_focusables(ctx, nav->id_a, nav->id_b);
    *nav->nav_flag = false;
    fl_ui_set_feedback_hook(nullptr, nullptr);
}

static void assert_single_feedback_event(int* utest_result, const FeedbackRecorder* rec, FlUiFeedbackKind kind,
                                         FlUiFeedbackDir dir) {
    ASSERT_EQ_MSG(rec->count, 1, "exactly one feedback event is emitted");
    ASSERT_EQ_MSG((int)rec->events[0].kind, (int)kind, "the event has the expected kind");
    ASSERT_EQ_MSG((int)rec->events[0].dir, (int)dir, "the event carries the expected direction");
}

// A NavMoved from A to B in direction dir, with focus now on B.
static void assert_nav_moved_a_to_b(int* utest_result, const NavSetup* nav, const FeedbackRecorder* rec,
                                    FlUiFeedbackDir dir) {
    assert_single_feedback_event(utest_result, rec, FlUiFeedbackKind_NavMoved, dir);
    RETURN_IF_TEST_FAILED();
    ASSERT_EQ_MSG(rec->events[0].from, ui_layout_id_to_clay_id(nav->id_a), "from is the previously focused widget (A)");
    ASSERT_EQ_MSG(rec->events[0].to, ui_layout_id_to_clay_id(nav->id_b), "to is the newly focused widget (B)");
    ASSERT_EQ_MSG(nav->ctx->focused_id, ui_layout_id_to_clay_id(nav->id_b), "focus actually moved to B");
}

// Arrow or D-pad spatial nav from A down to B emits exactly one NavMoved with the from and to widget ids and the
// pressed direction.
UTEST(feedback, nav_moved_spatial) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const NavSetup nav = { ctx, ID("fb_nav_a"), ID("fb_nav_b"), &ctx->nav_frame.nav_down };
    FeedbackRecorder rec = { 0 };
    feedback_run_nav(&nav, &rec);
    assert_nav_moved_a_to_b(utest_result, &nav, &rec, FlUiFeedbackDir_Down);
}

// Tab and Shift+Tab focus movement goes through ui_navigate_tab_order, separate from spatial nav, and reports
// dir == None. A is declared before B, so Tab from A lands on B.
UTEST(feedback, nav_moved_tab_order) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const NavSetup nav = { ctx, ID("fb_tab_a"), ID("fb_tab_b"), &ctx->nav_next_pressed };
    FeedbackRecorder rec = { 0 };
    feedback_run_nav(&nav, &rec);
    assert_nav_moved_a_to_b(utest_result, &nav, &rec, FlUiFeedbackDir_None);
}

// Pressing a direction with no candidate (up from the topmost widget) emits NavBlocked. Focus stays where it is
// and to is 0.
UTEST(feedback, nav_blocked_edge) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // A is the topmost widget, with nothing above it.
    const NavSetup nav = { ctx, ID("fb_edge_a"), ID("fb_edge_b"), &ctx->nav_frame.nav_up };
    FeedbackRecorder rec = { 0 };
    feedback_run_nav(&nav, &rec);

    assert_single_feedback_event(utest_result, &rec, FlUiFeedbackKind_NavBlocked, FlUiFeedbackDir_Up);
    RETURN_IF_TEST_FAILED();
    ASSERT_EQ_MSG(rec.events[0].from, ui_layout_id_to_clay_id(nav.id_a), "from is the widget that stayed focused (A)");
    ASSERT_EQ_MSG(rec.events[0].to, 0u, "to is 0 — no widget was moved to");
    ASSERT_EQ_MSG(ctx->focused_id, ui_layout_id_to_clay_id(nav.id_a), "focus stayed on A");
}

// Activating the focused widget (Enter, Space or gamepad A) emits an Activated event naming the widget.
UTEST(feedback, activated) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    ctx->focused_id = 0;
    ctx->last_focused_id = 0;

    const FlLayoutId id = ID("fb_act");
    const u32 clay_id = ui_layout_id_to_clay_id(id);

    // Warm-up frames keep the widget focused with no activate press, so its focused state gets past the
    // just-gained-focus check.
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
    assert_single_feedback_event(utest_result, &rec, FlUiFeedbackKind_Activated, FlUiFeedbackDir_None);
    RETURN_IF_TEST_FAILED();
    ASSERT_EQ_MSG(rec.events[0].to, clay_id, "to is the activated widget");
}

// With no hook installed, which is the default, a nav move records nothing.
UTEST(feedback, null_hook_no_emit) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    const FlLayoutId id_a = ID("fb_null_a");
    const FlLayoutId id_b = ID("fb_null_b");
    ctx->focused_id = 0;
    ctx->last_focused_id = 0;
    feedback_emit_two_focusables(ctx, id_a, id_b);
    ctx->focused_id = ui_layout_id_to_clay_id(id_a);

    fl_ui_set_feedback_hook(nullptr, nullptr);
    ctx->nav_frame.nav_down = true;
    feedback_emit_two_focusables(ctx, id_a, id_b);
    ctx->nav_frame.nav_down = false;

    const u32 clay_b = ui_layout_id_to_clay_id(id_b);
    ASSERT_EQ_MSG(ctx->focused_id, clay_b, "the move still happened (nav is independent of the hook)");
}

// The base colors each variant derives from: Solid takes interactive.base and text.primary, Outline the
// surface.base background, Ghost the secondary text, Soft the accent text.

typedef struct VariantBaseCase {
    bool is_text;
    FlWidgetVariant variant;
    FlColor expected;
    const char* message;
} VariantBaseCase;

UTEST(widget_state_colors, variant_base_derivation) {
    static const FlThemeColors theme = {
        .interactive.base = { 200, 30, 40, 255 },
        .surface.base = { 18, 20, 24, 255 },
        .text.primary = { 240, 240, 240, 255 },
        .text.secondary = { 150, 150, 150, 255 },
        .text.accent = { 200, 30, 40, 255 },
    };

    const VariantBaseCase cases[] = {
        { false, FlWidgetVariant_Solid, theme.interactive.base, "Solid bg derives from interactive.base" },
        { false, FlWidgetVariant_Outline, theme.surface.base, "Outline bg derives from surface.base" },
        { true, FlWidgetVariant_Solid, theme.text.primary, "Solid text derives from text.primary" },
        { true, FlWidgetVariant_Ghost, theme.text.secondary, "Ghost text derives from text.secondary" },
        { true, FlWidgetVariant_Soft, theme.text.accent, "Soft text derives from text.accent" },
    };
    for (size_t i = 0; i < sizeof_array(cases); i++) {
        const FlColor derived = cases[i].is_text ? ui_get_variant_text(cases[i].variant, &theme)
                                                 : ui_get_variant_bg(cases[i].variant, &theme);
        ASSERT_TRUE_MSG(color_eq(derived, cases[i].expected), cases[i].message);
    }
}

// The per-frame cursor request goes to the last writer and resets to the arrow when nobody asks. ui_update_begin
// resets the request, fl_cursor_set overwrites it during the frame, and the resolved value
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

    // Frame 1: Hand, the last writer, replaces the earlier Crosshair request.
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

    // Visibility is a toggle that stays set, and it is flushed independently of the per-frame shape.
    fl_cursor_set_visible(false);
    wb.set_cursor_visible(wb.user_data, w, fl_cursor_get_visible());
    ASSERT_FALSE_MSG(window_backend_headless_cursor_visible(&wb), "set_visible(false) hides the cursor");
    fl_cursor_set_visible(true);
    wb.set_cursor_visible(wb.user_data, w, fl_cursor_get_visible());
    ASSERT_TRUE_MSG(window_backend_headless_cursor_visible(&wb), "set_visible(true) shows it again");

    wb.destroy_window(wb.user_data, w);
    wb.destroy(wb.user_data);
}

// fl_resolve_widget_colors lightens the background on hover, darkens it on press, and moves the border toward
// interactive.focus under focus, all in OKLCH.
UTEST(widget_state_colors, perceptual_state_contract) {
    FlThemeColors theme = { 0 };
    theme.interactive.base = (FlColor) { 128, 128, 128, 255 }; // mid lightness, so lightening and darkening both show
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

    // The Solid border has a transparent base, so at focused_t = 1 it is exactly interactive.focus.
    ItemState focus = { 0 };
    focus.state.focused_t = 1.0f;
    FlWidgetColors c_focus
        = fl_resolve_widget_colors(FlWidgetVariant_Solid, (FlColor) { 0 }, (FlColor) { 0 }, &focus, &theme, false);
    ASSERT_TRUE_MSG(color_eq(c_focus.border, theme.interactive.focus),
                    "focus drives the derived border onto interactive.focus");
}

// ui_apply_state_color derives from the base color only: hover lightens by 0.15 * hot_t, press then darkens that
// result by 0.15 * active_t, and a null or resting item returns the base. Every variant's resolved background and
// text go through the same derivation.
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
    for (size_t i = 0; i < sizeof_array(variants); ++i) {
        FlWidgetColors c = fl_resolve_widget_colors(variants[i], (FlColor) { 0 }, (FlColor) { 0 }, &both, &theme, true);
        ASSERT_TRUE_MSG(color_eq(c.bg, ui_apply_state_color(ui_get_variant_bg(variants[i], &theme), &both)),
                        "variant bg is the derived state color of its base");
        ASSERT_TRUE_MSG(color_eq(c.text, ui_apply_state_color(ui_get_variant_text(variants[i], &theme), &both)),
                        "variant text is the derived state color of its base");
    }
}
