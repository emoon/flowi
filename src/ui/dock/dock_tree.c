// Every node lives in the tree's arena and is never freed on its own. Collapsed and dissolved nodes become
// unreachable and go away when the arena is destroyed.

#include "dock_tree.h"

#include <core/arena.h>

#include <string.h>

static f32 clamp_ratio(f32 r) {
    if (r < FL_DOCK_MIN_RATIO) {
        return FL_DOCK_MIN_RATIO;
    }
    if (r > 1.0f - FL_DOCK_MIN_RATIO) {
        return 1.0f - FL_DOCK_MIN_RATIO;
    }
    return r;
}

static FlDockNode* node_new_leaf(FlDockTree* tree) {
    FlDockNode* node = arena_alloc_zero(tree->arena, FlDockNode);
    node->kind = FlDockNodeKind_Leaf;
    node->id = tree->next_node_id++;
    return node;
}

void* dock_arena_array_reserve(FlArena* arena, void* items, u32 count, u32* cap, u32 elem_size, u32 need) {
    if (*cap >= need) {
        return items;
    }
    u32 new_cap = *cap ? *cap * 2 : 4;
    while (new_cap < need) {
        new_cap *= 2;
    }
    u8* grown = (u8*)fl_arena_alloc_raw(arena, (u64)new_cap * elem_size, 8);
    if (count > 0) {
        memcpy(grown, items, (u64)count * elem_size);
    }
    *cap = new_cap;
    return grown;
}

static void leaf_reserve(FlDockTree* tree, FlDockNode* leaf, u32 need) {
    leaf->leaf.panels = (FlDockPanelId*)dock_arena_array_reserve(tree->arena, leaf->leaf.panels, leaf->leaf.count,
                                                                 &leaf->leaf.cap, sizeof(FlDockPanelId), need);
}

void dock_tree_leaf_append(FlDockTree* tree, FlDockNode* leaf, FlDockPanelId panel) {
    leaf_reserve(tree, leaf, leaf->leaf.count + 1);
    leaf->leaf.panels[leaf->leaf.count++] = panel;
}

static u32 leaf_index_of(const FlDockNode* leaf, FlDockPanelId panel) {
    for_count(i, leaf->leaf.count) {
        if (leaf->leaf.panels[i] == panel) {
            return i;
        }
    }
    return UINT32_MAX;
}

// Remove the panel at idx from leaf, fixing the selected-tab index. The caller collapses an emptied leaf.
static void leaf_remove_at(FlDockNode* leaf, u32 idx) {
    for (u32 i = idx; i + 1 < leaf->leaf.count; ++i) {
        leaf->leaf.panels[i] = leaf->leaf.panels[i + 1];
    }
    leaf->leaf.count--;

    if (leaf->leaf.count == 0) {
        leaf->leaf.selected = 0;
    } else if (leaf->leaf.selected == idx) {
        if (leaf->leaf.selected >= leaf->leaf.count) {
            leaf->leaf.selected = leaf->leaf.count - 1;
        }
    } else if (leaf->leaf.selected > idx) {
        leaf->leaf.selected--;
    }
}

static void floating_reserve(FlDockTree* tree, u32 need) {
    tree->floating = (FlDockPanelId*)dock_arena_array_reserve(tree->arena, tree->floating, tree->floating_count,
                                                              &tree->floating_cap, sizeof(FlDockPanelId), need);
}

static u32 floating_index_of(const FlDockTree* tree, FlDockPanelId panel) {
    for_count(i, tree->floating_count) {
        if (tree->floating[i] == panel) {
            return i;
        }
    }
    return UINT32_MAX;
}

static void floating_remove_at(FlDockTree* tree, u32 idx) {
    for (u32 i = idx; i + 1 < tree->floating_count; ++i) {
        tree->floating[i] = tree->floating[i + 1];
    }
    tree->floating_count--;
}

void dock_tree_floating_append(FlDockTree* tree, FlDockPanelId panel) {
    floating_reserve(tree, tree->floating_count + 1);
    tree->floating[tree->floating_count++] = panel;
}

// Tree walking

static FlDockNode* find_node_rec(FlDockNode* node, FlDockNodeId id) {
    if (node == nullptr) {
        return nullptr;
    }
    if (node->id == id) {
        return node;
    }
    if (node->kind == FlDockNodeKind_Split) {
        FlDockNode* hit = find_node_rec(node->split.child[0], id);
        if (hit != nullptr) {
            return hit;
        }
        return find_node_rec(node->split.child[1], id);
    }
    return nullptr;
}

static FlDockNode* find_leaf_with_panel_rec(FlDockNode* node, FlDockPanelId panel) {
    if (node == nullptr) {
        return nullptr;
    }
    if (node->kind == FlDockNodeKind_Leaf) {
        return leaf_index_of(node, panel) != UINT32_MAX ? node : nullptr;
    }
    FlDockNode* hit = find_leaf_with_panel_rec(node->split.child[0], panel);
    if (hit != nullptr) {
        return hit;
    }
    return find_leaf_with_panel_rec(node->split.child[1], panel);
}

// Put replacement in the slot node holds under its parent, or at the root when node is the root.
static void replace_in_parent(FlDockTree* tree, const FlDockNode* node, FlDockNode* replacement) {
    FlDockNode* parent = node->parent;
    replacement->parent = parent;
    if (parent == nullptr) {
        tree->root = replacement;
    } else if (parent->split.child[0] == node) {
        parent->split.child[0] = replacement;
    } else {
        parent->split.child[1] = replacement;
    }
}

// Collapse: an emptied non-root leaf dissolves its single-child split; the sibling absorbs the slot. The root is
// exempt (it degrades to an empty leaf and stays). leaf must be empty.
static void collapse_empty_leaf(FlDockTree* tree, FlDockNode* leaf) {
    if (leaf == tree->root) {
        return; // the root is the floor and stays an empty leaf
    }

    FlDockNode* parent = leaf->parent; // a split with exactly two children, one of which is leaf
    bool leaf_is_first = parent->split.child[0] == leaf;
    FlDockNode* sibling = leaf_is_first ? parent->split.child[1] : parent->split.child[0];
    replace_in_parent(tree, parent, sibling);
}

// Detach panel from wherever it currently lives (a leaf or the floating set), collapsing an emptied non-root
// leaf. Returns true if the panel was found and removed. Every op that re-places a panel detaches first, so a dock
// of a panel that is already present moves it.
static bool detach_panel(FlDockTree* tree, FlDockPanelId panel) {
    u32 fidx = floating_index_of(tree, panel);
    if (fidx != UINT32_MAX) {
        floating_remove_at(tree, fidx);
        return true;
    }

    FlDockNode* leaf = find_leaf_with_panel_rec(tree->root, panel);
    if (leaf == nullptr) {
        return false;
    }
    leaf_remove_at(leaf, leaf_index_of(leaf, panel));
    if (leaf->leaf.count == 0) {
        collapse_empty_leaf(tree, leaf);
    }
    return true;
}

// child[0] is the left/top side.
static bool side_is_first(FlDockSide side) {
    return side == FlDockSide_Left || side == FlDockSide_Top;
}

static FlDockAxis side_axis(FlDockSide side) {
    return (side == FlDockSide_Left || side == FlDockSide_Right) ? FlDockAxis_X : FlDockAxis_Y;
}

// Split insertion: replace target (anywhere in the tree, including the root) with a new split whose other child
// is a fresh leaf holding req->panel, placed on req->side with req->ratio as its share.
static void insert_split_at(FlDockTree* tree, FlDockNode* target, const FlDockRequest* req) {
    FlDockNode* new_leaf = node_new_leaf(tree);
    dock_tree_leaf_append(tree, new_leaf, req->panel);

    // The new leaf takes the named side, the target the other.
    bool new_is_first = side_is_first(req->side);
    f32 share = clamp_ratio(req->ratio > 0.0f ? req->ratio : FL_DOCK_SPLIT_RATIO_DEFAULT);

    FlDockNode* split = arena_alloc_zero(tree->arena, FlDockNode);
    split->kind = FlDockNodeKind_Split;
    split->id = tree->next_node_id++;
    split->split.axis = side_axis(req->side);
    split->split.ratio = new_is_first ? share : 1.0f - share;
    split->split.child[0] = new_is_first ? new_leaf : target;
    split->split.child[1] = new_is_first ? target : new_leaf;

    replace_in_parent(tree, target, split); // reads target's old parent, so it runs before the re-parenting
    new_leaf->parent = split;
    target->parent = split;
}

// Operations

static bool is_empty_leaf(const FlDockNode* node) {
    return node->kind == FlDockNodeKind_Leaf && node->leaf.count == 0;
}

static bool leaf_holds_only(const FlDockNode* node, FlDockPanelId panel) {
    return node->kind == FlDockNodeKind_Leaf && node->leaf.count == 1 && node->leaf.panels[0] == panel;
}

// Dock an already-detached panel onto the root: into the root leaf if it is empty, else as a fresh split off the
// root. Used when a dock's target dissolved as the panel left it, so the panel is not lost.
static void rehome_to_root(FlDockTree* tree, FlDockPanelId panel) {
    if (is_empty_leaf(tree->root)) {
        dock_tree_leaf_append(tree, tree->root, panel);
        tree->root->leaf.selected = 0;
        return;
    }
    FlDockRequest right_of_root = {
        .kind = FlDockRequest_Outer, .panel = panel, .side = FlDockSide_Right, .ratio = FL_DOCK_SPLIT_RATIO_DEFAULT
    };
    insert_split_at(tree, tree->root, &right_of_root);
}

static bool apply_tab(FlDockTree* tree, const FlDockRequest* req) {
    FlDockNode* target = find_node_rec(tree->root, req->target_node);
    if (target == nullptr || target->kind != FlDockNodeKind_Leaf) {
        return false;
    }

    u32 existing = leaf_index_of(target, req->panel);
    if (existing != UINT32_MAX) {
        target->leaf.selected = existing; // already here, so re-select it
        return true;
    }

    detach_panel(tree, req->panel);                       // move (or no-op for a brand-new panel)
    target = find_node_rec(tree->root, req->target_node); // detach may have reshaped the tree
    if (target != nullptr && target->kind == FlDockNodeKind_Leaf) {
        dock_tree_leaf_append(tree, target, req->panel);
        target->leaf.selected = target->leaf.count - 1;
        return true;
    }
    rehome_to_root(tree, req->panel); // the target dissolved with the detach
    return true;
}

// Shared body for dock-split (target = a node id) and dock-outer (target = the root). Splitting an empty leaf
// would create an empty non-root sibling, so it degenerates to dock-as-tab into that leaf, which is how the
// first panel lands in a fresh tree.
static bool apply_split_against(FlDockTree* tree, FlDockNode* target, const FlDockRequest* req) {
    FlDockNodeId target_id = target->id;

    // Splitting a leaf whose sole content already is this panel is a no-op: detaching first would collapse the
    // target out from under it.
    if (leaf_holds_only(target, req->panel)) {
        return true;
    }

    detach_panel(tree, req->panel);
    target = find_node_rec(tree->root, target_id); // detach may have reshaped (or dissolved) the target
    if (target == nullptr) {
        rehome_to_root(tree, req->panel); // the intended target dissolved when the panel left
        return true;
    }

    if (is_empty_leaf(target)) {
        dock_tree_leaf_append(tree, target, req->panel);
        target->leaf.selected = 0;
        return true;
    }

    insert_split_at(tree, target, req);
    return true;
}

static bool apply_split(FlDockTree* tree, const FlDockRequest* req) {
    FlDockNode* target = find_node_rec(tree->root, req->target_node);
    if (target == nullptr) {
        return false;
    }
    return apply_split_against(tree, target, req);
}

static bool apply_outer(FlDockTree* tree, const FlDockRequest* req) {
    return apply_split_against(tree, tree->root, req);
}

static bool apply_undock(FlDockTree* tree, const FlDockRequest* req) {
    FlDockNode* leaf = find_leaf_with_panel_rec(tree->root, req->panel);
    if (leaf == nullptr) {
        return false; // not docked (already floating, or absent)
    }
    detach_panel(tree, req->panel);
    dock_tree_floating_append(tree, req->panel);
    return true;
}

// Move the panel at from to slot to, sliding the panels between them one slot over.
static void leaf_move_panel(FlDockNode* leaf, u32 from, u32 to) {
    FlDockPanelId* panels = leaf->leaf.panels;
    FlDockPanelId moved = panels[from];
    if (from < to) {
        memmove(&panels[from], &panels[from + 1], (to - from) * sizeof(FlDockPanelId));
    } else {
        memmove(&panels[to + 1], &panels[to], (from - to) * sizeof(FlDockPanelId));
    }
    panels[to] = moved;
}

static bool apply_reorder_tab(FlDockTree* tree, const FlDockRequest* req) {
    FlDockNode* leaf = find_leaf_with_panel_rec(tree->root, req->panel);
    if (leaf == nullptr) {
        return false;
    }
    u32 from = leaf_index_of(leaf, req->panel);
    u32 to = req->to_index;
    if (to >= leaf->leaf.count) {
        to = leaf->leaf.count - 1;
    }
    if (from == to) {
        return true;
    }

    FlDockPanelId selected_id = leaf->leaf.panels[leaf->leaf.selected];
    leaf_move_panel(leaf, from, to);
    leaf->leaf.selected = leaf_index_of(leaf, selected_id); // selection follows the panel, not the slot index
    return true;
}

static bool apply_close(FlDockTree* tree, const FlDockRequest* req) {
    return detach_panel(tree, req->panel); // collapses an emptied leaf; a floating panel is removed from the set
}

static bool apply_splitter_resize(FlDockTree* tree, const FlDockRequest* req) {
    FlDockNode* node = find_node_rec(tree->root, req->target_node);
    if (node == nullptr || node->kind != FlDockNodeKind_Split) {
        return false;
    }
    node->split.ratio = clamp_ratio(req->ratio);
    return true;
}

FlDockTree* dock_tree_create_from(FlDockNodeId first_node_id) {
    FlArena* arena = arena_new();
    FlDockTree* tree = arena_alloc_zero(arena, FlDockTree);
    tree->arena = arena;
    tree->next_node_id = first_node_id;
    tree->root = node_new_leaf(tree);
    return tree;
}

FlDockTree* dock_tree_create(void) {
    return dock_tree_create_from(1); // 0 is reserved as "no node"
}

void dock_tree_destroy(FlDockTree* tree) {
    if (tree != nullptr) {
        fl_arena_destroy(tree->arena); // the tree struct itself lives in this arena
    }
}

// Deep-copy a node hierarchy into arena, preserving every node's id so requests addressing original ids hit the
// same nodes in the clone.
static FlDockNode* clone_node_rec(FlArena* arena, const FlDockNode* src, FlDockNode* parent) {
    if (src == nullptr) {
        return nullptr;
    }
    FlDockNode* node = arena_alloc_zero(arena, FlDockNode);
    node->kind = src->kind;
    node->id = src->id;
    node->parent = parent;

    if (src->kind == FlDockNodeKind_Split) {
        node->split.axis = src->split.axis;
        node->split.ratio = src->split.ratio;
        node->split.child[0] = clone_node_rec(arena, src->split.child[0], node);
        node->split.child[1] = clone_node_rec(arena, src->split.child[1], node);
    } else {
        node->leaf.count = src->leaf.count;
        node->leaf.selected = src->leaf.selected;
        node->leaf.cap = src->leaf.count;
        if (src->leaf.count > 0) {
            node->leaf.panels = arena_alloc_array(arena, FlDockPanelId, node->leaf.cap);
            memcpy(node->leaf.panels, src->leaf.panels, src->leaf.count * sizeof(FlDockPanelId));
        }
    }
    return node;
}

FlDockTree* dock_tree_clone(const FlDockTree* src) {
    if (src == nullptr) {
        return nullptr;
    }
    FlArena* arena = arena_new();
    FlDockTree* tree = arena_alloc_zero(arena, FlDockTree);
    tree->arena = arena;
    tree->next_node_id = src->next_node_id; // so ids minted after the clone match what the original would mint
    tree->root = clone_node_rec(arena, src->root, nullptr);

    if (src->floating_count > 0) {
        tree->floating_cap = src->floating_count;
        tree->floating = arena_alloc_array(arena, FlDockPanelId, tree->floating_cap);
        memcpy(tree->floating, src->floating, src->floating_count * sizeof(FlDockPanelId));
        tree->floating_count = src->floating_count;
    }
    return tree;
}

// Builds whatever the spec describes, holes included; dock_tree_build validates the whole result afterward. A nullptr
// spec child yields a nullptr node, which the invariant checker then rejects as a missing-child split.
static FlDockNode* build_spec_rec(FlDockTree* tree, const FlDockSpec* spec, FlDockNode* parent) {
    if (spec == nullptr) {
        return nullptr;
    }

    if (spec->is_split) {
        FlDockNode* split = arena_alloc_zero(tree->arena, FlDockNode);
        split->kind = FlDockNodeKind_Split;
        split->id = tree->next_node_id++;
        split->parent = parent;
        split->split.axis = spec->axis;
        split->split.ratio = clamp_ratio(spec->ratio);
        split->split.child[0] = build_spec_rec(tree, spec->child[0], split);
        split->split.child[1] = build_spec_rec(tree, spec->child[1], split);
        return split;
    }

    FlDockNode* leaf = node_new_leaf(tree);
    leaf->parent = parent;
    for_count(i, spec->panel_count) {
        dock_tree_leaf_append(tree, leaf, spec->panels[i]);
    }
    if (spec->panel_count > 0) {
        leaf->leaf.selected = spec->selected < spec->panel_count ? spec->selected : 0;
    }
    return leaf;
}

FlDockTree* dock_tree_build(const FlDockSpec* spec) {
    FlDockTree* tree = dock_tree_create(); // starts as the floor; a nullptr spec keeps it
    if (spec != nullptr) {
        tree->root = build_spec_rec(tree, spec, nullptr); // the original empty-leaf root leaks into the arena
    }
    if (!dock_tree_check_invariants(tree)) {
        dock_tree_destroy(tree); // a malformed literal
        return nullptr;
    }
    return tree;
}

bool dock_tree_apply(FlDockTree* tree, const FlDockRequest* req) {
    switch (req->kind) {
        case FlDockRequest_Tab:
            return apply_tab(tree, req);
        case FlDockRequest_Split:
            return apply_split(tree, req);
        case FlDockRequest_Outer:
            return apply_outer(tree, req);
        case FlDockRequest_Undock:
            return apply_undock(tree, req);
        case FlDockRequest_ReorderTab:
            return apply_reorder_tab(tree, req);
        case FlDockRequest_Close:
            return apply_close(tree, req);
        case FlDockRequest_SplitterResize:
            return apply_splitter_resize(tree, req);
    }
    return false;
}

FlDockNode* dock_tree_find_node(const FlDockTree* tree, FlDockNodeId id) {
    if (tree == nullptr || id == 0) {
        return nullptr;
    }
    return find_node_rec(tree->root, id);
}

FlDockNode* dock_tree_find_leaf_with_panel(const FlDockTree* tree, FlDockPanelId panel) {
    if (tree == nullptr) {
        return nullptr;
    }
    return find_leaf_with_panel_rec(tree->root, panel);
}

bool dock_tree_panel_is_floating(const FlDockTree* tree, FlDockPanelId panel) {
    return tree != nullptr && floating_index_of(tree, panel) != UINT32_MAX;
}

bool dock_tree_has_panel(const FlDockTree* tree, FlDockPanelId panel) {
    return dock_tree_find_leaf_with_panel(tree, panel) != nullptr || dock_tree_panel_is_floating(tree, panel);
}

static u32 collect_rec(const FlDockNode* node, FlDockPanelId* out, u32 max, u32 written) {
    if (node == nullptr) {
        return written;
    }
    if (node->kind == FlDockNodeKind_Leaf) {
        for_count(i, node->leaf.count) {
            if (out != nullptr && written < max) {
                out[written] = node->leaf.panels[i];
            }
            written++;
        }
        return written;
    }
    written = collect_rec(node->split.child[0], out, max, written);
    written = collect_rec(node->split.child[1], out, max, written);
    return written;
}

u32 dock_tree_collect_panels(const FlDockTree* tree, FlDockPanelId* out, u32 max) {
    if (tree == nullptr) {
        return 0;
    }
    u32 written = collect_rec(tree->root, out, max, 0);
    for_count(i, tree->floating_count) {
        if (out != nullptr && written < max) {
            out[written] = tree->floating[i];
        }
        written++;
    }
    return written;
}

// Invariants

static bool check_node_rec(const FlDockTree* tree, const FlDockNode* node, const FlDockNode* expected_parent);

static bool leaf_is_valid(const FlDockTree* tree, const FlDockNode* leaf) {
    if (leaf->leaf.count == 0) {
        return leaf == tree->root; // only the root may be an empty leaf
    }
    return leaf->leaf.selected < leaf->leaf.count; // selected tab must be a member
}

static bool has_two_distinct_children(const FlDockNode* split) {
    const FlDockNode* a = split->split.child[0];
    const FlDockNode* b = split->split.child[1];
    return a != nullptr && b != nullptr && a != b;
}

static bool axis_is_valid(FlDockAxis axis) {
    return axis == FlDockAxis_X || axis == FlDockAxis_Y;
}

// The ratio must sit inside the min-size floor. A NaN fails both comparisons.
static bool ratio_is_valid(f32 ratio) {
    return ratio >= FL_DOCK_MIN_RATIO && ratio <= 1.0f - FL_DOCK_MIN_RATIO;
}

static bool split_is_valid(const FlDockTree* tree, const FlDockNode* split) {
    if (!has_two_distinct_children(split)) {
        return false;
    }
    if (!axis_is_valid(split->split.axis)) {
        return false;
    }
    if (!ratio_is_valid(split->split.ratio)) {
        return false;
    }
    return check_node_rec(tree, split->split.child[0], split) && check_node_rec(tree, split->split.child[1], split);
}

static bool check_node_rec(const FlDockTree* tree, const FlDockNode* node, const FlDockNode* expected_parent) {
    if (node == nullptr || node->parent != expected_parent) {
        return false;
    }
    if (node->kind == FlDockNodeKind_Leaf) {
        return leaf_is_valid(tree, node);
    }
    if (node->kind == FlDockNodeKind_Split) {
        return split_is_valid(tree, node);
    }
    return false; // kind is neither leaf nor split
}

static bool ids_are_distinct(const FlDockPanelId* ids, u32 count) {
    for_count(i, count) {
        for (u32 j = i + 1; j < count; ++j) {
            if (ids[i] == ids[j]) {
                return false;
            }
        }
    }
    return true;
}

// Panel-id uniqueness across the whole universe (leaves ∪ floating).
static bool panels_are_unique(const FlDockTree* tree) {
    u32 total = dock_tree_collect_panels(tree, nullptr, 0);
    if (total == 0) {
        return true;
    }
    FlTempArena scratch = fl_arena_scratch_begin_conflict(tree->arena);
    FlDockPanelId* ids = arena_alloc_array(scratch.arena, FlDockPanelId, total);
    u32 got = dock_tree_collect_panels(tree, ids, total);
    bool unique = got == total && ids_are_distinct(ids, total);
    fl_arena_scratch_end(scratch);
    return unique;
}

// check_node_rec is handed nullptr as the root's expected parent, so it also rejects a root with a parent.
bool dock_tree_check_invariants(const FlDockTree* tree) {
    if (tree == nullptr || tree->root == nullptr) {
        return false;
    }
    return check_node_rec(tree, tree->root, nullptr) && panels_are_unique(tree);
}
