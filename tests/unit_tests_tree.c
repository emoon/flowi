#include "unit_tests_common.h"

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
    ASSERT_FALSE_MSG(fl_get_nav_frame().nav_right, "a focused tree consumes Right even when Down moved the cursor");
    fl_tree_end();
    ui_update_end(ctx, 1.0f / 60.0f);
    ctx->nav_frame.nav_down = false;
    ctx->nav_frame.nav_right = false;
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
    const u32 container_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = tree.name, .id = 0 });
    const Clay_ElementData container = Clay_GetElementData((Clay_ElementId) { .id = container_id });
    ASSERT_TRUE_MSG(container.found, "the tree container was laid out");
    for_count(row, sizeof_array(s_open_rows)) {
        const FlLayoutId arrow_id = { .name = tree.name, .id = -(i32)(row + 1) };
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
