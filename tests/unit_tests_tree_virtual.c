#include "unit_tests_common.h"

#define VTREE_ROW_STRIDE 26.0f
#define VTREE_INDENT 16.0f
#define VTREE_BODY_X 200.0f
#define VTREE_BIG_COUNT 1000000u

typedef struct VTreeInput {
    Vec2 at;
    bool pressed;
    bool released;
    bool nav_left;
    bool nav_right;
    u32 modifiers;
} VTreeInput;

typedef struct VTreeModel {
    u32 root_count;
    const FlTreeExpanded* nodes;
    u32 node_count;
    FlSelectionSet* set;
} VTreeModel;

typedef struct VTreeFrame {
    FlTreeResult result;
    FlListRange range;
    FlTreeVirtualRow first;
    FlTreeVirtualRow last;
    FlTreeVirtualRow toggled;
} VTreeFrame;

static const FlTreeExpanded s_vtree_first_open[] = {
    { .parent = -1, .child_index = 0, .child_count = 3 },
};

static const FlTreeExpanded s_vtree_big[] = {
    { .parent = -1, .child_index = 1, .child_count = VTREE_BIG_COUNT },
};

static const Vec2 s_vtree_away = { 310.0f, 250.0f };

static void vtree_emit_rows(VTreeFrame* frame) {
    for (u32 row = frame->range.start; row < frame->range.end; row++) {
        FlTreeVirtualRow at = fl_tree_row_at(row);
        frame->last = at;
        if (row == frame->range.start) {
            frame->first = at;
        }
        fl_tree_item_begin_virtual(row, at.depth == 0);
        ui_layout_area(.sizing = { sizing_grow(), sizing_fixed(24) }) {}
        fl_tree_item_end();
    }
}

static VTreeFrame vtree_frame(FlUiCtx* ctx, FlLayoutId tree_id, VTreeModel model, VTreeInput in) {
    ui_input_set_mouse_position(ctx, in.at.x, in.at.y);
    ctx->input_state.mouse.buttons[FlMouseButton_Left] = in.pressed;
    ctx->input_state.mouse.buttons_pressed[FlMouseButton_Left] = in.pressed;
    ctx->input_state.mouse.buttons_released[FlMouseButton_Left] = in.released;
    ui_input_update(ctx, 1.0f / 60.0f);
    ctx->input_state.keyboard.modifiers = in.modifiers;
    ctx->nav_frame.nav_left = in.nav_left;
    ctx->nav_frame.nav_right = in.nav_right;

    VTreeFrame frame = { 0 };
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(200) }, .direction = FlLayoutDirection_TopToBottom) {
        frame.range = fl_tree_begin_virtual(tree_id, model.root_count, model.nodes, model.node_count, model.set,
                                            (FlTreeConfig) { .list = { .row_height = 24.0f, .gap = 2.0f } });
        vtree_emit_rows(&frame);
        frame.result = fl_tree_end();
        if (frame.result.toggled_id != 0) {
            frame.toggled = fl_tree_row_at(frame.result.toggled_row);
        }
    }
    ui_update_end(ctx, 1.0f / 60.0f);

    ctx->nav_frame.nav_left = false;
    ctx->nav_frame.nav_right = false;
    ctx->input_state.keyboard.modifiers = FlKeyModifier_None;
    return frame;
}

static FlUiCtx* vtree_fixture(void) {
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

static Vec2 vtree_body(u32 row) {
    return (Vec2) { VTREE_BODY_X, (f32)row * VTREE_ROW_STRIDE + 12.0f };
}

static Vec2 vtree_arrow(u32 row, u16 depth) {
    return (Vec2) { (f32)depth * VTREE_INDENT + VTREE_INDENT * 0.5f, (f32)row * VTREE_ROW_STRIDE + 12.0f };
}

static FlTreeResult vtree_press(FlUiCtx* ctx, FlLayoutId tree_id, VTreeModel model, VTreeInput in) {
    for_count(i, 3) {
        vtree_frame(ctx, tree_id, model, (VTreeInput) { .at = in.at });
    }
    in.pressed = true;
    return vtree_frame(ctx, tree_id, model, in).result;
}

static void vtree_click(FlUiCtx* ctx, FlLayoutId tree_id, VTreeModel model, VTreeInput in) {
    vtree_press(ctx, tree_id, model, in);
    vtree_frame(ctx, tree_id, model, (VTreeInput) { .at = in.at, .released = true, .modifiers = in.modifiers });
}

static FlTreeResult vtree_key(FlUiCtx* ctx, FlLayoutId tree_id, VTreeModel model, VTreeInput in) {
    in.at = s_vtree_away;
    for_count(i, 2) {
        vtree_frame(ctx, tree_id, model, (VTreeInput) { .at = in.at });
    }
    return vtree_frame(ctx, tree_id, model, in).result;
}

UTEST(tree_virtual, a_large_node_emits_only_the_visible_rows) {
    FlUiCtx* ctx = vtree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("vtree_big");
    const VTreeModel model = { 4, s_vtree_big, sizeof_array(s_vtree_big), nullptr };

    VTreeFrame frame = { 0 };
    for_count(i, 3) {
        frame = vtree_frame(ctx, tree, model, (VTreeInput) { .at = s_vtree_away });
    }
    ASSERT_EQ_MSG(0u, frame.range.start, "the tree starts at the first row");
    ASSERT_TRUE_MSG(frame.range.end < 32, "a million rows emit only what fits the viewport");
    ASSERT_EQ_MSG(-1, frame.first.parent, "the first row is a root row");
    ASSERT_EQ_MSG(0, frame.last.parent, "the last emitted row is a child of the expanded node");
    ASSERT_EQ_MSG((int)1, (int)frame.last.depth, "a child of a root node is one level in");
    ASSERT_EQ_MSG(frame.range.end - 3, frame.last.child_index, "children count from 0 below their parent");
}

UTEST(tree_virtual, arrow_press_reports_the_row) {
    FlUiCtx* ctx = vtree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("vtree_arrow");
    FlSelectionSet* set = selection_set_create(0);
    const VTreeModel model = { 4, nullptr, 0, set };

    FlTreeResult r = vtree_press(ctx, tree, model, (VTreeInput) { .at = vtree_arrow(2, 0) });
    ASSERT_EQ_MSG((u64)3, r.toggled_id, "a row's id is its row number plus one");
    ASSERT_EQ_MSG(2u, r.toggled_row, "the result names the row");
    ASSERT_TRUE_MSG(r.toggle_expanded, "a row that is not expanded is asked to expand");
    ASSERT_EQ_MSG((u64)0, selection_set_len(set), "the arrow does not select the row");

    selection_set_destroy(set);
}

UTEST(tree_virtual, right_and_left_walk_the_tree) {
    FlUiCtx* ctx = vtree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("vtree_keys");
    FlSelectionSet* set = selection_set_create(0);
    const VTreeModel closed = { 4, nullptr, 0, set };
    const VTreeModel open = { 4, s_vtree_first_open, sizeof_array(s_vtree_first_open), set };

    vtree_click(ctx, tree, closed, (VTreeInput) { .at = vtree_body(0) });
    FlTreeResult r = vtree_key(ctx, tree, closed, (VTreeInput) { .nav_right = true });
    ASSERT_EQ_MSG((u64)1, r.toggled_id, "Right on a row that is not expanded asks to expand it");
    ASSERT_TRUE_MSG(r.toggle_expanded, "the request is an expand");

    r = vtree_key(ctx, tree, open, (VTreeInput) { .nav_right = true });
    ASSERT_EQ_MSG((u64)0, r.toggled_id, "Right on an expanded row toggles nothing");
    ASSERT_EQ_MSG((u64)2, r.list.focused_id, "Right on an expanded row moves to its first child");

    vtree_key(ctx, tree, open, (VTreeInput) { 0 });
    r = vtree_key(ctx, tree, open, (VTreeInput) { .nav_left = true });
    ASSERT_EQ_MSG((u64)1, r.list.focused_id, "Left on a child moves to its parent");

    r = vtree_key(ctx, tree, open, (VTreeInput) { .nav_left = true });
    ASSERT_EQ_MSG((u64)1, r.toggled_id, "Left on an expanded row asks to collapse it");
    ASSERT_FALSE_MSG(r.toggle_expanded, "the request is a collapse");

    selection_set_destroy(set);
}

UTEST(tree_virtual, selection_follows_rows_pushed_down_by_an_expand) {
    FlUiCtx* ctx = vtree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("vtree_expand");
    FlSelectionSet* set = selection_set_create(0);
    const VTreeModel closed = { 4, nullptr, 0, set };
    const VTreeModel open = { 4, s_vtree_first_open, sizeof_array(s_vtree_first_open), set };

    vtree_click(ctx, tree, closed, (VTreeInput) { .at = vtree_body(2) });
    ASSERT_TRUE_MSG(selection_set_contains(set, 3), "the third row is selected");

    FlTreeResult r = vtree_press(ctx, tree, closed, (VTreeInput) { .at = vtree_arrow(0, 0) });
    ASSERT_EQ_MSG((u64)1, r.toggled_id, "the first row is asked to expand");

    vtree_frame(ctx, tree, open, (VTreeInput) { .at = vtree_arrow(0, 0), .released = true });
    ASSERT_EQ_MSG((u64)1, selection_set_len(set), "one row is still selected");
    ASSERT_TRUE_MSG(selection_set_contains(set, 6), "the selection moved down with its row");

    r = vtree_key(ctx, tree, open, (VTreeInput) { 0 });
    ASSERT_EQ_MSG((u64)6, r.list.focused_id, "the cursor moved down with its row");

    selection_set_destroy(set);
}

UTEST(tree_virtual, a_collapse_unselects_hidden_rows_and_pulls_later_rows_up) {
    FlUiCtx* ctx = vtree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("vtree_collapse");
    FlSelectionSet* set = selection_set_create(0);
    const VTreeModel closed = { 4, nullptr, 0, set };
    const VTreeModel open = { 4, s_vtree_first_open, sizeof_array(s_vtree_first_open), set };

    vtree_click(ctx, tree, open, (VTreeInput) { .at = vtree_body(1) });
    vtree_click(ctx, tree, open, (VTreeInput) { .at = vtree_body(5), .modifiers = FlKeyModifier_Control });
    ASSERT_EQ_MSG((u64)2, selection_set_len(set), "a child row and a later root row are selected");

    FlTreeResult r = vtree_press(ctx, tree, open, (VTreeInput) { .at = vtree_arrow(0, 0) });
    ASSERT_EQ_MSG((u64)1, r.toggled_id, "the first row is asked to collapse");
    ASSERT_FALSE_MSG(r.toggle_expanded, "the request is a collapse");

    vtree_frame(ctx, tree, closed, (VTreeInput) { .at = vtree_arrow(0, 0), .released = true });
    ASSERT_EQ_MSG((u64)1, selection_set_len(set), "the hidden child is no longer selected");
    ASSERT_TRUE_MSG(selection_set_contains(set, 3), "the later root row keeps its selection at its new row");

    selection_set_destroy(set);
}

UTEST(tree_virtual, a_row_change_without_a_toggle_clears_the_selection) {
    FlUiCtx* ctx = vtree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("vtree_unexplained");
    FlSelectionSet* set = selection_set_create(0);
    const VTreeModel closed = { 4, nullptr, 0, set };
    const VTreeModel open = { 4, s_vtree_first_open, sizeof_array(s_vtree_first_open), set };

    vtree_click(ctx, tree, closed, (VTreeInput) { .at = vtree_body(2) });
    ASSERT_EQ_MSG((u64)1, selection_set_len(set), "a row is selected");

    vtree_frame(ctx, tree, open, (VTreeInput) { .at = s_vtree_away });
    ASSERT_EQ_MSG((u64)0, selection_set_len(set), "rows changed with no toggle to explain it");

    selection_set_destroy(set);
}

UTEST(tree_virtual, an_ignored_toggle_leaves_the_selection_alone) {
    FlUiCtx* ctx = vtree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("vtree_ignored");
    FlSelectionSet* set = selection_set_create(0);
    const VTreeModel open = { 4, s_vtree_first_open, sizeof_array(s_vtree_first_open), set };

    vtree_click(ctx, tree, open, (VTreeInput) { .at = vtree_body(1) });
    FlTreeResult r = vtree_key(ctx, tree, open, (VTreeInput) { .nav_right = true });
    ASSERT_EQ_MSG(1u, r.toggled_row, "Right on a row with no children still asks, the tree cannot know");

    r = vtree_key(ctx, tree, open, (VTreeInput) { 0 });
    ASSERT_EQ_MSG((u64)1, selection_set_len(set), "the selection is unchanged");
    ASSERT_TRUE_MSG(selection_set_contains(set, 2), "the child row is still selected");
    ASSERT_EQ_MSG((u64)2, r.list.focused_id, "the cursor is still on the child row");

    selection_set_destroy(set);
}

UTEST(tree_virtual, only_rows_with_children_get_an_arrow) {
    FlUiCtx* ctx = vtree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("vtree_indent");
    const VTreeModel open = { 4, s_vtree_first_open, sizeof_array(s_vtree_first_open), nullptr };

    for_count(i, 3) {
        vtree_frame(ctx, tree, open, (VTreeInput) { .at = s_vtree_away });
    }
    const bool expect_arrow[] = { true, false, false, false, true, true, true };
    for_count(row, sizeof_array(expect_arrow)) {
        const FlLayoutId arrow_id = { .name = tree.name, .id = -(i32)(row + 1) };
        const Clay_ElementData arrow
            = Clay_GetElementData((Clay_ElementId) { .id = ui_layout_id_to_clay_id(arrow_id) });
        ASSERT_EQ_MSG(expect_arrow[row], arrow.found, "root rows have an arrow slot, child rows do not");
    }
}

UTEST(tree_virtual, a_declined_toggle_does_not_explain_a_later_row_change) {
    FlUiCtx* ctx = vtree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("vtree_declined");
    FlSelectionSet* set = selection_set_create(0);
    const VTreeModel open = { 4, s_vtree_first_open, sizeof_array(s_vtree_first_open), set };
    const VTreeModel grown = { 6, s_vtree_first_open, sizeof_array(s_vtree_first_open), set };

    vtree_click(ctx, tree, open, (VTreeInput) { .at = vtree_body(6) });
    vtree_click(ctx, tree, open, (VTreeInput) { .at = vtree_body(1), .modifiers = FlKeyModifier_Control });
    FlTreeResult r = vtree_key(ctx, tree, open, (VTreeInput) { .nav_right = true });
    ASSERT_EQ_MSG(1u, r.toggled_row, "Right on the child row asks to expand it");

    vtree_frame(ctx, tree, grown, (VTreeInput) { .at = s_vtree_away });
    ASSERT_EQ_MSG((u64)0, selection_set_len(set), "the toggled row did not expand, so the new rows are unexplained");

    selection_set_destroy(set);
}

UTEST(tree_virtual, a_focus_request_names_a_row_of_the_new_frame) {
    FlUiCtx* ctx = vtree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("vtree_seed");
    const VTreeModel closed = { 4, nullptr, 0, nullptr };
    const VTreeModel open = { 4, s_vtree_first_open, sizeof_array(s_vtree_first_open), nullptr };

    for_count(i, 3) {
        vtree_frame(ctx, tree, closed, (VTreeInput) { .at = s_vtree_away });
    }
    fl_list_request_focus(tree, 3);
    VTreeFrame frame = vtree_frame(ctx, tree, open, (VTreeInput) { .at = s_vtree_away });
    ASSERT_EQ_MSG((u64)3, frame.result.list.focused_id, "the requested row is not moved or dropped with the old rows");
}

static void vtree_dense_frame(FlUiCtx* ctx, FlLayoutId tree_id) {
    ui_input_set_mouse_position(ctx, s_vtree_away.x, s_vtree_away.y);
    ui_input_update(ctx, 1.0f / 60.0f);
    ui_update_begin(ctx);
    ui_layout_area(.sizing = { sizing_fixed(300), sizing_fixed(200) }, .direction = FlLayoutDirection_TopToBottom) {
        FlListRange range = fl_tree_begin_virtual(tree_id, 4, s_vtree_big, sizeof_array(s_vtree_big), nullptr,
                                                  (FlTreeConfig) { .list = { .row_height = 6.0f, .gap = 2.0f } });
        for (u32 row = range.start; row < range.end; row++) {
            fl_tree_item_begin_virtual(row, true);
            fl_tree_item_end();
        }
        fl_tree_end();
    }
    ui_update_end(ctx, 1.0f / 60.0f);
}

UTEST(tree_virtual, jumping_far_every_frame_keeps_working) {
    FlUiCtx* ctx = vtree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("vtree_jumps");

    for_count(i, 200) {
        vtree_dense_frame(ctx, tree);
        list_uniform_nudge_scroll(ctx->list_uniform_ctx, tree.name, 4000.0f, 8.0f, VTREE_BIG_COUNT);
    }
    const f64 offset = list_uniform_scroll_offset(ctx->list_uniform_ctx, tree.name);
    ASSERT_TRUE_MSG(offset > 700000.0, "the tree scrolled through fresh rows on every frame");
}

UTEST(tree_virtual, row_at_names_the_toggled_row_after_end) {
    FlUiCtx* ctx = vtree_fixture();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");
    const FlLayoutId tree = ID("vtree_row_after_end");
    const VTreeModel open = { 4, s_vtree_first_open, sizeof_array(s_vtree_first_open), nullptr };

    for_count(i, 3) {
        vtree_frame(ctx, tree, open, (VTreeInput) { .at = vtree_arrow(4, 0) });
    }
    VTreeFrame frame = vtree_frame(ctx, tree, open, (VTreeInput) { .at = vtree_arrow(4, 0), .pressed = true });
    ASSERT_EQ_MSG(4u, frame.result.toggled_row, "the arrow of the fifth row was pressed");
    ASSERT_EQ_MSG(-1, frame.toggled.parent, "the row is a top-level row");
    ASSERT_EQ_MSG(1u, frame.toggled.child_index, "it is the second top-level row, below the three children");
    ASSERT_EQ_MSG(-1, frame.toggled.node, "it is not expanded");
}
