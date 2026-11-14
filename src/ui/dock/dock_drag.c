#include "dock_drag.h"

#include "dock_layout.h"
#include "../ui.h"
#include "../ui_private.h"
#include <external/clay/clay.h>
#include <math.h>

static bool point_in_rect(FlDockRect r, FlVec2 p) {
    return p.x >= r.x && p.x <= r.x + r.width && p.y >= r.y && p.y <= r.y + r.height;
}

static f32 rect_dist_sq(FlDockRect r, FlVec2 p) {
    f32 dx = p.x < r.x ? r.x - p.x : (p.x > r.x + r.width ? p.x - (r.x + r.width) : 0.0f);
    f32 dy = p.y < r.y ? r.y - p.y : (p.y > r.y + r.height ? p.y - (r.y + r.height) : 0.0f);
    return dx * dx + dy * dy;
}

// The nearest leaf is used because splitter gutters belong to no leaf, so there is not always a containing one.
static i32 nearest_leaf(const FlDockDropNode* nodes, u32 count, FlVec2 cursor) {
    i32 best = -1;
    f32 best_dist = 0.0f;
    for (u32 i = 0; i < count; ++i) {
        if (!nodes[i].is_leaf) {
            continue;
        }
        f32 dist = rect_dist_sq(nodes[i].rect, cursor);
        if (best < 0 || dist < best_dist) {
            best = (i32)i;
            best_dist = dist;
        }
    }
    return best;
}

// The tab-into region: the leaf's tab header, or a box of the tile's middle covering FL_DOCK_TAB_CENTER_FRAC of it
// on each axis. Writes the insertion slot along the strip; a centre drop names the end of the strip.
static bool tab_target(FlDockRect leaf, FlVec2 cursor, u32* out_index) {
    if (cursor.y >= leaf.y && cursor.y < leaf.y + DOCK_TAB_HEIGHT) {
        f32 x = cursor.x - leaf.x;
        *out_index = x <= 0.0f ? 0 : (u32)(x / DOCK_TAB_MIN_WIDTH);
        return true;
    }
    f32 half = FL_DOCK_TAB_CENTER_FRAC * 0.5f;
    f32 u = leaf.width > 0.0f ? (cursor.x - leaf.x) / leaf.width - 0.5f : 0.0f;
    f32 v = leaf.height > 0.0f ? (cursor.y - leaf.y) / leaf.height - 0.5f : 0.0f;
    if (fabsf(u) <= half && fabsf(v) <= half) {
        *out_index = FL_DOCK_TAB_INDEX_END;
        return true;
    }
    return false;
}

// The tile's two diagonals cut it into four triangles, one per side, so every point outside the tab band splits.
static FlDockSide diagonal_side(FlDockRect leaf, FlVec2 cursor) {
    f32 u = leaf.width > 0.0f ? (cursor.x - leaf.x) / leaf.width - 0.5f : 0.0f;
    f32 v = leaf.height > 0.0f ? (cursor.y - leaf.y) / leaf.height - 0.5f : 0.0f;
    if (fabsf(u) >= fabsf(v)) {
        return u < 0.0f ? FlDockSide_Left : FlDockSide_Right;
    }
    return v < 0.0f ? FlDockSide_Top : FlDockSide_Bottom;
}

// The workspace border band, on the nearest edge. False when the cursor is deeper in than the band.
static bool outer_band_side(FlDockRect ws, FlVec2 cursor, FlDockSide* out_side) {
    f32 depth = cursor.x - ws.x;
    FlDockSide side = FlDockSide_Left;
    f32 candidates[] = { (ws.x + ws.width) - cursor.x, cursor.y - ws.y, (ws.y + ws.height) - cursor.y };
    FlDockSide sides[] = { FlDockSide_Right, FlDockSide_Top, FlDockSide_Bottom };
    for (u32 i = 0; i < sizeof_array(candidates); ++i) {
        if (candidates[i] < depth) {
            depth = candidates[i];
            side = sides[i];
        }
    }
    if (depth > FL_DOCK_OUTER_BAND) {
        return false;
    }
    *out_side = side;
    return true;
}

static FlDockDropZone split_zone(FlDockSide side) {
    switch (side) {
        case FlDockSide_Left:
            return FlDockDropZone_Left;
        case FlDockSide_Right:
            return FlDockDropZone_Right;
        case FlDockSide_Top:
            return FlDockDropZone_Top;
        default:
            return FlDockDropZone_Bottom;
    }
}

static FlDockDropZone outer_zone(FlDockSide side) {
    switch (side) {
        case FlDockSide_Left:
            return FlDockDropZone_OuterLeft;
        case FlDockSide_Right:
            return FlDockDropZone_OuterRight;
        case FlDockSide_Top:
            return FlDockDropZone_OuterTop;
        default:
            return FlDockDropZone_OuterBottom;
    }
}

static i32 root_index(const FlDockDropNode* nodes, u32 count) {
    for (u32 i = 0; i < count; ++i) {
        if (nodes[i].is_root) {
            return (i32)i;
        }
    }
    return -1;
}

// The dock-family gate: a general (untagged) panel is family 0, so it docks anywhere unfiltered.
static bool family_accepted(const FlDockFamilyGate* gate) {
    return gate->accept_filter == 0 || gate->panel_family == gate->accept_filter;
}

FlDockDropZone hit_test_drop_zones(const FlDockDropNode* nodes, u32 count, FlVec2 cursor, FlDockRequest* out_req) {
    *out_req = (FlDockRequest) { 0 };

    i32 root = root_index(nodes, count);
    if (root >= 0 && !point_in_rect(nodes[root].rect, cursor)) {
        return FlDockDropZone_None;
    }

    i32 leaf = nearest_leaf(nodes, count, cursor);
    if (leaf < 0) {
        return FlDockDropZone_None;
    }
    FlDockRect tile = nodes[leaf].rect;

    // Precedence: tab region, then outer band, then the leaf's split quadrants.
    u32 to_index = 0;
    if (tab_target(tile, cursor, &to_index)) {
        out_req->kind = FlDockRequest_Tab;
        out_req->target_node = nodes[leaf].node;
        out_req->to_index = to_index;
        return FlDockDropZone_Center;
    }

    FlDockSide side;
    if (root >= 0 && outer_band_side(nodes[root].rect, cursor, &side)) {
        out_req->kind = FlDockRequest_Outer;
        out_req->side = side;
        out_req->ratio = FL_DOCK_GAP_RATIO;
        return outer_zone(side);
    }

    out_req->kind = FlDockRequest_Split;
    out_req->side = diagonal_side(tile, cursor);
    out_req->target_node = nodes[leaf].node;
    out_req->ratio = FL_DOCK_GAP_RATIO;
    return split_zone(out_req->side);
}

// Sticky gap: from inside the slot the layout has opened, the cursor may push the gap around the node it opened in
// (the side still resolves) but may not hand it to another node or another kind of dock. Only a gap that opened
// space sticks; a tab-into displaces nothing.
static bool has_standing_gap(const FlDockDrag* drag) {
    return drag->zone != FlDockDropZone_None && drag->has_gap_rect && drag->gap_is_new;
}

static bool gap_holds_target(const FlDockDrag* drag, FlVec2 cursor, const FlDockRequest* fresh) {
    if (!has_standing_gap(drag) || !point_in_rect(drag->gap_rect, cursor)) {
        return false;
    }
    return fresh->kind != drag->request.kind || fresh->target_node != drag->request.target_node;
}

static bool within_hysteresis(const FlDockDrag* drag, FlVec2 cursor) {
    f32 dx = cursor.x - drag->hyst_anchor.x;
    f32 dy = cursor.y - drag->hyst_anchor.y;
    return dx * dx + dy * dy < FL_DOCK_DRAG_HYSTERESIS * FL_DOCK_DRAG_HYSTERESIS;
}

// A tab region hit never waits out the hysteresis, so tab-into stays in reach.
static bool switch_is_held(const FlDockDrag* drag, FlVec2 cursor, FlDockDropZone zone, const FlDockRequest* req) {
    if (drag->zone == FlDockDropZone_None || zone == FlDockDropZone_Center) {
        return false;
    }
    bool agrees = zone == drag->zone && req->target_node == drag->request.target_node;
    return !agrees && within_hysteresis(drag, cursor);
}

FlDockDropZone dock_drag_resolve(FlDockDrag* drag, FlVec2 cursor) {
    FlDockRequest req = { 0 };
    FlDockDropZone zone = FlDockDropZone_None;
    // A panel the workspace rejects gets no zone, so it has no highlight, preview or commit.
    if (family_accepted(&drag->gate)) {
        zone = hit_test_drop_zones(drag->base, drag->base_count, cursor, &req);
    }
    req.panel = drag->panel;

    if (gap_holds_target(drag, cursor, &req)) {
        return drag->zone;
    }
    if (switch_is_held(drag, cursor, zone, &req)) {
        return drag->zone; // still within the switch hysteresis of where the disagreement started
    }
    drag->hyst_anchor = cursor;
    drag->zone = zone;
    drag->request = req;
    return zone;
}

bool dock_drag_active(const FlDockDrag* drag) {
    return drag != nullptr && drag->active;
}

// Reciprocal of the UI scale, so physical px reach logical px by multiply.
static f32 drag_inv_scale(void) {
    return 1.0f / dock_ui_scale();
}

// Clay_GetElementData returns the previous frame's box during this frame, in physical px; the buffer stores logical
// px so the resolution constants mean logical px.
static FlDockRect node_rect(FlDockNodeId node, f32 inv_scale) {
    Clay_ElementData data = Clay_GetElementData((Clay_ElementId) { .id = dock_layout_node_element_id(node) });
    return (FlDockRect) { data.boundingBox.x * inv_scale, data.boundingBox.y * inv_scale,
                          data.boundingBox.width * inv_scale, data.boundingBox.height * inv_scale };
}

static void fill_base_rec(FlDockDrag* drag, const FlDockNode* node, bool is_root, f32 inv_scale) {
    if (node == nullptr || drag->base_count >= FL_DOCK_DRAG_MAX_NODES) {
        return;
    }
    FlDockRect rect = node_rect(node->id, inv_scale);

    if (node->id == drag->gap_leaf) {
        drag->gap_rect = rect;
        drag->has_gap_rect = true;
        if (drag->gap_is_new) {
            return; // the gap is never a hit-test candidate; the sticky-gap rule owns the cursor inside it
        }
    }
    if (node->id == drag->pre_gap_node && drag->has_pre_gap) {
        rect = drag->pre_gap; // pre-gap footprint: hit-test the shrunken node where it stood before the gap
    }

    drag->base[drag->base_count++] = (FlDockDropNode) {
        .node = node->id,
        .rect = rect,
        .is_leaf = (node->kind == FlDockNodeKind_Leaf),
        .is_root = is_root,
    };
    if (node->kind == FlDockNodeKind_Split) {
        fill_base_rec(drag, node->split.child[0], false, inv_scale);
        fill_base_rec(drag, node->split.child[1], false, inv_scale);
    }
}

static bool layout_readable(const FlDockTree* live) {
    if (live == nullptr) {
        return false;
    }
    return live->root != nullptr && Clay_GetCurrentContext() != nullptr;
}

// Without a live Clay layout context (a headless test) the buffer is left empty.
static void refresh_base(FlDockDrag* drag, const FlDockTree* live) {
    drag->base_count = 0;
    drag->has_gap_rect = false;
    if (layout_readable(live)) {
        fill_base_rec(drag, live->root, true, drag_inv_scale());
    }
}

static const FlDockRect* base_rect_of(const FlDockDrag* drag, FlDockNodeId node) {
    for (u32 i = 0; i < drag->base_count; ++i) {
        if (drag->base[i].node == node) {
            return &drag->base[i].rect;
        }
    }
    return nullptr;
}

void dock_drag_begin_undock(FlDockDrag* drag, FlDockTree* committed, FlDockPanelId panel, FlDockFamilyGate gate) {
    if (drag == nullptr || committed == nullptr) {
        return;
    }
    // Read the origin leaf before the undock dissolves it: it is the seat a drop over no zone returns to.
    FlDockNode* origin = dock_tree_find_leaf_with_panel(committed, panel);
    FlDockNodeId origin_leaf = origin != nullptr ? origin->id : 0;

    FlDockRequest undock = { .kind = FlDockRequest_Undock, .panel = panel };
    dock_tree_apply(committed, &undock);

    *drag = (FlDockDrag) {
        .active = true,
        .panel = panel,
        .gate = gate,
        // One settle frame lets dock_drag_emit lay the post-detach tiles out before the hit-test buffer is read.
        .settle_frames = 1,
        .zone = FlDockDropZone_None,
        .origin_leaf = origin_leaf,
    };
}

// Drop the whole in-flight state, preview tree included. The committed tree is untouched, so an abandoned drop
// leaves exactly the shape the drag started from.
static void drag_reset(FlDockDrag* drag) {
    dock_tree_destroy(drag->preview);
    *drag = (FlDockDrag) { 0 };
}

void dock_drag_discard(FlDockDrag* drag) {
    if (drag != nullptr) {
        drag_reset(drag);
    }
}

static bool commit_drop(FlDockDrag* drag, FlDockTree* committed) {
    if (!dock_tree_apply(committed, &drag->request)) {
        return false;
    }
    if (drag->request.kind == FlDockRequest_Tab) {
        // dock-as-tab lands the panel at the end of the strip; the resolved insertion slot is the reorder that
        // walks it back to where the cursor was along the strip.
        FlDockRequest place
            = { .kind = FlDockRequest_ReorderTab, .panel = drag->panel, .to_index = drag->request.to_index };
        dock_tree_apply(committed, &place);
    }
    return true;
}

// Released over no zone, on a panel this drag undocked: put it back in the seat it was pulled from.
static bool return_home(FlDockDrag* drag, FlDockTree* committed) {
    if (drag->origin_leaf == 0) {
        return false;
    }
    FlDockRequest home = { .kind = FlDockRequest_Tab, .panel = drag->panel, .target_node = drag->origin_leaf };
    return dock_tree_apply(committed, &home);
}

bool dock_drag_end(FlDockDrag* drag, FlDockTree* committed) {
    if (drag == nullptr) {
        return false;
    }
    bool committed_change = false;
    if (drag->active && committed != nullptr) {
        committed_change
            = drag->zone != FlDockDropZone_None ? commit_drop(drag, committed) : return_home(drag, committed);
    }
    drag_reset(drag);
    return committed_change;
}

bool dock_drag_cancel(FlDockDrag* drag, FlDockTree* committed) {
    if (drag == nullptr) {
        return false;
    }
    drag->zone = FlDockDropZone_None; // drops into dock_drag_end's return-home branch
    return dock_drag_end(drag, committed);
}

// Two requests raise the same gap when they name the same operation on the same target.
static bool request_same_target(const FlDockRequest* a, const FlDockRequest* b) {
    return a->kind == b->kind && a->target_node == b->target_node && a->side == b->side;
}

// Take the gap out of the preview, undoing the request that opened it. The panel lands in the preview's
// floating set, which the next request detaches it from.
static void close_gap(FlDockDrag* drag) {
    FlDockRequest undock = { .kind = FlDockRequest_Undock, .panel = drag->panel };
    dock_tree_apply(drag->preview, &undock);
    drag->applied_zone = FlDockDropZone_None;
    drag->applied = (FlDockRequest) { 0 };
    drag->gap_leaf = 0;
    drag->gap_is_new = false;
    drag->has_gap_rect = false;
    drag->has_pre_gap = false;
}

static const FlDockNode* sibling_of(const FlDockNode* node) {
    const FlDockNode* parent = node->parent;
    return parent->split.child[0] == node ? parent->split.child[1] : parent->split.child[0];
}

// Freeze the gap's new sibling's pre-gap rect so the hit-test keeps the boundary that resolved this zone.
static void freeze_pre_gap(FlDockDrag* drag, const FlDockNode* gap) {
    const FlDockNode* sibling = sibling_of(gap);
    const FlDockRect* rect = base_rect_of(drag, sibling->id);
    if (rect == nullptr) {
        return;
    }
    drag->pre_gap_node = sibling->id;
    drag->pre_gap = *rect;
    drag->has_pre_gap = true;
}

static void open_gap(FlDockDrag* drag, FlDockDropZone zone) {
    dock_tree_apply(drag->preview, &drag->request);
    drag->applied = drag->request;
    drag->applied_zone = zone;

    FlDockNode* gap = dock_tree_find_leaf_with_panel(drag->preview, drag->panel);
    drag->gap_leaf = gap != nullptr ? gap->id : 0;
    drag->gap_is_new = gap != nullptr && drag->request.kind != FlDockRequest_Tab;
    if (drag->gap_is_new && gap->parent != nullptr) {
        freeze_pre_gap(drag, gap);
    }
}

static void release_gap(FlDockDrag* drag) {
    if (drag->preview != nullptr && drag->applied_zone != FlDockDropZone_None) {
        close_gap(drag);
    }
}

static bool preview_stands_on(const FlDockDrag* drag, FlDockDropZone zone) {
    return drag->preview != nullptr && drag->applied_zone == zone
           && request_same_target(&drag->applied, &drag->request);
}

// Bring the retained preview onto zone. The tree is cloned once, at the first resolved zone, and mutated in place
// from there: a re-target is an undock followed by the new request, so the old gap closes while the new one opens.
static void retarget_preview(FlDockDrag* drag, FlDockTree* committed, FlDockDropZone zone) {
    if (zone == FlDockDropZone_None) {
        release_gap(drag);
        return;
    }
    if (preview_stands_on(drag, zone)) {
        return;
    }
    if (drag->preview == nullptr) {
        drag->preview = dock_tree_clone(committed);
    } else {
        release_gap(drag);
    }
    open_gap(drag, zone);
}

void dock_drag_emit(FlDockDrag* drag, FlDockTree* committed, const DockPanels* panels) {
    if (!dock_drag_active(drag) || committed == nullptr) {
        return;
    }
    // Waiting out the post-undock reflow: emit the committed tiles plainly, then read back what they laid out.
    if (drag->settle_frames > 0) {
        drag->settle_frames--;
        drag->zone = FlDockDropZone_None;
        drag->request = (FlDockRequest) { 0 };
        dock_layout_emit(committed, panels, 0, nullptr);
        return;
    }

    // The layout the previous frame computed is the provisional one, gap included. The hit-test resolves against it.
    FlDockTree* live = drag->preview != nullptr ? drag->preview : committed;
    refresh_base(drag, live);

    FlUiCtx* self = g_ui_ctx;
    f32 inv_scale = drag_inv_scale();
    // The absolute pointer, in the same logical-pixel space as the buffer rects.
    FlVec2 cursor = { self->input_state.mouse.x * inv_scale, self->input_state.mouse.y * inv_scale };

    FlDockDropZone zone = dock_drag_resolve(drag, cursor);
    retarget_preview(drag, committed, zone);

    // No grab is taken from a drag walk, because one is already in flight. A slot the drag opened stays empty: the
    // overlay attached to that leaf is the one thing drawing the dragged panel, so its tab is left out there. A
    // tab-into gap keeps the tab, which is its preview.
    FlDockTree* shown = drag->preview != nullptr ? drag->preview : committed;
    dock_layout_emit(shown, panels, drag->gap_is_new ? drag->panel : 0, nullptr);
}
