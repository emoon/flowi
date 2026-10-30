#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dock Layout - turn a logical dock tree into a laid-out region by emitting Clay containers and reading the
// computed geometry back (CONTEXT.md "Docking"). The docking layer stores topology + ratios only; Clay owns and
// computes every pixel rect. Every node carries a stable Clay element id, so after the layout pass each node's
// computed rect is read back by id into a node→rect map for later hit-testing.
//
// One-frame ordering: a node's computed rect is only valid AFTER a layout pass. Call dock_layout_emit inside
// the UI frame (between ui_update_begin and ui_update_end); call dock_layout_read_rects once that frame's
// layout has been computed (after ui_update_end).
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "dock_tree.h"

#ifdef __cplusplus
extern "C" {
#endif

// A node's computed pixel rectangle within the laid-out region.
typedef struct FlDockRect {
    f32 x, y, width, height;
} FlDockRect;

// found is false when Clay had no geometry for the node this frame (read before a layout pass, or the node was
// not emitted).
typedef struct FlDockNodeRect {
    FlDockNodeId node;
    FlDockRect rect;
    bool found;
} FlDockNodeRect;

// The node→rect map: one entry per tree node, in the same pre-order dock_layout_emit walks (root first).
typedef struct FlDockLayout {
    FlDockNodeRect* nodes;
    u32 count;
} FlDockLayout;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The UI scale (physical px per logical px), never zero.
f32 dock_ui_scale(void);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The width band a dock tab lays out in, in reference pixels: the min keeps a blank tab hittable, the max caps
// a long title, which elides into it. They bound the tabs of a SHARED header - a lone tab stretches to the bar
// instead, capped by nothing. Dock-only; other ui_tab_strip callers stay unbounded.
#define DOCK_TAB_MIN_WIDTH 56.0f
#define DOCK_TAB_MAX_WIDTH 180.0f

// The height of a leaf's tab header, in reference pixels.
#define DOCK_TAB_HEIGHT 32.0f

// The floor under a tile, in logical px - the whole tile, and the content area below its tab header. The floor is
// FLAT PER SPLIT: a split holds it for its two direct children only, never for the subtree under them, so a chain
// of N nested splits still starves the deepest tile once the window drops below roughly N times one split's floor.
#define FL_DOCK_MIN_TILE_WIDTH 120.0f
#define FL_DOCK_MIN_TILE_HEIGHT 80.0f
#define FL_DOCK_MIN_CONTENT_WIDTH 80.0f
#define FL_DOCK_MIN_CONTENT_HEIGHT 40.0f

// ratio clamped so neither child of a split of pixel extent extent_px along axis falls below the minimum
// tile extent for that axis (the divider strip comes out of child[1]'s share). Under twice that floor plus the
// divider the clamp has no solution: both children then take half the remaining room, so they shrink together,
// and out_starved (optional) is set. An extent_px of 0 - no geometry read back yet - returns ratio as is.
// Stateless: reads the UI scale and the extent handed in, writes nothing.
f32 dock_layout_clamp_ratio(f32 ratio, FlDockAxis axis, f32 extent_px, bool* out_starved);

// ratio rounded so the edge it puts child[0]'s far side on lands on a whole physical pixel of a split extent_px
// wide. An extent_px of 0 returns ratio as is.
f32 dock_layout_snap_ratio(f32 ratio, f32 extent_px);

// The Clay element id a node is emitted under and read back by. Stable for a given node id within a tree.
u32 dock_layout_node_element_id(FlDockNodeId node);

// The Clay element id of a leaf's CONTENT area - the region under its tab header, which is what a declared
// panel's body attaches to. Distinct from the node id, which covers the whole tile.
u32 dock_layout_content_element_id(FlDockNodeId node);

// Walk tree and emit nested Clay containers mirroring its topology: a split becomes a directional container
// (LeftToRight for an X split, TopToBottom for Y) whose first child takes sizing_percent(ratio), a draggable
// divider fills the seam, and the second child grows into the remainder; a non-empty leaf gets a tab header over a
// content area, else a bare placeholder. Call inside a UI frame (between ui_update_begin and ui_update_end). A
// null/empty tree emits nothing.
//
// The tree is non-const: a divider drag applies the splitter-resize op and a tab click the dock-as-tab (re-select)
// op, both through dock_tree_apply. Those are the only tree writes.
//
// out_grab (optional) reports the pull-out gesture: when a tab is dragged out of its header, the panel id is
// written there and NOTHING is mutated - detaching a panel is structural (a leaf can collapse), so it must not
// happen mid-walk. The caller starts the drag at its own safe point. Written only when a grab is seen; the caller
// clears it. Pass null to emit without the gesture, which is what a preview walk does.
void dock_layout_emit(FlDockTree* tree, FlDockPanelId* out_grab);

// After the layout pass, read every node's computed rect back by id into out, allocating the map from arena.
// Only meaningful once the frame's layout has been computed (see the one-frame ordering note above). A null/empty
// tree yields an empty map.
void dock_layout_read_rects(const FlDockTree* tree, FlArena* arena, FlDockLayout* out);

// Look up a node's rect in a populated layout, or null if the node is absent or had no geometry this frame.
const FlDockRect* dock_layout_find_rect(const FlDockLayout* layout, FlDockNodeId node);

#ifdef __cplusplus
}
#endif
