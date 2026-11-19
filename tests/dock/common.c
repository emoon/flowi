#include "dock/common.h"

FlDockPanelId dock_leaf_panel0(const FlDockNode* leaf) {
    return leaf->leaf.count > 0 ? leaf->leaf.panels[0] : 0;
}

FlDockTree* dock_pair_tree(FlDockPanelId a, FlDockPanelId b) {
    return dock_tree_build(dock_split(FlDockAxis_X, FL_DOCK_SPLIT_RATIO_DEFAULT, dock_leaf(a), dock_leaf(b)));
}

bool dock_tab_into(FlDockTree* tree, FlDockNodeId target_node, FlDockPanelId panel) {
    FlDockRequest req = { .kind = FlDockRequest_Tab, .panel = panel, .target_node = target_node };
    return dock_tree_apply(tree, &req) && dock_tree_check_invariants(tree);
}

bool dock_tab_panels(FlDockTree* tree, FlDockNodeId target_node, const FlDockPanelId* panels, u32 count) {
    for (u32 i = 0; i < count; ++i) {
        if (!dock_tab_into(tree, target_node, panels[i])) {
            return false;
        }
    }
    return true;
}

void dock_expect_tab_order(int* utest_result, const FlDockNode* leaf, const DockLeafWant* want) {
    ASSERT_EQ_MSG((int)leaf->leaf.count, (int)want->count, "the leaf holds the expected tabs");
    DOCK_STOP_ON_FAILURE();
    for (u32 i = 0; i < want->count; ++i) {
        ASSERT_TRUE_MSG(leaf->leaf.panels[i] == want->panels[i], "the leaf holds the panels in tab order");
    }
}

void dock_expect_leaf(int* utest_result, const FlDockNode* node, const DockLeafWant* want) {
    ASSERT_TRUE_MSG(node != nullptr, "the leaf exists");
    ASSERT_EQ_MSG((int)node->kind, (int)FlDockNodeKind_Leaf, "the node is a leaf");
    dock_expect_tab_order(utest_result, node, want);
    DOCK_STOP_ON_FAILURE();
    if (want->count > 0) {
        ASSERT_EQ_MSG((int)node->leaf.selected, (int)want->selected, "the expected tab is selected");
    }
}

void dock_expect_universe(int* utest_result, FlDockTree* tree, u32 count) {
    const u32 got = dock_tree_collect_panels(tree, nullptr, 0);
    ASSERT_EQ_MSG((int)got, (int)count, "the panel universe holds the expected panels");
}

static void dock_expect_tri(int* utest_result, bool got, DockTri want, const char* what) {
    if (want == DockTri_Any) {
        return;
    }
    ASSERT_EQ_MSG((int)got, want == DockTri_Yes ? 1 : 0, what);
}

void dock_expect_panel_states(int* utest_result, FlDockWorkspace* ws, const PanelWant* rows, u32 count) {
    for (u32 i = 0; i < count; ++i) {
        const FlDockPanelId panel = rows[i].panel;
        dock_expect_tri(utest_result, dock_workspace_panel_should_emit(ws, panel), rows[i].emit,
                        "the panel's emit decision");
        dock_expect_tri(utest_result, dock_workspace_panel_declared(ws, panel), rows[i].declared,
                        "whether the panel was declared this frame");
        dock_expect_tri(utest_result, dock_workspace_panel_reserved(ws, panel), rows[i].reserved,
                        "whether the panel's slot is reserved");
        dock_expect_tri(utest_result, dock_workspace_panel_visible(ws, panel), rows[i].visible,
                        "whether the panel is shown");
    }
}

void dock_expect_panel_leaves(int* utest_result, FlDockWorkspace* ws, const PanelLeaf* rows, u32 count) {
    for (u32 i = 0; i < count; ++i) {
        ASSERT_EQ_MSG((int)dock_workspace_panel_leaf(ws, rows[i].panel), (int)rows[i].node,
                      "the panel resolves to the leaf holding it");
    }
}

void dock_expect_box_on(int* utest_result, Clay_BoundingBox box, Clay_BoundingBox target) {
    const f32 got[4] = { box.x, box.y, box.width, box.height };
    const f32 want[4] = { target.x, target.y, target.width, target.height };
    for (u32 i = 0; i < 4; ++i) {
        ASSERT_NEAR_MSG(got[i], want[i], 1.0f, "the box sits on the rect it has to match");
    }
}

void dock_expect_title(int* utest_result, FlDockWorkspace* ws, FlDockPanelId panel, FlString want) {
    ASSERT_TRUE_MSG(fl_string_equals(dock_workspace_panel_title(ws, panel), want),
                    "the retained title reads back as declared");
}

static bool dock_leaf_equal(const FlDockNode* a, const FlDockNode* b) {
    if (a->leaf.count != b->leaf.count || a->leaf.selected != b->leaf.selected) {
        return false;
    }
    return memcmp(a->leaf.panels, b->leaf.panels, a->leaf.count * sizeof(a->leaf.panels[0])) == 0;
}

static bool dock_split_header_equal(const FlDockNode* a, const FlDockNode* b) {
    return a->split.axis == b->split.axis && fabsf(a->split.ratio - b->split.ratio) <= 1e-5f;
}

bool dock_logical_equal(const FlDockNode* a, const FlDockNode* b) {
    if (a == nullptr || b == nullptr) {
        return a == b;
    }
    if (a->kind != b->kind) {
        return false;
    }
    if (a->kind == FlDockNodeKind_Leaf) {
        return dock_leaf_equal(a, b);
    }
    return dock_split_header_equal(a, b) && dock_logical_equal(a->split.child[0], b->split.child[0])
           && dock_logical_equal(a->split.child[1], b->split.child[1]);
}

DockApp dock_app_open(FlString title, i32 width, i32 height) {
    DockApp scope = { nullptr };
    if (render_fixture_get() == nullptr) {
        return scope;
    }
    FlAppSettings settings = { .window_title = title, .width = width, .height = height, .headless = true };
    scope.app = fl_app_create(&settings);
    if (scope.app != nullptr) {
        fl_app_set_test_dt(scope.app, 1.0f / 60.0f);
    }
    return scope;
}

void dock_app_close(DockApp* scope) {
    if (scope->app != nullptr) {
        fl_app_inject_mouse_button(scope->app, FlMouseButton_Left, false);
        fl_app_destroy(scope->app);
    }
}

FlDockWorkspace* dock_fixture_emit(DockFixture* st, const FlDockSpec* spec) {
    st->ctx = g_ui_ctx;
    FlDockWorkspace* ws = dock_workspace_current();
    st->ws = ws;
    if (!st->installed && ws != nullptr) {
        dock_workspace_set_tree(ws, dock_tree_build(spec));
        st->installed = true;
    }
    dock_workspace_begin(ws);
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
        dock_workspace_emit(ws);
    }
    return ws;
}

void dock_ticks(FlApp* app, void* st, void (*frame)(void*), int n) {
    for (int i = 0; i < n; ++i) {
        fl_app_tick(app, st, frame);
    }
}

void dock_settle_hover(FlApp* app, void* st, void (*frame)(void*)) {
    dock_ticks(app, st, frame, 4);
}

void dock_press_at(const DockRig* rig, i32 cx, i32 cy) {
    fl_app_inject_mouse_move(rig->app, cx, cy);
    dock_settle_hover(rig->app, rig->st, rig->frame);
    fl_app_inject_mouse_button(rig->app, FlMouseButton_Left, true);
    fl_app_tick(rig->app, rig->st, rig->frame);
}

void dock_move_and_tick(const DockRig* rig, i32 x, i32 y, int n) {
    fl_app_inject_mouse_move(rig->app, x, y);
    dock_ticks(rig->app, rig->st, rig->frame, n);
}

void dock_release(const DockRig* rig) {
    fl_app_inject_mouse_button(rig->app, FlMouseButton_Left, false);
    fl_app_tick(rig->app, rig->st, rig->frame);
}

Clay_ElementData dock_element(u32 clay_id) {
    return Clay_GetElementData((Clay_ElementId) { .id = clay_id });
}

Clay_ElementData dock_layout_element(FlLayoutId id) {
    return dock_element(ui_layout_id_to_clay_id(id));
}

// Name "fl_dock_tab", instance id = leaf_id << 8 folded with the index by ui_item_id_seed (see dock_layout.c and
// ui_tab_strip).
u32 dock_tab_clay_id(FlDockNodeId leaf_id, u32 index) {
    return ui_layout_id_to_clay_id(
        (FlLayoutId) { .name = FL_S("fl_dock_tab"), .id = ui_item_id_seed((u32)leaf_id << 8, index) });
}

DockPoint dock_item_centre(const ItemState* item) {
    return (DockPoint) { (i32)(item->state.pos.x + item->state.size.x * 0.5f),
                         (i32)(item->state.pos.y + item->state.size.y * 0.5f) };
}

DockPoint dock_box_centre(Clay_BoundingBox box) {
    return (DockPoint) { (i32)(box.x + box.width * 0.5f), (i32)(box.y + box.height * 0.5f) };
}

bool dock_tab_centre(const DockFixture* fx, FlDockNodeId leaf_id, u32 index, DockPoint* out) {
    const ItemState* tab = hashmap_get(&fx->ctx->shared->item_state, dock_tab_clay_id(leaf_id, index));
    if (tab == nullptr || tab->state.size.x <= 0.0f) {
        return false;
    }
    *out = dock_item_centre(tab);
    return true;
}

bool dock_press_tab(const DockRig* rig, const DockFixture* fx, FlDockNodeId leaf_id, u32 index) {
    DockPoint centre = { 0, 0 };
    if (!dock_tab_centre(fx, leaf_id, index, &centre)) {
        return false;
    }
    dock_press_at(rig, centre.x, centre.y);
    return true;
}

void dock_tab_header_frame(void* user_data) {
    FlDockWorkspace* ws = dock_fixture_emit((DockFixture*)user_data, dock_leaf(100, 101));
    dock_workspace_end(ws);
}

void dock_nest_panel(FlDockPanelId id) {
    dock_panel(id, FL_S("nest")) {
        // Coloured so the marker emits a rectangle command that frame_has_rect can find.
        ui_layout_area_id(((FlLayoutId) { .name = FL_S("nest_marker"), .id = (u32)id }),
                          .sizing = { .width = sizing_grow(), .height = sizing_grow() },
                          .color = ui_color(40, 40, 40, 255)) {}
    }
}

bool dock_size_is_zero(FlVec2 size) {
    return fabsf(size.x) < 1e-4f && fabsf(size.y) < 1e-4f;
}

f32 dock_node_width(FlDockNodeId node) {
    Clay_ElementData data = dock_element(dock_layout_node_element_id(node));
    return data.found ? data.boundingBox.width : -1.0f;
}

static bool dock_text_cmd_matches(const UiRenderCommand* cmd, FlString want, int z) {
    if (cmd->type != UiRenderCmd_Text) {
        return false;
    }
    if (z != DOCK_ANY_Z && (int)cmd->z_index != z) {
        return false;
    }
    return fl_string_equals(cmd->text, want);
}

bool frame_has_text_at_z(FlString want, int z) {
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (ui_get_render_command(i, &cmd) && dock_text_cmd_matches(&cmd, want, z)) {
            return true;
        }
    }
    return false;
}

bool frame_has_text(FlString want) {
    return frame_has_text_at_z(want, DOCK_ANY_Z);
}

bool frame_has_rect(FlLayoutId id) {
    u32 want = ui_layout_id_to_clay_id(id);
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; ++i) {
        UiRenderCommand cmd;
        if (!ui_get_render_command(i, &cmd)) {
            continue;
        }
        if (cmd.type == UiRenderCmd_Rectangle && cmd.id == want) {
            return true;
        }
    }
    return false;
}

DockDefaultTree dock_default_tree_swap(FlUiCtx* fixture, const FlDockSpec* spec) {
    ui_make_current(fixture);
    DockDefaultTree scope = { .ws = dock_shared_default(fixture->shared->dock) };
    scope.saved = dock_tree_clone(scope.ws->tree);
    dock_workspace_set_tree(scope.ws, dock_tree_build(spec));
    return scope;
}

void dock_default_tree_restore(DockDefaultTree* scope) {
    dock_workspace_set_tree(scope->ws, scope->saved);
}
