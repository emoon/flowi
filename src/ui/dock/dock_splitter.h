#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dock Splitter - the draggable divider between a split node's two children (CONTEXT.md "Dock Tree / Dock Node").
// A thin handle on the seam that, when dragged, mutates the split's single ratio scalar through the
// splitter-resize request op - never a direct geometry write, since Clay owns every rect. A min-size floor
// (FL_DOCK_MIN_RATIO, enforced by the op) keeps either child from collapsing.
//
// Two seams, behaviour then draw, so the new ratio takes effect the same frame:
//   - dock_splitter_handle_drag: applies the splitter-resize op. Call BEFORE reading the ratio to emit the
//     children.
//   - dock_splitter_emit: draws the divider element. Call between the two child emits so it sits on the boundary.
// Both derive the divider's element id from the split node id, so the behaviour and draw address one element.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "dock_tree.h"

#ifdef __cplusplus
extern "C" {
#endif

// Thickness (logical px) of the divider strip that fills a split's seam - also its grab area.
#define FL_DOCK_DIVIDER_THICKNESS 6.0f

// A split spans extent_px along its axis and the divider is dragged to cursor_rel_px (the cursor's offset from
// the split's leading edge along that axis). The new child[0] fraction is the clamped cursor_rel_px / extent_px,
// kept inside the min-size floor [FL_DOCK_MIN_RATIO, 1 - FL_DOCK_MIN_RATIO] so neither child collapses to zero. A
// non-positive extent_px (no geometry yet) yields the centred 0.5 fallback, itself clamped.
f32 dock_splitter_ratio_at(f32 cursor_rel_px, f32 extent_px);

// Behaviour pass for the divider on split node split_id. While dragging, maps the cursor into the split's
// last-frame rect and applies a splitter-resize request to tree. Also requests a resize cursor while
// hovered/dragging. Call inside a UI frame, before emitting the split's children, so the updated ratio drives this
// frame's layout.
void dock_splitter_handle_drag(FlDockTree* tree, FlDockNodeId split_id, FlDockAxis axis);

// Draw pass: emit the divider element filling the seam of split node split_id (a fixed-thickness strip across
// the cross axis). Call between the two child emits so it lands on the boundary.
void dock_splitter_emit(FlDockNodeId split_id, FlDockAxis axis);

#ifdef __cplusplus
}
#endif
