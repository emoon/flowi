#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dock Tree - the retained logical dock tree and the pure operations over it (CONTEXT.md "Docking",
// "Dock Tree / Dock Node", "Workspace").
//
// Topology and ratios only: no rendering, no pixel geometry. Clay owns every rect.
//
// Mutation is one deferred request applied in place by dock_tree_apply. Every successful apply leaves the tree
// invariant-clean, as dock_tree_check_invariants defines clean, which makes every possible "hole" topological.
//
// Panel-id conservation law: the panel universe is (every leaf's panels) ∪ (the floating set).
// FlDockRequest_Close is the only request that removes a panel from it; every other request conserves it.
//
// The struct layout is public and the render layer walks it each frame; dock_tree_apply is the only sanctioned
// mutator.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <core/types.h>
#include <flowi/ui/dock.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FlArena FlArena;

// Stable id of a Detachable Panel (location-independent widget id; identity only, never interpreted as data).
typedef u64 FlDockPanelId;

// Monotonic id of a node within one tree, assigned at node creation. Requests address nodes by id, never by
// pointer, and node rects are read back by it.
typedef u32 FlDockNodeId;

// The minimum fraction either side of a split may occupy - the "min-size floor". Ratios are clamped into
// [FL_DOCK_MIN_RATIO, 1 - FL_DOCK_MIN_RATIO], keeping them strictly inside (0,1).
#define FL_DOCK_MIN_RATIO 0.05f

// The share of the split a dock-split's new leaf takes when the request names no ratio of its own.
#define FL_DOCK_SPLIT_RATIO_DEFAULT 0.5f

typedef enum FlDockNodeKind {
    FlDockNodeKind_Leaf = 0, // a tab group of panel ids
    FlDockNodeKind_Split,    // two children divided along an axis
} FlDockNodeKind;

// Split axis. X divides left|right (a vertical divider); Y divides top/bottom (a horizontal divider). child[0] is
// the left/top side, child[1] the right/bottom; ratio is the fraction occupied by child[0].
typedef enum FlDockAxis {
    FlDockAxis_X = 0,
    FlDockAxis_Y,
} FlDockAxis;

typedef enum FlDockRequestKind {
    FlDockRequest_Tab = 0,       // dock-as-tab: add/move panel into the leaf target_node.
    FlDockRequest_Split,         // dock-split: split target_node, panel's leaf taking side.
    FlDockRequest_Outer,         // dock-outer: split the root, panel's leaf taking the outer side.
    FlDockRequest_Undock,        // undock: move panel from its leaf into the floating set.
    FlDockRequest_ReorderTab,    // reorder-tab: move panel to to_index within its leaf.
    FlDockRequest_Close,         // close: remove panel from the universe.
    FlDockRequest_SplitterResize // splitter-resize: set split target_node's ratio to ratio.
} FlDockRequestKind;

typedef struct FlDockNode FlDockNode;
struct FlDockNode {
    FlDockNodeKind kind;
    FlDockNodeId id;
    FlDockNode* parent; // null only for the root
    union {
        struct {
            FlDockAxis axis;
            f32 ratio; // logical fraction occupied by child[0]; in [FL_DOCK_MIN_RATIO, 1 - FL_DOCK_MIN_RATIO]
            // Set once the error reporting this split as too small for the tile floor has been emitted; cleared when
            // the room comes back.
            bool starve_logged;
            FlDockNode* child[2];
        } split;
        struct {
            FlDockPanelId* panels; // arena-backed, grows by doubling
            u32 count;
            u32 cap;
            u32 selected; // index in [0,count) when count>0; 0 when the (root-only) leaf is empty
        } leaf;
    };
};

typedef struct FlDockTree {
    FlArena* arena;          // owns the root hierarchy, every node, and the panel arrays
    FlDockNode* root;        // never null; the only collapse-exempt node (degrades to an empty leaf)
    FlDockPanelId* floating; // undocked-but-loose panels (internal-floating), arena-backed
    u32 floating_count;
    u32 floating_cap;
    FlDockNodeId next_node_id; // monotonic node-id allocator
} FlDockTree;

// A deferred structural-change request. Only the fields named for a given kind are read.
typedef struct FlDockRequest {
    FlDockRequestKind kind;
    FlDockPanelId panel;      // Tab, Split, Outer, Undock, ReorderTab, Close
    FlDockNodeId target_node; // Tab (must be a leaf), Split (node to split), SplitterResize (must be a split)
    FlDockSide side;          // Split, Outer
    u32 to_index;             // ReorderTab (clamped into the leaf's tab range)
    // SplitterResize: the split's new ratio. Split/Outer: the share the NEW leaf takes of the split it creates.
    // Clamped to the min-size floor; 0 means FL_DOCK_SPLIT_RATIO_DEFAULT.
    f32 ratio;
} FlDockRequest;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Lifecycle. A fresh tree's root is an empty leaf (the floor).

FlDockTree* dock_tree_create(void);
void dock_tree_destroy(FlDockTree* tree);

// Deep-copy src into a fresh, independently-owned tree (caller owns it: dock_tree_destroy). Reproduces topology,
// axes, ratios, every leaf's panels/selected, the floating set, and the node-id allocator, so node ids match the
// original (a request addressing a src node id applies identically to the clone, and the new node ids a
// subsequent dock_tree_apply mints match what src would have minted).
FlDockTree* dock_tree_clone(const FlDockTree* src);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Declarative first-run layout. FlDockSpec is one node of the literal: exactly a SPLIT (two child specs along an
// axis at a ratio) or a LEAF (a tab group of one-or-more panel ids). The dock_split / dock_leaf macros in
// ui/dock.h build these as nested compound literals; dock_tree_build lowers a spec onto a real tree.

typedef struct FlDockSpec FlDockSpec;
struct FlDockSpec {
    bool is_split;
    // Split (is_split == true): two children divided along axis; ratio is the fraction child[0] occupies
    // (clamped to the min-size floor, like every other ratio in the tree).
    FlDockAxis axis;
    f32 ratio;
    const FlDockSpec* child[2];
    // Leaf (is_split == false): a tab group of panel_count ids, with selected the initial tab (clamped into
    // range). A non-root leaf must carry at least one panel.
    const FlDockPanelId* panels;
    u32 panel_count;
    u32 selected;
};

// Lower a declarative layout spec onto a fresh dock tree, then run the result through
// dock_tree_check_invariants. Returns the built, invariant-clean tree (caller owns it: dock_tree_destroy), or
// null if the literal lowers to an invalid tree (a hole - duplicate panel id, empty non-root leaf, a missing
// split child). A null spec yields a fresh floor (a single empty root leaf).
FlDockTree* dock_tree_build(const FlDockSpec* spec);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mutation. Applies req in place; returns true if it changed the tree (or was an accepted no-op such as
// re-selecting a tab), false if the request was invalid (target missing/wrong kind, panel absent where one is
// required, etc.). On a true return the tree is invariant-clean.

bool dock_tree_apply(FlDockTree* tree, const FlDockRequest* req);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The invariant chokepoint. Returns true iff: every node is split-xor-leaf; every split has exactly two distinct
// non-null children with back-pointers and a ratio inside the min-size floor; no single-child splits; every leaf
// except the root is non-empty; each panel id appears in at most one leaf and never both docked and floating; the
// selected tab is a member of its leaf; and the root has no parent. Pure predicate - no asserts, safe to call in
// a tight fuzz loop.

bool dock_tree_check_invariants(const FlDockTree* tree);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Queries.

// The leaf currently holding panel, or null if panel is floating or absent.
FlDockNode* dock_tree_find_leaf_with_panel(const FlDockTree* tree, FlDockPanelId panel);

// The node with id id, or null if no such node is reachable from the root.
FlDockNode* dock_tree_find_node(const FlDockTree* tree, FlDockNodeId id);

// True if panel is in the floating set.
bool dock_tree_panel_is_floating(const FlDockTree* tree, FlDockPanelId panel);

// True if panel is docked in a leaf or in the floating set.
bool dock_tree_has_panel(const FlDockTree* tree, FlDockPanelId panel);

// Census of the whole panel universe (every leaf's panels ∪ the floating set) - the quantity the conservation law
// is stated over. Returns the total count; writes up to max ids into out (pass out=null/max=0 to just count).
u32 dock_tree_collect_panels(const FlDockTree* tree, FlDockPanelId* out, u32 max);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Arena-backed array growth, shared with the persistence layer's tree reconstruction.

// Ensure the arena array items (count live elements, capacity *cap) holds need elements, growing geometrically; the
// old array is abandoned in the arena. Returns the (possibly new) array and updates *cap.
void* dock_arena_array_reserve(FlArena* arena, void* items, u32 count, u32* cap, u32 elem_size, u32 need);

// Append panel to leaf's panel array / the tree's floating set, growing as needed. Does not check duplicates.
void dock_tree_leaf_append(FlDockTree* tree, FlDockNode* leaf, FlDockPanelId panel);
void dock_tree_floating_append(FlDockTree* tree, FlDockPanelId panel);

#ifdef __cplusplus
}
#endif
