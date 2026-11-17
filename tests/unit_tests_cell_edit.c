// Editing in place: a borderless text input in a cell of a list with columns, driven through the headless app.
//
// The list sits at the window's top left: a header of CE_HEADER_H, then rows of CE_ROW_H with CE_GAP between them.
// The row with the id CE_EDIT_ID holds the input in the middle column (x 100 to 180) while CeState.editing is set.

#include "unit_tests_common.h"
#include <app/app.h>
#include <flowi/ui/cell.h>
#include <flowi/ui/text_input.h>

#define CE_ROWS 100u
#define CE_ROW_H 24.0f
#define CE_GAP 4.0f
#define CE_HEADER_H 20.0f
#define CE_PAD 6.0f
#define CE_EDIT_ID 2u
#define CE_INPUT_X 140
#define CE_INPUT_Y 60

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const FlColumnDef ce_cols[3] = {
    { .id = 11, .title = { .data = "A", .length = 1 }, .width = 100.0f },
    { .id = 22, .title = { .data = "B", .length = 1 }, .width = 80.0f },
    { .id = 33, .title = { .data = "C", .length = 1 }, .flex = 1.0f },
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct CeState {
    FlTextInputState* input;
    bool editing;
    bool bordered;             // the input is an ordinary bordered one
    bool refocus_when_emitted; // the app asks for focus again on the frame the row comes back
    bool was_emitted;
    bool emitted;
    FlSignal input_signal;
    FlElementState input_rect;
    FlListResult result;
    FlListRange range;
    u32 input_clicks;
    u32 row_clicks;
    u32 frames_unfocused; // frames the input was shown without focus
    u32 submits;
    u32 selection_requests;
} CeState;

static u64 ce_ids[CE_ROWS];

static const FlLayoutId ce_list = { .name = { .data = "ce_list", .length = 7 }, .id = 0 };
static const FlLayoutId ce_input = { .name = { .data = "ce_input", .length = 8 }, .id = 0 };

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ce_on_submit(CeState* st) {
    st->submits++;
    st->editing = false;
    fl_list_request_focus(ce_list, CE_EDIT_ID);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ce_input_cell(CeState* st) {
    if (st->refocus_when_emitted && !st->was_emitted) {
        fl_text_input_request_focus(st->input, false);
    }
    st->emitted = true;
    st->input_signal = ui_text_input(ce_input.name, st->input, (FlTextInputConfig) { .borderless = !st->bordered });
    st->input_clicks += st->input_signal.clicked ? 1 : 0;
    st->frames_unfocused += st->input_signal.focused ? 0 : 1;
    if (st->input_signal.submitted) {
        ce_on_submit(st);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool ce_is_edit_row(const CeState* st, u64 id) {
    return st->editing && id == CE_EDIT_ID;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ce_row(CeState* st, u64 id) {
    FlSignal row = fl_list_item_begin(id);
    if (id == CE_EDIT_ID) {
        st->row_clicks += row.clicked ? 1 : 0;
    }
    fl_cell_begin(22);
    if (ce_is_edit_row(st, id)) {
        ce_input_cell(st);
    }
    fl_cell_end();
    fl_list_item_end();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ce_frame(void* user) {
    CeState* st = user;
    st->was_emitted = st->emitted;
    st->emitted = false;
    ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(160) }, .direction = FlLayoutDirection_TopToBottom) {
        st->range = fl_list_begin(ce_list, ce_ids, CE_ROWS, nullptr,
                                  (FlListConfig) { .row_height = CE_ROW_H,
                                                   .gap = CE_GAP,
                                                   .columns = ce_cols,
                                                   .column_count = 3,
                                                   .header_height = CE_HEADER_H });
        for (u32 i = st->range.start; i < st->range.end; i++) {
            ce_row(st, ce_ids[i]);
        }
        st->result = fl_list_end();
        st->selection_requests += st->result.request_count;
    }
    st->input_rect = ui_get_element_state_by_id(ui_layout_id_to_clay_id(ce_input));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct CeApp {
    FlApp* app;
    CeState st;
} CeApp;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static CeApp ce_open(void) {
    CeApp scope = { 0 };
    if (render_fixture_get() == nullptr) {
        return scope;
    }
    FlAppSettings settings = { .window_title = FL_S("ce"), .width = 400, .height = 300, .headless = true };
    scope.app = fl_app_create(&settings);
    if (scope.app != nullptr) {
        fl_app_set_test_dt(scope.app, 1.0f / 60.0f);
        fl_app_inject_mouse_move(scope.app, 390, 290);
        scope.st.input = fl_text_input_create(64);
        scope.st.editing = true;
        for_count(i, CE_ROWS) {
            ce_ids[i] = (u64)i + 1;
        }
    }
    return scope;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ce_close(CeApp* scope) {
    if (scope->app != nullptr) {
        fl_app_inject_mouse_button(scope->app, FlMouseButton_Left, false);
        fl_text_input_destroy(scope->st.input);
        fl_app_destroy(scope->app);
    }
}

#define CE_APP(ce)                                           \
    __attribute__((cleanup(ce_close))) CeApp ce = ce_open(); \
    ASSERT_TRUE_MSG(ce.app != nullptr, "headless fl_app_create failed")

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ce_ticks(CeApp* ce, int n) {
    for (int i = 0; i < n; i++) {
        fl_app_tick(ce->app, &ce->st, ce_frame);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ce_key_tap(CeApp* ce, FlKey key) {
    fl_app_inject_key(ce->app, key, true);
    ce_ticks(ce, 1);
    fl_app_inject_key(ce->app, key, false);
    ce_ticks(ce, 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ce_focus_input(CeApp* ce, bool select_all) {
    ce_ticks(ce, 4);
    fl_text_input_request_focus(ce->st.input, select_all);
    ce_ticks(ce, 2);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void ce_click_input(CeApp* ce) {
    fl_app_inject_mouse_move(ce->app, CE_INPUT_X, CE_INPUT_Y);
    ce_ticks(ce, 4);
    fl_app_inject_mouse_button(ce->app, FlMouseButton_Left, true);
    ce_ticks(ce, 1);
    fl_app_inject_mouse_button(ce->app, FlMouseButton_Left, false);
    ce_ticks(ce, 2);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cell_edit, a_borderless_input_fills_its_cell) {
    CE_APP(ce);
    ce_ticks(&ce, 4);
    ASSERT_TRUE_MSG(ce.st.input_rect.found, "the input was laid out");
    ASSERT_NEAR(100.0 + CE_PAD, (double)ce.st.input_rect.pos.x, 0.01);
    ASSERT_NEAR(80.0 - 2.0 * CE_PAD, (double)ce.st.input_rect.size.x, 0.01);
    ASSERT_NEAR((double)CE_ROW_H, (double)ce.st.input_rect.size.y, 0.01);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cell_edit, a_long_text_does_not_widen_a_borderless_input) {
    CE_APP(ce);
    fl_text_input_set_text(ce.st.input, FL_S("a value that is far wider than the column it is edited in"));
    ce_ticks(&ce, 4);
    ASSERT_NEAR(80.0 - 2.0 * CE_PAD, (double)ce.st.input_rect.size.x, 0.01);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cell_edit, request_focus_focuses_the_input) {
    CE_APP(ce);
    fl_text_input_set_text(ce.st.input, FL_S("abc"));
    ce_focus_input(&ce, false);
    ASSERT_TRUE_MSG(ce.st.input_signal.focused, "the input has focus after the request");
    fl_app_inject_text(ce.app, FL_S("x"));
    ce_ticks(&ce, 2);
    ASSERT_TRUE_MSG(fl_string_equals(fl_text_input_get_text(ce.st.input), FL_S("abcx")), "typing appends at the caret");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cell_edit, request_focus_with_select_all_lets_typing_replace_the_text) {
    CE_APP(ce);
    fl_text_input_set_text(ce.st.input, FL_S("abc"));
    ce_focus_input(&ce, true);
    fl_app_inject_text(ce.app, FL_S("x"));
    ce_ticks(&ce, 2);
    ASSERT_TRUE_MSG(fl_string_equals(fl_text_input_get_text(ce.st.input), FL_S("x")), "typing replaces the selection");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cell_edit, up_and_down_stay_in_a_focused_input) {
    CE_APP(ce);
    ce_focus_input(&ce, false);
    ce.st.frames_unfocused = 0;
    ce_key_tap(&ce, FlKey_Down);
    ce_key_tap(&ce, FlKey_Up);
    ce_ticks(&ce, 2);
    ASSERT_EQ_MSG(0u, ce.st.frames_unfocused, "the arrow keys do not move focus off the input");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cell_edit, down_moves_focus_off_a_bordered_input) {
    CE_APP(ce);
    ce.st.bordered = true;
    ce_focus_input(&ce, false);
    ce.st.frames_unfocused = 0;
    ce_key_tap(&ce, FlKey_Down);
    ce_ticks(&ce, 2);
    ASSERT_GT_MSG(ce.st.frames_unfocused, 0u, "an input with a border leaves Down to focus navigation");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cell_edit, a_click_on_an_input_in_a_row_goes_to_the_input) {
    CE_APP(ce);
    ce_click_input(&ce);
    ASSERT_EQ_MSG(1u, ce.st.input_clicks, "the input is clicked");
    ASSERT_EQ_MSG(0u, ce.st.row_clicks, "the row under it is not");
    ASSERT_TRUE_MSG(ce.st.input_signal.focused, "the click focuses the input");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cell_edit, a_press_inside_a_focused_input_keeps_its_focus) {
    CE_APP(ce);
    ce_click_input(&ce);
    ASSERT_TRUE_MSG(ce.st.input_signal.focused, "the first click focuses the input");
    ce.st.frames_unfocused = 0;
    fl_app_inject_mouse_button(ce.app, FlMouseButton_Left, true);
    ce_ticks(&ce, 3);
    fl_app_inject_mouse_button(ce.app, FlMouseButton_Left, false);
    ce_ticks(&ce, 2);
    ASSERT_EQ_MSG(0u, ce.st.frames_unfocused, "a second press does not take focus from the input");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cell_edit, a_drag_inside_a_focused_input_selects_no_rows) {
    CE_APP(ce);
    ce_click_input(&ce);
    ce.st.selection_requests = 0;
    fl_app_inject_mouse_button(ce.app, FlMouseButton_Left, true);
    ce_ticks(&ce, 1);
    fl_app_inject_mouse_move(ce.app, CE_INPUT_X + 30, CE_INPUT_Y + 60);
    ce_ticks(&ce, 3);
    fl_app_inject_mouse_button(ce.app, FlMouseButton_Left, false);
    ce_ticks(&ce, 2);
    ASSERT_EQ_MSG(0u, ce.st.selection_requests, "the drag is not a marquee over the rows");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cell_edit, submit_hands_focus_back_to_the_list) {
    CE_APP(ce);
    ce_focus_input(&ce, false);
    ce_key_tap(&ce, FlKey_Enter);
    ASSERT_EQ_MSG(1u, ce.st.submits, "Enter submits the edit");
    ce_key_tap(&ce, FlKey_Down);
    ASSERT_EQ_MSG((u64)CE_EDIT_ID + 1, ce.st.result.focused_id, "Down moves the list cursor from the edited row");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(cell_edit, an_edit_survives_its_row_scrolling_out_and_back) {
    CE_APP(ce);
    fl_text_input_set_text(ce.st.input, FL_S("abc"));
    ce_focus_input(&ce, false);
    ce.st.refocus_when_emitted = true;

    fl_app_inject_mouse_move(ce.app, 250, 100);
    ce_ticks(&ce, 2);
    fl_app_inject_wheel(ce.app, 0.0f, -5.0f);
    ce_ticks(&ce, 30);
    ASSERT_TRUE_MSG(ce.st.range.start >= CE_EDIT_ID, "the edited row scrolled out");
    ASSERT_FALSE_MSG(ce.st.emitted, "its input is not shown");

    fl_app_inject_wheel(ce.app, 0.0f, 5.0f);
    ce_ticks(&ce, 30);
    ASSERT_TRUE_MSG(ce.st.emitted, "the edited row is back");
    ASSERT_TRUE_MSG(ce.st.input_signal.focused, "the input has focus again");
    ASSERT_TRUE_MSG(fl_string_equals(fl_text_input_get_text(ce.st.input), FL_S("abc")), "the text is kept");
}
