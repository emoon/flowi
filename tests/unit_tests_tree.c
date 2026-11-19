#include "unit_tests_common.h"
#include <ui/ui_tree.h>
#include <flowi/ui/cell.h>

#define TREE_ROW_STRIDE 26.0f
#define TREE_INDENT 16.0f
#define TREE_BODY_X 200.0f

typedef struct TreeInput {
    f32 mx;
    f32 my;
    bool pressed;
    bool held;
    bool released;
    bool nav_left;
    bool nav_right;
    bool nav_down;
    u32 modifiers;
} TreeInput;

typedef struct TreeModel {
    const FlTreeRow* rows;
    u32 count;
    FlSelectionSet* set;
} TreeModel;

static const FlTreeRow s_open_rows[] = {
    { .id = 1, .depth = 0, .has_children = true, .expanded = true },
    { .id = 2, .depth = 1, .has_children = true, .expanded = true },
    { .id = 3, .depth = 2 },
    { .id = 4, .depth = 2 },
    { .id = 5, .depth = 1 },
    { .id = 6, .depth = 0, .has_children = true },
    { .id = 7, .depth = 0 },
};

static const FlTreeRow s_root_collapsed_rows[] = {
    { .id = 1, .depth = 0, .has_children = true },
    { .id = 6, .depth = 0, .has_children = true },
    { .id = 7, .depth = 0 },
};

static FlTreeResult tree_frame(FlUiCtx* ctx, FlLayoutId tree_id, TreeModel model, TreeInput in) {
    ui_input_set_mouse_position(ctx, in.mx, in.my);
    ctx->input_state.mouse.buttons[FlMouseButton_Left] = in.pressed || in.held;
    ctx->input_state.mouse.buttons_pressed[FlMouseButton_Left] = in.pressed;
    ctx->input_state.mouse.buttons_released[FlMouseButton_Left] = in.released;
    ui_input_update(ctx, 1.0f / 60.0f);
    ctx->input_state.keyboard.modifiers = in.modifiers;
    ctx->nav_frame.nav_left = in.nav_left;
    ctx->nav_frame.nav_right = in.nav_right;
    ctx->nav_frame.nav_down = in.nav_down;

    FlTreeResult result = { 0 };
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(200) }, .direction = FlLayoutDirection_TopToBottom) {
        FlListRange range = fl_tree_begin(tree_id, model.rows, model.count, model.set,
                                          (FlTreeConfig) { .list = { .row_height = 24.0f, .gap = 2.0f } });
        for (u32 row = range.start; row < range.end; row++) {
            fl_tree_item_begin(row);
            ui_layout_area(.sizing = { sizing_grow(), sizing_fixed(24) }) {}
            fl_tree_item_end();
        }
        result = fl_tree_end();
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ctx->nav_frame.nav_left = false;
    ctx->nav_frame.nav_right = false;
    ctx->nav_frame.nav_down = false;
    ctx->input_state.keyboard.modifiers = FlKeyModifier_None;
    return result;
}

static FlUiCtx* tree_fixture(void) {
    FlUiCtx* ctx = render_fixture_get();
    if (ctx == nullptr) {
        return nullptr;
    }
    ctx->layout_width = 320.0f;
    ctx->layout_height = 256.0f;
    ui_set_scale(1.0f, 1.0f);
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = 320.0f, .height = 256.0f });
    return ctx;
}

static f32 tree_row_y(u32 row) {
    return (f32)row * TREE_ROW_STRIDE + 12.0f;
}

static f32 tree_arrow_x(u16 depth) {
    return (f32)depth * TREE_INDENT + TREE_INDENT * 0.5f;
}

static FlTreeResult tree_click(FlUiCtx* ctx, FlLayoutId tree_id, TreeModel model, Vec2 at) {
    for_count(i, 3) {
        tree_frame(ctx, tree_id, model, (TreeInput) { .mx = at.x, .my = at.y });
    }
    FlTreeResult press = tree_frame(ctx, tree_id, model, (TreeInput) { .mx = at.x, .my = at.y, .pressed = true });
    FlTreeResult release = tree_frame(ctx, tree_id, model, (TreeInput) { .mx = at.x, .my = at.y, .released = true });
    if (release.toggled_id != 0) {
        return release;
    }
    return press;
}

static FlTreeResult tree_key(FlUiCtx* ctx, FlLayoutId tree_id, TreeModel model, TreeInput in) {
    in.mx = 310.0f;
    in.my = 250.0f;
    for_count(i, 2) {
        tree_frame(ctx, tree_id, model, (TreeInput) { .mx = in.mx, .my = in.my });
    }
    return tree_frame(ctx, tree_id, model, in);
}

UTEST(tree, arrow_press_toggles_without_selecting) {
    FlUiCtx* ctx = tree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("tree_arrow");
    FlSelectionSet* set = selection_set_create(0);
    const TreeModel model = { s_open_rows, sizeof_array(s_open_rows), set };

    FlTreeResult r = tree_click(ctx, tree, model, (Vec2) { tree_arrow_x(1), tree_row_y(1) });
    ASSERT_EQ_MSG((u64)2, r.toggled_id, "a press on the arrow reports the row");
    ASSERT_FALSE_MSG(r.toggle_expanded, "an expanded row is asked to collapse");
    ASSERT_EQ_MSG((u64)0, selection_set_len(set), "the arrow does not select the row");

    r = tree_click(ctx, tree, model, (Vec2) { tree_arrow_x(0), tree_row_y(5) });
    ASSERT_EQ_MSG((u64)6, r.toggled_id, "a press on a collapsed row's arrow reports the row");
    ASSERT_TRUE_MSG(r.toggle_expanded, "a collapsed row is asked to expand");

    r = tree_click(ctx, tree, model, (Vec2) { tree_arrow_x(2), tree_row_y(2) });
    ASSERT_EQ_MSG((u64)0, r.toggled_id, "a leaf has no arrow to press");

    r = tree_click(ctx, tree, model, (Vec2) { TREE_BODY_X, tree_row_y(1) });
    ASSERT_EQ_MSG((u64)0, r.toggled_id, "a click on the row body does not toggle");
    ASSERT_TRUE_MSG(selection_set_contains(set, 2), "a click on the row body selects the row");

    selection_set_destroy(set);
}

UTEST(tree, arrow_press_does_not_start_a_marquee) {
    FlUiCtx* ctx = tree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("tree_arrow_drag");
    FlSelectionSet* set = selection_set_create(0);
    const TreeModel model = { s_open_rows, sizeof_array(s_open_rows), set };
    const f32 x = tree_arrow_x(0);

    for_count(i, 3) {
        tree_frame(ctx, tree, model, (TreeInput) { .mx = x, .my = tree_row_y(0) });
    }
    tree_frame(ctx, tree, model, (TreeInput) { .mx = x, .my = tree_row_y(0), .pressed = true });
    for_count(i, 4) {
        tree_frame(ctx, tree, model, (TreeInput) { .mx = x + 40.0f, .my = tree_row_y(4), .held = true });
    }
    ASSERT_EQ_MSG((u64)0, selection_set_len(set), "dragging from the arrow selects nothing");

    tree_frame(ctx, tree, model, (TreeInput) { .mx = x + 40.0f, .my = tree_row_y(4), .released = true });
    selection_set_destroy(set);
}

UTEST(tree, right_expands_then_steps_into_the_first_child) {
    FlUiCtx* ctx = tree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("tree_right");
    FlSelectionSet* set = selection_set_create(0);
    const TreeModel model = { s_open_rows, sizeof_array(s_open_rows), set };

    FlTreeResult r = tree_key(ctx, tree, model, (TreeInput) { .nav_right = true });
    ASSERT_EQ_MSG((u64)0, r.toggled_id, "an unfocused tree ignores Right");

    tree_click(ctx, tree, model, (Vec2) { TREE_BODY_X, tree_row_y(5) });
    r = tree_key(ctx, tree, model, (TreeInput) { .nav_right = true });
    ASSERT_EQ_MSG((u64)6, r.toggled_id, "Right on a collapsed row asks to expand it");
    ASSERT_TRUE_MSG(r.toggle_expanded, "the request is an expand");
    ASSERT_EQ_MSG((u64)6, r.list.focused_id, "the cursor stays on the row");

    tree_click(ctx, tree, model, (Vec2) { TREE_BODY_X, tree_row_y(0) });
    r = tree_key(ctx, tree, model, (TreeInput) { .nav_right = true });
    ASSERT_EQ_MSG((u64)0, r.toggled_id, "Right on an expanded row toggles nothing");
    ASSERT_EQ_MSG((u64)2, r.list.focused_id, "Right on an expanded row moves to its first child");
    ASSERT_EQ_MSG((u64)1, selection_set_len(set), "the move leaves the selection alone");
    ASSERT_TRUE_MSG(selection_set_contains(set, 1), "the clicked row is still the selected one");

    tree_click(ctx, tree, model, (Vec2) { TREE_BODY_X, tree_row_y(2) });
    r = tree_key(ctx, tree, model, (TreeInput) { .nav_right = true });
    ASSERT_EQ_MSG((u64)0, r.toggled_id, "Right on a leaf toggles nothing");
    ASSERT_EQ_MSG((u64)3, r.list.focused_id, "Right on a leaf leaves the cursor in place");

    selection_set_destroy(set);
}

UTEST(tree, left_collapses_then_steps_out_to_the_parent) {
    FlUiCtx* ctx = tree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("tree_left");
    FlSelectionSet* set = selection_set_create(0);
    const TreeModel model = { s_open_rows, sizeof_array(s_open_rows), set };

    tree_click(ctx, tree, model, (Vec2) { TREE_BODY_X, tree_row_y(3) });
    FlTreeResult r = tree_key(ctx, tree, model, (TreeInput) { .nav_left = true });
    ASSERT_EQ_MSG((u64)0, r.toggled_id, "Left on a leaf toggles nothing");
    ASSERT_EQ_MSG((u64)2, r.list.focused_id, "Left on a leaf moves to its parent");

    r = tree_key(ctx, tree, model, (TreeInput) { .nav_left = true });
    ASSERT_EQ_MSG((u64)2, r.toggled_id, "Left on an expanded row asks to collapse it");
    ASSERT_FALSE_MSG(r.toggle_expanded, "the request is a collapse");
    ASSERT_EQ_MSG((u64)2, r.list.focused_id, "the cursor stays on the row");

    tree_click(ctx, tree, model, (Vec2) { TREE_BODY_X, tree_row_y(5) });
    r = tree_key(ctx, tree, model, (TreeInput) { .nav_left = true });
    ASSERT_EQ_MSG((u64)0, r.toggled_id, "Left on a collapsed root toggles nothing");
    ASSERT_EQ_MSG((u64)6, r.list.focused_id, "Left on a root with no parent leaves the cursor in place");

    selection_set_destroy(set);
}

UTEST(tree, left_and_right_are_consumed_only_while_focused) {
    FlUiCtx* ctx = tree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("tree_consume");
    const TreeModel model = { s_open_rows, sizeof_array(s_open_rows), nullptr };

    for_count(i, 2) {
        tree_key(ctx, tree, model, (TreeInput) { 0 });
    }
    ui_input_set_mouse_position(ctx, 310.0f, 250.0f);
    ui_input_update(ctx, 1.0f / 60.0f);
    ctx->nav_frame.nav_left = true;
    ui_update_begin(ctx);
    fl_tree_begin(tree, model.rows, model.count, nullptr, (FlTreeConfig) { 0 });
    ASSERT_TRUE_MSG(fl_get_nav_frame().nav_left, "an unfocused tree leaves Left to spatial navigation");
    fl_tree_end();
    ui_update_end(ctx, 1.0f / 60.0f);

    tree_click(ctx, tree, model, (Vec2) { TREE_BODY_X, tree_row_y(6) });
    ui_input_set_mouse_position(ctx, 310.0f, 250.0f);
    ui_input_update(ctx, 1.0f / 60.0f);
    ctx->nav_frame.nav_left = true;
    ui_update_begin(ctx);
    fl_tree_begin(tree, model.rows, model.count, nullptr, (FlTreeConfig) { 0 });
    ASSERT_FALSE_MSG(fl_get_nav_frame().nav_left, "a focused tree consumes Left");
    fl_tree_end();
    ui_update_end(ctx, 1.0f / 60.0f);
    ctx->nav_frame.nav_left = false;
}

UTEST(tree, right_is_consumed_when_down_moves_the_cursor) {
    FlUiCtx* ctx = tree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("tree_consume_both");
    const TreeModel model = { s_open_rows, sizeof_array(s_open_rows), nullptr };

    for_count(i, 2) {
        tree_key(ctx, tree, model, (TreeInput) { 0 });
    }
    tree_click(ctx, tree, model, (Vec2) { TREE_BODY_X, tree_row_y(0) });
    ui_input_set_mouse_position(ctx, 310.0f, 250.0f);
    ui_input_update(ctx, 1.0f / 60.0f);
    ctx->nav_frame.nav_down = true;
    ctx->nav_frame.nav_right = true;
    ui_update_begin(ctx);
    fl_tree_begin(tree, model.rows, model.count, nullptr, (FlTreeConfig) { 0 });
    const bool right_left_over = fl_get_nav_frame().nav_right;
    fl_tree_end();
    ui_update_end(ctx, 1.0f / 60.0f);
    ctx->nav_frame.nav_down = false;
    ctx->nav_frame.nav_right = false;
    ASSERT_FALSE_MSG(right_left_over, "a focused tree consumes Right even when Down moved the cursor");
}

UTEST(tree, collapse_moves_a_cursor_inside_the_subtree_to_the_row) {
    FlUiCtx* ctx = tree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("tree_collapse_cursor");
    FlSelectionSet* set = selection_set_create(0);
    const TreeModel open = { s_open_rows, sizeof_array(s_open_rows), set };
    const TreeModel collapsed = { s_root_collapsed_rows, sizeof_array(s_root_collapsed_rows), set };

    tree_click(ctx, tree, open, (Vec2) { TREE_BODY_X, tree_row_y(3) });
    FlTreeResult r = tree_click(ctx, tree, open, (Vec2) { tree_arrow_x(0), tree_row_y(0) });
    ASSERT_EQ_MSG((u64)1, r.toggled_id, "the root's arrow asks to collapse it");

    r = tree_key(ctx, tree, collapsed, (TreeInput) { 0 });
    ASSERT_EQ_MSG((u64)1, r.list.focused_id, "the cursor moved from the hidden row to the collapsed one");
    ASSERT_TRUE_MSG(selection_set_contains(set, 4), "the hidden row stays selected");

    r = tree_key(ctx, tree, collapsed, (TreeInput) { .nav_down = true });
    ASSERT_EQ_MSG((u64)6, r.list.focused_id, "Down continues from the collapsed row");

    selection_set_destroy(set);
}

UTEST(tree, collapse_leaves_a_cursor_outside_the_subtree_alone) {
    FlUiCtx* ctx = tree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("tree_collapse_other");
    FlSelectionSet* set = selection_set_create(0);
    const TreeModel open = { s_open_rows, sizeof_array(s_open_rows), set };
    const TreeModel collapsed = { s_root_collapsed_rows, sizeof_array(s_root_collapsed_rows), set };

    tree_click(ctx, tree, open, (Vec2) { TREE_BODY_X, tree_row_y(6) });
    tree_click(ctx, tree, open, (Vec2) { tree_arrow_x(0), tree_row_y(0) });
    FlTreeResult r = tree_key(ctx, tree, collapsed, (TreeInput) { 0 });
    ASSERT_EQ_MSG((u64)7, r.list.focused_id, "a cursor outside the collapsed subtree stays where it was");

    selection_set_destroy(set);
}

UTEST(tree, rows_indent_by_depth) {
    FlUiCtx* ctx = tree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("tree_indent");
    const TreeModel model = { s_open_rows, sizeof_array(s_open_rows), nullptr };

    for_count(i, 3) {
        tree_key(ctx, tree, model, (TreeInput) { 0 });
    }
    const u32 container_id = ui_layout_id_to_clay_id(list_container_layout_id(tree.name));
    const Clay_ElementData container = Clay_GetElementData((Clay_ElementId) { .id = container_id });
    ASSERT_TRUE_MSG(container.found, "the tree container was laid out");
    for_count(row, sizeof_array(s_open_rows)) {
        const FlLayoutId arrow_id = list_layout_id(tree.name, LIST_ID_TREE_ARROW(row));
        const Clay_ElementData arrow
            = Clay_GetElementData((Clay_ElementId) { .id = ui_layout_id_to_clay_id(arrow_id) });
        ASSERT_EQ_MSG(s_open_rows[row].has_children, arrow.found, "only rows with children have an arrow slot");
        if (!arrow.found) {
            continue;
        }
        const f32 expected = container.boundingBox.x + (f32)s_open_rows[row].depth * TREE_INDENT;
        ASSERT_TRUE_MSG(fabsf(arrow.boundingBox.x - expected) < 0.01f, "the slot starts one indent per depth level in");
        ASSERT_TRUE_MSG(fabsf(arrow.boundingBox.width - TREE_INDENT) < 0.01f, "the slot is one indent wide");
    }
}

UTEST(tree, null_rows_are_an_empty_tree) {
    FlUiCtx* ctx = tree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const TreeModel model = { nullptr, 5, nullptr };

    FlTreeResult r = tree_key(ctx, ID("tree_null"), model, (TreeInput) { .nav_right = true });
    ASSERT_EQ_MSG((u64)0, r.toggled_id, "nothing toggles");
    ASSERT_EQ_MSG((u64)0, r.list.focused_id, "nothing is focused");
}

#ifdef NDEBUG
UTEST(tree, a_row_past_the_count_starts_no_row) {
    FlUiCtx* ctx = tree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const u32 bad_row = sizeof_array(s_open_rows) + 1000000;
    FlSelectionSet* set = selection_set_create(0);

    bool bad_signal_empty = true;
    FlTreeResult plain = { 0 };
    FlTreeResult flat = { 0 };
    for_count(i, 3) {
        ui_input_set_mouse_position(ctx, TREE_BODY_X, tree_row_y(1));
        ctx->input_state.mouse.buttons[FlMouseButton_Left] = i == 1;
        ctx->input_state.mouse.buttons_pressed[FlMouseButton_Left] = i == 1;
        ctx->input_state.mouse.buttons_released[FlMouseButton_Left] = i == 2;
        ui_input_update(ctx, 1.0f / 60.0f);
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(200) }, .direction = FlLayoutDirection_TopToBottom) {
            FlListRange range = fl_tree_begin(ID("tree_bad_row"), s_open_rows, sizeof_array(s_open_rows), set,
                                              (FlTreeConfig) { .list = { .row_height = 24.0f, .gap = 2.0f } });
            for (u32 row = range.start; row < range.end; row++) {
                fl_tree_item_begin(row);
                ui_layout_area(.sizing = { sizing_grow(), sizing_fixed(24) }) {}
                fl_tree_item_end();
            }
            FlSignal bad = fl_tree_item_begin(bad_row);
            fl_tree_item_end();
            bad_signal_empty &= !bad.hovered && !bad.clicked && !bad.pressed;
            plain = fl_tree_end();
        }
        ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(200) }, .direction = FlLayoutDirection_TopToBottom) {
            fl_tree_begin_virtual(ID("tree_bad_virtual_row"), 4, nullptr, 0, nullptr,
                                  (FlTreeConfig) { .list = { .row_height = 24.0f } });
            FlSignal bad = fl_tree_item_begin_virtual(bad_row, true);
            fl_tree_item_end();
            bad_signal_empty &= !bad.hovered && !bad.clicked && !bad.pressed;
            flat = fl_tree_end();
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }
    ctx->input_state.mouse.buttons_released[FlMouseButton_Left] = false;
    ASSERT_TRUE_MSG(bad_signal_empty, "a row that does not exist reports no input");
    ASSERT_EQ_MSG((u64)0, plain.toggled_id, "a row that does not exist toggles nothing");
    ASSERT_EQ_MSG((u64)0, flat.toggled_id, "a virtual row that does not exist toggles nothing");
    ASSERT_EQ_MSG((u64)1, selection_set_len(set), "the click selects one row");
    ASSERT_TRUE_MSG(selection_set_contains(set, s_open_rows[1].id), "the rows before the bad one still work");
    selection_set_destroy(set);
}
#endif

UTEST(tree, shift_click_after_the_anchor_was_hidden_selects_the_row) {
    FlUiCtx* ctx = tree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("tree_hidden_anchor");
    FlSelectionSet* set = selection_set_create(0);
    const TreeModel open = { s_open_rows, sizeof_array(s_open_rows), set };
    const TreeModel collapsed = { s_root_collapsed_rows, sizeof_array(s_root_collapsed_rows), set };

    tree_click(ctx, tree, open, (Vec2) { TREE_BODY_X, tree_row_y(3) });
    tree_click(ctx, tree, open, (Vec2) { tree_arrow_x(0), tree_row_y(0) });

    for_count(i, 3) {
        tree_frame(ctx, tree, collapsed, (TreeInput) { .mx = TREE_BODY_X, .my = tree_row_y(2) });
    }
    const TreeInput at = { .mx = TREE_BODY_X, .my = tree_row_y(2), .modifiers = FlKeyModifier_Shift };
    TreeInput press = at;
    press.pressed = true;
    TreeInput release = at;
    release.released = true;
    tree_frame(ctx, tree, collapsed, press);
    tree_frame(ctx, tree, collapsed, release);

    ASSERT_EQ_MSG((u64)1, selection_set_len(set), "the click falls back to a plain replace");
    ASSERT_TRUE_MSG(selection_set_contains(set, 7), "the clicked row is selected");

    selection_set_destroy(set);
}

UTEST(tree, dragging_a_row_does_not_reorder) {
    FlUiCtx* ctx = tree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("tree_no_reorder");
    FlSelectionSet* set = selection_set_create(0);
    const TreeModel model = { s_open_rows, sizeof_array(s_open_rows), set };

    for_count(i, 3) {
        tree_frame(ctx, tree, model, (TreeInput) { .mx = TREE_BODY_X, .my = tree_row_y(0) });
    }
    tree_frame(ctx, tree, model, (TreeInput) { .mx = TREE_BODY_X, .my = tree_row_y(0), .pressed = true });
    FlTreeResult mid
        = tree_frame(ctx, tree, model, (TreeInput) { .mx = TREE_BODY_X, .my = tree_row_y(4), .held = true });
    ASSERT_FALSE_MSG(mid.list.drag_active, "moving a pressed row does not begin a reorder drag");

    FlTreeResult drop
        = tree_frame(ctx, tree, model, (TreeInput) { .mx = TREE_BODY_X, .my = tree_row_y(4), .released = true });
    ASSERT_FALSE_MSG(drop.list.reorder_emitted, "releasing over another row emits no reorder request");

    selection_set_destroy(set);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define TREE_CELL_PAD 6.0f
#define TREE_COL_ROWS 7

typedef struct TreeColsFrame {
    FlTreeResult result;
    FlSignal rows[TREE_COL_ROWS];
} TreeColsFrame;

static FlElementState tree_probe(u32 row, u32 col) {
    FlLayoutId id = { .name = FL_S("tcp"), .id = (i32)(row * 10 + col + 1) };
    return ui_get_element_state_by_id(ui_layout_id_to_clay_id(id));
}

static FlElementState tree_header_cell(FlLayoutId tree, i32 col) {
    FlLayoutId id = { .name = tree.name, .id = -3 - 2 * col };
    return ui_get_element_state_by_id(ui_layout_id_to_clay_id(id));
}

static void tree_cells(u32 row) {
    const u32 ids[2] = { 11, 22 };
    for (u32 c = 0; c < 2; c++) {
        fl_cell_begin(ids[c]);
        FlLayoutId pid = { .name = FL_S("tcp"), .id = (i32)(row * 10 + c + 1) };
        fl_layout_start(pid);
        fl_layout_configure(
            (FlLayoutConfig) { .sizing = { sizing_grow(), sizing_fixed(10) }, .clip = { .horizontal = true } });
        fl_item_behavior(pid, FlItemFlags_clickable);
        fl_layout_end();
        fl_cell_end();
    }
}

static FlTreeConfig tree_cols_config(const FlColumnDef* cols) {
    return (FlTreeConfig) { .list = { .row_height = 24.0f, .columns = cols, .column_count = 2 } };
}

static void tree_cols_input(FlUiCtx* ctx, TreeInput in) {
    ui_input_set_mouse_position(ctx, in.mx, in.my);
    ctx->input_state.mouse.buttons[FlMouseButton_Left] = in.pressed || in.held;
    ctx->input_state.mouse.buttons_pressed[FlMouseButton_Left] = in.pressed;
    ctx->input_state.mouse.buttons_released[FlMouseButton_Left] = in.released;
    ui_input_update(ctx, 1.0f / 60.0f);
}

static TreeColsFrame tree_columns_frame(FlUiCtx* ctx, FlLayoutId tree_id, f32 first_width, TreeInput in) {
    const FlColumnDef cols[2] = {
        { .id = 11, .title = { .data = "A", .length = 1 }, .width = first_width },
        { .id = 22, .title = { .data = "B", .length = 1 }, .width = 80.0f },
    };
    TreeColsFrame f = { 0 };
    tree_cols_input(ctx, in);
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(240) }, .direction = FlLayoutDirection_TopToBottom) {
        FlListRange range
            = fl_tree_begin(tree_id, s_open_rows, sizeof_array(s_open_rows), nullptr, tree_cols_config(cols));
        for (u32 row = range.start; row < range.end; row++) {
            f.rows[row] = fl_tree_item_begin(row);
            tree_cells(row);
            fl_tree_item_end();
        }
        f.result = fl_tree_end();
    }
    ui_update_end(ctx, 1.0f / 60.0f);
    return f;
}

static const TreeInput s_tree_mouse_away = { .mx = -100.0f, .my = -100.0f };

UTEST(tree, column_edges_line_up_at_every_depth) {
    FlUiCtx* ctx = tree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("tree_columns");
    for_count(i, 3) {
        tree_columns_frame(ctx, tree, 120.0f, s_tree_mouse_away);
    }

    const FlElementState second = tree_header_cell(tree, 1);
    ASSERT_TRUE_MSG(second.found, "the second header cell was laid out");
    for_count(i, 3) {
        const FlElementState col0 = tree_probe(i, 0);
        const FlElementState col1 = tree_probe(i, 1);
        ASSERT_TRUE_MSG(col0.found && col1.found, "both cells were laid out");
        ASSERT_NEAR((double)second.pos.x + TREE_CELL_PAD, (double)col1.pos.x, 0.01);
        ASSERT_NEAR((double)second.pos.x - TREE_CELL_PAD, (double)(col0.pos.x + col0.size.x), 0.01);
        ASSERT_NEAR((double)tree_probe(0, 0).pos.x + (double)i * TREE_INDENT, (double)col0.pos.x, 0.01);
    }
}

UTEST(tree, a_lead_wider_than_the_first_column_leaves_the_other_columns_in_place) {
    FlUiCtx* ctx = tree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("tree_columns_narrow");
    for_count(i, 3) {
        tree_columns_frame(ctx, tree, 20.0f, s_tree_mouse_away);
    }

    const FlElementState second = tree_header_cell(tree, 1);
    ASSERT_TRUE_MSG(second.found, "the second header cell was laid out");
    ASSERT_NEAR(20.0, (double)second.pos.x, 0.01);
    for_count(i, 5) {
        const FlElementState col1 = tree_probe(i, 1);
        ASSERT_TRUE_MSG(col1.found, "the second cell was laid out");
        ASSERT_NEAR((double)second.pos.x + TREE_CELL_PAD, (double)col1.pos.x, 0.01);
    }
}

UTEST(tree, a_row_at_depth_names_the_column_under_the_mouse) {
    FlUiCtx* ctx = tree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("tree_columns_cell");
    for_count(i, 3) {
        tree_columns_frame(ctx, tree, 120.0f, s_tree_mouse_away);
    }
    const f32 y = tree_probe(2, 0).pos.y + 2.0f;

    TreeColsFrame f = { 0 };
    for_count(i, 3) {
        f = tree_columns_frame(ctx, tree, 120.0f, (TreeInput) { .mx = 100.0f, .my = y });
    }
    ASSERT_EQ_MSG(11u, f.rows[2].cell, "inside the first column at depth 2");
    ASSERT_EQ_MSG(0u, f.rows[1].cell, "another row names no column");

    for_count(i, 3) {
        f = tree_columns_frame(ctx, tree, 120.0f, (TreeInput) { .mx = 150.0f, .my = y });
    }
    ASSERT_EQ_MSG(22u, f.rows[2].cell, "inside the second column at depth 2");

    const f32 arrow_y = tree_probe(1, 0).pos.y + 2.0f;
    for_count(i, 3) {
        f = tree_columns_frame(ctx, tree, 120.0f, (TreeInput) { .mx = tree_arrow_x(1), .my = arrow_y });
    }
    ASSERT_EQ_MSG(11u, f.rows[1].cell, "over the arrow, which sits in the first column");
}

UTEST(tree, an_arrow_press_toggles_in_a_tree_with_columns) {
    FlUiCtx* ctx = tree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("tree_columns_arrow");
    for_count(i, 3) {
        tree_columns_frame(ctx, tree, 120.0f, s_tree_mouse_away);
    }
    const TreeInput at = { .mx = tree_arrow_x(1), .my = tree_probe(1, 0).pos.y + 2.0f };
    for_count(i, 3) {
        tree_columns_frame(ctx, tree, 120.0f, at);
    }
    TreeInput press = at;
    press.pressed = true;
    TreeColsFrame f = tree_columns_frame(ctx, tree, 120.0f, press);
    TreeInput release = at;
    release.released = true;
    TreeColsFrame g = tree_columns_frame(ctx, tree, 120.0f, release);
    const u64 toggled = f.result.toggled_id != 0 ? f.result.toggled_id : g.result.toggled_id;
    ASSERT_EQ_MSG((u64)2, toggled, "the arrow of the depth 1 row toggles it");
}

static const FlTreeExpanded s_vtree_cols_open[] = {
    { .parent = -1, .child_index = 0, .child_count = 3 },
};

UTEST(tree, a_virtual_tree_keeps_its_columns_under_the_header) {
    FlUiCtx* ctx = tree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("vtree_columns");
    const FlColumnDef cols[2] = {
        { .id = 11, .title = { .data = "A", .length = 1 }, .width = 120.0f },
        { .id = 22, .title = { .data = "B", .length = 1 }, .width = 80.0f },
    };
    u16 depths[4] = { 0 };
    for_count(frame, 3) {
        tree_cols_input(ctx, s_tree_mouse_away);
        ui_update_begin(ctx);
        ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(240) }, .direction = FlLayoutDirection_TopToBottom) {
            FlListRange range = fl_tree_begin_virtual(tree, 2, s_vtree_cols_open, 1, nullptr, tree_cols_config(cols));
            for (u32 row = range.start; row < range.end && row < 4; row++) {
                FlTreeVirtualRow at = fl_tree_row_at(row);
                depths[row] = at.depth;
                fl_tree_item_begin_virtual(row, at.depth == 0);
                tree_cells(row);
                fl_tree_item_end();
            }
            fl_tree_end();
        }
        ui_update_end(ctx, 1.0f / 60.0f);
    }

    const FlElementState second = tree_header_cell(tree, 1);
    ASSERT_TRUE_MSG(second.found, "the second header cell was laid out");
    ASSERT_EQ_MSG(1u, (u32)depths[1], "the second row is a child");
    for_count(i, 2) {
        const FlElementState col0 = tree_probe(i, 0);
        const FlElementState col1 = tree_probe(i, 1);
        ASSERT_TRUE_MSG(col0.found && col1.found, "both cells were laid out");
        ASSERT_NEAR((double)second.pos.x + TREE_CELL_PAD, (double)col1.pos.x, 0.01);
        ASSERT_NEAR((double)(depths[i] + 1) * TREE_INDENT + TREE_CELL_PAD, (double)col0.pos.x, 0.01);
    }
}
