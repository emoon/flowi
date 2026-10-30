// Dock Splitter - the draggable divider that mutates a split's ratio through the splitter-resize op. The tree owns
// topology + the one ratio scalar; Clay owns every rect.

#include "dock_splitter.h"

#include "dock_layout.h"
#include <core/math.h>
#include "../layout.h"
#include "../ui.h"
#include "../ui_private.h"
#include <flowi/ui/cursor.h>
#include <external/clay/clay.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Both the behaviour pass and the draw pass derive the divider's element id from the split node id, so they
// address one element by construction.

static FlLayoutId divider_layout_id(FlDockNodeId split_id) {
    return ID_INT("fl_dock_divider", (i32)split_id);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

f32 dock_splitter_ratio_at(f32 cursor_rel_px, f32 extent_px) {
    f32 r = (extent_px > 0.0f) ? cursor_rel_px / extent_px : 0.5f;
    return clamp_f32_impl(r, FL_DOCK_MIN_RATIO, 1.0f - FL_DOCK_MIN_RATIO);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void dock_splitter_handle_drag(FlDockTree* tree, FlDockNodeId split_id, FlDockAxis axis) {
    FlUiCtx* self = g_ui_ctx;

    // draggable yields hover and the drag latch; FlItem_DragThreshold ignores sub-threshold movement.
    FlSignal sig = ui_item_behavior(divider_layout_id(split_id), FlItemFlags_draggable | FlItem_DragThreshold,
                                    (FlWidgetConfig) { 0 });

    if (sig.hovered || sig.dragging) {
        fl_cursor_set((FlCursor) {
            .shape = (axis == FlDockAxis_X) ? FlCursorShape_ResizeH : FlCursorShape_ResizeV,
        });
    }

    if (!sig.dragging) {
        return;
    }

    // Map the cursor into the split's last-frame rect and turn the offset along the axis into the new child[0]
    // fraction. Position-based, so the divider tracks the cursor with no per-divider drag state to drift.
    Clay_ElementData data = Clay_GetElementData((Clay_ElementId) { .id = dock_layout_node_element_id(split_id) });
    if (!data.found) {
        return;
    }
    f32 extent = (axis == FlDockAxis_X) ? data.boundingBox.width : data.boundingBox.height;
    f32 cursor_rel = (axis == FlDockAxis_X) ? self->input_state.mouse.x - data.boundingBox.x
                                            : self->input_state.mouse.y - data.boundingBox.y;
    if (extent <= 0.0f) {
        return;
    }

    // The drag is the one place a floor gets WRITTEN into the tree: it already holds the extent, so it stops at the
    // same pixel floor the emit walk clamps to, on top of the fractional floor the op re-applies.
    FlDockRequest req = {
        .kind = FlDockRequest_SplitterResize,
        .target_node = split_id,
        .ratio = dock_layout_clamp_ratio(dock_splitter_ratio_at(cursor_rel, extent), axis, extent, nullptr),
    };
    dock_tree_apply(tree, &req);
}

void dock_splitter_emit(FlDockNodeId split_id, FlDockAxis axis) {
    const FlThemeColors* theme = ui_get_theme_colors(g_ui_ctx);

    // Clay skips hit-testing on alpha 0/1, so floor the alpha at 2 to keep the strip grabbable even when the
    // theme's separator is near-transparent.
    FlColor color = theme->border.separator;
    if (color.a < 2) {
        color.a = 2;
    }

    FlLayoutSizing sizing = (axis == FlDockAxis_X)
                                ? (FlLayoutSizing) { sizing_fixed(FL_DOCK_DIVIDER_THICKNESS), sizing_grow() }
                                : (FlLayoutSizing) { sizing_grow(), sizing_fixed(FL_DOCK_DIVIDER_THICKNESS) };

    ui_layout_area_id(divider_layout_id(split_id), .sizing = sizing, .color = color) {
        // The strip is pure chrome + grab area; no content.
    }
}
