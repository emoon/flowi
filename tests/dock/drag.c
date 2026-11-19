// Dock drag: the drag-to-dock hit test and the drag controller.
//
// The targeting layer (ui/dock/dock_drag.{h,c}). Most cases test the pure hit test, which takes node rects and a
// cursor and returns a drop zone and a request, with no Clay and no state.

#include "dock/common.h"

// A single leaf filling the 800x600 workspace. It has both is_leaf and is_root, so the tab region, the outer band
// and the split quadrants are all offered over the one rect. Its strip is the header, y 0..DOCK_TAB_HEIGHT, its centre
// box is x 240..560 by y 180..420, and the diagonals cross at (400, 300).
static const FlDockDropNode k_one_leaf_ws[] = {
    { .node = 7, .rect = { 0.0f, 0.0f, 800.0f, 600.0f }, .is_leaf = true, .is_root = true },
};

// The fields of the raised request a probe also checks.
enum {
    PROBE_KIND = 1 << 0,
    PROBE_SIDE = 1 << 1,
    PROBE_TARGET = 1 << 2,
    PROBE_INDEX = 1 << 3,
    PROBE_RATIO = 1 << 4,
    PROBE_UNNAMED_PANEL = 1 << 5,
};

// A cursor position, the zone it has to resolve to, and the request fields that are checked besides the zone.
typedef struct HitProbe {
    FlVec2 at;
    FlDockDropZone zone;
    const char* msg;
    u32 checks;
    FlDockRequestKind kind;
    FlDockSide side;
    FlDockNodeId target;
    u32 to_index;
    f32 ratio;
} HitProbe;

typedef struct HitScene {
    const FlDockDropNode* nodes;
    u32 count;
} HitScene;

static const HitScene k_one_leaf_scene = { k_one_leaf_ws, 1 };

static void dock_expect_request(int* utest_result, const FlDockRequest* req, const HitProbe* probe) {
    if (probe->checks & PROBE_KIND) {
        ASSERT_EQ((int)req->kind, (int)probe->kind);
    }
    if (probe->checks & PROBE_SIDE) {
        ASSERT_EQ((int)req->side, (int)probe->side);
    }
    if (probe->checks & PROBE_TARGET) {
        ASSERT_EQ((int)req->target_node, (int)probe->target);
    }
    if (probe->checks & PROBE_INDEX) {
        ASSERT_EQ((int)req->to_index, (int)probe->to_index);
    }
    if (probe->checks & PROBE_RATIO) {
        ASSERT_NEAR(req->ratio, probe->ratio, 1e-6f);
    }
    if (probe->checks & PROBE_UNNAMED_PANEL) {
        ASSERT_EQ((int)req->panel, 0); // the hit test is geometry only, and the caller fills in the dragged panel
    }
}

static void dock_expect_probes(int* utest_result, HitScene scene, const HitProbe* probes, u32 count) {
    for (u32 i = 0; i < count; ++i) {
        FlDockRequest req;
        const FlDockDropZone zone = hit_test_drop_zones(scene.nodes, scene.count, probes[i].at, &req);
        ASSERT_EQ_MSG((int)zone, (int)probes[i].zone,
                      probes[i].msg != nullptr ? probes[i].msg : "the cursor resolves to the expected zone");
        dock_expect_request(utest_result, &req, &probes[i]);
    }
}

UTEST(dock_drag, hit_test_resolution_matrix) {
    const u32 split_left = PROBE_KIND | PROBE_SIDE | PROBE_TARGET | PROBE_UNNAMED_PANEL;
    const HitProbe probes[] = {
        // Corner diagonals over the whole tile, probed at 20% and 70% of the width.
        { .at = { 160.0f, 300.0f },
          .zone = FlDockDropZone_Left,
          .msg = "20% across the tile is the left quadrant",
          .checks = split_left,
          .kind = FlDockRequest_Split,
          .side = FlDockSide_Left,
          .target = 7 },
        { .at = { 640.0f, 300.0f },
          .zone = FlDockDropZone_Right,
          .msg = "80% across the tile is the right quadrant",
          .checks = PROBE_SIDE,
          .side = FlDockSide_Right },
        { .at = { 400.0f, 150.0f },
          .zone = FlDockDropZone_Top,
          .msg = "above the centre box, below the strip",
          .checks = PROBE_SIDE,
          .side = FlDockSide_Top },
        { .at = { 400.0f, 500.0f }, .zone = FlDockDropZone_Bottom, .checks = PROBE_SIDE, .side = FlDockSide_Bottom },
        // The strip and the centre box, and nothing else, tab the panel in.
        { .at = { 400.0f, 10.0f },
          .zone = FlDockDropZone_Center,
          .checks = PROBE_KIND | PROBE_TARGET,
          .kind = FlDockRequest_Tab,
          .target = 7 },
        // The tab-into band is the header the leaf draws, to its last pixel.
        { .at = { 400.0f, DOCK_TAB_HEIGHT - 1.0f },
          .zone = FlDockDropZone_Center,
          .msg = "the bottom of the header still tabs in" },
        { .at = { 400.0f, DOCK_TAB_HEIGHT },
          .zone = FlDockDropZone_Top,
          .msg = "the first row of the content area splits again" },
        { .at = { 400.0f, 300.0f },
          .zone = FlDockDropZone_Center,
          .msg = "the middle of the tile tabs in",
          .checks = PROBE_KIND | PROBE_INDEX,
          .kind = FlDockRequest_Tab,
          .to_index = FL_DOCK_TAB_INDEX_END },
        // The centre box is 40% of the tile on each axis, so it scales with the tile.
        { .at = { 400.0f, 181.0f }, .zone = FlDockDropZone_Center, .msg = "the box opens 30% down the tile" },
        { .at = { 400.0f, 179.0f }, .zone = FlDockDropZone_Top },
        { .at = { 241.0f, 300.0f }, .zone = FlDockDropZone_Center },
        { .at = { 239.0f, 300.0f }, .zone = FlDockDropZone_Left },
    };
    dock_expect_probes(utest_result, k_one_leaf_scene, probes, sizeof_array(probes));

    const FlDockDropNode shortish[] = { { .node = 9, .rect = { 0.0f, 0.0f, 400.0f, 200.0f }, .is_leaf = true } };
    const HitProbe short_probes[] = {
        { .at = { 200.0f, 100.0f }, .zone = FlDockDropZone_Center, .msg = "a 200px tile has a 80px tall centre box" },
        { .at = { 200.0f, 55.0f }, .zone = FlDockDropZone_Top },
    };
    dock_expect_probes(utest_result, (HitScene) { shortish, 1 }, short_probes, sizeof_array(short_probes));
}

// Precedence: the tab region beats the workspace outer band, and the outer band beats the leaf's split quadrants.
UTEST(dock_drag, hit_test_precedence) {
    const HitProbe probes[] = {
        { .at = { 5.0f, 10.0f },
          .zone = FlDockDropZone_Center,
          .msg = "inside the outer band but on the strip: the strip wins",
          .checks = PROBE_KIND,
          .kind = FlDockRequest_Tab },
        { .at = { 400.0f, 5.0f },
          .zone = FlDockDropZone_Center,
          .msg = "the workspace's top edge is a leaf's strip, not OuterTop" },
        { .at = { 5.0f, 300.0f },
          .zone = FlDockDropZone_OuterLeft,
          .msg = "below the band the border docks outer, not split-left",
          .checks = PROBE_KIND | PROBE_SIDE,
          .kind = FlDockRequest_Outer,
          .side = FlDockSide_Left },
        { .at = { 795.0f, 300.0f }, .zone = FlDockDropZone_OuterRight, .checks = PROBE_SIDE, .side = FlDockSide_Right },
        { .at = { 400.0f, 590.0f },
          .zone = FlDockDropZone_OuterBottom,
          .checks = PROBE_SIDE,
          .side = FlDockSide_Bottom },
        { .at = { 30.0f, 300.0f },
          .zone = FlDockDropZone_Left,
          .msg = "one px deeper than the 24px band is the left quadrant again",
          .checks = PROBE_KIND,
          .kind = FlDockRequest_Split },
    };
    dock_expect_probes(utest_result, k_one_leaf_scene, probes, sizeof_array(probes));

    // A tile short enough for its centre box to reach into the outer band resolves the same way.
    const FlDockDropNode narrow[] = {
        { .node = 4, .rect = { 0.0f, 0.0f, 300.0f, 60.0f }, .is_leaf = true, .is_root = true },
    };
    const HitProbe narrow_probe = { .at = { 150.0f, 40.0f },
                                    .zone = FlDockDropZone_Center,
                                    .msg = "inside the outer band but in the centre box: the centre wins" };
    dock_expect_probes(utest_result, (HitScene) { narrow, 1 }, &narrow_probe, 1);
}

// The hit test picks the nearest leaf. Splitter gutters have no containing leaf, so with a containing-leaf rule
// the cursor would miss there. With nearest-leaf, None means only "outside the workspace".
UTEST(dock_drag, hit_test_nearest_leaf) {
    const FlDockDropNode nodes[] = {
        { .node = 1, .rect = { 0.0f, 0.0f, 800.0f, 600.0f }, .is_leaf = false, .is_root = true },
        { .node = 2, .rect = { 0.0f, 0.0f, 390.0f, 600.0f }, .is_leaf = true, .is_root = false },
        { .node = 3, .rect = { 410.0f, 0.0f, 390.0f, 600.0f }, .is_leaf = true, .is_root = false },
    };
    const HitProbe probes[] = {
        // Mid-gutter, 8px from the left tile and 12px from the right one. The target is the nearer leaf, never the
        // split root.
        { .at = { 398.0f, 300.0f },
          .zone = FlDockDropZone_Right,
          .msg = "the gutter resolves against the nearer tile",
          .checks = PROBE_TARGET,
          .target = 2 },
        { .at = { 402.0f, 300.0f }, .zone = FlDockDropZone_Left, .checks = PROBE_TARGET, .target = 3 },
        // A leaf's own centre box, over the right tile.
        { .at = { 600.0f, 300.0f },
          .zone = FlDockDropZone_Center,
          .checks = PROBE_KIND | PROBE_TARGET,
          .kind = FlDockRequest_Tab,
          .target = 3 },
        // Only a cursor outside the workspace misses, and out_req stays zeroed.
        { .at = { -50.0f, -50.0f },
          .zone = FlDockDropZone_None,
          .checks = PROBE_KIND | PROBE_TARGET,
          .kind = (FlDockRequestKind)0,
          .target = 0 },
        { .at = { 900.0f, 300.0f }, .zone = FlDockDropZone_None },
    };
    dock_expect_probes(utest_result, (HitScene) { nodes, 3 }, probes, sizeof_array(probes));
}

// The insertion index follows the cursor along the strip, so the drop chooses where in the tab order the panel
// lands. The dock tree clamps out-of-range indices.
UTEST(dock_drag, hit_test_tab_insertion_index) {
    FlDockRequest req;
    u32 previous = 0;

    for (f32 x = 0.0f; x < 800.0f; x += 8.0f) {
        FlDockDropZone z = hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { x, 10.0f }, &req);
        ASSERT_EQ((int)z, (int)FlDockDropZone_Center);
        ASSERT_TRUE_MSG(req.to_index >= previous, "the index never walks backwards as the cursor moves right");
        previous = req.to_index;
    }

    const HitProbe probes[] = {
        { .at = { 10.0f, 10.0f },
          .zone = FlDockDropZone_Center,
          .msg = "the first slot is the front of the strip",
          .checks = PROBE_INDEX,
          .to_index = 0 },
        { .at = { 170.0f, 10.0f }, .zone = FlDockDropZone_Center, .checks = PROBE_INDEX, .to_index = 3 },
    };
    dock_expect_probes(utest_result, k_one_leaf_scene, probes, sizeof_array(probes));
}

enum { FAMILY_EFFECT = 2, FAMILY_OTHER = 5 };

// A drag over the one-leaf workspace whose dock-family gate is gate.
static FlDockDrag dock_gated_drag(FlDockFamilyGate gate) {
    return (FlDockDrag) { .base_count = 1, .gate = gate, .base = { k_one_leaf_ws[0] } };
}

// The dock-family filter at drop resolution. A workspace with a non-zero accept filter offers no zone to a panel
// whose family it rejects, so there is no highlight, no preview and no commit. A matching panel, or a general
// panel into an unfiltered workspace, docks as usual.
UTEST(dock_drag, hit_test_family_filter) {
    const FlVec2 probes[] = {
        { 400.0f, 10.0f },  // tab strip
        { 160.0f, 300.0f }, // left quadrant
        { 640.0f, 300.0f }, // right quadrant
        { 400.0f, 500.0f }, // bottom quadrant
        { 5.0f, 300.0f },   // outer band
    };
    // A workspace filtering for FAMILY_EFFECT rejects a FAMILY_OTHER panel. No zone resolves anywhere and the
    // request stays empty, the same result as leaving the workspace, so the view shows no preview.
    for (u32 i = 0; i < sizeof_array(probes); ++i) {
        FlDockDrag drag = dock_gated_drag((FlDockFamilyGate) { FAMILY_OTHER, FAMILY_EFFECT });
        FlDockDropZone z = dock_drag_resolve(&drag, probes[i]);
        ASSERT_EQ_MSG((int)z, (int)FlDockDropZone_None, "a rejected family must offer no zone");
        ASSERT_EQ((int)drag.request.kind, 0);
        ASSERT_EQ((int)drag.request.target_node, 0);
    }

    FlVec2 band = { 400.0f, 10.0f };
    FlDockDrag matching = dock_gated_drag((FlDockFamilyGate) { FAMILY_EFFECT, FAMILY_EFFECT });
    FlDockDropZone z = dock_drag_resolve(&matching, band);
    ASSERT_EQ_MSG((int)z, (int)FlDockDropZone_Center, "matching family docks normally");
    ASSERT_EQ((int)matching.request.kind, (int)FlDockRequest_Tab);
    ASSERT_EQ((int)matching.request.target_node, 7);

    FlDockDrag general = dock_gated_drag((FlDockFamilyGate) { 0 });
    z = dock_drag_resolve(&general, (FlVec2) { 640.0f, 300.0f });
    ASSERT_EQ_MSG((int)z, (int)FlDockDropZone_Right, "the default accept-anywhere case still resolves");
    ASSERT_EQ((int)general.request.kind, (int)FlDockRequest_Split);

    FlDockDrag unfiltered = dock_gated_drag((FlDockFamilyGate) { FAMILY_EFFECT, 0 });
    z = dock_drag_resolve(&unfiltered, band);
    ASSERT_EQ_MSG((int)z, (int)FlDockDropZone_Center, "an unfiltered workspace accepts any family");
}

// A live drag of panel 42 over the one-leaf workspace.
static FlDockDrag dock_live_drag(void) {
    return (FlDockDrag) { .active = true, .panel = 42, .base_count = 1, .base = { k_one_leaf_ws[0] } };
}

// A cursor position a stateful drag resolves to a zone.
typedef struct ZoneStep {
    FlVec2 at;
    FlDockDropZone zone;
    const char* msg;
} ZoneStep;

static void dock_expect_zone_steps(int* utest_result, FlDockDrag* drag, const ZoneStep* steps, u32 count) {
    for (u32 i = 0; i < count; ++i) {
        const FlDockDropZone zone = dock_drag_resolve(drag, steps[i].at);
        ASSERT_EQ_MSG((int)zone, (int)steps[i].zone,
                      steps[i].msg != nullptr ? steps[i].msg : "the cursor resolves to the expected zone");
    }
}

// Switch hysteresis on the controller. A sweep across the Left/Top diagonal, which at y=100 crosses x=133,
// gives one stable run and one transition 8px past where the two zones started to disagree, with no flicker
// back.
UTEST(dock_drag, resolve_hysteresis) {
    FlDockDrag drag = dock_live_drag();

    const ZoneStep forward[] = {
        { { 120.0f, 100.0f }, FlDockDropZone_Left, "the first resolution has nothing to stick to" },
    };
    dock_expect_zone_steps(utest_result, &drag, forward, sizeof_array(forward));
    ASSERT_EQ((int)drag.request.panel, 42);

    const ZoneStep held[] = {
        { { 132.0f, 100.0f }, FlDockDropZone_Left, nullptr },
        { { 134.0f, 100.0f }, FlDockDropZone_Left, "2px past the boundary is inside the hysteresis" },
        { { 139.0f, 100.0f }, FlDockDropZone_Left, "7px past it still holds" },
        { { 140.0f, 100.0f }, FlDockDropZone_Top, "8px past it switches" },
    };
    dock_expect_zone_steps(utest_result, &drag, held, sizeof_array(held));
    ASSERT_EQ((int)drag.request.side, (int)FlDockSide_Top);

    const ZoneStep back[] = {
        { { 138.0f, 100.0f }, FlDockDropZone_Top, nullptr },
        { { 131.0f, 100.0f },
          FlDockDropZone_Top,
          "jittering back across the boundary does not flicker the target back" },
        { { 130.0f, 100.0f }, FlDockDropZone_Left, "8px past it the other way does switch" },
    };
    dock_expect_zone_steps(utest_result, &drag, back, sizeof_array(back));
}

// The tab region also beats stickiness: a held target never suppresses a tab-into, however small the movement
// into the region.
UTEST(dock_drag, resolve_tab_region_beats_stickiness) {
    FlDockDrag drag = dock_live_drag();

    const ZoneStep steps[] = {
        { { 400.0f, 174.0f }, FlDockDropZone_Top, nullptr },
        { { 400.0f, 181.0f },
          FlDockDropZone_Center,
          "7px of movement, well inside the hysteresis, still reaches the centre box" },
    };
    dock_expect_zone_steps(utest_result, &drag, steps, sizeof_array(steps));
    ASSERT_EQ((int)drag.request.kind, (int)FlDockRequest_Tab);
}

// Every split the hit test raises carries the gap's share on the request, so the committed split has the
// proportion the gap showed.
UTEST(dock_drag, hit_test_carries_the_gap_ratio) {
    const HitProbe probes[] = {
        { .at = { 160.0f, 300.0f }, .zone = FlDockDropZone_Left, .checks = PROBE_RATIO, .ratio = FL_DOCK_GAP_RATIO },
        { .at = { 5.0f, 300.0f }, .zone = FlDockDropZone_OuterLeft, .checks = PROBE_RATIO, .ratio = FL_DOCK_GAP_RATIO },
        { .at = { 400.0f, 300.0f },
          .zone = FlDockDropZone_Center,
          .msg = "a tab creates no split, so it names no ratio",
          .checks = PROBE_RATIO,
          .ratio = 0.0f },
    };
    dock_expect_probes(utest_result, k_one_leaf_scene, probes, sizeof_array(probes));
}

// The sticky-gap rule keeps the live hit test from reacting to the gap it opened. From inside the slot the layout
// has opened, the cursor moves the gap around the node it opened in, but cannot hand it to another node or
// another kind of dock.
UTEST(dock_drag, sticky_gap_holds_the_target) {
    FlDockDrag drag = dock_live_drag();

    const ZoneStep open[] = { { { 160.0f, 300.0f }, FlDockDropZone_Left, nullptr } };
    dock_expect_zone_steps(utest_result, &drag, open, sizeof_array(open));

    // The gap opens over the left third. Inside it are both the workspace's outer band and the leaf's tab strip,
    // two other kinds of dock that the gap must not move to because it made room over them.
    drag.gap_rect = (FlDockRect) { 0.0f, 0.0f, 280.0f, 600.0f };
    drag.has_gap_rect = true;
    drag.gap_is_new = true;

    const ZoneStep inside[] = {
        { { 20.0f, 300.0f }, FlDockDropZone_Left, "the outer band inside the gap does not re-target" },
        { { 140.0f, 20.0f }, FlDockDropZone_Left, "nor does the tab strip inside the gap" },
    };
    dock_expect_zone_steps(utest_result, &drag, inside, sizeof_array(inside));
    ASSERT_EQ((int)drag.request.side, (int)FlDockSide_Left);

    const ZoneStep other_side[] = {
        { { 260.0f, 500.0f },
          FlDockDropZone_Bottom,
          "the same node's other side is still reachable from inside the gap" },
    };
    dock_expect_zone_steps(utest_result, &drag, other_side, sizeof_array(other_side));
    ASSERT_EQ((int)drag.request.target_node, 7);

    const ZoneStep leave[] = { { { 640.0f, 300.0f }, FlDockDropZone_Right, "leaving the gap resolves freely again" } };
    dock_expect_zone_steps(utest_result, &drag, leave, sizeof_array(leave));

    // A tab-into displaces nothing, so its "gap" is the target leaf and never sticks.
    drag.gap_is_new = false;
    const ZoneStep tab_into[] = {
        { { 140.0f, 20.0f }, FlDockDropZone_Center, "a tab-into gap opened no space, so it holds nothing" },
    };
    dock_expect_zone_steps(utest_result, &drag, tab_into, sizeof_array(tab_into));
}

// Pulls panel out of tree and drops it back as a tab of target_node at to_index.
static bool dock_pull_and_drop_tab(FlDockTree* tree, FlDockPanelId panel, FlDockNodeId target_node, u32 to_index) {
    FlDockDrag drag = { 0 };
    dock_drag_begin_undock(&drag, tree, panel, (FlDockFamilyGate) { 0 });
    if (!dock_tree_panel_is_floating(tree, panel)) {
        return false;
    }
    drag.zone = FlDockDropZone_Center;
    drag.request = (FlDockRequest) {
        .kind = FlDockRequest_Tab, .panel = panel, .target_node = target_node, .to_index = to_index
    };
    return dock_drag_end(&drag, tree);
}

// Dragging a tab within its own leaf is a reorder. The undock pulls it out, the drop tabs it back in, and the
// resolved insertion slot puts it where the cursor was along the strip.
UTEST(dock_drag, within_leaf_drag_reorders) {
    FlDockTree* committed = dock_tree_create();
    FlDockNodeId root = committed->root->id;
    ASSERT_TRUE(DOCK_TAB_PANELS(committed, root, 10, 20, 30));
    ASSERT_TRUE(dock_pull_and_drop_tab(committed, 30, root, 0));

    FlDockNode* leaf = dock_tree_find_leaf_with_panel(committed, 30);
    ASSERT_TRUE(leaf != nullptr);
    // The drop's insertion slot puts 30 at the front of the strip.
    dock_expect_tab_order(utest_result, leaf, &(DockLeafWant) { .count = 3, .panels = { 30, 10, 20 } });
    ASSERT_TRUE(dock_tree_check_invariants(committed));
    ASSERT_EQ_MSG((int)dock_tree_collect_panels(committed, nullptr, 0), 3, "a reorder conserves the panel universe");

    // A slot past the end of the strip clamps to the back.
    ASSERT_TRUE(dock_pull_and_drop_tab(committed, 30, root, 99));

    leaf = dock_tree_find_leaf_with_panel(committed, 30);
    dock_expect_tab_order(utest_result, leaf, &(DockLeafWant) { .count = 3, .panels = { 10, 20, 30 } });

    dock_tree_destroy(committed);
}

UTEST(dock_drag, clone_is_independent_and_equal) {
    enum { NAV = 11, VIEWPORT = 22, CONSOLE = 33, LOG = 44, EXTRA = 55 };
    const FlDockSpec* spec = dock_split(FlDockAxis_X, 0.25f, dock_leaf(NAV),
                                        dock_split(FlDockAxis_Y, 0.6f, dock_leaf(VIEWPORT), dock_leaf(CONSOLE, LOG)));
    FlDockTree* orig = dock_tree_build(spec);
    ASSERT_TRUE_MSG(orig != nullptr, "layout literal must build");

    FlDockTree* clone = dock_tree_clone(orig);
    ASSERT_TRUE_MSG(clone != nullptr, "clone must succeed");
    ASSERT_TRUE(dock_tree_check_invariants(clone));
    const FlDockNode* clone_root = clone->root;
    ASSERT_TRUE_MSG(dock_logical_equal(orig->root, clone_root), "clone must be logically identical to the original");
    ASSERT_EQ((int)clone->next_node_id, (int)orig->next_node_id); // the id allocator is copied, so future ids match

    FlDockNode* nav_leaf = dock_tree_find_leaf_with_panel(clone, NAV);
    ASSERT_TRUE(nav_leaf != nullptr);
    FlDockRequest req = { .kind = FlDockRequest_Tab, .panel = EXTRA, .target_node = nav_leaf->id };
    ASSERT_TRUE(dock_tree_apply(clone, &req));
    ASSERT_TRUE(dock_tree_check_invariants(clone));
    const FlDockNode* in_clone = dock_tree_find_leaf_with_panel(clone, EXTRA);
    ASSERT_TRUE_MSG(in_clone != nullptr, "EXTRA must be in the clone");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(orig, EXTRA) == nullptr, "the original must be untouched");

    dock_tree_destroy(clone);
    dock_tree_destroy(orig);
}

// Releasing over a valid zone commits apply(committed, request) into the live tree. The controller's deferred
// commit or cancel is applied in dock_drag_end, so the tree is never mutated in the middle of a walk.
UTEST(dock_drag, end_commits_over_zone) {
    FlDockTree* committed = dock_tree_create();
    FlDockNodeId root = committed->root->id;
    ASSERT_TRUE(dock_tab_into(committed, root, 10));

    FlDockTree* expected = dock_pair_tree(10, 20);
    ASSERT_TRUE(expected != nullptr);

    // Arm the controller and set the resolved drop (Right split of the root), as dock_drag_emit does each frame.
    FlDockDrag drag = { 0 };
    drag.active = true;
    drag.panel = 20;
    drag.zone = FlDockDropZone_Right;
    drag.request
        = (FlDockRequest) { .kind = FlDockRequest_Split, .panel = 20, .target_node = root, .side = FlDockSide_Right };

    ASSERT_TRUE(dock_drag_end(&drag, committed));
    ASSERT_FALSE(dock_drag_active(&drag));
    ASSERT_TRUE(dock_tree_check_invariants(committed));
    const FlDockNode* committed_root = committed->root;
    ASSERT_TRUE_MSG(dock_logical_equal(committed_root, expected->root),
                    "committing over a zone must match apply(committed, request)");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(committed, 20) != nullptr, "panel 20 lands in the new leaf");

    dock_tree_destroy(expected);
    dock_tree_destroy(committed);
}

UTEST(dock_drag, end_cancel_leaves_tree_unchanged) {
    FlDockTree* committed = dock_pair_tree(10, 20);
    FlDockTree* control = dock_pair_tree(10, 20);
    ASSERT_TRUE(committed != nullptr && control != nullptr);

    FlDockDrag drag = { 0 };
    drag.active = true;
    drag.panel = 30;
    drag.zone = FlDockDropZone_None;

    ASSERT_FALSE(dock_drag_end(&drag, committed));
    ASSERT_FALSE(dock_drag_active(&drag));
    ASSERT_TRUE(dock_tree_check_invariants(committed));
    const FlDockNode* committed_root = committed->root;
    ASSERT_TRUE_MSG(dock_logical_equal(committed_root, control->root), "cancelling must leave the tree unchanged");

    dock_tree_destroy(control);
    dock_tree_destroy(committed);
}

UTEST(dock_drag, undock_then_redock_conserves) {
    // Headless, so arm_drag's snapshot does nothing (there is no Clay context). The case is about the tree
    // mutations.
    FlDockTree* committed = dock_pair_tree(10, 20);
    ASSERT_TRUE(committed != nullptr);
    u32 before = dock_tree_collect_panels(committed, nullptr, 0);
    ASSERT_EQ((int)before, 2);

    FlDockDrag drag = { 0 };
    dock_drag_begin_undock(&drag, committed, 10, (FlDockFamilyGate) { 0 });
    ASSERT_TRUE_MSG(dock_drag_active(&drag), "the undock grab arms a drag");
    ASSERT_TRUE_MSG(dock_tree_panel_is_floating(committed, 10), "the panel is now internal-floating");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(committed, 10) == nullptr, "10 is no longer in a leaf");
    const bool clean = dock_tree_check_invariants(committed);
    ASSERT_TRUE(clean);
    ASSERT_EQ_MSG((int)dock_tree_collect_panels(committed, nullptr, 0), (int)before,
                  "undock conserves the panel universe (float, not close)");

    FlDockNode* leaf20 = dock_tree_find_leaf_with_panel(committed, 20);
    ASSERT_TRUE(leaf20 != nullptr);
    drag.zone = FlDockDropZone_Center;
    drag.request = (FlDockRequest) { .kind = FlDockRequest_Tab, .panel = 10, .target_node = leaf20->id };
    ASSERT_TRUE(dock_drag_end(&drag, committed));

    ASSERT_FALSE_MSG(dock_tree_panel_is_floating(committed, 10), "re-docking pulls 10 back out of the floating set");
    const FlDockNode* leaf10 = dock_tree_find_leaf_with_panel(committed, 10);
    ASSERT_TRUE_MSG(leaf10 != nullptr, "10 is docked again");
    ASSERT_EQ_MSG((int)leaf10->id, (int)leaf20->id, "10 re-docks into 20's leaf");
    const bool still_clean = dock_tree_check_invariants(committed);
    ASSERT_TRUE(still_clean);
    ASSERT_EQ_MSG((int)dock_tree_collect_panels(committed, nullptr, 0), (int)before,
                  "the whole undock→re-dock round trip conserves the panel universe");

    dock_tree_destroy(committed);
}
