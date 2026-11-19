// Dock make-space: the gap a drag opens and the targets it resolves.

#include "dock/common.h"

// Make-space. The hit test runs on the live provisional layout, so the gap the drag opens is part of the geometry
// that resolves the next frame's target. These cases run the real loop because that feedback exists only once
// Clay has laid the gap out.

// Pulls the root leaf's second tab (101) out into a live drag and leaves the cursor at (cx, cy) with the button
// still down. False when the frame did not run, the tab was not laid out, or the pull did not arm a drag.
static bool dock_start_drag(FlApp* app, DockFixture* st, i32 cx, i32 cy) {
    fl_app_tick(app, st, dock_tab_header_frame);
    if (st->ctx == nullptr || st->ws == nullptr) {
        return false;
    }
    FlDockTree* tree = st->ws->tree;

    if (!dock_press_tab(DOCK_RIG(app, st, dock_tab_header_frame), st, tree->root->id, 1u)) {
        return false;
    }

    dock_move_and_tick(DOCK_RIG(app, st, dock_tab_header_frame), cx, cy, 4);
    return dock_drag_active(&st->ws->drag);
}

// Sweeping the cursor across the workspace resolves targets monotonically: a zone the sweep has left does not
// come back. A target that came back would mean the gap had moved the geometry that chose it.
UTEST(dock_workspace, drag_sweep_resolves_targets_monotonically) {
    DOCK_APP(app, "docksweep", 600, 400);
    DockFixture st = { 0 };
    ASSERT_TRUE_MSG(dock_start_drag(app, &st, 10, 300), "the pull did not arm a drag");
    const FlDockDrag* drag = &st.ws->drag;

    bool seen[FlDockDropZone_OuterBottom + 1] = { false };
    int distinct = 0;
    int held = (int)drag->zone;
    seen[held] = true;
    distinct++;

    // 20px steps clear the 8px switch hysteresis, so a repeated zone means the layout flickered and not that the
    // cursor sat on a boundary. Both ticks per step are sampled, so a flick of one frame counts.
    for (i32 x = 10; x <= 590; x += 20) {
        fl_app_inject_mouse_move(app, x, 300);
        for (int t = 0; t < 2; ++t) {
            fl_app_tick(app, &st, dock_tab_header_frame);
            int zone = (int)drag->zone;
            if (zone == held) {
                continue;
            }
            ASSERT_FALSE_MSG(seen[zone], "the sweep resolved back to a zone it had already left");
            seen[zone] = true;
            distinct++;
            held = zone;
        }
    }

    ASSERT_TRUE_MSG(distinct >= 3, "a sweep of the whole workspace must move the target, not freeze on one zone");
    ASSERT_NE_MSG(held, (int)FlDockDropZone_None, "and it ends over a live target, not off the workspace");

    dock_release(DOCK_RIG(app, &st, dock_tab_header_frame));
}

// How many times a sweep has moved the gap to a new leaf, and the leaf it held last.
typedef struct GapSweep {
    int retargets;
    FlDockNodeId last_gap;
} GapSweep;

// After a frame: counts a new gap leaf as a re-target and checks the overlay sits on the gap, when both laid out.
static void dock_check_overlay_on_gap(int* utest_result, const FlDockDrag* drag, GapSweep* sweep) {
    if (drag->gap_leaf == 0) {
        return;
    }
    if (drag->gap_leaf != sweep->last_gap) {
        sweep->retargets++;
        sweep->last_gap = drag->gap_leaf;
    }
    Clay_ElementData gap = dock_element(dock_layout_node_element_id(drag->gap_leaf));
    Clay_ElementData overlay
        = dock_layout_element((FlLayoutId) { .name = FL_S("fl_dock_drag_overlay_fill"), .id = (u32)drag->panel });
    if (gap.found && overlay.found) {
        dock_expect_box_on(utest_result, overlay.boundingBox, gap.boundingBox);
    }
}

// The drag overlay has the rect of its gap leaf on every frame of a sweep, including the frames where a re-target
// creates a new gap leaf.
UTEST(dock_workspace, overlay_never_lags_the_gap_across_a_retarget) {
    DOCK_APP(app, "docklag", 600, 400);
    DockFixture st = { 0 };
    ASSERT_TRUE_MSG(dock_start_drag(app, &st, 400, 20), "the pull did not arm a drag");
    const FlDockDrag* drag = &st.ws->drag;

    // A sweep that re-targets repeatedly. Each move creates a new gap leaf.
    static const struct {
        i32 x, y;
    } sweep[] = { { 100, 300 }, { 300, 380 }, { 550, 200 }, { 100, 300 }, { 300, 40 }, { 550, 200 } };

    GapSweep gaps = { 0, 0 };
    for (u32 p = 0; p < sizeof_array(sweep); ++p) {
        fl_app_inject_mouse_move(app, sweep[p].x, sweep[p].y);
        for (int i = 0; i < 4; ++i) {
            fl_app_tick(app, &st, dock_tab_header_frame);
            dock_check_overlay_on_gap(utest_result, drag, &gaps);
            DOCK_STOP_ON_FAILURE();
        }
    }
    ASSERT_TRUE_MSG(gaps.retargets >= 3, "the sweep really did re-target, so the risky frames were exercised");

    dock_release(DOCK_RIG(app, &st, dock_tab_header_frame));
}

// The node a gap took its space from keeps hit-testing at the rect it held before the gap opened.
//
// The probe is a cursor position the two footprints disagree about. In a 600-wide leaf the left quadrant reaches
// x=150. Once a left gap has taken 35%, the surviving tile is [210,600] and its left quadrant reaches x=307. So
// x=250 is Bottom against the frozen rect and Left against the live one, and it is outside the gap.
UTEST(dock_workspace, pre_gap_footprint_freezes_the_boundary_node) {
    DOCK_APP(app, "dockpregap", 600, 400);
    DockFixture st = { 0 };
    ASSERT_TRUE_MSG(dock_start_drag(app, &st, 100, 300), "the pull did not arm a drag");
    const FlDockDrag* drag = &st.ws->drag;
    ASSERT_EQ_MSG((int)drag->zone, (int)FlDockDropZone_Left, "100px into a 600px-wide leaf is its left quadrant");

    // 101 was pulled out of the root leaf, which still holds 100. That leaf is the node the gap splits.
    FlDockNodeId survivor = st.ws->tree->root->id;
    ASSERT_EQ_MSG((int)drag->pre_gap_node, (int)survivor, "the gap's sibling is the node whose footprint is frozen");
    ASSERT_TRUE_MSG(drag->has_pre_gap, "and the drag stored it");

    dock_ticks(app, &st, dock_tab_header_frame, 30);

    // The tile has shrunk, which the probe below depends on.
    const f32 live_w = dock_node_width(survivor);
    ASSERT_TRUE_MSG(live_w > 0.0f && live_w < 0.8f * 600.0f, "the open gap has taken its share of the tile");
    ASSERT_NEAR_MSG(drag->pre_gap.width, 600.0f, 1.0f, "yet the frozen footprint is still the whole workspace");

    dock_move_and_tick(DOCK_RIG(app, &st, dock_tab_header_frame), 250, 300, 2);
    ASSERT_EQ_MSG((int)drag->zone, (int)FlDockDropZone_Bottom,
                  "the quadrant resolves against the pre-gap rect; the shrunken tile would still read Left here");
    ASSERT_EQ((int)drag->request.target_node, (int)survivor);

    dock_release(DOCK_RIG(app, &st, dock_tab_header_frame));
}

// A cursor that does not move gets a gap that does not move. The gap is part of the geometry the next frame
// resolves against, so this checks that the loop converges.
UTEST(dock_workspace, gap_settles_under_a_held_cursor) {
    DOCK_APP(app, "dockgapsettle", 600, 400);
    DockFixture st = { 0 };
    ASSERT_TRUE_MSG(dock_start_drag(app, &st, 100, 300), "the pull did not arm a drag");
    const FlDockDrag* drag = &st.ws->drag;

    ASSERT_EQ_MSG((int)drag->zone, (int)FlDockDropZone_Left, "100px into a 600px-wide leaf is its left quadrant");
    FlDockNodeId gap = drag->gap_leaf;
    ASSERT_NE_MSG((int)gap, 0, "a resolved target opens a gap");

    for (int i = 0; i < 30; ++i) {
        fl_app_tick(app, &st, dock_tab_header_frame);
        ASSERT_EQ_MSG((int)drag->zone, (int)FlDockDropZone_Left, "the zone must not oscillate under a still cursor");
        ASSERT_EQ_MSG((int)drag->gap_leaf, (int)gap, "nor may the gap be torn down and reopened");
    }

    // The gap shows the share it will commit at.
    const FlDockNode* gap_node = dock_tree_find_node(drag->preview, gap);
    ASSERT_TRUE_MSG(gap_node != nullptr && gap_node->parent != nullptr, "the gap is a leaf inside a split");
    ASSERT_NEAR(gap_node->parent->split.ratio, FL_DOCK_GAP_RATIO, 1e-5f);

    dock_release(DOCK_RIG(app, &st, dock_tab_header_frame));
}

// Re-targeting mutates the retained preview: the same tree carries a new gap slot and is not rebuilt.
UTEST(dock_workspace, retarget_mutates_the_retained_preview) {
    DOCK_APP(app, "dockretarget", 600, 400);
    DockFixture st = { 0 };
    ASSERT_TRUE_MSG(dock_start_drag(app, &st, 100, 300), "the pull did not arm a drag");
    const FlDockDrag* drag = &st.ws->drag;

    dock_ticks(app, &st, dock_tab_header_frame, 30);
    ASSERT_EQ((int)drag->zone, (int)FlDockDropZone_Left);
    const FlDockNodeId next_id_before = drag->preview->next_node_id;
    FlDockNodeId first_gap = drag->gap_leaf;

    // Straight across to the far side, a different quadrant of the same leaf.
    dock_move_and_tick(DOCK_RIG(app, &st, dock_tab_header_frame), 500, 300, 1);

    ASSERT_EQ_MSG((int)drag->zone, (int)FlDockDropZone_Right, "the far quadrant re-targets");
    ASSERT_GT_MSG((int)drag->preview->next_node_id, (int)next_id_before,
                  "the preview tree is retained across a re-target: its node ids keep counting up");
    ASSERT_NE_MSG((int)drag->gap_leaf, (int)first_gap, "and the gap it carries is a new slot");

    const FlDockNode* gap_node = dock_tree_find_node(drag->preview, drag->gap_leaf);
    ASSERT_TRUE_MSG(gap_node != nullptr && gap_node->parent != nullptr, "the new gap is a leaf inside a split");
    // A Right gap is child[1], so the split's ratio is the target's share and the gap holds the remainder.
    ASSERT_NEAR_MSG(gap_node->parent->split.ratio, 1.0f - FL_DOCK_GAP_RATIO, 1e-5f,
                    "and it opens at the gap ratio, like the one it replaced");
    ASSERT_TRUE_MSG(dock_tree_find_node(drag->preview, first_gap) == nullptr, "the old slot is gone from the tree");

    dock_release(DOCK_RIG(app, &st, dock_tab_header_frame));
}

// The preview is a clone, and nothing the drag does to it reaches the committed tree. Abandoning the drop leaves
// the shape the gesture started from, whatever gaps and re-targets happened.
UTEST(dock_workspace, abandoned_drop_restores_the_committed_shape) {
    DOCK_APP(app, "dockabandon", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_tab_header_frame, 1);
    FlDockTree* tree = st.ws->tree;

    ASSERT_TRUE_MSG(dock_press_tab(DOCK_RIG(app, &st, dock_tab_header_frame), &st, tree->root->id, 1u),
                    "101's tab was not laid out");

    // The committed shape as the gesture found it. The press has already selected the tab it landed on.
    FlDockTree* before = dock_tree_clone(tree);

    // Three different targets, each given time to open, then a release outside the workspace.
    const i32 stops[][2] = { { 100, 300 }, { 500, 300 }, { 300, 380 } };
    for (u32 s = 0; s < sizeof_array(stops); ++s) {
        dock_move_and_tick(DOCK_RIG(app, &st, dock_tab_header_frame), stops[s][0], stops[s][1], 6);
    }
    ASSERT_TRUE_MSG(dock_tree_panel_is_floating(tree, 101), "the committed tree holds the panel loose mid-gesture");

    dock_move_and_tick(DOCK_RIG(app, &st, dock_tab_header_frame), 5000, 5000, 1);
    dock_release(DOCK_RIG(app, &st, dock_tab_header_frame));

    ASSERT_TRUE_MSG(dock_logical_equal(tree->root, before->root),
                    "an abandoned drop leaves exactly the committed shape it started from");
    ASSERT_EQ_MSG((int)tree->floating_count, 0, "and nothing is stranded in the holding pen");
    ASSERT_TRUE(dock_tree_check_invariants(tree));

    dock_tree_destroy(before);
}

// The split the release commits carries the gap's share.
UTEST(dock_workspace, committed_split_inherits_the_gap_ratio) {
    DOCK_APP(app, "dockgapratio", 600, 400);
    DockFixture st = { 0 };
    ASSERT_TRUE_MSG(dock_start_drag(app, &st, 100, 300), "the pull did not arm a drag");
    FlDockTree* tree = st.ws->tree;
    ASSERT_EQ((int)st.ws->drag.zone, (int)FlDockDropZone_Left);

    dock_release(DOCK_RIG(app, &st, dock_tab_header_frame));

    ASSERT_EQ_MSG((int)tree->root->kind, (int)FlDockNodeKind_Split, "the drop split the root");
    ASSERT_EQ((int)tree->root->split.axis, (int)FlDockAxis_X);
    ASSERT_EQ_MSG((int)dock_leaf_panel0(tree->root->split.child[0]), 101, "the dropped panel took the left side");
    ASSERT_NEAR_MSG(tree->root->split.ratio, FL_DOCK_GAP_RATIO, 1e-5f,
                    "and the committed split is exactly the share the gap showed");
    ASSERT_TRUE(dock_tree_check_invariants(tree));
}

// The slot a split gap opens draws neither the dragged panel's tab strip nor its title. The ghost draws the
// panel. A drop onto a tab strip previews the insertion by showing the dragged tab in that strip.

static void dock_gaptab_frame(void* user_data) {
    FlDockWorkspace* ws = dock_fixture_emit((DockFixture*)user_data, dock_leaf(100, 101));
    dock_panel(100, FL_S("Alpha")) {
        ui_text(FL_S("alpha-body"));
    }
    dock_panel(101, FL_S("Beta")) {
        ui_text(FL_S("beta-body"));
    }
    dock_workspace_end(ws);
}

// What one pass over the drag band found: whether the ghost's fill drew and how many rectangles are opaque.
typedef struct DragBand {
    bool fill_drew;
    int opaque;
} DragBand;

static bool dock_is_ghost_fill(const UiRenderCommand* cmd) {
    return cmd->name.data != nullptr && fl_string_equals(cmd->name, FL_S("fl_dock_drag_overlay_fill"));
}

static void dock_expect_translucent(int* utest_result, const UiRenderCommand* cmd) {
    ASSERT_LT_MSG(cmd->bg_color.a, 255.0f, "the ghost's fill is translucent, not a solid block");
    ASSERT_GT_MSG(cmd->bg_color.a, 0.0f, "but not invisible either");
}

// Walks the drag band's commands, asserting the ghost's fill is translucent and counting the opaque rectangles.
static void dock_scan_drag_band(int* utest_result, DragBand* band) {
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (!ui_get_render_command(i, &cmd) || (int)cmd.z_index != UI_Z_DOCK_DRAG_CHROME) {
            continue;
        }
        if (dock_is_ghost_fill(&cmd)) {
            band->fill_drew = true;
            dock_expect_translucent(utest_result, &cmd);
            DOCK_STOP_ON_FAILURE();
        }
        if (cmd.type == UiRenderCmd_Rectangle && cmd.bg_color.a >= 255.0f) {
            band->opaque++;
        }
    }
}

// Nothing the ghost draws is opaque, so the layout it passes over stays visible behind it. It carries the panel's
// title so the user can tell which panel is being dragged.
UTEST(dock_workspace, drag_ghost_is_translucent_and_names_its_panel) {
    DOCK_APP(app, "dockzorder", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_gaptab_frame, 2);

    ASSERT_TRUE_MSG(dock_press_tab(DOCK_RIG(app, &st, dock_gaptab_frame), &st, st.ws->tree->root->id, 1u),
                    "the dragged panel's tab was not laid out");
    dock_move_and_tick(DOCK_RIG(app, &st, dock_gaptab_frame), 100, 300, 30);
    ASSERT_TRUE_MSG(dock_drag_active(&st.ws->drag), "the pull did not arm a drag");

    DragBand band = { false, 0 };
    dock_scan_drag_band(utest_result, &band);
    DOCK_STOP_ON_FAILURE();
    ASSERT_TRUE_MSG(band.fill_drew, "the ghost drew");
    ASSERT_EQ_MSG(band.opaque, 0, "nothing in the drag band is opaque — the layout stays visible through it");
    const bool names_panel = frame_has_text_at_z(FL_S("Beta"), UI_Z_DOCK_DRAG_CHROME);
    ASSERT_TRUE_MSG(names_panel, "the ghost names the panel in hand");
    ASSERT_FALSE_MSG(frame_has_text(FL_S("beta-body")), "and does NOT carry the panel's body");

    dock_release(DOCK_RIG(app, &st, dock_gaptab_frame));
}

UTEST(dock_workspace, split_gap_slot_never_draws_the_dragged_panels_tab) {
    DOCK_APP(app, "dockgaptab", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_gaptab_frame, 2);
    ASSERT_TRUE_MSG(frame_has_text(FL_S("Beta")), "the panel's tab renders normally before the drag");

    ASSERT_TRUE_MSG(dock_press_tab(DOCK_RIG(app, &st, dock_gaptab_frame), &st, st.ws->tree->root->id, 1u),
                    "101's tab was not laid out");

    // Out of the strip and deep into the tile's left quadrant. That is a split gap, which opens a slot of its own.
    dock_move_and_tick(DOCK_RIG(app, &st, dock_gaptab_frame), 100, 300, 6);
    const FlDockDrag* drag = &st.ws->drag;
    ASSERT_TRUE_MSG(dock_drag_active(drag), "the pull did not arm a drag");
    ASSERT_EQ_MSG((int)drag->zone, (int)FlDockDropZone_Left, "100px into a 600px-wide tile is its left quadrant");
    ASSERT_TRUE_MSG(drag->gap_is_new, "a split gap opens a slot of its own");

    // Checked on every frame the gap is there, and not only the one the overlay is late for. The title still
    // reaches the screen in the drag band, where the ghost draws it, so the assertion covers only the docked band.
    for (int i = 0; i < 4; ++i) {
        fl_app_tick(app, &st, dock_gaptab_frame);
        ASSERT_FALSE_MSG(frame_has_text_at_z(FL_S("Beta"), UI_Z_DOCK_CONTENT),
                         "the gap slot must not draw the dragged panel's tab");
        ASSERT_TRUE_MSG(frame_has_text_at_z(FL_S("Alpha"), UI_Z_DOCK_CONTENT),
                        "while the leaf it was pulled out of keeps its own");
    }

    dock_release(DOCK_RIG(app, &st, dock_gaptab_frame));
    fl_app_tick(app, &st, dock_gaptab_frame);
    ASSERT_TRUE_MSG(frame_has_text(FL_S("Beta")), "and the tab is back the moment the drop commits");
}

// The centre region is a tab-into target that is stable under its own preview. Aiming for the strip while a top
// gap is open puts the cursor over a strip the gap has pushed away, but the centre resolves with the gap closed.
// The hit test then reads the same rect that is on screen, and the target holds.
UTEST(dock_workspace, center_region_tabs_in_over_an_open_gap) {
    DOCK_APP(app, "dockcenter", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_gaptab_frame, 2);

    ASSERT_TRUE_MSG(dock_press_tab(DOCK_RIG(app, &st, dock_gaptab_frame), &st, st.ws->tree->root->id, 1u),
                    "101's tab was not laid out");

    // The top quadrant: a gap opens there and pushes the tile, strip included, down the window.
    dock_move_and_tick(DOCK_RIG(app, &st, dock_gaptab_frame), 300, 60, 6);
    const FlDockDrag* drag = &st.ws->drag;
    ASSERT_EQ_MSG((int)drag->zone, (int)FlDockDropZone_Top, "60px down a 400px window is its top quadrant");
    ASSERT_TRUE_MSG(drag->gap_is_new, "a split gap opens a slot of its own");

    dock_move_and_tick(DOCK_RIG(app, &st, dock_gaptab_frame), 300, 200, 1);
    ASSERT_EQ_MSG((int)drag->zone, (int)FlDockDropZone_Center, "the middle of the tile tabs in");
    ASSERT_EQ((int)drag->request.kind, (int)FlDockRequest_Tab);
    ASSERT_FALSE_MSG(drag->gap_is_new, "the split gap closed, so the tile is back at its full rect");

    // Jitter further than the hysteresis every frame. The target does not flip, as the strip's would once its own
    // gap had moved it.
    for (int i = 0; i < 30; ++i) {
        fl_app_inject_mouse_move(app, (i & 1) ? 290 : 310, (i & 2) ? 190 : 210);
        fl_app_tick(app, &st, dock_gaptab_frame);
        ASSERT_EQ_MSG((int)drag->zone, (int)FlDockDropZone_Center, "the centre holds under a moving cursor");
    }

    dock_release(DOCK_RIG(app, &st, dock_gaptab_frame));

    FlDockNode* leaf = dock_tree_find_leaf_with_panel(st.ws->tree, 101);
    ASSERT_TRUE(leaf != nullptr);
    // The drop tabbed the panel back in rather than splitting, and a centre drop lands at the end of the strip.
    dock_expect_tab_order(utest_result, leaf, &(DockLeafWant) { .count = 2, .panels = { 100, 101 } });
}
