// Dock Layout - emit Clay containers for a logical dock tree, then read the computed node rects back. Topology +
// ratios come from the tree; Clay computes every pixel rect.

#include "dock_layout.h"
#include <core/arena.h>
#include <core/log.h>
#include "dock_splitter.h"
#include "dock_workspace.h"
#include "../layout.h"
#include "../pixel_snap.h"
#include "../ui.h"
#include "../ui_private.h"
#include "../ui_widget_internal.h"
#include <external/clay/clay.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Element-id mapping

static FlLayoutId node_layout_id(FlDockNodeId node) {
    return ID_INT("fl_dock_node", (i32)node);
}

u32 dock_layout_node_element_id(FlDockNodeId node) {
    return ui_layout_id_to_clay_id(node_layout_id(node));
}

// The region under a leaf's tab header - a second id on the same node; the node id covers the whole tile.
static FlLayoutId content_layout_id(FlDockNodeId node) {
    return ID_INT("fl_dock_content", (i32)node);
}

u32 dock_layout_content_element_id(FlDockNodeId node) {
    return ui_layout_id_to_clay_id(content_layout_id(node));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The tile floor and the whole-pixel edge - both pure functions of the ratio and the extent, so a window resize
// runs them again and never writes the tree.

// The factor between Clay's physical px and the logical px every dock constant and geometry read-back is
// denominated in; never zero.
f32 dock_ui_scale(void) {
    f32 scale = ui_get_scale();
    return scale > 0.0f ? scale : 1.0f;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

f32 dock_layout_clamp_ratio(f32 ratio, FlDockAxis axis, f32 extent_px, bool* out_starved) {
    if (out_starved != nullptr) {
        *out_starved = false;
    }
    if (extent_px <= 0.0f) {
        return ratio;
    }

    f32 scale = dock_ui_scale();
    f32 min_child = ((axis == FlDockAxis_X) ? FL_DOCK_MIN_TILE_WIDTH : FL_DOCK_MIN_TILE_HEIGHT) * scale;
    f32 divider = FL_DOCK_DIVIDER_THICKNESS * scale;

    // child[0] measures ratio * extent, the divider a fixed strip, child[1] the remainder - so the two bounds are
    // the ratios at which each of them lands exactly on the floor.
    f32 lo = min_child / extent_px;
    f32 hi = 1.0f - (min_child + divider) / extent_px;
    if (lo >= hi) {
        f32 even = 0.5f * (1.0f - divider / extent_px);
        if (out_starved != nullptr) {
            *out_starved = true;
        }
        return even > 0.0f ? even : 0.5f;
    }
    if (ratio < lo) {
        return lo;
    }
    return ratio > hi ? hi : ratio;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

f32 dock_layout_snap_ratio(f32 ratio, f32 extent_px) {
    if (extent_px <= 0.0f) {
        return ratio;
    }
    return ui_snap_to_pixel(ratio * extent_px) / extent_px;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Emit

// Every non-empty leaf gets a tab header over a content area - a one-panel leaf included; the header is
// also the leaf's GRAB HANDLE: clicking a tab re-selects that panel through the dock-as-tab op, and dragging a tab
// out of the header reports the grab through out_grab for the caller to turn into an undock at its safe point.
// Only the empty root (the floor) is a bare placeholder. The content area carries its own element id: a declared
// panel's body attaches to it as a floating element (dock_panel_scope_begin), which is how the panel's widgets end
// up inside the tile without the walk having to call back into the app mid-recursion.
static void emit_leaf(FlDockTree* tree, const FlDockNode* leaf, FlLayoutSizingAxis width, FlLayoutSizingAxis height,
                      FlDockPanelId* out_grab) {
    FlLayoutId id = node_layout_id(leaf->id);

    FlUiCtx* self = g_ui_ctx;
    const FlThemeColors* theme = ui_get_theme_colors(self);
    const FlDockWorkspace* ws = dock_workspace_current();

    // A hidden panel keeps its slot and its tab index in the tree but contributes no tab, so the strip is built
    // over the visible subset and shown maps a strip index back to the leaf's tab index.
    // Titles live in the workspace arena, so the frame-arena array can hold them by reference.
    FlString* labels = arena_alloc_array(self->render_frame_allocator, FlString, leaf->leaf.count);
    u32* shown = arena_alloc_array(self->render_frame_allocator, u32, leaf->leaf.count);
    FlDockPanelId shown_panel = dock_workspace_leaf_shown_panel(ws, leaf);

    // The slot a drag opened is empty by contract - the overlay is the one thing drawing the dragged panel - but
    // the gap leaf still holds that panel, so its tab is suppressed here. A tab-into gap is excluded: showing the
    // tab in the strip IS its preview.
    FlDockPanelId gap_panel = 0;
    if (dock_drag_active(&ws->drag) && ws->drag.gap_is_new && ws->drag.gap_leaf == leaf->id) {
        gap_panel = ws->drag.panel;
    }

    int shown_count = 0;
    int selected = 0;
    for_count(i, leaf->leaf.count) {
        FlDockPanelId panel = leaf->leaf.panels[i];
        if (panel == gap_panel || !dock_workspace_panel_visible(ws, panel)) {
            continue;
        }
        if (panel == shown_panel) {
            selected = shown_count;
        }
        labels[shown_count] = dock_workspace_panel_title(ws, panel);
        shown[shown_count++] = i;
    }

    if (shown_count == 0) {
        ui_layout_area_id(id, .sizing = { .width = width, .height = height }) {
            // The empty floor, or a leaf whose panels are all hidden - no header and nothing to grab.
        }
        return;
    }

    // Namespace the per-tab ids by leaf node id (shifted to give each leaf its own block), so dock tabs never
    // collide with a ui_tab_bar elsewhere or with another leaf.
    u32 instance_id = (u32)leaf->id << 8;
    int new_selected = selected;
    int dragged = -1;

    ui_layout_area_id(id, .direction = LayoutDirection_TopToBottom, .sizing = { .width = width, .height = height }) {
        // The header is the tile's TITLE BAR: a full-width strip at a title-bar height carrying the leaf's
        // tabs. A LONE panel's tab stretches across the whole bar and drops its tint. TWO OR MORE keep the
        // [min, max] width band and the selected one only tints.
        const bool lone = shown_count == 1;
        const UiTabStripLayout header = {
            .min_width = DOCK_TAB_MIN_WIDTH,
            .max_width = DOCK_TAB_MAX_WIDTH,
            .height = DOCK_TAB_HEIGHT,
            .stretch = lone,
            .selected_variant = lone ? FlWidgetVariant_Ghost : FlWidgetVariant_Soft,
        };
        const FlTabBarConfig header_cfg = {
            .item_radius = (f32)FlRadius_SM,
            .widget.text_color = lone ? theme->text.primary : (FlColor) { 0 },
        };
        ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_fixed(DOCK_TAB_HEIGHT) },
                       .color = theme->surface.elevated) {
            new_selected = ui_tab_strip(FL_S("fl_dock_tab"), instance_id, labels, shown_count, selected, header_cfg,
                                        theme, header, out_grab != nullptr ? &dragged : nullptr);
        }
        ui_layout_area_id(content_layout_id(leaf->id), .sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
            // The selected panel's body attaches here by id - emitted later in the frame, by the app.
        }
    }

    // A pulled-out tab is only reported: the undock it stands for is structural, so the caller performs it outside
    // this walk. First grab of the frame wins; a second strip cannot overwrite it.
    if (dragged >= 0 && out_grab != nullptr && *out_grab == 0) {
        *out_grab = leaf->leaf.panels[shown[dragged]];
    }

    // A selection change goes through the dock-as-tab op (re-select), the only sanctioned tree mutator.
    if (new_selected >= 0 && shown[new_selected] != leaf->leaf.selected) {
        FlDockRequest req = {
            .kind = FlDockRequest_Tab,
            .panel = leaf->leaf.panels[shown[new_selected]],
            .target_node = leaf->id,
        };
        dock_tree_apply(tree, &req);
    }
}

static void emit_node(FlDockTree* tree, FlDockNode* node, FlLayoutSizingAxis width, FlLayoutSizingAxis height,
                      FlDockPanelId* out_grab);

// True when nothing under node renders: every panel in every leaf below it is hidden. An empty leaf (only the
// root can be one) is the floor, not a hidden subtree.
static bool subtree_all_hidden(const FlDockWorkspace* ws, const FlDockNode* node) {
    if (node->kind == FlDockNodeKind_Split) {
        return subtree_all_hidden(ws, node->split.child[0]) && subtree_all_hidden(ws, node->split.child[1]);
    }
    for_count(i, node->leaf.count) {
        if (dock_workspace_panel_visible(ws, node->leaf.panels[i])) {
            return false;
        }
    }
    return node->leaf.count > 0;
}

static void emit_node(FlDockTree* tree, FlDockNode* node, FlLayoutSizingAxis width, FlLayoutSizingAxis height,
                      FlDockPanelId* out_grab) {
    FlLayoutId id = node_layout_id(node->id);

    if (node->kind == FlDockNodeKind_Split) {
        FlLayoutDirection dir
            = (node->split.axis == FlDockAxis_X) ? LayoutDirection_LeftToRight : LayoutDirection_TopToBottom;

        // A child everything under which is hidden is not emitted: its sibling takes the whole slot and no divider
        // is drawn. The split itself is untouched, so showing a panel back restores the exact arrangement.
        const FlDockWorkspace* ws = dock_workspace_current();
        bool hidden0 = subtree_all_hidden(ws, node->split.child[0]);
        bool hidden1 = subtree_all_hidden(ws, node->split.child[1]);

        if (hidden0 != hidden1) {
            ui_layout_area_id(id, .direction = dir, .sizing = { .width = width, .height = height }) {
                emit_node(tree, node->split.child[hidden0 ? 1 : 0], sizing_grow(), sizing_grow(), out_grab);
            }
            return;
        }

        if (hidden0 && hidden1) {
            ui_layout_area_id(id, .sizing = { .width = width, .height = height }) {
                // Every panel below this split is hidden - nothing to tile.
            }
            return;
        }

        // Resolve a divider drag against last frame's geometry BEFORE reading the ratio, so the new ratio drives
        // THIS frame's layout. Only for a split that emits its divider - the branches above emit none.
        dock_splitter_handle_drag(tree, node->id, node->split.axis);

        // The split's own box from the PREVIOUS frame's layout, like every other geometry read-back - the room the
        // floor below is measured against. Absent on the first frame, which makes the clamp a no-op.
        Clay_ElementData box = Clay_GetElementData((Clay_ElementId) { .id = dock_layout_node_element_id(node->id) });
        f32 extent = 0.0f;
        if (box.found) {
            extent = (node->split.axis == FlDockAxis_X) ? box.boundingBox.width : box.boundingBox.height;
        }

        bool starved = false;
        f32 clamped = dock_layout_clamp_ratio(node->split.ratio, node->split.axis, extent, &starved);
        if (starved != node->split.starve_logged) {
            node->split.starve_logged = starved;
            if (starved) {
                log_error(
                    "dock split %u: %.0fpx along %c leaves both tiles under the %.0fpx floor — the workspace "
                    "overflows its host and is clipped",
                    (unsigned)node->id, (double)extent, node->split.axis == FlDockAxis_X ? 'x' : 'y',
                    (double)((node->split.axis == FlDockAxis_X) ? FL_DOCK_MIN_TILE_WIDTH : FL_DOCK_MIN_TILE_HEIGHT));
            }
        }

        ui_layout_area_id(id, .direction = dir, .sizing = { .width = width, .height = height }) {
            // Floored and snapped to a whole-pixel edge on the way in. Both are pure functions of the ratio and
            // the extent, so neither writes node->split.ratio - a resize re-derives r instead.
            f32 r = dock_layout_snap_ratio(clamped, extent);
            // child[0] keeps its exact percent(ratio); the divider takes a fixed strip on the seam; child[1] grows
            // into the remainder, so the seam never leaves a hole.
            FlLayoutSizingAxis w0, h0, w1, h1;
            if (node->split.axis == FlDockAxis_X) {
                w0 = sizing_percent(r);
                h0 = sizing_grow();
                w1 = sizing_grow();
                h1 = sizing_grow();
            } else {
                w0 = sizing_grow();
                h0 = sizing_percent(r);
                w1 = sizing_grow();
                h1 = sizing_grow();
            }
            emit_node(tree, node->split.child[0], w0, h0, out_grab);
            dock_splitter_emit(node->id, node->split.axis);
            emit_node(tree, node->split.child[1], w1, h1, out_grab);
        }
    } else {
        emit_leaf(tree, node, width, height, out_grab);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void dock_layout_emit(FlDockTree* tree, FlDockPanelId* out_grab) {
    if (tree == nullptr || tree->root == nullptr) {
        return;
    }
    emit_node(tree, tree->root, sizing_grow(), sizing_grow(), out_grab);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Readback - the same nodes in the same pre-order, each rect queried by element id.

static u32 count_nodes(const FlDockNode* node) {
    if (node == nullptr) {
        return 0;
    }
    if (node->kind == FlDockNodeKind_Split) {
        return 1 + count_nodes(node->split.child[0]) + count_nodes(node->split.child[1]);
    }
    return 1;
}

static u32 read_rects_rec(const FlDockNode* node, FlDockNodeRect* out, u32 written) {
    if (node == nullptr) {
        return written;
    }

    Clay_ElementId clay_id = { .id = dock_layout_node_element_id(node->id) };
    Clay_ElementData data = Clay_GetElementData(clay_id);

    out[written] = (FlDockNodeRect) {
        .node = node->id,
        .rect = { data.boundingBox.x, data.boundingBox.y, data.boundingBox.width, data.boundingBox.height },
        .found = data.found,
    };
    written++;

    if (node->kind == FlDockNodeKind_Split) {
        written = read_rects_rec(node->split.child[0], out, written);
        written = read_rects_rec(node->split.child[1], out, written);
    }
    return written;
}

void dock_layout_read_rects(const FlDockTree* tree, FlArena* arena, FlDockLayout* out) {
    out->nodes = nullptr;
    out->count = 0;
    if (tree == nullptr || tree->root == nullptr) {
        return;
    }

    u32 n = count_nodes(tree->root);
    out->nodes = arena_alloc_array(arena, FlDockNodeRect, n);
    out->count = read_rects_rec(tree->root, out->nodes, 0);
}

const FlDockRect* dock_layout_find_rect(const FlDockLayout* layout, FlDockNodeId node) {
    for (u32 i = 0; i < layout->count; i++) {
        if (layout->nodes[i].node == node && layout->nodes[i].found) {
            return &layout->nodes[i].rect;
        }
    }
    return nullptr;
}
