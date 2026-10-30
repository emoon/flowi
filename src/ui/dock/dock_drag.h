#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dock Drag - the drag-to-dock targeting experience (CONTEXT.md "Docking"). The layout makes space where the panel
// would land: the drop target is an empty slot the provisional layout opens, and on release the resolved request is
// committed into the committed tree. The commit (and the undock that detaches a dragged tab into the floating set)
// lands at one safe point, never mid-walk.
//
// The floating set is a transient holding pen for the duration of a gesture, never a resting state:
// every exit - commit, cancel, release over no zone - puts the panel back in a leaf.
//
// Zone model: the NEAREST leaf owns the cursor. Its tab strip and a box in the middle of the tile tab the panel in -
// the strip picks a slot in the tab order, the centre appends; every other point of the tile picks a split side
// by which corner-diagonal quadrant it falls in; a band along the workspace border docks outer. None means
// outside the workspace and nothing else.
//
// Coordinate space: LOGICAL px throughout - the hit-test buffer divides the physical Clay boxes by ui_get_scale(),
// so every constant below is logical px.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "dock_layout.h"
#include "dock_tree.h"

#include <flowi/core/math_data.h>

#ifdef __cplusplus
extern "C" {
#endif

// Resolution geometry (logical px, except the fraction), pinned by the make-space prototype. The tab-into region is
// the strip plus a centred box covering FL_DOCK_TAB_CENTER_FRAC of the tile on each axis.
#define FL_DOCK_TAB_STRIP 26.0f
#define FL_DOCK_TAB_CENTER_FRAC 0.40f
#define FL_DOCK_OUTER_BAND 24.0f

// The insertion slot a centre drop names: past the end of any strip, so the tree clamps it to the back.
#define FL_DOCK_TAB_INDEX_END 0xffffffffu

#define FL_DOCK_DRAG_HYSTERESIS 8.0f

// The share of the split the gap takes, carried on the request so the committed split inherits it.
#define FL_DOCK_GAP_RATIO 0.35f

// Movement the cursor must have travelled from the press point before a reported tab grab becomes an undock.
#define FL_DOCK_DRAG_THRESHOLD 5.0f

// The zone the cursor resolved to; None means outside the workspace. Center docks as a tab into the nearest
// leaf, the four sides dock-split it, and the Outer* zones dock-split the workspace root. Each non-None value maps
// onto exactly one FlDockRequest kind + side. OuterTop is unreachable while the tab strip beats the outer band -
// the workspace's top edge is always some leaf's strip.
typedef enum FlDockDropZone {
    FlDockDropZone_None = 0,
    FlDockDropZone_Center,
    FlDockDropZone_Left,
    FlDockDropZone_Right,
    FlDockDropZone_Top,
    FlDockDropZone_Bottom,
    FlDockDropZone_OuterLeft,
    FlDockDropZone_OuterRight,
    FlDockDropZone_OuterTop,
    FlDockDropZone_OuterBottom,
} FlDockDropZone;

// One candidate node for the hit-test. is_leaf nodes are the split/tab targets; the single is_root node's rect
// bounds the workspace and seeds the outer band. A root that is itself a leaf carries both flags (a one-leaf tree).
typedef struct FlDockDropNode {
    FlDockNodeId node;
    FlDockRect rect;
    bool is_leaf;
    bool is_root;
} FlDockDropNode;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Decide which drop zone cursor falls in over the laid-out nodes. Resolution is nearest leaf, not containing
// leaf, so a cursor in a splitter gutter still has an answer; only a cursor outside the root rect returns None.
// Precedence: the nearest leaf's tab region beats the workspace outer band, which beats that leaf's split quadrants.
// Returns the zone and, when not None, writes the request that zone raises into out_req: kind + target_node +
// side (+ to_index for a tab), with panel left 0 for the caller to fill. out_req is always cleared first, so
// a None return leaves it zeroed.
//
// The dock-family gate is enforced here at drop-resolution: accept_filter is the workspace's accepted-family
// filter (0 = accept any) and panel_family the dragged panel's tag (0 = general). When the filter rejects the
// family, NO zone is offered - so an incompatible panel never highlights a target and never previews.
FlDockDropZone hit_test_drop_zones(const FlDockDropNode* nodes, u32 count, FlVec2 cursor, u32 panel_family,
                                   u32 accept_filter, FlDockRequest* out_req);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Drag controller. Holds the in-flight drag: the dragged panel, the geometry the hit-test resolves against, the
// retained preview tree that carries the gap, and the last resolved zone/request.
//
// The hit-test runs on the LIVE provisional layout - the preview tiles the gap is itself opening in. Two rules
// keep that feedback loop stable:
//
//   - sticky gap: a cursor inside the gap may push the gap around the node it opened in, but may not hand it to
//     another node. The gap is also never a hit-test candidate.
//   - pre-gap footprint: the node the gap took its space from is hit-tested at the rect it held BEFORE the gap
//     opened.

#define FL_DOCK_DRAG_MAX_NODES 256

typedef struct FlDockDrag {
    bool active;
    FlDockPanelId panel;
    // Dock-family gate captured at drag begin (both 0 = the accept-anywhere default).
    u32 panel_family;
    u32 accept_filter;
    // Hit-test candidates, refilled every dock_drag_emit from the layout the previous frame computed. The gap leaf
    // is never a candidate; the pre-gap footprint is substituted for the node the gap split off.
    FlDockDropNode base[FL_DOCK_DRAG_MAX_NODES];
    u32 base_count;
    // Frames dock_drag_emit must let pass before the buffer is read; the undock grab reflows the committed tree.
    u32 settle_frames;
    // Last resolved target, refreshed each dock_drag_emit.
    FlDockDropZone zone;
    FlDockRequest request;
    // Where the hit-test last agreed with zone. dock_drag_resolve measures the switch hysteresis from here, so a
    // held target survives until the cursor is FL_DOCK_DRAG_HYSTERESIS px past the boundary it disagreed at.
    FlVec2 hyst_anchor;
    // The leaf the panel was pulled out of, captured before the undock reflowed the tree; a release over no zone
    // puts the panel back there. Zero when the drag began on an already-loose panel - nothing to go back to.
    FlDockNodeId origin_leaf;

    // The retained preview and the gap it carries. committed is cloned once at the first resolved zone and then
    // mutated in place, so a re-target closes the old gap (undock) while the new one opens (the request). The tree is
    // owned here; destroyed when the drag ends.
    FlDockRequest applied; // the request currently standing in preview
    FlDockRect gap_rect;   // the gap's live rect, used by the sticky-gap rule; zero-sized when unknown
    FlDockRect pre_gap;    // the rect pre_gap_node filled before the gap opened
    FlDockTree* preview;   // null until the first resolved zone clones it
    FlDockDropZone applied_zone;
    FlDockNodeId gap_leaf;     // the leaf holding the dragged panel in preview, 0 when nothing is applied
    FlDockNodeId pre_gap_node; // the node the gap split off, hit-tested at pre_gap while the gap stands
    bool gap_is_new;           // the gap is a leaf the request created, not an existing leaf gaining a tab
    bool has_gap_rect;
    bool has_pre_gap;
} FlDockDrag;

// Begin the "drag a tab out of its leaf" gesture: first UNDOCK panel into the internal-floating set, then arm the
// drag so the now-floating panel can be re-docked through the same overlay. The undock mutates committed at the
// safe input-handling point, before any per-frame emit walk. Undocking a panel that is not currently docked is a
// no-op on the tree; the drag is armed regardless so the grab still tracks the panel. Call from a UI frame.
void dock_drag_begin_undock(FlDockDrag* drag, FlDockTree* committed, FlDockPanelId panel, u32 panel_family,
                            u32 accept_filter);

// Resolve cursor (logical px) against the drag's hit-test buffer and latch the result into `drag->zone` /
// `drag->request` (with panel filled in). Applies the sticky-gap rule and then the switch hysteresis: a held
// target survives a disagreeing resolution until the cursor has travelled FL_DOCK_DRAG_HYSTERESIS px from where the
// disagreement started. A tab region hit outside the gap takes effect immediately - the hysteresis must never
// put tab-into out of reach. Returns the held zone.
FlDockDropZone dock_drag_resolve(FlDockDrag* drag, FlVec2 cursor);

// End the drag and COMMIT the drop. If a valid drop zone was resolved this drag (zone != None), apply its request
// to committed so the reserved preview slot becomes a real leaf. A release over no zone re-seats a panel this
// drag undocked, else discards with no structural change. The commit lands here, at the safe input-handling point
// outside any emit walk. Returns true iff committed was mutated. Always clears the controller.
bool dock_drag_end(FlDockDrag* drag, FlDockTree* committed);

// Cancel the drag: end it with no drop applied, so the panel returns to the seat it was pulled from. Escape and a
// release outside the workspace both take this path. Returns true iff committed was mutated. Always clears the
// controller.
bool dock_drag_cancel(FlDockDrag* drag, FlDockTree* committed);

bool dock_drag_active(const FlDockDrag* drag);

// Drop an in-flight drag without applying anything and without touching a tree - the teardown path, where the
// committed tree is about to go away too. Frees the retained preview.
void dock_drag_discard(FlDockDrag* drag);

// Per-frame drive while a drag is active: refresh the hit-test buffer from the layout the previous frame computed,
// resolve the cursor against it, bring the retained preview onto the resolved target, and emit it. committed is
// cloned once for the preview and never mutated. Call inside a UI frame. No-op if not active.
void dock_drag_emit(FlDockDrag* drag, FlDockTree* committed);

#ifdef __cplusplus
}
#endif
