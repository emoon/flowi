// Dock Persistence - serialize/restore a workspace layout + the rebind-on-load rules. Data in / data out: no UI
// context, no file I/O (that lives in the dock_workspace hooks). A restored tree is rebuilt node-by-node into a
// fresh dock_tree's arena and then validated through the dock_tree invariant chokepoint, so a malformed blob can
// never produce a holed tree.

#include "dock_persist.h"

#include <core/arena.h>
#include <core/json.h>
#include <core/json_builder.h>
#include <core/log.h>
#include <core/string.h>

#include <math.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// u64 <-> decimal-string. Panel and workspace ids are full 64-bit, so they are serialized as decimal strings to
// preserve every bit - JSON numbers route through the builder's i64 setter, which would mangle ids at or above
// 2^63.

// Write v in decimal into dst (which must hold at least 20 chars) and return the digit count.
static u32 write_u64_digits(char* dst, u64 v) {
    char tmp[20];
    u32 n = 0;
    if (v == 0) {
        tmp[n++] = '0';
    }
    while (v > 0) {
        tmp[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    for (u32 i = 0; i < n; ++i) {
        dst[i] = tmp[n - 1 - i];
    }
    return n;
}

static FlString u64_to_str(FlArena* arena, u64 v) {
    char* out = arena_alloc_array(arena, char, 20);
    u32 n = write_u64_digits(out, v);
    return string_from_cstr_len(out, n);
}

// Parse the leading run of decimal digits into a u64; a non-digit ends the parse, so this doubles as the element
// parser for the space-separated id lists below. A run too long for a u64 saturates to UINT64_MAX and
// still reports its full length as consumed.
static u64 str_to_u64(const char* data, u64 len, u64* consumed) {
    u64 v = 0;
    u64 i = 0;
    bool overflow = false;
    for (; i < len; ++i) {
        char c = data[i];
        if (c < '0' || c > '9') {
            break;
        }
        u64 digit = (u64)(c - '0');
        if (v > (UINT64_MAX - digit) / 10) {
            overflow = true;
            continue;
        }
        v = v * 10 + digit;
    }
    if (consumed != nullptr) {
        *consumed = i;
    }
    return overflow ? UINT64_MAX : v;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlDockNode* leftmost_leaf(FlDockNode* node) {
    while (node != nullptr && node->kind == FlDockNodeKind_Split) {
        node = node->split.child[0];
    }
    return node;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialize

// Panel ids as a single space-separated decimal string ("" when empty) - the json builder has no standalone
// string/number value constructor for array elements.
static FlString id_list_to_str(FlArena* arena, const FlDockPanelId* ids, u32 count) {
    if (count == 0) {
        return string_from_cstr_len("", 0);
    }
    // Worst case 20 digits + 1 separator per id.
    char* buf = arena_alloc_array(arena, char, count * 21);
    u32 n = 0;
    for (u32 i = 0; i < count; ++i) {
        if (i > 0) {
            buf[n++] = ' ';
        }
        n += write_u64_digits(buf + n, ids[i]);
    }
    return string_from_cstr_len(buf, n);
}

static FlJsonValue* node_to_json(const FlDockNode* node, FlArena* arena) {
    FlJsonValue* obj = json_build_object(arena);
    json_build_set_int(arena, obj, FL_S("kind"), (i64)node->kind);
    json_build_set_int(arena, obj, FL_S("id"), (i64)node->id);

    if (node->kind == FlDockNodeKind_Split) {
        json_build_set_int(arena, obj, FL_S("axis"), (i64)node->split.axis);
        json_build_set_float(arena, obj, FL_S("ratio"), (f64)node->split.ratio);
        json_build_set_value(arena, obj, FL_S("child0"), node_to_json(node->split.child[0], arena));
        json_build_set_value(arena, obj, FL_S("child1"), node_to_json(node->split.child[1], arena));
    } else {
        json_build_set_int(arena, obj, FL_S("selected"), (i64)node->leaf.selected);
        json_build_set_string(arena, obj, FL_S("panels"), id_list_to_str(arena, node->leaf.panels, node->leaf.count));
    }
    return obj;
}

static FlJsonValue* workspace_to_json(const FlDockPersistWorkspace* ws, FlArena* arena) {
    FlJsonValue* obj = json_build_object(arena);
    json_build_set_string(arena, obj, FL_S("id"), u64_to_str(arena, ws->id));
    json_build_set_int(arena, obj, FL_S("family"), (i64)ws->family_filter);
    json_build_set_float(arena, obj, FL_S("host_w"), (f64)ws->host_width);
    json_build_set_float(arena, obj, FL_S("host_h"), (f64)ws->host_height);
    json_build_set_int(arena, obj, FL_S("next_node_id"), (i64)ws->tree->next_node_id);
    json_build_set_string(arena, obj, FL_S("floating"),
                          id_list_to_str(arena, ws->tree->floating, ws->tree->floating_count));
    json_build_set_value(arena, obj, FL_S("root"), node_to_json(ws->tree->root, arena));
    return obj;
}

FlJsonValue* dock_persist_to_json(const FlDockPersistWorkspace* workspaces, u32 count, FlArena* arena) {
    if (workspaces == nullptr) {
        return nullptr;
    }
    FlJsonValue* root = json_build_object(arena);
    json_build_set_int(arena, root, FL_S("version"), (i64)FL_DOCK_PERSIST_VERSION);

    FlJsonValue* arr = json_build_array(arena);
    for (u32 i = 0; i < count; ++i) {
        json_build_array_push(arena, arr, workspace_to_json(&workspaces[i], arena));
    }
    json_build_set_value(arena, root, FL_S("workspaces"), arr);
    return root;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Load

// Append every id in a space-separated decimal string to leaf, or to the tree's floating set when floating.
static void parse_id_list(FlDockTree* tree, FlDockNode* leaf, FlString list, bool floating) {
    u64 i = 0;
    while (i < list.length) {
        char c = list.data[i];
        if (c < '0' || c > '9') {
            i++;
            continue;
        }
        u64 consumed = 0;
        u64 v = str_to_u64(list.data + i, list.length - i, &consumed);
        if (floating) {
            dock_tree_floating_append(tree, v);
        } else {
            dock_tree_leaf_append(tree, leaf, v);
        }
        i += consumed > 0 ? consumed : 1;
    }
}

// Depth ceiling on the reconstructed tree. Past it build_node returns null, leaving a split short a child - a
// shape dock_tree_check_invariants rejects, so an over-nested blob fails the load rather than yielding a tree.
#define DOCK_PERSIST_MAX_DEPTH 64

static FlDockNode* build_node(FlDockTree* tree, const FlJsonValue* nj, FlDockNode* parent, u32 depth) {
    if (nj == nullptr || depth >= DOCK_PERSIST_MAX_DEPTH) {
        return nullptr;
    }
    FlDockNode* node = arena_alloc_zero(tree->arena, FlDockNode);
    node->id = fl_json_get_u32_prop(nj, FL_S("id"));
    node->parent = parent;

    u32 kind = fl_json_get_u32_prop(nj, FL_S("kind"));
    if (kind == FlDockNodeKind_Split) {
        node->kind = FlDockNodeKind_Split;
        node->split.axis = (FlDockAxis)fl_json_get_u32_prop(nj, FL_S("axis"));
        node->split.ratio = (f32)fl_json_get_f64_prop(nj, FL_S("ratio"));
        node->split.child[0] = build_node(tree, fl_json_get_object_prop(nj, FL_S("child0")), node, depth + 1);
        node->split.child[1] = build_node(tree, fl_json_get_object_prop(nj, FL_S("child1")), node, depth + 1);
    } else {
        node->kind = FlDockNodeKind_Leaf;
        parse_id_list(tree, node, fl_json_get_string_prop(nj, FL_S("panels")), false);
        u32 selected = fl_json_get_u32_prop(nj, FL_S("selected"));
        node->leaf.selected = (node->leaf.count > 0 && selected < node->leaf.count) ? selected : 0;
    }
    return node;
}

// Rebuild one workspace's tree from JSON, or null if the result fails the invariant checker. The fresh tree's
// original floor leaf leaks into its arena.
static FlDockTree* build_tree(const FlJsonValue* wsj) {
    FlDockTree* tree = dock_tree_create();
    FlJsonValue* root_json = fl_json_get_object_prop(wsj, FL_S("root"));
    if (root_json == nullptr) {
        dock_tree_destroy(tree);
        return nullptr;
    }
    tree->root = build_node(tree, root_json, nullptr, 0);
    parse_id_list(tree, nullptr, fl_json_get_string_prop(wsj, FL_S("floating")), true);

    u32 next_id = fl_json_get_u32_prop(wsj, FL_S("next_node_id"));
    if (next_id == 0) {
        next_id = 1;
    }
    tree->next_node_id = next_id;

    if (!dock_tree_check_invariants(tree)) {
        dock_tree_destroy(tree);
        return nullptr;
    }
    return tree;
}

FlDockPersistLoad dock_persist_from_json(const FlJsonValue* blob, FlArena* arena) {
    FlDockPersistLoad out = { .ok = false, .workspaces = nullptr, .count = 0 };
    if (blob == nullptr || blob->type != FlJsonType_Object) {
        return out;
    }
    if (fl_json_get_u32_prop(blob, FL_S("version")) != FL_DOCK_PERSIST_VERSION) {
        return out; // bad-version-to-default
    }

    FlJsonArray* arr = fl_json_get_array_prop(blob, FL_S("workspaces"));
    u32 count = arr != nullptr ? (u32)arr->length : 0;
    if (count == 0) {
        out.ok = true; // a valid but empty blob
        return out;
    }

    FlDockPersistWorkspace* list = arena_alloc_array(arena, FlDockPersistWorkspace, count);
    u32 i = 0;
    for (FlJsonArrayElement* e = arr->start; e != nullptr && i < count; e = e->next, ++i) {
        const FlJsonValue* wsj = e->value;
        FlDockTree* tree = build_tree(wsj);
        if (tree == nullptr) {
            // Malformed tree - release everything built so far.
            for (u32 j = 0; j < i; ++j) {
                dock_tree_destroy(list[j].tree);
            }
            return out;
        }
        FlString id_str = fl_json_get_string_prop(wsj, FL_S("id"));
        list[i].id = str_to_u64(id_str.data, id_str.length, nullptr);
        list[i].family_filter = fl_json_get_u32_prop(wsj, FL_S("family"));
        list[i].host_width = (f32)fl_json_get_f64_prop(wsj, FL_S("host_w"));
        list[i].host_height = (f32)fl_json_get_f64_prop(wsj, FL_S("host_h"));
        list[i].tree = tree;
    }

    out.ok = true;
    out.workspaces = list;
    out.count = i;
    return out;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Reconcile (the rebind-on-load rules)

static bool id_in(const u64* ids, u32 count, u64 id) {
    for (u32 i = 0; i < count; ++i) {
        if (ids[i] == id) {
            return true;
        }
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static bool panel_in_any(const FlDockPersistLoad* load, FlDockPanelId p) {
    for (u32 i = 0; i < load->count; ++i) {
        if (dock_tree_has_panel(load->workspaces[i].tree, p)) {
            return true;
        }
    }
    return false;
}

// Dock p into target's root region (its leftmost leaf), re-homing a panel that lost its workspace.
static void reparent_to_root(FlDockTree* target, FlDockPanelId p) {
    FlDockNode* leaf = leftmost_leaf(target->root);
    if (leaf == nullptr) {
        return;
    }
    FlDockRequest req = { .kind = FlDockRequest_Tab, .target_node = leaf->id, .panel = p };
    dock_tree_apply(target, &req);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Close every panel in tree that declared does not name. Callers skip this for an empty roster.

static void prune_undeclared(FlDockTree* tree, const FlDockPanelId* declared, u32 declared_count) {
    u32 total = dock_tree_collect_panels(tree, nullptr, 0);
    if (total == 0) {
        return;
    }
    arena_scratch_auto_conflict(temp, tree->arena);
    FlDockPanelId* ids = arena_alloc_array(temp.arena, FlDockPanelId, total);
    total = dock_tree_collect_panels(tree, ids, total);

    for_count(i, total) {
        if (id_in(declared, declared_count, ids[i])) {
            continue;
        }
        log_info("dock: saved panel %llu is not declared this session — dropping it from the restored layout",
                 (unsigned long long)ids[i]);
        FlDockRequest close = { .kind = FlDockRequest_Close, .panel = ids[i] };
        dock_tree_apply(tree, &close);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dock every parked panel into tree's root region, emptying the floating set. The count is cleared before the
// walk, not after, so each re-dock is a plain insert and the saved order survives.

static void drain_floating(FlDockTree* tree) {
    u32 count = tree->floating_count;
    tree->floating_count = 0;
    for_count(i, count) {
        reparent_to_root(tree, tree->floating[i]);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A panel already present in target (a cross-workspace id collision) is a no-op move.
static void reparent_docked_rec(const FlDockNode* node, FlDockTree* target) {
    if (node == nullptr) {
        return;
    }
    if (node->kind == FlDockNodeKind_Leaf) {
        for (u32 i = 0; i < node->leaf.count; ++i) {
            if (!dock_tree_has_panel(target, node->leaf.panels[i])) {
                reparent_to_root(target, node->leaf.panels[i]);
            }
        }
        return;
    }
    reparent_docked_rec(node->split.child[0], target);
    reparent_docked_rec(node->split.child[1], target);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static void reparent_workspace(FlDockTree* src, FlDockTree* target) {
    reparent_docked_rec(src->root, target);
    for (u32 i = 0; i < src->floating_count; ++i) {
        if (!dock_tree_has_panel(target, src->floating[i])) {
            dock_tree_floating_append(target, src->floating[i]);
        }
    }
}

void dock_persist_reconcile(FlDockPersistLoad* load, const u64* live_ws_ids, u32 live_count,
                            const FlDockPanelId* declared, u32 declared_count) {
    if (load == nullptr || !load->ok || load->count == 0) {
        return;
    }

    // The surviving root workspace: the first live one, or index 0 defensively if none matched.
    u32 target_idx = 0;
    bool found = false;
    for (u32 i = 0; i < load->count; ++i) {
        if (id_in(live_ws_ids, live_count, load->workspaces[i].id)) {
            target_idx = i;
            found = true;
            break;
        }
    }
    FlDockTree* target = load->workspaces[target_idx].tree;

    // dangling-workspace-reparent: fold every non-live workspace into the target, then compact it out.
    u32 kept = 0;
    for (u32 i = 0; i < load->count; ++i) {
        bool live = (i == target_idx) || (found && id_in(live_ws_ids, live_count, load->workspaces[i].id));
        if (live) {
            load->workspaces[kept++] = load->workspaces[i];
            continue;
        }
        reparent_workspace(load->workspaces[i].tree, target);
        dock_tree_destroy(load->workspaces[i].tree);
    }
    load->count = kept;

    // stale-panel-prune: a saved id the roster does not name is closed. An empty roster prunes nothing.
    if (declared_count > 0) {
        for_count(i, load->count) {
            prune_undeclared(load->workspaces[i].tree, declared, declared_count);
        }
    }

    // unknown-panel-to-default: a panel declared this session but absent everywhere lands loose in the target.
    for_count(i, declared_count) {
        if (!panel_in_any(load, declared[i])) {
            dock_tree_floating_append(target, declared[i]);
        }
    }

    // Dock every floating panel at its tree root.
    for_count(i, load->count) {
        drain_floating(load->workspaces[i].tree);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Structural equality

static bool nodes_equal(const FlDockNode* a, const FlDockNode* b) {
    if (a == nullptr || b == nullptr) {
        return a == b;
    }
    if (a->kind != b->kind) {
        return false;
    }
    if (a->kind == FlDockNodeKind_Split) {
        if (a->split.axis != b->split.axis) {
            return false;
        }
        if (fabsf(a->split.ratio - b->split.ratio) > 1e-4f) {
            return false;
        }
        return nodes_equal(a->split.child[0], b->split.child[0]) && nodes_equal(a->split.child[1], b->split.child[1]);
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

bool dock_persist_tree_equal(const FlDockTree* a, const FlDockTree* b) {
    if (a == nullptr || b == nullptr) {
        return a == b;
    }
    if (a->floating_count != b->floating_count) {
        return false;
    }
    for (u32 i = 0; i < a->floating_count; ++i) {
        if (a->floating[i] != b->floating[i]) {
            return false;
        }
    }
    return nodes_equal(a->root, b->root);
}
