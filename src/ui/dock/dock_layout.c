#include "dock_layout.h"
#include <core/arena.h>
#include <core/log.h>
#include "dock_splitter.h"
#include "../layout.h"
#include "../pixel_snap.h"
#include "../ui.h"
#include "../ui_private.h"
#include "../ui_widget_internal.h"
#include <external/clay/clay.h>

static FlLayoutId node_layout_id(FlDockNodeId node) {
    return ID_INT("fl_dock_node", (i32)node);
}

u32 dock_layout_node_element_id(FlDockNodeId node) {
    return ui_layout_id_to_clay_id(node_layout_id(node));
}

// The region under a leaf's tab header. It is a second id on the same node; the node id covers the whole tile.
static FlLayoutId content_layout_id(FlDockNodeId node) {
    return ID_INT("fl_dock_content", (i32)node);
}

u32 dock_layout_content_element_id(FlDockNodeId node) {
    return ui_layout_id_to_clay_id(content_layout_id(node));
}

// The factor between Clay's physical px and the logical px every dock constant and geometry read-back is
// denominated in. Never zero.
f32 dock_ui_scale(void) {
    f32 scale = ui_get_scale();
    return scale > 0.0f ? scale : 1.0f;
}

static f32 min_tile_extent(FlDockAxis axis) {
    return (axis == FlDockAxis_X) ? FL_DOCK_MIN_TILE_WIDTH : FL_DOCK_MIN_TILE_HEIGHT;
}

static f32 clamp_within_floor(f32 ratio, FlDockAxis axis, f32 extent_px, bool* starved) {
    f32 scale = dock_ui_scale();
    f32 min_child = min_tile_extent(axis) * scale;
    f32 divider = FL_DOCK_DIVIDER_THICKNESS * scale;

    // child[0] measures ratio * extent, the divider is a fixed strip and child[1] takes the remainder, so the two
    // bounds are the ratios at which each child lands exactly on the floor.
    f32 lo = min_child / extent_px;
    f32 hi = 1.0f - (min_child + divider) / extent_px;
    if (lo >= hi) {
        f32 even = 0.5f * (1.0f - divider / extent_px);
        *starved = true;
        return even > 0.0f ? even : 0.5f;
    }
    if (ratio < lo) {
        return lo;
    }
    return ratio > hi ? hi : ratio;
}

f32 dock_layout_clamp_ratio(f32 ratio, FlDockAxis axis, f32 extent_px, bool* out_starved) {
    bool starved = false;
    f32 clamped = (extent_px > 0.0f) ? clamp_within_floor(ratio, axis, extent_px, &starved) : ratio;
    if (out_starved != nullptr) {
        *out_starved = starved;
    }
    return clamped;
}

f32 dock_layout_snap_ratio(f32 ratio, f32 extent_px) {
    if (extent_px <= 0.0f) {
        return ratio;
    }
    return ui_snap_to_pixel(ratio * extent_px) / extent_px;
}

// State shared by one emit walk: the tree the walk may restructure, the per-panel records it reads, the panel whose
// tab is left out, and the slot the first tab grab is reported through.
typedef struct EmitWalk {
    FlDockTree* tree;
    const DockPanels* panels;
    FlDockPanelId untabbed;
    FlDockPanelId* out_grab;
} EmitWalk;

// The tab strip of one leaf. A hidden panel keeps its slot and its tab index in the tree but contributes no tab, so
// the strip is built over the visible subset and shown maps a strip index back to the leaf's tab index.
typedef struct LeafTabs {
    FlString* labels;
    u32* shown;
    int count;
    int selected;
} LeafTabs;

static LeafTabs collect_leaf_tabs(const EmitWalk* walk, const FlDockNode* leaf) {
    FlUiCtx* self = g_ui_ctx;
    // Titles live in the workspace arena, so the frame-arena array can hold them by reference.
    LeafTabs tabs = {
        .labels = arena_alloc_array(self->render_frame_allocator, FlString, leaf->leaf.count),
        .shown = arena_alloc_array(self->render_frame_allocator, u32, leaf->leaf.count),
    };
    FlDockPanelId shown_panel = dock_panels_shown_in_leaf(walk->panels, leaf);

    for_count(i, leaf->leaf.count) {
        FlDockPanelId panel = leaf->leaf.panels[i];
        if (panel == walk->untabbed || !dock_panels_visible(walk->panels, panel)) {
            continue;
        }
        if (panel == shown_panel) {
            tabs.selected = tabs.count;
        }
        tabs.labels[tabs.count] = dock_panels_title(walk->panels, panel);
        tabs.shown[tabs.count++] = i;
    }
    return tabs;
}

// The header is the tile's title bar: a full-width strip at a title-bar height carrying the leaf's tabs. A lone
// panel's tab stretches across the whole bar and drops its tint. Two or more keep the [min, max] width band and
// the selected one only tints. Returns the strip index selected after this frame's clicks; dragged receives the
// strip index of a tab pulled out of the header, or stays as it was.
static int emit_tab_header(const LeafTabs* tabs, FlDockNodeId leaf_id, const FlThemeColors* theme, int* dragged) {
    const bool lone = tabs->count == 1;
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
    // Namespace the per-tab ids by leaf node id (shifted to give each leaf its own block), so dock tabs never
    // collide with a ui_tab_bar elsewhere or with another leaf.
    u32 instance_id = (u32)leaf_id << 8;
    int new_selected = tabs->selected;
    ui_layout_area(.sizing = { .width = sizing_grow(), .height = sizing_fixed(DOCK_TAB_HEIGHT) },
                   .color = theme->surface.elevated) {
        new_selected = ui_tab_strip(FL_S("fl_dock_tab"), instance_id, tabs->labels, tabs->count, tabs->selected,
                                    header_cfg, theme, header, dragged);
    }
    return new_selected;
}

// A pulled-out tab is only reported: the undock it stands for is structural, so the caller performs it outside
// this walk. First grab of the frame wins; a second strip cannot overwrite it.
static bool grab_slot_free(const FlDockPanelId* out_grab) {
    return out_grab != nullptr && *out_grab == 0;
}

// A selection change goes through the dock-as-tab op (re-select).
static void apply_tab_selection(FlDockTree* tree, const FlDockNode* leaf, const LeafTabs* tabs, int new_selected) {
    if (new_selected < 0 || tabs->shown[new_selected] == leaf->leaf.selected) {
        return;
    }
    FlDockRequest req = {
        .kind = FlDockRequest_Tab,
        .panel = leaf->leaf.panels[tabs->shown[new_selected]],
        .target_node = leaf->id,
    };
    dock_tree_apply(tree, &req);
}

// Every non-empty leaf gets a tab header over a content area, a one-panel leaf included. The header is also the
// leaf's grab handle: clicking a tab re-selects that panel through the dock-as-tab op, and dragging a tab out of
// the header reports the grab through out_grab for the caller to turn into an undock at its safe point. Only the
// empty root (the floor) is a bare placeholder. The content area carries its own element id, and a declared
// panel's body attaches to it as a floating element (dock_panel_scope_begin).
static void emit_leaf(const EmitWalk* walk, const FlDockNode* leaf, FlLayoutSizingAxis width,
                      FlLayoutSizingAxis height) {
    FlLayoutId id = node_layout_id(leaf->id);
    const FlThemeColors* theme = ui_get_theme_colors(g_ui_ctx);
    LeafTabs tabs = collect_leaf_tabs(walk, leaf);

    if (tabs.count == 0) {
        ui_layout_area_id(id, .sizing = { .width = width, .height = height }) {
            // The empty floor, or a leaf whose panels are all hidden. It has no header and nothing to grab.
        }
        return;
    }

    int new_selected = tabs.selected;
    int dragged = -1;
    ui_layout_area_id(id, .direction = LayoutDirection_TopToBottom, .sizing = { .width = width, .height = height }) {
        new_selected = emit_tab_header(&tabs, leaf->id, theme, walk->out_grab != nullptr ? &dragged : nullptr);
        ui_layout_area_id(content_layout_id(leaf->id), .sizing = { .width = sizing_grow(), .height = sizing_grow() }) {
            // The selected panel's body attaches here by id. The app emits it later in the frame.
        }
    }

    if (dragged >= 0 && grab_slot_free(walk->out_grab)) {
        *walk->out_grab = leaf->leaf.panels[tabs.shown[dragged]];
    }
    apply_tab_selection(walk->tree, leaf, &tabs, new_selected);
}

static void emit_node(const EmitWalk* walk, FlDockNode* node, FlLayoutSizingAxis width, FlLayoutSizingAxis height);

// True when nothing under node renders: every panel in every leaf below it is hidden. An empty leaf (only the
// root can be one) is the floor, not a hidden subtree.
static bool subtree_all_hidden(const DockPanels* panels, const FlDockNode* node) {
    if (node->kind == FlDockNodeKind_Split) {
        return subtree_all_hidden(panels, node->split.child[0]) && subtree_all_hidden(panels, node->split.child[1]);
    }
    for_count(i, node->leaf.count) {
        if (dock_panels_visible(panels, node->leaf.panels[i])) {
            return false;
        }
    }
    return node->leaf.count > 0;
}

// The split's own box from the previous frame's layout, which is the room the floor below is measured against.
// Absent on the first frame, which makes the clamp a no-op.
static f32 split_extent(const FlDockNode* node) {
    Clay_ElementData box = Clay_GetElementData((Clay_ElementId) { .id = dock_layout_node_element_id(node->id) });
    if (!box.found) {
        return 0.0f;
    }
    return (node->split.axis == FlDockAxis_X) ? box.boundingBox.width : box.boundingBox.height;
}

// Logs once per starved spell: the flag is stored on the node and the line is written on the rising edge only.
static void track_starvation(FlDockNode* node, bool starved, f32 extent) {
    if (starved == node->split.starve_logged) {
        return;
    }
    node->split.starve_logged = starved;
    if (starved) {
        log_error("dock split %u: %.0fpx along %c leaves both tiles under the %.0fpx floor — the workspace "
                  "overflows its host and is clipped",
                  (unsigned)node->id, (double)extent, node->split.axis == FlDockAxis_X ? 'x' : 'y',
                  (double)min_tile_extent(node->split.axis));
    }
}

static void emit_split_children(const EmitWalk* walk, FlDockNode* node, f32 r) {
    // child[0] keeps its exact percent(ratio); the divider takes a fixed strip on the seam; child[1] grows
    // into the remainder, so the seam never leaves a hole.
    const bool along_x = node->split.axis == FlDockAxis_X;
    emit_node(walk, node->split.child[0], along_x ? sizing_percent(r) : sizing_grow(),
              along_x ? sizing_grow() : sizing_percent(r));
    dock_splitter_emit(node->id, node->split.axis);
    emit_node(walk, node->split.child[1], sizing_grow(), sizing_grow());
}

static void emit_split(const EmitWalk* walk, FlDockNode* node, FlLayoutSizingAxis width, FlLayoutSizingAxis height) {
    FlLayoutId id = node_layout_id(node->id);
    FlLayoutDirection dir
        = (node->split.axis == FlDockAxis_X) ? LayoutDirection_LeftToRight : LayoutDirection_TopToBottom;

    // A child everything under which is hidden is not emitted: its sibling takes the whole slot and no divider
    // is drawn. The split itself is untouched, so showing a panel back restores the exact arrangement.
    bool hidden0 = subtree_all_hidden(walk->panels, node->split.child[0]);
    bool hidden1 = subtree_all_hidden(walk->panels, node->split.child[1]);

    if (hidden0 != hidden1) {
        ui_layout_area_id(id, .direction = dir, .sizing = { .width = width, .height = height }) {
            emit_node(walk, node->split.child[hidden0 ? 1 : 0], sizing_grow(), sizing_grow());
        }
        return;
    }

    if (hidden0 && hidden1) {
        ui_layout_area_id(id, .sizing = { .width = width, .height = height }) {
            // Every panel below this split is hidden, so there is nothing to tile.
        }
        return;
    }

    // Resolve a divider drag against last frame's geometry before reading the ratio, so the new ratio drives
    // this frame's layout. Only for a split that emits its divider; the branches above emit none.
    dock_splitter_handle_drag(walk->tree, node->id, node->split.axis);

    f32 extent = split_extent(node);
    bool starved = false;
    f32 clamped = dock_layout_clamp_ratio(node->split.ratio, node->split.axis, extent, &starved);
    track_starvation(node, starved, extent);

    ui_layout_area_id(id, .direction = dir, .sizing = { .width = width, .height = height }) {
        // Floored and snapped to a whole-pixel edge on the way in. Neither step writes node->split.ratio, so
        // a resize derives r again from the stored ratio and the new extent.
        emit_split_children(walk, node, dock_layout_snap_ratio(clamped, extent));
    }
}

static void emit_node(const EmitWalk* walk, FlDockNode* node, FlLayoutSizingAxis width, FlLayoutSizingAxis height) {
    if (node->kind == FlDockNodeKind_Split) {
        emit_split(walk, node, width, height);
    } else {
        emit_leaf(walk, node, width, height);
    }
}

void dock_layout_emit(FlDockTree* tree, const DockPanels* panels, FlDockPanelId untabbed, FlDockPanelId* out_grab) {
    if (tree == nullptr || tree->root == nullptr) {
        return;
    }
    const EmitWalk walk = { .tree = tree, .panels = panels, .untabbed = untabbed, .out_grab = out_grab };
    emit_node(&walk, tree->root, sizing_grow(), sizing_grow());
}

// Readback walks the same nodes in the same pre-order, each rect queried by element id.

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
