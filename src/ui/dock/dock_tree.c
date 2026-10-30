///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dock Tree - retained logical dock tree + pure operations (see dock_tree.h).
//
// Topology + ratios only; no pixel geometry. Every node lives in the tree's arena and is never individually
// freed - collapsed/dissolved nodes simply become unreachable (the arena is torn down wholesale on destroy).
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "dock_tree.h"

#include <core/arena.h>

#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Small helpers

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void leaf_reserve(FlDockTree* tree, FlDockNode* leaf, u32 need) {
    leaf->leaf.panels = (FlDockPanelId*)dock_arena_array_reserve(tree->arena, leaf->leaf.panels, leaf->leaf.count,
                                                                 &leaf->leaf.cap, sizeof(FlDockPanelId), need);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void dock_tree_leaf_append(FlDockTree* tree, FlDockNode* leaf, FlDockPanelId panel) {
    leaf_reserve(tree, leaf, leaf->leaf.count + 1);
    leaf->leaf.panels[leaf->leaf.count++] = panel;
}

static u32 leaf_index_of(const FlDockNode* leaf, FlDockPanelId panel) {
    for (u32 i = 0; i < leaf->leaf.count; ++i) {
        if (leaf->leaf.panels[i] == panel) {
            return i;
        }
    }
    return UINT32_MAX;
}

// Remove the panel at idx from leaf, fixing the selected-tab index. Does NOT collapse an emptied leaf - that
// is the caller's concern.
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void floating_reserve(FlDockTree* tree, u32 need) {
    tree->floating = (FlDockPanelId*)dock_arena_array_reserve(tree->arena, tree->floating, tree->floating_count,
                                                              &tree->floating_cap, sizeof(FlDockPanelId), need);
}

static u32 floating_index_of(const FlDockTree* tree, FlDockPanelId panel) {
    for (u32 i = 0; i < tree->floating_count; ++i) {
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void dock_tree_floating_append(FlDockTree* tree, FlDockPanelId panel) {
    floating_reserve(tree, tree->floating_count + 1);
    tree->floating[tree->floating_count++] = panel;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Collapse: an emptied non-root leaf dissolves its single-child split; the sibling absorbs the slot. The root is
// exempt (it degrades to an empty leaf and stays). leaf must be empty.

static void collapse_empty_leaf(FlDockTree* tree, FlDockNode* leaf) {
    if (leaf == tree->root) {
        return; // root is the floor - stays an empty leaf
    }

    FlDockNode* parent = leaf->parent; // a split with exactly two children, one of which is leaf
    bool leaf_is_first = parent->split.child[0] == leaf;
    FlDockNode* sibling = leaf_is_first ? parent->split.child[1] : parent->split.child[0];
    FlDockNode* grand = parent->parent;

    sibling->parent = grand;
    if (grand == nullptr) {
        tree->root = sibling; // the dissolved split was the root
    } else if (grand->split.child[0] == parent) {
        grand->split.child[0] = sibling;
    } else {
        grand->split.child[1] = sibling;
    }
}

// Detach panel from wherever it currently lives (a leaf or the floating set), collapsing an emptied non-root
// leaf. Returns true if the panel was found and removed. Every op that re-places a panel detaches first, so a dock
// of an already-present panel is a move, never a duplicate.
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Split insertion: replace target (anywhere in the tree, including the root) with a new split whose other child
// is a fresh leaf holding panel, placed on side.

static void insert_split_at(FlDockTree* tree, FlDockNode* target, FlDockPanelId panel, FlDockSide side, f32 new_share) {
    FlDockNode* new_leaf = node_new_leaf(tree);
    dock_tree_leaf_append(tree, new_leaf, panel);

    // child[0] is the left/top side: the new leaf takes the named side, the target the other.
    bool new_is_first = (side == FlDockSide_Left || side == FlDockSide_Top);
    f32 share = clamp_ratio(new_share > 0.0f ? new_share : FL_DOCK_SPLIT_RATIO_DEFAULT);

    FlDockNode* split = arena_alloc_zero(tree->arena, FlDockNode);
    split->kind = FlDockNodeKind_Split;
    split->id = tree->next_node_id++;
    split->split.axis = (side == FlDockSide_Left || side == FlDockSide_Right) ? FlDockAxis_X : FlDockAxis_Y;
    split->split.ratio = new_is_first ? share : 1.0f - share;

    split->split.child[0] = new_is_first ? new_leaf : target;
    split->split.child[1] = new_is_first ? target : new_leaf;

    FlDockNode* grand = target->parent;
    split->parent = grand;
    new_leaf->parent = split;
    target->parent = split;

    if (grand == nullptr) {
        tree->root = split;
    } else if (grand->split.child[0] == target) {
        grand->split.child[0] = split;
    } else {
        grand->split.child[1] = split;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Operations

// Dock an already-detached panel onto the root: into the root leaf if it is empty, else as a fresh split off the
// root. The safety net for a dock whose target dissolved when the panel left it - the panel is re-homed, not lost.
static void rehome_to_root(FlDockTree* tree, FlDockPanelId panel) {
    if (tree->root->kind == FlDockNodeKind_Leaf && tree->root->leaf.count == 0) {
        dock_tree_leaf_append(tree, tree->root, panel);
        tree->root->leaf.selected = 0;
    } else {
        insert_split_at(tree, tree->root, panel, FlDockSide_Right, FL_DOCK_SPLIT_RATIO_DEFAULT);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static bool apply_tab(FlDockTree* tree, const FlDockRequest* req) {
    FlDockNode* target = find_node_rec(tree->root, req->target_node);
    if (target == nullptr || target->kind != FlDockNodeKind_Leaf) {
        return false;
    }

    u32 existing = leaf_index_of(target, req->panel);
    if (existing != UINT32_MAX) {
        target->leaf.selected = existing; // already here - re-select it
        return true;
    }

    detach_panel(tree, req->panel);                       // move (or no-op for a brand-new panel)
    target = find_node_rec(tree->root, req->target_node); // detach may have reshaped the tree
    if (target != nullptr && target->kind == FlDockNodeKind_Leaf) {
        dock_tree_leaf_append(tree, target, req->panel);
        target->leaf.selected = target->leaf.count - 1;
        return true;
    }
    rehome_to_root(tree, req->panel); // target dissolved with the detach - never drop the panel
    return true;
}

// Shared body for dock-split (target = a node id) and dock-outer (target = the root). Splitting an EMPTY leaf
// would create an empty non-root sibling, so it degenerates to dock-as-tab into that leaf - which is how the
// first panel lands in a fresh tree.
static bool apply_split_against(FlDockTree* tree, FlDockNode* target, FlDockPanelId panel, FlDockSide side,
                                f32 new_share) {
    FlDockNodeId target_id = target->id;

    // Splitting a leaf whose sole content already IS this panel is a no-op: detaching first would collapse the
    // target out from under it.
    if (target->kind == FlDockNodeKind_Leaf && target->leaf.count == 1 && target->leaf.panels[0] == panel) {
        return true;
    }

    detach_panel(tree, panel);
    target = find_node_rec(tree->root, target_id); // detach may have reshaped (or dissolved) the target
    if (target == nullptr) {
        rehome_to_root(tree, panel); // intended target dissolved when the panel left - re-home, never drop it
        return true;
    }

    if (target->kind == FlDockNodeKind_Leaf && target->leaf.count == 0) {
        dock_tree_leaf_append(tree, target, panel);
        target->leaf.selected = 0;
        return true;
    }

    insert_split_at(tree, target, panel, side, new_share);
    return true;
}

static bool apply_split(FlDockTree* tree, const FlDockRequest* req) {
    FlDockNode* target = find_node_rec(tree->root, req->target_node);
    if (target == nullptr) {
        return false;
    }
    return apply_split_against(tree, target, req->panel, req->side, req->ratio);
}

static bool apply_outer(FlDockTree* tree, const FlDockRequest* req) {
    return apply_split_against(tree, tree->root, req->panel, req->side, req->ratio);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static bool apply_undock(FlDockTree* tree, const FlDockRequest* req) {
    FlDockNode* leaf = find_leaf_with_panel_rec(tree->root, req->panel);
    if (leaf == nullptr) {
        return false; // not docked (already floating, or absent) - nothing to undock
    }
    detach_panel(tree, req->panel);
    dock_tree_floating_append(tree, req->panel);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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
    FlDockPanelId moved = leaf->leaf.panels[from];
    if (from < to) {
        for (u32 i = from; i < to; ++i) {
            leaf->leaf.panels[i] = leaf->leaf.panels[i + 1];
        }
    } else {
        for (u32 i = from; i > to; --i) {
            leaf->leaf.panels[i] = leaf->leaf.panels[i - 1];
        }
    }
    leaf->leaf.panels[to] = moved;
    leaf->leaf.selected = leaf_index_of(leaf, selected_id); // selection follows the panel, not the slot
    return true;
}

static bool apply_close(FlDockTree* tree, const FlDockRequest* req) {
    return detach_panel(tree, req->panel); // collapses an emptied leaf; floating panels just vanish
}

static bool apply_splitter_resize(FlDockTree* tree, const FlDockRequest* req) {
    FlDockNode* node = find_node_rec(tree->root, req->target_node);
    if (node == nullptr || node->kind != FlDockNodeKind_Split) {
        return false;
    }
    node->split.ratio = clamp_ratio(req->ratio);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Public API

FlDockTree* dock_tree_create(void) {
    FlArena* arena = arena_new();
    FlDockTree* tree = arena_alloc_zero(arena, FlDockTree);
    tree->arena = arena;
    tree->next_node_id = 1; // 0 is reserved as "no node"
    tree->root = node_new_leaf(tree);
    return tree;
}

void dock_tree_destroy(FlDockTree* tree) {
    if (tree != nullptr) {
        fl_arena_destroy(tree->arena); // the tree handle itself lives in this arena
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
        node->leaf.cap = src->leaf.count; // tighten to fit
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
    tree->next_node_id = src->next_node_id; // preserve the allocator so cloned + future ids match the original
    tree->root = clone_node_rec(arena, src->root, nullptr);

    if (src->floating_count > 0) {
        tree->floating_cap = src->floating_count;
        tree->floating = arena_alloc_array(arena, FlDockPanelId, tree->floating_cap);
        memcpy(tree->floating, src->floating, src->floating_count * sizeof(FlDockPanelId));
        tree->floating_count = src->floating_count;
    }
    return tree;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Builds whatever the spec describes, holes included; dock_tree_build validates the whole result afterward. A null
// spec child yields a null node, which the invariant checker then rejects as a missing-child split.
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
    for (u32 i = 0; i < spec->panel_count; ++i) {
        dock_tree_leaf_append(tree, leaf, spec->panels[i]);
    }
    if (spec->panel_count > 0) {
        leaf->leaf.selected = spec->selected < spec->panel_count ? spec->selected : 0;
    }
    return leaf;
}

FlDockTree* dock_tree_build(const FlDockSpec* spec) {
    FlDockTree* tree = dock_tree_create(); // starts as the floor; a null spec keeps it
    if (spec != nullptr) {
        tree->root = build_spec_rec(tree, spec, nullptr); // the original empty-leaf root leaks into the arena
    }
    if (!dock_tree_check_invariants(tree)) {
        dock_tree_destroy(tree); // a malformed literal - never hand back a holed tree
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool dock_tree_has_panel(const FlDockTree* tree, FlDockPanelId panel) {
    return dock_tree_find_leaf_with_panel(tree, panel) != nullptr || dock_tree_panel_is_floating(tree, panel);
}

static u32 collect_rec(const FlDockNode* node, FlDockPanelId* out, u32 max, u32 written) {
    if (node == nullptr) {
        return written;
    }
    if (node->kind == FlDockNodeKind_Leaf) {
        for (u32 i = 0; i < node->leaf.count; ++i) {
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
    for (u32 i = 0; i < tree->floating_count; ++i) {
        if (out != nullptr && written < max) {
            out[written] = tree->floating[i];
        }
        written++;
    }
    return written;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Invariant checker - pure predicate, no asserts (safe in a tight fuzz loop).

static bool check_node_rec(const FlDockTree* tree, const FlDockNode* node, const FlDockNode* expected_parent) {
    if (node == nullptr) {
        return false;
    }
    if (node->parent != expected_parent) {
        return false;
    }

    if (node->kind == FlDockNodeKind_Leaf) {
        if (node->leaf.count == 0) {
            return node == tree->root; // only the root may be an empty leaf
        }
        if (node->leaf.selected >= node->leaf.count) {
            return false; // selected tab must be a member
        }
        return true;
    }

    if (node->kind == FlDockNodeKind_Split) {
        FlDockNode* a = node->split.child[0];
        FlDockNode* b = node->split.child[1];
        if (a == nullptr || b == nullptr || a == b) {
            return false; // exactly two distinct children - no single-child splits
        }
        if (node->split.axis != FlDockAxis_X && node->split.axis != FlDockAxis_Y) {
            return false;
        }
        if (!(node->split.ratio >= FL_DOCK_MIN_RATIO && node->split.ratio <= 1.0f - FL_DOCK_MIN_RATIO)) {
            return false; // ratio clamped inside (0,1) by the min-size floor
        }
        return check_node_rec(tree, a, node) && check_node_rec(tree, b, node);
    }

    return false; // kind is neither leaf nor split
}

bool dock_tree_check_invariants(const FlDockTree* tree) {
    if (tree == nullptr || tree->root == nullptr) {
        return false;
    }
    if (tree->root->parent != nullptr) {
        return false;
    }
    if (!check_node_rec(tree, tree->root, nullptr)) {
        return false;
    }

    // Panel-id uniqueness across the whole universe (leaves ∪ floating).
    u32 total = dock_tree_collect_panels(tree, nullptr, 0);
    if (total == 0) {
        return true;
    }

    FlTempArena scratch = fl_arena_scratch_begin_conflict(tree->arena);
    FlDockPanelId* ids = arena_alloc_array(scratch.arena, FlDockPanelId, total);
    u32 got = dock_tree_collect_panels(tree, ids, total);
    bool unique = (got == total);
    for (u32 i = 0; unique && i < total; ++i) {
        for (u32 j = i + 1; j < total; ++j) {
            if (ids[i] == ids[j]) {
                unique = false;
                break;
            }
        }
    }
    fl_arena_scratch_end(scratch);
    return unique;
}
