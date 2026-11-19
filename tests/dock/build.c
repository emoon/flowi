// Dock build: the declarative layout literal and workspace installation.

#include "dock/common.h"

// Dock build: the declarative first-run layout literal (dock_split/dock_leaf, ui/dock.h), lowered onto the tree
// by dock_tree_build and checked against the invariants.

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

    // The dock_leaf macro defaults selected to 0, so a hand-built spec covers the other case: a two-tab leaf with
    // the second tab selected.
    const FlDockPanelId pair[] = { 700, 800 };
    FlDockSpec sel = { .is_split = false, .panels = pair, .panel_count = 2, .selected = 1 };
    FlDockTree* t2 = dock_tree_build(&sel);
    ASSERT_TRUE(t2 != nullptr);
    ASSERT_EQ((int)t2->root->leaf.selected, 1);
    ASSERT_EQ((int)t2->root->leaf.panels[1], 800);
    dock_tree_destroy(t2);

    // A null spec gives the floor: a single empty root leaf that passes the invariants.
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
    // An app installs an authored default through dock_workspace_set_tree: build and validate, then hand the tree
    // to the workspace, which takes ownership and destroys its previous tree.
    FlDockWorkspace* ws = dock_workspace_create();
    ASSERT_TRUE(ws->tree != nullptr);
    ASSERT_TRUE(dock_tree_check_invariants(ws->tree)); // the floor the workspace was created with
    ASSERT_EQ((int)dock_tree_collect_panels(ws->tree, nullptr, 0), 0);

    FlDockTree* authored = dock_tree_build(dock_split(FlDockAxis_X, 0.5f, dock_leaf(10), dock_leaf(20)));
    ASSERT_TRUE(authored != nullptr);

    dock_workspace_set_tree(ws, authored);
    ASSERT_EQ((int)dock_tree_collect_panels(ws->tree, nullptr, 0), 2);
    ASSERT_EQ((int)dock_workspace_panel_leaf(ws, 10), (int)dock_tree_find_leaf_with_panel(ws->tree, 10)->id);
    ASSERT_EQ((int)dock_workspace_panel_leaf(ws, 20), (int)dock_tree_find_leaf_with_panel(ws->tree, 20)->id);

    // Installing the tree the workspace already has does not destroy it.
    dock_workspace_set_tree(ws, authored);
    ASSERT_TRUE(dock_tree_check_invariants(ws->tree));
    ASSERT_EQ((int)dock_tree_collect_panels(ws->tree, nullptr, 0), 2);

    // A failed dock_tree_build returns null, and passing null leaves the existing tree in place.
    dock_workspace_set_tree(ws, nullptr);
    ASSERT_TRUE(dock_tree_check_invariants(ws->tree));
    ASSERT_EQ((int)dock_tree_collect_panels(ws->tree, nullptr, 0), 2);

    // Replacing with a second authored tree installs it and destroys the first.
    FlDockTree* second = dock_tree_build(dock_leaf(99));
    ASSERT_TRUE(second != nullptr);
    dock_workspace_set_tree(ws, second);
    ASSERT_EQ((int)dock_tree_collect_panels(ws->tree, nullptr, 0), 1);
    ASSERT_EQ((int)dock_workspace_panel_leaf(ws, 99), (int)dock_tree_find_leaf_with_panel(ws->tree, 99)->id);
    ASSERT_EQ((int)dock_workspace_panel_leaf(ws, 99), (int)second->root->id);

    dock_workspace_destroy(ws); // destroys second. authored was destroyed when it was replaced
}

// fl_dock_dock_outer_share docks the new outer region at the named share of the root. fl_dock_dock_outer keeps
// the even split.

static void dock_outer_share_frame(void* user_data) {
    DockFixture* st = (DockFixture*)user_data;
    st->ctx = g_ui_ctx;
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
