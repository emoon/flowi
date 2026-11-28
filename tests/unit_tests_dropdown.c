// UI dropdown tests

#include "widgets/common.h"
#include <math.h>

// The dropdown popup is a virtualized ui_list of label rows. Clicking a row commits (1-based stable id to
// *selected_index, index = id - 1) and closes. max_visible_rows bounds the viewport height to min(count, max) *
// row_stride and the List virtualizes the rest. The substring filter passes only the matching subset to the
// List.
//
// Row layout ids: list_item_begin_impl keys each row on {list-container name, emit_pos + 1}. The container name
// is "<label>_dropdown_list" (the button id is the label verbatim, see resolve_widget_id), and emit_pos is the
// absolute index into the filtered item array, so the row at filtered position p has clay id {name, p + 1}.

typedef struct DropdownState {
    bool is_open;
    int choice;   // *selected_index, written on commit
    FlUiCtx* ctx; // captured during the tick so the laid-out row and container geometry can be read afterwards
    FlString* options;
    int option_count;
    int max_visible_rows; // DropdownConfig.max_visible_rows (0 = default)
    bool searchable;
    bool show_cancel;
    FlTextInputState search; // the dropdown initializes this on the first open frame
} DropdownState;

static void dropdown_frame(void* user_data) {
    DropdownState* st = (DropdownState*)user_data;
    st->ctx = g_ui_ctx;
    FlDropdownConfig cfg = {
        .show_cancel = st->show_cancel,
        .min_width = 200.0f,
        .max_visible_rows = st->max_visible_rows,
        .searchable = st->searchable,
        .search_state = st->searchable ? &st->search : nullptr,
    };
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        ui_dropdown_impl(FL_S("Pick"), &st->is_open, st->options, st->option_count, &st->choice, cfg);
    }
}

static void dropdown_ticks(FlApp* app, DropdownState* st, int count) {
    for (int f = 0; f < count; f++) {
        fl_app_tick(app, st, dropdown_frame);
    }
}

// The list container is emitted under {name "Pick_dropdown_list", id 0}. Its laid-out height is the bounded
// viewport, min(count, max_visible_rows) * (row_height + gap). Returns -1 if it is not laid out yet.
static float dropdown_list_height(DropdownState* st) {
    u32 cid = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Pick_dropdown_list"), .id = 0 });
    ItemState* it = hashmap_get(&st->ctx->shared->item_state, cid);
    return (it != nullptr && it->state.size.y > 0.0f) ? it->state.size.y : -1.0f;
}

// Center of the option row at filtered position pos, whose clay id is {name, pos + 1}. False if it is not laid
// out, for example because it is virtualized away, which the clamp case relies on.
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

// True if the frame drew a rectangle in the theme's surface.selected color, which is the List's selection fill on
// the row seeded from *selected_index. Unselected rows use a transparent fill that emits no rectangle.
static bool channels_within_one(UiColor4 got, FlColor want) {
    return fabsf(got.r - want.r) < 1.0f && fabsf(got.g - want.g) < 1.0f && fabsf(got.b - want.b) < 1.0f;
}

static bool is_selection_fill(const UiRenderCommand* cmd, FlColor want) {
    return cmd->type == UiRenderCmd_Rectangle && channels_within_one(cmd->bg_color, want);
}

static bool dropdown_selection_highlight_drawn(void) {
    FlColor want = fl_theme_colors()->surface.selected;
    if (want.a <= 0.0f) {
        return false;
    }
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (ui_get_render_command(i, &cmd) && is_selection_fill(&cmd, want)) {
            return true;
        }
    }
    return false;
}

// FlDropdownConfig's padding_h, padding_v and corner_radius set the closed trigger's box, and a zeroed config
// falls back to the default metrics. The case checks the debug render-command snapshot, so the numbers in the
// assertions are the geometry. The dropdown stays closed throughout, and the popup keeps its own metrics.
UTEST(dropdown, trigger_geometry_follows_config) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // At ui_scale 1.0 the snapshot's logical rects are the pixels the config asked for.
    ctx->layout_width = 1920.0f;
    ctx->layout_height = 1080.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 1920.0f, .height = 1080.0f });
    const float dt = 1.0f / 60.0f;

    // A trigger sized by padding fits around its label, so it needs text metrics. This uses the built-in 8x8
    // font.
    ui_font_set(font_cache_load_default_font(ctx->shared->font_cache, 1));

    FlString opts[2] = { FL_S("Sine"), FL_S("Saw") };
    bool is_open = false;
    int selected = 0;
    UiRenderCommand rects[4];
    UiRenderCommand texts[4];

    // Each case takes two frames: the first registers the trigger's ItemState for the hover and focus animation,
    // and only the second is measured. Each case passes its own label because a label is also the widget's id,
    // and the fixture's ItemState map outlives a case.
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

    // Zeroed: the fixed FlWidgetHeight_MD box with FlRadius_MD corners and FlSpacing_MD/SM padding.
    DROPDOWN_GEOM_FRAME("dropdown_geom_default", .show_cancel = true);
    ASSERT_NEAR_MSG((double)rects[0].height, (double)FlWidgetHeight_MD, 1e-4,
                    "a zeroed config keeps the fixed trigger height");
    ASSERT_NEAR_MSG((double)rects[0].corner_radius.tl, (double)FlRadius_MD, 1e-4, "and the default trigger radius");
    ASSERT_NEAR_MSG((double)(texts[0].x - rects[0].x), (double)FlSpacing_MD, 1e-4,
                    "and the default horizontal padding, read as the label's inset");

    // The compact pill: 12x7 padding at radius 8. The radius equals FlRadius_MD, so only the height and the inset
    // tell this from the default case. The radius is checked on its own below.
    DROPDOWN_GEOM_FRAME("dropdown_geom_pill", .padding_h = 12.0f, .padding_v = 7.0f, .corner_radius = 8.0f);
    const float pill_h = rects[0].height;
    ASSERT_LT_MSG((double)pill_h, (double)FlWidgetHeight_MD, "padding_v takes the trigger off the fixed height");
    ASSERT_NEAR_MSG((double)(texts[0].x - rects[0].x), 12.0, 1e-4, "padding_h insets the label");

    // With padding set, the size comes from the padding: 10px more per side is 20px more trigger. The check is a
    // delta against the case above, so no font metric appears in the expected number.
    DROPDOWN_GEOM_FRAME("dropdown_geom_taller", .padding_h = 12.0f, .padding_v = 17.0f, .corner_radius = 8.0f);
    ASSERT_NEAR_MSG((double)(rects[0].height - pill_h), 20.0, 1e-4, "padding_v drives the trigger height");

    // Each field keeps its own default. padding_h does not turn on padding-based sizing, since the trigger grows
    // to its row horizontally either way, so it insets the label and leaves the fixed height alone.
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

// Clicking an option row commits its index to *selected_index (index = id - 1) and closes the popup.
UTEST(dropdown, click_commits_and_closes) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[4] = { FL_S("Red"), FL_S("Green"), FL_S("Blue"), FL_S("Alpha") };
    DropdownState st = { .is_open = true, .choice = 2, .options = opts, .option_count = 4 };

    // The list needs one frame to establish the container height for virtualization and another to emit the
    // visible rows so their ItemStates exist. The popup stays open, since nothing is clicked yet.
    dropdown_ticks(app, &st, 4);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    ASSERT_TRUE_MSG(st.is_open, "popup stays open with no click");
    ASSERT_EQ_MSG(st.choice, 2, "selection unchanged before any click");

    // The current selection (choice 2) is highlighted: the transient set seeded from *selected_index makes the
    // List draw its surface.selected fill on that row before any click.
    const bool highlight_drawn = dropdown_selection_highlight_drawn();
    ASSERT_TRUE_MSG(highlight_drawn, "the current selection's row is highlighted while open");

    // Click option 0 ("Red"): hover so hot propagates, then press and release.
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

// max_visible_rows bounds the popup viewport. A long list is cut to max_visible_rows and the rest is virtualized.
// A list with fewer rows than max_visible_rows is sized to its content.
UTEST(dropdown, max_visible_rows_clamps_and_virtualizes) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    // The file-local UI_DROPDOWN_ROW_HEIGHT (28) + UI_DROPDOWN_ROW_GAP (2) in ui_dropdown.c.
    const float row_stride = 28.0f + 2.0f;

    // A long list (100 options) cut to 5 visible rows.
    FlString many[100];
    static char many_labels[100][8];
    for (int i = 0; i < 100; i++) {
        snprintf(many_labels[i], sizeof(many_labels[i]), "opt%d", i);
        many[i] = string_from_cstr_len(many_labels[i], strlen(many_labels[i]));
    }
    DropdownState st = { .is_open = true, .choice = 0, .options = many, .option_count = 100, .max_visible_rows = 5 };
    dropdown_ticks(app, &st, 4);
    ASSERT_NEAR(dropdown_list_height(&st), 5.0f * row_stride, 1.5f);

    // The top rows are laid out, and a row well past the viewport is not emitted at all.
    i32 cx = 0, cy = 0;
    ASSERT_TRUE_MSG(dropdown_row_center(&st, 0, &cx, &cy), "top row is laid out");
    ASSERT_FALSE_MSG(dropdown_row_center(&st, 60, &cx, &cy), "a row far past the viewport is virtualized away");

    // A short list (3 options) with the same cap is sized to its content.
    FlString few[3] = { FL_S("A"), FL_S("B"), FL_S("C") };
    DropdownState st2 = { .is_open = true, .choice = 0, .options = few, .option_count = 3, .max_visible_rows = 5 };
    dropdown_ticks(app, &st2, 4);
    ASSERT_NEAR(dropdown_list_height(&st2), 3.0f * row_stride, 1.5f);

    // max_visible_rows left at 0 means the default cap (UI_DROPDOWN_DEFAULT_MAX_ROWS = 10): a 20-option list is
    // cut to 10 rows, which shows the default is a cap and not "no cap".
    DropdownState st3 = { .is_open = true, .choice = 0, .options = many, .option_count = 20, .max_visible_rows = 0 };
    dropdown_ticks(app, &st3, 4);
    ASSERT_NEAR(dropdown_list_height(&st3), 10.0f * row_stride, 1.5f);

    fl_app_destroy(app);
}

// When the substring filter matches nothing, the popup shows an empty list. The selection is unchanged and the
// popup stays open.
UTEST(dropdown, empty_when_all_filtered_out) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[3] = { FL_S("Red"), FL_S("Green"), FL_S("Blue") };
    DropdownState st = { .is_open = true, .choice = 1, .options = opts, .option_count = 3, .searchable = true };

    // The first frame lets the dropdown initialize the search buffer. Then set a query that matches no option.
    fl_app_tick(app, &st, dropdown_frame);
    ASSERT_TRUE_MSG(st.search.buffer != nullptr, "searchable dropdown initialized its search buffer");
    memcpy(st.search.buffer, "zzz", 3);
    st.search.buffer_length = 3;

    dropdown_ticks(app, &st, 3);

    i32 cx = 0, cy = 0;
    ASSERT_FALSE_MSG(dropdown_row_center(&st, 0, &cx, &cy), "all options filtered out: no option row is laid out");
    ASSERT_EQ_MSG(st.choice, 1, "an empty filtered popup leaves the selection unchanged");
    ASSERT_TRUE_MSG(st.is_open, "an empty filtered popup stays open (nothing to commit)");
    const int command_count = ui_get_render_command_count();
    ASSERT_TRUE_MSG(command_count > 0, "the popup panel still renders (no crash on an empty list)");

    fl_app_destroy(app);
}

// Clicking a row of a filtered list commits the original option index and not the filtered position. The stable
// id orig_index + 1 is put in the descriptor during the filter, so index = id - 1 accounts for the rows filtered
// out. The options are "zero".."four", and the substring "o" keeps {zero(0), one(1), two(2), four(4)}.
UTEST(dropdown, searchable_filter_click_commits_original_index) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[5] = { FL_S("zero"), FL_S("one"), FL_S("two"), FL_S("three"), FL_S("four") };
    DropdownState st = { .is_open = true, .choice = 0, .options = opts, .option_count = 5, .searchable = true };

    // Let the dropdown initialize the search buffer, then set the query "o".
    fl_app_tick(app, &st, dropdown_frame);
    ASSERT_TRUE_MSG(st.search.buffer != nullptr, "searchable dropdown initialized its search buffer");
    memcpy(st.search.buffer, "o", 1);
    st.search.buffer_length = 1;
    dropdown_ticks(app, &st, 4);

    // The filter passes only the matching subset to the List. 4 rows match, so there is no 5th filtered position.
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

// Center of the trigger button from its laid-out ItemState. Its id is the label "Pick" verbatim, with no
// override, so resolve_widget_id gives {name, 0}. False if it is not laid out yet.
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

// Taps a key through the real app loop (press tick, then release tick). The pointer stays parked off the rows, so
// the keyboard and not hover owns the list's focus cursor.
static void dropdown_key_tap(FlApp* app, DropdownState* st, FlKey key) {
    fl_app_inject_key(app, key, true);
    fl_app_tick(app, st, dropdown_frame);
    fl_app_inject_key(app, key, false);
    fl_app_tick(app, st, dropdown_frame);
}

// Clicks the trigger to open the popup, then parks the pointer off every row so the keyboard owns the cursor and
// settles the popup list.
static void dropdown_open_and_settle(int* utest_result, FlApp* app, DropdownState* st) {
    ASSERT_TRUE_MSG(dropdown_click_trigger(app, st), "trigger button was not laid out");
    ASSERT_TRUE_MSG(st->is_open, "clicking the trigger opens the popup");
    fl_app_inject_mouse_move(app, 5, 5);
    dropdown_ticks(app, st, 3);
}

// A non-searchable dropdown opens with the list focused. The focus ring is on the current-value row at once, arrow
// keys navigate without a first click, and Enter and Space commit the focused option, which maps the List
// activation back to *selected_index and closes, as a click does.
UTEST(dropdown, keyboard_opens_focused_and_enter_space_commit) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[5] = { FL_S("Red"), FL_S("Green"), FL_S("Blue"), FL_S("Alpha"), FL_S("Cyan") };
    DropdownState st = { .is_open = false, .choice = 1, .options = opts, .option_count = 5 };

    // Warm up with the popup closed so the trigger button is laid out. It stays closed until clicked.
    dropdown_ticks(app, &st, 3);
    ASSERT_TRUE_MSG(st.ctx != nullptr, "frame did not run / no UI context");
    ASSERT_FALSE_MSG(st.is_open, "popup stays closed until the trigger is clicked");

    // Click the trigger. Going from closed to open requests list focus (non-searchable), seeded on the current
    // value.
    dropdown_open_and_settle(utest_result, app, &st);
    RETURN_IF_TEST_FAILED();

    // The list is focused and its ring is drawn on the row of the current value before any click.
    ASSERT_TRUE_MSG(focus_ring_drawn(), "the freshly-opened non-searchable dropdown draws the list focus ring");
    ASSERT_EQ_MSG(st.choice, 1, "opening the popup does not change the selection");

    // Arrow Down moves the cursor to option 2 (id 3). Enter commits index 2 and closes, with no row click.
    dropdown_key_tap(app, &st, FlKey_Down);
    dropdown_key_tap(app, &st, FlKey_Enter);
    ASSERT_EQ_MSG(st.choice, 2, "Down then Enter commits the roved option (index 2)");
    ASSERT_FALSE_MSG(st.is_open, "Enter-commit closes the popup");

    // Reopen, now on choice 2, move Down to option 3 (id 4), and commit with Space this time.
    dropdown_open_and_settle(utest_result, app, &st);
    RETURN_IF_TEST_FAILED();
    dropdown_key_tap(app, &st, FlKey_Down);
    dropdown_key_tap(app, &st, FlKey_Space);
    ASSERT_EQ_MSG(st.choice, 3, "Down then Space commits the roved option (index 3)");
    ASSERT_FALSE_MSG(st.is_open, "Space-commit closes the popup");

    fl_app_destroy(app);
}

// Reopening a non-searchable dropdown puts the focus cursor on the current value, even after an earlier open left
// the cursor on a different row and closed without committing. The List's has_focus is not cleared while the
// popup is closed, so the request also checks who owns the global focus ring.
UTEST(dropdown, reopen_reseeds_cursor_to_current_value) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[5] = { FL_S("Red"), FL_S("Green"), FL_S("Blue"), FL_S("Alpha"), FL_S("Cyan") };
    DropdownState st = { .is_open = false, .choice = 1, .options = opts, .option_count = 5 };

    dropdown_ticks(app, &st, 3);

    // Open (current value = choice 1, seeded on id 2), then move Down to option 2 (id 3) without committing.
    dropdown_open_and_settle(utest_result, app, &st);
    RETURN_IF_TEST_FAILED();
    dropdown_key_tap(app, &st, FlKey_Down);

    // Close without committing (is_open is caller-owned), so the selection stays choice 1. While closed the List
    // is not emitted, so its has_focus stays true and the moved cursor (id 3) stays in the persisted state.
    st.is_open = false;
    dropdown_ticks(app, &st, 3);
    ASSERT_EQ_MSG(st.choice, 1, "closing without committing leaves the selection unchanged");

    // Reopen and commit with Enter at once, with no arrow. The cursor is back on the current value (choice 1, id
    // 2), so Enter commits index 1. A stale cursor (id 3) would commit index 2.
    dropdown_open_and_settle(utest_result, app, &st);
    RETURN_IF_TEST_FAILED();
    dropdown_key_tap(app, &st, FlKey_Enter);
    ASSERT_EQ_MSG(st.choice, 1, "reopen reseeds the cursor onto the current value: Enter commits index 1, not 2");
    ASSERT_FALSE_MSG(st.is_open, "Enter-commit closes the popup");

    fl_app_destroy(app);
}

// Clay id of the searchable dropdown's visible search input ({name "Pick_dropdown_search", id 0}) and of its
// containing list. Whichever holds the global focus ring gets the keyboard.
static u32 dropdown_search_clay_id(void) {
    return ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Pick_dropdown_search"), .id = 0 });
}

static u32 dropdown_list_clay_id(void) {
    return ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Pick_dropdown_list"), .id = 0 });
}

// A searchable dropdown renders a visible text input at the top of the popup, and keyboard focus starts there on
// open: the input is the first focusable widget in the popup scope, so the popup's initial focus goes to it. The
// list holds no focus and draws no ring until focus is handed to it.
UTEST(dropdown, searchable_renders_input_and_focuses_it_on_open) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[5] = { FL_S("Red"), FL_S("Green"), FL_S("Blue"), FL_S("Alpha"), FL_S("Cyan") };
    DropdownState st = { .is_open = false, .choice = 1, .options = opts, .option_count = 5, .searchable = true };

    dropdown_ticks(app, &st, 3);

    dropdown_open_and_settle(utest_result, app, &st);
    RETURN_IF_TEST_FAILED();

    ItemState* search_item = hashmap_get(&st.ctx->shared->item_state, dropdown_search_clay_id());
    ASSERT_TRUE_MSG(search_item != nullptr && search_item->state.size.y > 0.0f,
                    "the searchable dropdown renders a visible search input at the top of the popup");

    ASSERT_EQ_MSG((u64)dropdown_search_clay_id(), (u64)st.ctx->focused_id,
                  "a freshly-opened searchable dropdown puts keyboard focus in the search input");
    ASSERT_FALSE_MSG(focus_ring_drawn(), "the list holds no focus on open, so it draws no focus ring");

    fl_app_destroy(app);
}

// Typing into the visible search input drives the filter: the input's buffer text selects the matching options.
// The options are "zero".."four", and typing "o" keeps {zero, one, two, four}.
UTEST(dropdown, searchable_typing_visible_input_filters_list) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[5] = { FL_S("zero"), FL_S("one"), FL_S("two"), FL_S("three"), FL_S("four") };
    DropdownState st = { .is_open = false, .choice = 0, .options = opts, .option_count = 5, .searchable = true };

    dropdown_ticks(app, &st, 3);

    // Open and settle so the search input is focused. It consumes typed text only while focused.
    dropdown_open_and_settle(utest_result, app, &st);
    RETURN_IF_TEST_FAILED();
    ASSERT_EQ_MSG((u64)dropdown_search_clay_id(), (u64)st.ctx->focused_id,
                  "focus is in the search input before typing");

    // Type "o" as a keystroke. It is queued in the headless backend and the next tick drains it into the input.
    fl_app_inject_text(app, FL_S("o"));
    dropdown_ticks(app, &st, 4);

    FlString query = ui_text_input_get_text(&st.search);
    ASSERT_TRUE_MSG(query.length == 1 && query.data[0] == 'o', "the typed character reached the visible search input");

    i32 cx = 0, cy = 0;
    ASSERT_TRUE_MSG(dropdown_row_center(&st, 3, &cx, &cy), "typing 'o' keeps 4 options: the 4th survivor is laid out");
    ASSERT_FALSE_MSG(dropdown_row_center(&st, 4, &cx, &cy), "only 4 options survive the 'o' filter — no 5th row");

    fl_app_destroy(app);
}

// Down from the search input moves focus into the list, and its ring appears on the first row. Up on the list's
// first row (nav_up_edge) returns focus to the input, and the ring disappears. Both go through the real app
// loop.
UTEST(dropdown, searchable_focus_handoff_input_list_input) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[5] = { FL_S("Red"), FL_S("Green"), FL_S("Blue"), FL_S("Alpha"), FL_S("Cyan") };
    DropdownState st = { .is_open = false, .choice = 0, .options = opts, .option_count = 5, .searchable = true };

    dropdown_ticks(app, &st, 3);

    dropdown_open_and_settle(utest_result, app, &st);
    RETURN_IF_TEST_FAILED();
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
    const bool ring_after_return = focus_ring_drawn();
    ASSERT_FALSE_MSG(ring_after_return, "the list drops its focus ring once focus returns to the input");
    ASSERT_TRUE_MSG(st.is_open, "focus routing does not close the popup or change the selection");
    ASSERT_EQ_MSG(st.choice, 0, "focus routing alone commits nothing");

    fl_app_destroy(app);
}

// After Down hands focus to the list, arrow keys and type-ahead navigate the filtered subset and Enter commits.
// This is the List keyboard path the non-searchable dropdown uses, reached through the search input.
UTEST(dropdown, searchable_into_list_arrow_typeahead_commit) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[5] = { FL_S("Red"), FL_S("Green"), FL_S("Blue"), FL_S("Alpha"), FL_S("Cyan") };
    DropdownState st = { .is_open = false, .choice = 0, .options = opts, .option_count = 5, .searchable = true };

    dropdown_ticks(app, &st, 3);

    // Hand focus to the list, with the cursor on row 0 ("Red").
    dropdown_open_and_settle(utest_result, app, &st);
    RETURN_IF_TEST_FAILED();
    dropdown_key_tap(app, &st, FlKey_Down); // input -> list, cursor on row 0
    ASSERT_EQ_MSG((u64)dropdown_list_clay_id(), (u64)st.ctx->focused_id, "Down handed focus to the list");

    dropdown_key_tap(app, &st, FlKey_Down);
    dropdown_key_tap(app, &st, FlKey_Enter);
    ASSERT_EQ_MSG(st.choice, 1, "arrow-nav within the filtered list then Enter commits the roved option (index 1)");
    ASSERT_FALSE_MSG(st.is_open, "Enter-commit closes the popup");

    // Reopen, now on choice 1, hand focus to the list, then type 'c', which jumps to "Cyan" (index 4). Enter
    // commits.
    dropdown_open_and_settle(utest_result, app, &st);
    RETURN_IF_TEST_FAILED();
    dropdown_key_tap(app, &st, FlKey_Down); // input -> list
    fl_app_inject_text(app, FL_S("c"));     // type-ahead within the list, since the input is no longer focused
    dropdown_ticks(app, &st, 2);
    dropdown_key_tap(app, &st, FlKey_Enter);
    ASSERT_EQ_MSG(st.choice, 4, "type-ahead in the handed-off list jumps to \"Cyan\" and Enter commits index 4");
    ASSERT_FALSE_MSG(st.is_open, "Enter-commit closes the popup");

    fl_app_destroy(app);
}

// The Down hand-off puts the list cursor on a row of the filtered subset. When a query removes leading options
// the cursor lands on the first remaining one, and arrow navigation within the subset commits the right original
// index. The options are "zero".."four", and typing "t" keeps {two(2), three(3)}. So the first remaining row is
// "two" (original index 2) and the row below it is "three" (original index 3).
UTEST(dropdown, searchable_filtered_handoff_commits_original_index) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[5] = { FL_S("zero"), FL_S("one"), FL_S("two"), FL_S("three"), FL_S("four") };
    DropdownState st = { .is_open = false, .choice = 0, .options = opts, .option_count = 5, .searchable = true };

    dropdown_ticks(app, &st, 3);

    // Open, settle, type "t". Only "two" and "three" remain, a subset that drops the leading options.
    dropdown_open_and_settle(utest_result, app, &st);
    RETURN_IF_TEST_FAILED();
    fl_app_inject_text(app, FL_S("t"));
    dropdown_ticks(app, &st, 4);
    i32 cx = 0, cy = 0;
    ASSERT_TRUE_MSG(dropdown_row_center(&st, 1, &cx, &cy),
                    "the 't' filter keeps 2 options: the 2nd survivor is laid out");
    ASSERT_FALSE_MSG(dropdown_row_center(&st, 2, &cx, &cy), "only 2 options survive the 't' filter — no 3rd row");

    // Down puts the list cursor on the first remaining row ("two", original index 2), and Enter with no arrow
    // commits index 2. A cursor on original option 0 ("zero", filtered out) would commit 0.
    dropdown_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((u64)dropdown_list_clay_id(), (u64)st.ctx->focused_id, "Down hands focus to the filtered list");
    dropdown_key_tap(app, &st, FlKey_Enter);
    ASSERT_EQ_MSG(st.choice, 2,
                  "the hand-off seeds the first surviving filtered row (\"two\"), so Enter commits index 2");
    ASSERT_FALSE_MSG(st.is_open, "Enter-commit closes the popup");

    // Reopen (choice is 2 and the search is cleared), filter to {two, three} again, hand off, arrow Down to the
    // second remaining row ("three", original index 3), Enter.
    dropdown_open_and_settle(utest_result, app, &st);
    RETURN_IF_TEST_FAILED();
    fl_app_inject_text(app, FL_S("t"));
    dropdown_ticks(app, &st, 4);
    dropdown_key_tap(app, &st, FlKey_Down); // input -> list, cursor on "two"
    dropdown_key_tap(app, &st, FlKey_Down); // "two" -> "three" within the filtered subset
    dropdown_key_tap(app, &st, FlKey_Enter);
    ASSERT_EQ_MSG(st.choice, 3, "arrow-nav within the filtered subset then Enter commits original index 3 (\"three\")");
    ASSERT_FALSE_MSG(st.is_open, "Enter-commit closes the popup");

    fl_app_destroy(app);
}

// Pressing Down in the search input when the filter matched nothing does nothing. There is no list to enter, and
// an empty list can never raise nav_up_edge to hand focus back, so focus stays in the input. This is the
// `filtered_count > 0` condition on the hand-off.
UTEST(dropdown, searchable_down_with_empty_filter_keeps_focus_in_input) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[3] = { FL_S("zero"), FL_S("one"), FL_S("two") };
    DropdownState st = { .is_open = false, .choice = 0, .options = opts, .option_count = 3, .searchable = true };

    dropdown_ticks(app, &st, 3);

    // Type a query that matches nothing.
    dropdown_open_and_settle(utest_result, app, &st);
    RETURN_IF_TEST_FAILED();
    fl_app_inject_text(app, FL_S("zzz"));
    dropdown_ticks(app, &st, 4);
    i32 cx = 0, cy = 0;
    ASSERT_FALSE_MSG(dropdown_row_center(&st, 0, &cx, &cy), "the 'zzz' filter removes every option — no rows laid out");
    ASSERT_EQ_MSG((u64)dropdown_search_clay_id(), (u64)st.ctx->focused_id,
                  "focus is in the input before the Down press");

    dropdown_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((u64)dropdown_search_clay_id(), (u64)st.ctx->focused_id,
                  "Down over an empty filtered list keeps focus in the search input (no focus trap)");
    ASSERT_TRUE_MSG(st.is_open, "the popup stays open");
    ASSERT_EQ_MSG(st.choice, 0, "nothing is committed");
    const int command_count = ui_get_render_command_count();
    ASSERT_TRUE_MSG(command_count > 0, "the popup still renders (no crash on the empty-list hand-off)");

    fl_app_destroy(app);
}

// Activating a focused non-searchable trigger with Enter or Space opens the popup and leaves it open instead of
// immediately committing on the opening frame.
UTEST(dropdown, keyboard_trigger_activation_keeps_popup_open) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[4] = { FL_S("Red"), FL_S("Green"), FL_S("Blue"), FL_S("Alpha") };
    DropdownState st = { .is_open = false, .choice = 1, .options = opts, .option_count = 4 };
    dropdown_ticks(app, &st, 3);

    // Focus the trigger with Tab, settle one frame, then open it with Enter.
    dropdown_key_tap(app, &st, FlKey_Tab);
    dropdown_ticks(app, &st, 1);
    dropdown_key_tap(app, &st, FlKey_Enter);
    ASSERT_TRUE_MSG(st.is_open, "pressing Enter on the focused trigger opens the popup and leaves it open");
    ASSERT_EQ_MSG(st.choice, 1, "opening with Enter leaves the current selection unchanged");

    // Arrow Down moves to option 2 and a subsequent Enter commits and closes.
    dropdown_key_tap(app, &st, FlKey_Down);
    dropdown_key_tap(app, &st, FlKey_Enter);
    ASSERT_EQ_MSG(st.choice, 2, "Down then Enter after keyboard-open commits index 2");
    ASSERT_FALSE_MSG(st.is_open, "committing closes the popup");

    // Closing the popup restores focus to the trigger; settle two frames so focus stabilizes, then open with Space.
    dropdown_ticks(app, &st, 2);
    dropdown_key_tap(app, &st, FlKey_Space);
    ASSERT_TRUE_MSG(st.is_open, "pressing Space on the focused trigger opens the popup and leaves it open");
    ASSERT_EQ_MSG(st.choice, 2, "opening with Space leaves the current selection unchanged");

    fl_app_destroy(app);
}

static u32 dropdown_cancel_clay_id(void) {
    return ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("Pick_cancel"), .id = 0 });
}

// Down past the last option hands focus from the list to the Cancel button, Up returns to the last option, and
// confirming Cancel closes the popup without changing *selected_index.
UTEST(dropdown, keyboard_nav_reaches_and_activates_cancel_button) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlAppSettings settings = { .window_title = FL_S("dropdown"), .width = 400, .height = 400, .headless = true };
    FlApp* app = fl_app_create(&settings);
    ASSERT_TRUE_MSG(app != nullptr, "fl_app_create(headless) failed");

    FlString opts[3] = { FL_S("Red"), FL_S("Green"), FL_S("Blue") };
    DropdownState st = { .is_open = false, .choice = 1, .options = opts, .option_count = 3, .show_cancel = true };

    dropdown_ticks(app, &st, 3);
    dropdown_open_and_settle(utest_result, app, &st);
    RETURN_IF_TEST_FAILED();

    // Start on option 1 ("Green"): Down -> option 2 ("Blue"), Down -> Cancel button.
    dropdown_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((u64)dropdown_list_clay_id(), (u64)st.ctx->focused_id, "first Down stays on last option in list");

    dropdown_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((u64)dropdown_cancel_clay_id(), (u64)st.ctx->focused_id,
                  "Down past the last option hands focus to the Cancel button");

    // Up from Cancel hands focus back to the list on the last option.
    dropdown_key_tap(app, &st, FlKey_Up);
    ASSERT_EQ_MSG((u64)dropdown_list_clay_id(), (u64)st.ctx->focused_id,
                  "Up from Cancel returns focus to the option list");

    // Down back to Cancel and activate with Enter: popup closes and original selection is preserved.
    dropdown_key_tap(app, &st, FlKey_Down);
    ASSERT_EQ_MSG((u64)dropdown_cancel_clay_id(), (u64)st.ctx->focused_id,
                  "Down from the last option reaches Cancel again");
    dropdown_key_tap(app, &st, FlKey_Enter);
    ASSERT_FALSE_MSG(st.is_open, "activating Cancel closes the popup");
    ASSERT_EQ_MSG(st.choice, 1, "activating Cancel preserves the previous selection");

    fl_app_destroy(app);
}
