// Lists with columns: the header, the body cells, Signal.cell, header clicks and resizing.
//
// Layout ids: the header cell of column i sits at {list, -3 - 2i} and its resize handle at {list, -4 - 2i}. Each
// body cell holds a probe element {"colp", row * 10 + column index + 1} that fills the cell, so a probe sits
// LC_PAD inside its cell on each side. Hover is stamped on the element's ItemState in place of moving a real cursor.

#include "unit_tests_common.h"
#include <flowi/ui/cell.h>

#define LC_ROWS 1000000
#define LC_PAD 6.0f
#define LC_HEADER_H 20.0f

static const FlColumnDef lc_cols[3] = {
    { .id = 11, .title = { .data = "A", .length = 1 }, .width = 100.0f },
    { .id = 22, .title = { .data = "B", .length = 1 }, .width = 80.0f },
    { .id = 33, .title = { .data = "C", .length = 1 }, .flex = 1.0f, .min_width = 40.0f },
};

typedef struct LcFrame {
    FlListResult result;
    FlListRange range;
    FlSignal row0;
} LcFrame;

typedef struct LcOpts {
    bool wide_first_cell;    // row 0's first cell holds a child far wider than the column
    bool shrink_first_cell;  // row 0's first cell holds a child that is wider than the column and can shrink
    bool full_width_row0;    // row 0 draws no cells
    bool skip_middle_row0;   // row 0 draws no cell for the middle column
    bool scrollbar;          // the scrollbar is always shown
    const FlColumnDef* cols; // null uses lc_cols
} LcOpts;

static FlLayoutId lc_id(FlString name, i32 id) {
    return (FlLayoutId) { .name = name, .id = id };
}

static FlElementState lc_state(FlString name, i32 id) {
    return ui_get_element_state_by_id(ui_layout_id_to_clay_id(lc_id(name, id)));
}

static void lc_wide_child(void) {
    ui_layout_area(.sizing = { sizing_fixed(500), sizing_fixed(10) }, .clip = { .horizontal = true }) {}
}

static void lc_shrink_child(void) {
    const FlLayoutId id = lc_id(FL_S("lcshrink"), 1);
    fl_layout_start(id);
    fl_layout_configure(
        (FlLayoutConfig) { .sizing = { sizing_fit(), sizing_fixed(10) }, .clip = { .horizontal = true } });
    fl_item_behavior(id, FlItemFlags_clickable);
    lc_wide_child();
    fl_layout_end();
}

static bool lc_is_wide_cell(u32 row, u32 col, LcOpts o) {
    return o.wide_first_cell && row == 0 && col == 0;
}

static void lc_probe(FlLayoutId pid) {
    fl_layout_start(pid);
    fl_layout_configure(
        (FlLayoutConfig) { .sizing = { sizing_grow(), sizing_fixed(10) }, .clip = { .horizontal = true } });
    fl_item_behavior(pid, FlItemFlags_clickable);
    fl_layout_end();
}

static bool lc_is_full_width_row(u32 row, LcOpts o) {
    return o.full_width_row0 && row == 0;
}

static bool lc_is_skipped_cell(u32 row, u32 col, LcOpts o) {
    return o.skip_middle_row0 && row == 0 && col == 1;
}

static bool lc_is_first_cell(u32 row, u32 col) {
    return row == 0 && col == 0;
}

static void lc_cell(u32 i, u32 c, LcOpts o) {
    fl_cell_begin(lc_cols[c].id);
    if (o.shrink_first_cell && lc_is_first_cell(i, c)) {
        lc_shrink_child();
        fl_cell_end();
        return;
    }
    lc_probe(lc_id(FL_S("colp"), (i32)(i * 10 + c + 1)));
    if (lc_is_wide_cell(i, c, o)) {
        lc_wide_child();
    }
    fl_cell_end();
}

static void lc_row(u32 i, LcOpts o) {
    if (lc_is_full_width_row(i, o)) {
        lc_probe(lc_id(FL_S("colp"), -1));
        return;
    }
    for (u32 c = 0; c < 3; c++) {
        if (!lc_is_skipped_cell(i, c, o)) {
            lc_cell(i, c, o);
        }
    }
}

static LcFrame lc_frame(FlUiCtx* ctx, FlLayoutId list_id, u32 total, LcOpts o) {
    LcFrame f = { 0 };
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(120) }, .direction = FlLayoutDirection_TopToBottom) {
        f.range = fl_list_begin(list_id, nullptr, total, nullptr,
                                (FlListConfig) { .row_height = 24.0f,
                                                 .gap = 4.0f,
                                                 .columns = o.cols ? o.cols : lc_cols,
                                                 .column_count = 3,
                                                 .header_height = LC_HEADER_H,
                                                 .scrollbar_mode = o.scrollbar ? FlScrollbarDisplayMode_Always
                                                                               : FlScrollbarDisplayMode_Never });
        for (u32 i = f.range.start; i < f.range.end; i++) {
            FlSignal s = fl_list_item_begin((u64)i + 1);
            if (i == 0) {
                f.row0 = s;
            }
            lc_row(i, o);
            fl_list_item_end();
        }
        f.result = fl_list_end();
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    return f;
}

// Frame 1 measures the container, frame 2 resolves the widths from it, frame 3 lays the rows out at those widths.
static LcFrame lc_settle(FlUiCtx* ctx, FlLayoutId list_id, u32 total, LcOpts o) {
    ctx->layout_width = 320.0f;
    ctx->layout_height = 256.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 320.0f, .height = 256.0f });
    ctx->input_state.mouse.x = -100.0f;
    ctx->input_state.mouse.y = -100.0f;
    LcFrame f = { 0 };
    for (int i = 0; i < 3; i++) {
        f = lc_frame(ctx, list_id, total, o);
    }
    return f;
}

static void lc_stamp_hot(FlUiCtx* ctx, FlLayoutId id) {
    ItemState* it = hashmap_get(&ctx->shared->item_state, ui_layout_id_to_clay_id(id));
    if (it != nullptr) {
        it->state.hot = 1;
    }
}

// Parks the mouse on the element for two frames, so the hit test has settled on it before a press.
static void lc_hover(FlUiCtx* ctx, FlLayoutId list, FlLayoutId target) {
    const FlElementState es = ui_get_element_state_by_id(ui_layout_id_to_clay_id(target));
    ctx->input_state.mouse.x = es.pos.x + 2.0f;
    ctx->input_state.mouse.y = es.pos.y + 5.0f;
    lc_frame(ctx, list, LC_ROWS, (LcOpts) { 0 });
    lc_frame(ctx, list, LC_ROWS, (LcOpts) { 0 });
}

UTEST(list_columns_ui, header_cells_and_body_cells_share_the_resolved_widths) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId list = ID("lc_align");
    LcFrame f = lc_settle(ctx, list, LC_ROWS, (LcOpts) { 0 });
    ASSERT_EQ_MSG(3u, f.result.column_width_count, "the result carries one width per column");
    ASSERT_NEAR(120.0, (double)f.result.column_widths[2], 0.01);

    f32 x = 0.0f;
    for (i32 c = 0; c < 3; c++) {
        const FlElementState hdr = lc_state(list.name, -3 - 2 * c);
        const FlElementState probe = lc_state(FL_S("colp"), c + 1);
        ASSERT_TRUE_MSG(hdr.found && probe.found, "header cell and body probe were laid out");
        ASSERT_NEAR((double)x, (double)hdr.pos.x, 0.01);
        ASSERT_NEAR((double)hdr.pos.x + LC_PAD, (double)probe.pos.x, 0.01);
        ASSERT_NEAR((double)hdr.size.x, (double)probe.size.x + 2.0 * LC_PAD, 0.01);
        x += f.result.column_widths[c];
    }
}

static bool lc_near(f32 a, f32 b) {
    return fabsf(a - b) < 0.01f;
}

static bool lc_is_scissor(const UiRenderCommand* cmd, f32 x, f32 width, f32 height) {
    return cmd->type == UiRenderCmd_ScissorStart && lc_near(cmd->x, x) && lc_near(cmd->width, width)
           && lc_near(cmd->height, height);
}

// True if the frame cuts drawing off at a box that starts at x and has this size.
static bool lc_frame_has_scissor(f32 x, f32 width, f32 height) {
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (ui_get_render_command(i, &cmd) && lc_is_scissor(&cmd, x, width, height)) {
            return true;
        }
    }
    return false;
}

UTEST(list_columns_ui, a_wide_cell_child_is_clipped_to_its_column) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId list = ID("lc_clip");
    lc_settle(ctx, list, LC_ROWS, (LcOpts) { .wide_first_cell = true });
    const FlElementState second = lc_state(FL_S("colp"), 2);
    ASSERT_NEAR(100.0 + LC_PAD, (double)second.pos.x, 0.01);
    ASSERT_NEAR(80.0 - 2.0 * LC_PAD, (double)second.size.x, 0.01);
    ASSERT_TRUE_MSG(lc_frame_has_scissor(0.0f, 100.0f, 24.0f), "drawing in the cell is cut off at the column edge");
}

UTEST(list_columns_ui, a_cell_child_that_can_shrink_is_fitted_to_its_column) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    lc_settle(ctx, ID("lc_shrink"), LC_ROWS, (LcOpts) { .shrink_first_cell = true });
    const FlElementState child = lc_state(FL_S("lcshrink"), 1);
    ASSERT_TRUE_MSG(child.found, "the child was laid out");
    ASSERT_NEAR(100.0 - 2.0 * LC_PAD, (double)child.size.x, 0.01);
}

UTEST(list_columns_ui, only_the_visible_rows_are_emitted) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    LcFrame f = lc_settle(ctx, ID("lc_virtual"), LC_ROWS, (LcOpts) { 0 });
    ASSERT_EQ_MSG((u32)LC_ROWS, f.range.total, "the range reports every row");
    ASSERT_EQ_MSG(0u, f.range.start, "the window starts at the first row");
    ASSERT_TRUE_MSG(f.range.end > f.range.start, "the visible rows are emitted");
    ASSERT_TRUE_MSG(f.range.end - f.range.start < 16, "a 1,000,000 row list still emits only the visible window");
}

UTEST(list_columns_ui, a_row_without_cells_spans_every_column) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    lc_settle(ctx, ID("lc_full"), LC_ROWS, (LcOpts) { .full_width_row0 = true });
    ASSERT_NEAR(300.0, (double)lc_state(FL_S("colp"), -1).size.x, 0.01);
}

UTEST(list_columns_ui, clicking_a_header_reports_its_column) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId list = ID("lc_click");
    lc_settle(ctx, list, LC_ROWS, (LcOpts) { 0 });
    lc_stamp_hot(ctx, lc_id(list.name, -3 - 2));
    ctx->mouse_pressed_this_frame = true;
    ctx->mouse_released_this_frame = true;
    LcFrame f = lc_frame(ctx, list, LC_ROWS, (LcOpts) { 0 });
    ctx->mouse_pressed_this_frame = false;
    ctx->mouse_released_this_frame = false;
    ASSERT_EQ_MSG(22u, f.result.header_clicked_column, "the clicked header names its column id");
    ASSERT_EQ_MSG(0u, f.result.resized_column, "a click is not a resize");
}

UTEST(list_columns_ui, dragging_a_header_edge_reports_the_width_from_the_press_width) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId list = ID("lc_resize");
    lc_settle(ctx, list, LC_ROWS, (LcOpts) { 0 });
    const FlLayoutId handle = lc_id(list.name, -4);
    const FlElementState hs = ui_get_element_state_by_id(ui_layout_id_to_clay_id(handle));
    ASSERT_TRUE_MSG(hs.found, "the resize handle was laid out");

    lc_hover(ctx, list, handle);
    lc_stamp_hot(ctx, handle);
    ctx->mouse_pressed_this_frame = true;
    ctx->input_state.mouse.buttons[FlMouseButton_Left] = true;
    LcFrame f = lc_frame(ctx, list, LC_ROWS, (LcOpts) { 0 });
    ctx->mouse_pressed_this_frame = false;
    ASSERT_EQ_MSG(0u, f.result.resized_column, "nothing is reported on the press");
    lc_stamp_hot(ctx, handle);
    f = lc_frame(ctx, list, LC_ROWS, (LcOpts) { 0 });
    ASSERT_EQ_MSG(0u, f.result.resized_column, "holding the edge without moving reports nothing");

    FlColumnDef cols[3] = { lc_cols[0], lc_cols[1], lc_cols[2] };
    const LcOpts fed = { .cols = cols };
    ctx->input_state.mouse.x += 25.0f;
    lc_stamp_hot(ctx, handle);
    f = lc_frame(ctx, list, LC_ROWS, fed);
    ASSERT_EQ_MSG(11u, f.result.resized_column, "the dragged column is named by id");
    ASSERT_NEAR(125.0, (double)f.result.resized_width, 0.01);

    cols[0].width = f.result.resized_width;
    ctx->input_state.mouse.x += 10.0f;
    lc_stamp_hot(ctx, handle);
    f = lc_frame(ctx, list, LC_ROWS, fed);
    ASSERT_NEAR(135.0, (double)f.result.resized_width, 0.01);

    cols[0].width = f.result.resized_width;
    ctx->input_state.mouse.buttons[FlMouseButton_Left] = false;
    ctx->mouse_released_this_frame = true;
    f = lc_frame(ctx, list, LC_ROWS, fed);
    ctx->mouse_released_this_frame = false;
    ASSERT_NEAR(135.0, (double)f.result.resized_width, 0.01);

    f = lc_frame(ctx, list, LC_ROWS, fed);
    ASSERT_EQ_MSG(0u, f.result.resized_column, "the resize ends with the button");
}

UTEST(list_columns_ui, a_resize_never_goes_below_the_column_minimum) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId list = ID("lc_resize_min");
    lc_settle(ctx, list, LC_ROWS, (LcOpts) { 0 });
    const FlLayoutId handle = lc_id(list.name, -4 - 2 * 2);
    lc_hover(ctx, list, handle);
    lc_stamp_hot(ctx, handle);
    ctx->mouse_pressed_this_frame = true;
    ctx->input_state.mouse.buttons[FlMouseButton_Left] = true;
    lc_frame(ctx, list, LC_ROWS, (LcOpts) { 0 });
    ctx->mouse_pressed_this_frame = false;
    ctx->input_state.mouse.x -= 200.0f;
    lc_stamp_hot(ctx, handle);
    LcFrame f = lc_frame(ctx, list, LC_ROWS, (LcOpts) { 0 });
    ASSERT_EQ_MSG(33u, f.result.resized_column, "the flexible column can be resized too");
    ASSERT_NEAR(40.0, (double)f.result.resized_width, 0.01);
    ctx->input_state.mouse.buttons[FlMouseButton_Left] = false;
    lc_frame(ctx, list, LC_ROWS, (LcOpts) { 0 });
}

UTEST(list_columns_ui, a_hovered_row_names_the_column_under_the_mouse) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId list = ID("lc_cell");
    lc_settle(ctx, list, LC_ROWS, (LcOpts) { 0 });
    const FlLayoutId row = lc_id(list.name, 1);
    const FlElementState rs = ui_get_element_state_by_id(ui_layout_id_to_clay_id(row));
    ASSERT_TRUE_MSG(rs.found, "the first row was laid out");

    const f32 xs[3] = { 50.0f, 150.0f, 290.0f };
    const u32 want[3] = { 11, 22, 33 };
    for (int c = 0; c < 3; c++) {
        ctx->input_state.mouse.x = rs.pos.x + xs[c];
        ctx->input_state.mouse.y = rs.pos.y + 5.0f;
        LcFrame f = { 0 };
        for (int frame = 0; frame < 3; frame++) {
            f = lc_frame(ctx, list, LC_ROWS, (LcOpts) { 0 });
        }
        ASSERT_EQ_MSG(want[c], f.row0.cell, "Signal.cell is the column id under the mouse, also over a child");
    }

    ctx->input_state.mouse.x = rs.pos.x + 150.0f;
    lc_stamp_hot(ctx, row);
    memset(ctx->mouse_buttons_pressed_this_frame, 0, sizeof(ctx->mouse_buttons_pressed_this_frame));
    ctx->mouse_buttons_pressed_this_frame[FlMouseButton_Right] = true;
    LcFrame f = lc_frame(ctx, list, LC_ROWS, (LcOpts) { 0 });
    ctx->mouse_buttons_pressed_this_frame[FlMouseButton_Right] = false;
    ASSERT_EQ_MSG(1u, (u32)f.result.context_menu_id, "the right-clicked row");
    ASSERT_EQ_MSG(22u, f.result.context_menu_cell, "and the column it was clicked in");
}

UTEST(list_columns_ui, a_row_that_skips_a_column_keeps_the_next_cell_under_its_header) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId list = ID("lc_skip");
    lc_settle(ctx, list, LC_ROWS, (LcOpts) { .skip_middle_row0 = true });
    const FlElementState hdr = lc_state(list.name, -3 - 2 * 2);
    const FlElementState probe = lc_state(FL_S("colp"), 3);
    ASSERT_TRUE_MSG(hdr.found && probe.found, "the third header cell and the third cell of row 0 were laid out");
    ASSERT_NEAR((double)hdr.pos.x + LC_PAD, (double)probe.pos.x, 0.01);
}

UTEST(list_columns_ui, the_widths_are_not_reported_before_the_list_is_measured) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId list = ID("lc_unmeasured");
    ctx->input_state.mouse.x = -100.0f;
    ctx->input_state.mouse.y = -100.0f;
    LcFrame f = lc_frame(ctx, list, LC_ROWS, (LcOpts) { 0 });
    ASSERT_EQ_MSG(0u, f.result.column_width_count, "the first frame has no widths to save");
    f = lc_settle(ctx, list, LC_ROWS, (LcOpts) { 0 });
    ASSERT_EQ_MSG(3u, f.result.column_width_count, "a measured list reports them");
}

UTEST(list_columns_ui, a_press_on_the_header_gives_the_list_keyboard_focus) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId list = ID("lc_focus");
    lc_settle(ctx, list, LC_ROWS, (LcOpts) { 0 });
    const FlLayoutId cell = lc_id(list.name, -3);
    lc_hover(ctx, list, cell);
    lc_stamp_hot(ctx, cell);
    ctx->focused_id = 0;
    ctx->mouse_pressed_this_frame = true;
    lc_frame(ctx, list, LC_ROWS, (LcOpts) { 0 });
    ctx->mouse_pressed_this_frame = false;
    ASSERT_EQ_MSG(ui_layout_id_to_clay_id(list), ctx->focused_id, "the header press focuses the list");
}

UTEST(list_columns_ui, the_columns_fit_beside_the_scrollbar_at_scale_two) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId list = ID("lc_scale");
    ctx->layout_width = 640.0f;
    ctx->layout_height = 512.0f;
    ui_set_scale(2.0f, 2.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 640.0f, .height = 512.0f });
    ctx->input_state.mouse.x = -100.0f;
    ctx->input_state.mouse.y = -100.0f;
    LcFrame f = { 0 };
    for (int i = 0; i < 4; i++) {
        f = lc_frame(ctx, list, LC_ROWS, (LcOpts) { .scrollbar = true });
    }
    const FlElementState row = lc_state(list.name, 1);
    ui_set_scale(1.0f, 1.0f);
    ASSERT_TRUE_MSG(row.found, "the first row was laid out");
    ASSERT_EQ_MSG(3u, f.result.column_width_count, "the widths are reported");
    const f32 sum = f.result.column_widths[0] + f.result.column_widths[1] + f.result.column_widths[2];
    ASSERT_TRUE_MSG(row.size.x < 600.0f, "the scrollbar takes room from the rows");
    ASSERT_NEAR((double)row.size.x / 2.0, (double)sum, 0.01);
}
