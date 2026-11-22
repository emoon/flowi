// Dock persistence: serializing a workspace layout and rebinding it on load.

#include "dock/common.h"

// An arena released when the scope ends, on every exit path.
static void dock_arena_release(FlArena** arena) {
    fl_arena_destroy(*arena);
}

#define DOCK_ARENA(var) __attribute__((cleanup(dock_arena_release))) FlArena* var = arena_new()

// Dock persistence: serializing a workspace layout to an opaque blob and restoring it, with the rebind-on-load
// rules (ui/dock/dock_persist.{h,c}). Every case is headless.

// A directory under the system temp dir, named for this process and case, emptied on creation and removed when the
// scope ends.
typedef struct DockTempDir {
    FlArena* arena;
    FlString path;
} DockTempDir;

static DockTempDir dock_temp_dir_make(const char* name) {
    DockTempDir dir = { .arena = arena_new() };
    FlString leaf = fl_sprintf_arena(dir.arena, "flowi_dock_%s_%u", name, fl_os_process_id());
    dir.path = file_build_path_os(dir.arena, fl_os_temp_directory(dir.arena), leaf);
    file_remove_directory_recursive_os(dir.path);
    return dir;
}

static void dock_temp_dir_remove(DockTempDir* dir) {
    file_remove_directory_recursive_os(dir->path);
    fl_arena_destroy(dir->arena);
}

// Shuts the log file down if it was opened.
static void dock_log_file_close(bool* open) {
    if (*open) {
        fl_log_file_shutdown();
    }
}

#define DOCK_TEMP_DIR(var, name) \
    __attribute__((cleanup(dock_temp_dir_remove))) DockTempDir var = dock_temp_dir_make(name)

// Asserts every panel is docked in a leaf of tree.
static void dock_expect_docked(int* utest_result, FlDockTree* tree, const FlDockPanelId* panels, u32 count) {
    for (u32 i = 0; i < count; ++i) {
        ASSERT_TRUE_MSG(dock_tree_find_leaf_with_panel(tree, panels[i]) != nullptr, "the panel is docked in a leaf");
    }
}

// Parses JSON text, with the relaxed flags the persisted layout is read with, into the arena.
static FlJsonValue* dock_json_parse(FlArena* arena, FlString text) {
    return fl_json_parse_with_flags(arena, text, FlJsonParseFlags_AllowJson5, nullptr);
}

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

// True when tree serializes, loads back as one workspace, and the loaded tree passes the invariants and equals it.
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
    // The corpus covers the floor, a single leaf, a multi-tab leaf with a non-zero selection, a deeply nested
    // split tree, and full 64-bit panel ids, whose string encoding has to keep the top bit.
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
    // Select the second tab in the (2,3) leaf and undock 4 to the floating set, so the corpus tree carries a
    // non-default selection and a loose panel.
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
    // The per-workspace attributes (id, family filter and host size) round-trip. Position has no field, so it is
    // never persisted.
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
    const FlDockPersistWorkspace* got = &load.workspaces[0];
    ASSERT_TRUE(got->id == 0xABCDEF0123456789ull);
    ASSERT_EQ((int)got->family_filter, 9);
    ASSERT_NEAR(got->host_width, 1280.5f, 0.01f);
    ASSERT_NEAR(got->host_height, 720.25f, 0.01f);
    dock_tree_destroy(load.workspaces[0].tree);
    dock_tree_destroy(tree);
}

UTEST(dock_persist, rebind_bad_version_to_default) {
    // A syntactically valid blob with a mismatched version reports failure, so the caller falls back to its
    // code-defined default layout.
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

// Builds a chain of depth splits, each nesting the next in child0 and closing child1 with a leaf.
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
    // An over-nested tree and an overlong digit run are both stopped before the tree builder accepts them.
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

    // Past the ceiling, the truncated recursion leaves a split short of a child, which the invariant check
    // rejects. 200 is under JSON_PARSE_MAX_DEPTH, so the parser passes the blob on to the tree builder.
    FlString deep = deep_blob(arena, 200);
    FlJsonValue* deep_blob_json = dock_json_parse(arena, deep);
    ASSERT_TRUE(deep_blob_json != nullptr);
    ASSERT_FALSE(dock_persist_from_json(deep_blob_json, arena).ok);

    // A 40-digit panel id saturates and does not wrap onto a live id, and the separator scan still finds the next
    // element.
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
    // A panel declared this session but absent from the saved tree is docked into the root region and never
    // dropped. The saved panels stay where they were.
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
    dock_expect_docked(utest_result, r, declared, sizeof_array(declared));
    dock_tree_destroy(r);
}

UTEST(dock_persist, rebind_dangling_workspace_reparent) {
    // A saved workspace whose host is gone this session is dissolved, and its panels, loose and docked, are
    // reparented into the surviving root workspace.
    DOCK_ARENA(arena);
    FlDockTree* t1 = dock_tree_build(dock_leaf(1)); // workspace 1: live host
    FlDockTree* t2 = dock_tree_build(dock_split(FlDockAxis_X, 0.5f, dock_leaf(2), dock_leaf(3))); // workspace 2: gone
    FlDockRequest undock = { .kind = FlDockRequest_Undock, .panel = 3 };
    ASSERT_TRUE(dock_tree_apply(t2, &undock));

    FlDockPersistLoad load = dock_persist_reload(arena, (FlDockTree*[]) { t1, t2 }, 2);
    ASSERT_TRUE(load.ok);
    ASSERT_EQ((int)load.count, 2);

    const u64 live[] = { 1 }; // only workspace 1 exists this session, so workspace 2 is dangling
    dock_persist_reconcile(&load, live, 1, nullptr, 0);

    ASSERT_EQ((int)load.count, 1);
    FlDockTree* r = load.workspaces[0].tree;
    ASSERT_TRUE(load.workspaces[0].id == 1);
    ASSERT_TRUE(dock_tree_check_invariants(r));
    const FlDockPanelId all[] = { 1, 2, 3 };
    dock_expect_docked(utest_result, r, all, sizeof_array(all));
    dock_tree_destroy(r);
}

UTEST(dock_persist, workspace_save_load_and_prune) {
    ASSERT_TRUE_MSG(render_fixture_get() != nullptr, "render fixture creation failed");
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
    ASSERT_NEAR(ws2->host_height, 768.0f, 0.01f); // position has no field and is never persisted

    // The roster panels keep the saved topology. 13 is removed from the universe, where it would otherwise stay
    // as a tab with no title and no close button.
    FlDockNodeId leaf11 = dock_workspace_panel_leaf(ws2, 11);
    FlDockNodeId leaf12 = dock_workspace_panel_leaf(ws2, 12);
    ASSERT_TRUE(leaf11 != 0);
    ASSERT_TRUE(leaf12 != 0);
    ASSERT_TRUE(leaf11 != leaf12);
    const PanelLeaf pruned[] = { { 13, 0 } };
    dock_expect_panel_leaves(utest_result, ws2, pruned, sizeof_array(pruned));
    dock_expect_universe(utest_result, ws2->tree, 2);

    // Reserve is still per frame: 12 survived the roster and reports reserved when not declared this frame.
    dock_workspace_begin(ws2);
    dock_workspace_declare(ws2, 11, FL_S("Eleven"));
    dock_workspace_end(ws2);
    const PanelWant frame[] = {
        { .panel = 12, .reserved = DockTri_Yes },
        { .panel = 11, .declared = DockTri_Yes },
        { .panel = 13, .reserved = DockTri_No }, // pruned, so it has no slot left to reserve
    };
    dock_expect_panel_states(utest_result, ws2, frame, sizeof_array(frame));

    // Titles are not in the blob, so a restored panel the app has not declared this session is untitled and
    // carries no stale label.
    dock_expect_title(utest_result, ws2, 12, FL_S(""));
    dock_expect_title(utest_result, ws2, 11, FL_S("Eleven"));

    // Visibility is not in the blob either. A panel hidden when the layout was saved loads back shown.
    const PanelWant restored[] = { { .panel = 12, .visible = DockTri_Yes } };
    dock_expect_panel_states(utest_result, ws2, restored, sizeof_array(restored));

    // With no saved layout, load returns false so the caller keeps its default.
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
    const FlDockPanelId saved_panels[] = { 1, 2 };
    dock_expect_docked(utest_result, r, saved_panels, sizeof_array(saved_panels));
    dock_tree_destroy(r);
}

UTEST(dock_persist, prune_logs_the_drop) {
    // The prune removes a panel that the user's saved layout still shows, so it is logged.
    DOCK_TEMP_DIR(temp, "prune_log");
    __attribute__((cleanup(dock_log_file_close))) bool log_open = fl_log_file_init(temp.path);
    ASSERT_TRUE(log_open);
    fl_log_set_level(LogLevel_Info); // the drop is logged at info, below the test runner's level

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
    // Floating is transient drag state, so reconcile leaves nothing floating. The dangling workspace's loose panel
    // and a roster panel absent from every saved tree both end up docked.
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
    dock_expect_docked(utest_result, r, roster, sizeof_array(roster));
    dock_tree_destroy(r);
}

UTEST(dock_persist, rehydrated_floating_drains) {
    // A blob with a non-empty floating string loads those panels as floating, and reconcile then docks them in
    // order.
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
    dock_expect_tab_order(utest_result, r->root, &(DockLeafWant) { .count = 3, .panels = { 7, 42, 43 } });
    dock_tree_destroy(r);
}
