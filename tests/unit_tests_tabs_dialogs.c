// UI tab bar, checkbox, message box and radio group tests

#include "widgets/common.h"
#include <math.h>

// FlTabBarConfig's item and strip fields set the tab geometry, and a zeroed config leaves the strip at its
// built-in geometry (the dock leaf header passes a zeroed one). The case checks the debug render-command
// snapshot, so the numbers in the assertions are the geometry.

typedef struct TabGeom {
    FlUiCtx* ctx;
    FlString tabs[3];
    int selected;
    UiRenderCommand rects[8];
    int count;
} TabGeom;

// Renders one tab bar with the given config and leaves its rectangles in rects and count. Each case takes two
// frames: the first registers each tab's ItemState for the hover and focus animation, and only the second is
// measured.
#define TAB_GEOM_FRAME(g, ...)                                                 \
    do {                                                                       \
        for (int geom_pass = 0; geom_pass < 2; geom_pass++) {                  \
            ui_update_begin((g)->ctx);                                         \
            ui_layout_area(.sizing = { sizing_grow(), sizing_grow() }) {       \
                ui_tab_bar((g)->tabs, 3, &(g)->selected, __VA_ARGS__);         \
            }                                                                  \
            ui_update_end((g)->ctx, 1.0f / 60.0f);                             \
        }                                                                      \
        (g)->count = render_cmd_collect(UiRenderCmd_Rectangle, (g)->rects, 8); \
    } while (0)

// Padding sets the size on both axes: 10px more padding per side is 20px more tab. The check is a delta against the
// skinned strip, so no font metric appears in the expected numbers.
static void tab_geom_check_skinned_strip(int* utest_result, TabGeom* g) {
    UiRenderCommand* rects = g->rects;
    TAB_GEOM_FRAME(g, .item_padding_h = 18.0f, .item_padding_v = 6.0f, .item_radius = 6.0f, .container_padding = 3.0f,
                   .container_radius = 8.0f);
    ASSERT_EQ_MSG(g->count, 4, "asking for strip padding/radius paints a pill behind the three tabs");
    const UiRenderCommand pill = rects[0];
    const float skin_h = rects[1].height;
    const float skin_w = rects[1].width;
    ASSERT_NEAR_CHECKS(NEAR_CHECK(pill.corner_radius.tl, 8.0, "the pill takes container_radius"),
                       NEAR_CHECK(rects[1].corner_radius.tl, 6.0, "the tabs take item_radius"),
                       NEAR_CHECK(rects[1].x - pill.x, 3.0, "container_padding insets the first tab"),
                       NEAR_CHECK(pill.height - skin_h, 6.0, "the pill hugs the tabs, padded top and bottom"),
                       NEAR_CHECK(pill.width, rects[3].x + rects[3].width + 3.0f - pill.x,
                                  "and hugs them across too, rather than stretching the row like the bare strip does"));
    ASSERT_LT_MSG((double)skin_h, (double)FlWidgetHeight_MD, "item padding takes the tab off the fixed height");

    TAB_GEOM_FRAME(g, .item_padding_h = 18.0f, .item_padding_v = 16.0f, .item_radius = 6.0f, .container_padding = 3.0f,
                   .container_radius = 8.0f);
    ASSERT_NEAR_CHECKS(NEAR_CHECK(rects[1].height - skin_h, 20.0, "vertical padding drives the tab height"));
    TAB_GEOM_FRAME(g, .item_padding_h = 28.0f, .item_padding_v = 6.0f, .item_radius = 6.0f, .container_padding = 3.0f,
                   .container_radius = 8.0f);
    ASSERT_NEAR_CHECKS(NEAR_CHECK(rects[1].width - skin_w, 20.0, "horizontal padding drives the tab width"));
}

// child_gap sets the space between tabs. The check is a delta against the 4px default, so no font metric
// appears in it: the strip's first tab does not move and the last one moves by two extra gaps. A negative
// value puts the second tab flush against the first, which 0 cannot ask for.
static void tab_geom_check_child_gap(int* utest_result, TabGeom* g) {
    UiRenderCommand* rects = g->rects;
    TAB_GEOM_FRAME(g, .item_padding_h = 18.0f, .item_padding_v = 6.0f, .container_padding = 3.0f);
    const float gap_default_last_x = rects[3].x;
    const float gap_default_pill_w = rects[0].width;
    ASSERT_NEAR_CHECKS(NEAR_CHECK(rects[2].x - (rects[1].x + rects[1].width), 4.0,
                                  "a zeroed child_gap keeps the built-in FlSpacing_XS between tabs"));
    TAB_GEOM_FRAME(g, .item_padding_h = 18.0f, .item_padding_v = 6.0f, .container_padding = 3.0f, .child_gap = 12.0f);
    ASSERT_NEAR_CHECKS(NEAR_CHECK(rects[3].x - gap_default_last_x, 16.0,
                                  "child_gap widens each of the two gaps ahead of the last tab by 8px"),
                       NEAR_CHECK(rects[0].width - gap_default_pill_w, 16.0, "and the pill grows with them"));
    TAB_GEOM_FRAME(g, .item_padding_h = 18.0f, .item_padding_v = 6.0f, .container_padding = 3.0f, .child_gap = -1.0f);
    ASSERT_NEAR_CHECKS(
        NEAR_CHECK(rects[2].x - (rects[1].x + rects[1].width), 0.0,
                   "a negative child_gap makes the tabs flush, which 0 cannot ask for"),
        NEAR_CHECK(gap_default_pill_w - rects[0].width, 8.0, "so the strip loses both gaps off its pinned width"));
}

// Either strip field alone is enough to paint the pill, and each item field keeps its own default.
static void tab_geom_check_single_fields(int* utest_result, TabGeom* g) {
    UiRenderCommand* rects = g->rects;
    TAB_GEOM_FRAME(g, .container_radius = 8.0f);
    ASSERT_EQ_MSG(g->count, 4, "container_radius alone paints the pill");
    ASSERT_NEAR_CHECKS(NEAR_CHECK(rects[1].height, FlWidgetHeight_MD,
                                  "a pill without item padding leaves the tabs at the fixed height"));
    TAB_GEOM_FRAME(g, .item_radius = 6.0f);
    ASSERT_EQ_MSG(g->count, 3, "item_radius alone paints no pill");
    ASSERT_NEAR_CHECKS(NEAR_CHECK(rects[0].corner_radius.tl, 6.0, "the tab radius applies on its own"),
                       NEAR_CHECK(rects[0].height, FlWidgetHeight_MD, "and leaves the height alone"));
}

// font_size goes on the run, and the snapshot carries it. letter_spacing has no snapshot field, so it is read
// from the fit-sized tab's width. "One" has three glyphs, so two gaps.
static void tab_geom_check_font(int* utest_result, TabGeom* g) {
    TAB_GEOM_FRAME(g, .font_size = 10);
    UiRenderCommand texts[8];
    ASSERT_EQ_MSG(render_cmd_collect(UiRenderCmd_Text, texts, 8), 3, "one label per tab");
    ASSERT_EQ_MSG((int)texts[0].font_size, 10, "the config's font_size reaches the label");
    const float unspaced_w = g->rects[0].width;
    TAB_GEOM_FRAME(g, .font_size = 10, .letter_spacing = 4);
    ASSERT_NEAR_CHECKS(
        NEAR_CHECK(g->rects[0].width - unspaced_w, 8.0, "letter_spacing widens the label by one gap per glyph pair"));
}

UTEST(ui_tab_bar, geometry_follows_config) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // At ui_scale 1.0 the snapshot's logical rects are the pixels the config asked for.
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });

    // The tabs are fit-sized around their labels, so the strip needs text metrics. This uses the built-in 8x8
    // font.
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 1));

    TabGeom geom = { .ctx = ctx, .tabs = { FL_S("One"), FL_S("Two"), FL_S("Three") } };

    TAB_GEOM_FRAME(&geom, .vertical = false);
    ASSERT_EQ_MSG(geom.count, 3, "a zeroed strip paints one rectangle per tab and no pill behind them");
    ASSERT_NEAR_CHECKS(
        NEAR_CHECK(geom.rects[0].height, FlWidgetHeight_MD, "a zeroed config keeps the fixed tab height"),
        NEAR_CHECK(geom.rects[0].corner_radius.tl, FlRadius_MD, "and the default tab radius"));

    tab_geom_check_skinned_strip(utest_result, &geom);
    RETURN_IF_TEST_FAILED();
    tab_geom_check_child_gap(utest_result, &geom);
    RETURN_IF_TEST_FAILED();
    tab_geom_check_single_fields(utest_result, &geom);
    RETURN_IF_TEST_FAILED();
    tab_geom_check_font(utest_result, &geom);
}

#undef TAB_GEOM_FRAME

// Tab colors come from the shared variant color code: Solid for the selected tab, Ghost for the rest.

typedef struct TabColorCheck {
    UiColor4 got;
    FlColor want;
    const char* message;
} TabColorCheck;

static void assert_tab_colors(int* utest_result, const TabColorCheck* checks, size_t count) {
    for (size_t i = 0; i < count; i++) {
        ASSERT_TRUE_MSG(tab_color_eq(checks[i].got, checks[i].want), checks[i].message);
    }
}

// Asserts every TabColorCheck listed and ends the calling UTEST at the first one that fails.
#define ASSERT_TAB_COLORS(...)                                                               \
    do {                                                                                     \
        const TabColorCheck tab_color_checks_[] = { __VA_ARGS__ };                           \
        assert_tab_colors(utest_result, tab_color_checks_, sizeof_array(tab_color_checks_)); \
        RETURN_IF_TEST_FAILED();                                                             \
    } while (0)

UTEST(ui_tab_bar, colors_follow_theme) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    const float dt = 1.0f / 60.0f;

    // The theme these colors resolve through.
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

    // Park the cursor off the strip. Hover lightens both fill and label, which would move every color below off
    // its exact theme value.
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
    ASSERT_TAB_COLORS(
        { rects[0].bg_color, base.interactive.base, "the selected tab fills with the theme's accent" },
        { rects[1].bg_color, base.surface.base, "an unselected tab fills from the theme, not a hardcoded transparent" },
        { texts[0].text_color, base.text.primary, "the selected label follows the theme, not a hardcoded black" },
        { texts[1].text_color, base.text.secondary, "an unselected label is secondary text" });

    // The per-widget override is a named local because a compound literal inside the ui_tab_bar() macro would
    // split on its internal commas. The preprocessor protects parentheses but not braces.
    const FlColor I1 = { 20, 60, 200, 255 };
    const FlColor T1 = { 10, 250, 90, 255 };
    const FlThemeOverride ov = {
        .slots = FlThemeSlot_InteractiveBase | FlThemeSlot_TextPrimary,
        .colors = { .interactive.base = I1, .text.primary = T1 },
    };
    TAB_COLOR_FRAME(.widget.theme_override = &ov);
    ASSERT_TAB_COLORS(
        { rects[0].bg_color, I1, "a per-widget override reaches the selected tab's fill" },
        { texts[0].text_color, T1, "and its label" },
        { rects[1].bg_color, base.surface.base, "slots the override does not flag stay on the base theme" });

    // FlWidgetConfig::color is not forwarded as this widget's background override, unlike in every other caller
    // of fl_resolve_widget_colors. One override covers both tab states, so using it would paint the selected and
    // unselected tabs the same.
    TAB_COLOR_FRAME(.widget.color = { 250, 5, 250, 255 });
    ASSERT_TAB_COLORS({ rects[0].bg_color, base.interactive.base,
                        "a widget color override leaves the selected tab on the theme's accent" },
                      { rects[1].bg_color, base.surface.base, "and the unselected tab on its surface" });

#undef TAB_COLOR_FRAME
}

// A checkbox and a switch moving toward the same target with the same dt share anim_toggle_toward, so their
// check_anim_t match every frame.
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

    // Start both off for one frame so their animation state is initialized at 0.0, then flip both on.
    bool checked = false;
    bool switched = false;

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
    checked = true;
    switched = true;
    BOTH_FRAME();
    ItemState* cb = hashmap_get(&ctx->shared->item_state, cb_clay);
    ItemState* sw = hashmap_get(&ctx->shared->item_state, sw_clay);
    ASSERT_TRUE_MSG(cb != nullptr && sw != nullptr, "checkbox and switch registered interactive items");
    const double first_frame_fill = cb->check_anim_t;
    ASSERT_GT_MSG(first_frame_fill, 0.0, "checkbox fill advances on the first frame");
    ASSERT_LT_MSG(first_frame_fill, 1.0, "checkbox fill eases (no one-frame linear snap to fully checked)");
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

// Each dismissal route returns a one-shot FlDialogResult and clears the caller-owned open bool. Frame 1 opens the
// dialog, which returns None on the opening frame, and the dismissal happens on frame 2.

typedef struct MsgBoxFixture {
    FlUiCtx* ctx;
    FlDialogResult result;
} MsgBoxFixture;

#define MB_FRAME(f, open_ptr)                                                                                     \
    do {                                                                                                          \
        ui_update_begin((f)->ctx);                                                                                \
        (f)->result = ui_message_box(FL_S("Title"), FL_S("Message"), (open_ptr), .cancel_label = FL_S("Cancel")); \
        ui_update_end((f)->ctx, 1.0f / 60.0f);                                                                    \
    } while (0)

static void msgbox_clear_mouse_edges(MsgBoxFixture* f) {
    f->ctx->mouse_pressed_this_frame = false;
    f->ctx->mouse_released_this_frame = false;
}

// Marks the named button hot, so a fast tap on the next frame registers as a click, then taps it.
static bool msgbox_tap_button(MsgBoxFixture* f, FlString name, bool* open) {
    const u32 clay = ui_layout_id_to_clay_id((FlLayoutId) { .name = name, .id = 0 });
    ItemState* button = hashmap_get(&f->ctx->shared->item_state, clay);
    if (button == nullptr) {
        return false;
    }
    button->state.hot = 1; // skips the frames hover takes to propagate, as in the switch case
    f->ctx->mouse_pressed_this_frame = true;
    f->ctx->mouse_released_this_frame = true;
    MB_FRAME(f, open);
    return true;
}

// Opens the dialog, then taps the named button with a fast press and release and checks the one-shot result.
static void msgbox_check_tap_dismisses(int* utest_result, MsgBoxFixture* f, FlString button, FlDialogResult want) {
    bool open = true;
    msgbox_clear_mouse_edges(f);
    MB_FRAME(f, &open);
    ASSERT_EQ_MSG((int)f->result, (int)FlDialogResult_None, "opening frame returns None (no decision yet)");
    ASSERT_TRUE_MSG(open, "dialog stays open on the opening frame");

    const bool tapped = msgbox_tap_button(f, button, &open);
    ASSERT_TRUE_MSG(tapped, "the button registered an interactive item");
    ASSERT_EQ_MSG((int)f->result, (int)want, "a fast tap on the button returns its dialog result");
    ASSERT_FALSE_MSG(open, "dismissing clears the caller-owned open bool");
}

// ESC cancels.
static void msgbox_check_escape_cancels(int* utest_result, MsgBoxFixture* f) {
    bool open = true;
    msgbox_clear_mouse_edges(f);
    MB_FRAME(f, &open);
    ASSERT_EQ_MSG((int)f->result, (int)FlDialogResult_None, "opening frame returns None");

    // ui_key_pressed reads the per-frame edge snapshot. ui_input_update normally captures it, and this
    // fixture never runs ui_input_update.
    f->ctx->keys_pressed_this_frame[FlKey_Escape] = true;
    MB_FRAME(f, &open);
    f->ctx->keys_pressed_this_frame[FlKey_Escape] = false;
    ASSERT_EQ_MSG((int)f->result, (int)FlDialogResult_Cancelled, "ESC returns Cancelled");
    ASSERT_FALSE_MSG(open, "ESC clears the caller-owned open bool");
}

// ESC also cancels a single-button alert (empty cancel_label).
static void msgbox_check_escape_cancels_alert(int* utest_result, MsgBoxFixture* f) {
    bool open = true;
    msgbox_clear_mouse_edges(f);
    ui_update_begin(f->ctx);
    f->result = ui_message_box(FL_S("Title"), FL_S("Message"), &open, .confirm_label = FL_S("OK"));
    ui_update_end(f->ctx, 1.0f / 60.0f);
    ASSERT_EQ_MSG((int)f->result, (int)FlDialogResult_None, "alert opening frame returns None");

    f->ctx->keys_pressed_this_frame[FlKey_Escape] = true; // the edge snapshot, as above
    ui_update_begin(f->ctx);
    f->result = ui_message_box(FL_S("Title"), FL_S("Message"), &open, .confirm_label = FL_S("OK"));
    ui_update_end(f->ctx, 1.0f / 60.0f);
    f->ctx->keys_pressed_this_frame[FlKey_Escape] = false;
    ASSERT_EQ_MSG((int)f->result, (int)FlDialogResult_Cancelled, "ESC cancels a single-button alert");
    ASSERT_FALSE_MSG(open, "ESC clears open for a single-button alert");
}

UTEST(ui_message_box, result_paths) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    ctx->delta_time = 1.0f / 60.0f;

    MsgBoxFixture fixture = { .ctx = ctx, .result = FlDialogResult_None };
    msgbox_check_tap_dismisses(utest_result, &fixture, FL_S("##msgbox_ok"), FlDialogResult_Confirmed);
    RETURN_IF_TEST_FAILED();
    msgbox_check_tap_dismisses(utest_result, &fixture, FL_S("##msgbox_cancel"), FlDialogResult_Cancelled);
    RETURN_IF_TEST_FAILED();
    msgbox_check_escape_cancels(utest_result, &fixture);
    RETURN_IF_TEST_FAILED();
    msgbox_check_escape_cancels_alert(utest_result, &fixture);
}

#undef MB_FRAME

// While the dialog is open the modal scope blocks input to widgets emitted outside it. Closing it is the control
// that shows the same click works otherwise.
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

    // The dialog is emitted before the background content, which is the documented order, so the opening frame
    // already blocks the button behind it.
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

    // Control: with the dialog closed, the click reaches the button.
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

// Exclusive single-choice selection and index clamping. The case runs the headless fl_app loop, so selection goes
// through the real hover -> hot -> click path.
typedef struct RadioPressState {
    int choice;
    FlUiCtx* ctx; // captured during the tick so the laid-out option geometry can be read afterwards
    bool last_changed;
} RadioPressState;

static void radio_press_frame(void* user_data) {
    RadioPressState* st = (RadioPressState*)user_data;
    st->ctx = g_ui_ctx;
    FlString options[3] = { FL_S("A"), FL_S("B"), FL_S("C") };
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        st->last_changed = ui_radio_group(options, (int)sizeof_array(options), &st->choice).clicked;
    }
}

// The id is built as in ui_radio_group_impl: instance id = the selected_index pointer, folded with the option
// index by ui_item_id_seed.
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

    // Hover option 2 for two ticks so the hot state propagates (next_hot_id -> hot_id -> state.hot).
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

    // Select the option that is already current.
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, &st, radio_press_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    fl_app_tick(app, &st, radio_press_frame);
    ASSERT_EQ_MSG(st.choice, 0, "re-selecting the current option leaves the index unchanged");
    ASSERT_FALSE_MSG(st.last_changed, "re-selecting the current option reports no selection change");

    fl_app_destroy(app);
}
