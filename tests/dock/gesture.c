// Dock tab drag gestures: undock, cancel, redock and the drag ghost.

#include "dock/common.h"

// A held press that moves less than FL_DOCK_DRAG_THRESHOLD does not undock. The probe travels past the tab
// widget's own 4px threshold, so the strip reports a grab, and stays inside the dock's.
UTEST(dock_workspace, drag_threshold_does_not_undock) {
    DOCK_APP(app, "dockarm", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_tab_header_frame, 1);
    FlDockTree* tree = st.ws->tree;

    DockPoint tab = { 0, 0 };
    ASSERT_TRUE_MSG(dock_tab_centre(&st, tree->root->id, 1u, &tab), "second dock tab was not laid out");
    dock_press_at(DOCK_RIG(app, &st, dock_tab_header_frame), tab.x, tab.y);

    // 4 across and 2 down is a squared distance of 20: past the widget's 16 and inside the dock's 25. Held for
    // several frames.
    dock_move_and_tick(DOCK_RIG(app, &st, dock_tab_header_frame), tab.x + 4, tab.y + 2, 3);
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 101) == tree->root, "a sub-threshold press never detaches");
    ASSERT_EQ_MSG((int)tree->floating_count, 0, "and nothing reaches the holding pen");
    ASSERT_EQ_MSG((int)tree->root->leaf.selected, 1, "the press that stayed put selected the tab it landed on");

    // Once the same held press travels past the threshold, it tears the panel out.
    dock_move_and_tick(DOCK_RIG(app, &st, dock_tab_header_frame), tab.x + 40, tab.y + 20, 2);
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 101) == nullptr, "past the threshold the grab undocks");

    dock_release(DOCK_RIG(app, &st, dock_tab_header_frame));
    ASSERT_EQ_MSG((int)tree->floating_count, 0, "the release empties the holding pen again");
}

// A dock that fills the whole window has no outside to release over, so Escape is its only cancel. Escape drops
// the target the cursor is resolving without committing it, and the same held press does not arm again.
UTEST(dock_workspace, escape_cancels_and_restores) {
    DOCK_APP(app, "dockesc", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_tab_header_frame, 1);
    FlDockTree* tree = st.ws->tree;
    ASSERT_EQ_MSG((int)tree->root->kind, (int)FlDockNodeKind_Leaf, "the tree starts as one leaf holding {100, 101}");

    ASSERT_TRUE_MSG(dock_press_tab(DOCK_RIG(app, &st, dock_tab_header_frame), &st, tree->root->id, 1u),
                    "second dock tab was not laid out");

    // Hold it deep in the bottom of the workspace, a live drop zone where a release would split the root.
    dock_move_and_tick(DOCK_RIG(app, &st, dock_tab_header_frame), 300, 380, 3);
    ASSERT_TRUE_MSG(dock_tree_panel_is_floating(tree, 101), "the pull parks the panel in the holding pen");

    fl_app_inject_key(app, FlKey_Escape, true);
    fl_app_tick(app, &st, dock_tab_header_frame);
    fl_app_inject_key(app, FlKey_Escape, false);

    ASSERT_EQ_MSG((int)tree->root->kind, (int)FlDockNodeKind_Leaf,
                  "Escape drops the resolved target — a release at this cursor would have split the root");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 101) == dock_tree_find_leaf_with_panel(tree, 100),
                    "the panel returns to its committed slot");
    ASSERT_EQ_MSG((int)tree->floating_count, 0, "nothing is left in the holding pen");

    // The button is still down and the strip keeps reporting the grab, and the gesture stays cancelled.
    dock_move_and_tick(DOCK_RIG(app, &st, dock_tab_header_frame), 500, 380, 3);
    ASSERT_EQ_MSG((int)tree->root->kind, (int)FlDockNodeKind_Leaf,
                  "a cancelled gesture does not re-arm from the same press");
    ASSERT_EQ_MSG((int)tree->floating_count, 0, "and nothing drifts back into the holding pen");

    dock_release(DOCK_RIG(app, &st, dock_tab_header_frame));
    ASSERT_EQ_MSG((int)dock_tree_collect_panels(tree, nullptr, 0), 2, "both panels conserved across the cancel");
}

// The gesture fixture with bodies: each panel declares one marker element filling its leaf's content area, and a
// panel in flight declares no body.
static void dock_drag_body_frame(void* user_data) {
    FlDockWorkspace* ws = dock_fixture_emit((DockFixture*)user_data, dock_leaf(100, 101));
    dock_nest_panel(100);
    dock_nest_panel(101);
    dock_workspace_end(ws);
}

static FlLayoutId dock_overlay_fill_id(FlDockPanelId panel) {
    return (FlLayoutId) { .name = FL_S("fl_dock_drag_overlay_fill"), .id = (u32)panel };
}

// The ghost stands in for the held panel 101's body, while panel 100 keeps drawing.
static void dock_expect_ghost_instead_of_body(int* utest_result) {
    ASSERT_TRUE_MSG(frame_has_rect(dock_overlay_fill_id(101)), "and the ghost draws in its place");
    ASSERT_FALSE_MSG(frame_has_rect((FlLayoutId) { .name = FL_S("nest_marker"), .id = 101 }),
                     "the held panel's body does not draw — the ghost stands in for it");
    ASSERT_TRUE_MSG(frame_has_rect((FlLayoutId) { .name = FL_S("nest_marker"), .id = 100 }),
                    "while every other panel keeps drawing normally");
}

// Only the ghost under the cursor draws a dragged panel. The ghost is translucent, so the panel's own body draws
// nowhere for the whole drag. An app's panel background is opaque and would fill the ghost in.
UTEST(dock_workspace, dragged_panel_ghost_fills_the_gap_without_its_body) {
    DOCK_APP(app, "dockover", 600, 400);
    // Two frames before reading the strip. The bodies declare after the emit, so frame one lays the tabs out with
    // no titles yet and frame two is the first with settled tab widths.
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_drag_body_frame, 2);
    FlDockTree* tree = st.ws->tree;
    ASSERT_FALSE_MSG(frame_has_rect(dock_overlay_fill_id(101)), "no overlay outside a gesture");

    ASSERT_TRUE_MSG(dock_press_tab(DOCK_RIG(app, &st, dock_drag_body_frame), &st, tree->root->id, 1u),
                    "second dock tab was not laid out");

    dock_move_and_tick(DOCK_RIG(app, &st, dock_drag_body_frame), 300, 300,
                       4); // grab reported, undock + settle, snapshot, steady state
    ASSERT_TRUE_MSG(dock_tree_panel_is_floating(tree, 101), "the panel is in flight");

    // The body of the panel in flight draws nowhere, in the ghost or in the slot it left. Panel 100 is the
    // control: an ordinary docked body still draws, so an empty draw list would not pass this check.
    dock_expect_ghost_instead_of_body(utest_result);

    Clay_ElementData overlay = dock_layout_element(dock_overlay_fill_id(101));
    ASSERT_TRUE_MSG(overlay.found, "the ghost laid out");
    ASSERT_TRUE_MSG(overlay.boundingBox.width > 0.0f, "the ghost has room");

    // Over a resolved target the ghost is the gap. It attaches to that leaf's element, so the two are laid out in
    // the same pass and agree even on the frame a re-target creates the leaf.
    const FlDockDrag* drag = &st.ws->drag;
    ASSERT_NE_MSG((int)drag->zone, (int)FlDockDropZone_None, "the cursor is over a live target");
    ASSERT_NE_MSG((int)drag->gap_leaf, 0, "which opened a gap for the ghost to fill");
    Clay_ElementData gap = dock_element(dock_layout_node_element_id(drag->gap_leaf));
    ASSERT_TRUE_MSG(gap.found, "the gap leaf is tiled");
    dock_expect_box_on(utest_result, overlay.boundingBox, gap.boundingBox);

    // The overlay lasts only as long as the gesture: the release puts the panel back in a tile and removes it.
    dock_release(DOCK_RIG(app, &st, dock_drag_body_frame));
    fl_app_tick(app, &st, dock_drag_body_frame);
    ASSERT_FALSE_MSG(frame_has_rect(dock_overlay_fill_id(101)), "the overlay is gone once the gesture ends");
    DOCK_ASSERT_GESTURE_ENDED(tree);
}

// Per-frame record of whether panel_should_emit and the dock_panel scope agree.
typedef struct DockEmitAgreeState {
    DockFixture fx;
    int frames_checked;
    int frames_disagreed;
    int floating_frames;
    int floating_emitted;
    int floating_sized;
} DockEmitAgreeState;

// A floating panel neither reported emit nor drew its body, and had no room to report.
static void dock_expect_floating_silent(int* utest_result, const DockEmitAgreeState* st) {
    ASSERT_EQ_MSG(st->floating_emitted, 0, "a floating panel neither reports emit nor draws its body");
    ASSERT_EQ_MSG(st->floating_sized, 0, "a floating panel has no room to report");
}

static void dock_emit_agree_frame(void* user_data) {
    DockEmitAgreeState* st = (DockEmitAgreeState*)user_data;
    FlDockWorkspace* ws = dock_fixture_emit(&st->fx, dock_leaf(100, 101));
    for (FlDockPanelId id = 100; id <= 101; ++id) {
        bool floating = dock_tree_panel_is_floating(ws->tree, id);
        bool should_emit = dock_workspace_panel_should_emit(ws, id);
        bool drew = false;
        dock_panel(id, FL_S("nest")) {
            drew = true;
        }
        st->frames_checked++;
        st->frames_disagreed += should_emit != drew;
        st->floating_frames += floating;
        st->floating_emitted += floating && (should_emit || drew);
        FlVec2 size = dock_workspace_panel_size(ws, id);
        st->floating_sized += floating && !dock_size_is_zero(size);
    }
    dock_workspace_end(ws);
}

// panel_should_emit says whether the body is drawn. On every frame of a drag it agrees with whether the
// dock_panel scope opens, and for a floating panel both are false.
UTEST(dock_workspace, should_emit_matches_the_drawn_body_throughout_a_drag) {
    DOCK_APP(app, "dockagree", 600, 400);
    DockEmitAgreeState st = { 0 };
    DOCK_RUN_FRAMES(app, st, st.fx, dock_emit_agree_frame, 2);
    FlDockTree* tree = st.fx.ws->tree;

    ASSERT_TRUE_MSG(dock_press_tab(DOCK_RIG(app, &st, dock_emit_agree_frame), &st.fx, tree->root->id, 1u),
                    "second dock tab was not laid out");

    dock_move_and_tick(DOCK_RIG(app, &st, dock_emit_agree_frame), 300, 300, 6);
    ASSERT_TRUE_MSG(dock_tree_panel_is_floating(tree, 101), "the panel is in flight");
    ASSERT_GT_MSG(st.floating_frames, 0, "frames of the flight were observed");

    dock_release(DOCK_RIG(app, &st, dock_emit_agree_frame));
    fl_app_tick(app, &st, dock_emit_agree_frame);

    ASSERT_GT_MSG(st.frames_checked, st.floating_frames, "docked frames were checked as well");
    ASSERT_EQ_MSG(st.frames_disagreed, 0, "should_emit matched the dock_panel scope on every frame");
    dock_expect_floating_silent(utest_result, &st);
}

// Dragging a tab out of its leaf header undocks the panel into the internal floating set and arms drag-to-dock.
// The release commits the resolved drop. The whole gesture runs inside dock_workspace_emit.
UTEST(dock_workspace, tab_drag_out_undocks_then_redocks) {
    DOCK_APP(app, "dockgrab", 600, 400);
    DockFixture st = { 0 };

    DOCK_RUN_FRAMES(app, st, st, dock_tab_header_frame, 1);
    FlDockTree* tree = st.ws->tree;
    FlDockNodeId leaf_id = tree->root->id;

    ASSERT_TRUE_MSG(dock_press_tab(DOCK_RIG(app, &st, dock_tab_header_frame), &st, leaf_id, 1u),
                    "second dock tab was not laid out");

    // A press alone is only a selection. The tabs opt into FlItem_DragThreshold, so nothing detaches until the
    // cursor travels.
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 101) != nullptr, "a press must not detach the panel");
    ASSERT_EQ_MSG((int)tree->floating_count, 0, "a press under the threshold is not a drag");

    // Travel across the workspace, past the drag threshold and onto the tab band of the leaf the tab is being
    // pulled out of. The strip reports the grab during that frame's walk. The next frame turns it into the undock
    // at the start of the workspace, because a structural change cannot happen mid-walk, and arms the drag.
    dock_move_and_tick(DOCK_RIG(app, &st, dock_tab_header_frame), 400, 20, 2);
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 101) == nullptr, "the grabbed panel is undocked");
    ASSERT_EQ_MSG((int)tree->floating_count, 1, "the grabbed panel lands in the internal-floating set");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 100) != nullptr, "the panel left behind keeps its leaf");

    // One more frame so dock_drag_emit resolves the zone under the cursor, then release.
    fl_app_tick(app, &st, dock_tab_header_frame);
    dock_release(DOCK_RIG(app, &st, dock_tab_header_frame));
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 101) == dock_tree_find_leaf_with_panel(tree, 100),
                    "releasing over the tab band re-docks the panel as a tab of that leaf");
    DOCK_ASSERT_GESTURE_ENDED(tree);
}

// The strip reports a pulled tab one frame before the workspace acts on it. A panel that left the tree in between,
// as a promote takes it, is no longer this workspace's to drag.
UTEST(dock_workspace, a_grab_on_a_panel_that_has_left_arms_no_drag) {
    DOCK_APP(app, "dockgrabgone", 600, 400);
    DockFixture st = { 0 };

    DOCK_RUN_FRAMES(app, st, st, dock_tab_header_frame, 1);
    FlDockTree* tree = st.ws->tree;
    ASSERT_TRUE_MSG(dock_press_tab(DOCK_RIG(app, &st, dock_tab_header_frame), &st, tree->root->id, 1u),
                    "second dock tab was not laid out");
    dock_move_and_tick(DOCK_RIG(app, &st, dock_tab_header_frame), 400, 20, 1);
    ASSERT_EQ_MSG((int)st.ws->pending_grab, 101, "the strip reported the pull");

    FlDockRequest close = { .kind = FlDockRequest_Close, .panel = 101 };
    ASSERT_TRUE(dock_tree_apply(tree, &close));
    fl_app_tick(app, &st, dock_tab_header_frame);

    ASSERT_FALSE_MSG(dock_drag_active(&st.ws->drag), "no drag starts on a panel the workspace no longer holds");
    dock_release(DOCK_RIG(app, &st, dock_tab_header_frame));
    ASSERT_FALSE_MSG(dock_tree_has_panel(tree, 101), "and the release docks no second copy of it");
}

// Drop resolution works in logical px: a 1200x800 window at zoom 2 is a 600x400 logical tile. The release lands
// Bottom in that tile. Dividing the cursor but not the snapshot rects, or the reverse, resolves Left.
UTEST(dock_workspace, drop_resolution_is_logical_px_under_zoom) {
    DOCK_APP(app, "dockzoom", 1200, 800);
    fl_app_set_ui_zoom(app, 2.0f);

    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_tab_header_frame, 1);
    ASSERT_NEAR_MSG(ui_get_scale(), 2.0f, 1e-4f, "the zoom composed into the UI scale");
    FlDockTree* tree = st.ws->tree;

    ASSERT_TRUE_MSG(dock_press_tab(DOCK_RIG(app, &st, dock_tab_header_frame), &st, tree->root->id, 1u),
                    "second dock tab was not laid out");

    // Physical (600, 760) is logical (300, 380): centred horizontally and 95% of the way down.
    dock_move_and_tick(DOCK_RIG(app, &st, dock_tab_header_frame), 600, 760, 2);
    ASSERT_TRUE_MSG(dock_tree_panel_is_floating(tree, 101), "pulling a tab out must arm the drag on that panel");

    fl_app_tick(app, &st, dock_tab_header_frame);
    dock_release(DOCK_RIG(app, &st, dock_tab_header_frame));

    ASSERT_EQ_MSG((int)tree->root->kind, (int)FlDockNodeKind_Split, "the bottom quadrant splits the survivor");
    ASSERT_EQ_MSG((int)tree->root->split.axis, (int)FlDockAxis_Y, "a Bottom split is a Y split, not an X one");
    const FlDockNode* dropped = dock_tree_find_leaf_with_panel(tree, 101);
    ASSERT_TRUE_MSG(dropped == tree->root->split.child[1],
                    "101 lands BELOW the survivor, which is what the logical geometry resolves to");
    ASSERT_EQ_MSG((int)dock_tree_collect_panels(tree, nullptr, 0), 2, "both panels conserved across the gesture");
}

// Dragging out a leaf's only tab. The drop targets the geometry the user sees after the detach: the drag's settle
// frame lets the remaining tiles lay out and snapshots those. The release below lands right of the workspace
// centre, which is inside the survivor's tab band once it has reflowed to fill the workspace and outside the
// left tile as it was before the detach.
static void dock_grab_split_frame(void* user_data) {
    // Two one-panel leaves side by side, {200} | {201}, each with a single-tab header that is its grab handle.
    FlDockWorkspace* ws
        = dock_fixture_emit((DockFixture*)user_data, dock_split(FlDockAxis_X, 0.5f, dock_leaf(200), dock_leaf(201)));
    dock_workspace_end(ws);
}

UTEST(dock_workspace, single_tab_leaf_drag_out_collapses_and_redocks) {
    DOCK_APP(app, "dockgrab1", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_grab_split_frame, 1);
    FlDockTree* tree = st.ws->tree;
    ASSERT_EQ_MSG((int)tree->root->kind, (int)FlDockNodeKind_Split, "the tree starts as an X split of two leaves");
    FlDockNodeId left_leaf = tree->root->split.child[0]->id;

    ASSERT_TRUE_MSG(dock_press_tab(DOCK_RIG(app, &st, dock_grab_split_frame), &st, left_leaf, 0u),
                    "a single-panel leaf must still render a tab");
    dock_move_and_tick(DOCK_RIG(app, &st, dock_grab_split_frame), 450, 20, 2); // the header reports the grab

    ASSERT_TRUE_MSG(dock_tree_panel_is_floating(tree, 200), "the panel is being dragged");
    ASSERT_EQ_MSG((int)tree->root->kind, (int)FlDockNodeKind_Leaf,
                  "emptying a leaf collapses it and dissolves the split — the survivor becomes the root");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 201) == tree->root, "201's leaf is the survivor");

    // The snapshot frame reads back the reflowed tiles. Then release over the survivor's tab band.
    fl_app_tick(app, &st, dock_grab_split_frame);
    dock_release(DOCK_RIG(app, &st, dock_grab_split_frame));

    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 200) != nullptr,
                    "the drop must resolve against the REFLOWED survivor, not the collapsed pre-detach rects");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 200) == dock_tree_find_leaf_with_panel(tree, 201),
                    "released on the survivor's tab band, so 200 re-docks as a tab of it");
    DOCK_ASSERT_GESTURE_ENDED(tree);
}

// Releasing outside the workspace also cancels. It takes the same path as Escape and returns the panel to where
// it came from. Pulling a leaf's only tab collapses that leaf and dissolves its split, so the original place is
// gone by release and the panel is docked on the floor.
UTEST(dock_workspace, release_outside_workspace_cancels) {
    DOCK_APP(app, "dockcancel", 600, 400);
    // Two single-tab leaves side by side, {200} | {201}. Dragging 200 out collapses its leaf.
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_grab_split_frame, 1);
    FlDockTree* tree = st.ws->tree;

    FlDockNode* origin = dock_tree_find_leaf_with_panel(tree, 200);
    ASSERT_TRUE_MSG(origin != nullptr, "200 starts docked");
    ASSERT_TRUE_MSG(dock_press_tab(DOCK_RIG(app, &st, dock_grab_split_frame), &st, origin->id, 0u),
                    "200's tab was not laid out");

    // Pull far outside the workspace, past the drag threshold. Outside is the only place a drop misses.
    dock_move_and_tick(DOCK_RIG(app, &st, dock_grab_split_frame), 5000, 5000, 3);
    ASSERT_TRUE_MSG(dock_tree_panel_is_floating(tree, 200), "the pull arms the drag");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 200) == nullptr, "and the grab undocked it");

    dock_release(DOCK_RIG(app, &st, dock_grab_split_frame));

    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 200) != nullptr,
                    "a release over no zone must seat the panel, not strand it in the floating set");
    DOCK_ASSERT_GESTURE_ENDED(tree);
}

// The same cancel when the origin leaf holds two tabs, so pulling one leaves the leaf standing. The release lands
// outside the workspace and the panel goes back into the leaf it came from.
UTEST(dock_workspace, release_outside_workspace_rejoins_the_surviving_leaf) {
    DOCK_APP(app, "dockcancel2", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_tab_header_frame, 1);
    FlDockTree* tree = st.ws->tree;
    FlDockNode* origin = tree->root;
    ASSERT_EQ_MSG((int)origin->kind, (int)FlDockNodeKind_Leaf, "the tree starts as one leaf holding {100, 101}");
    ASSERT_EQ_MSG((int)origin->leaf.count, 2, "the origin leaf holds two tabs");

    ASSERT_TRUE_MSG(dock_press_tab(DOCK_RIG(app, &st, dock_tab_header_frame), &st, origin->id, 1u),
                    "101's tab was not laid out");

    // Pull far outside the workspace, past the drag threshold. Outside is the only place a drop misses.
    dock_move_and_tick(DOCK_RIG(app, &st, dock_tab_header_frame), 5000, 5000, 3);
    ASSERT_TRUE_MSG(dock_tree_panel_is_floating(tree, 101), "the pull arms the drag");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 101) == nullptr, "and the grab undocked it");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 100) == tree->root,
                    "the origin leaf keeps its other tab, so it survives the pull");

    dock_release(DOCK_RIG(app, &st, dock_tab_header_frame));

    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 101) == dock_tree_find_leaf_with_panel(tree, 100),
                    "an aimless drop returns the panel to the leaf it was pulled from");
    DOCK_ASSERT_GESTURE_ENDED(tree);
}
