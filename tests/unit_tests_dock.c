// docking tests: tree, layout, animation, drag, splitter, workspace, layout literals and persistence

#include "unit_tests_common.h"
#include "ui/ui_popup.h"
#include <float.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dock tree - the retained logical dock tree, the seven pure operations and the invariant chokepoint
// (ui/dock/dock_tree.{h,c}). Pure data + algebra, no rendering: a dedicated tree per case, invariants asserted
// after every mutation.

// The first panel of a leaf node (0 if empty) - keeps the topology assertions terse.
static FlDockPanelId leaf_panel0(const FlDockNode* leaf) {
    return leaf->leaf.count > 0 ? leaf->leaf.panels[0] : 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// An arena released when the scope ends, on every exit path.
static void dock_arena_release(FlArena** arena) {
    fl_arena_destroy(*arena);
}

#define DOCK_ARENA(var) __attribute__((cleanup(dock_arena_release))) FlArena* var = arena_new()

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Two single-panel leaves, a and b, in an even X split.
static FlDockTree* dock_pair_tree(FlDockPanelId a, FlDockPanelId b) {
    return dock_tree_build(dock_split(FlDockAxis_X, FL_DOCK_SPLIT_RATIO_DEFAULT, dock_leaf(a), dock_leaf(b)));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Docks panel as a tab into target_node; true when it applied and left the tree invariant-clean.
static bool dock_tab_into(FlDockTree* tree, FlDockNodeId target_node, FlDockPanelId panel) {
    FlDockRequest req = { .kind = FlDockRequest_Tab, .panel = panel, .target_node = target_node };
    return dock_tree_apply(tree, &req) && dock_tree_check_invariants(tree);
}

UTEST(dock_tree, tab) {
    FlDockTree* tree = dock_tree_create();
    FlDockNodeId root = tree->root->id;
    ASSERT_TRUE(dock_tree_check_invariants(tree)); // fresh tree: a single empty root leaf (the floor)
    ASSERT_EQ((int)tree->root->kind, (int)FlDockNodeKind_Leaf);
    ASSERT_EQ((int)tree->root->leaf.count, 0);

    ASSERT_TRUE(dock_tab_into(tree, root, 1001));
    ASSERT_TRUE(dock_tab_into(tree, root, 1002));
    ASSERT_EQ((int)tree->root->kind, (int)FlDockNodeKind_Leaf);
    ASSERT_EQ((int)tree->root->leaf.count, 2);
    ASSERT_EQ((int)tree->root->leaf.selected, 1); // a freshly added tab becomes selected

    // Re-docking a panel already present just re-selects it; no duplicate appears.
    FlDockRequest again = { .kind = FlDockRequest_Tab, .panel = 1001, .target_node = root };
    ASSERT_TRUE(dock_tree_apply(tree, &again));
    ASSERT_EQ((int)tree->root->leaf.count, 2);
    ASSERT_EQ((int)tree->root->leaf.selected, 0);
    ASSERT_TRUE(dock_tree_check_invariants(tree));

    // A Tab request whose target is a split node (not a leaf) is rejected.
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

    // Split the root leaf to the Right: root becomes an X split; the original leaf is child[0], the new leaf
    // (holding the new panel) is child[1].
    FlDockRequest right = { .kind = FlDockRequest_Split, .panel = 2002, .target_node = root, .side = FlDockSide_Right };
    ASSERT_TRUE(dock_tree_apply(tree, &right));
    ASSERT_TRUE(dock_tree_check_invariants(tree));
    ASSERT_EQ((int)tree->root->kind, (int)FlDockNodeKind_Split);
    ASSERT_EQ((int)tree->root->split.axis, (int)FlDockAxis_X);
    ASSERT_EQ((int)tree->root->split.child[0]->id, (int)root);
    ASSERT_EQ((int)leaf_panel0(tree->root->split.child[1]), 2002);

    // A Top split produces a Y axis with the new leaf as child[0] (the top side).
    FlDockTree* t2 = dock_tree_create();
    FlDockNodeId r2 = t2->root->id;
    ASSERT_TRUE(dock_tab_into(t2, r2, 5));
    FlDockRequest top = { .kind = FlDockRequest_Split, .panel = 6, .target_node = r2, .side = FlDockSide_Top };
    ASSERT_TRUE(dock_tree_apply(t2, &top));
    ASSERT_TRUE(dock_tree_check_invariants(t2));
    ASSERT_EQ((int)t2->root->split.axis, (int)FlDockAxis_Y);
    ASSERT_EQ((int)leaf_panel0(t2->root->split.child[0]), 6); // top side is child[0]

    dock_tree_destroy(t2);
    dock_tree_destroy(tree);
}

// A split request's ratio is the new leaf's share of the split. An unset ratio keeps the even split.
UTEST(dock_tree, split_ratio_rides_the_request) {
    FlDockTree* tree = dock_tree_create();
    FlDockNodeId root = tree->root->id;
    ASSERT_TRUE(dock_tab_into(tree, root, 1));

    FlDockRequest left
        = { .kind = FlDockRequest_Split, .panel = 2, .target_node = root, .side = FlDockSide_Left, .ratio = 0.35f };
    ASSERT_TRUE(dock_tree_apply(tree, &left));
    ASSERT_TRUE(dock_tree_check_invariants(tree));
    ASSERT_NEAR_MSG(tree->root->split.ratio, 0.35f, 1e-5f, "the new leaf is child[0], so the ratio is its share");

    // On the far side the same share lands as the complement: the new leaf is child[1].
    FlDockTree* t2 = dock_tree_create();
    FlDockNodeId r2 = t2->root->id;
    ASSERT_TRUE(dock_tab_into(t2, r2, 1));
    FlDockRequest bottom
        = { .kind = FlDockRequest_Split, .panel = 2, .target_node = r2, .side = FlDockSide_Bottom, .ratio = 0.35f };
    ASSERT_TRUE(dock_tree_apply(t2, &bottom));
    ASSERT_NEAR(t2->root->split.ratio, 0.65f, 1e-5f);

    FlDockTree* t3 = dock_tree_create();
    FlDockNodeId r3 = t3->root->id;
    ASSERT_TRUE(dock_tab_into(t3, r3, 1));
    FlDockRequest plain = { .kind = FlDockRequest_Split, .panel = 2, .target_node = r3, .side = FlDockSide_Right };
    ASSERT_TRUE(dock_tree_apply(t3, &plain));
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
    // Outer against a fresh (empty) tree degenerates to dropping the first panel into the root leaf.
    FlDockTree* tree = dock_tree_create();
    FlDockRequest first = { .kind = FlDockRequest_Outer, .panel = 100, .side = FlDockSide_Left };
    ASSERT_TRUE(dock_tree_apply(tree, &first));
    ASSERT_TRUE(dock_tree_check_invariants(tree));
    ASSERT_EQ((int)tree->root->kind, (int)FlDockNodeKind_Leaf);
    ASSERT_EQ((int)tree->root->leaf.count, 1);

    // A second panel docked Outer/Bottom splits the whole workspace: Y axis, the new leaf on the bottom side.
    FlDockRequest bottom = { .kind = FlDockRequest_Outer, .panel = 200, .side = FlDockSide_Bottom };
    ASSERT_TRUE(dock_tree_apply(tree, &bottom));
    ASSERT_TRUE(dock_tree_check_invariants(tree));
    ASSERT_EQ((int)tree->root->kind, (int)FlDockNodeKind_Split);
    ASSERT_EQ((int)tree->root->split.axis, (int)FlDockAxis_Y);
    ASSERT_EQ((int)leaf_panel0(tree->root->split.child[1]), 200); // bottom side is child[1]

    dock_tree_destroy(tree);
}

UTEST(dock_tree, undock) {
    // split(leafA[10], leafB[20]); undocking 10 empties leafA, collapses it, and the root becomes leafB. Panel 10
    // moves to the floating set - still in the universe (conservation), just not docked.
    FlDockTree* tree = dock_pair_tree(10, 20);
    ASSERT_TRUE(tree != nullptr);

    FlDockRequest undock = { .kind = FlDockRequest_Undock, .panel = 10 };
    ASSERT_TRUE(dock_tree_apply(tree, &undock));
    ASSERT_TRUE(dock_tree_check_invariants(tree));
    ASSERT_EQ((int)tree->root->kind, (int)FlDockNodeKind_Leaf);
    ASSERT_EQ((int)leaf_panel0(tree->root), 20);
    ASSERT_TRUE(dock_tree_panel_is_floating(tree, 10));
    ASSERT_EQ((int)dock_tree_collect_panels(tree, nullptr, 0), 2); // both panels still in the universe

    // Undocking an already-floating (or absent) panel is a no-op failure.
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
    ASSERT_EQ((int)tree->root->leaf.panels[0], 20);
    ASSERT_EQ((int)tree->root->leaf.panels[1], 30);
    ASSERT_EQ((int)tree->root->leaf.panels[2], 10);
    ASSERT_EQ((int)tree->root->leaf.selected, 1); // selection follows panel 30, now at index 1

    dock_tree_destroy(tree);
}

UTEST(dock_tree, close) {
    // split(leafA[100], leafB[200,201]). Close 100 -> leafA empties, its single-child split dissolves, leafB
    // absorbs the slot and becomes the root.
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
    ASSERT_EQ((int)tree->root->kind, (int)FlDockNodeKind_Leaf);
    ASSERT_EQ((int)tree->root->id, (int)leaf_b);
    ASSERT_EQ((int)tree->root->leaf.count, 2);
    ASSERT_EQ((int)dock_tree_collect_panels(tree, nullptr, 0), 2);

    // Closing the remaining panels empties the root leaf, which (being the floor) stays an empty leaf.
    FlDockRequest close200 = { .kind = FlDockRequest_Close, .panel = 200 };
    FlDockRequest close201 = { .kind = FlDockRequest_Close, .panel = 201 };
    ASSERT_TRUE(dock_tree_apply(tree, &close200));
    ASSERT_TRUE(dock_tree_apply(tree, &close201));
    ASSERT_TRUE(dock_tree_check_invariants(tree));
    ASSERT_EQ((int)tree->root->kind, (int)FlDockNodeKind_Leaf);
    ASSERT_EQ((int)tree->root->leaf.count, 0);
    ASSERT_EQ((int)dock_tree_collect_panels(tree, nullptr, 0), 0);

    dock_tree_destroy(tree);
}

UTEST(dock_tree, close_selected_tab_adjust) {
    FlDockTree* tree = dock_tree_create();
    FlDockNodeId root = tree->root->id;
    ASSERT_TRUE(dock_tab_into(tree, root, 10));
    ASSERT_TRUE(dock_tab_into(tree, root, 20));
    ASSERT_TRUE(dock_tab_into(tree, root, 30));

    // Select the middle tab (re-docking a present panel re-selects it).
    FlDockRequest sel20 = { .kind = FlDockRequest_Tab, .panel = 20, .target_node = root };
    ASSERT_TRUE(dock_tree_apply(tree, &sel20));
    ASSERT_EQ((int)tree->root->leaf.selected, 1);

    // Closing a tab BEFORE the selected one shifts the selection index down - selection still on panel 20.
    FlDockRequest close10 = { .kind = FlDockRequest_Close, .panel = 10 };
    ASSERT_TRUE(dock_tree_apply(tree, &close10));
    ASSERT_TRUE(dock_tree_check_invariants(tree));
    ASSERT_EQ((int)tree->root->leaf.count, 2);
    ASSERT_EQ((int)tree->root->leaf.selected, 0);
    ASSERT_EQ((int)tree->root->leaf.panels[0], 20);

    // Closing a tab AFTER the selected one leaves the selection index unchanged.
    FlDockRequest close30 = { .kind = FlDockRequest_Close, .panel = 30 };
    ASSERT_TRUE(dock_tree_apply(tree, &close30));
    ASSERT_TRUE(dock_tree_check_invariants(tree));
    ASSERT_EQ((int)tree->root->leaf.count, 1);
    ASSERT_EQ((int)tree->root->leaf.selected, 0);
    ASSERT_EQ((int)tree->root->leaf.panels[0], 20);

    // Closing the SELECTED last tab clamps the selection back into range.
    ASSERT_TRUE(dock_tab_into(tree, root, 40));
    ASSERT_TRUE(dock_tab_into(tree, root, 50));
    ASSERT_EQ((int)tree->root->leaf.selected, 2);
    FlDockRequest close50 = { .kind = FlDockRequest_Close, .panel = 50 };
    ASSERT_TRUE(dock_tree_apply(tree, &close50));
    ASSERT_TRUE(dock_tree_check_invariants(tree));
    ASSERT_EQ((int)tree->root->leaf.count, 2);
    ASSERT_EQ((int)tree->root->leaf.selected, 1);
    ASSERT_EQ((int)tree->root->leaf.panels[1], 40);

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

    // Out-of-range ratios clamp to the min-size floor, keeping the ratio strictly inside (0,1).
    resize.ratio = 0.999f;
    ASSERT_TRUE(dock_tree_apply(tree, &resize));
    ASSERT_TRUE(tree->root->split.ratio <= 1.0f - FL_DOCK_MIN_RATIO + 0.0001f);
    resize.ratio = -1.0f;
    ASSERT_TRUE(dock_tree_apply(tree, &resize));
    ASSERT_TRUE(tree->root->split.ratio >= FL_DOCK_MIN_RATIO - 0.0001f);
    ASSERT_TRUE(dock_tree_check_invariants(tree));

    // Resizing a leaf (not a split) is rejected.
    FlDockRequest bad = { .kind = FlDockRequest_SplitterResize, .target_node = leaf_id, .ratio = 0.5f };
    ASSERT_FALSE(dock_tree_apply(tree, &bad));

    dock_tree_destroy(tree);
}

// ---- Fuzz / conservation -------------------------------------------------------------------------------------
// Long randomized sequences of valid operations. After EVERY step the tree must stay invariant-clean, and the
// panel-id universe (leaves ∪ floating) must obey the conservation law: only close may remove a panel.

static u32 dock_rng_next(u32* state) {
    u32 x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x ? x : 1;
    return *state;
}

static void dock_gather_nodes(FlDockNode* node, FlDockNodeId* leaves, u32* nleaf, FlDockNodeId* splits, u32* nsplit) {
    if (node == nullptr) {
        return;
    }
    if (node->kind == FlDockNodeKind_Leaf) {
        leaves[(*nleaf)++] = node->id;
    } else {
        splits[(*nsplit)++] = node->id;
        dock_gather_nodes(node->split.child[0], leaves, nleaf, splits, nsplit);
        dock_gather_nodes(node->split.child[1], leaves, nleaf, splits, nsplit);
    }
}

static bool dock_contains(const FlDockPanelId* arr, u32 n, FlDockPanelId id) {
    for (u32 i = 0; i < n; ++i) {
        if (arr[i] == id) {
            return true;
        }
    }
    return false;
}

UTEST(dock_tree, fuzz_conservation) {
    enum { POOL = 24, MAX_NODES = 512, STEPS = 400, SEEDS = 16, MIN_APPLIED = SEEDS * STEPS / 2 };

    u32 applied = 0;
    for (u32 seed = 1; seed <= SEEDS; ++seed) {
        u32 rng = seed * 2654435761u + 1u;
        FlDockTree* tree = dock_tree_create();

        for (u32 step = 0; step < STEPS; ++step) {
            FlDockPanelId before[POOL * 2];
            u32 n_before = dock_tree_collect_panels(tree, before, POOL * 2);

            FlDockNodeId leaves[MAX_NODES];
            FlDockNodeId splits[MAX_NODES];
            u32 nleaf = 0, nsplit = 0;
            dock_gather_nodes(tree->root, leaves, &nleaf, splits, &nsplit);

            FlDockPanelId docked[POOL * 2];
            u32 ndock = 0;
            for (u32 i = 0; i < nleaf; ++i) {
                FlDockNode* ln = dock_tree_find_node(tree, leaves[i]);
                for (u32 j = 0; j < ln->leaf.count; ++j) {
                    docked[ndock++] = ln->leaf.panels[j];
                }
            }

            FlDockPanelId available[POOL];
            u32 navail = 0;
            for (u32 p = 1; p <= POOL; ++p) {
                if (!dock_contains(before, n_before, p)) {
                    available[navail++] = p;
                }
            }

            // Pick a panel id to dock: either introduce a new one (grows the universe - allowed) or move one
            // that is already in the universe (conserved).
            FlDockPanelId dock_panel = 0;
            if (navail > 0 && (n_before == 0 || (dock_rng_next(&rng) & 1))) {
                dock_panel = available[dock_rng_next(&rng) % navail];
            } else if (n_before > 0) {
                dock_panel = before[dock_rng_next(&rng) % n_before];
            }

            FlDockRequest req = { 0 };
            bool assembled = false;
            u32 op = dock_rng_next(&rng) % 7;
            switch (op) {
                case 0:
                    if (nleaf > 0 && dock_panel != 0) {
                        req.kind = FlDockRequest_Tab;
                        req.panel = dock_panel;
                        req.target_node = leaves[dock_rng_next(&rng) % nleaf];
                        assembled = true;
                    }
                    break;
                case 1:
                    if ((nleaf + nsplit) > 0 && dock_panel != 0) {
                        req.kind = FlDockRequest_Split;
                        req.panel = dock_panel;
                        u32 total = nleaf + nsplit;
                        u32 pick = dock_rng_next(&rng) % total;
                        req.target_node = pick < nleaf ? leaves[pick] : splits[pick - nleaf];
                        req.side = (FlDockSide)(dock_rng_next(&rng) % 4);
                        assembled = true;
                    }
                    break;
                case 2:
                    if (dock_panel != 0) {
                        req.kind = FlDockRequest_Outer;
                        req.panel = dock_panel;
                        req.side = (FlDockSide)(dock_rng_next(&rng) % 4);
                        assembled = true;
                    }
                    break;
                case 3:
                    if (ndock > 0) {
                        req.kind = FlDockRequest_Undock;
                        req.panel = docked[dock_rng_next(&rng) % ndock];
                        assembled = true;
                    }
                    break;
                case 4:
                    if (ndock > 0) {
                        req.kind = FlDockRequest_ReorderTab;
                        req.panel = docked[dock_rng_next(&rng) % ndock];
                        req.to_index = dock_rng_next(&rng) % POOL;
                        assembled = true;
                    }
                    break;
                case 5:
                    if (n_before > 0) {
                        req.kind = FlDockRequest_Close;
                        req.panel = before[dock_rng_next(&rng) % n_before];
                        assembled = true;
                    }
                    break;
                case 6:
                    if (nsplit > 0) {
                        req.kind = FlDockRequest_SplitterResize;
                        req.target_node = splits[dock_rng_next(&rng) % nsplit];
                        req.ratio = (f32)(dock_rng_next(&rng) % 1000) / 1000.0f;
                        assembled = true;
                    }
                    break;
                default:
                    break;
            }

            if (!assembled) {
                continue; // no valid op of the chosen kind for this state - skip the step
            }
            applied += dock_tree_apply(tree, &req);

            ASSERT_TRUE_MSG(dock_tree_check_invariants(tree), "dock tree invariants broken during fuzz");

            FlDockPanelId after[POOL * 2];
            u32 n_after = dock_tree_collect_panels(tree, after, POOL * 2);
            if (req.kind == FlDockRequest_Close) {
                // The closed panel is drawn from before, so close always succeeds and removes EXACTLY one.
                ASSERT_TRUE_MSG(n_after + 1 == n_before, "close must remove exactly one panel");
                for (u32 i = 0; i < n_after; ++i) {
                    ASSERT_TRUE_MSG(dock_contains(before, n_before, after[i]), "close invented a panel");
                }
            } else {
                for (u32 i = 0; i < n_before; ++i) {
                    ASSERT_TRUE_MSG(dock_contains(after, n_after, before[i]),
                                    "a non-close op removed a panel (conservation violated)");
                }
            }
        }

        dock_tree_destroy(tree);
    }
    ASSERT_GE_MSG(applied, (u32)MIN_APPLIED, "the fuzz must apply a real share of its operations");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dock layout - the logical tree → Clay emit + node-rect readback (ui/dock/dock_layout.{h,c}).

#define DOCK_ASSERT_FLUSH(a, b) ASSERT_NEAR_MSG((a), (b), 0.5f, "dock rects must tile with no gap or overlap")

UTEST(dock_layout, tile_no_gaps_match_ratios) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    //   S = X-split (ratio 0.25)
    //     child0 = LA  leaf{1}              (left, 25% width)
    //     child1 = T = Y-split (ratio 0.50) (right, 75% width)
    //       child0 = LB leaf{2}            (top, 50% height)
    //       child1 = LC leaf{3}            (bottom, 50% height)
    FlDockTree* tree = dock_tree_build(
        dock_split(FlDockAxis_X, 0.25f, dock_leaf(1), dock_split(FlDockAxis_Y, 0.5f, dock_leaf(2), dock_leaf(3))));
    ASSERT_TRUE(tree != nullptr);
    ASSERT_TRUE(dock_tree_check_invariants(tree));

    FlDockNode* s = tree->root;
    ASSERT_EQ((int)s->kind, (int)FlDockNodeKind_Split);
    ASSERT_EQ((int)s->split.axis, (int)FlDockAxis_X);
    FlDockNode* la = s->split.child[0];
    FlDockNode* t = s->split.child[1];
    ASSERT_EQ((int)t->kind, (int)FlDockNodeKind_Split);
    ASSERT_EQ((int)t->split.axis, (int)FlDockAxis_Y);
    FlDockNode* lb = t->split.child[0];
    FlDockNode* lc = t->split.child[1];
    FlDockNodeId id_s = s->id, id_la = la->id, id_t = t->id, id_lb = lb->id, id_lc = lc->id;

    // Rects are valid only after the layout pass (ui_update_end).
    const f32 w = 800.0f;
    const f32 h = 600.0f;
    ctx->layout_width = w;
    ctx->layout_height = h;
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = w, .height = h });

    ui_update_begin(ctx);
    dock_layout_emit(tree, nullptr);
    ui_update_end(ctx, 1.0f / 60.0f);

    FlDockLayout layout;
    dock_layout_read_rects(tree, tree->arena, &layout);
    ASSERT_EQ((int)layout.count, 5); // S, LA, T, LB, LC

    const FlDockRect* r_s = dock_layout_find_rect(&layout, id_s);
    const FlDockRect* r_la = dock_layout_find_rect(&layout, id_la);
    const FlDockRect* r_t = dock_layout_find_rect(&layout, id_t);
    const FlDockRect* r_lb = dock_layout_find_rect(&layout, id_lb);
    const FlDockRect* r_lc = dock_layout_find_rect(&layout, id_lc);
    ASSERT_TRUE_MSG(r_s && r_la && r_t && r_lb && r_lc, "every node rect must be read back after layout");

    // A divider strip of FL_DOCK_DIVIDER_THICKNESS fills each split's seam: child[0] keeps its exact percent
    // (its fraction of the parent), the divider takes a fixed strip, and child[1] grows into the remainder.
    const f32 d = FL_DOCK_DIVIDER_THICKNESS;

    ASSERT_NEAR(r_s->x, 0.0f, 0.5f);
    ASSERT_NEAR(r_s->y, 0.0f, 0.5f);
    ASSERT_NEAR(r_s->width, w, 0.5f);
    ASSERT_NEAR(r_s->height, h, 0.5f);

    // Expected geometry from the ratios: LA = left 25% (200px exact), then a divider, then T grows into the rest.
    ASSERT_NEAR(r_la->x, 0.0f, 0.5f);
    ASSERT_NEAR(r_la->y, 0.0f, 0.5f);
    ASSERT_NEAR(r_la->width, 200.0f, 0.5f);
    ASSERT_NEAR(r_la->height, 600.0f, 0.5f);

    ASSERT_NEAR(r_t->x, 200.0f + d, 0.5f);
    ASSERT_NEAR(r_t->y, 0.0f, 0.5f);
    ASSERT_NEAR(r_t->width, 600.0f - d, 0.5f);
    ASSERT_NEAR(r_t->height, 600.0f, 0.5f);

    // Inside T (Y split, ratio 0.5): LB = top 50% (300px exact), then a divider, then LC grows into the rest.
    ASSERT_NEAR(r_lb->x, 200.0f + d, 0.5f);
    ASSERT_NEAR(r_lb->y, 0.0f, 0.5f);
    ASSERT_NEAR(r_lb->width, 600.0f - d, 0.5f);
    ASSERT_NEAR(r_lb->height, 300.0f, 0.5f);

    ASSERT_NEAR(r_lc->x, 200.0f + d, 0.5f);
    ASSERT_NEAR(r_lc->y, 300.0f + d, 0.5f);
    ASSERT_NEAR(r_lc->width, 600.0f - d, 0.5f);
    ASSERT_NEAR(r_lc->height, 300.0f - d, 0.5f);

    // Tiling, stated as adjacency invariants, independent of the absolute numbers above.
    DOCK_ASSERT_FLUSH(r_la->x + r_la->width + d, r_t->x);
    DOCK_ASSERT_FLUSH(r_la->x, r_s->x);
    DOCK_ASSERT_FLUSH(r_t->x + r_t->width, r_s->x + r_s->width);
    DOCK_ASSERT_FLUSH(r_la->height, r_s->height);
    DOCK_ASSERT_FLUSH(r_t->height, r_s->height);
    DOCK_ASSERT_FLUSH(r_lb->y + r_lb->height + d, r_lc->y);
    DOCK_ASSERT_FLUSH(r_lb->y, r_t->y);
    DOCK_ASSERT_FLUSH(r_lc->y + r_lc->height, r_t->y + r_t->height);
    DOCK_ASSERT_FLUSH(r_lb->x, r_t->x);
    DOCK_ASSERT_FLUSH(r_lb->width, r_t->width);
    DOCK_ASSERT_FLUSH(r_lc->x, r_t->x);
    DOCK_ASSERT_FLUSH(r_lc->width, r_t->width);

    ASSERT_NEAR(r_la->width / r_s->width, 0.25f, 0.01f);
    ASSERT_NEAR(r_lb->height / r_t->height, 0.5f, 0.01f);

    dock_tree_destroy(tree);
}

#undef DOCK_ASSERT_FLUSH

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The minimum tile floor and the whole-pixel tile edge - both applied at emit, on the extent the previous frame's
// layout produced, and neither ever written back into the tree.

// One direct layout pass over tree into a wxh region: the dock_layout emit path with no workspace around it.
static void dock_layout_frame(FlUiCtx* ctx, FlDockTree* tree, f32 w, f32 h) {
    ctx->layout_width = w;
    ctx->layout_height = h;
    ctx->delta_time = 1.0f / 60.0f; // only the app loop writes this, and these frames do not run one
    Clay_SetLayoutDimensions((Clay_Dimensions) { .width = w, .height = h });
    ui_update_begin(ctx);
    dock_layout_emit(tree, nullptr);
    ui_update_end(ctx, 1.0f / 60.0f);
}

// A two-leaf X split at ratio.
static FlDockTree* dock_two_leaf_split(f32 ratio) {
    return dock_tree_build(dock_split(FlDockAxis_X, ratio, dock_leaf(1), dock_leaf(2)));
}

UTEST(dock_layout, tile_floor_and_pixel_snap_math) {
    const f32 e = 800.0f;
    const f32 d = FL_DOCK_DIVIDER_THICKNESS;
    bool starved = true;

    // With room for both floors only the ends move.
    ASSERT_NEAR(dock_layout_clamp_ratio(0.5f, FlDockAxis_X, e, &starved), 0.5f, 0.0001f);
    ASSERT_FALSE(starved);
    ASSERT_NEAR(dock_layout_clamp_ratio(0.01f, FlDockAxis_X, e, &starved), FL_DOCK_MIN_TILE_WIDTH / e, 0.0001f);
    ASSERT_NEAR(dock_layout_clamp_ratio(0.99f, FlDockAxis_X, e, &starved), 1.0f - (FL_DOCK_MIN_TILE_WIDTH + d) / e,
                0.0001f);
    // A Y split floors on the tile HEIGHT instead.
    ASSERT_NEAR(dock_layout_clamp_ratio(0.0f, FlDockAxis_Y, e, &starved), FL_DOCK_MIN_TILE_HEIGHT / e, 0.0001f);
    ASSERT_FALSE(starved);

    // Under two floors plus the divider the clamp has no solution: both children take half the room, whatever the
    // ratio asked for, and the split reports itself starved.
    const f32 tight = 2.0f * FL_DOCK_MIN_TILE_WIDTH + d - 20.0f;
    f32 first = dock_layout_clamp_ratio(0.2f, FlDockAxis_X, tight, &starved) * tight;
    ASSERT_TRUE_MSG(starved, "a split too small for both floors reports starved");
    ASSERT_NEAR_MSG(first, (tight - d) - first, 0.01f, "both children of a starved split degrade to the same size");
    ASSERT_NEAR(dock_layout_clamp_ratio(0.8f, FlDockAxis_X, tight, nullptr) * tight, first, 0.01f);

    // Frame one - nothing read back yet - is a no-op for both.
    ASSERT_NEAR(dock_layout_clamp_ratio(0.123f, FlDockAxis_X, 0.0f, &starved), 0.123f, 0.0001f);
    ASSERT_FALSE(starved);
    ASSERT_NEAR(dock_layout_snap_ratio(0.123f, 0.0f), 0.123f, 0.0001f);

    // The snap lands child[0]'s far edge on a whole pixel of the extent it was measured against.
    ASSERT_NEAR(dock_layout_snap_ratio(0.5f, 801.0f) * 801.0f, 401.0f, 0.001f); // 400.5 rounds up
    ASSERT_NEAR(dock_layout_snap_ratio(0.3f, 801.0f) * 801.0f, 240.0f, 0.001f); // 240.3 rounds down
    ASSERT_NEAR(dock_layout_snap_ratio(1.0f / 3.0f, 800.0f) * 800.0f, 267.0f, 0.001f);
}

UTEST(dock_layout, shrink_then_re_expand_restores_the_ratios) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlDockTree* tree = dock_tree_build(
        dock_split(FlDockAxis_X, 0.25f, dock_leaf(1), dock_split(FlDockAxis_Y, 0.5f, dock_leaf(2), dock_leaf(3))));
    ASSERT_TRUE(tree != nullptr);

    FlDockNode* s = tree->root;
    FlDockNode* t = s->split.child[1];
    dock_layout_frame(ctx, tree, 800.0f, 600.0f);
    dock_layout_frame(ctx, tree, 800.0f, 600.0f);

    const f32 s_ratio = s->split.ratio;
    const f32 t_ratio = t->split.ratio;

    // Far below every floor, for long enough that any write-back would have had many frames to accumulate.
    for (int i = 0; i < 16; ++i) {
        dock_layout_frame(ctx, tree, 150.0f, 110.0f);
    }
    ASSERT_TRUE_MSG(dock_tree_check_invariants(tree), "starving the layout leaves the tree invariant-clean");

    FlDockLayout squeezed;
    dock_layout_read_rects(tree, tree->arena, &squeezed);
    for (u32 i = 0; i < squeezed.count; ++i) {
        ASSERT_TRUE_MSG(squeezed.nodes[i].found && squeezed.nodes[i].rect.width > 0.0f
                            && squeezed.nodes[i].rect.height > 0.0f,
                        "no node drops out of the layout below the floor — the workspace overflows and clips");
    }

    for (int i = 0; i < 16; ++i) {
        dock_layout_frame(ctx, tree, 800.0f, 600.0f);
    }

    ASSERT_NEAR_MSG(s->split.ratio, s_ratio, 1e-5f, "a resize is not a layout act: the ratios come back unchanged");
    ASSERT_NEAR_MSG(t->split.ratio, t_ratio, 1e-5f, "a resize is not a layout act: the ratios come back unchanged");

    dock_tree_destroy(tree);
}

UTEST(dock_layout, starved_split_degrades_symmetrically_and_reports_once) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // 0.05 of 800px is 40px - well under the floor, so the clamp has to lift it.
    FlDockTree* tree = dock_two_leaf_split(0.05f);
    ASSERT_TRUE(tree != nullptr);
    FlDockNode* s = tree->root;
    FlDockNodeId id_a = s->split.child[0]->id;
    FlDockNodeId id_b = s->split.child[1]->id;

    // Wide enough for both floors: the lopsided ratio is lifted to exactly the minimum tile width.
    for (int i = 0; i < 3; ++i) {
        dock_layout_frame(ctx, tree, 800.0f, 600.0f);
    }
    FlDockLayout wide;
    dock_layout_read_rects(tree, tree->arena, &wide);
    ASSERT_NEAR_MSG(dock_layout_find_rect(&wide, id_a)->width, FL_DOCK_MIN_TILE_WIDTH, 1.0f,
                    "a ratio under the floor is lifted to exactly the minimum tile width");
    ASSERT_FALSE(s->split.starve_logged);

    // Below two floors plus the divider neither child can have its floor, so they share what is left evenly.
    for (int i = 0; i < 3; ++i) {
        dock_layout_frame(ctx, tree, 200.0f, 600.0f);
    }
    FlDockLayout narrow;
    dock_layout_read_rects(tree, tree->arena, &narrow);
    const FlDockRect* a = dock_layout_find_rect(&narrow, id_a);
    const FlDockRect* b = dock_layout_find_rect(&narrow, id_b);
    ASSERT_TRUE_MSG(a != nullptr && b != nullptr, "both leaves keep a tile below the floor");
    ASSERT_NEAR_MSG(a->width, b->width, 1.0f, "both children of a starved split degrade symmetrically");
    ASSERT_TRUE_MSG(s->split.starve_logged, "the starved split latched its one error");

    // The latch is a latch, not a per-frame message, and it re-arms once the room comes back.
    for (int i = 0; i < 3; ++i) {
        dock_layout_frame(ctx, tree, 800.0f, 600.0f);
    }
    ASSERT_FALSE_MSG(s->split.starve_logged, "room coming back clears the latch");

    dock_tree_destroy(tree);
}

UTEST(dock_layout, tile_edges_land_on_whole_pixels) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    FlDockTree* tree = dock_tree_build(
        dock_split(FlDockAxis_X, FL_DOCK_SPLIT_RATIO_DEFAULT, dock_leaf(1),
                   dock_split(FlDockAxis_Y, FL_DOCK_SPLIT_RATIO_DEFAULT, dock_leaf(2), dock_leaf(3))));
    ASSERT_TRUE(tree != nullptr);

    // Odd extents and half ratios: every edge wants to land between two pixels.
    const f32 w = 801.0f;
    const f32 h = 601.0f;
    // Prime the extent read-back: the first frame has no geometry at all, and the second only the outer split's.
    dock_layout_frame(ctx, tree, w, h);
    dock_layout_frame(ctx, tree, w, h);
    dock_layout_frame(ctx, tree, w, h);

    for (int frame = 0; frame < 10; ++frame) {
        FlDockLayout layout;
        dock_layout_read_rects(tree, tree->arena, &layout);
        for (u32 i = 0; i < layout.count; ++i) {
            const FlDockRect* r = &layout.nodes[i].rect;
            ASSERT_TRUE_MSG(layout.nodes[i].found, "every node laid out");
            ASSERT_NEAR_MSG(r->x, roundf(r->x), 0.01f, "a tile's left edge lands on a whole pixel");
            ASSERT_NEAR_MSG(r->y, roundf(r->y), 0.01f, "a tile's top edge lands on a whole pixel");
            ASSERT_NEAR_MSG(r->x + r->width, roundf(r->x + r->width), 0.01f, "and so does its right edge");
            ASSERT_NEAR_MSG(r->y + r->height, roundf(r->y + r->height), 0.01f, "and its bottom edge");
        }
        dock_layout_frame(ctx, tree, w, h);
    }

    dock_tree_destroy(tree);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dock tree structural changes as seen through the layout (ui/dock/dock_tree.{h,c}). Every op lands in
// full on the frame it is applied - there is no transitional tier between the tree and what Clay is fed.

// Deep structural equality of two trees.
static bool dock_logical_equal(const FlDockNode* a, const FlDockNode* b) {
    if (a == nullptr || b == nullptr) {
        return a == b;
    }
    if (a->kind != b->kind) {
        return false;
    }
    if (a->kind == FlDockNodeKind_Split) {
        if (a->split.axis != b->split.axis || fabsf(a->split.ratio - b->split.ratio) > 1e-5f) {
            return false;
        }
        return dock_logical_equal(a->split.child[0], b->split.child[0])
               && dock_logical_equal(a->split.child[1], b->split.child[1]);
    }
    if (a->leaf.count != b->leaf.count || a->leaf.selected != b->leaf.selected) {
        return false;
    }
    for (u32 i = 0; i < a->leaf.count; ++i) {
        if (a->leaf.panels[i] != b->leaf.panels[i]) {
            return false;
        }
    }
    return true;
}

UTEST(dock_layout, a_collapse_hands_the_survivor_the_merged_rect) {
    FlUiCtx* ctx = render_fixture_get();
    ASSERT_TRUE_MSG(ctx != nullptr, "render fixture creation failed");

    // split(leaf{10} | leaf{20}) at 50/50; closing one panel empties its leaf, which collapses. The survivor takes
    // the whole merged rect on the very next frame - no transitional geometry.
    FlDockTree* tree = dock_pair_tree(10, 20);
    ASSERT_TRUE(tree != nullptr);

    FlDockRequest close = { .kind = FlDockRequest_Close, .panel = 10 };
    ASSERT_TRUE(dock_tree_apply(tree, &close));
    ASSERT_TRUE_MSG(dock_tree_check_invariants(tree), "the tree stays clean through the collapse");
    ASSERT_EQ((int)tree->root->kind, (int)FlDockNodeKind_Leaf);

    dock_layout_frame(ctx, tree, 800.0f, 600.0f);

    FlDockLayout layout;
    dock_layout_read_rects(tree, tree->arena, &layout);
    const FlDockRect* r = dock_layout_find_rect(&layout, tree->root->id);
    ASSERT_TRUE(r != nullptr);
    ASSERT_NEAR_MSG(r->width, 800.0f, 0.5f, "the survivor fills the merged rect immediately");
    ASSERT_NEAR(r->height, 600.0f, 0.5f);

    dock_tree_destroy(tree);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dock drag - the drag-to-dock targeting layer (ui/dock/dock_drag.{h,c}). The centerpiece is the PURE
// hit-test: synthetic node rects + a cursor in, a drop zone + request out - no Clay, no state.

// A single leaf filling the 800x600 workspace: it carries BOTH is_leaf and is_root, so the tab region, the outer
// band and the split quadrants are all offered over the one rect. Its strip is y 0..26, its centre box is x
// 240..560 by y 180..420, and the diagonals cross at (400, 300).
static const FlDockDropNode k_one_leaf_ws[] = {
    { .node = 7, .rect = { 0.0f, 0.0f, 800.0f, 600.0f }, .is_leaf = true, .is_root = true },
};

UTEST(dock_drag, hit_test_resolution_matrix) {
    FlDockRequest req;

    // Corner diagonals over the whole tile, probed at the pinned 20% / 70% of the width.
    FlDockDropZone z = hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 160.0f, 300.0f }, 0, 0, &req);
    ASSERT_EQ_MSG((int)z, (int)FlDockDropZone_Left, "20% across the tile is the left quadrant");
    ASSERT_EQ((int)req.kind, (int)FlDockRequest_Split);
    ASSERT_EQ((int)req.side, (int)FlDockSide_Left);
    ASSERT_EQ((int)req.target_node, 7);
    ASSERT_EQ((int)req.panel, 0); // geometry-only: the caller fills the dragged panel

    z = hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 640.0f, 300.0f }, 0, 0, &req);
    ASSERT_EQ_MSG((int)z, (int)FlDockDropZone_Right, "80% across the tile is the right quadrant");
    ASSERT_EQ((int)req.side, (int)FlDockSide_Right);

    z = hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 400.0f, 150.0f }, 0, 0, &req);
    ASSERT_EQ_MSG((int)z, (int)FlDockDropZone_Top, "above the centre box, below the strip");
    ASSERT_EQ((int)req.side, (int)FlDockSide_Top);

    z = hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 400.0f, 500.0f }, 0, 0, &req);
    ASSERT_EQ((int)z, (int)FlDockDropZone_Bottom);
    ASSERT_EQ((int)req.side, (int)FlDockSide_Bottom);

    // The strip and the centre box, and nothing else, tab the panel in.
    z = hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 400.0f, 10.0f }, 0, 0, &req);
    ASSERT_EQ((int)z, (int)FlDockDropZone_Center);
    ASSERT_EQ((int)req.kind, (int)FlDockRequest_Tab);
    ASSERT_EQ((int)req.target_node, 7);

    z = hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 400.0f, 27.0f }, 0, 0, &req);
    ASSERT_EQ_MSG((int)z, (int)FlDockDropZone_Top, "one px past the 26px strip it splits again");

    z = hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 400.0f, 300.0f }, 0, 0, &req);
    ASSERT_EQ_MSG((int)z, (int)FlDockDropZone_Center, "the middle of the tile tabs in");
    ASSERT_EQ((int)req.kind, (int)FlDockRequest_Tab);
    ASSERT_EQ_MSG((int)req.to_index, (int)FL_DOCK_TAB_INDEX_END, "a centre drop lands at the end of the strip");

    // The centre box is 40% of the tile on each axis, so it scales with the tile rather than holding a px size.
    z = hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 400.0f, 181.0f }, 0, 0, &req);
    ASSERT_EQ_MSG((int)z, (int)FlDockDropZone_Center, "the box opens 30% down the tile");
    z = hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 400.0f, 179.0f }, 0, 0, &req);
    ASSERT_EQ((int)z, (int)FlDockDropZone_Top);
    z = hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 241.0f, 300.0f }, 0, 0, &req);
    ASSERT_EQ((int)z, (int)FlDockDropZone_Center);
    ASSERT_EQ((int)hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 239.0f, 300.0f }, 0, 0, &req),
              (int)FlDockDropZone_Left);

    FlDockDropNode shortish[] = { { .node = 9, .rect = { 0.0f, 0.0f, 400.0f, 200.0f }, .is_leaf = true } };
    z = hit_test_drop_zones(shortish, 1, (FlVec2) { 200.0f, 100.0f }, 0, 0, &req);
    ASSERT_EQ_MSG((int)z, (int)FlDockDropZone_Center, "a 200px tile has a 80px tall centre box");
    z = hit_test_drop_zones(shortish, 1, (FlVec2) { 200.0f, 55.0f }, 0, 0, &req);
    ASSERT_EQ((int)z, (int)FlDockDropZone_Top);
}

// Precedence, both halves of it: the tab region beats the workspace outer band, and the outer band beats the leaf's
// split quadrants.
UTEST(dock_drag, hit_test_precedence) {
    FlDockRequest req;

    FlDockDropZone z = hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 5.0f, 10.0f }, 0, 0, &req);
    ASSERT_EQ_MSG((int)z, (int)FlDockDropZone_Center, "inside the outer band but on the strip: the strip wins");
    ASSERT_EQ((int)req.kind, (int)FlDockRequest_Tab);

    // A tile short enough for its centre box to reach into the outer band resolves the same way.
    FlDockDropNode narrow[] = {
        { .node = 4, .rect = { 0.0f, 0.0f, 300.0f, 60.0f }, .is_leaf = true, .is_root = true },
    };
    z = hit_test_drop_zones(narrow, 1, (FlVec2) { 150.0f, 40.0f }, 0, 0, &req);
    ASSERT_EQ_MSG((int)z, (int)FlDockDropZone_Center, "inside the outer band but in the centre box: the centre wins");

    z = hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 400.0f, 5.0f }, 0, 0, &req);
    ASSERT_EQ_MSG((int)z, (int)FlDockDropZone_Center, "the workspace's top edge is a leaf's strip, not OuterTop");

    z = hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 5.0f, 300.0f }, 0, 0, &req);
    ASSERT_EQ_MSG((int)z, (int)FlDockDropZone_OuterLeft, "below the band the border docks outer, not split-left");
    ASSERT_EQ((int)req.kind, (int)FlDockRequest_Outer);
    ASSERT_EQ((int)req.side, (int)FlDockSide_Left);

    z = hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 795.0f, 300.0f }, 0, 0, &req);
    ASSERT_EQ((int)z, (int)FlDockDropZone_OuterRight);
    ASSERT_EQ((int)req.side, (int)FlDockSide_Right);

    z = hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 400.0f, 590.0f }, 0, 0, &req);
    ASSERT_EQ((int)z, (int)FlDockDropZone_OuterBottom);
    ASSERT_EQ((int)req.side, (int)FlDockSide_Bottom);

    z = hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 30.0f, 300.0f }, 0, 0, &req);
    ASSERT_EQ_MSG((int)z, (int)FlDockDropZone_Left, "one px deeper than the 24px band is the left quadrant again");
    ASSERT_EQ((int)req.kind, (int)FlDockRequest_Split);
}

// Nearest leaf, not containing leaf: splitter gutters have no containing leaf, so None collapses
// to "outside the workspace" and a geometric miss stops existing.
UTEST(dock_drag, hit_test_nearest_leaf) {
    FlDockDropNode nodes[] = {
        { .node = 1, .rect = { 0.0f, 0.0f, 800.0f, 600.0f }, .is_leaf = false, .is_root = true },
        { .node = 2, .rect = { 0.0f, 0.0f, 390.0f, 600.0f }, .is_leaf = true, .is_root = false },
        { .node = 3, .rect = { 410.0f, 0.0f, 390.0f, 600.0f }, .is_leaf = true, .is_root = false },
    };
    FlDockRequest req;

    // Mid-gutter, 8px from the left tile and 12px from the right one.
    FlDockDropZone z = hit_test_drop_zones(nodes, 3, (FlVec2) { 398.0f, 300.0f }, 0, 0, &req);
    ASSERT_EQ_MSG((int)z, (int)FlDockDropZone_Right, "the gutter resolves against the nearer tile");
    ASSERT_EQ_MSG((int)req.target_node, 2, "the nearer leaf, never the split root");

    z = hit_test_drop_zones(nodes, 3, (FlVec2) { 402.0f, 300.0f }, 0, 0, &req);
    ASSERT_EQ((int)z, (int)FlDockDropZone_Left);
    ASSERT_EQ((int)req.target_node, 3);

    // A leaf's own centre box, over the right tile.
    z = hit_test_drop_zones(nodes, 3, (FlVec2) { 600.0f, 300.0f }, 0, 0, &req);
    ASSERT_EQ((int)z, (int)FlDockDropZone_Center);
    ASSERT_EQ((int)req.kind, (int)FlDockRequest_Tab);
    ASSERT_EQ((int)req.target_node, 3);

    // Only leaving the workspace misses, and out_req stays zeroed.
    z = hit_test_drop_zones(nodes, 3, (FlVec2) { -50.0f, -50.0f }, 0, 0, &req);
    ASSERT_EQ((int)z, (int)FlDockDropZone_None);
    ASSERT_EQ((int)req.kind, 0);
    ASSERT_EQ((int)req.target_node, 0);

    z = hit_test_drop_zones(nodes, 3, (FlVec2) { 900.0f, 300.0f }, 0, 0, &req);
    ASSERT_EQ((int)z, (int)FlDockDropZone_None);
}

// The insertion index tracks the cursor along the strip, so the drop chooses where in the tab order the panel
// lands. Out-of-range indices are the dock tree's to clamp.
UTEST(dock_drag, hit_test_tab_insertion_index) {
    FlDockRequest req;
    u32 previous = 0;

    for (f32 x = 0.0f; x < 800.0f; x += 8.0f) {
        FlDockDropZone z = hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { x, 10.0f }, 0, 0, &req);
        ASSERT_EQ((int)z, (int)FlDockDropZone_Center);
        ASSERT_TRUE_MSG(req.to_index >= previous, "the index never walks backwards as the cursor moves right");
        previous = req.to_index;
    }

    ASSERT_EQ((int)hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 10.0f, 10.0f }, 0, 0, &req),
              (int)FlDockDropZone_Center);
    ASSERT_EQ_MSG((int)req.to_index, 0, "the first slot is the front of the strip");
    ASSERT_EQ((int)hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 170.0f, 10.0f }, 0, 0, &req),
              (int)FlDockDropZone_Center);
    ASSERT_EQ((int)req.to_index, 3);
}

// The dock-family gate at drop-resolution: a workspace with a non-zero accept
// filter offers NO zone to a panel whose family it rejects - no highlight, no preview, no commit - while a
// matching (or general-into-unfiltered) panel docks exactly as it would with the gate open.
UTEST(dock_drag, hit_test_family_filter) {
    enum { FAMILY_EFFECT = 2, FAMILY_OTHER = 5 };
    const FlVec2 probes[] = {
        { 400.0f, 10.0f },  // tab strip
        { 160.0f, 300.0f }, // left quadrant
        { 640.0f, 300.0f }, // right quadrant
        { 400.0f, 500.0f }, // bottom quadrant
        { 5.0f, 300.0f },   // outer band
    };
    FlDockRequest req;

    // REJECT: a workspace filtering for FAMILY_EFFECT rejects a FAMILY_OTHER panel - no zone resolves anywhere, and
    // out_req stays zeroed (indistinguishable from leaving the workspace, so the view never previews).
    for (u32 i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i) {
        FlDockDropZone z = hit_test_drop_zones(k_one_leaf_ws, 1, probes[i], FAMILY_OTHER, FAMILY_EFFECT, &req);
        ASSERT_EQ_MSG((int)z, (int)FlDockDropZone_None, "a rejected family must offer no zone");
        ASSERT_EQ((int)req.kind, 0);
        ASSERT_EQ((int)req.target_node, 0);
    }

    FlVec2 band = { 400.0f, 10.0f };
    FlDockDropZone z = hit_test_drop_zones(k_one_leaf_ws, 1, band, FAMILY_EFFECT, FAMILY_EFFECT, &req);
    ASSERT_EQ_MSG((int)z, (int)FlDockDropZone_Center, "matching family docks normally");
    ASSERT_EQ((int)req.kind, (int)FlDockRequest_Tab);
    ASSERT_EQ((int)req.target_node, 7);

    z = hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 640.0f, 300.0f }, 0, 0, &req);
    ASSERT_EQ_MSG((int)z, (int)FlDockDropZone_Right, "the default accept-anywhere case still resolves");
    ASSERT_EQ((int)req.kind, (int)FlDockRequest_Split);

    z = hit_test_drop_zones(k_one_leaf_ws, 1, band, FAMILY_EFFECT, 0, &req);
    ASSERT_EQ_MSG((int)z, (int)FlDockDropZone_Center, "an unfiltered workspace accepts any family");
}

// Switch hysteresis on the controller: a sweep across the Left/Top diagonal - which at y=100 crosses x=133 -
// resolves monotonically: one stable run, one transition 8px past where the disagreement started, no flicker back.
UTEST(dock_drag, resolve_hysteresis) {
    FlDockDrag drag = { 0 };
    drag.active = true;
    drag.panel = 42;
    drag.base[0] = k_one_leaf_ws[0];
    drag.base_count = 1;

    ASSERT_EQ_MSG((int)dock_drag_resolve(&drag, (FlVec2) { 120.0f, 100.0f }), (int)FlDockDropZone_Left,
                  "the first resolution has nothing to stick to");
    ASSERT_EQ((int)drag.request.panel, 42);
    ASSERT_EQ((int)dock_drag_resolve(&drag, (FlVec2) { 132.0f, 100.0f }), (int)FlDockDropZone_Left);

    ASSERT_EQ_MSG((int)dock_drag_resolve(&drag, (FlVec2) { 134.0f, 100.0f }), (int)FlDockDropZone_Left,
                  "2px past the boundary is inside the hysteresis");
    ASSERT_EQ_MSG((int)dock_drag_resolve(&drag, (FlVec2) { 139.0f, 100.0f }), (int)FlDockDropZone_Left,
                  "7px past it still holds");
    ASSERT_EQ_MSG((int)dock_drag_resolve(&drag, (FlVec2) { 140.0f, 100.0f }), (int)FlDockDropZone_Top,
                  "8px past it switches");
    ASSERT_EQ((int)drag.request.side, (int)FlDockSide_Top);

    ASSERT_EQ((int)dock_drag_resolve(&drag, (FlVec2) { 138.0f, 100.0f }), (int)FlDockDropZone_Top);
    ASSERT_EQ_MSG((int)dock_drag_resolve(&drag, (FlVec2) { 131.0f, 100.0f }), (int)FlDockDropZone_Top,
                  "jittering back across the boundary does not flicker the target back");
    ASSERT_EQ_MSG((int)dock_drag_resolve(&drag, (FlVec2) { 130.0f, 100.0f }), (int)FlDockDropZone_Left,
                  "8px past it the other way does switch");
}

// The tab region beats stickiness too: a held target never suppresses a tab-into, however small the movement into
// the region.
UTEST(dock_drag, resolve_tab_region_beats_stickiness) {
    FlDockDrag drag = { 0 };
    drag.active = true;
    drag.panel = 42;
    drag.base[0] = k_one_leaf_ws[0];
    drag.base_count = 1;

    ASSERT_EQ((int)dock_drag_resolve(&drag, (FlVec2) { 400.0f, 174.0f }), (int)FlDockDropZone_Top);
    ASSERT_EQ_MSG((int)dock_drag_resolve(&drag, (FlVec2) { 400.0f, 181.0f }), (int)FlDockDropZone_Center,
                  "7px of movement, well inside the hysteresis, still reaches the centre box");
    ASSERT_EQ((int)drag.request.kind, (int)FlDockRequest_Tab);
}

// Every split the hit-test raises carries the gap's share on the request, so the committed split inherits exactly
// the proportion the gap showed.
UTEST(dock_drag, hit_test_carries_the_gap_ratio) {
    FlDockRequest req;

    ASSERT_EQ((int)hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 160.0f, 300.0f }, 0, 0, &req),
              (int)FlDockDropZone_Left);
    ASSERT_NEAR(req.ratio, FL_DOCK_GAP_RATIO, 1e-6f);

    ASSERT_EQ((int)hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 5.0f, 300.0f }, 0, 0, &req),
              (int)FlDockDropZone_OuterLeft);
    ASSERT_NEAR(req.ratio, FL_DOCK_GAP_RATIO, 1e-6f);

    ASSERT_EQ((int)hit_test_drop_zones(k_one_leaf_ws, 1, (FlVec2) { 400.0f, 300.0f }, 0, 0, &req),
              (int)FlDockDropZone_Center);
    ASSERT_NEAR_MSG(req.ratio, 0.0f, 1e-6f, "a tab creates no split, so it names no ratio");
}

// The sticky-gap rule, one of the two that keep the live hit-test from chasing itself: from inside the slot the
// layout has opened, the cursor pushes the gap around the node it opened in but cannot hand it to another node or
// another kind of dock.
UTEST(dock_drag, sticky_gap_holds_the_target) {
    FlDockDrag drag = { 0 };
    drag.active = true;
    drag.panel = 42;
    drag.base[0] = k_one_leaf_ws[0];
    drag.base_count = 1;

    ASSERT_EQ((int)dock_drag_resolve(&drag, (FlVec2) { 160.0f, 300.0f }), (int)FlDockDropZone_Left);

    // The gap opens over the left third. Inside it sit both the workspace's outer band and the leaf's tab strip -
    // two other kinds of dock the gap must not slide into just because it made room over them.
    drag.gap_rect = (FlDockRect) { 0.0f, 0.0f, 280.0f, 600.0f };
    drag.has_gap_rect = true;
    drag.gap_is_new = true;

    ASSERT_EQ_MSG((int)dock_drag_resolve(&drag, (FlVec2) { 20.0f, 300.0f }), (int)FlDockDropZone_Left,
                  "the outer band inside the gap does not re-target");
    ASSERT_EQ_MSG((int)dock_drag_resolve(&drag, (FlVec2) { 140.0f, 20.0f }), (int)FlDockDropZone_Left,
                  "nor does the tab strip inside the gap");
    ASSERT_EQ((int)drag.request.side, (int)FlDockSide_Left);

    ASSERT_EQ_MSG((int)dock_drag_resolve(&drag, (FlVec2) { 260.0f, 500.0f }), (int)FlDockDropZone_Bottom,
                  "the same node's other side is still reachable from inside the gap");
    ASSERT_EQ((int)drag.request.target_node, 7);

    ASSERT_EQ_MSG((int)dock_drag_resolve(&drag, (FlVec2) { 640.0f, 300.0f }), (int)FlDockDropZone_Right,
                  "leaving the gap resolves freely again");

    // A tab-into displaces nothing, so its "gap" is just the target leaf and never sticks.
    drag.gap_is_new = false;
    ASSERT_EQ_MSG((int)dock_drag_resolve(&drag, (FlVec2) { 140.0f, 20.0f }), (int)FlDockDropZone_Center,
                  "a tab-into gap opened no space, so it holds nothing");
}

// Dragging a tab within its own leaf is a reorder: the undock pulls it out, the drop tabs it back in, and the
// resolved insertion slot walks it to where the cursor was along the strip.
UTEST(dock_drag, within_leaf_drag_reorders) {
    FlDockTree* committed = dock_tree_create();
    FlDockNodeId root = committed->root->id;
    ASSERT_TRUE(dock_tab_into(committed, root, 10));
    ASSERT_TRUE(dock_tab_into(committed, root, 20));
    ASSERT_TRUE(dock_tab_into(committed, root, 30));

    FlDockDrag drag = { 0 };
    dock_drag_begin_undock(&drag, committed, 30, 0, 0);
    ASSERT_TRUE(dock_tree_panel_is_floating(committed, 30));

    drag.zone = FlDockDropZone_Center;
    drag.request = (FlDockRequest) { .kind = FlDockRequest_Tab, .panel = 30, .target_node = root, .to_index = 0 };
    ASSERT_TRUE(dock_drag_end(&drag, committed));

    FlDockNode* leaf = dock_tree_find_leaf_with_panel(committed, 30);
    ASSERT_TRUE(leaf != nullptr);
    ASSERT_EQ((int)leaf->leaf.count, 3);
    ASSERT_EQ_MSG((int)leaf->leaf.panels[0], 30, "the drop's insertion slot puts 30 at the front of the strip");
    ASSERT_EQ((int)leaf->leaf.panels[1], 10);
    ASSERT_EQ((int)leaf->leaf.panels[2], 20);
    ASSERT_TRUE(dock_tree_check_invariants(committed));
    ASSERT_EQ_MSG((int)dock_tree_collect_panels(committed, nullptr, 0), 3, "a reorder conserves the panel universe");

    // And a slot past the end of the strip clamps to the back rather than dropping the panel.
    FlDockDrag again = { 0 };
    dock_drag_begin_undock(&again, committed, 30, 0, 0);
    again.zone = FlDockDropZone_Center;
    again.request = (FlDockRequest) { .kind = FlDockRequest_Tab, .panel = 30, .target_node = root, .to_index = 99 };
    ASSERT_TRUE(dock_drag_end(&again, committed));

    leaf = dock_tree_find_leaf_with_panel(committed, 30);
    ASSERT_EQ((int)leaf->leaf.count, 3);
    ASSERT_EQ((int)leaf->leaf.panels[2], 30);

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
    ASSERT_TRUE_MSG(dock_logical_equal(orig->root, clone->root), "clone must be logically identical to the original");
    ASSERT_EQ((int)clone->next_node_id, (int)orig->next_node_id); // allocator preserved → matching future ids

    FlDockNode* nav_leaf = dock_tree_find_leaf_with_panel(clone, NAV);
    ASSERT_TRUE(nav_leaf != nullptr);
    FlDockRequest req = { .kind = FlDockRequest_Tab, .panel = EXTRA, .target_node = nav_leaf->id };
    ASSERT_TRUE(dock_tree_apply(clone, &req));
    ASSERT_TRUE(dock_tree_check_invariants(clone));
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(clone, EXTRA) != nullptr, "EXTRA must be in the clone");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(orig, EXTRA) == nullptr, "the original must be untouched");

    dock_tree_destroy(clone);
    dock_tree_destroy(orig);
}

// Close the drag-dock loop: releasing over a valid zone commits apply(committed, request) into the live tree, and
// the controller's deferred commit/cancel land at the single safe point dock_drag_end (no mid-walk mutation).
UTEST(dock_drag, end_commits_over_zone) {
    FlDockTree* committed = dock_tree_create();
    FlDockNodeId root = committed->root->id;
    ASSERT_TRUE(dock_tab_into(committed, root, 10));

    FlDockTree* expected = dock_pair_tree(10, 20);
    ASSERT_TRUE(expected != nullptr);

    // Arm the controller and stamp the resolved drop (Right split of the root), as dock_drag_emit would each frame.
    FlDockDrag drag = { 0 };
    drag.active = true;
    drag.panel = 20;
    drag.zone = FlDockDropZone_Right;
    drag.request
        = (FlDockRequest) { .kind = FlDockRequest_Split, .panel = 20, .target_node = root, .side = FlDockSide_Right };

    ASSERT_TRUE(dock_drag_end(&drag, committed));
    ASSERT_FALSE(dock_drag_active(&drag));
    ASSERT_TRUE(dock_tree_check_invariants(committed));
    ASSERT_TRUE_MSG(dock_logical_equal(committed->root, expected->root),
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
    ASSERT_TRUE_MSG(dock_logical_equal(committed->root, control->root), "cancelling must leave the tree unchanged");

    dock_tree_destroy(control);
    dock_tree_destroy(committed);
}

UTEST(dock_drag, undock_then_redock_conserves) {
    // Headless, so arm_drag's snapshot no-ops (no Clay context); the tree mutations are the substance here.
    FlDockTree* committed = dock_pair_tree(10, 20);
    ASSERT_TRUE(committed != nullptr);
    u32 before = dock_tree_collect_panels(committed, nullptr, 0);
    ASSERT_EQ((int)before, 2);

    FlDockDrag drag = { 0 };
    dock_drag_begin_undock(&drag, committed, 10, 0, 0);
    ASSERT_TRUE_MSG(dock_drag_active(&drag), "the undock grab arms a drag");
    ASSERT_TRUE_MSG(dock_tree_panel_is_floating(committed, 10), "the panel is now internal-floating");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(committed, 10) == nullptr, "10 is no longer in a leaf");
    ASSERT_TRUE(dock_tree_check_invariants(committed));
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
    ASSERT_TRUE(dock_tree_check_invariants(committed));
    ASSERT_EQ_MSG((int)dock_tree_collect_panels(committed, nullptr, 0), (int)before,
                  "the whole undock→re-dock round trip conserves the panel universe");

    dock_tree_destroy(committed);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dock splitter - the divider drag mutates a split's ratio through the splitter-resize op
// (ui/dock/dock_splitter.{h,c}). The ratio math is pure (no Clay), so the clamp is tested directly;
// the live drag is then exercised end-to-end through the headless fl_app loop.

UTEST(dock_splitter, ratio_math_and_clamp) {
    const f32 extent = 800.0f;

    ASSERT_NEAR(dock_splitter_ratio_at(200.0f, extent), 0.25f, 0.0001f);
    ASSERT_NEAR(dock_splitter_ratio_at(480.0f, extent), 0.60f, 0.0001f);

    // Both ends clamp to the min-size floor, so neither child can be dragged to zero.
    ASSERT_NEAR(dock_splitter_ratio_at(0.0f, extent), FL_DOCK_MIN_RATIO, 0.0001f);
    ASSERT_NEAR(dock_splitter_ratio_at(-50.0f, extent), FL_DOCK_MIN_RATIO, 0.0001f);
    ASSERT_NEAR(dock_splitter_ratio_at(extent, extent), 1.0f - FL_DOCK_MIN_RATIO, 0.0001f);
    ASSERT_NEAR(dock_splitter_ratio_at(extent + 50.0f, extent), 1.0f - FL_DOCK_MIN_RATIO, 0.0001f);

    // A degenerate (zero) extent - no geometry yet - falls back to a clamped centre rather than dividing by zero.
    ASSERT_NEAR(dock_splitter_ratio_at(0.0f, 0.0f), 0.5f, 0.0001f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared frame fixture for the live cases below. Everything from here on drives the headless fl_app loop, and every
// such frame does the same three things: capture the frame's context and workspace, install a default tree on the
// first frame a workspace exists, then open the workspace and emit its tiles.

typedef struct DockFixture {
    FlUiCtx* ctx;
    FlDockWorkspace* ws;
    bool installed;
} DockFixture;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A headless app that lives for one case. dock_app_close runs on every exit from the case, including an ASSERT
// return, so the app never outlives it and the pointer is never left pressed.
typedef struct DockApp {
    FlApp* app;
} DockApp;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static DockApp dock_app_open(FlString title, i32 width, i32 height) {
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void dock_app_close(DockApp* scope) {
    if (scope->app != nullptr) {
        fl_app_inject_mouse_button(scope->app, FlMouseButton_Left, false);
        fl_app_destroy(scope->app);
    }
}

// Declares `FlApp* app`: a headless window of the given size with a fixed 1/60 s step per tick, destroyed when the
// case returns.
#define DOCK_APP(app, title, width, height)                                                                       \
    __attribute__((cleanup(dock_app_close))) DockApp app##_scope = dock_app_open(FL_S(title), (width), (height)); \
    ASSERT_TRUE_MSG(app##_scope.app != nullptr, "render fixture or headless fl_app_create failed");               \
    FlApp* app = app##_scope.app

// Captures ctx/ws into st, installs spec as the tree the first time a workspace exists, then begins the
// workspace and emits its tiles into a grow/grow area. Returns the workspace, or null before one exists.
// The caller declares any panel bodies and then calls dock_workspace_end on the returned workspace.
static FlDockWorkspace* dock_fixture_emit(DockFixture* st, const FlDockSpec* spec) {
    st->ctx = ui_get_ctx();
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Ticks the app n times with frame.
static void dock_ticks(FlApp* app, void* st, void (*frame)(void*), int n) {
    for (int i = 0; i < n; ++i) {
        fl_app_tick(app, st, frame);
    }
}

// Asserts a two-panel gesture ended with nothing in the holding pen and both panels still in the tree.
#define DOCK_ASSERT_GESTURE_ENDED(tree)                                                              \
    do {                                                                                             \
        ASSERT_EQ_MSG((int)(tree)->floating_count, 0, "nothing is left floating after the gesture"); \
        ASSERT_EQ_MSG((int)dock_tree_collect_panels((tree), nullptr, 0), 2,                          \
                      "both panels are conserved across the gesture");                               \
    } while (0)

// Ticks n frames, then asserts the fixture saw a context and a workspace in them.
#define DOCK_RUN_FRAMES(app, state, fx, frame, n)                                                       \
    do {                                                                                                \
        dock_ticks((app), &(state), (frame), (n));                                                      \
        ASSERT_TRUE_MSG((fx).ctx != nullptr && (fx).ws != nullptr, "frame did not run / no workspace"); \
    } while (0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Ticks until an injected pointer move has settled: it is pumped at a tick boundary and the behaviour pass reads the
// previous frame's hit test, so hover lands two frames late. Four ticks leaves margin.
static void dock_settle_hover(FlApp* app, void* st, void (*frame)(void*)) {
    dock_ticks(app, st, frame, 4);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Hovers (cx, cy) until it goes hot, then presses without releasing - arming the drag latch on the hot element.
static void dock_press_at(FlApp* app, void* st, void (*frame)(void*), i32 cx, i32 cy) {
    fl_app_inject_mouse_move(app, cx, cy);
    dock_settle_hover(app, st, frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, st, frame);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Moves the pointer to (x, y) and ticks n frames.
static void dock_move_and_tick(FlApp* app, void* st, void (*frame)(void*), i32 x, i32 y, int n) {
    fl_app_inject_mouse_move(app, x, y);
    dock_ticks(app, st, frame, n);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Releases the left button and ticks once.
static void dock_release(FlApp* app, void* st, void (*frame)(void*)) {
    fl_app_inject_mouse_button(app, FlMouseButton_Left, false);
    fl_app_tick(app, st, frame);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The element data Clay laid out for a Clay id, or for a layout id.
static Clay_ElementData dock_element(u32 clay_id) {
    return Clay_GetElementData((Clay_ElementId) { .id = clay_id });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static Clay_ElementData dock_layout_element(FlLayoutId id) {
    return dock_element(ui_layout_id_to_clay_id(id));
}

// The Clay element id of leaf leaf_id's index-th tab: name "fl_dock_tab", instance id = leaf_id << 8 folded
// with the index by ui_item_id_seed (see dock_layout.c / ui_tab_strip).
static u32 dock_tab_clay_id(FlDockNodeId leaf_id, u32 index) {
    return ui_layout_id_to_clay_id(
        (FlLayoutId) { .name = FL_S("fl_dock_tab"), .id = ui_item_id_seed((u32)leaf_id << 8, index) });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The centre in px of the leaf's index-th dock tab; false when the tab was not laid out.
static bool dock_tab_centre(const DockFixture* fx, FlDockNodeId leaf_id, u32 index, i32* cx, i32* cy) {
    const ItemState* tab = hashmap_get(&fx->ctx->shared->item_state, dock_tab_clay_id(leaf_id, index));
    if (tab == nullptr || tab->state.size.x <= 0.0f) {
        return false;
    }
    *cx = (i32)(tab->state.pos.x + tab->state.size.x * 0.5f);
    *cy = (i32)(tab->state.pos.y + tab->state.size.y * 0.5f);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Presses and holds the centre of the leaf's index-th dock tab; false when the tab was not laid out.
static bool dock_press_tab(FlApp* app, void* st, const DockFixture* fx, void (*frame)(void*), FlDockNodeId leaf_id,
                           u32 index) {
    i32 cx = 0;
    i32 cy = 0;
    if (!dock_tab_centre(fx, leaf_id, index, &cx, &cy)) {
        return false;
    }
    dock_press_at(app, st, frame, cx, cy);
    return true;
}

// The ratio only ever moves through the splitter-resize op (dock_splitter applies a SplitterResize request; there
// is no direct geometry write anywhere in the path).
static void dock_splitter_drag_frame(void* user_data) {
    // Two single-panel leaves, so the only chrome is the divider between them.
    FlDockWorkspace* ws
        = dock_fixture_emit((DockFixture*)user_data, dock_split(FlDockAxis_X, 0.5f, dock_leaf(1), dock_leaf(2)));
    dock_workspace_end(ws);
}

UTEST(dock_splitter, divider_drag_resizes_through_op) {
    DOCK_APP(app, "dockdrag", 800, 600);
    DockFixture st = { 0 };

    DOCK_RUN_FRAMES(app, st, st, dock_splitter_drag_frame, 1);
    FlDockTree* tree = st.ws->tree;
    ASSERT_EQ_MSG((int)tree->root->kind, (int)FlDockNodeKind_Split, "root must be the X split");
    FlDockNodeId split_id = tree->root->id;
    ASSERT_NEAR(tree->root->split.ratio, 0.5f, 0.01f);

    // Locate the divider element (name "fl_dock_divider", id = split node id; see dock_splitter.c).
    u32 divider_id = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("fl_dock_divider"), .id = (u32)split_id });
    ItemState* divider = hashmap_get(&st.ctx->shared->item_state, divider_id);
    ASSERT_TRUE_MSG(divider != nullptr && divider->state.size.x > 0.0f, "divider was not laid out");
    i32 cx = (i32)(divider->state.pos.x + divider->state.size.x * 0.5f);
    i32 cy = (i32)(divider->state.pos.y + divider->state.size.y * 0.5f);

    dock_press_at(app, &st, dock_splitter_drag_frame, cx, cy);

    // Drag to x = 200 (25% of the 800px split). Past the 4px threshold, so the drag is live; the ratio tracks the
    // cursor's fraction of the split extent.
    dock_move_and_tick(app, &st, dock_splitter_drag_frame, 200, cy, 1);
    ASSERT_NEAR_MSG(tree->root->split.ratio, 0.25f, 0.02f, "divider drag must set the ratio to the cursor fraction");

    // Past the edge the drag stops at the pixel floor, not at the fractional one: 120px of an 800px split.
    dock_move_and_tick(app, &st, dock_splitter_drag_frame, -100, cy, 1);
    ASSERT_NEAR_MSG(tree->root->split.ratio, FL_DOCK_MIN_TILE_WIDTH / 800.0f, 0.005f,
                    "dragging past the near edge clamps to the minimum tile width");

    dock_move_and_tick(app, &st, dock_splitter_drag_frame, 900, cy, 1);
    ASSERT_NEAR_MSG(tree->root->split.ratio, 1.0f - (FL_DOCK_MIN_TILE_WIDTH + FL_DOCK_DIVIDER_THICKNESS) / 800.0f,
                    0.005f, "dragging past the far edge leaves the far child its minimum tile width");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dock workspace - the immediate panel-declaration layer over the retained tree
// (ui/dock/dock_workspace.{h,c}). Pure data + algebra - no Clay frame needed.

UTEST(dock_workspace, binding_and_reservation) {
    FlDockWorkspace* ws = dock_workspace_create();
    FlDockTree* tree = ws->tree;
    FlDockNodeId root = tree->root->id;

    // Hand-build: root leaf {10, 20} (20 selected - a freshly added tab becomes selected), split Right into {30}.
    FlDockRequest tab10 = { .kind = FlDockRequest_Tab, .panel = 10, .target_node = root };
    ASSERT_TRUE(dock_tree_apply(tree, &tab10));
    FlDockRequest tab20 = { .kind = FlDockRequest_Tab, .panel = 20, .target_node = root };
    ASSERT_TRUE(dock_tree_apply(tree, &tab20));
    FlDockRequest split = { .kind = FlDockRequest_Split, .panel = 30, .target_node = root, .side = FlDockSide_Right };
    ASSERT_TRUE(dock_tree_apply(tree, &split));
    ASSERT_TRUE(dock_tree_check_invariants(tree));

    FlDockNode* leaf_ab = dock_tree_find_leaf_with_panel(tree, 10);
    FlDockNode* leaf_c = dock_tree_find_leaf_with_panel(tree, 30);
    ASSERT_TRUE_MSG(leaf_ab != nullptr && leaf_c != nullptr, "both leaves must be found");
    ASSERT_EQ((int)leaf_ab->leaf.panels[leaf_ab->leaf.selected], 20);

    // Binding by stable id: each panel resolves to the leaf node holding it; an absent id resolves to 0 (no node).
    ASSERT_EQ((int)dock_workspace_panel_leaf(ws, 10), (int)leaf_ab->id);
    ASSERT_EQ((int)dock_workspace_panel_leaf(ws, 20), (int)leaf_ab->id);
    ASSERT_EQ((int)dock_workspace_panel_leaf(ws, 30), (int)leaf_c->id);
    ASSERT_EQ((int)dock_workspace_panel_leaf(ws, 999), 0);

    // Frame 1: declare 20 and 30, but deliberately NOT 10 (it stays a reserved background tab).
    dock_workspace_begin(ws);
    bool vis20 = dock_workspace_declare(ws, 20, FL_S("Twenty"));
    bool vis30 = dock_workspace_declare(ws, 30, FL_S("Thirty"));
    dock_workspace_end(ws);

    // The render decision: only a leaf's selected tab emits. 10 is bound but not selected.
    ASSERT_TRUE(vis20);
    ASSERT_TRUE(vis30);
    ASSERT_TRUE(dock_workspace_panel_should_emit(ws, 20));
    ASSERT_TRUE(dock_workspace_panel_should_emit(ws, 30));
    ASSERT_FALSE(dock_workspace_panel_should_emit(ws, 10));

    ASSERT_TRUE(dock_workspace_panel_declared(ws, 20));
    ASSERT_TRUE(dock_workspace_panel_declared(ws, 30));
    ASSERT_FALSE(dock_workspace_panel_declared(ws, 10));

    // Reserved-on-absence: 10 sits in the tree but was not declared → its slot is reserved, not collapsed.
    ASSERT_TRUE(dock_workspace_panel_reserved(ws, 10));
    ASSERT_FALSE(dock_workspace_panel_reserved(ws, 20));
    ASSERT_EQ((int)dock_workspace_panel_leaf(ws, 10), (int)leaf_ab->id);
    ASSERT_EQ((int)leaf_ab->leaf.count, 2);
    ASSERT_TRUE(dock_tree_check_invariants(tree));

    // Frame 2: a fresh generation. Declaring nobody makes every previously-declared panel fall back to reserved.
    dock_workspace_begin(ws);
    dock_workspace_end(ws);
    ASSERT_FALSE(dock_workspace_panel_declared(ws, 20));
    ASSERT_TRUE(dock_workspace_panel_reserved(ws, 20));
    ASSERT_TRUE(dock_workspace_panel_reserved(ws, 30));

    // A panel that is absent from the tree is neither bound nor reserved.
    ASSERT_FALSE(dock_workspace_panel_reserved(ws, 999));

    dock_workspace_destroy(ws);
}

// A panel's title is copied into its record: the declared string is borrowed for the call only, and a reserved panel
// keeps the title from its last declaration.
UTEST(dock_workspace, panel_title_is_copied_and_retained) {
    FlDockWorkspace* ws = dock_workspace_create();
    FlDockTree* tree = ws->tree;
    FlDockRequest tab = { .kind = FlDockRequest_Tab, .panel = 40, .target_node = tree->root->id };
    ASSERT_TRUE(dock_tree_apply(tree, &tab));

    // A caller-owned, non-static buffer - the shape a Rust &str or a formatted app string arrives as.
    char scratch[] = "Inspector";
    dock_workspace_begin(ws);
    dock_workspace_declare(ws, 40, string_from_cstr_len(scratch, 9));
    dock_workspace_end(ws);
    ASSERT_TRUE_MSG(fl_string_equals(dock_workspace_panel_title(ws, 40), FL_S("Inspector")), "the title reads back");

    // Scribble over the caller's bytes. A retained title that aliased them would now read "XXXXXXXXX".
    memset(scratch, 'X', 9);
    ASSERT_TRUE_MSG(fl_string_equals(dock_workspace_panel_title(ws, 40), FL_S("Inspector")),
                    "the title must be a copy — the caller's bytes are borrowed for the call only");

    dock_workspace_begin(ws);
    dock_workspace_end(ws);
    ASSERT_TRUE_MSG(dock_workspace_panel_reserved(ws, 40), "40 is in the tree but undeclared → reserved");
    ASSERT_TRUE_MSG(fl_string_equals(dock_workspace_panel_title(ws, 40), FL_S("Inspector")),
                    "a reserved panel keeps the title from its last declaration");

    // Re-declaring with a different string replaces it; with the same one it is left alone.
    dock_workspace_begin(ws);
    dock_workspace_declare(ws, 40, FL_S("Properties"));
    dock_workspace_declare(ws, 40, FL_S("Properties"));
    dock_workspace_end(ws);
    ASSERT_TRUE_MSG(fl_string_equals(dock_workspace_panel_title(ws, 40), FL_S("Properties")),
                    "a changed title replaces");

    dock_workspace_begin(ws);
    dock_workspace_declare(ws, 40, FL_S(""));
    dock_workspace_end(ws);
    ASSERT_EQ_MSG((int)dock_workspace_panel_title(ws, 40).length, 0, "an empty title clears back to untitled");

    ASSERT_EQ_MSG((int)dock_workspace_panel_title(ws, 999).length, 0, "an unknown panel is untitled");

    dock_workspace_destroy(ws);
}

// A loose (undocked) panel does not emit: the drag overlay draws its title and the body draws nowhere, so the
// emit decision and dock_workspace_declare both answer false for it.
UTEST(dock_workspace, floating_panel_does_not_emit) {
    FlDockWorkspace* ws = dock_workspace_create();
    FlDockTree* tree = ws->tree;

    FlDockRequest tab10 = { .kind = FlDockRequest_Tab, .panel = 10, .target_node = tree->root->id };
    ASSERT_TRUE(dock_tree_apply(tree, &tab10));
    ASSERT_TRUE(dock_workspace_panel_should_emit(ws, 10));

    FlDockRequest undock = { .kind = FlDockRequest_Undock, .panel = 10 };
    ASSERT_TRUE(dock_tree_apply(tree, &undock));
    ASSERT_TRUE(dock_tree_panel_is_floating(tree, 10));
    ASSERT_EQ((int)dock_workspace_panel_leaf(ws, 10), 0);

    ASSERT_FALSE(dock_workspace_panel_should_emit(ws, 10));
    dock_workspace_begin(ws);
    ASSERT_FALSE(dock_workspace_declare(ws, 10, FL_S("Ten")));
    dock_workspace_end(ws);

    ASSERT_FALSE(dock_workspace_panel_should_emit(ws, 999));

    ASSERT_TRUE(dock_tree_check_invariants(tree));
    dock_workspace_destroy(ws);
}

UTEST(dock_workspace, panel_family_tags) {
    FlDockWorkspace* ws = dock_workspace_create();

    // Default: every panel is general until tagged - including ids the workspace has never seen.
    ASSERT_EQ((int)dock_workspace_panel_family(ws, 10), 0);
    ASSERT_EQ((int)dock_workspace_panel_family(ws, 999), 0);

    dock_workspace_set_panel_family(ws, 10, 2);
    dock_workspace_set_panel_family(ws, 20, 5);
    ASSERT_EQ((int)dock_workspace_panel_family(ws, 10), 2);
    ASSERT_EQ((int)dock_workspace_panel_family(ws, 20), 5);
    ASSERT_EQ((int)dock_workspace_panel_family(ws, 30), 0);

    dock_workspace_set_panel_family(ws, 10, 7);
    ASSERT_EQ((int)dock_workspace_panel_family(ws, 10), 7);
    dock_workspace_set_panel_family(ws, 10, 0);
    ASSERT_EQ((int)dock_workspace_panel_family(ws, 10), 0);

    // The workspace's accept filter defaults to 0 (accept any).
    ASSERT_EQ((int)ws->family_filter, 0);

    dock_workspace_destroy(ws);
}

// Visibility is a bit the app sets over the tree, not a control-flow trick: a hidden panel keeps everything the
// tree holds for it and only stops rendering.
UTEST(dock_workspace, hidden_panel_keeps_its_slot_and_tab_index) {
    FlDockWorkspace* ws = dock_workspace_create();
    FlDockTree* tree = ws->tree;

    // Root leaf {10, 20}, 20 selected - a freshly added tab becomes selected.
    ASSERT_TRUE(dock_tab_into(tree, tree->root->id, 10));
    ASSERT_TRUE(dock_tab_into(tree, tree->root->id, 20));
    FlDockNode* leaf = tree->root;

    // Default: everything is shown, including ids the workspace has never seen.
    ASSERT_TRUE(dock_workspace_panel_visible(ws, 10));
    ASSERT_TRUE(dock_workspace_panel_visible(ws, 999));

    dock_workspace_set_panel_visible(ws, 20, false);
    ASSERT_FALSE(dock_workspace_panel_visible(ws, 20));

    // The slot, the tab index and the selection are the tree's, and hiding never touches the tree.
    ASSERT_EQ((int)leaf->leaf.count, 2);
    ASSERT_EQ((int)leaf->leaf.panels[1], 20);
    ASSERT_EQ((int)leaf->leaf.selected, 1);
    ASSERT_TRUE(dock_tree_check_invariants(tree));

    // The app declares hidden panels like any other, so hiding never turns one reserved. A hidden selected tab
    // emits nothing and its leaf falls back to the first shown panel - without rewriting the selection.
    dock_workspace_begin(ws);
    ASSERT_FALSE_MSG(dock_workspace_declare(ws, 20, FL_S("Twenty")), "a hidden panel must not emit");
    ASSERT_TRUE_MSG(dock_workspace_declare(ws, 10, FL_S("Ten")), "the leaf shows the first panel that is not hidden");
    dock_workspace_end(ws);
    ASSERT_TRUE(dock_workspace_panel_declared(ws, 20));
    ASSERT_FALSE_MSG(dock_workspace_panel_reserved(ws, 20), "hiding a declared panel must never reserve it");
    ASSERT_EQ((int)leaf->leaf.selected, 1);

    // A hidden floating panel does not emit either - floating is an exception to selection, not to hiding.
    dock_workspace_set_panel_visible(ws, 30, false);
    FlDockRequest tab30 = { .kind = FlDockRequest_Tab, .panel = 30, .target_node = leaf->id };
    ASSERT_TRUE(dock_tree_apply(tree, &tab30));
    FlDockRequest undock30 = { .kind = FlDockRequest_Undock, .panel = 30 };
    ASSERT_TRUE(dock_tree_apply(tree, &undock30));
    ASSERT_TRUE(dock_tree_panel_is_floating(tree, 30));
    ASSERT_FALSE(dock_workspace_panel_should_emit(ws, 30));

    // Showing a panel back restores the emit decision exactly.
    dock_workspace_set_panel_visible(ws, 20, true);
    ASSERT_TRUE(dock_workspace_panel_should_emit(ws, 20));
    ASSERT_FALSE(dock_workspace_panel_should_emit(ws, 10));

    dock_workspace_destroy(ws);
}

// Headless promote/demote (the "promoted" state over the shared tier): detaching a panel to its own window
// is a SUBTREE MOVE between two in-process workspaces, not a copy.
UTEST(dock_workspace, promote_demote_between_workspaces) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    // dst is a freshly-created empty floor - the promotion target that becomes a second window's root.
    FlDockWorkspace* src = dock_workspace_create();
    FlDockWorkspace* dst = dock_workspace_create();
    dock_workspace_set_tree(
        src, dock_tree_build(dock_split(FlDockAxis_X, FL_DOCK_SPLIT_RATIO_DEFAULT, dock_leaf(10, 20), dock_leaf(30))));
    FlDockTree* src_tree = src->tree;
    FlDockTree* dst_tree = dst->tree;
    ASSERT_TRUE(dock_tree_check_invariants(src_tree));

    // Tag panel 30 a non-general family so we can confirm the tag travels with the panel across the move.
    dock_workspace_set_panel_family(src, 30, 7);

    ASSERT_EQ((int)dock_tree_collect_panels(src_tree, nullptr, 0), 3);
    ASSERT_EQ((int)dock_tree_collect_panels(dst_tree, nullptr, 0), 0);

    // --- Promote panel 30 from src to dst -------------------------------------------------------------------------
    ASSERT_TRUE(dock_workspace_promote(src, dst, 30));

    ASSERT_TRUE(dock_tree_check_invariants(dst_tree));
    FlDockNode* dst_root_node = dst_tree->root;
    ASSERT_EQ((int)dst_root_node->kind, (int)FlDockNodeKind_Leaf);
    ASSERT_EQ((int)dst_root_node->leaf.count, 1);
    ASSERT_EQ((int)dst_root_node->leaf.panels[0], 30);
    ASSERT_EQ((int)dst_root_node->leaf.panels[dst_root_node->leaf.selected], 30);
    ASSERT_EQ((int)dock_workspace_panel_leaf(dst, 30), (int)dst_root_node->id);

    ASSERT_TRUE(dock_tree_check_invariants(src_tree));
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(src_tree, 30) == nullptr);
    ASSERT_FALSE(dock_tree_panel_is_floating(src_tree, 30));
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(src_tree, 10) != nullptr);
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(src_tree, 20) != nullptr);

    // Conservation across the pair: 30 left src's universe and entered dst's - never in both, never lost.
    ASSERT_EQ((int)dock_tree_collect_panels(src_tree, nullptr, 0), 2);
    ASSERT_EQ((int)dock_tree_collect_panels(dst_tree, nullptr, 0), 1);

    // The family tag travelled with the panel.
    ASSERT_EQ((int)dock_workspace_panel_family(dst, 30), 7);

    // Promote is rejected (no-op) when the target is not an empty floor: dst already hosts 30.
    ASSERT_FALSE(dock_workspace_promote(src, dst, 10));
    ASSERT_EQ((int)dock_tree_collect_panels(src_tree, nullptr, 0), 2);
    ASSERT_EQ((int)dock_tree_collect_panels(dst_tree, nullptr, 0), 1);

    // --- Demote panel 30 back into src ---------------------------------------------------------------------------
    ASSERT_TRUE(dock_workspace_demote(src, dst, 30));

    ASSERT_TRUE(dock_tree_check_invariants(src_tree));
    ASSERT_TRUE(dock_tree_check_invariants(dst_tree));
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(src_tree, 30) != nullptr);
    ASSERT_NE((int)dock_workspace_panel_leaf(src, 30), 0);
    ASSERT_EQ((int)dst_tree->root->kind, (int)FlDockNodeKind_Leaf);
    ASSERT_EQ((int)dst_tree->root->leaf.count, 0);

    ASSERT_EQ((int)dock_tree_collect_panels(src_tree, nullptr, 0), 3);
    ASSERT_EQ((int)dock_tree_collect_panels(dst_tree, nullptr, 0), 0);
    ASSERT_EQ((int)dock_workspace_panel_family(src, 30), 7);

    // Demoting a panel that is not in dst is a rejected no-op.
    ASSERT_FALSE(dock_workspace_demote(src, dst, 30));

    dock_workspace_destroy(src);
    dock_workspace_destroy(dst);
}

// Workspace registry + per-window binding: the set of dock trees is app-global (the
// registry in the shared tier), but dock_workspace_current() resolves to the workspace the ACTIVE window hosts via
// its non-owning ctx->dock_workspace. A fresh context defaults to registry entry 0 (so a single-window app is
// unchanged); binding a context to its own workspace makes resolution per-window.
// A second context and workspace made for one case. dock_ctx_pair_release makes the fixture current again and frees
// both on every exit from the case.
typedef struct DockCtxPair {
    FlUiCtx* fixture;
    FlUiCtx* other;
    FlDockWorkspace* other_ws;
} DockCtxPair;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void dock_ctx_pair_release(DockCtxPair* pair) {
    ui_make_current(pair->fixture);
    if (pair->other_ws != nullptr) {
        dock_workspace_destroy(pair->other_ws);
    }
    if (pair->other != nullptr) {
        ui_destroy(pair->other);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Whether the active context's workspace holds panel.
static bool dock_current_workspace_holds(FlDockPanelId panel) {
    return dock_tree_has_panel(dock_workspace_current()->tree, panel);
}

UTEST(dock_workspace, registry_resolves_per_active_ctx) {
    enum { MARKER_PANEL = 777 };
    FlUiCtx* fixture = render_fixture_get();
    ASSERT_TRUE_MSG(fixture != nullptr, "render fixture creation failed");
    UiShared* shared = fixture->shared;
    __attribute__((cleanup(dock_ctx_pair_release))) DockCtxPair pair = { .fixture = fixture };

    FlDockWorkspace* def = dock_shared_default(shared->dock);
    ASSERT_TRUE_MSG(def != nullptr, "registry has a default workspace (entry 0)");

    ui_make_current(fixture);
    ASSERT_TRUE(dock_workspace_current() != nullptr);
    ASSERT_FALSE(dock_current_workspace_holds(MARKER_PANEL));

    // A second window on the SAME shared tier defaults to the same registry entry (single-window behaviour).
    pair.other = ui_create(s_arena, shared);
    ASSERT_TRUE(pair.other != nullptr);
    ui_make_current(pair.other);
    ASSERT_FALSE(dock_current_workspace_holds(MARKER_PANEL));

    // Bind the second context to its own workspace: resolution is now per the active context.
    pair.other_ws = dock_workspace_create();
    dock_workspace_set_tree(pair.other_ws, dock_tree_build(dock_leaf(MARKER_PANEL)));
    ui_ctx_set_dock_workspace(pair.other, pair.other_ws);
    ui_make_current(pair.other);
    ASSERT_TRUE(dock_current_workspace_holds(MARKER_PANEL));
    ui_make_current(fixture);
    ASSERT_FALSE(dock_current_workspace_holds(MARKER_PANEL));
}

// Promote/demote lifecycle through the FlDockHost seam with a STUB host (no OS window). Drives the
// whole flowi-owned half: dock_promote_panel enqueues; dock_workspace_begin (the docking safe point) drains it -
// creating a second workspace, moving the subtree in, and calling the host to open a window; fl_dock_notify_window_
// closed runs the demote and dissolves the second workspace.
typedef struct StubDockHost {
    int open_calls;
    int close_calls;
    FlDockWorkspace* last_ws;
    void* handle;
    bool return_null; // when set, open_promoted_window reports failure (returns null)
} StubDockHost;

static void* stub_dock_open(void* ud, FlDockWorkspace* ws, f32 w, f32 h) {
    StubDockHost* s = (StubDockHost*)ud;
    s->open_calls++;
    s->last_ws = ws;
    (void)w;
    (void)h;
    return s->return_null ? nullptr : s->handle;
}

static void stub_dock_close(void* ud, void* host_window) {
    StubDockHost* s = (StubDockHost*)ud;
    s->close_calls++;
    (void)host_window;
}

// The default workspace's tree, replaced for the case and put back when the scope ends.
typedef struct DockDefaultTree {
    FlDockWorkspace* ws;
    FlDockTree* saved;
} DockDefaultTree;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static DockDefaultTree dock_default_tree_swap(FlUiCtx* fixture, const FlDockSpec* spec) {
    ui_make_current(fixture);
    DockDefaultTree scope = { .ws = dock_shared_default(fixture->shared->dock) };
    scope.saved = dock_tree_clone(scope.ws->tree);
    dock_workspace_set_tree(scope.ws, dock_tree_build(spec));
    return scope;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void dock_default_tree_restore(DockDefaultTree* scope) {
    dock_workspace_set_tree(scope->ws, scope->saved);
}

#define DOCK_DEFAULT_TREE(var, fixture, spec) \
    __attribute__((cleanup(dock_default_tree_restore))) DockDefaultTree var = dock_default_tree_swap((fixture), (spec))

// The stub host registered with the shared tier, plus the default workspace's tree swapped for spec. The host and its
// window handle are file-static, so the shared tier never holds a stack address; the scope clears the registration
// and restores the tree when the case returns.
static StubDockHost s_stub_host;
static int s_stub_host_handle;

typedef struct DockStubHostScope {
    DockDefaultTree tree;
} DockStubHostScope;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static DockStubHostScope dock_stub_host_begin(FlUiCtx* fixture, const FlDockSpec* spec) {
    s_stub_host = (StubDockHost) { .handle = &s_stub_host_handle };
    DockStubHostScope scope = { .tree = dock_default_tree_swap(fixture, spec) };
    fl_dock_register_host((FlDockHost) {
        .user_data = &s_stub_host, .open_promoted_window = stub_dock_open, .close_promoted_window = stub_dock_close });
    return scope;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void dock_stub_host_end(DockStubHostScope* scope) {
    fl_dock_register_host((FlDockHost) { 0 });
    dock_default_tree_restore(&scope->tree);
}

#define DOCK_STUB_HOST(var, fixture, spec) \
    __attribute__((cleanup(dock_stub_host_end))) DockStubHostScope var = dock_stub_host_begin((fixture), (spec))

UTEST(dock_workspace, promote_demote_lifecycle_via_host) {
    FlUiCtx* fixture = render_fixture_get();
    ASSERT_TRUE_MSG(fixture != nullptr, "render fixture creation failed");
    DOCK_STUB_HOST(scope, fixture, dock_split(FlDockAxis_X, 0.5f, dock_leaf(101), dock_leaf(102)));
    FlDockWorkspace* src = scope.tree.ws;
    ASSERT_EQ((int)dock_tree_collect_panels(src->tree, nullptr, 0), 2);

    // Enqueue the promote: deferred, so nothing happens until the safe point.
    dock_promote_panel(101);
    ASSERT_EQ(s_stub_host.open_calls, 0);

    dock_workspace_begin(src);
    dock_workspace_end(src);
    ASSERT_EQ(s_stub_host.open_calls, 1);
    ASSERT_TRUE_MSG(s_stub_host.last_ws != nullptr, "host received the new (second) workspace");
    FlDockWorkspace* dst = s_stub_host.last_ws;

    ASSERT_TRUE(dock_tree_find_leaf_with_panel(src->tree, 101) == nullptr);
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(dst->tree, 101) != nullptr);
    ASSERT_EQ((int)dock_tree_collect_panels(src->tree, nullptr, 0), 1);
    ASSERT_EQ((int)dock_tree_collect_panels(dst->tree, nullptr, 0), 1);

    fl_dock_notify_window_closed(&s_stub_host_handle);
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(src->tree, 101) != nullptr);
    ASSERT_EQ((int)dock_tree_collect_panels(src->tree, nullptr, 0), 2);
    ASSERT_EQ_MSG(s_stub_host.close_calls, 0,
                  "the window is already gone on this path, so flowi must not ask the host to close it again");

    // An unknown handle is a no-op.
    fl_dock_notify_window_closed(&s_stub_host_handle); // already removed - must not crash or double-demote
    ASSERT_EQ((int)dock_tree_collect_panels(src->tree, nullptr, 0), 2);
}

// Promote FAILURE paths: the three guards that keep a panel from ever being lost.
UTEST(dock_workspace, promote_failure_paths_via_host) {
    FlUiCtx* fixture = render_fixture_get();
    ASSERT_TRUE_MSG(fixture != nullptr, "render fixture creation failed");
    DOCK_STUB_HOST(scope, fixture, dock_split(FlDockAxis_X, 0.5f, dock_leaf(101), dock_leaf(102)));
    FlDockWorkspace* src = scope.tree.ws;

    // Enqueue-time guard: promoting an id the workspace does not hold enqueues nothing - the host is never called.
    dock_promote_panel(999);
    dock_workspace_begin(src);
    dock_workspace_end(src);
    ASSERT_EQ_MSG(s_stub_host.open_calls, 0, "absent-panel promote is a no-op (nothing enqueued)");
    ASSERT_EQ((int)dock_tree_collect_panels(src->tree, nullptr, 0), 2);

    // Flush-time guard: the panel left the source between enqueue and the flush, so dock_workspace_promote fails and
    // the freshly-created second workspace is rolled back rather than left dangling - no window is opened.
    dock_promote_panel(101);
    FlDockRequest close101 = { .kind = FlDockRequest_Close, .panel = 101 };
    dock_tree_apply(src->tree, &close101); // 101 leaves src before the next begin drains the queue
    dock_workspace_begin(src);
    dock_workspace_end(src);
    ASSERT_EQ_MSG(s_stub_host.open_calls, 0, "flush with a vanished panel opens no window (dst rolled back)");
    ASSERT_EQ((int)dock_tree_collect_panels(src->tree, nullptr, 0), 1);

    // Host-failure guard: open_promoted_window returns null, so the panel is demoted straight back into the source.
    dock_workspace_set_tree(src, dock_tree_build(dock_split(FlDockAxis_X, 0.5f, dock_leaf(101), dock_leaf(102))));
    s_stub_host.return_null = true;
    dock_promote_panel(101);
    dock_workspace_begin(src);
    dock_workspace_end(src);
    ASSERT_EQ_MSG(s_stub_host.open_calls, 1, "host was asked to open the window");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(src->tree, 101) != nullptr,
                    "host failure → 101 demoted back into the source, never lost");
    ASSERT_EQ_MSG((int)dock_tree_collect_panels(src->tree, nullptr, 0), 2, "conservation holds across a failed open");
}

// End-to-end through the REAL headless fl_app + window backend: the default dock host registered by
// fl_app_create opens a real headless window for the promoted subtree, and an injected window-close demotes it back.
typedef struct DockPromoteE2E {
    DockFixture fx;           // fx.ws follows whichever window is mid-frame, so it is not the main workspace
    FlDockWorkspace* main_ws; // the main window's workspace (registry entry 0) - stable for the app's lifetime
    bool requested;
} DockPromoteE2E;

static void dock_promote_e2e_frame(void* user_data) {
    DockPromoteE2E* st = (DockPromoteE2E*)user_data;
    FlDockWorkspace* ws = dock_fixture_emit(&st->fx, dock_split(FlDockAxis_X, 0.5f, dock_leaf(201), dock_leaf(202)));
    if (st->main_ws == nullptr) {
        st->main_ws = ws; // the promoted window does not exist yet, so the first workspace to appear is the main one
    }
    dock_workspace_end(ws);

    // Request the promote once, at the tail of the main window's frame; the next dock_workspace_begin flushes it.
    if (!st->requested) {
        st->requested = true;
        dock_promote_panel(201);
    }
}

UTEST(dock_workspace, promote_open_close_demote_via_fl_app) {
    DOCK_APP(app, "dockpromote", 800, 600);
    DockPromoteE2E st = { 0 };

    // Tick 1: install the layout, emit, then enqueue the promote. Only the main window exists so far.
    fl_app_tick(app, &st, dock_promote_e2e_frame);
    ASSERT_TRUE_MSG(st.main_ws != nullptr, "main workspace captured");
    ASSERT_EQ_MSG(fl_app_window_count(app), 1, "only the main window before the promote materializes");
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(st.main_ws->tree, 201) != nullptr);

    // Tick 2: the main window's begin flushes the promote - the subtree moves to a second workspace and the host
    // QUEUES the OS window open (mid-frame), so 201 has already left the source but the window is not live yet.
    fl_app_tick(app, &st, dock_promote_e2e_frame);
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(st.main_ws->tree, 201) == nullptr,
                    "201 promoted out of the source workspace");

    // Tick 3: the queued promoted window materializes - a real headless window hosting the second workspace.
    fl_app_tick(app, &st, dock_promote_e2e_frame);
    ASSERT_EQ_MSG(fl_app_window_count(app), 2, "the promoted window is live (compositor-placed by the WM on Wayland)");

    // Close the promoted window (windows[1]); the default host's on_close demotes 201 back and dissolves the second
    // workspace. Next tick tears the window down.
    fl_app_inject_window_close_for(app, fl_app_window_at(app, 1));
    fl_app_tick(app, &st, dock_promote_e2e_frame);
    ASSERT_EQ_MSG(fl_app_window_count(app), 1,
                  "the promoted window closed — second workspace dissolved (registry clean)");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(st.main_ws->tree, 201) != nullptr,
                    "201 demoted back into the source workspace by stable id");
    ASSERT_EQ((int)dock_tree_collect_panels(st.main_ws->tree, nullptr, 0), 2);
}

// Tab-header selection changes the rendered panel: a leaf with two panels gets a tab strip (the embeddable
// ui_tab_strip core, driven by dock_layout's leaf header), and clicking the second tab re-selects that panel -
// which flips which panel the workspace reports visible.
static void dock_tab_header_frame(void* user_data) {
    // A single root leaf holding two panels {100, 101}, panel 100 the initial selected tab.
    FlDockWorkspace* ws = dock_fixture_emit((DockFixture*)user_data, dock_leaf(100, 101));
    dock_workspace_end(ws);
}

// A panel body that is one marker element filling its tile, keyed by panel id so each rect can be found. Two panels
// split left/right: each marker sits within its own half, whatever the declaration order.
static void dock_nest_panel(FlDockPanelId id) {
    dock_panel(id, FL_S("nest")) {
        // Coloured so the marker emits a rectangle command that frame_has_rect can find.
        ui_layout_area_id(((FlLayoutId) { .name = FL_S("nest_marker"), .id = (u32)id }),
                          .sizing = { .width = sizing_grow(), .height = sizing_grow() },
                          .color = ui_color(40, 40, 40, 255)) {}
    }
}

static void dock_nest_frame(void* user_data) {
    // Left half panel 100, right half panel 101 - an even split so the boundary is the midpoint.
    FlDockWorkspace* ws
        = dock_fixture_emit((DockFixture*)user_data, dock_split(FlDockAxis_X, 0.5f, dock_leaf(100), dock_leaf(101)));
    // Declared AFTER the tiles and in one flat sequence, yet each lands in its own half.
    dock_nest_panel(100);
    dock_nest_panel(101);
    dock_workspace_end(ws);
}

// A panel whose body wants MORE room than its tile: flowi clips the overflow, so the extra never paints over the
// neighbour - but the body's own element still measures at the size it laid out at. fl_dock_panel_size is the
// honest answer, and this pins the difference.
typedef struct DockOversizeState {
    DockFixture fx;
    FlVec2 reported; // what fl_dock_panel_size answered inside the frame
} DockOversizeState;

static void dock_oversize_frame(void* user_data) {
    DockOversizeState* st = (DockOversizeState*)user_data;
    // Top half panel 100, bottom half panel 101 - 101 is the neighbour whose room 100 must not claim.
    FlDockWorkspace* ws = dock_fixture_emit(&st->fx, dock_split(FlDockAxis_Y, 0.5f, dock_leaf(100), dock_leaf(101)));
    dock_panel(100, FL_S("oversize")) {
        // 900px of content in a tile barely 170px tall: the body grows to its content, the tile does not.
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

    // What the panel's own element measures: the full 900, because Clay never shrinks below content minimum.
    Clay_ElementData body = dock_layout_element((FlLayoutId) { .name = FL_S("oversize_body"), .id = 100u });
    ASSERT_TRUE_MSG(body.found, "the oversized body laid out");
    ASSERT_TRUE_MSG(body.boundingBox.height > 800.0f, "the body grows to its content, overflowing its tile");

    ASSERT_TRUE_MSG(st.reported.y > 0.0f, "panel_size answers for a laid-out panel");
    ASSERT_TRUE_MSG(st.reported.y < body.boundingBox.height * 0.5f,
                    "panel_size must report the tile the dock gave the panel, not the size its body grew to");

    FlDockNode* neighbour = dock_tree_find_leaf_with_panel(st.fx.ws->tree, 101);
    ASSERT_TRUE_MSG(neighbour != nullptr, "the neighbour keeps its leaf");
    Clay_ElementData below = dock_element(dock_layout_content_element_id(neighbour->id));
    ASSERT_TRUE_MSG(below.found, "the neighbour's tile laid out");

    // The property is about EDGES, not magnitudes: the room the panel was told it has must END before the
    // neighbour BEGINS. That is `tile_top + reported_height <= neighbour_top` - comparing the height against the
    // neighbour's y alone would only hold while the tile happens to start at zero, and every non-empty leaf now
    // carries a tab header that pushes its content area down. Clay boxes are physical px and panel_size answers in
    // logical px, so scale the reported height back up before comparing.
    FlDockNode* own = dock_tree_find_leaf_with_panel(st.fx.ws->tree, 100);
    ASSERT_TRUE_MSG(own != nullptr, "the panel keeps its leaf");
    Clay_ElementData mine = dock_element(dock_layout_content_element_id(own->id));
    ASSERT_TRUE_MSG(mine.found, "the panel's own tile laid out");
    ASSERT_TRUE_MSG(mine.boundingBox.y > 0.0f, "the tile starts below its leaf's tab header, not at the origin");
    ASSERT_TRUE_MSG(mine.boundingBox.y + st.reported.y * ui_get_scale() <= below.boundingBox.y + 0.5f,
                    "the room reported to the panel must stop before the panel below it starts");
}

// Clipping the overflow away hides the mistake, so the dock reports it - once per panel, not once per frame.
UTEST(dock_workspace, panel_body_overflow_is_reported_once) {
    DOCK_APP(app, "dockspill", 600, 400);
    DockOversizeState st = { 0 };
    DOCK_RUN_FRAMES(app, st, st.fx, dock_oversize_frame, 8);
    ASSERT_EQ_MSG((int)dock_workspace_panel_overflow_logs(st.fx.ws, 100), 1,
                  "the first overflowing frame reports, every later frame stays silent");
}

// A placed child is positioned by its offset, not by the flow, so it must not size the body either: one far wider
// than its tile leaves the body at the tile and takes no share from its flowed siblings.
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

    ASSERT_LT_MSG(tile.boundingBox.width, 2000.0f, "the placed child must be wider than the tile to test anything");
    ASSERT_NEAR_MSG(row.boundingBox.width, tile.boundingBox.width, 0.5f, "the body stays at its tile's width");
    ASSERT_NEAR_MSG(fill.boundingBox.width, tile.boundingBox.width, 0.5f,
                    "the flowed sibling takes the whole row - the placed child claims no share of it");
    ASSERT_NEAR_MSG(wide.boundingBox.width, 2000.0f, 0.5f, "the placed child keeps its own size");
    ASSERT_NEAR_MSG(wide.boundingBox.height, row.boundingBox.height, 0.5f, "a grow placed child fills its parent");
    ASSERT_EQ_MSG((int)dock_workspace_panel_overflow_logs(st.ws, 100), 0, "nothing overflowed, so nothing reports");
}

// {0,0} means DO NOT ACT ON THIS. The family: absent, not laid out yet, hidden, mid-drag, and a
// background tab, which has no room in its leaf's tile.
typedef struct DockSizeContractState {
    DockFixture fx;
    FlVec2 shown;      // the tab its leaf shows
    FlVec2 background; // the other tab in the same leaf
    FlVec2 neighbour;  // the sibling leaf's panel, hidden partway through the test
    FlVec2 absent;     // never in the tree
} DockSizeContractState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Whether a reported size is the {0,0} "do not act" answer.
static bool dock_size_is_zero(FlVec2 size) {
    return fabsf(size.x) < 1e-4f && fabsf(size.y) < 1e-4f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

UTEST(dock_workspace, panel_size_answers_zero_for_the_do_not_act_family) {
    DOCK_APP(app, "dockzero", 800, 400);
    DockSizeContractState st = { 0 };
    fl_app_tick(app, &st, dock_size_contract_frame); // the tree is installed during this frame, so nothing has a rect
    ASSERT_TRUE_MSG(dock_size_is_zero(st.shown), "a panel not laid out yet reports {0,0}");

    DOCK_RUN_FRAMES(app, st, st.fx, dock_size_contract_frame,
                    2); // read-back is one frame behind, so settle before asserting
    ASSERT_TRUE_MSG(st.shown.x > 0.0f && st.shown.y > 0.0f, "the tab its leaf shows reports the tile it was given");
    ASSERT_TRUE_MSG(dock_size_is_zero(st.background),
                    "a background tab has no room of its own — its leaf's tile belongs to the tab in front");
    ASSERT_TRUE_MSG(dock_size_is_zero(st.absent), "a panel absent from the tree reports {0,0}");
    ASSERT_TRUE_MSG(st.neighbour.x > 0.0f && st.neighbour.y > 0.0f, "the sibling leaf's panel starts out with room");

    // A hidden panel reports {0,0} from the first frame it is hidden, and keeps doing so.
    dock_workspace_set_panel_visible(st.fx.ws, 402, false);
    for (int i = 0; i < 4; ++i) {
        fl_app_tick(app, &st, dock_size_contract_frame);
        ASSERT_TRUE_MSG(dock_size_is_zero(st.neighbour), "a hidden panel reports {0,0}");
    }

    dock_workspace_set_panel_visible(st.fx.ws, 402, true);
    dock_ticks(app, &st, dock_size_contract_frame, 2);
    ASSERT_TRUE_MSG(st.neighbour.x > 0.0f && st.neighbour.y > 0.0f, "showing it back gives it its room again");

    ASSERT_EQ_MSG((int)dock_workspace_panel_overflow_logs(st.fx.ws, 400), 0, "an empty body never overflows");
}

// The floor's last consequence: a tile squeezed under the minimum content size joins the {0,0} family, so an app
// does no work for room it does not have. The leaf still tiles - the workspace overflows its host and is clipped.
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
    // 140px across two tiles and a divider: both land well under the 80px minimum content width.
    DOCK_APP(app, "dockstarve", 140, 300);

    DockStarvedState st = { 0 };
    DOCK_RUN_FRAMES(app, st, st.fx, dock_starved_frame, 5);

    ASSERT_TRUE_MSG(dock_size_is_zero(st.left), "a starved panel reports {0,0}");
    ASSERT_TRUE_MSG(dock_size_is_zero(st.right), "and so does its equally starved sibling");

    // Reporting no room is not dropping out: both leaves still tile, at the same size.
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

// An overlay opened from a panel body draws past the panel's tile, up to the window. Both attach modes cross the
// seam: root-attached (a dialog or context menu) and element-attached (anchored to a widget in the body).
#define DOCK_SEAM_OVERLAY_W 250.0f
#define DOCK_SEAM_ID(lit) ((FlLayoutId) { .name = FL_S(lit), .id = 100u })
#define DOCK_SEAM_SCISSOR_MAX 16

static void dock_seam_frame(void* user_data) {
    // Left half panel 100, right half panel 101 - the seam is the midpoint the overlays must cross.
    FlDockWorkspace* ws
        = dock_fixture_emit((DockFixture*)user_data, dock_split(FlDockAxis_X, 0.5f, dock_leaf(100), dock_leaf(101)));
    dock_panel(100, FL_S("seam")) {
        // Coloured so it emits a rectangle of its own: a plain, non-floating child of the body, and therefore the
        // control that shows the tile clip is really in force around the overlays below.
        ui_layout_area_id(DOCK_SEAM_ID("seam_anchor"), .sizing = { sizing_grow(), sizing_fixed(40.0f) },
                          .color = ((FlColor) { 0, 0, 255, 255 })) {}

        fl_floating_area_id(DOCK_SEAM_ID("seam_root_overlay"), .attach_to = FlFloatingAttachTo_Root,
                            .pointer_capture = FlFloatingPointerCapture_Passthrough, .offset = { 200.0f, 150.0f },
                            .sizing = { sizing_fixed(DOCK_SEAM_OVERLAY_W), sizing_fixed(40.0f) }) {
            ui_layout_area_id(DOCK_SEAM_ID("seam_root_fill"), .sizing = { sizing_grow(), sizing_grow() },
                              .color = ((FlColor) { 255, 0, 0, 255 })) {}
        }

        // Anchored to the body's own element, starting at its horizontal centre so the fixed width runs past the
        // tile's right edge whatever the host size is.
        fl_floating_area_id(DOCK_SEAM_ID("seam_elem_overlay"), .attach_to = FlFloatingAttachTo_Element,
                            .parent_id = ui_layout_id_to_clay_id(DOCK_SEAM_ID("seam_anchor")),
                            .pointer_capture = FlFloatingPointerCapture_Passthrough,
                            .element_attach = FlFloatingAttachPoint_LeftTop,
                            .parent_attach = FlFloatingAttachPoint_CenterTop,
                            .sizing = { sizing_fixed(DOCK_SEAM_OVERLAY_W), sizing_fixed(40.0f) }) {
            ui_layout_area_id(DOCK_SEAM_ID("seam_elem_fill"), .sizing = { sizing_grow(), sizing_grow() },
                              .color = ((FlColor) { 0, 255, 0, 255 })) {}
        }
    }
    dock_workspace_end(ws);
}

// The rectangle the given element drew this frame, plus the right edge of the tightest scissor in force at that
// point (FLT_MAX when none). A command's bounding box reports the same geometry whether or not a scissor cuts
// it, so the clip edge is the half that decides whether the overlay actually escaped the panel.
static bool dock_seam_drawn_rect(FlLayoutId id, UiRenderCommand* out, f32* clip_right) {
    u32 want = ui_layout_id_to_clay_id(id);
    f32 scissor_right[DOCK_SEAM_SCISSOR_MAX];
    u32 depth = 0; // counts every open scissor; only the first DOCK_SEAM_SCISSOR_MAX are recorded
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; ++i) {
        UiRenderCommand cmd;
        if (!ui_get_render_command(i, &cmd)) {
            continue;
        }
        if (cmd.type == UiRenderCmd_ScissorStart) {
            f32 right = cmd.x + cmd.width;
            if (depth > 0 && depth <= DOCK_SEAM_SCISSOR_MAX && scissor_right[depth - 1] < right) {
                right = scissor_right[depth - 1]; // nested scissors intersect
            }
            if (depth < DOCK_SEAM_SCISSOR_MAX) {
                scissor_right[depth] = right;
            }
            depth++;
        } else if (cmd.type == UiRenderCmd_ScissorEnd) {
            if (depth > 0) {
                depth--;
            }
        } else if (cmd.id == want && cmd.type == UiRenderCmd_Rectangle) {
            *out = cmd;
            // Nested past what the stack records: answer with a clip that cuts everything, so the caller fails
            // rather than passing on an edge this never tracked.
            *clip_right = depth == 0 ? FLT_MAX : (depth <= DOCK_SEAM_SCISSOR_MAX ? scissor_right[depth - 1] : 0.0f);
            return true;
        }
    }
    return false;
}

// The right edge of panel 100's tile: the seam every overlay below has to cross.
static f32 dock_seam_tile_right(FlDockWorkspace* ws) {
    FlDockNode* leaf = dock_tree_find_leaf_with_panel(ws->tree, 100);
    if (leaf == nullptr) {
        return -1.0f;
    }
    Clay_ElementData tile = dock_element(dock_layout_content_element_id(leaf->id));
    return tile.found ? tile.boundingBox.x + tile.boundingBox.width : -1.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Checks the overlay fill named by fill_id draws from inside panel 100's tile to past its right edge with no scissor
// cutting it back, while the non-floating anchor is clipped to that tile.
static void dock_seam_expect_overlay_unclipped(int* utest_result, FlDockWorkspace* ws, FlLayoutId fill_id) {
    f32 seam = dock_seam_tile_right(ws);
    ASSERT_GT_MSG(seam, 0.0f, "panel 100's tile must lay out");

    UiRenderCommand anchor = { 0 };
    f32 anchor_clip = 0.0f;
    ASSERT_TRUE_MSG(dock_seam_drawn_rect(DOCK_SEAM_ID("seam_anchor"), &anchor, &anchor_clip), "the anchor drew");
    ASSERT_LE_MSG(anchor_clip, seam + 0.5f, "control: a non-floating child of the panel body IS clipped to the tile");

    UiRenderCommand drawn = { 0 };
    f32 clip_right = 0.0f;
    ASSERT_TRUE_MSG(dock_seam_drawn_rect(fill_id, &drawn, &clip_right), "an overlay opened inside a panel body draws");
    ASSERT_LT_MSG(drawn.x, seam, "the overlay starts inside the panel that opened it");
    ASSERT_GT_MSG(drawn.x + drawn.width, seam, "and it draws on past that panel's tile, across the dock seam");
    ASSERT_GE_MSG(clip_right, drawn.x + drawn.width,
                  "no scissor in force may cut the overlay back to the tile it was opened from");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(dock_workspace, root_attached_overlay_from_a_panel_crosses_the_dock_seam) {
    DOCK_APP(app, "dockseam", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_seam_frame, 2); // frame 1 installs the tree, frame 2 lays it out settled
    dock_seam_expect_overlay_unclipped(utest_result, st.ws, DOCK_SEAM_ID("seam_root_fill"));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(dock_workspace, element_attached_overlay_escapes_the_panel_clip) {
    DOCK_APP(app, "dockseam2", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_seam_frame, 2);
    dock_seam_expect_overlay_unclipped(utest_result, st.ws, DOCK_SEAM_ID("seam_elem_fill"));
}

UTEST(dock_workspace, tab_header_selection_changes_emitting_panel) {
    DOCK_APP(app, "docktabs", 600, 400);
    DockFixture st = { 0 };

    DOCK_RUN_FRAMES(app, st, st, dock_tab_header_frame, 1);
    ASSERT_TRUE_MSG(dock_workspace_panel_should_emit(st.ws, 100), "panel 100 must start emitting (selected tab)");
    ASSERT_FALSE_MSG(dock_workspace_panel_should_emit(st.ws, 101), "panel 101 is a background tab — it must not emit");

    i32 cx = 0;
    i32 cy = 0;
    ASSERT_TRUE_MSG(dock_tab_centre(&st, st.ws->tree->root->id, 1u, &cx, &cy), "second dock tab was not laid out");

    // Hover the second tab so it goes hot, then press: tabs activate on press, re-selecting panel 101 through the
    // dock-as-tab op.
    fl_app_inject_mouse_move(app, cx, cy);
    dock_settle_hover(app, &st, dock_tab_header_frame);
    ASSERT_TRUE_MSG(dock_workspace_panel_should_emit(st.ws, 100), "hovering a tab must not change the selection");

    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, &st, dock_tab_header_frame);
    ASSERT_TRUE_MSG(dock_workspace_panel_should_emit(st.ws, 101), "clicking the second tab must make panel 101 emit");
    ASSERT_FALSE_MSG(dock_workspace_panel_should_emit(st.ws, 100), "panel 100 must stop emitting once 101 is selected");
    ASSERT_EQ_MSG((int)st.ws->tree->root->leaf.selected, 1, "leaf selected index must be 1");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Panel titles. Panels are declared after the emit, and the tab strip reads the title the workspace retained.

typedef struct DockTitleState {
    DockFixture fx;
    bool declare;         // off to exercise the reserved frame: the tabs must keep their labels with nothing declared
    FlCursorShape cursor; // the shape the frame resolved, sampled inside it (the request is per-frame state)
} DockTitleState;

// Longer than any tab can be, so it is elided at the maximum tab width.
#define DOCK_TITLE_LONG FL_S("A panel title far too long to ever fit inside one dock tab")

static void dock_title_frame(void* user_data) {
    DockTitleState* st = (DockTitleState*)user_data;
    // A root leaf of three panels; 200 is the initial selected tab (the first listed).
    FlDockWorkspace* ws = dock_fixture_emit(&st->fx, dock_leaf(200, 201, 202));
    if (st->declare) {
        dock_workspace_declare(ws, 200, FL_S("Files"));
        dock_workspace_declare(ws, 201, FL_S("")); // untitled - a blank tab, never "201"
        dock_workspace_declare(ws, 202, DOCK_TITLE_LONG);
    }
    dock_workspace_end(ws);
    st->cursor = (FlCursorShape)fl_cursor_get_requested().shape; // per-frame state - only valid inside the frame
}

// The laid-out state of the root leaf's index-th tab.
static ItemState* dock_tab_item(const DockTitleState* st, u32 index) {
    FlDockNodeId leaf_id = st->fx.ws->tree->root->id;
    return hashmap_get(&st->fx.ctx->shared->item_state, dock_tab_clay_id(leaf_id, index));
}

// Matching the prefix (not just the ellipsis) is what separates a real elision from a vanished label.
static bool frame_has_elided_text(FlString full, UiRenderCommand* out_cmd) {
    static const char ellipsis[3] = { (char)0xE2, (char)0x80, (char)0xA6 }; // U+2026, what the elide pass appends
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (!ui_get_render_command(i, &cmd) || cmd.type != UiRenderCmd_Text || cmd.text.data == nullptr) {
            continue;
        }
        const u64 len = cmd.text.length;
        if (len <= 3 || memcmp((const char*)cmd.text.data + (len - 3), ellipsis, 3) != 0) {
            continue;
        }
        const u64 prefix_len = len - 3;
        if (prefix_len < full.length && memcmp(cmd.text.data, full.data, prefix_len) == 0) {
            *out_cmd = cmd;
            return true;
        }
    }
    return false;
}

static bool frame_has_text(FlString want) {
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (ui_get_render_command(i, &cmd) && cmd.type == UiRenderCmd_Text && fl_string_equals(cmd.text, want)) {
            return true;
        }
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Same, restricted to one z band. The drag ghost carries the held panel's title, so bare text presence does not
// tell a tab label from the ghost; the band does.
static bool frame_has_text_at_z(FlString want, int z) {
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (ui_get_render_command(i, &cmd) && cmd.type == UiRenderCmd_Text && (int)cmd.z_index == z
            && fl_string_equals(cmd.text, want)) {
            return true;
        }
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(dock_workspace, tab_labels_render_retained_titles) {
    DOCK_APP(app, "docktitles", 800, 400);
    DockTitleState st = { .declare = true };

    // Frame 1 emits before any declaration has run, so it has no titles yet; frame 2 is the steady state.
    DOCK_RUN_FRAMES(app, st, st.fx, dock_title_frame, 2);

    ASSERT_TRUE_MSG(frame_has_text(FL_S("Files")), "a titled tab renders its title");
    ASSERT_FALSE_MSG(frame_has_text(FL_S("200")), "the numeric panel id must never reach the screen");
    ASSERT_FALSE_MSG(frame_has_text(FL_S("201")), "an untitled panel renders a blank tab, not its id");
    ASSERT_FALSE_MSG(frame_has_text(FL_S("202")), "the numeric panel id must never reach the screen");
    // Asserted positively: the full string being absent would also hold if the label had simply vanished.
    ASSERT_FALSE_MSG(frame_has_text(DOCK_TITLE_LONG), "an over-long title must be elided, not rendered whole");
    UiRenderCommand elided = { 0 };
    ASSERT_TRUE_MSG(frame_has_elided_text(DOCK_TITLE_LONG, &elided),
                    "the over-long title must render as a prefix of itself plus an ellipsis");
    ASSERT_TRUE_MSG(elided.width <= DOCK_TAB_MAX_WIDTH + 0.5f, "the elided run fits inside the max tab width");

    st.declare = false;
    fl_app_tick(app, &st, dock_title_frame);
    ASSERT_TRUE_MSG(dock_workspace_panel_reserved(st.fx.ws, 200), "200 must be reserved with nothing declared");
    ASSERT_TRUE_MSG(frame_has_text(FL_S("Files")), "a reserved panel's tab keeps the title from its last declaration");
    ASSERT_FALSE_MSG(frame_has_text(FL_S("200")), "a reserved tab must not fall back to the numeric id either");
}

UTEST(dock_workspace, blank_and_long_tabs_clamp_to_the_width_band) {
    DOCK_APP(app, "dockwidth", 800, 400);
    DockTitleState st = { .declare = true };
    DOCK_RUN_FRAMES(app, st, st.fx, dock_title_frame, 2);

    ItemState* blank = dock_tab_item(&st, 1);
    ItemState* long_tab = dock_tab_item(&st, 2);
    ASSERT_TRUE_MSG(blank != nullptr && long_tab != nullptr, "the blank and long tabs must both be laid out");
    ASSERT_NEAR_MSG((double)blank->state.size.x, (double)DOCK_TAB_MIN_WIDTH, 0.51,
                    "a blank tab keeps the minimum width so it stays hittable and draggable");
    ASSERT_NEAR_MSG((double)long_tab->state.size.x, (double)DOCK_TAB_MAX_WIDTH, 0.51,
                    "an over-long title caps at the maximum width and elides instead of growing the tab");

    // The min width is a real hit area, not just a rectangle: pressing the blank tab selects its panel.
    i32 cx = (i32)(blank->state.pos.x + blank->state.size.x * 0.5f);
    i32 cy = (i32)(blank->state.pos.y + blank->state.size.y * 0.5f);
    fl_app_inject_mouse_move(app, cx, cy);
    dock_settle_hover(app, &st, dock_title_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    fl_app_tick(app, &st, dock_title_frame);
    ASSERT_TRUE_MSG(dock_workspace_panel_should_emit(st.fx.ws, 201), "clicking the blank tab must select its panel");
}

// Every tab reports the hand cursor on hover - the selected one included, because in a dock header the same press
// that re-selects also begins the drag that pulls the panel out. The strip's empty remainder requests nothing.
UTEST(dock_workspace, every_tab_hovers_the_hand_cursor) {
    DOCK_APP(app, "dockcursor", 800, 400);
    DockTitleState st = { .declare = true };
    DOCK_RUN_FRAMES(app, st, st.fx, dock_title_frame, 2);

    // Tab 0 is the selected tab, 1 and 2 are not - the shape must be identical across all three.
    for (u32 i = 0; i < 3; i++) {
        ItemState* tab = dock_tab_item(&st, i);
        ASSERT_TRUE_MSG(tab != nullptr && tab->state.size.x > 0.0f, "every dock tab must be laid out");
        fl_app_inject_mouse_move(app, (i32)(tab->state.pos.x + tab->state.size.x * 0.5f),
                                 (i32)(tab->state.pos.y + tab->state.size.y * 0.5f));
        dock_settle_hover(app, &st, dock_title_frame);
        ASSERT_EQ_MSG((int)st.cursor, (int)FlCursorShape_Hand,
                      "hovering any dock tab, selected or not, must request the hand cursor");
    }

    // The strip's remainder: same header row, past the last tab. Inert - the arrow, per-frame default.
    ItemState* last = dock_tab_item(&st, 2);
    fl_app_inject_mouse_move(app, (i32)(last->state.pos.x + last->state.size.x + 40.0f),
                             (i32)(last->state.pos.y + last->state.size.y * 0.5f));
    dock_settle_hover(app, &st, dock_title_frame);
    ASSERT_EQ_MSG((int)st.cursor, (int)FlCursorShape_Arrow,
                  "the tab strip's empty remainder stays inert — no grab, so no affordance");
}

// A root split of two leaves, all three panels declared every frame - hiding is asserted against the same
// declarations, since hiding a panel never changes how the app declares it.
static void dock_visibility_frame(void* user_data) {
    DockFixture* fx = (DockFixture*)user_data;
    FlDockWorkspace* ws = dock_fixture_emit(fx, dock_split(FlDockAxis_X, 0.5f, dock_leaf(300, 301), dock_leaf(302)));
    dock_workspace_declare(ws, 300, FL_S("Alpha"));
    dock_workspace_declare(ws, 301, FL_S("Beta"));
    dock_workspace_declare(ws, 302, FL_S("Gamma"));
    dock_workspace_end(ws);
}

// The laid-out width of node's tile, or -1 when it was not laid out.
static f32 dock_node_width(FlDockNodeId node) {
    Clay_ElementData data = dock_element(dock_layout_node_element_id(node));
    return data.found ? data.boundingBox.width : -1.0f;
}

// Whether id drew this frame. Frame-accurate, unlike Clay's element data, which answers for the last frame that
// filled it - the difference is the whole point when asserting that something stopped rendering.
static bool frame_has_rect(FlLayoutId id) {
    u32 want = ui_layout_id_to_clay_id(id);
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; ++i) {
        UiRenderCommand cmd;
        if (ui_get_render_command(i, &cmd) && cmd.id == want && cmd.type == UiRenderCmd_Rectangle) {
            return true;
        }
    }
    return false;
}

UTEST(dock_workspace, hidden_panels_drop_their_tabs_then_the_leaf_out) {
    DOCK_APP(app, "dockvis", 800, 400);
    DockFixture fx = { 0 };
    DOCK_RUN_FRAMES(app, fx, fx, dock_visibility_frame, 2); // frame 1 installs the tree; frame 2 is the steady state

    FlDockTree* tree = fx.ws->tree;
    FlDockNode* left = dock_tree_find_leaf_with_panel(tree, 300);
    FlDockNode* right = dock_tree_find_leaf_with_panel(tree, 302);
    ASSERT_TRUE(left != nullptr && right != nullptr);
    const f32 left_w = dock_node_width(left->id); // the arrangement re-showing has to restore
    const f32 right_w = dock_node_width(right->id);
    ASSERT_TRUE_MSG(left_w > 0.0f && right_w > 0.0f, "both tiles must be laid out to start with");
    ASSERT_TRUE(frame_has_text(FL_S("Beta")));

    // Hiding one tab of a two-tab leaf takes its tab off the strip and nothing else.
    dock_workspace_set_panel_visible(fx.ws, 301, false);
    fl_app_tick(app, &fx, dock_visibility_frame);
    ASSERT_FALSE_MSG(frame_has_text(FL_S("Beta")), "a hidden panel renders no tab");
    ASSERT_TRUE_MSG(frame_has_text(FL_S("Alpha")), "its leaf keeps rendering the tabs that are shown");
    ASSERT_EQ_MSG((int)left->leaf.count, 2, "the hidden panel keeps its slot");
    ASSERT_EQ_MSG((int)left->leaf.panels[1], 301, "and its tab index");
    ASSERT_FALSE_MSG(dock_workspace_panel_reserved(fx.ws, 301), "a declared panel stays unreserved while hidden");
    ASSERT_NEAR_MSG(dock_node_width(left->id), left_w, 0.5f, "hiding one tab does not resize its leaf");

    // Hiding the rest of the leaf drops it out on the display tier: its space goes to its neighbour on the next
    // frame, and the logical split is untouched.
    dock_workspace_set_panel_visible(fx.ws, 300, false);
    dock_ticks(app, &fx, dock_visibility_frame, 2); // read-back is one frame behind
    ASSERT_FALSE_MSG(frame_has_text(FL_S("Alpha")), "an all-hidden leaf renders no tabs at all");
    ASSERT_EQ_MSG((int)tree->root->kind, (int)FlDockNodeKind_Split, "the logical split survives the hide");
    ASSERT_TRUE(dock_tree_check_invariants(tree));
    ASSERT_NEAR_MSG(dock_node_width(right->id), dock_node_width(tree->root->id), 1.0f,
                    "the all-hidden leaf's space ends up entirely with its neighbour");

    // Hiding everything leaves nothing to tile - the last visible leaf has no neighbour to give its space to.
    dock_workspace_set_panel_visible(fx.ws, 302, false);
    fl_app_tick(app, &fx, dock_visibility_frame);
    ASSERT_FALSE_MSG(frame_has_text(FL_S("Gamma")), "an all-hidden workspace renders no tabs at all");
    ASSERT_TRUE(dock_tree_check_invariants(tree));

    // Showing the panels back restores the exact prior arrangement - nothing about the tree ever moved.
    dock_workspace_set_panel_visible(fx.ws, 300, true);
    dock_workspace_set_panel_visible(fx.ws, 301, true);
    dock_workspace_set_panel_visible(fx.ws, 302, true);
    dock_ticks(app, &fx, dock_visibility_frame, 2); // read-back is one frame behind
    ASSERT_NEAR_MSG(dock_node_width(left->id), left_w, 0.5f, "the re-shown leaf is back at its old width");
    ASSERT_NEAR_MSG(dock_node_width(right->id), right_w, 0.5f, "and so is its neighbour");
    ASSERT_TRUE_MSG(frame_has_text(FL_S("Alpha")) && frame_has_text(FL_S("Beta")), "both tabs are back on the strip");
    ASSERT_EQ_MSG((int)left->leaf.selected, 0, "and the leaf's selection was never rewritten");
    ASSERT_TRUE(dock_tree_check_invariants(tree));
}

// Floating is a transient holding pen, not a resting state, so a panel absent from the tree has nowhere loose to
// land: showing a CLOSED panel seats it on the floor through the same re-home every dock op falls back to.
UTEST(dock_workspace, show_after_close_rehomes_to_root) {
    FlDockWorkspace* ws = dock_workspace_create();
    dock_workspace_set_tree(ws, dock_tree_build(dock_split(FlDockAxis_X, 0.5f, dock_leaf(1), dock_leaf(2))));
    FlDockTree* tree = ws->tree;

    FlDockRequest close = { .kind = FlDockRequest_Close, .panel = 2 };
    ASSERT_TRUE(dock_tree_apply(tree, &close));
    ASSERT_EQ_MSG((int)dock_tree_collect_panels(tree, nullptr, 0), 1, "close is the one op that leaves the universe");

    dock_workspace_set_panel_visible(ws, 2, true);
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 2) != nullptr, "showing a closed panel seats it in a leaf");
    ASSERT_EQ_MSG((int)tree->floating_count, 0, "never loose — nothing can park in the holding pen");
    ASSERT_TRUE(dock_tree_check_invariants(tree));

    // A panel already in the tree is untouched by the rule: showing it does not move its seat.
    const FlDockNode* seat = dock_tree_find_leaf_with_panel(tree, 1);
    ASSERT_TRUE(seat != nullptr);
    const FlDockNodeId seat_id = seat->id;
    dock_workspace_set_panel_visible(ws, 1, true);
    const FlDockNode* after = dock_tree_find_leaf_with_panel(tree, 1);
    ASSERT_TRUE(after != nullptr);
    ASSERT_EQ_MSG((int)after->id, (int)seat_id, "an already-seated panel does not re-home");

    dock_workspace_destroy(ws);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The active panel - one panel id per workspace, moved by a press inside a tile, plus the keyboard-ring blur that
// rides every change of it (ui/dock/dock_workspace.{h,c}).

#define DOCK_ACTIVE_MODAL_SCOPE 0xD0C4A1u

// Two side-by-side single-panel leaves, each body holding one focusable widget. text_input swaps the right body's
// button for a text input, the one widget family that claims the keyboard ring on its own click.
typedef struct DockActiveState {
    DockFixture fx;
    bool text_input;
    bool modal;
    bool popup;
    bool skip_right; // stop declaring 501, so its slot reads as reserved
} DockActiveState;

// The text input's state. File-static because the UI context keeps a pointer to the focused input.
static FlTextInputState s_dock_active_input;

static void dock_active_frame(void* user_data) {
    DockActiveState* st = (DockActiveState*)user_data;
    FlUiCtx* ctx = ui_get_ctx();
    if (ctx != nullptr) {
        ctx->physical_keyboard_available = true; // no virtual keyboard on the text input's focus-gain path
    }
    if (st->modal) {
        fl_modal_begin_frame(DOCK_ACTIVE_MODAL_SCOPE);
    } else if (ctx != nullptr && ctx->modal_scope_id != 0) {
        fl_modal_close();
    }

    FlDockWorkspace* ws = dock_fixture_emit(&st->fx, dock_split(FlDockAxis_X, 0.5f, dock_leaf(500), dock_leaf(501)));
    dock_panel(500, FL_S("Left")) {
        ui_button(FL_S("left_button"));
    }
    if (!st->skip_right) {
        dock_panel(501, FL_S("Right")) {
            if (st->text_input) {
                ui_text_input(FL_S("right_input"), &s_dock_active_input, (FlTextInputConfig) { 0 });
            } else {
                ui_button(FL_S("right_button"));
            }
        }
    }
    dock_workspace_end(ws);

    if (st->popup) {
        // Point-anchored: an element-anchored popup would need an anchor element this frame never emits.
        ui_popup(ID("dock_active_popup"), .anchor = FlPopupAnchor_Point, .point = { 10.0f, 10.0f },
                 .auto_close = false) {
            ui_layout_area(.sizing = { sizing_fixed(40.0f), sizing_fixed(40.0f) }) {}
        }
    }
}

// A headless app and a zeroed DockActiveState named st.
#define DOCK_ACTIVE_APP(app, st)           \
    DOCK_APP(app, "dockactive", 800, 400); \
    DockActiveState st = { 0 }

static void dock_active_settle(FlApp* app, DockActiveState* st) {
    dock_ticks(app, st, dock_active_frame, 3);
}

// Presses the centre of the tile panel is docked in and holds. False when that tile is not laid out.
static bool dock_press_panel_tile(FlApp* app, DockActiveState* st, FlDockPanelId panel) {
    if (st->fx.ws == nullptr) {
        return false;
    }
    const FlDockNode* leaf = dock_tree_find_leaf_with_panel(st->fx.ws->tree, panel);
    if (leaf == nullptr) {
        return false;
    }
    Clay_ElementData data = dock_element(dock_layout_node_element_id(leaf->id));
    if (!data.found || data.boundingBox.width <= 0.0f) {
        return false;
    }
    dock_press_at(app, st, dock_active_frame, (i32)(data.boundingBox.x + data.boundingBox.width * 0.5f),
                  (i32)(data.boundingBox.y + data.boundingBox.height * 0.5f));
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Settles the frame, then presses and releases panel's tile. True when the tile was laid out and panel is active.
static bool dock_activate_panel(FlApp* app, DockActiveState* st, FlDockPanelId panel) {
    dock_active_settle(app, st);
    if (!dock_press_panel_tile(app, st, panel)) {
        return false;
    }
    dock_release(app, st, dock_active_frame);
    return st->fx.ws->active_panel == panel;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Ticks once and answers the active panel after it.
static FlDockPanelId dock_active_after_tick(FlApp* app, DockActiveState* st) {
    fl_app_tick(app, st, dock_active_frame);
    return st->fx.ws->active_panel;
}

UTEST(dock_workspace, a_press_activates_a_panel_and_hover_never_does) {
    DOCK_ACTIVE_APP(app, st);
    DOCK_RUN_FRAMES(app, st, st.fx, dock_active_frame, 3);
    ASSERT_EQ_MSG((int)st.fx.ws->active_panel, 0, "nothing is active before the first press");

    const FlDockNode* right = dock_tree_find_leaf_with_panel(st.fx.ws->tree, 501);
    Clay_ElementData box = dock_element(dock_layout_node_element_id(right->id));
    ASSERT_TRUE_MSG(box.found && box.boundingBox.width > 0.0f, "the right tile was not laid out");
    i32 cx = (i32)(box.boundingBox.x + box.boundingBox.width * 0.5f);
    i32 cy = (i32)(box.boundingBox.y + box.boundingBox.height * 0.5f);

    fl_app_inject_mouse_move(app, cx, cy);
    dock_active_settle(app, &st);
    ASSERT_EQ_MSG((int)st.fx.ws->active_panel, 0, "hovering a tile must never activate its panel");

    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    ASSERT_EQ_MSG((int)dock_active_after_tick(app, &st), 501, "a press inside a tile activates its panel");
    dock_release(app, &st, dock_active_frame);

    ASSERT_TRUE(dock_press_panel_tile(app, &st, 500));
    ASSERT_EQ_MSG((int)st.fx.ws->active_panel, 500, "pressing the other tile moves the active panel");
    dock_release(app, &st, dock_active_frame);
}

UTEST(dock_workspace, changing_the_active_panel_blurs_the_keyboard_ring) {
    DOCK_ACTIVE_APP(app, st);
    ASSERT_TRUE_MSG(dock_activate_panel(app, &st, 501), "the right panel is active to start with");

    // Hand the ring to a widget in the OTHER panel. The dock cannot attribute it to a panel, which is exactly why
    // the blur below is unconditional.
    const u32 left_button = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("left_button"), .id = 0 });
    st.fx.ctx->focused_id = left_button;
    fl_app_tick(app, &st, dock_active_frame);
    ASSERT_EQ_MSG(fl_focused_id(), left_button, "the button holds the ring while it keeps emitting");

    ASSERT_TRUE(dock_press_panel_tile(app, &st, 500));
    ASSERT_EQ_MSG((int)st.fx.ws->active_panel, 500, "the press moved the active panel");
    ASSERT_EQ_MSG(fl_focused_id(), 0u, "a change of active panel blurs the keyboard ring");
    dock_release(app, &st, dock_active_frame);

    // And the blur survives the frame: auto-restore repairs a vanished widget, never an unclaimed ring.
    fl_app_tick(app, &st, dock_active_frame);
    ASSERT_EQ_MSG(fl_focused_id(), 0u, "nothing re-seeds the ring on the frames after the blur");
}

UTEST(dock_workspace, a_text_input_reclaims_the_ring_the_same_frame) {
    DOCK_ACTIVE_APP(app, st);
    s_dock_active_input = (FlTextInputState) { 0 };
    st.text_input = true;
    ASSERT_TRUE_MSG(dock_activate_panel(app, &st, 500), "the left panel starts out active");

    const u32 right_input = ui_layout_id_to_clay_id((FlLayoutId) { .name = FL_S("right_input"), .id = 0 });
    ItemState* input = hashmap_get(&st.fx.ctx->shared->item_state, right_input);
    ASSERT_TRUE_MSG(input != nullptr && input->state.size.x > 0.0f, "the right panel's text input was not laid out");
    i32 cx = (i32)(input->state.pos.x + input->state.size.x * 0.5f);
    i32 cy = (i32)(input->state.pos.y + input->state.size.y * 0.5f);

    // A text input claims the ring on the click, so press and release have to land in the same frame for the blur
    // and the claim to collide at all. The workspace emit runs before the panel bodies, so the claim comes second.
    fl_app_inject_mouse_move(app, cx, cy);
    dock_settle_hover(app, &st, dock_active_frame);
    fl_app_inject_mouse_button(app, FlMouseButton_Left, true);
    dock_release(app, &st, dock_active_frame);
    ASSERT_EQ_MSG((int)st.fx.ws->active_panel, 501, "tapping the input activated its panel");
    ASSERT_EQ_MSG(fl_focused_id(), right_input, "the tapped text input holds the ring the same frame it blurred");
}

UTEST(dock_workspace, hiding_the_active_panel_drops_the_active_id) {
    DOCK_ACTIVE_APP(app, st);
    ASSERT_TRUE_MSG(dock_activate_panel(app, &st, 501), "the right panel is active to start with");

    dock_workspace_set_panel_visible(st.fx.ws, 501, false);
    ASSERT_EQ_MSG((int)dock_active_after_tick(app, &st), 0, "a hidden panel cannot stay active");
}

UTEST(dock_workspace, closing_the_active_panel_drops_the_active_id) {
    DOCK_ACTIVE_APP(app, st);
    ASSERT_TRUE_MSG(dock_activate_panel(app, &st, 501), "the right panel is active to start with");

    FlDockRequest close = { .kind = FlDockRequest_Close, .panel = 501 };
    ASSERT_TRUE(dock_tree_apply(st.fx.ws->tree, &close));
    ASSERT_EQ_MSG((int)dock_active_after_tick(app, &st), 0, "a closed panel cannot stay active");
}

UTEST(dock_workspace, a_reserved_active_panel_drops_the_active_id) {
    DOCK_ACTIVE_APP(app, st);
    ASSERT_TRUE_MSG(dock_activate_panel(app, &st, 501), "the right panel is active to start with");

    st.skip_right = true;
    fl_app_tick(app, &st, dock_active_frame);
    ASSERT_TRUE_MSG(dock_workspace_panel_reserved(st.fx.ws, 501), "the undeclared panel's slot is reserved");
    ASSERT_EQ_MSG((int)st.fx.ws->active_panel, 0, "a reserved panel cannot stay active");
}

UTEST(dock_workspace, a_dragged_panel_stays_active) {
    DOCK_ACTIVE_APP(app, st);
    ASSERT_TRUE_MSG(dock_activate_panel(app, &st, 501), "the right panel is active to start with");

    FlDockTree* tree = st.fx.ws->tree;
    FlDockNodeId right_leaf = dock_tree_find_leaf_with_panel(tree, 501)->id;
    ASSERT_TRUE_MSG(dock_press_tab(app, &st, &st.fx, dock_active_frame, right_leaf, 0u),
                    "the right leaf's tab was not laid out");
    dock_move_and_tick(app, &st, dock_active_frame, 100, 300, 2); // past the arm threshold, over the left half
    ASSERT_EQ_MSG((int)tree->floating_count, 1, "the grabbed panel is in flight");
    ASSERT_EQ_MSG((int)st.fx.ws->active_panel, 501, "a dragged panel keeps the active id");

    ASSERT_EQ_MSG((int)dock_active_after_tick(app, &st), 501, "and keeps it for as long as the gesture runs");
    dock_release(app, &st, dock_active_frame);
}

UTEST(dock_workspace, showing_a_panel_never_steals_activation) {
    DOCK_ACTIVE_APP(app, st);
    ASSERT_TRUE_MSG(dock_activate_panel(app, &st, 501), "the right panel is active to start with");

    dock_workspace_set_panel_visible(st.fx.ws, 500, false);
    fl_app_tick(app, &st, dock_active_frame);
    dock_workspace_set_panel_visible(st.fx.ws, 500, true);
    ASSERT_EQ_MSG((int)dock_active_after_tick(app, &st), 501, "showing another panel does not activate it");
}

UTEST(dock_workspace, reshowing_the_hidden_active_panel_does_not_restore_it) {
    DOCK_ACTIVE_APP(app, st);
    ASSERT_TRUE_MSG(dock_activate_panel(app, &st, 501), "the right panel is active to start with");

    dock_workspace_set_panel_visible(st.fx.ws, 501, false);
    ASSERT_EQ_MSG((int)dock_active_after_tick(app, &st), 0, "hiding the active panel drops the id");

    dock_workspace_set_panel_visible(st.fx.ws, 501, true);
    ASSERT_EQ_MSG((int)dock_active_after_tick(app, &st), 0, "showing it again does not resurrect its activation");
}

UTEST(dock_workspace, a_fresh_workspace_reports_no_live_tiles) {
    FlUiCtx* fixture = render_fixture_get();
    ASSERT_TRUE_MSG(fixture != nullptr, "render fixture creation failed");
    DOCK_DEFAULT_TREE(scope, fixture, dock_leaf(100));

    dock_workspace_begin(scope.ws);
    ASSERT_FALSE_MSG(fl_dock_panel(100, FL_S("Body")), "a panel body cannot attach before the workspace has emitted");
    dock_workspace_end(scope.ws);
}

UTEST(dock_workspace, a_modal_blocks_activation_and_a_popup_does_not) {
    DOCK_ACTIVE_APP(app, st);
    ASSERT_TRUE_MSG(dock_activate_panel(app, &st, 501), "the right panel is active to start with");

    st.modal = true;
    dock_active_settle(app, &st);
    ASSERT_TRUE(dock_press_panel_tile(app, &st, 500));
    ASSERT_EQ_MSG((int)st.fx.ws->active_panel, 501, "a modal blocks a press from moving the active panel");
    dock_release(app, &st, dock_active_frame);

    st.modal = false;
    dock_active_settle(app, &st);
    ASSERT_EQ_MSG((int)st.fx.ctx->modal_scope_id, 0, "the modal closed");

    st.popup = true;
    dock_active_settle(app, &st);
    ASSERT_TRUE_MSG(st.fx.ctx->popup_focus_scope_id != 0, "the popup owns the focus scope");
    ASSERT_TRUE(dock_press_panel_tile(app, &st, 500));
    ASSERT_EQ_MSG((int)st.fx.ws->active_panel, 500, "a popup does not block panel activation");
    dock_release(app, &st, dock_active_frame);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A held press that moves less than FL_DOCK_DRAG_THRESHOLD does not undock. The probe travels past the tab widget's
// own 4px threshold, so the strip reports a grab, and stays inside the dock's.
UTEST(dock_workspace, drag_threshold_does_not_undock) {
    DOCK_APP(app, "dockarm", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_tab_header_frame, 1);
    FlDockTree* tree = st.ws->tree;

    i32 cx = 0;
    i32 cy = 0;
    ASSERT_TRUE_MSG(dock_tab_centre(&st, tree->root->id, 1u, &cx, &cy), "second dock tab was not laid out");
    dock_press_at(app, &st, dock_tab_header_frame, cx, cy);

    // 4 across and 2 down is 20px² of travel: past the widget substrate's 16, inside the dock's 25. Held for
    // several frames.
    dock_move_and_tick(app, &st, dock_tab_header_frame, cx + 4, cy + 2, 3);
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 101) == tree->root, "a sub-threshold press never detaches");
    ASSERT_EQ_MSG((int)tree->floating_count, 0, "and nothing reaches the holding pen");
    ASSERT_EQ_MSG((int)tree->root->leaf.selected, 1, "the press that stayed put selected the tab it landed on");

    // The same held press, once it travels past the threshold, does tear the panel out.
    dock_move_and_tick(app, &st, dock_tab_header_frame, cx + 40, cy + 20, 2);
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 101) == nullptr, "past the threshold the grab undocks");

    dock_release(app, &st, dock_tab_header_frame);
    ASSERT_EQ_MSG((int)tree->floating_count, 0, "the release empties the holding pen again");
}

// Escape is the cancel a dock filling the whole window depends on - there is no outside to release over. It must
// drop the target the cursor is currently resolving, not commit it, and it must not re-arm from the same held press.
UTEST(dock_workspace, escape_cancels_and_restores) {
    DOCK_APP(app, "dockesc", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_tab_header_frame, 1);
    FlDockTree* tree = st.ws->tree;
    ASSERT_EQ_MSG((int)tree->root->kind, (int)FlDockNodeKind_Leaf, "the tree starts as one leaf holding {100, 101}");

    ASSERT_TRUE_MSG(dock_press_tab(app, &st, &st, dock_tab_header_frame, tree->root->id, 1u),
                    "second dock tab was not laid out");

    // Hold it deep in the bottom of the workspace: a live drop zone, so releasing here would split the root.
    dock_move_and_tick(app, &st, dock_tab_header_frame, 300, 380, 3);
    ASSERT_TRUE_MSG(dock_tree_panel_is_floating(tree, 101), "the pull parks the panel in the holding pen");

    fl_app_inject_key(app, FlKey_Escape, true);
    fl_app_tick(app, &st, dock_tab_header_frame);
    fl_app_inject_key(app, FlKey_Escape, false);

    ASSERT_EQ_MSG((int)tree->root->kind, (int)FlDockNodeKind_Leaf,
                  "Escape drops the resolved target — a release at this cursor would have split the root");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 101) == dock_tree_find_leaf_with_panel(tree, 100),
                    "the panel returns to its committed slot");
    ASSERT_EQ_MSG((int)tree->floating_count, 0, "nothing is left in the holding pen");

    // The button is still down and the strip keeps reporting the grab, so the cancelled gesture must stay cancelled.
    dock_move_and_tick(app, &st, dock_tab_header_frame, 500, 380, 3);
    ASSERT_EQ_MSG((int)tree->root->kind, (int)FlDockNodeKind_Leaf,
                  "a cancelled gesture does not re-arm from the same press");
    ASSERT_EQ_MSG((int)tree->floating_count, 0, "and nothing drifts back into the holding pen");

    dock_release(app, &st, dock_tab_header_frame);
    ASSERT_EQ_MSG((int)dock_tree_collect_panels(tree, nullptr, 0), 2, "both panels conserved across the cancel");
}

// The gesture fixture with bodies: each panel declares one marker element filling its leaf's content area;
// a panel in flight declares no body.
static void dock_drag_body_frame(void* user_data) {
    FlDockWorkspace* ws = dock_fixture_emit((DockFixture*)user_data, dock_leaf(100, 101));
    dock_nest_panel(100);
    dock_nest_panel(101);
    dock_workspace_end(ws);
}

static FlLayoutId dock_overlay_fill_id(FlDockPanelId panel) {
    return (FlLayoutId) { .name = FL_S("fl_dock_drag_overlay_fill"), .id = (u32)panel };
}

// Exactly one thing may draw a dragged panel, and it is the ghost in the user's hand. The ghost is translucent, so
// the panel's own body must not draw anywhere for the whole flight - an app's panel background is opaque and would
// fill the ghost in, leaving a solid block riding the cursor.
UTEST(dock_workspace, dragged_panel_ghost_fills_the_gap_without_its_body) {
    DOCK_APP(app, "dockover", 600, 400);
    // Two frames before reading the strip: the bodies declare AFTER the emit, so frame one lays the tabs out with
    // no titles yet and frame two is the first with settled tab widths.
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_drag_body_frame, 2);
    FlDockTree* tree = st.ws->tree;
    ASSERT_FALSE_MSG(frame_has_rect(dock_overlay_fill_id(101)), "no overlay outside a gesture");

    ASSERT_TRUE_MSG(dock_press_tab(app, &st, &st, dock_drag_body_frame, tree->root->id, 1u),
                    "second dock tab was not laid out");

    dock_move_and_tick(app, &st, dock_drag_body_frame, 300, 300,
                       4); // grab reported, undock + settle, snapshot, steady state
    ASSERT_TRUE_MSG(dock_tree_panel_is_floating(tree, 101), "the panel is in flight");
    ASSERT_TRUE_MSG(frame_has_rect(dock_overlay_fill_id(101)), "and the ghost draws in its place");

    // The body of the panel in flight draws nowhere: not in the ghost, not in the slot it left. Panel 100 is the
    // control - an ordinary docked body still draws, so an empty draw list would not pass this by accident.
    ASSERT_FALSE_MSG(frame_has_rect((FlLayoutId) { .name = FL_S("nest_marker"), .id = 101 }),
                     "the held panel's body does not draw — the ghost stands in for it");
    ASSERT_TRUE_MSG(frame_has_rect((FlLayoutId) { .name = FL_S("nest_marker"), .id = 100 }),
                    "while every other panel keeps drawing normally");

    Clay_ElementData overlay = dock_layout_element(dock_overlay_fill_id(101));
    ASSERT_TRUE_MSG(overlay.found, "the ghost laid out");
    ASSERT_TRUE_MSG(overlay.boundingBox.width > 0.0f, "the ghost has room");

    // Over a resolved target the ghost IS the gap: it attaches to that leaf's element, so the two are laid out in
    // the same pass and cannot disagree even on the frame a re-target mints the leaf.
    ASSERT_NE_MSG((int)st.ws->drag.zone, (int)FlDockDropZone_None, "the cursor is over a live target");
    ASSERT_NE_MSG((int)st.ws->drag.gap_leaf, 0, "which opened a gap for the ghost to fill");
    Clay_ElementData gap = dock_element(dock_layout_node_element_id(st.ws->drag.gap_leaf));
    ASSERT_TRUE_MSG(gap.found, "the gap leaf is tiled");
    ASSERT_NEAR_MSG(overlay.boundingBox.x, gap.boundingBox.x, 1.0f, "the ghost sits exactly on its gap");
    ASSERT_NEAR_MSG(overlay.boundingBox.y, gap.boundingBox.y, 1.0f, "the ghost sits exactly on its gap");
    ASSERT_NEAR_MSG(overlay.boundingBox.width, gap.boundingBox.width, 1.0f, "and fills it");

    // The overlay is gesture-scoped: the release puts the panel back in a tile and takes the overlay away.
    dock_release(app, &st, dock_drag_body_frame);
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

// panel_should_emit promises the body is drawn: for every frame of a drag it must agree with whether the
// dock_panel scope opens, and a floating panel must do neither.
UTEST(dock_workspace, should_emit_matches_the_drawn_body_throughout_a_drag) {
    DOCK_APP(app, "dockagree", 600, 400);
    DockEmitAgreeState st = { 0 };
    DOCK_RUN_FRAMES(app, st, st.fx, dock_emit_agree_frame, 2);
    FlDockTree* tree = st.fx.ws->tree;

    ASSERT_TRUE_MSG(dock_press_tab(app, &st, &st.fx, dock_emit_agree_frame, tree->root->id, 1u),
                    "second dock tab was not laid out");

    dock_move_and_tick(app, &st, dock_emit_agree_frame, 300, 300, 6);
    ASSERT_TRUE_MSG(dock_tree_panel_is_floating(tree, 101), "the panel is in flight");
    ASSERT_GT_MSG(st.floating_frames, 0, "frames of the flight were observed");

    dock_release(app, &st, dock_emit_agree_frame);
    fl_app_tick(app, &st, dock_emit_agree_frame);

    ASSERT_GT_MSG(st.frames_checked, st.floating_frames, "docked frames were checked as well");
    ASSERT_EQ_MSG(st.frames_disagreed, 0, "should_emit matched the dock_panel scope on every frame");
    ASSERT_EQ_MSG(st.floating_emitted, 0, "a floating panel neither reports emit nor draws its body");
    ASSERT_EQ_MSG(st.floating_sized, 0, "a floating panel has no room to report");
}

// Dragging a tab OUT of its leaf header is the grab gesture: it undocks the panel into the internal-floating set
// and arms drag-to-dock, then the release commits the resolved drop. The whole gesture lives inside
// dock_workspace_emit.
UTEST(dock_workspace, tab_drag_out_undocks_then_redocks) {
    DOCK_APP(app, "dockgrab", 600, 400);
    DockFixture st = { 0 };

    DOCK_RUN_FRAMES(app, st, st, dock_tab_header_frame, 1);
    FlDockTree* tree = st.ws->tree;
    FlDockNodeId leaf_id = tree->root->id;

    ASSERT_TRUE_MSG(dock_press_tab(app, &st, &st, dock_tab_header_frame, leaf_id, 1u),
                    "second dock tab was not laid out");

    // A press alone is still just a selection - the tabs opt into FlItem_DragThreshold, so nothing detaches until
    // the cursor travels.
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 101) != nullptr, "a press must not detach the panel");
    ASSERT_EQ_MSG((int)tree->floating_count, 0, "a press under the threshold is not a drag");

    // Travel across the workspace - past the drag threshold, and onto the TAB BAND of the leaf the tab is being
    // pulled out of. The strip reports the grab during that frame's walk; the next frame's safe point turns it
    // into the undock (structural, so never mid-walk) and arms the drag.
    dock_move_and_tick(app, &st, dock_tab_header_frame, 400, 20, 2);
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 101) == nullptr, "the grabbed panel is undocked");
    ASSERT_EQ_MSG((int)tree->floating_count, 1, "the grabbed panel lands in the internal-floating set");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 100) != nullptr, "the panel left behind keeps its leaf");

    // One more frame so dock_drag_emit resolves the zone under the cursor, then release.
    fl_app_tick(app, &st, dock_tab_header_frame);
    dock_release(app, &st, dock_tab_header_frame);
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 101) == dock_tree_find_leaf_with_panel(tree, 100),
                    "releasing over the tab band re-docks the panel as a tab of that leaf");
    DOCK_ASSERT_GESTURE_ENDED(tree);
}

// Drop resolution is denominated in LOGICAL px: a 1200x800 window at zoom 2 is a 600x400 logical tile. The release
// lands Bottom in that tile; dividing the cursor but not the snapshot rects (or vice versa) resolves Left instead,
// which is what makes this point discriminating.
UTEST(dock_workspace, drop_resolution_is_logical_px_under_zoom) {
    DOCK_APP(app, "dockzoom", 1200, 800);
    fl_app_set_ui_zoom(app, 2.0f);

    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_tab_header_frame, 1);
    ASSERT_NEAR_MSG(ui_get_scale(), 2.0f, 1e-4f, "the zoom composed into the UI scale");
    FlDockTree* tree = st.ws->tree;

    ASSERT_TRUE_MSG(dock_press_tab(app, &st, &st, dock_tab_header_frame, tree->root->id, 1u),
                    "second dock tab was not laid out");

    // Physical (600, 760) is logical (300, 380): dead centre horizontally, 95% of the way down.
    dock_move_and_tick(app, &st, dock_tab_header_frame, 600, 760, 2);
    ASSERT_TRUE_MSG(dock_tree_panel_is_floating(tree, 101), "pulling a tab out must arm the drag on that panel");

    fl_app_tick(app, &st, dock_tab_header_frame);
    dock_release(app, &st, dock_tab_header_frame);

    ASSERT_EQ_MSG((int)tree->root->kind, (int)FlDockNodeKind_Split, "the bottom quadrant splits the survivor");
    ASSERT_EQ_MSG((int)tree->root->split.axis, (int)FlDockAxis_Y, "a Bottom split is a Y split, not an X one");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 101) == tree->root->split.child[1],
                    "101 lands BELOW the survivor, which is what the logical geometry resolves to");
    ASSERT_EQ_MSG((int)dock_tree_collect_panels(tree, nullptr, 0), 2, "both panels conserved across the gesture");
}

// Dragging out a leaf's ONLY tab. The drop must target the geometry the user now SEES, which is what the drag's
// settle frame buys - it lets the post-detach tiles lay out and snapshots those. The release below lands right of
// the workspace centre: inside the survivor's tab band once it has reflowed to fill the workspace, and outside the
// pre-detach left tile entirely.
static void dock_grab_split_frame(void* user_data) {
    // Two one-panel leaves side by side: {200} | {201}, each a single-tab header (its own grab handle).
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

    ASSERT_TRUE_MSG(dock_press_tab(app, &st, &st, dock_grab_split_frame, left_leaf, 0u),
                    "a single-panel leaf must still render a tab");
    dock_move_and_tick(app, &st, dock_grab_split_frame, 450, 20, 2); // the header reports the grab

    ASSERT_TRUE_MSG(dock_tree_panel_is_floating(tree, 200), "the panel is being dragged");
    ASSERT_EQ_MSG((int)tree->root->kind, (int)FlDockNodeKind_Leaf,
                  "emptying a leaf collapses it and dissolves the split — the survivor becomes the root");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 201) == tree->root, "201's leaf is the survivor");

    // Snapshot frame (reads back the reflowed tiles), then release over the survivor's tab band.
    fl_app_tick(app, &st, dock_grab_split_frame);
    dock_release(app, &st, dock_grab_split_frame);

    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 200) != nullptr,
                    "the drop must resolve against the REFLOWED survivor, not the collapsed pre-detach rects");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 200) == dock_tree_find_leaf_with_panel(tree, 201),
                    "released on the survivor's tab band, so 200 re-docks as a tab of it");
    DOCK_ASSERT_GESTURE_ENDED(tree);
}

// The second cancel trigger: releasing outside the workspace. It lands on the same path Escape does, returning the
// panel to the seat it came from. Pulling a leaf's ONLY tab collapses that leaf and dissolves its split, so the
// seat is gone by release and the floor rule has to catch the panel.
UTEST(dock_workspace, release_outside_workspace_cancels) {
    DOCK_APP(app, "dockcancel", 600, 400);
    // Two side-by-side single-tab leaves {200} | {201}: dragging 200 out collapses its leaf (the hard shape).
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_grab_split_frame, 1);
    FlDockTree* tree = st.ws->tree;

    FlDockNode* origin = dock_tree_find_leaf_with_panel(tree, 200);
    ASSERT_TRUE_MSG(origin != nullptr, "200 starts docked");
    ASSERT_TRUE_MSG(dock_press_tab(app, &st, &st, dock_grab_split_frame, origin->id, 0u), "200's tab was not laid out");

    // Pull far outside the workspace: past the drag threshold, and the only place a drop still misses.
    dock_move_and_tick(app, &st, dock_grab_split_frame, 5000, 5000, 3);
    ASSERT_TRUE_MSG(dock_tree_panel_is_floating(tree, 200), "the pull arms the drag");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 200) == nullptr, "and the grab undocked it");

    dock_release(app, &st, dock_grab_split_frame);

    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 200) != nullptr,
                    "a release over no zone must seat the panel, not strand it in the floating set");
    DOCK_ASSERT_GESTURE_ENDED(tree);
}

// The other shape of the same cancel: the origin leaf holds TWO tabs, so pulling one leaves the leaf standing.
// The release lands outside the workspace, and the panel must go back into the seat it still has.
UTEST(dock_workspace, release_outside_workspace_rejoins_the_surviving_leaf) {
    DOCK_APP(app, "dockcancel2", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_tab_header_frame, 1);
    FlDockTree* tree = st.ws->tree;
    FlDockNode* origin = tree->root;
    ASSERT_EQ_MSG((int)origin->kind, (int)FlDockNodeKind_Leaf, "the tree starts as one leaf holding {100, 101}");
    ASSERT_EQ_MSG((int)origin->leaf.count, 2, "the origin leaf holds two tabs");

    ASSERT_TRUE_MSG(dock_press_tab(app, &st, &st, dock_tab_header_frame, origin->id, 1u), "101's tab was not laid out");

    // Pull far outside the workspace: past the drag threshold, and the only place a drop still misses.
    dock_move_and_tick(app, &st, dock_tab_header_frame, 5000, 5000, 3);
    ASSERT_TRUE_MSG(dock_tree_panel_is_floating(tree, 101), "the pull arms the drag");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 101) == nullptr, "and the grab undocked it");
    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 100) == tree->root,
                    "the origin leaf keeps its other tab, so it survives the pull");

    dock_release(app, &st, dock_tab_header_frame);

    ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, 101) == dock_tree_find_leaf_with_panel(tree, 100),
                    "an aimless drop returns the panel to the leaf it was pulled from");
    DOCK_ASSERT_GESTURE_ENDED(tree);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Make-space - the hit-test runs on the live provisional layout, so the gap the drag opens is inside the geometry
// that resolves the next frame's target ("preview feedback"). These drive the real loop because the
// feedback only exists once Clay has laid the gap out.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Pull the root leaf's second tab (101) out into a live drag and leave the cursor at (cx, cy), button still down.
// False when the frame did not run, the tab was not laid out, or the pull did not arm a drag.
static bool dock_start_drag(FlApp* app, DockFixture* st, i32 cx, i32 cy) {
    fl_app_tick(app, st, dock_tab_header_frame);
    if (st->ctx == nullptr || st->ws == nullptr) {
        return false;
    }
    FlDockTree* tree = st->ws->tree;

    if (!dock_press_tab(app, st, st, dock_tab_header_frame, tree->root->id, 1u)) {
        return false;
    }

    dock_move_and_tick(app, st, dock_tab_header_frame, cx, cy, 4);
    return dock_drag_active(&st->ws->drag);
}

// The load-bearing case: sweeping the cursor across the workspace resolves targets MONOTONICALLY. Every zone the
// sweep leaves is left for good - a target that comes back is the gap having moved the geometry that chose it.
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

    // 20px steps clear the 8px switch hysteresis, so a repeated zone is the layout flickering and not the cursor
    // sitting on a boundary. Both ticks per step are sampled: a one-frame flick counts.
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

    dock_release(app, &st, dock_tab_header_frame);
}

// The drag overlay has the rect of its gap leaf on every frame of a sweep, including the frames where a re-target
// mints a new gap leaf.
UTEST(dock_workspace, overlay_never_lags_the_gap_across_a_retarget) {
    DOCK_APP(app, "docklag", 600, 400);
    DockFixture st = { 0 };
    ASSERT_TRUE_MSG(dock_start_drag(app, &st, 400, 20), "the pull did not arm a drag");
    const FlDockDrag* drag = &st.ws->drag;

    // A sweep that re-targets repeatedly: each move mints a fresh gap leaf.
    static const struct {
        i32 x, y;
    } sweep[] = { { 100, 300 }, { 300, 380 }, { 550, 200 }, { 100, 300 }, { 300, 40 }, { 550, 200 } };

    int retargets = 0;
    u32 last_gap = 0;
    for (u32 p = 0; p < sizeof_array(sweep); ++p) {
        fl_app_inject_mouse_move(app, sweep[p].x, sweep[p].y);
        for (int i = 0; i < 4; ++i) {
            fl_app_tick(app, &st, dock_tab_header_frame);
            if (drag->gap_leaf == 0) {
                continue;
            }
            if (drag->gap_leaf != last_gap) {
                retargets++;
                last_gap = drag->gap_leaf;
            }
            Clay_ElementData gap = dock_element(dock_layout_node_element_id(drag->gap_leaf));
            Clay_ElementData overlay = dock_layout_element(
                (FlLayoutId) { .name = FL_S("fl_dock_drag_overlay_fill"), .id = (u32)drag->panel });
            if (!gap.found || !overlay.found) {
                continue;
            }
            ASSERT_NEAR_MSG(overlay.boundingBox.x, gap.boundingBox.x, 1.0f, "the overlay is never left behind its gap");
            ASSERT_NEAR_MSG(overlay.boundingBox.y, gap.boundingBox.y, 1.0f, "the overlay is never left behind its gap");
            ASSERT_NEAR_MSG(overlay.boundingBox.width, gap.boundingBox.width, 1.0f, "nor sized to a stale one");
            ASSERT_NEAR_MSG(overlay.boundingBox.height, gap.boundingBox.height, 1.0f, "nor sized to a stale one");
        }
    }
    ASSERT_TRUE_MSG(retargets >= 3, "the sweep really did re-target, so the risky frames were exercised");

    dock_release(app, &st, dock_tab_header_frame);
}

// The node a gap took its space from keeps hit-testing at the rect it held before the gap opened.
//
// The probe is a cursor the two footprints disagree about. In a 600-wide leaf the left quadrant reaches x=150; once
// a left gap has taken 35%, the surviving tile is [210,600] and its left quadrant reaches x=307. So x=250 is Bottom
// against the frozen rect and Left against the live one, and it sits outside the gap.
UTEST(dock_workspace, pre_gap_footprint_freezes_the_boundary_node) {
    DOCK_APP(app, "dockpregap", 600, 400);
    DockFixture st = { 0 };
    ASSERT_TRUE_MSG(dock_start_drag(app, &st, 100, 300), "the pull did not arm a drag");
    const FlDockDrag* drag = &st.ws->drag;
    ASSERT_EQ_MSG((int)drag->zone, (int)FlDockDropZone_Left, "100px into a 600px-wide leaf is its left quadrant");

    // 101 was pulled out of the root leaf, which still holds 100 - that leaf is the node the gap splits.
    FlDockNodeId survivor = st.ws->tree->root->id;
    ASSERT_EQ_MSG((int)drag->pre_gap_node, (int)survivor, "the gap's sibling is the node whose footprint is frozen");
    ASSERT_TRUE_MSG(drag->has_pre_gap, "and the drag stored it");

    dock_ticks(app, &st, dock_tab_header_frame, 30);

    // The tile really has shrunk - otherwise the probe below would prove nothing.
    const f32 live_w = dock_node_width(survivor);
    ASSERT_TRUE_MSG(live_w > 0.0f && live_w < 0.8f * 600.0f, "the open gap has taken its share of the tile");
    ASSERT_NEAR_MSG(drag->pre_gap.width, 600.0f, 1.0f, "yet the frozen footprint is still the whole workspace");

    dock_move_and_tick(app, &st, dock_tab_header_frame, 250, 300, 2);
    ASSERT_EQ_MSG((int)drag->zone, (int)FlDockDropZone_Bottom,
                  "the quadrant resolves against the pre-gap rect; the shrunken tile would still read Left here");
    ASSERT_EQ((int)drag->request.target_node, (int)survivor);

    dock_release(app, &st, dock_tab_header_frame);
}

// A still cursor gets a still gap. The gap is inside the geometry the next frame resolves against, so "it settles"
// is a claim about the loop converging, not about one hit-test being deterministic.
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

    dock_release(app, &st, dock_tab_header_frame);
}

// Re-targeting is a mutation of the RETAINED preview, not a rebuild: the same tree carries a new gap slot.
UTEST(dock_workspace, retarget_mutates_the_retained_preview) {
    DOCK_APP(app, "dockretarget", 600, 400);
    DockFixture st = { 0 };
    ASSERT_TRUE_MSG(dock_start_drag(app, &st, 100, 300), "the pull did not arm a drag");
    const FlDockDrag* drag = &st.ws->drag;

    dock_ticks(app, &st, dock_tab_header_frame, 30);
    ASSERT_EQ((int)drag->zone, (int)FlDockDropZone_Left);
    const FlDockNodeId next_id_before = drag->preview->next_node_id;
    FlDockNodeId first_gap = drag->gap_leaf;

    // Straight across to the far side - a different quadrant of the same leaf.
    dock_move_and_tick(app, &st, dock_tab_header_frame, 500, 300, 1);

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

    dock_release(app, &st, dock_tab_header_frame);
}

// The preview is a clone; nothing the drag did to it may reach the committed tree. Abandoning the drop must leave
// the shape the gesture started from, gap and re-targets notwithstanding.
UTEST(dock_workspace, abandoned_drop_restores_the_committed_shape) {
    DOCK_APP(app, "dockabandon", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_tab_header_frame, 1);
    FlDockTree* tree = st.ws->tree;

    ASSERT_TRUE_MSG(dock_press_tab(app, &st, &st, dock_tab_header_frame, tree->root->id, 1u),
                    "101's tab was not laid out");

    // The committed shape as the gesture found it - the press has already selected the tab it landed on.
    FlDockTree* before = dock_tree_clone(tree);

    // Three different targets, each given time to open, then a release outside the workspace.
    const i32 stops[][2] = { { 100, 300 }, { 500, 300 }, { 300, 380 } };
    for (u32 s = 0; s < sizeof_array(stops); ++s) {
        dock_move_and_tick(app, &st, dock_tab_header_frame, stops[s][0], stops[s][1], 6);
    }
    ASSERT_TRUE_MSG(dock_tree_panel_is_floating(tree, 101), "the committed tree holds the panel loose mid-gesture");

    dock_move_and_tick(app, &st, dock_tab_header_frame, 5000, 5000, 1);
    dock_release(app, &st, dock_tab_header_frame);

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

    dock_release(app, &st, dock_tab_header_frame);

    ASSERT_EQ_MSG((int)tree->root->kind, (int)FlDockNodeKind_Split, "the drop split the root");
    ASSERT_EQ((int)tree->root->split.axis, (int)FlDockAxis_X);
    ASSERT_EQ_MSG((int)leaf_panel0(tree->root->split.child[0]), 101, "the dropped panel took the left side");
    ASSERT_NEAR_MSG(tree->root->split.ratio, FL_DOCK_GAP_RATIO, 1e-5f,
                    "and the committed split is exactly the share the gap showed");
    ASSERT_TRUE(dock_tree_check_invariants(tree));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dock build - the declarative first-run layout literal (dock_split/dock_leaf, ui/dock.h) lowered onto the tree by
// dock_tree_build, validated by the invariant checker. Pure data + algebra.

UTEST(dock_build, literal_to_tree) {
    enum { NAV = 101, VIEWPORT = 202, CONSOLE = 303, LOG = 404 };
    const FlDockSpec* spec = dock_split(FlDockAxis_X, 0.22f, dock_leaf(NAV),
                                        dock_split(FlDockAxis_Y, 0.70f, dock_leaf(VIEWPORT), dock_leaf(CONSOLE, LOG)));

    FlDockTree* tree = dock_tree_build(spec);
    ASSERT_TRUE_MSG(tree != nullptr, "a valid literal must lower to a tree");
    ASSERT_TRUE(dock_tree_check_invariants(tree));

    FlDockNode* root = tree->root;
    ASSERT_EQ((int)root->kind, (int)FlDockNodeKind_Split);
    ASSERT_EQ((int)root->split.axis, (int)FlDockAxis_X);
    ASSERT_NEAR(root->split.ratio, 0.22f, 0.001f);

    FlDockNode* nav = root->split.child[0];
    ASSERT_EQ((int)nav->kind, (int)FlDockNodeKind_Leaf);
    ASSERT_EQ((int)nav->leaf.count, 1);
    ASSERT_EQ((int)nav->leaf.panels[0], NAV);

    FlDockNode* right = root->split.child[1];
    ASSERT_EQ((int)right->kind, (int)FlDockNodeKind_Split);
    ASSERT_EQ((int)right->split.axis, (int)FlDockAxis_Y);
    ASSERT_NEAR(right->split.ratio, 0.70f, 0.001f);

    FlDockNode* viewport = right->split.child[0];
    ASSERT_EQ((int)viewport->kind, (int)FlDockNodeKind_Leaf);
    ASSERT_EQ((int)viewport->leaf.panels[0], VIEWPORT);

    // The bottom tab group: two panels, first listed selected.
    FlDockNode* bottom = right->split.child[1];
    ASSERT_EQ((int)bottom->kind, (int)FlDockNodeKind_Leaf);
    ASSERT_EQ((int)bottom->leaf.count, 2);
    ASSERT_EQ((int)bottom->leaf.panels[0], CONSOLE);
    ASSERT_EQ((int)bottom->leaf.panels[1], LOG);
    ASSERT_EQ((int)bottom->leaf.selected, 0);

    ASSERT_TRUE(nav->parent == root && right->parent == root);
    ASSERT_TRUE(viewport->parent == right && bottom->parent == right);
    ASSERT_TRUE(root->parent == nullptr);
    ASSERT_EQ((int)dock_tree_collect_panels(tree, nullptr, 0), 4);
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(tree, LOG) == bottom);

    dock_tree_destroy(tree);

    // A hand-built spec exercises the selected lowering the dock_leaf macro defaults to 0: a two-tab leaf with
    // the second tab selected.
    const FlDockPanelId pair[] = { 700, 800 };
    FlDockSpec sel = { .is_split = false, .panels = pair, .panel_count = 2, .selected = 1 };
    FlDockTree* t2 = dock_tree_build(&sel);
    ASSERT_TRUE(t2 != nullptr);
    ASSERT_EQ((int)t2->root->leaf.selected, 1);
    ASSERT_EQ((int)t2->root->leaf.panels[1], 800);
    dock_tree_destroy(t2);

    // A null spec yields the floor: a single empty root leaf, invariant-clean.
    FlDockTree* floor = dock_tree_build(nullptr);
    ASSERT_TRUE(floor != nullptr);
    ASSERT_EQ((int)floor->root->kind, (int)FlDockNodeKind_Leaf);
    ASSERT_EQ((int)floor->root->leaf.count, 0);
    ASSERT_TRUE(dock_tree_check_invariants(floor));
    dock_tree_destroy(floor);
}

UTEST(dock_build, invalid_literal_fails) {
    const FlDockSpec* dup = dock_split(FlDockAxis_X, 0.5f, dock_leaf(42), dock_leaf(42));
    ASSERT_TRUE_MSG(dock_tree_build(dup) == nullptr, "duplicate panel id must not lower to a tree");

    const FlDockSpec* missing = dock_split(FlDockAxis_Y, 0.5f, dock_leaf(1), nullptr);
    ASSERT_TRUE_MSG(dock_tree_build(missing) == nullptr, "a missing split child must not lower to a tree");
}

UTEST(dock_build, set_tree_into_workspace) {
    // dock_workspace_set_tree is the path an app installs an authored default: build + validate, hand the tree to
    // the workspace, which adopts ownership and destroys its previous tree.
    FlDockWorkspace* ws = dock_workspace_create();
    ASSERT_TRUE(ws->tree != nullptr);
    ASSERT_TRUE(dock_tree_check_invariants(ws->tree)); // the fresh floor the workspace was created with
    ASSERT_EQ((int)dock_tree_collect_panels(ws->tree, nullptr, 0), 0);

    FlDockTree* authored = dock_tree_build(dock_split(FlDockAxis_X, 0.5f, dock_leaf(10), dock_leaf(20)));
    ASSERT_TRUE(authored != nullptr);

    dock_workspace_set_tree(ws, authored);
    ASSERT_EQ((int)dock_tree_collect_panels(ws->tree, nullptr, 0), 2);
    ASSERT_EQ((int)dock_workspace_panel_leaf(ws, 10), (int)dock_tree_find_leaf_with_panel(ws->tree, 10)->id);
    ASSERT_EQ((int)dock_workspace_panel_leaf(ws, 20), (int)dock_tree_find_leaf_with_panel(ws->tree, 20)->id);

    // Identity no-op: re-installing the same tree must not destroy it (no use-after-free).
    dock_workspace_set_tree(ws, authored);
    ASSERT_TRUE(dock_tree_check_invariants(ws->tree));
    ASSERT_EQ((int)dock_tree_collect_panels(ws->tree, nullptr, 0), 2);

    // Null guard: a failed dock_tree_build (null) leaves the existing tree in place.
    dock_workspace_set_tree(ws, nullptr);
    ASSERT_TRUE(dock_tree_check_invariants(ws->tree));
    ASSERT_EQ((int)dock_tree_collect_panels(ws->tree, nullptr, 0), 2);

    // Replacing with a second authored tree swaps it in and destroys the first.
    FlDockTree* second = dock_tree_build(dock_leaf(99));
    ASSERT_TRUE(second != nullptr);
    dock_workspace_set_tree(ws, second);
    ASSERT_EQ((int)dock_tree_collect_panels(ws->tree, nullptr, 0), 1);
    ASSERT_EQ((int)dock_workspace_panel_leaf(ws, 99), (int)dock_tree_find_leaf_with_panel(ws->tree, 99)->id);
    ASSERT_EQ((int)dock_workspace_panel_leaf(ws, 99), (int)second->root->id);

    dock_workspace_destroy(ws); // destroys second; authored was already destroyed by the swap
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dock persistence - serialize/restore a workspace layout to an opaque blob + the rebind-on-load rules
// (ui/dock/dock_persist.{h,c}). Pure data in / data out, so every case is
// headless.

// A directory under the system temp dir, named for this process and case, emptied on creation and removed when the
// scope ends.
typedef struct DockTempDir {
    FlArena* arena;
    FlString path;
} DockTempDir;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static DockTempDir dock_temp_dir_make(const char* name) {
    DockTempDir dir = { .arena = arena_new() };
    FlString leaf = fl_sprintf_arena(dir.arena, "flowi_dock_%s_%u", name, get_process_id_os());
    dir.path = file_build_path_os(dir.arena, get_temp_directory_os(dir.arena), leaf);
    file_remove_directory_recursive_os(dir.path);
    return dir;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void dock_temp_dir_remove(DockTempDir* dir) {
    file_remove_directory_recursive_os(dir->path);
    fl_arena_destroy(dir->arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Shuts the log file down if it was opened.
static void dock_log_file_close(bool* open) {
    if (*open) {
        fl_log_file_shutdown();
    }
}

#define DOCK_TEMP_DIR(var, name) \
    __attribute__((cleanup(dock_temp_dir_remove))) DockTempDir var = dock_temp_dir_make(name)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Parses JSON text, with the relaxed flags the persisted layout is read with, into the arena.
static FlJsonValue* dock_json_parse(FlArena* arena, FlString text) {
    return fl_json_parse_with_flags(arena, text.data, text.length, FlJsonParseFlags_AllowJson5, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Serializes trees as workspaces with ids 1..count, destroys the source trees and loads the blob back.
static FlDockPersistLoad dock_persist_reload(FlArena* arena, FlDockTree* const* trees, u32 count) {
    enum { MAX_WORKSPACES = 4 };
    FlDockPersistWorkspace workspaces[MAX_WORKSPACES] = { 0 };
    if (count > MAX_WORKSPACES) {
        return (FlDockPersistLoad) { 0 };
    }
    for (u32 i = 0; i < count; ++i) {
        workspaces[i] = (FlDockPersistWorkspace) { .id = i + 1, .tree = trees[i] };
    }
    FlDockPersistLoad load = dock_persist_from_json(dock_persist_to_json(workspaces, count, arena), arena);
    for (u32 i = 0; i < count; ++i) {
        dock_tree_destroy(trees[i]);
    }
    return load;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// True when tree serializes, loads back as one workspace, and the loaded tree is invariant-clean and equal to it.
static bool persist_round_trips(FlDockTree* tree) {
    DOCK_ARENA(arena);
    FlDockPersistWorkspace pw
        = { .id = 1, .family_filter = 0, .host_width = 800.0f, .host_height = 600.0f, .tree = tree };
    FlJsonValue* blob = dock_persist_to_json(&pw, 1, arena);
    if (blob == nullptr) {
        return false;
    }
    FlDockPersistLoad load = dock_persist_from_json(blob, arena);
    if (!load.ok || load.count != 1) {
        return false;
    }
    const bool equal
        = dock_tree_check_invariants(load.workspaces[0].tree) && dock_persist_tree_equal(tree, load.workspaces[0].tree);
    dock_tree_destroy(load.workspaces[0].tree);
    return equal;
}

UTEST(dock_persist, round_trip_corpus) {
    // A corpus spanning the structural variety: the floor, a single leaf, a multi-tab leaf with a non-zero
    // selection, a deeply nested split tree, and full 64-bit panel ids (the string id-encoding must not lose the
    // top bit).
    FlDockTree* floor = dock_tree_build(nullptr);
    ASSERT_TRUE(persist_round_trips(floor));
    dock_tree_destroy(floor);

    FlDockTree* single = dock_tree_build(dock_leaf(101));
    ASSERT_TRUE(persist_round_trips(single));
    dock_tree_destroy(single);

    const FlDockPanelId tabs[] = { 201, 202, 203 };
    FlDockSpec multi = { .is_split = false, .panels = tabs, .panel_count = 3, .selected = 2 };
    FlDockTree* tabbed = dock_tree_build(&multi);
    ASSERT_TRUE(persist_round_trips(tabbed));
    dock_tree_destroy(tabbed);

    FlDockTree* nested = dock_tree_build(
        dock_split(FlDockAxis_X, 0.3f, dock_leaf(1), dock_split(FlDockAxis_Y, 0.6f, dock_leaf(2, 3), dock_leaf(4))));
    // Re-select the second tab in the (2,3) leaf and undock 4 to the floating set, so the corpus tree carries a
    // non-default selection AND a loose panel.
    FlDockNode* leaf23 = dock_tree_find_leaf_with_panel(nested, 3);
    FlDockRequest reselect = { .kind = FlDockRequest_Tab, .target_node = leaf23->id, .panel = 3 };
    ASSERT_TRUE(dock_tree_apply(nested, &reselect));
    FlDockRequest undock = { .kind = FlDockRequest_Undock, .panel = 4 };
    ASSERT_TRUE(dock_tree_apply(nested, &undock));
    ASSERT_TRUE(dock_tree_panel_is_floating(nested, 4));
    ASSERT_TRUE(persist_round_trips(nested));
    dock_tree_destroy(nested);

    const FlDockPanelId big[] = { 0xFFFFFFFFFFFFFFF1ull, 0x8000000000000007ull };
    FlDockSpec wide = { .is_split = false, .panels = big, .panel_count = 2, .selected = 1 };
    FlDockTree* wide_ids = dock_tree_build(&wide);
    ASSERT_TRUE(persist_round_trips(wide_ids));
    dock_tree_destroy(wide_ids);
}

UTEST(dock_persist, round_trip_attributes) {
    // The per-workspace attributes (id + family filter + host SIZE) round-trip; position has no field, so it can
    // never be persisted ("size but not position").
    DOCK_ARENA(arena);
    FlDockTree* tree = dock_tree_build(dock_leaf(7));
    FlDockPersistWorkspace pw = {
        .id = 0xABCDEF0123456789ull, .family_filter = 9, .host_width = 1280.5f, .host_height = 720.25f, .tree = tree
    };
    FlJsonValue* blob = dock_persist_to_json(&pw, 1, arena);
    ASSERT_TRUE(blob != nullptr);
    FlDockPersistLoad load = dock_persist_from_json(blob, arena);
    ASSERT_TRUE(load.ok);
    ASSERT_EQ((int)load.count, 1);
    ASSERT_TRUE(load.workspaces[0].id == 0xABCDEF0123456789ull);
    ASSERT_EQ((int)load.workspaces[0].family_filter, 9);
    ASSERT_NEAR(load.workspaces[0].host_width, 1280.5f, 0.01f);
    ASSERT_NEAR(load.workspaces[0].host_height, 720.25f, 0.01f);
    dock_tree_destroy(load.workspaces[0].tree);
    dock_tree_destroy(tree);
}

UTEST(dock_persist, rebind_bad_version_to_default) {
    // bad-version-to-default: a syntactically valid blob with a mismatched version reports failure so the caller
    // falls back to its code-defined default layout.
    DOCK_ARENA(arena);

    FlJsonValue* bad_blob = dock_json_parse(arena, FL_S("{ \"version\": 999, \"workspaces\": [] }"));
    ASSERT_TRUE(bad_blob != nullptr);
    ASSERT_FALSE(dock_persist_from_json(bad_blob, arena).ok);

    ASSERT_FALSE(dock_persist_from_json(nullptr, arena).ok);

    FlJsonValue* ok_blob = dock_json_parse(
        arena, fl_sprintf_arena(arena, "{ \"version\": %d, \"workspaces\": [] }", FL_DOCK_PERSIST_VERSION));
    FlDockPersistLoad ok_load = dock_persist_from_json(ok_blob, arena);
    ASSERT_TRUE(ok_load.ok);
    ASSERT_EQ((int)ok_load.count, 0);
}

// Build a depth-level chain of splits, each nesting the next in child0 and closing child1 with a leaf.
static FlString deep_blob(FlArena* arena, u32 depth) {
    const u64 cap = 256 * (u64)(depth + 2);
    char* buf = arena_alloc_array(arena, char, cap);
    u64 n = (u64)snprintf(buf, cap,
                          "{ \"version\": %d, \"workspaces\": [ { \"id\": \"1\", \"next_node_id\": %u, "
                          "\"floating\": \"\", \"root\": ",
                          FL_DOCK_PERSIST_VERSION, depth + 2);
    for (u32 i = 0; i < depth; ++i) {
        n += (u64)snprintf(buf + n, cap - n, "{ \"kind\": %d, \"id\": %u, \"axis\": 0, \"ratio\": 0.5, \"child0\": ",
                           FlDockNodeKind_Split, i + 1);
    }
    n += (u64)snprintf(buf + n, cap - n, "{ \"kind\": %d, \"id\": 1000, \"selected\": 0, \"panels\": \"1\" }",
                       FlDockNodeKind_Leaf);
    for (u32 i = 0; i < depth; ++i) {
        n += (u64)snprintf(buf + n, cap - n,
                           ", \"child1\": { \"kind\": %d, \"id\": %u, \"selected\": 0, \"panels\": \"%u\" } }",
                           FlDockNodeKind_Leaf, 2000 + i, 2000 + i);
    }
    n += (u64)snprintf(buf + n, cap - n, " } ] }");
    return string_from_cstr_len(buf, n);
}

UTEST(dock_persist, hostile_blob_depth_and_id_overflow) {
    // Neither an over-nested tree nor an overlong digit run reaches the tree builder intact.
    DOCK_ARENA(arena);

    // A tree inside the depth ceiling still loads.
    FlString shallow = deep_blob(arena, 8);
    FlJsonValue* shallow_blob = dock_json_parse(arena, shallow);
    ASSERT_TRUE(shallow_blob != nullptr);
    FlDockPersistLoad shallow_load = dock_persist_from_json(shallow_blob, arena);
    ASSERT_TRUE(shallow_load.ok);
    ASSERT_EQ((int)shallow_load.count, 1);
    ASSERT_TRUE(dock_tree_check_invariants(shallow_load.workspaces[0].tree));
    dock_tree_destroy(shallow_load.workspaces[0].tree);

    // Past it, the truncated recursion leaves a split short a child, which the invariant checker rejects. 200 is
    // under JSON_PARSE_MAX_DEPTH, so the blob reaches the tree builder rather than being stopped by the parser.
    FlString deep = deep_blob(arena, 200);
    FlJsonValue* deep_blob_json = dock_json_parse(arena, deep);
    ASSERT_TRUE(deep_blob_json != nullptr);
    ASSERT_FALSE(dock_persist_from_json(deep_blob_json, arena).ok);

    // A 40-digit panel id saturates instead of wrapping onto a live id, and the separator scan still finds the
    // next element.
    FlString wide = fl_sprintf_arena(arena,
                                     "{ \"version\": %d, \"workspaces\": [ { \"id\": \"1\", \"next_node_id\": 2, "
                                     "\"floating\": \"\", \"root\": { \"kind\": 0, \"id\": 1, \"selected\": 0, "
                                     "\"panels\": \"9999999999999999999999999999999999999999 7\" } } ] }",
                                     FL_DOCK_PERSIST_VERSION);
    FlJsonValue* wide_blob = dock_json_parse(arena, wide);
    ASSERT_TRUE(wide_blob != nullptr);
    FlDockPersistLoad wide_load = dock_persist_from_json(wide_blob, arena);
    ASSERT_TRUE(wide_load.ok);
    ASSERT_EQ((int)wide_load.count, 1);
    FlDockNode* root = wide_load.workspaces[0].tree->root;
    ASSERT_EQ((int)root->leaf.count, 2);
    ASSERT_TRUE(root->leaf.panels[0] == UINT64_MAX);
    ASSERT_TRUE(root->leaf.panels[1] == 7);
    dock_tree_destroy(wide_load.workspaces[0].tree);
}

UTEST(dock_persist, rebind_unknown_panel_to_default) {
    // unknown-panel-to-default: a panel declared this session but absent from the saved tree is docked into the
    // root region, never dropped; the saved panels stay where they were.
    DOCK_ARENA(arena);
    FlDockTree* saved = dock_tree_build(dock_split(FlDockAxis_X, 0.5f, dock_leaf(1), dock_leaf(2)));
    FlDockPersistLoad load = dock_persist_reload(arena, (FlDockTree*[]) { saved }, 1);
    ASSERT_TRUE(load.ok);

    const FlDockPanelId declared[] = { 1, 2, 99 }; // 99 is new this session
    const u64 live[] = { 1 };
    dock_persist_reconcile(&load, live, 1, declared, 3);

    ASSERT_EQ((int)load.count, 1);
    FlDockTree* r = load.workspaces[0].tree;
    ASSERT_TRUE(dock_tree_check_invariants(r));
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(r, 1) != nullptr);
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(r, 2) != nullptr);
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(r, 99) != nullptr);
    dock_tree_destroy(r);
}

UTEST(dock_persist, rebind_dangling_workspace_reparent) {
    // dangling-workspace-reparent: a saved workspace whose host is gone this session is dissolved and its panels
    // are reparented into the surviving root workspace - its loose ones along with its docked ones.
    DOCK_ARENA(arena);
    FlDockTree* t1 = dock_tree_build(dock_leaf(1)); // workspace 1: live host
    FlDockTree* t2 = dock_tree_build(dock_split(FlDockAxis_X, 0.5f, dock_leaf(2), dock_leaf(3))); // workspace 2: gone
    FlDockRequest undock = { .kind = FlDockRequest_Undock, .panel = 3 };
    ASSERT_TRUE(dock_tree_apply(t2, &undock));

    FlDockPersistLoad load = dock_persist_reload(arena, (FlDockTree*[]) { t1, t2 }, 2);
    ASSERT_TRUE(load.ok);
    ASSERT_EQ((int)load.count, 2);

    const u64 live[] = { 1 }; // only workspace 1 exists this session - workspace 2 is dangling
    dock_persist_reconcile(&load, live, 1, nullptr, 0);

    ASSERT_EQ((int)load.count, 1);
    FlDockTree* r = load.workspaces[0].tree;
    ASSERT_TRUE(load.workspaces[0].id == 1);
    ASSERT_TRUE(dock_tree_check_invariants(r));
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(r, 1) != nullptr);
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(r, 2) != nullptr);
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(r, 3) != nullptr);
    dock_tree_destroy(r);
}

UTEST(dock_persist, workspace_save_load_and_prune) {
    DOCK_TEMP_DIR(temp, "persist_save_load");
    const FlString dir = temp.path;

    FlDockWorkspace* ws = dock_workspace_create();
    ws->id = 5;
    ws->family_filter = 3;
    ws->host_width = 1024.0f;
    ws->host_height = 768.0f;
    dock_workspace_set_tree(ws, dock_tree_build(dock_split(FlDockAxis_X, 0.4f, dock_leaf(11), dock_leaf(12, 13))));
    dock_workspace_set_panel_visible(ws, 12, false);
    ASSERT_TRUE(dock_workspace_save(ws, dir));

    FlDockWorkspace* ws2 = dock_workspace_create();
    ws2->id = 5;
    const FlDockPanelId roster[] = { 11, 12 }; // 13 was saved but this session's roster does not name it
    ASSERT_TRUE(dock_workspace_load(ws2, dir, roster, sizeof_array(roster)));

    ASSERT_EQ((int)ws2->family_filter, 3);
    ASSERT_NEAR(ws2->host_width, 1024.0f, 0.01f);
    ASSERT_NEAR(ws2->host_height, 768.0f, 0.01f); // position has no field - it is never persisted

    // stale-panel-prune: the roster panels keep the saved topology; 13 is gone from the universe rather than left
    // as a titleless tab with no close button.
    FlDockNodeId leaf11 = dock_workspace_panel_leaf(ws2, 11);
    FlDockNodeId leaf12 = dock_workspace_panel_leaf(ws2, 12);
    ASSERT_TRUE(leaf11 != 0);
    ASSERT_TRUE(leaf12 != 0);
    ASSERT_TRUE(leaf11 != leaf12);
    ASSERT_EQ((int)dock_workspace_panel_leaf(ws2, 13), 0);
    ASSERT_EQ((int)dock_tree_collect_panels(ws2->tree, nullptr, 0), 2);

    // Reserve stays frame-scoped: 12 survived the roster and reports reserved when not declared this frame.
    dock_workspace_begin(ws2);
    dock_workspace_declare(ws2, 11, FL_S("Eleven"));
    dock_workspace_end(ws2);
    ASSERT_TRUE(dock_workspace_panel_reserved(ws2, 12));
    ASSERT_TRUE(dock_workspace_panel_declared(ws2, 11));
    ASSERT_FALSE(dock_workspace_panel_reserved(ws2, 13)); // pruned, so it has no slot left to reserve

    // Titles are deliberately NOT in the blob, so a restored panel the app has not declared this session is
    // untitled rather than carrying a stale label.
    ASSERT_EQ_MSG((int)dock_workspace_panel_title(ws2, 12).length, 0, "a loaded-but-undeclared panel has no title");
    ASSERT_TRUE_MSG(fl_string_equals(dock_workspace_panel_title(ws2, 11), FL_S("Eleven")),
                    "this session's declaration is where a title comes from");

    // Visibility is not in the blob either - a panel hidden when the layout was saved loads back shown.
    ASSERT_TRUE_MSG(dock_workspace_panel_visible(ws2, 12), "a restored panel is shown until this session hides it");

    // No saved layout → load returns false so the caller keeps its default.
    FlDockWorkspace* ws3 = dock_workspace_create();
    ASSERT_FALSE(dock_workspace_load(ws3, file_build_path_os(temp.arena, dir, FL_S("absent")), nullptr, 0));

    dock_workspace_destroy(ws);
    dock_workspace_destroy(ws2);
    dock_workspace_destroy(ws3);
}

UTEST(dock_persist, no_roster_prunes_nothing) {
    // A caller supplying no roster is not claiming to know the panel set, so every saved panel survives the load.
    DOCK_ARENA(arena);
    FlDockTree* saved = dock_tree_build(dock_split(FlDockAxis_X, 0.5f, dock_leaf(1), dock_leaf(2)));
    FlDockPersistLoad load = dock_persist_reload(arena, (FlDockTree*[]) { saved }, 1);
    ASSERT_TRUE(load.ok);

    const u64 live[] = { 1 };
    dock_persist_reconcile(&load, live, sizeof_array(live), nullptr, 0);

    FlDockTree* r = load.workspaces[0].tree;
    ASSERT_TRUE(dock_tree_check_invariants(r));
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(r, 1) != nullptr);
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(r, 2) != nullptr);
    dock_tree_destroy(r);
}

UTEST(dock_persist, prune_logs_the_drop) {
    // The prune takes a panel away that the user's saved layout still shows, so it is never silent.
    DOCK_TEMP_DIR(temp, "prune_log");
    __attribute__((cleanup(dock_log_file_close))) bool log_open = fl_log_file_init(temp.path);
    ASSERT_TRUE(log_open);

    DOCK_ARENA(arena);
    FlDockTree* saved = dock_tree_build(dock_split(FlDockAxis_X, 0.5f, dock_leaf(1), dock_leaf(4242)));
    FlDockPersistLoad load = dock_persist_reload(arena, (FlDockTree*[]) { saved }, 1);
    ASSERT_TRUE(load.ok);

    const u64 live[] = { 1 };
    const FlDockPanelId roster[] = { 1 };
    dock_persist_reconcile(&load, live, sizeof_array(live), roster, sizeof_array(roster));
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(load.workspaces[0].tree, 4242) == nullptr);

    u8 buffer[16384] = { 0 };
    const i64 bytes = file_read_os(buffer, fl_log_file_get_path(), (i64)sizeof(buffer) - 1);
    ASSERT_TRUE(bytes > 0);
    ASSERT_TRUE(strstr((const char*)buffer, "saved panel 4242 is not declared this session") != nullptr);

    dock_tree_destroy(load.workspaces[0].tree);
}

UTEST(dock_persist, floating_drains_on_load) {
    // floating-drains-on-load: floating is transient drag state, so reconcile hands back nothing parked. The
    // dangling workspace's loose panel and a roster panel absent from every saved tree both end docked.
    DOCK_ARENA(arena);
    FlDockTree* t1 = dock_tree_build(dock_leaf(1));
    FlDockTree* t2 = dock_tree_build(dock_split(FlDockAxis_X, 0.5f, dock_leaf(2), dock_leaf(3)));
    FlDockRequest undock = { .kind = FlDockRequest_Undock, .panel = 3 };
    ASSERT_TRUE(dock_tree_apply(t2, &undock));

    FlDockPersistLoad load = dock_persist_reload(arena, (FlDockTree*[]) { t1, t2 }, 2);
    ASSERT_TRUE(load.ok);

    const u64 live[] = { 1 };
    const FlDockPanelId roster[] = { 1, 2, 3, 99 }; // 99 is new this session
    dock_persist_reconcile(&load, live, sizeof_array(live), roster, sizeof_array(roster));

    ASSERT_EQ((int)load.count, 1);
    FlDockTree* r = load.workspaces[0].tree;
    ASSERT_TRUE(dock_tree_check_invariants(r));
    ASSERT_EQ((int)r->floating_count, 0);
    for_count(i, sizeof_array(roster)) {
        ASSERT_TRUE(dock_tree_find_leaf_with_panel(r, roster[i]) != nullptr);
    }
    dock_tree_destroy(r);
}

UTEST(dock_persist, rehydrated_floating_drains) {
    // A blob with a non-empty floating string loads those panels as floating; reconcile then docks them in order.
    DOCK_ARENA(arena);
    FlString text = fl_sprintf_arena(arena,
                                     "{ \"version\": %d, \"workspaces\": [ { \"id\": \"1\", \"next_node_id\": 2, "
                                     "\"floating\": \"42 43\", \"root\": { \"kind\": 0, \"id\": 1, \"selected\": 0, "
                                     "\"panels\": \"7\" } } ] }",
                                     FL_DOCK_PERSIST_VERSION);
    FlJsonValue* blob = dock_json_parse(arena, text);
    ASSERT_TRUE(blob != nullptr);

    FlDockPersistLoad load = dock_persist_from_json(blob, arena);
    ASSERT_TRUE(load.ok);
    ASSERT_EQ((int)load.count, 1);
    ASSERT_EQ((int)load.workspaces[0].tree->floating_count, 2);

    const u64 live[] = { 1 };
    dock_persist_reconcile(&load, live, sizeof_array(live), nullptr, 0);

    FlDockTree* r = load.workspaces[0].tree;
    ASSERT_TRUE(dock_tree_check_invariants(r));
    ASSERT_EQ((int)r->floating_count, 0);
    ASSERT_EQ((int)r->root->kind, (int)FlDockNodeKind_Leaf);
    ASSERT_EQ((int)r->root->leaf.count, 3);
    ASSERT_TRUE(r->root->leaf.panels[1] == 42);
    ASSERT_TRUE(r->root->leaf.panels[2] == 43);
    dock_tree_destroy(r);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The slot a split gap opens draws neither the dragged panel's tab strip nor its title; the ghost draws the panel.
// A drop onto a tab strip previews the insertion by showing the dragged tab in that strip.

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

// The ghost has to read as held, not as a solid block: nothing it draws may be opaque, or the layout it is passing
// over is hidden behind it. It carries the panel's title so the user can tell which panel is in hand.
UTEST(dock_workspace, drag_ghost_is_translucent_and_names_its_panel) {
    DOCK_APP(app, "dockzorder", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_gaptab_frame, 2);

    ASSERT_TRUE_MSG(dock_press_tab(app, &st, &st, dock_gaptab_frame, st.ws->tree->root->id, 1u),
                    "the dragged panel's tab was not laid out");
    dock_move_and_tick(app, &st, dock_gaptab_frame, 100, 300, 30);
    ASSERT_TRUE_MSG(dock_drag_active(&st.ws->drag), "the pull did not arm a drag");

    bool fill_drew = false;
    int opaque_in_band = 0;
    const int n = ui_get_render_command_count();
    for (int i = 0; i < n; i++) {
        UiRenderCommand cmd;
        if (!ui_get_render_command(i, &cmd) || (int)cmd.z_index != UI_Z_DOCK_DRAG_CHROME) {
            continue;
        }
        if (cmd.name.data != nullptr && fl_string_equals(cmd.name, FL_S("fl_dock_drag_overlay_fill"))) {
            fill_drew = true;
            ASSERT_LT_MSG(cmd.bg_color.a, 255.0f, "the ghost's fill is translucent, not a solid block");
            ASSERT_GT_MSG(cmd.bg_color.a, 0.0f, "but not invisible either");
        }
        if (cmd.type == UiRenderCmd_Rectangle && cmd.bg_color.a >= 255.0f) {
            opaque_in_band++;
        }
    }
    ASSERT_TRUE_MSG(fill_drew, "the ghost drew");
    ASSERT_EQ_MSG(opaque_in_band, 0, "nothing in the drag band is opaque — the layout stays visible through it");
    ASSERT_TRUE_MSG(frame_has_text_at_z(FL_S("Beta"), UI_Z_DOCK_DRAG_CHROME), "the ghost names the panel in hand");
    ASSERT_FALSE_MSG(frame_has_text(FL_S("beta-body")), "and does NOT carry the panel's body");

    dock_release(app, &st, dock_gaptab_frame);
}

UTEST(dock_workspace, split_gap_slot_never_draws_the_dragged_panels_tab) {
    DOCK_APP(app, "dockgaptab", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_gaptab_frame, 2);
    ASSERT_TRUE_MSG(frame_has_text(FL_S("Beta")), "the panel's tab renders normally before the drag");

    ASSERT_TRUE_MSG(dock_press_tab(app, &st, &st, dock_gaptab_frame, st.ws->tree->root->id, 1u),
                    "101's tab was not laid out");

    // Out of the strip and deep into the tile's left quadrant: a split gap, which opens a slot of its own.
    dock_move_and_tick(app, &st, dock_gaptab_frame, 100, 300, 6);
    const FlDockDrag* drag = &st.ws->drag;
    ASSERT_TRUE_MSG(dock_drag_active(drag), "the pull did not arm a drag");
    ASSERT_EQ_MSG((int)drag->zone, (int)FlDockDropZone_Left, "100px into a 600px-wide tile is its left quadrant");
    ASSERT_TRUE_MSG(drag->gap_is_new, "a split gap opens a slot of its own");

    // Every frame the gap sits there, not just the one the overlay is late for. The title still reaches the screen
    // in the drag band - that is the ghost riding the cursor - so the assertion is scoped to the docked band.
    for (int i = 0; i < 4; ++i) {
        fl_app_tick(app, &st, dock_gaptab_frame);
        ASSERT_FALSE_MSG(frame_has_text_at_z(FL_S("Beta"), UI_Z_DOCK_CONTENT),
                         "the gap slot must not draw the dragged panel's tab");
        ASSERT_TRUE_MSG(frame_has_text_at_z(FL_S("Alpha"), UI_Z_DOCK_CONTENT),
                        "while the leaf it was pulled out of keeps its own");
    }

    dock_release(app, &st, dock_gaptab_frame);
    fl_app_tick(app, &st, dock_gaptab_frame);
    ASSERT_TRUE_MSG(frame_has_text(FL_S("Beta")), "and the tab is back the moment the drop commits");
}

// The centre region is the tab-into target that survives its own preview: aiming for the strip while a top gap
// stands puts the cursor over a strip the gap has pushed out from under it, but the centre resolves with the gap
// closed, so the geometry the hit-test reads and the geometry on screen are the same rect and the target holds.
UTEST(dock_workspace, center_region_tabs_in_over_an_open_gap) {
    DOCK_APP(app, "dockcenter", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_gaptab_frame, 2);

    ASSERT_TRUE_MSG(dock_press_tab(app, &st, &st, dock_gaptab_frame, st.ws->tree->root->id, 1u),
                    "101's tab was not laid out");

    // The top quadrant: a gap opens there and pushes the tile (strip and all) down the window.
    dock_move_and_tick(app, &st, dock_gaptab_frame, 300, 60, 6);
    const FlDockDrag* drag = &st.ws->drag;
    ASSERT_EQ_MSG((int)drag->zone, (int)FlDockDropZone_Top, "60px down a 400px window is its top quadrant");
    ASSERT_TRUE_MSG(drag->gap_is_new, "a split gap opens a slot of its own");

    dock_move_and_tick(app, &st, dock_gaptab_frame, 300, 200, 1);
    ASSERT_EQ_MSG((int)drag->zone, (int)FlDockDropZone_Center, "the middle of the tile tabs in");
    ASSERT_EQ((int)drag->request.kind, (int)FlDockRequest_Tab);
    ASSERT_FALSE_MSG(drag->gap_is_new, "the split gap closed, so the tile is back at its full rect");

    // Jitter further than the hysteresis every frame: the target must not flip, which is what the strip could not do
    // once its own gap had moved it.
    for (int i = 0; i < 30; ++i) {
        fl_app_inject_mouse_move(app, (i & 1) ? 290 : 310, (i & 2) ? 190 : 210);
        fl_app_tick(app, &st, dock_gaptab_frame);
        ASSERT_EQ_MSG((int)drag->zone, (int)FlDockDropZone_Center, "the centre holds under a moving cursor");
    }

    dock_release(app, &st, dock_gaptab_frame);

    FlDockNode* leaf = dock_tree_find_leaf_with_panel(st.ws->tree, 101);
    ASSERT_TRUE(leaf != nullptr);
    ASSERT_EQ_MSG((int)leaf->leaf.count, 2, "the drop tabbed the panel back in rather than splitting");
    ASSERT_EQ_MSG((int)leaf->leaf.panels[1], 101, "a centre drop lands at the end of the strip");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A panel body that overflows its tile is clipped to it, and so is its input. The nested column tests the whole clip
// chain rather than the nearest clip ancestor: a scroll area inside a panel has a clip box taller than its tile.

#define DOCK_HIT_ID(lit) ((FlLayoutId) { .name = FL_S(lit), .id = 100u })
#define DOCK_HIT_COL_W 120.0f
#define DOCK_HIT_COL_H 600.0f

static void dock_hit_frame(void* user_data) {
    // Panel 100 on top, panel 101 below it: the seam the overflow must not take input across.
    FlDockWorkspace* ws
        = dock_fixture_emit((DockFixture*)user_data, dock_split(FlDockAxis_Y, 0.5f, dock_leaf(100), dock_leaf(101)));
    dock_panel(100, FL_S("hit")) {
        // Clipped by the panel tile alone.
        ui_layout_area_id(
            DOCK_HIT_ID("hit_direct"), .sizing = { sizing_fixed(DOCK_HIT_COL_W), sizing_fixed(DOCK_HIT_COL_H) },
            .color = ((FlColor) { 0, 0, 255, 255 }), .placed = { .enabled = true, .offset = { 0.0f, 0.0f } }) {
            ui_item_behavior(DOCK_HIT_ID("hit_direct"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
        }
        // Clipped by the panel tile AND by this scroll area, whose own box runs past the tile.
        ui_layout_area_id(DOCK_HIT_ID("hit_scroll"),
                          .sizing = { sizing_fixed(DOCK_HIT_COL_W), sizing_fixed(DOCK_HIT_COL_H) },
                          .clip = { .horizontal = true, .vertical = true, .manual_scroll = true },
                          .placed = { .enabled = true, .offset = { 200.0f, 0.0f } }) {
            ui_layout_area_id(DOCK_HIT_ID("hit_nested"),
                              .sizing = { sizing_fixed(DOCK_HIT_COL_W), sizing_fixed(DOCK_HIT_COL_H) },
                              .color = ((FlColor) { 0, 255, 0, 255 })) {
                ui_item_behavior(DOCK_HIT_ID("hit_nested"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
        }
    }
    dock_panel(101, FL_S("below")) {}
    dock_workspace_end(ws);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The bottom edge of panel 100's tile: the seam the overflowing columns above must not take input across.

static f32 dock_hit_tile_bottom(FlDockWorkspace* ws) {
    FlDockNode* leaf = dock_tree_find_leaf_with_panel(ws->tree, 100);
    if (leaf == nullptr) {
        return -1.0f;
    }
    Clay_ElementData tile = dock_element(dock_layout_content_element_id(leaf->id));
    return tile.found ? tile.boundingBox.y + tile.boundingBox.height : -1.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Settles the pointer on a position and answers the hot id it resolved to. Hot lands at the end of a frame, so one
// tick is enough, but a second keeps the read clear of the frame that installed the position.

static u32 dock_hit_hot_at(FlApp* app, DockFixture* st, void (*frame)(void*), f32 x, f32 y) {
    fl_app_inject_mouse_move(app, (i32)x, (i32)y);
    dock_settle_hover(app, st, frame);
    return st->ctx->hot_id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The hot id after the pointer settles on the centre of an element.
static u32 dock_hit_hot_at_centre(FlApp* app, DockFixture* st, void (*frame)(void*), Clay_ElementData element) {
    return dock_hit_hot_at(app, st, frame, element.boundingBox.x + element.boundingBox.width * 0.5f,
                           element.boundingBox.y + element.boundingBox.height * 0.5f);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Checks the element named id is hot inside panel 100's tile and not hot past the tile's bottom edge, though its own
// box reaches there.
static void dock_hit_expect_clipped_to_tile(int* utest_result, FlApp* app, DockFixture* st, FlLayoutId id) {
    const f32 tile_bottom = dock_hit_tile_bottom(st->ws);
    ASSERT_GT_MSG(tile_bottom, 0.0f, "panel 100's tile must lay out");

    const u32 clay_id = ui_layout_id_to_clay_id(id);
    Clay_ElementData element = dock_element(clay_id);
    ASSERT_TRUE_MSG(element.found, "the overflowing element must lay out");
    const f32 probe_y = tile_bottom + 20.0f;
    ASSERT_GT_MSG(element.boundingBox.y + element.boundingBox.height, probe_y,
                  "the element must reach past the tile for this to test anything");

    const f32 x = element.boundingBox.x + element.boundingBox.width * 0.5f;
    ASSERT_EQ_MSG(clay_id, dock_hit_hot_at(app, st, dock_hit_frame, x, (element.boundingBox.y + tile_bottom) * 0.5f),
                  "control: inside the tile the element is hot");
    ASSERT_NE_MSG(clay_id, dock_hit_hot_at(app, st, dock_hit_frame, x, probe_y),
                  "input below the tile must not reach the content that overflowed it");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(dock_workspace, panel_overflow_takes_no_input_past_the_tile) {
    DOCK_APP(app, "dockhit", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_hit_frame, 2); // frame 1 installs the tree, frame 2 lays it out settled
    dock_hit_expect_clipped_to_tile(utest_result, app, &st, DOCK_HIT_ID("hit_direct"));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(dock_workspace, a_nested_scroll_area_does_not_widen_the_tile_it_sits_in) {
    DOCK_APP(app, "dockhit2", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_hit_frame, 2);

    const f32 tile_bottom = dock_hit_tile_bottom(st.ws);
    ASSERT_GT_MSG(tile_bottom, 0.0f, "panel 100's tile must lay out");

    Clay_ElementData scroll = dock_layout_element(DOCK_HIT_ID("hit_scroll"));
    Clay_ElementData nested = dock_layout_element(DOCK_HIT_ID("hit_nested"));
    ASSERT_TRUE_MSG(scroll.found && nested.found, "the scroll area and its child must lay out");

    const f32 probe_y = tile_bottom + 20.0f;
    ASSERT_GT_MSG(scroll.boundingBox.y + scroll.boundingBox.height, probe_y,
                  "the probe must sit inside the scroll area's own clip box, past the tile — the nested case");
    ASSERT_GT_MSG(nested.boundingBox.y + nested.boundingBox.height, probe_y, "and inside the child's own rect");

    dock_hit_expect_clipped_to_tile(utest_result, app, &st, DOCK_HIT_ID("hit_nested"));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The other side of the same walk: an overlay opened from a panel body is a layout root of its own, under no
// scissor at all, so clipping the hit test must leave it hot everywhere it paints - including past the tile the
// panel that opened it lives in. Both attach modes.

#define DOCK_OVL_ID(lit) ((FlLayoutId) { .name = FL_S(lit), .id = 100u })
#define DOCK_OVL_W 100.0f
#define DOCK_OVL_H 40.0f

static void dock_overlay_hit_frame(void* user_data) {
    // Panel 100 on top, panel 101 below it: the seam the overlays have to stay hot across.
    FlDockWorkspace* ws
        = dock_fixture_emit((DockFixture*)user_data, dock_split(FlDockAxis_Y, 0.5f, dock_leaf(100), dock_leaf(101)));
    dock_panel(100, FL_S("ovl")) {
        // Fills the tile, so its bottom edge IS the tile's bottom edge.
        ui_layout_area_id(DOCK_OVL_ID("ovl_anchor"), .sizing = { sizing_grow(), sizing_grow() },
                          .color = ((FlColor) { 0, 0, 255, 255 })) {}

        fl_floating_area_id(DOCK_OVL_ID("ovl_root"), .attach_to = FlFloatingAttachTo_Root, .offset = { 400.0f, 300.0f },
                            .sizing = { sizing_fixed(DOCK_OVL_W), sizing_fixed(DOCK_OVL_H) }) {
            ui_layout_area_id(DOCK_OVL_ID("ovl_root_fill"), .sizing = { sizing_grow(), sizing_grow() },
                              .color = ((FlColor) { 255, 0, 0, 255 })) {
                ui_item_behavior(DOCK_OVL_ID("ovl_root_fill"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
        }

        // Hangs off the anchor's bottom edge, so it lands entirely below the tile.
        fl_floating_area_id(DOCK_OVL_ID("ovl_elem"), .attach_to = FlFloatingAttachTo_Element,
                            .parent_id = ui_layout_id_to_clay_id(DOCK_OVL_ID("ovl_anchor")),
                            .element_attach = FlFloatingAttachPoint_LeftTop,
                            .parent_attach = FlFloatingAttachPoint_LeftBottom,
                            .sizing = { sizing_fixed(DOCK_OVL_W), sizing_fixed(DOCK_OVL_H) }) {
            ui_layout_area_id(DOCK_OVL_ID("ovl_elem_fill"), .sizing = { sizing_grow(), sizing_grow() },
                              .color = ((FlColor) { 0, 255, 0, 255 })) {
                ui_item_behavior(DOCK_OVL_ID("ovl_elem_fill"), FlItemFlags_clickable, (FlWidgetConfig) { 0 });
            }
        }
    }
    dock_panel(101, FL_S("below")) {}
    dock_workspace_end(ws);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(dock_workspace, an_overlay_stays_hot_past_the_tile_it_was_opened_from) {
    DOCK_APP(app, "dockovl", 600, 400);
    DockFixture st = { 0 };
    DOCK_RUN_FRAMES(app, st, st, dock_overlay_hit_frame, 2); // frame 1 installs the tree, frame 2 lays it out settled

    const f32 tile_bottom = dock_hit_tile_bottom(st.ws);
    ASSERT_GT_MSG(tile_bottom, 0.0f, "panel 100's tile must lay out");

    const u32 root_id = ui_layout_id_to_clay_id(DOCK_OVL_ID("ovl_root_fill"));
    Clay_ElementData root_fill = dock_element(root_id);
    ASSERT_TRUE_MSG(root_fill.found, "the root-attached overlay must lay out");
    ASSERT_GT_MSG(root_fill.boundingBox.y, tile_bottom,
                  "the root-attached overlay must sit past the tile for this to test anything");
    ASSERT_EQ_MSG(root_id, dock_hit_hot_at_centre(app, &st, dock_overlay_hit_frame, root_fill),
                  "a root-attached overlay is hot outside the tile of the panel that opened it");

    const u32 elem_id = ui_layout_id_to_clay_id(DOCK_OVL_ID("ovl_elem_fill"));
    Clay_ElementData elem_fill = dock_element(elem_id);
    ASSERT_TRUE_MSG(elem_fill.found, "the element-attached overlay must lay out");
    ASSERT_GE_MSG(elem_fill.boundingBox.y, tile_bottom - 0.5f,
                  "the element-attached overlay must hang off the tile's bottom edge");
    ASSERT_EQ_MSG(elem_id, dock_hit_hot_at_centre(app, &st, dock_overlay_hit_frame, elem_fill),
                  "an overlay anchored to a widget at the tile's bottom edge is hot below the tile");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// fl_dock_dock_outer_share docks the new outer region at the named share of the root; fl_dock_dock_outer keeps the
// even split.

static void dock_outer_share_frame(void* user_data) {
    DockFixture* st = (DockFixture*)user_data;
    st->ctx = ui_get_ctx();
    st->ws = dock_workspace_current();
    if (!st->installed && st->ws != nullptr) {
        fl_dock_add_panel(1);
        fl_dock_dock_outer_share(2, FlDockSide_Left, 0.25f);
        fl_dock_dock_outer(3, FlDockSide_Right);
        st->installed = true;
    }
}

UTEST(dock_api, dock_outer_share_sets_the_new_regions_share) {
    DOCK_APP(app, "dockshare", 800, 600);
    DockFixture st = { 0 };
    fl_app_tick(app, &st, dock_outer_share_frame);
    ASSERT_TRUE_MSG(st.installed, "frame did not run / no workspace");

    // Right of everything: the even split, the shared Left region and panel 1 inside its first child.
    FlDockNode* root = st.ws->tree->root;
    ASSERT_EQ((int)root->kind, (int)FlDockNodeKind_Split);
    ASSERT_NEAR(root->split.ratio, 1.0f - FL_DOCK_SPLIT_RATIO_DEFAULT, 1e-5f);
    FlDockNode* left = root->split.child[0];
    ASSERT_EQ((int)left->kind, (int)FlDockNodeKind_Split);
    ASSERT_NEAR_MSG(left->split.ratio, 0.25f, 1e-5f, "the Left region takes a quarter");
    ASSERT_EQ((int)left->split.child[0]->kind, (int)FlDockNodeKind_Leaf);
    ASSERT_EQ(left->split.child[0]->leaf.panels[0], (FlDockPanelId)2);
}
