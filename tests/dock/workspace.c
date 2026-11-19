// Dock workspace: panel declaration, titles, families, visibility and the per-context registry.

#include "dock/common.h"

// Dock workspace: the immediate panel-declaration layer over the retained tree (ui/dock/dock_workspace.{h,c}).
// These cases need no Clay frame.

UTEST(dock_workspace, binding_and_reservation) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlDockWorkspace* ws = dock_workspace_create();
    FlDockTree* tree = ws->tree;
    FlDockNodeId root = tree->root->id;

    // Built by hand: root leaf {10, 20} with 20 selected (a newly added tab becomes selected), split Right into
    // {30}.
    ASSERT_TRUE(DOCK_TAB_PANELS(tree, root, 10, 20));
    FlDockRequest split = { .kind = FlDockRequest_Split, .panel = 30, .target_node = root, .side = FlDockSide_Right };
    ASSERT_TRUE(dock_tree_apply(tree, &split));
    ASSERT_TRUE(dock_tree_check_invariants(tree));

    FlDockNode* leaf_ab = dock_tree_find_leaf_with_panel(tree, 10);
    FlDockNode* leaf_c = dock_tree_find_leaf_with_panel(tree, 30);
    ASSERT_TRUE_MSG(leaf_ab != nullptr && leaf_c != nullptr, "both leaves must be found");
    ASSERT_EQ((int)leaf_ab->leaf.panels[leaf_ab->leaf.selected], 20);

    // Each panel resolves by stable id to the leaf node holding it. An absent id resolves to 0, no node.
    const PanelLeaf leaves[] = { { 10, leaf_ab->id }, { 20, leaf_ab->id }, { 30, leaf_c->id }, { 999, 0 } };
    dock_expect_panel_leaves(utest_result, ws, leaves, sizeof_array(leaves));

    // Frame 1: declare 20 and 30 but not 10, which stays a reserved background tab.
    dock_workspace_begin(ws);
    bool vis20 = dock_workspace_declare(ws, 20, FL_S("Twenty"));
    bool vis30 = dock_workspace_declare(ws, 30, FL_S("Thirty"));
    dock_workspace_end(ws);

    // Only a leaf's selected tab emits. 10 is bound but not selected, and in the tree but undeclared, so its slot
    // is reserved and does not collapse.
    ASSERT_TRUE(vis20);
    ASSERT_TRUE(vis30);
    const PanelWant frame1[] = {
        { .panel = 20, .emit = DockTri_Yes, .declared = DockTri_Yes, .reserved = DockTri_No },
        { .panel = 30, .emit = DockTri_Yes, .declared = DockTri_Yes },
        { .panel = 10, .emit = DockTri_No, .declared = DockTri_No, .reserved = DockTri_Yes },
    };
    dock_expect_panel_states(utest_result, ws, frame1, sizeof_array(frame1));
    dock_expect_panel_leaves(utest_result, ws, leaves, 1);
    ASSERT_EQ((int)leaf_ab->leaf.count, 2);
    ASSERT_TRUE(dock_tree_check_invariants(tree));

    // Frame 2 is a new generation. With nothing declared, every panel declared before falls back to reserved. A
    // panel that is absent from the tree is neither bound nor reserved.
    dock_workspace_begin(ws);
    dock_workspace_end(ws);
    const PanelWant frame2[] = {
        { .panel = 20, .declared = DockTri_No, .reserved = DockTri_Yes },
        { .panel = 30, .reserved = DockTri_Yes },
        { .panel = 999, .reserved = DockTri_No },
    };
    dock_expect_panel_states(utest_result, ws, frame2, sizeof_array(frame2));

    dock_workspace_destroy(ws);
}

// A panel's title is copied into its record. The declared string is borrowed for the call only, and a reserved
// panel keeps the title from its last declaration.
UTEST(dock_workspace, panel_title_is_copied_and_retained) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlDockWorkspace* ws = dock_workspace_create();
    FlDockTree* tree = ws->tree;
    FlDockRequest tab = { .kind = FlDockRequest_Tab, .panel = 40, .target_node = tree->root->id };
    ASSERT_TRUE(dock_tree_apply(tree, &tab));

    // A caller-owned buffer that is not static, as a Rust &str or a formatted app string arrives.
    char scratch[] = "Inspector";
    dock_workspace_begin(ws);
    dock_workspace_declare(ws, 40, string_from_cstr_len(scratch, 9));
    dock_workspace_end(ws);
    ASSERT_TRUE_MSG(fl_string_equals(dock_workspace_panel_title(ws, 40), FL_S("Inspector")), "the title reads back");

    // Overwrite the caller's bytes. A retained title that aliased them would now read "XXXXXXXXX".
    memset(scratch, 'X', 9);
    ASSERT_TRUE_MSG(fl_string_equals(dock_workspace_panel_title(ws, 40), FL_S("Inspector")),
                    "the title must be a copy — the caller's bytes are borrowed for the call only");

    dock_workspace_begin(ws);
    dock_workspace_end(ws);
    ASSERT_TRUE_MSG(dock_workspace_panel_reserved(ws, 40), "40 is in the tree but undeclared → reserved");
    ASSERT_TRUE_MSG(fl_string_equals(dock_workspace_panel_title(ws, 40), FL_S("Inspector")),
                    "a reserved panel keeps the title from its last declaration");

    // Declaring again with a different string replaces the title. The same string leaves it alone.
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

// A loose (undocked) panel does not emit. The drag overlay draws its title and the body is drawn nowhere, so the
// emit decision and dock_workspace_declare both return false for it.
UTEST(dock_workspace, floating_panel_does_not_emit) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlDockWorkspace* ws = dock_workspace_create();
    FlDockTree* tree = ws->tree;

    ASSERT_TRUE(dock_tab_into(tree, tree->root->id, 10));
    const PanelWant docked[] = { { .panel = 10, .emit = DockTri_Yes } };
    dock_expect_panel_states(utest_result, ws, docked, sizeof_array(docked));

    FlDockRequest undock = { .kind = FlDockRequest_Undock, .panel = 10 };
    ASSERT_TRUE(dock_tree_apply(tree, &undock));
    ASSERT_TRUE(dock_tree_panel_is_floating(tree, 10));
    const PanelLeaf no_leaf[] = { { 10, 0 } };
    dock_expect_panel_leaves(utest_result, ws, no_leaf, sizeof_array(no_leaf));

    const PanelWant loose[] = { { .panel = 10, .emit = DockTri_No }, { .panel = 999, .emit = DockTri_No } };
    dock_expect_panel_states(utest_result, ws, loose, 1);
    dock_workspace_begin(ws);
    ASSERT_FALSE(dock_workspace_declare(ws, 10, FL_S("Ten")));
    dock_workspace_end(ws);

    dock_expect_panel_states(utest_result, ws, loose + 1, 1);

    ASSERT_TRUE(dock_tree_check_invariants(tree));
    dock_workspace_destroy(ws);
}

UTEST(dock_workspace, panel_family_tags) {
    FlDockWorkspace* ws = dock_workspace_create();

    // Every panel is general until tagged, including ids the workspace has never seen.
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

// Visibility is a bit the app sets alongside the tree. A hidden panel keeps everything the tree holds for it and
// only stops rendering.
UTEST(dock_workspace, hidden_panel_keeps_its_slot_and_tab_index) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
    FlDockWorkspace* ws = dock_workspace_create();
    FlDockTree* tree = ws->tree;

    // Root leaf {10, 20} with 20 selected, since a newly added tab becomes selected.
    ASSERT_TRUE(DOCK_TAB_PANELS(tree, tree->root->id, 10, 20));
    FlDockNode* leaf = tree->root;

    // By default everything is shown, including ids the workspace has never seen.
    const PanelWant shown[] = { { .panel = 10, .visible = DockTri_Yes }, { .panel = 999, .visible = DockTri_Yes } };
    dock_expect_panel_states(utest_result, ws, shown, sizeof_array(shown));

    dock_workspace_set_panel_visible(ws, 20, false);
    const PanelWant hidden[] = { { .panel = 20, .visible = DockTri_No } };
    dock_expect_panel_states(utest_result, ws, hidden, sizeof_array(hidden));

    // The slot, the tab index and the selection belong to the tree, and hiding does not touch the tree.
    dock_expect_leaf(utest_result, leaf, &(DockLeafWant) { .count = 2, .selected = 1, .panels = { 10, 20 } });
    ASSERT_TRUE(dock_tree_check_invariants(tree));

    // The app declares hidden panels like any other, so hiding never makes one reserved. A hidden selected tab
    // emits nothing and its leaf falls back to the first shown panel, without rewriting the selection.
    dock_workspace_begin(ws);
    ASSERT_FALSE_MSG(dock_workspace_declare(ws, 20, FL_S("Twenty")), "a hidden panel must not emit");
    ASSERT_TRUE_MSG(dock_workspace_declare(ws, 10, FL_S("Ten")), "the leaf shows the first panel that is not hidden");
    dock_workspace_end(ws);
    const PanelWant declared[] = { { .panel = 20, .declared = DockTri_Yes, .reserved = DockTri_No } };
    dock_expect_panel_states(utest_result, ws, declared, sizeof_array(declared));
    ASSERT_EQ((int)leaf->leaf.selected, 1);

    // A hidden floating panel does not emit either. Floating exempts a panel from selection but not from hiding.
    dock_workspace_set_panel_visible(ws, 30, false);
    ASSERT_TRUE(dock_tab_into(tree, leaf->id, 30));
    FlDockRequest undock30 = { .kind = FlDockRequest_Undock, .panel = 30 };
    ASSERT_TRUE(dock_tree_apply(tree, &undock30));
    ASSERT_TRUE(dock_tree_panel_is_floating(tree, 30));
    const PanelWant floating[] = { { .panel = 30, .emit = DockTri_No } };
    dock_expect_panel_states(utest_result, ws, floating, sizeof_array(floating));

    // Showing the panel again restores the emit decision.
    dock_workspace_set_panel_visible(ws, 20, true);
    const PanelWant reshown[] = { { .panel = 20, .emit = DockTri_Yes }, { .panel = 10, .emit = DockTri_No } };
    dock_expect_panel_states(utest_result, ws, reshown, sizeof_array(reshown));

    dock_workspace_destroy(ws);
}

// Asserts a promoted panel sits alone in the root leaf of ws's tree and that ws resolves it there.
static void dock_expect_promoted(int* utest_result, FlDockWorkspace* ws, FlDockPanelId panel) {
    const FlDockNode* root = ws->tree->root;
    dock_expect_leaf(utest_result, root, &(DockLeafWant) { .count = 1, .panels = { panel } });
    ASSERT_EQ((int)root->leaf.panels[root->leaf.selected], (int)panel);
    const PanelLeaf at_root[] = { { panel, root->id } };
    dock_expect_panel_leaves(utest_result, ws, at_root, sizeof_array(at_root));
}

// Headless promote and demote. Detaching a panel to its own window moves the subtree between two in-process
// workspaces. Nothing is copied.
UTEST(dock_workspace, promote_demote_between_workspaces) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");

    // dst is a new empty floor, the promotion target that becomes a second window's root.
    FlDockWorkspace* src = dock_workspace_create();
    FlDockWorkspace* dst = dock_workspace_create();
    dock_workspace_set_tree(
        src, dock_tree_build(dock_split(FlDockAxis_X, FL_DOCK_SPLIT_RATIO_DEFAULT, dock_leaf(10, 20), dock_leaf(30))));
    FlDockTree* src_tree = src->tree;
    FlDockTree* dst_tree = dst->tree;
    ASSERT_TRUE(dock_tree_check_invariants(src_tree));

    // Panel 30 gets a non-general family, to check that the tag moves with the panel.
    dock_workspace_set_panel_family(src, 30, 7);
    dock_expect_universe(utest_result, src_tree, 3);
    dock_expect_universe(utest_result, dst_tree, 0);

    // Promote panel 30 from src to dst.
    ASSERT_TRUE(dock_workspace_promote(src, dst, 30));
    ASSERT_TRUE(dock_tree_check_invariants(dst_tree));
    dock_expect_promoted(utest_result, dst, 30);

    ASSERT_TRUE(dock_tree_check_invariants(src_tree));
    ASSERT_TRUE(dock_tree_find_leaf_with_panel(src_tree, 30) == nullptr);
    ASSERT_FALSE(dock_tree_panel_is_floating(src_tree, 30));
    const bool kept_10 = dock_tree_find_leaf_with_panel(src_tree, 10) != nullptr;
    const bool kept_20 = dock_tree_find_leaf_with_panel(src_tree, 20) != nullptr;
    ASSERT_TRUE(kept_10);
    ASSERT_TRUE(kept_20);

    // 30 left src's universe and entered dst's. It is never in both and never lost. The family tag moved with the
    // panel.
    dock_expect_universe(utest_result, src_tree, 2);
    dock_expect_universe(utest_result, dst_tree, 1);
    ASSERT_EQ((int)dock_workspace_panel_family(dst, 30), 7);

    // Promote is rejected when the target is not an empty floor: dst already hosts 30.
    ASSERT_FALSE(dock_workspace_promote(src, dst, 10));
    dock_expect_universe(utest_result, src_tree, 2);
    dock_expect_universe(utest_result, dst_tree, 1);

    // Demote panel 30 back into src.
    ASSERT_TRUE(dock_workspace_demote(src, dst, 30));
    ASSERT_TRUE(dock_tree_check_invariants(src_tree));
    ASSERT_TRUE(dock_tree_check_invariants(dst_tree));
    const FlDockNode* homed = dock_tree_find_leaf_with_panel(src_tree, 30);
    ASSERT_TRUE(homed != nullptr);
    ASSERT_NE((int)dock_workspace_panel_leaf(src, 30), 0);
    dock_expect_leaf(utest_result, dst_tree->root, &(DockLeafWant) { 0 });

    dock_expect_universe(utest_result, src_tree, 3);
    dock_expect_universe(utest_result, dst_tree, 0);
    ASSERT_EQ((int)dock_workspace_panel_family(src, 30), 7);

    // Demoting a panel that is not in dst is rejected and changes nothing.
    ASSERT_FALSE(dock_workspace_demote(src, dst, 30));

    dock_workspace_destroy(src);
    dock_workspace_destroy(dst);
}

// The set of dock trees is app-global (the registry in the shared tier), but dock_workspace_current() resolves to
// the workspace the active window hosts, through its non-owning ctx->dock_workspace. A new context defaults to
// registry entry 0, which is all a single-window app needs. Binding a context to its own workspace makes the
// lookup per window.
//
// A second context and workspace made for one case. dock_ctx_pair_release makes the fixture current again and
// frees both on every exit from the case.
typedef struct DockCtxPair {
    FlUiCtx* fixture;
    FlUiCtx* other;
    FlDockWorkspace* other_ws;
} DockCtxPair;

static void dock_ctx_pair_release(DockCtxPair* pair) {
    ui_make_current(pair->fixture);
    if (pair->other_ws != nullptr) {
        dock_workspace_destroy(pair->other_ws);
    }
    if (pair->other != nullptr) {
        ui_destroy(pair->other);
    }
}

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

    // A second window on the same shared tier defaults to the same registry entry.
    pair.other = ui_create(s_arena, shared);
    ASSERT_TRUE(pair.other != nullptr);
    ui_make_current(pair.other);
    ASSERT_FALSE(dock_current_workspace_holds(MARKER_PANEL));

    // Once the second context is bound to its own workspace, the lookup follows the active context.
    pair.other_ws = dock_workspace_create();
    dock_workspace_set_tree(pair.other_ws, dock_tree_build(dock_leaf(MARKER_PANEL)));
    ui_ctx_set_dock_workspace(pair.other, pair.other_ws);
    ui_make_current(pair.other);
    ASSERT_TRUE(dock_current_workspace_holds(MARKER_PANEL));
    ui_make_current(fixture);
    ASSERT_FALSE(dock_current_workspace_holds(MARKER_PANEL));
}

// Floating is a transient state during a drag, so a panel absent from the tree has nowhere loose to land. Showing
// a closed panel docks it on the floor through the same re-home every dock op falls back to.
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

    // Showing a panel that is already in the tree does not move it.
    const FlDockNode* seat = dock_tree_find_leaf_with_panel(tree, 1);
    ASSERT_TRUE(seat != nullptr);
    const FlDockNodeId seat_id = seat->id;
    dock_workspace_set_panel_visible(ws, 1, true);
    const FlDockNode* after = dock_tree_find_leaf_with_panel(tree, 1);
    ASSERT_TRUE(after != nullptr);
    ASSERT_EQ_MSG((int)after->id, (int)seat_id, "an already-seated panel does not re-home");

    dock_workspace_destroy(ws);
}

UTEST(dock_workspace, a_fresh_workspace_reports_no_live_tiles) {
    FlUiCtx* fixture = render_fixture_get();
    ASSERT_TRUE_MSG(fixture != nullptr, "render fixture creation failed");
    DOCK_DEFAULT_TREE(scope, fixture, dock_leaf(100));

    dock_workspace_begin(scope.ws);
    ASSERT_FALSE_MSG(fl_dock_panel(100, FL_S("Body")), "a panel body cannot attach before the workspace has emitted");
    dock_workspace_end(scope.ws);
}
