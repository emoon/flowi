// Dock tree: the logical tree operations, the invariant check and the fuzz conservation case.
//
// The retained logical dock tree, its seven pure operations and the invariant check (ui/dock/dock_tree.{h,c}).
// Nothing is rendered. Each case has its own tree and checks the invariants after every mutation.

#include "dock/common.h"

UTEST(dock_tree, tab) {
    FlDockTree* tree = dock_tree_create();
    FlDockNodeId root = tree->root->id;
    ASSERT_TRUE(dock_tree_check_invariants(tree)); // a new tree is a single empty root leaf
    dock_expect_leaf(utest_result, tree->root, &(DockLeafWant) { 0 });

    ASSERT_TRUE(dock_tab_into(tree, root, 1001));
    ASSERT_TRUE(dock_tab_into(tree, root, 1002));
    // A newly added tab becomes selected.
    dock_expect_leaf(utest_result, tree->root, &(DockLeafWant) { .count = 2, .selected = 1, .panels = { 1001, 1002 } });

    // Docking a panel that is already present selects it again and adds no duplicate.
    FlDockRequest again = { .kind = FlDockRequest_Tab, .panel = 1001, .target_node = root };
    ASSERT_TRUE(dock_tree_apply(tree, &again));
    dock_expect_leaf(utest_result, tree->root, &(DockLeafWant) { .count = 2, .selected = 0, .panels = { 1001, 1002 } });
    ASSERT_TRUE(dock_tree_check_invariants(tree));

    // A Tab request whose target is a split node is rejected.
    FlDockRequest mk_split
        = { .kind = FlDockRequest_Split, .panel = 3003, .target_node = root, .side = FlDockSide_Right };
    ASSERT_TRUE(dock_tree_apply(tree, &mk_split));
    ASSERT_EQ((int)tree->root->kind, (int)FlDockNodeKind_Split);
    FlDockRequest tab_into_split = { .kind = FlDockRequest_Tab, .panel = 4004, .target_node = tree->root->id };
    ASSERT_FALSE(dock_tree_apply(tree, &tab_into_split));
    ASSERT_TRUE(dock_tree_check_invariants(tree));

    dock_tree_destroy(tree);
}

UTEST(dock_tree, split) {
    FlDockTree* tree = dock_tree_create();
    FlDockNodeId root = tree->root->id;
    ASSERT_TRUE(dock_tab_into(tree, root, 1001));

    // Split the root leaf to the Right: the root becomes an X split, the original leaf is child[0] and the new
    // leaf, holding the new panel, is child[1].
    FlDockRequest right = { .kind = FlDockRequest_Split, .panel = 2002, .target_node = root, .side = FlDockSide_Right };
    ASSERT_TRUE(dock_tree_apply(tree, &right));
    ASSERT_TRUE(dock_tree_check_invariants(tree));
    const FlDockNode* split = tree->root;
    ASSERT_EQ((int)split->kind, (int)FlDockNodeKind_Split);
    ASSERT_EQ((int)split->split.axis, (int)FlDockAxis_X);
    const FlDockNode* original = split->split.child[0];
    ASSERT_EQ((int)original->id, (int)root);
    ASSERT_EQ((int)dock_leaf_panel0(split->split.child[1]), 2002);

    // A Top split gives a Y axis with the new leaf as child[0], the top side.
    FlDockTree* t2 = dock_tree_create();
    FlDockNodeId r2 = t2->root->id;
    ASSERT_TRUE(dock_tab_into(t2, r2, 5));
    FlDockRequest top = { .kind = FlDockRequest_Split, .panel = 6, .target_node = r2, .side = FlDockSide_Top };
    ASSERT_TRUE(dock_tree_apply(t2, &top));
    ASSERT_TRUE(dock_tree_check_invariants(t2));
    const FlDockNode* top_split = t2->root;
    ASSERT_EQ((int)top_split->split.axis, (int)FlDockAxis_Y);
    ASSERT_EQ((int)dock_leaf_panel0(top_split->split.child[0]), 6); // top side is child[0]

    dock_tree_destroy(t2);
    dock_tree_destroy(tree);
}

// Splits tree's root leaf on side with a new leaf holding panel. True when the request applied and the tree still
// passes its invariants.
static bool dock_split_root_leaf(FlDockTree* tree, FlDockPanelId panel, FlDockSide side, f32 ratio) {
    FlDockRequest req
        = { .kind = FlDockRequest_Split, .panel = panel, .target_node = tree->root->id, .side = side, .ratio = ratio };
    return dock_tree_apply(tree, &req) && dock_tree_check_invariants(tree);
}

// A tree whose root leaf holds panel 1.
static FlDockTree* dock_single_panel_tree(void) {
    FlDockTree* tree = dock_tree_create();
    dock_tab_into(tree, tree->root->id, 1);
    return tree;
}

// A split request's ratio is the new leaf's share of the split. An unset ratio keeps the even split.
UTEST(dock_tree, split_ratio_rides_the_request) {
    FlDockTree* tree = dock_single_panel_tree();
    ASSERT_TRUE(dock_split_root_leaf(tree, 2, FlDockSide_Left, 0.35f));
    ASSERT_NEAR_MSG(tree->root->split.ratio, 0.35f, 1e-5f, "the new leaf is child[0], so the ratio is its share");

    // On the far side the new leaf is child[1], so the same share is stored as the complement.
    FlDockTree* t2 = dock_single_panel_tree();
    ASSERT_TRUE(dock_split_root_leaf(t2, 2, FlDockSide_Bottom, 0.35f));
    ASSERT_NEAR(t2->root->split.ratio, 0.65f, 1e-5f);

    FlDockTree* t3 = dock_single_panel_tree();
    ASSERT_TRUE(dock_split_root_leaf(t3, 2, FlDockSide_Right, 0.0f));
    ASSERT_NEAR_MSG(t3->root->split.ratio, FL_DOCK_SPLIT_RATIO_DEFAULT, 1e-5f, "no ratio named keeps the even split");

    // An outer dock reads the same field, and out-of-range shares clamp to the min-size floor like any other ratio.
    FlDockRequest outer
        = { .kind = FlDockRequest_Outer, .panel = 3, .side = FlDockSide_Left, .ratio = FL_DOCK_MIN_RATIO * 0.1f };
    ASSERT_TRUE(dock_tree_apply(t3, &outer));
    ASSERT_TRUE(dock_tree_check_invariants(t3));
    ASSERT_NEAR(t3->root->split.ratio, FL_DOCK_MIN_RATIO, 1e-5f);

    dock_tree_destroy(t3);
    dock_tree_destroy(t2);
    dock_tree_destroy(tree);
}

UTEST(dock_tree, outer) {
    // Outer against an empty tree puts the first panel into the root leaf.
    FlDockTree* tree = dock_tree_create();
    FlDockRequest first = { .kind = FlDockRequest_Outer, .panel = 100, .side = FlDockSide_Left };
    ASSERT_TRUE(dock_tree_apply(tree, &first));
    ASSERT_TRUE(dock_tree_check_invariants(tree));
    dock_expect_leaf(utest_result, tree->root, &(DockLeafWant) { .count = 1, .panels = { 100 } });

    // A second panel docked Outer/Bottom splits the whole workspace: Y axis, the new leaf on the bottom side.
    FlDockRequest bottom = { .kind = FlDockRequest_Outer, .panel = 200, .side = FlDockSide_Bottom };
    ASSERT_TRUE(dock_tree_apply(tree, &bottom));
    ASSERT_TRUE(dock_tree_check_invariants(tree));
    const FlDockNode* split = tree->root;
    ASSERT_EQ((int)split->kind, (int)FlDockNodeKind_Split);
    ASSERT_EQ((int)split->split.axis, (int)FlDockAxis_Y);
    ASSERT_EQ((int)dock_leaf_panel0(split->split.child[1]), 200); // bottom side is child[1]

    dock_tree_destroy(tree);
}

UTEST(dock_tree, undock) {
    // split(leafA[10], leafB[20]). Undocking 10 empties leafA, which collapses, and the root becomes leafB. Panel
    // 10 moves to the floating set, so it is still in the universe but not docked.
    FlDockTree* tree = dock_pair_tree(10, 20);
    ASSERT_TRUE(tree != nullptr);

    FlDockRequest undock = { .kind = FlDockRequest_Undock, .panel = 10 };
    ASSERT_TRUE(dock_tree_apply(tree, &undock));
    ASSERT_TRUE(dock_tree_check_invariants(tree));
    const FlDockNode* survivor = tree->root;
    ASSERT_EQ((int)survivor->kind, (int)FlDockNodeKind_Leaf);
    ASSERT_EQ((int)dock_leaf_panel0(survivor), 20);
    const u32 universe = dock_tree_collect_panels(tree, nullptr, 0);
    ASSERT_TRUE(dock_tree_panel_is_floating(tree, 10));
    ASSERT_EQ((int)universe, 2); // both panels still in the universe

    // Undocking a panel that is already floating, or absent, fails and changes nothing.
    ASSERT_FALSE(dock_tree_apply(tree, &undock));

    dock_tree_destroy(tree);
}

UTEST(dock_tree, reorder_tab) {
    FlDockTree* tree = dock_tree_create();
    FlDockNodeId root = tree->root->id;
    ASSERT_TRUE(dock_tab_into(tree, root, 10));
    ASSERT_TRUE(dock_tab_into(tree, root, 20));
    ASSERT_TRUE(dock_tab_into(tree, root, 30)); // [10,20,30], selected=2 (panel 30)

    FlDockRequest reorder = { .kind = FlDockRequest_ReorderTab, .panel = 10, .to_index = 2 };
    ASSERT_TRUE(dock_tree_apply(tree, &reorder));
    ASSERT_TRUE(dock_tree_check_invariants(tree));
    // The selection follows panel 30, now at index 1.
    dock_expect_leaf(utest_result, tree->root, &(DockLeafWant) { .count = 3, .selected = 1, .panels = { 20, 30, 10 } });

    dock_tree_destroy(tree);
}

UTEST(dock_tree, close) {
    // split(leafA[100], leafB[200,201]). Closing 100 empties leafA, the split with one child dissolves, and leafB
    // becomes the root.
    FlDockTree* tree = dock_tree_create();
    FlDockNodeId root = tree->root->id;
    ASSERT_TRUE(dock_tab_into(tree, root, 100));
    FlDockRequest split = { .kind = FlDockRequest_Split, .panel = 200, .target_node = root, .side = FlDockSide_Right };
    ASSERT_TRUE(dock_tree_apply(tree, &split));
    FlDockNodeId leaf_b = tree->root->split.child[1]->id;
    ASSERT_TRUE(dock_tab_into(tree, leaf_b, 201));

    FlDockRequest close100 = { .kind = FlDockRequest_Close, .panel = 100 };
    ASSERT_TRUE(dock_tree_apply(tree, &close100));
    ASSERT_TRUE(dock_tree_check_invariants(tree));
    const FlDockNode* survivor = tree->root;
    ASSERT_EQ((int)survivor->kind, (int)FlDockNodeKind_Leaf);
    ASSERT_EQ((int)survivor->id, (int)leaf_b);
    const u32 survivor_tabs = survivor->leaf.count;
    ASSERT_EQ((int)survivor_tabs, 2);
    const u32 universe = dock_tree_collect_panels(tree, nullptr, 0);
    ASSERT_EQ((int)universe, 2);

    // Closing the remaining panels empties the root leaf, which stays as an empty leaf.
    FlDockRequest close200 = { .kind = FlDockRequest_Close, .panel = 200 };
    FlDockRequest close201 = { .kind = FlDockRequest_Close, .panel = 201 };
    ASSERT_TRUE(dock_tree_apply(tree, &close200));
    ASSERT_TRUE(dock_tree_apply(tree, &close201));
    const bool clean = dock_tree_check_invariants(tree);
    ASSERT_TRUE(clean);
    dock_expect_leaf(utest_result, tree->root, &(DockLeafWant) { 0 });
    const u32 emptied = dock_tree_collect_panels(tree, nullptr, 0);
    ASSERT_EQ((int)emptied, 0);

    dock_tree_destroy(tree);
}

// Closes panel in tree and checks the tree still passes its invariants.
static bool dock_close_panel(FlDockTree* tree, FlDockPanelId panel) {
    FlDockRequest req = { .kind = FlDockRequest_Close, .panel = panel };
    return dock_tree_apply(tree, &req) && dock_tree_check_invariants(tree);
}

UTEST(dock_tree, close_selected_tab_adjust) {
    FlDockTree* tree = dock_tree_create();
    FlDockNodeId root = tree->root->id;
    ASSERT_TRUE(dock_tab_into(tree, root, 10));
    ASSERT_TRUE(dock_tab_into(tree, root, 20));
    ASSERT_TRUE(dock_tab_into(tree, root, 30));

    // Select the middle tab. Docking a panel that is already present selects it.
    FlDockRequest sel20 = { .kind = FlDockRequest_Tab, .panel = 20, .target_node = root };
    ASSERT_TRUE(dock_tree_apply(tree, &sel20));
    ASSERT_EQ((int)tree->root->leaf.selected, 1);

    // Closing a tab before the selected one shifts the selection index down, so panel 20 stays selected.
    ASSERT_TRUE(dock_close_panel(tree, 10));
    dock_expect_leaf(utest_result, tree->root, &(DockLeafWant) { .count = 2, .selected = 0, .panels = { 20, 30 } });

    // Closing a tab after the selected one leaves the selection index unchanged.
    ASSERT_TRUE(dock_close_panel(tree, 30));
    dock_expect_leaf(utest_result, tree->root, &(DockLeafWant) { .count = 1, .selected = 0, .panels = { 20 } });

    // Closing the selected last tab clamps the selection back into range.
    ASSERT_TRUE(dock_tab_into(tree, root, 40));
    ASSERT_TRUE(dock_tab_into(tree, root, 50));
    const u32 selected = tree->root->leaf.selected;
    ASSERT_EQ((int)selected, 2);
    ASSERT_TRUE(dock_close_panel(tree, 50));
    dock_expect_leaf(utest_result, tree->root, &(DockLeafWant) { .count = 2, .selected = 1, .panels = { 20, 40 } });

    dock_tree_destroy(tree);
}

UTEST(dock_tree, splitter_resize) {
    FlDockTree* tree = dock_pair_tree(1, 2);
    ASSERT_TRUE(tree != nullptr);
    FlDockNodeId split_id = tree->root->id;
    FlDockNodeId leaf_id = tree->root->split.child[0]->id;

    FlDockRequest resize = { .kind = FlDockRequest_SplitterResize, .target_node = split_id, .ratio = 0.3f };
    ASSERT_TRUE(dock_tree_apply(tree, &resize));
    ASSERT_TRUE(dock_tree_check_invariants(tree));
    ASSERT_TRUE(tree->root->split.ratio > 0.29f && tree->root->split.ratio < 0.31f);

    // Out-of-range ratios clamp to the min-size floor, which keeps the ratio strictly inside (0,1).
    resize.ratio = 0.999f;
    ASSERT_TRUE(dock_tree_apply(tree, &resize));
    ASSERT_TRUE(tree->root->split.ratio <= 1.0f - FL_DOCK_MIN_RATIO + 0.0001f);
    resize.ratio = -1.0f;
    ASSERT_TRUE(dock_tree_apply(tree, &resize));
    ASSERT_TRUE(tree->root->split.ratio >= FL_DOCK_MIN_RATIO - 0.0001f);
    ASSERT_TRUE(dock_tree_check_invariants(tree));

    // Resizing a leaf is rejected.
    FlDockRequest bad = { .kind = FlDockRequest_SplitterResize, .target_node = leaf_id, .ratio = 0.5f };
    ASSERT_FALSE(dock_tree_apply(tree, &bad));

    dock_tree_destroy(tree);
}

// Fuzz and conservation. Long random sequences of valid operations. After every step the tree passes its
// invariants, and the panel-id universe (leaves plus floating) changes only as allowed: only close may remove a
// panel.

enum { FUZZ_POOL = 24, FUZZ_MAX_NODES = 512, FUZZ_STEPS = 400, FUZZ_SEEDS = 16, FUZZ_OP_COUNT = 7 };
enum { FUZZ_MIN_APPLIED = FUZZ_SEEDS * FUZZ_STEPS / 2 };

// What the tree held before a step: its panel universe, its node ids, its docked panels and the panel ids still
// free to be introduced.
typedef struct FuzzView {
    FlDockPanelId universe[FUZZ_POOL * 2];
    u32 universe_count;
    FlDockNodeId leaves[FUZZ_MAX_NODES];
    u32 leaf_count;
    FlDockNodeId splits[FUZZ_MAX_NODES];
    u32 split_count;
    FlDockPanelId docked[FUZZ_POOL * 2];
    u32 docked_count;
    FlDockPanelId available[FUZZ_POOL];
    u32 available_count;
} FuzzView;

typedef struct FuzzSet {
    const FlDockPanelId* ids;
    u32 count;
} FuzzSet;

static u32 dock_rng_next(u32* state) {
    u32 x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x ? x : 1;
    return *state;
}

static void dock_gather_nodes(const FlDockNode* node, FuzzView* view) {
    if (node == nullptr) {
        return;
    }
    if (node->kind == FlDockNodeKind_Leaf) {
        view->leaves[view->leaf_count++] = node->id;
        return;
    }
    view->splits[view->split_count++] = node->id;
    dock_gather_nodes(node->split.child[0], view);
    dock_gather_nodes(node->split.child[1], view);
}

static bool dock_contains(const FlDockPanelId* arr, u32 n, FlDockPanelId id) {
    for (u32 i = 0; i < n; ++i) {
        if (arr[i] == id) {
            return true;
        }
    }
    return false;
}

static void dock_fuzz_collect_docked(const FlDockTree* tree, FuzzView* view) {
    for (u32 i = 0; i < view->leaf_count; ++i) {
        const FlDockNode* leaf = dock_tree_find_node(tree, view->leaves[i]);
        memcpy(&view->docked[view->docked_count], leaf->leaf.panels, leaf->leaf.count * sizeof(leaf->leaf.panels[0]));
        view->docked_count += leaf->leaf.count;
    }
}

static void dock_fuzz_collect_available(FuzzView* view) {
    for (u32 p = 1; p <= FUZZ_POOL; ++p) {
        if (!dock_contains(view->universe, view->universe_count, p)) {
            view->available[view->available_count++] = p;
        }
    }
}

static void dock_fuzz_observe(FlDockTree* tree, FuzzView* view) {
    *view = (FuzzView) { 0 };
    view->universe_count = dock_tree_collect_panels(tree, view->universe, FUZZ_POOL * 2);
    dock_gather_nodes(tree->root, view);
    dock_fuzz_collect_docked(tree, view);
    dock_fuzz_collect_available(view);
}

// A panel id to dock: a new one, which grows the universe, or one that is already in it. 0 when there is none.
static FlDockPanelId dock_fuzz_pick_panel(const FuzzView* view, u32* rng) {
    const bool take_new = view->available_count > 0 && (view->universe_count == 0 || (dock_rng_next(rng) & 1));
    if (take_new) {
        return view->available[dock_rng_next(rng) % view->available_count];
    }
    if (view->universe_count > 0) {
        return view->universe[dock_rng_next(rng) % view->universe_count];
    }
    return 0;
}

static bool dock_fuzz_op_tab(const FuzzView* view, FlDockPanelId panel, u32* rng, FlDockRequest* req) {
    if (view->leaf_count == 0 || panel == 0) {
        return false;
    }
    *req = (FlDockRequest) { .kind = FlDockRequest_Tab, .panel = panel };
    req->target_node = view->leaves[dock_rng_next(rng) % view->leaf_count];
    return true;
}

static bool dock_fuzz_op_split(const FuzzView* view, FlDockPanelId panel, u32* rng, FlDockRequest* req) {
    const u32 total = view->leaf_count + view->split_count;
    if (total == 0 || panel == 0) {
        return false;
    }
    *req = (FlDockRequest) { .kind = FlDockRequest_Split, .panel = panel };
    const u32 pick = dock_rng_next(rng) % total;
    req->target_node = pick < view->leaf_count ? view->leaves[pick] : view->splits[pick - view->leaf_count];
    req->side = (FlDockSide)(dock_rng_next(rng) % 4);
    return true;
}

static bool dock_fuzz_op_outer(const FuzzView* view, FlDockPanelId panel, u32* rng, FlDockRequest* req) {
    (void)view;
    if (panel == 0) {
        return false;
    }
    *req = (FlDockRequest) { .kind = FlDockRequest_Outer, .panel = panel };
    req->side = (FlDockSide)(dock_rng_next(rng) % 4);
    return true;
}

// A request of kind on a panel drawn from ids. False when ids is empty.
static bool dock_fuzz_panel_request(FlDockRequestKind kind, FuzzSet set, u32* rng, FlDockRequest* req) {
    if (set.count == 0) {
        return false;
    }
    *req = (FlDockRequest) { .kind = kind };
    req->panel = set.ids[dock_rng_next(rng) % set.count];
    return true;
}

static bool dock_fuzz_op_undock(const FuzzView* view, FlDockPanelId panel, u32* rng, FlDockRequest* req) {
    (void)panel;
    return dock_fuzz_panel_request(FlDockRequest_Undock, (FuzzSet) { view->docked, view->docked_count }, rng, req);
}

static bool dock_fuzz_op_reorder(const FuzzView* view, FlDockPanelId panel, u32* rng, FlDockRequest* req) {
    (void)panel;
    if (!dock_fuzz_panel_request(FlDockRequest_ReorderTab, (FuzzSet) { view->docked, view->docked_count }, rng, req)) {
        return false;
    }
    req->to_index = dock_rng_next(rng) % FUZZ_POOL;
    return true;
}

static bool dock_fuzz_op_close(const FuzzView* view, FlDockPanelId panel, u32* rng, FlDockRequest* req) {
    (void)panel;
    return dock_fuzz_panel_request(FlDockRequest_Close, (FuzzSet) { view->universe, view->universe_count }, rng, req);
}

static bool dock_fuzz_op_resize(const FuzzView* view, FlDockPanelId panel, u32* rng, FlDockRequest* req) {
    (void)panel;
    if (view->split_count == 0) {
        return false;
    }
    *req = (FlDockRequest) { .kind = FlDockRequest_SplitterResize };
    req->target_node = view->splits[dock_rng_next(rng) % view->split_count];
    req->ratio = (f32)(dock_rng_next(rng) % 1000) / 1000.0f;
    return true;
}

typedef bool (*DockFuzzOp)(const FuzzView*, FlDockPanelId, u32*, FlDockRequest*);

static const DockFuzzOp k_fuzz_ops[FUZZ_OP_COUNT] = {
    dock_fuzz_op_tab,     dock_fuzz_op_split, dock_fuzz_op_outer,  dock_fuzz_op_undock,
    dock_fuzz_op_reorder, dock_fuzz_op_close, dock_fuzz_op_resize,
};

// Every panel of sub is in super.
static void dock_fuzz_expect_subset(int* utest_result, FuzzSet sub, FuzzSet super, const char* msg) {
    for (u32 i = 0; i < sub.count; ++i) {
        ASSERT_TRUE_MSG(dock_contains(super.ids, super.count, sub.ids[i]), msg);
    }
}

// The panel universe after a step: a close removes exactly one panel, any other op removes none.
static void dock_fuzz_expect_conserved(int* utest_result, const FuzzView* before, FlDockTree* tree, bool closed) {
    FlDockPanelId after[FUZZ_POOL * 2];
    const u32 after_count = dock_tree_collect_panels(tree, after, FUZZ_POOL * 2);
    const FuzzSet was = { before->universe, before->universe_count };
    const FuzzSet now = { after, after_count };
    if (closed) {
        // The closed panel is drawn from the universe, so close always succeeds and removes exactly one.
        ASSERT_TRUE_MSG(after_count + 1 == before->universe_count, "close must remove exactly one panel");
        dock_fuzz_expect_subset(utest_result, now, was, "close invented a panel");
        return;
    }
    dock_fuzz_expect_subset(utest_result, was, now, "a non-close op removed a panel (conservation violated)");
}

// One random op against tree. Counts it in applied when the tree accepted it.
static void dock_fuzz_step(int* utest_result, FlDockTree* tree, u32* rng, u32* applied) {
    FuzzView view;
    dock_fuzz_observe(tree, &view);
    const FlDockPanelId panel = dock_fuzz_pick_panel(&view, rng);

    FlDockRequest req = { 0 };
    const DockFuzzOp op = k_fuzz_ops[dock_rng_next(rng) % FUZZ_OP_COUNT];
    if (!op(&view, panel, rng, &req)) {
        return; // this state has no valid op of the chosen kind
    }
    *applied += dock_tree_apply(tree, &req);

    ASSERT_TRUE_MSG(dock_tree_check_invariants(tree), "dock tree invariants broken during fuzz");
    dock_fuzz_expect_conserved(utest_result, &view, tree, req.kind == FlDockRequest_Close);
}

// Runs one seed's sequence and returns how many of its ops the tree applied.
static u32 dock_fuzz_run_seed(int* utest_result, u32 seed) {
    u32 rng = seed * 2654435761u + 1u;
    u32 applied = 0;
    FlDockTree* tree = dock_tree_create();
    for (u32 step = 0; step < FUZZ_STEPS && *utest_result == UTEST_TEST_PASSED; ++step) {
        dock_fuzz_step(utest_result, tree, &rng, &applied);
    }
    dock_tree_destroy(tree);
    return applied;
}

UTEST(dock_tree, fuzz_conservation) {
    u32 applied = 0;
    for (u32 seed = 1; seed <= FUZZ_SEEDS; ++seed) {
        applied += dock_fuzz_run_seed(utest_result, seed);
        DOCK_STOP_ON_FAILURE();
    }
    ASSERT_GE_MSG(applied, (u32)FUZZ_MIN_APPLIED, "the fuzz must apply a real share of its operations");
}
