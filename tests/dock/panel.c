// Dock panel bodies: size reports, placement and tab selection inside a tile.

#include "dock/common.h"

static void dock_nest_frame(void* user_data) {
    // Panel 100 in the left half and 101 in the right, an even split so the boundary is the midpoint.
    FlDockWorkspace* ws
        = dock_fixture_emit((DockFixture*)user_data, dock_split(FlDockAxis_X, 0.5f, dock_leaf(100), dock_leaf(101)));
    // Declared after the tiles and in one flat sequence, and each still lands in its own half.
    dock_nest_panel(100);
    dock_nest_panel(101);
    dock_workspace_end(ws);
}

// A panel whose body wants more room than its tile. flowi clips the overflow so it does not paint over the
// neighbour, but the body's own element still measures at the size it laid out at. fl_dock_panel_size returns
// the tile's room, and the case checks the two differ.
typedef struct DockOversizeState {
    DockFixture fx;
    FlVec2 reported; // what fl_dock_panel_size answered inside the frame
} DockOversizeState;

static void dock_oversize_frame(void* user_data) {
    DockOversizeState* st = (DockOversizeState*)user_data;
    // Panel 100 in the top half and 101 in the bottom. 101 is the neighbour whose room 100 must not take.
    FlDockWorkspace* ws = dock_fixture_emit(&st->fx, dock_split(FlDockAxis_Y, 0.5f, dock_leaf(100), dock_leaf(101)));
    dock_panel(100, FL_S("oversize")) {
        // 900px of content in a tile barely 170px tall. The body grows to its content and the tile does not.
        ui_layout_area_id(((FlLayoutId) { .name = FL_S("oversize_body"), .id = 100u }),
                          .sizing = { .width = sizing_grow(), .height = sizing_fixed(900.0f) }) {}
    }
    st->reported = fl_dock_panel_size(100);
    dock_workspace_end(ws);
}

UTEST(dock_workspace, panel_size_reports_the_tile_not_the_overflow) {
    DOCK_APP(app, "dockroom", 600, 400);
    DockOversizeState st = { 0 };
    DOCK_RUN_FRAMES(app, st, st.fx, dock_oversize_frame,
                    3); // read-back is one frame behind, so settle before asserting

    // The panel's own element measures the full 900, because Clay never shrinks below the content minimum.
    Clay_ElementData body = dock_layout_element((FlLayoutId) { .name = FL_S("oversize_body"), .id = 100u });
    ASSERT_TRUE_MSG(body.found, "the oversized body laid out");
    ASSERT_TRUE_MSG(body.boundingBox.height > 800.0f, "the body grows to its content, overflowing its tile");

    const f32 reported = st.reported.y;
    ASSERT_TRUE_MSG(reported > 0.0f, "panel_size answers for a laid-out panel");
    ASSERT_TRUE_MSG(reported < body.boundingBox.height * 0.5f,
                    "panel_size must report the tile the dock gave the panel, not the size its body grew to");

    FlDockNode* neighbour = dock_tree_find_leaf_with_panel(st.fx.ws->tree, 101);
    ASSERT_TRUE_MSG(neighbour != nullptr, "the neighbour keeps its leaf");
    Clay_ElementData below = dock_element(dock_layout_content_element_id(neighbour->id));
    ASSERT_TRUE_MSG(below.found, "the neighbour's tile laid out");

    // The room the panel was told it has ends before the neighbour begins: tile_top + reported_height <=
    // neighbour_top. Comparing the height with the neighbour's y alone holds only when the tile starts at zero,
    // and every non-empty leaf has a tab header that pushes its content area down. Clay boxes are in physical px
    // and panel_size returns logical px, so the reported height is scaled back up before comparing.
    FlDockNode* own = dock_tree_find_leaf_with_panel(st.fx.ws->tree, 100);
    ASSERT_TRUE_MSG(own != nullptr, "the panel keeps its leaf");
    Clay_ElementData mine = dock_element(dock_layout_content_element_id(own->id));
    ASSERT_TRUE_MSG(mine.found, "the panel's own tile laid out");
    ASSERT_TRUE_MSG(mine.boundingBox.y > 0.0f, "the tile starts below its leaf's tab header, not at the origin");
    ASSERT_TRUE_MSG(mine.boundingBox.y + reported * ui_get_scale() <= below.boundingBox.y + 0.5f,
                    "the room reported to the panel must stop before the panel below it starts");
}

// Clipping hides the overflow, so the dock reports it, once per panel.
UTEST(dock_workspace, panel_body_overflow_is_reported_once) {
    DOCK_APP(app, "dockspill", 600, 400);
    DockOversizeState st = { 0 };
    DOCK_RUN_FRAMES(app, st, st.fx, dock_oversize_frame, 8);
    ASSERT_EQ_MSG((int)dock_workspace_panel_overflow_logs(st.fx.ws, 100), 1,
                  "the first overflowing frame reports, every later frame stays silent");
}

// A placed child is positioned by its offset and takes no part in the flow, so it does not size the body either.
// One far wider than its tile leaves the body at the tile's size and takes no share from its flowed siblings.
#define DOCK_PLACED_ID(lit) ((FlLayoutId) { .name = FL_S(lit), .id = 100u })

static void dock_placed_frame(void* user_data) {
    DockFixture* fx = (DockFixture*)user_data;
    FlDockWorkspace* ws = dock_fixture_emit(fx, dock_split(FlDockAxis_X, 0.5f, dock_leaf(100), dock_leaf(101)));
    dock_panel(100, FL_S("placed")) {
        ui_layout_area_id(DOCK_PLACED_ID("placed_row"), .sizing = { sizing_grow(), sizing_grow() }) {
            ui_layout_area_id(DOCK_PLACED_ID("placed_fill"), .sizing = { sizing_grow(), sizing_grow() }) {}
            ui_layout_area_id(DOCK_PLACED_ID("placed_wide"), .sizing = { sizing_fixed(2000.0f), sizing_grow() },
                              .placed = { .enabled = true, .offset = { 50.0f, 0.0f } }) {}
        }
    }
    dock_panel(101, FL_S("beside")) {}
    dock_workspace_end(ws);
}

UTEST(dock_workspace, a_placed_child_does_not_size_the_panel_body) {
    DOCK_APP(app, "dockplaced", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_placed_frame, 4);

    FlDockNode* leaf = dock_tree_find_leaf_with_panel(st.ws->tree, 100);
    ASSERT_TRUE_MSG(leaf != nullptr, "the panel keeps its leaf");
    Clay_ElementData tile = dock_element(dock_layout_content_element_id(leaf->id));
    Clay_ElementData row = dock_layout_element(DOCK_PLACED_ID("placed_row"));
    Clay_ElementData fill = dock_layout_element(DOCK_PLACED_ID("placed_fill"));
    Clay_ElementData wide = dock_layout_element(DOCK_PLACED_ID("placed_wide"));
    ASSERT_TRUE_MSG(tile.found && row.found && fill.found && wide.found, "the tile and the body must lay out");

    const f32 tile_width = tile.boundingBox.width;
    ASSERT_LT_MSG(tile_width, 2000.0f, "the placed child must be wider than the tile to test anything");
    ASSERT_NEAR_MSG(row.boundingBox.width, tile_width, 0.5f, "the body stays at its tile's width");
    ASSERT_NEAR_MSG(fill.boundingBox.width, tile_width, 0.5f,
                    "the flowed sibling takes the whole row - the placed child claims no share of it");
    const f32 row_height = row.boundingBox.height;
    ASSERT_NEAR_MSG(wide.boundingBox.width, 2000.0f, 0.5f, "the placed child keeps its own size");
    ASSERT_NEAR_MSG(wide.boundingBox.height, row_height, 0.5f, "a grow placed child fills its parent");
    ASSERT_EQ_MSG((int)dock_workspace_panel_overflow_logs(st.ws, 100), 0, "nothing overflowed, so nothing reports");
}

// fl_dock_panel_size returns {0,0} when the caller should not act on the size: the panel is absent, not laid out
// yet, hidden, mid-drag, or a background tab, which has no room in its leaf's tile.
typedef struct DockSizeContractState {
    DockFixture fx;
    FlVec2 shown;      // the tab its leaf shows
    FlVec2 background; // the other tab in the same leaf
    FlVec2 neighbour;  // the sibling leaf's panel, hidden partway through the test
    FlVec2 absent;     // never in the tree
} DockSizeContractState;

static void dock_size_contract_frame(void* user_data) {
    DockSizeContractState* st = (DockSizeContractState*)user_data;
    FlDockWorkspace* ws
        = dock_fixture_emit(&st->fx, dock_split(FlDockAxis_X, 0.5f, dock_leaf(400, 401), dock_leaf(402)));
    dock_panel(400, FL_S("Front")) {}
    dock_panel(401, FL_S("Back")) {}
    dock_panel(402, FL_S("Side")) {}
    st->shown = fl_dock_panel_size(400);
    st->background = fl_dock_panel_size(401);
    st->neighbour = fl_dock_panel_size(402);
    st->absent = fl_dock_panel_size(999);
    dock_workspace_end(ws);
}

// Asserts a reported size is either a real room, both sides above zero, or the {0,0} answer.
static void dock_expect_room(int* utest_result, FlVec2 size, bool has_room, const char* msg) {
    if (has_room) {
        ASSERT_TRUE_MSG(size.x > 0.0f && size.y > 0.0f, msg);
        return;
    }
    ASSERT_TRUE_MSG(dock_size_is_zero(size), msg);
}

UTEST(dock_workspace, panel_size_answers_zero_for_the_do_not_act_family) {
    DOCK_APP(app, "dockzero", 800, 400);
    DockSizeContractState st = { 0 };
    fl_app_tick(app, &st, dock_size_contract_frame); // the tree is installed during this frame, so nothing has a rect
    ASSERT_TRUE_MSG(dock_size_is_zero(st.shown), "a panel not laid out yet reports {0,0}");

    DOCK_RUN_FRAMES(app, st, st.fx, dock_size_contract_frame,
                    2); // read-back is one frame behind, so settle before asserting
    dock_expect_room(utest_result, st.shown, true, "the tab its leaf shows reports the tile it was given");
    dock_expect_room(utest_result, st.background, false,
                     "a background tab has no room of its own — its leaf's tile belongs to the tab in front");
    dock_expect_room(utest_result, st.absent, false, "a panel absent from the tree reports {0,0}");
    dock_expect_room(utest_result, st.neighbour, true, "the sibling leaf's panel starts out with room");

    // A hidden panel reports {0,0} from the first frame it is hidden.
    dock_workspace_set_panel_visible(st.fx.ws, 402, false);
    for (int i = 0; i < 4; ++i) {
        fl_app_tick(app, &st, dock_size_contract_frame);
        ASSERT_TRUE_MSG(dock_size_is_zero(st.neighbour), "a hidden panel reports {0,0}");
    }

    dock_workspace_set_panel_visible(st.fx.ws, 402, true);
    dock_ticks(app, &st, dock_size_contract_frame, 2);
    dock_expect_room(utest_result, st.neighbour, true, "showing it back gives it its room again");

    ASSERT_EQ_MSG((int)dock_workspace_panel_overflow_logs(st.fx.ws, 400), 0, "an empty body never overflows");
}

// A tile squeezed under the minimum content size also reports {0,0}, so an app does no work for room it does not
// have. The leaf still tiles. The workspace overflows its host and is clipped.
typedef struct DockStarvedState {
    DockFixture fx;
    FlVec2 left;
    FlVec2 right;
} DockStarvedState;

static void dock_starved_frame(void* user_data) {
    DockStarvedState* st = (DockStarvedState*)user_data;
    FlDockWorkspace* ws = dock_fixture_emit(&st->fx, dock_split(FlDockAxis_X, 0.5f, dock_leaf(500), dock_leaf(501)));
    dock_panel(500, FL_S("Left")) {}
    dock_panel(501, FL_S("Right")) {}
    st->left = fl_dock_panel_size(500);
    st->right = fl_dock_panel_size(501);
    dock_workspace_end(ws);
}

UTEST(dock_workspace, a_starved_panel_reports_zero) {
    // 140px across two tiles and a divider puts both well under the 80px minimum content width.
    DOCK_APP(app, "dockstarve", 140, 300);

    DockStarvedState st = { 0 };
    DOCK_RUN_FRAMES(app, st, st.fx, dock_starved_frame, 5);

    ASSERT_TRUE_MSG(dock_size_is_zero(st.left), "a starved panel reports {0,0}");
    ASSERT_TRUE_MSG(dock_size_is_zero(st.right), "and so does its equally starved sibling");

    // Both leaves still tile, at the same size.
    FlDockNode* s = st.fx.ws->tree->root;
    ASSERT_EQ_MSG((int)s->kind, (int)FlDockNodeKind_Split, "root must be the X split");
    ASSERT_TRUE_MSG(s->split.starve_logged, "the split reported the starvation");
    Clay_ElementData a = dock_element(dock_layout_node_element_id(s->split.child[0]->id));
    Clay_ElementData b = dock_element(dock_layout_node_element_id(s->split.child[1]->id));
    ASSERT_TRUE_MSG(a.found && b.found && a.boundingBox.width > 0.0f && b.boundingBox.width > 0.0f,
                    "no leaf drops out of the layout below the floor");
    ASSERT_NEAR_MSG(a.boundingBox.width, b.boundingBox.width, 1.0f, "both tiles degrade symmetrically");
}

UTEST(dock_workspace, panel_content_lands_in_its_tile) {
    DOCK_APP(app, "docknest", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_nest_frame, 2); // frame 1 installs the tree, frame 2 lays it out settled

    Clay_ElementData left = dock_layout_element((FlLayoutId) { .name = FL_S("nest_marker"), .id = 100 });
    Clay_ElementData right = dock_layout_element((FlLayoutId) { .name = FL_S("nest_marker"), .id = 101 });
    ASSERT_TRUE_MSG(left.found && right.found, "both panels must lay out");
    ASSERT_TRUE_MSG(left.boundingBox.width > 0.0f && right.boundingBox.width > 0.0f,
                    "a panel attached to its tile inherits the tile's size, never zero");

    f32 left_end = left.boundingBox.x + left.boundingBox.width;
    ASSERT_LE_MSG(left_end, right.boundingBox.x + 1.0f, "panel 100 must sit entirely left of panel 101");
    ASSERT_LT_MSG(left.boundingBox.x, right.boundingBox.x, "the two panels must not share an origin");
}

// Two panel ids that share their low 32 bits. Each body still gets an element of its own and lands in its own tile.
#define DOCK_WIDE_ID_LOW 100ull
#define DOCK_WIDE_ID_HIGH ((1ull << 32) | DOCK_WIDE_ID_LOW)

static void dock_wide_id_panel(FlDockPanelId id, u32 marker) {
    dock_panel(id, FL_S("wide")) {
        ui_layout_area_id(((FlLayoutId) { .name = FL_S("wide_marker"), .id = marker }),
                          .sizing = { .width = sizing_grow(), .height = sizing_grow() },
                          .color = ui_color(40, 40, 40, 255)) {}
    }
}

static void dock_wide_id_frame(void* user_data) {
    FlDockWorkspace* ws
        = dock_fixture_emit((DockFixture*)user_data,
                            dock_split(FlDockAxis_X, 0.5f, dock_leaf(DOCK_WIDE_ID_LOW), dock_leaf(DOCK_WIDE_ID_HIGH)));
    dock_wide_id_panel(DOCK_WIDE_ID_LOW, 1);
    dock_wide_id_panel(DOCK_WIDE_ID_HIGH, 2);
    dock_workspace_end(ws);
}

UTEST(dock_workspace, panel_ids_sharing_their_low_32_bits_get_separate_tiles) {
    DOCK_APP(app, "dockwideid", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_wide_id_frame, 2);

    Clay_ElementData left = dock_layout_element((FlLayoutId) { .name = FL_S("wide_marker"), .id = 1 });
    Clay_ElementData right = dock_layout_element((FlLayoutId) { .name = FL_S("wide_marker"), .id = 2 });
    ASSERT_TRUE_MSG(left.found && right.found, "both panels must lay out");
    const f32 left_end = left.boundingBox.x + left.boundingBox.width;
    ASSERT_NEAR_MSG(left.boundingBox.width, 300.0f, 8.0f, "the low id's body fills its own half");
    ASSERT_NEAR_MSG(right.boundingBox.width, 300.0f, 8.0f, "and so does the high id's, less the divider");
    ASSERT_LE_MSG(left_end, right.boundingBox.x + 1.0f, "the two bodies must not overlap");
}

UTEST(dock_workspace, tab_header_selection_changes_emitting_panel) {
    DOCK_APP(app, "docktabs", 600, 400);
    DockFixture st = { 0 };

    DOCK_RUN_FRAMES(app, st, st, dock_tab_header_frame, 1);
    ASSERT_TRUE_MSG(dock_workspace_panel_should_emit(st.ws, 100), "panel 100 must start emitting (selected tab)");
    ASSERT_FALSE_MSG(dock_workspace_panel_should_emit(st.ws, 101), "panel 101 is a background tab — it must not emit");

    DockPoint tab = { 0, 0 };
    ASSERT_TRUE_MSG(dock_tab_centre(&st, st.ws->tree->root->id, 1u, &tab), "second dock tab was not laid out");

    // Hover the second tab so it goes hot, then press. Tabs activate on press, which selects panel 101 through
    // the dock-as-tab op.
    fl_app_inject_mouse_move(app, tab.x, tab.y);
    dock_settle_hover(app, &st, dock_tab_header_frame);
    ASSERT_TRUE_MSG(dock_workspace_panel_should_emit(st.ws, 100), "hovering a tab must not change the selection");

    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, &st, dock_tab_header_frame);
    ASSERT_TRUE_MSG(dock_workspace_panel_should_emit(st.ws, 101), "clicking the second tab must make panel 101 emit");
    ASSERT_FALSE_MSG(dock_workspace_panel_should_emit(st.ws, 100), "panel 100 must stop emitting once 101 is selected");
    ASSERT_EQ_MSG((int)st.ws->tree->root->leaf.selected, 1, "leaf selected index must be 1");
}
