// Dock Workspace - bind immediately-declared panels to the retained dock tree's leaves, and decide which leaf's
// selected panel emits this frame. The tree is the source of truth; this layer is the thin per-frame declaration
// window over it. Also the frame's render entry (the emit walk and the drag gesture it drives), the layout
// save/load hooks, and the dock_panel scope the macro in ui/dock.h opens. The public fl_dock_* forwarders onto all
// of it live in dock_api.c.

#include "dock_workspace.h"

#include <core/arena.h>
#include <core/json.h>
#include <core/log.h>
#include <core/os/os.h>
#include <core/path.h>
#include <core/string.h>

#include <string.h>

#include "dock_drag.h"
#include "dock_layout.h"
#include "dock_persist.h"
#include "dock_shared.h"
#include "../layout.h"
#include "../ui.h"
#include "../ui_color_utils.h"
#include "../ui_private.h"
#include <external/clay/clay.h>

#include <flowi/core/assert.h>

// The settings-store file the layout blob lives in, alongside settings' own <module>.json files.
#define DOCK_LAYOUT_FILE FL_S("dock_layout.json")

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static DeclRecord* decl_find(const FlDockWorkspace* ws, FlDockPanelId id) {
    for (u32 i = 0; i < ws->decl_count; ++i) {
        if (ws->decls[i].id == id) {
            return &ws->decls[i];
        }
    }
    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static DeclRecord* decl_get_or_add(FlDockWorkspace* ws, FlDockPanelId id) {
    DeclRecord* existing = decl_find(ws, id);
    if (existing != nullptr) {
        return existing;
    }
    ws->decls = (DeclRecord*)dock_arena_array_reserve(ws->arena, ws->decls, ws->decl_count, &ws->decl_cap,
                                                      sizeof(DeclRecord), ws->decl_count + 1);
    DeclRecord* rec = &ws->decls[ws->decl_count++];
    rec->id = id;
    rec->last_declared = 0;
    rec->family = 0; // general until the app tags it
    rec->title = string_empty();
    rec->hidden = false; // visible until the app hides it
    rec->overflow_logs = 0;
    return rec;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Lifecycle

FlDockWorkspace* dock_workspace_create(void) {
    FlArena* arena = arena_new();
    FlDockWorkspace* ws = arena_alloc_zero(arena, FlDockWorkspace);
    ws->arena = arena;
    ws->tree = dock_tree_create();
    ws->frame = 1;
    return ws;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void dock_workspace_destroy(FlDockWorkspace* ws) {
    if (ws == nullptr) {
        return;
    }
    dock_drag_discard(&ws->drag); // a gesture still in flight owns a preview tree of its own
    dock_tree_destroy(ws->tree);  // the tree owns its own arena
    fl_arena_destroy(ws->arena);  // the workspace handle itself lives in this arena
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void dock_workspace_set_tree(FlDockWorkspace* ws, FlDockTree* tree) {
    if (tree == nullptr || tree == ws->tree) {
        return; // a failed dock_tree_build (null) or a redundant swap leaves the existing tree in place
    }
    dock_tree_destroy(ws->tree);
    ws->tree = tree;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Promotion - the headless subtree move between two in-process workspaces, built from the dock_tree_apply ops:
// Close extracts the panel from one tree's universe, Tab/Outer re-homes it in the other. Nothing copies a node or
// migrates per-panel state - the panel id is the only thing that moves, and the shared id-keyed widget store
// tracks it for free.

// Remove panel from tree's universe entirely - detaches and collapses an emptied non-root leaf.
static bool tree_extract_panel(FlDockTree* tree, FlDockPanelId panel) {
    FlDockRequest req = { .kind = FlDockRequest_Close, .panel = panel };
    return dock_tree_apply(tree, &req);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Dock a not-yet-present panel into tree: a tab of the root leaf when the root is a leaf, else an outer dock
// that splits the root.
static bool tree_dock_panel_at_root(FlDockTree* tree, FlDockPanelId panel) {
    if (tree->root->kind == FlDockNodeKind_Leaf) {
        FlDockRequest req = { .kind = FlDockRequest_Tab, .panel = panel, .target_node = tree->root->id };
        return dock_tree_apply(tree, &req);
    }
    FlDockRequest req = { .kind = FlDockRequest_Outer, .panel = panel, .side = FlDockSide_Right };
    return dock_tree_apply(tree, &req);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// True when tree is a fresh empty floor (single empty root leaf, no floating panels) - the only shape a promote
// target may take, so the promoted panel becomes the literal root.
static bool tree_is_empty_floor(const FlDockTree* tree) {
    return tree->root->kind == FlDockNodeKind_Leaf && tree->root->leaf.count == 0 && tree->floating_count == 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool dock_workspace_promote(FlDockWorkspace* src, FlDockWorkspace* dst, FlDockPanelId panel) {
    if (src == nullptr || dst == nullptr || src == dst) {
        return false;
    }
    if (!dock_tree_has_panel(src->tree, panel) || !tree_is_empty_floor(dst->tree)) {
        return false;
    }

    u32 family = dock_workspace_panel_family(src, panel); // carry the family tag so it survives the move

    tree_extract_panel(src->tree, panel); // a move: re-added to dst below
    FlDockRequest tab = { .kind = FlDockRequest_Tab, .panel = panel, .target_node = dst->tree->root->id };
    bool docked = dock_tree_apply(dst->tree, &tab);
    FL_ASSERT_FATAL(docked); // an empty-floor root leaf always accepts a tab

    dock_workspace_set_panel_family(dst, panel, family);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool dock_workspace_demote(FlDockWorkspace* src, FlDockWorkspace* dst, FlDockPanelId panel) {
    if (src == nullptr || dst == nullptr || src == dst) {
        return false;
    }
    if (!dock_tree_has_panel(dst->tree, panel)) {
        return false;
    }

    u32 family = dock_workspace_panel_family(dst, panel); // carry the family tag back

    tree_extract_panel(dst->tree, panel);
    bool docked = tree_dock_panel_at_root(src->tree, panel);
    FL_ASSERT_FATAL(docked); // a root leaf accepts a tab and a root split accepts an outer dock

    dock_workspace_set_panel_family(src, panel, family);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-panel attributes

void dock_workspace_set_panel_family(FlDockWorkspace* ws, FlDockPanelId id, u32 family) {
    decl_get_or_add(ws, id)->family = family;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u32 dock_workspace_panel_family(const FlDockWorkspace* ws, FlDockPanelId id) {
    const DeclRecord* rec = decl_find(ws, id);
    return rec != nullptr ? rec->family : 0; // an untagged (or never-declared) panel is general
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void dock_workspace_set_panel_visible(FlDockWorkspace* ws, FlDockPanelId id, bool visible) {
    decl_get_or_add(ws, id)->hidden = !visible;
    // Showing a CLOSED panel - one absent from the tree entirely - seats it on the floor.
    if (visible && !dock_tree_has_panel(ws->tree, id)) {
        tree_dock_panel_at_root(ws->tree, id);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool dock_workspace_panel_visible(const FlDockWorkspace* ws, FlDockPanelId id) {
    const DeclRecord* rec = decl_find(ws, id);
    return rec == nullptr || !rec->hidden; // a panel the app never hid is shown
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The active panel

void dock_workspace_set_active_panel(FlDockWorkspace* ws, FlDockPanelId id) {
    if (ws->active_panel == id) {
        return;
    }
    ws->active_panel = id;
    // Clear focus; a widget pressed inside the newly active panel re-claims it later in the same frame.
    if (g_ui_ctx != nullptr) {
        g_ui_ctx->focused_id = 0;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Persistence hooks. The serialize / load / rebind logic lives in dock_persist; these hooks add only the file I/O,
// routed through the settings store's directory + JSON/file substrate.

bool dock_workspace_save(FlDockWorkspace* ws, FlString settings_dir) {
    arena_scratch_auto(temp);

    FlDockPersistWorkspace pw = {
        .id = ws->id,
        .family_filter = ws->family_filter,
        .host_width = ws->host_width,
        .host_height = ws->host_height,
        .tree = ws->tree,
    };
    FlJsonValue* blob = dock_persist_to_json(&pw, 1, temp.arena);
    FlString json = fl_json_write_pretty(temp.arena, blob, "  ", "\n");
    if (json.length == 0) {
        return false;
    }

    file_create_directory_recursive_os(settings_dir);
    FlString path = fl_path_join(temp.arena, settings_dir, DOCK_LAYOUT_FILE);
    return file_write_os(path, (const u8*)json.data, (i64)json.length) >= 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool dock_workspace_load(FlDockWorkspace* ws, FlString settings_dir, const FlDockPanelId* known_panels,
                         u32 known_count) {
    arena_scratch_auto(temp);

    FlString path = fl_path_join(temp.arena, settings_dir, DOCK_LAYOUT_FILE);
    i64 size = file_get_size_os(path);
    if (size <= 0) {
        return false; // no saved layout - caller keeps its default
    }
    u8* buffer = arena_alloc_array(temp.arena, u8, (u64)size + 1);
    i64 read = file_read_os(buffer, path, size);
    if (read <= 0) {
        return false;
    }
    FlJsonValue* blob
        = fl_json_parse_with_flags(temp.arena, buffer, (size_t)read, FlJsonParseFlags_AllowJson5, nullptr);
    if (blob == nullptr) {
        return false;
    }

    FlDockPersistLoad load = dock_persist_from_json(blob, temp.arena);
    if (!load.ok || load.count == 0) {
        return false; // bad/old version or empty - caller keeps its default
    }

    // Reconcile against this single-window session: this workspace is the only live host, so a saved layout from a
    // multi-window session folds its other workspaces into ours. If the saved id does not match ours, adopt the
    // first saved workspace as the de-facto root.
    u64 live_id = ws->id;
    bool matches = false;
    for (u32 i = 0; i < load.count; ++i) {
        if (load.workspaces[i].id == ws->id) {
            matches = true;
            break;
        }
    }
    if (!matches) {
        live_id = load.workspaces[0].id;
    }
    dock_persist_reconcile(&load, &live_id, 1, known_panels, known_count);

    if (load.count == 0) {
        return false;
    }
    FlDockPersistWorkspace* chosen = &load.workspaces[0];
    ws->id = chosen->id;
    ws->family_filter = chosen->family_filter;
    ws->host_width = chosen->host_width;
    ws->host_height = chosen->host_height;
    dock_workspace_set_tree(ws, chosen->tree); // adopts ownership, destroys the previous tree
    chosen->tree = nullptr;

    // Defensive: reconcile against one live id leaves exactly one survivor, but never leak if that changes.
    for (u32 i = 1; i < load.count; ++i) {
        if (load.workspaces[i].tree != nullptr) {
            dock_tree_destroy(load.workspaces[i].tree);
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-frame declaration window

void dock_workspace_begin(FlDockWorkspace* ws) {
    // Drain deferred promote requests before this frame's emit walk. The queue is app-global and each request carries
    // its own source workspace; no window is mid-emit when another begins (fl_app ticks windows in sequence).
    dock_shared_flush_promotes();
    ws->frame++; // records not stamped with this generation read as not declared
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void dock_workspace_end(FlDockWorkspace* ws) {
    // The active id must name a panel that emits or is held in a drag - hidden, closed, reserved and background-tab
    // all drop it. Reservation is only decided once this frame's declarations are all in, so the check lands at the
    // bracket.
    bool in_flight
        = dock_workspace_panel_visible(ws, ws->active_panel) && dock_tree_panel_is_floating(ws->tree, ws->active_panel);
    if (ws->active_panel != 0
        && ((!dock_workspace_panel_should_emit(ws, ws->active_panel) && !in_flight)
            || dock_workspace_panel_reserved(ws, ws->active_panel))) {
        dock_workspace_set_active_panel(ws, 0);
    }

    // A return or break out of a dock_panel body skips the loop's update expression, leaking two open layout
    // elements into the rest of the frame; catch it at the bracket, in debug.
    FL_ASSERT_DEBUG(ws->open_panels == 0);
    ws->open_panels = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool dock_workspace_declare(FlDockWorkspace* ws, FlDockPanelId id, FlString title) {
    DeclRecord* rec = decl_get_or_add(ws, id);
    rec->last_declared = ws->frame;
    if (!fl_string_equals(rec->title, title)) { // copy only on change
        rec->title = fl_string_copy(ws->arena, title);
    }
    return dock_workspace_panel_should_emit(ws, id);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString dock_workspace_panel_title(const FlDockWorkspace* ws, FlDockPanelId id) {
    const DeclRecord* rec = decl_find(ws, id);
    return rec != nullptr ? rec->title : string_empty();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Queries

FlDockNodeId dock_workspace_panel_leaf(const FlDockWorkspace* ws, FlDockPanelId id) {
    FlDockNode* leaf = dock_tree_find_leaf_with_panel(ws->tree, id);
    return leaf != nullptr ? leaf->id : 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool dock_workspace_panel_declared(const FlDockWorkspace* ws, FlDockPanelId id) {
    const DeclRecord* rec = decl_find(ws, id);
    return rec != nullptr && rec->last_declared == ws->frame;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool dock_workspace_panel_reserved(const FlDockWorkspace* ws, FlDockPanelId id) {
    bool in_tree = dock_tree_find_leaf_with_panel(ws->tree, id) != nullptr;
    return in_tree && !dock_workspace_panel_declared(ws, id);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool dock_workspace_panel_should_emit(const FlDockWorkspace* ws, FlDockPanelId id) {
    if (!dock_workspace_panel_visible(ws, id)) {
        return false;
    }
    // A floating panel has no leaf: the drag overlay draws its title and its body draws nowhere.
    FlDockNode* leaf = dock_tree_find_leaf_with_panel(ws->tree, id);
    return leaf != nullptr && dock_workspace_leaf_shown_panel(ws, leaf) == id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u32 dock_workspace_panel_overflow_logs(const FlDockWorkspace* ws, FlDockPanelId id) {
    const DeclRecord* rec = decl_find(ws, id);
    return rec != nullptr ? rec->overflow_logs : 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlDockPanelId dock_workspace_leaf_shown_panel(const FlDockWorkspace* ws, const FlDockNode* leaf) {
    FlDockPanelId first_shown = 0;
    for_count(i, leaf->leaf.count) {
        FlDockPanelId panel = leaf->leaf.panels[i];
        if (!dock_workspace_panel_visible(ws, panel)) {
            continue;
        }
        if (i == leaf->leaf.selected) {
            return panel;
        }
        if (first_shown == 0) {
            first_shown = panel;
        }
    }
    return first_shown;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Render entry

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The fill element inside the drag overlay - the box that holds the dragged panel's title.
static FlLayoutId drag_overlay_fill_layout_id(FlDockPanelId panel) {
    return (FlLayoutId) { .name = FL_S("fl_dock_drag_overlay_fill"), .id = (u32)panel };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Overlay geometry in logical px. The pointer sits just inside the top-left, so the panel hangs off the cursor the
// way the grabbed tab did.
#define DOCK_DRAG_OVERLAY_W 280.0f
#define DOCK_DRAG_OVERLAY_H 180.0f
#define DOCK_DRAG_OVERLAY_GRAB_X 24.0f
#define DOCK_DRAG_OVERLAY_GRAB_Y 12.0f
// How strongly the in-flight overlay reads. Opaque enough to mark the panel as held, sheer enough that the layout
// underneath stays visible through it.
#define DOCK_DRAG_OVERLAY_TINT_ALPHA 96
#define DOCK_DRAG_OVERLAY_LABEL_PAD 12

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The panel in the user's hand: a translucent ghost carrying the panel's title, not its body. Untargeted it hangs
// off the cursor; over a resolved target it fills the gap the layout opened.
static void emit_drag_overlay(FlDockWorkspace* ws) {
    if (!dock_drag_active(&ws->drag) || !dock_workspace_panel_visible(ws, ws->drag.panel)) {
        return;
    }
    FlUiCtx* self = g_ui_ctx;
    const FlThemeColors* theme = ui_get_theme_colors(self);
    f32 scale = dock_ui_scale();
    // Clay boxes and the raw pointer are physical px; floating offsets are logical.
    FlDockRect free_rect
        = { self->input_state.mouse.x / scale - DOCK_DRAG_OVERLAY_GRAB_X,
            self->input_state.mouse.y / scale - DOCK_DRAG_OVERLAY_GRAB_Y, DOCK_DRAG_OVERLAY_W, DOCK_DRAG_OVERLAY_H };
    FlColor fill = ui_color_with_alpha(ui_color_lerp(theme->surface.elevated, theme->surface.selected, 0.35f),
                                       DOCK_DRAG_OVERLAY_TINT_ALPHA);

    // Over a resolved target the overlay attaches to the gap leaf's element; untargeted it hangs off the cursor.
    const bool on_gap = ws->drag.gap_leaf != 0;

    fl_floating_start((FlLayoutId) { .name = FL_S("fl_dock_drag_overlay"), .id = (u32)ws->drag.panel });
    fl_floating_configure(on_gap ? (FlFloatingConfig) {
        .attach_to = FlFloatingAttachTo_Element,
        .parent_id = dock_layout_node_element_id(ws->drag.gap_leaf),
        .pointer_capture = FlFloatingPointerCapture_Passthrough,
        .z_index = UI_Z_DOCK_DRAG_CHROME,
        .sizing = { sizing_grow(), sizing_grow() },
    } : (FlFloatingConfig) {
        .attach_to = FlFloatingAttachTo_Root,
        .pointer_capture = FlFloatingPointerCapture_Passthrough,
        .z_index = UI_Z_DOCK_DRAG_CHROME,
        .offset = { free_rect.x, free_rect.y },
        .sizing = { sizing_fixed(free_rect.width), sizing_fixed(free_rect.height) },
    });
    fl_layout_start(drag_overlay_fill_layout_id(ws->drag.panel));
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_grow(), sizing_grow() },
        .color = fill,
        .border = fl_border(theme->border.focus, 1),
        .radius = corner_radius(FlRadius_SM),
        .padding = padding_all(DOCK_DRAG_OVERLAY_LABEL_PAD),
    });
    ui_text(dock_workspace_panel_title(ws, ws->drag.panel));
    fl_layout_end();
    fl_layout_end();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The tab widget already gates pending_grab on the substrate's own click-vs-drag distance, so a dock threshold
// below that one would never be the binding one.
_Static_assert(FL_DOCK_DRAG_THRESHOLD >= UI_MOUSE_DRAG_THRESHOLD,
               "FL_DOCK_DRAG_THRESHOLD must be at least UI_MOUSE_DRAG_THRESHOLD");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The distance the cursor has travelled since the press, in LOGICAL px - dock constants are logical throughout,
// while the input field is physical. It is the press's max travel, not the current offset, so a jitter back toward
// the press point cannot disarm a drag mid-flight.
static bool drag_past_arm_threshold(void) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr) {
        return false;
    }
    f32 scale = dock_ui_scale();
    f32 travelled_sqr = self->input_state.mouse.drag_max_distance_sqr[FlMouseButton_Left] / (scale * scale);
    return travelled_sqr > FL_DOCK_DRAG_THRESHOLD * FL_DOCK_DRAG_THRESHOLD;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// End the gesture and leave the panel in a leaf; if its origin leaf is gone it is docked at the floor, so nothing is
// left in the floating set once the gesture is over.
static void dock_gesture_finish(FlDockWorkspace* ws, bool cancel) {
    FlDockPanelId dropped = ws->drag.panel;
    if (cancel) {
        dock_drag_cancel(&ws->drag, ws->tree);
    } else {
        dock_drag_end(&ws->drag, ws->tree);
    }
    if (dock_tree_panel_is_floating(ws->tree, dropped)) {
        tree_dock_panel_at_root(ws->tree, dropped);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The leaf whose tile contains x,y, or null. Clay's boxes are physical px and so is the raw pointer, so this
// needs no scale conversion; the boxes are the PREVIOUS frame's, like every other geometry read-back.
static const FlDockNode* leaf_at_point(const FlDockNode* node, f32 x, f32 y) {
    if (node == nullptr) {
        return nullptr;
    }
    if (node->kind == FlDockNodeKind_Split) {
        const FlDockNode* hit = leaf_at_point(node->split.child[0], x, y);
        return hit != nullptr ? hit : leaf_at_point(node->split.child[1], x, y);
    }
    Clay_ElementData data = Clay_GetElementData((Clay_ElementId) { .id = dock_layout_node_element_id(node->id) });
    if (!data.found) {
        return nullptr;
    }
    Clay_BoundingBox box = data.boundingBox;
    bool inside = x >= box.x && x < box.x + box.width && y >= box.y && y < box.y + box.height;
    return inside ? node : nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A press inside a tile activates that tile's shown panel. A modal blocks it; a popup does not.
static void activate_panel_under_press(FlDockWorkspace* ws) {
    FlUiCtx* self = g_ui_ctx;
    if (self == nullptr || self->modal_scope_id != 0 || !ui_mouse_pressed(FlMouseButton_Left)) {
        return;
    }
    const FlDockNode* leaf = leaf_at_point(ws->tree->root, self->input_state.mouse.x, self->input_state.mouse.y);
    if (leaf == nullptr) {
        return;
    }
    FlDockPanelId panel = dock_workspace_leaf_shown_panel(ws, leaf);
    if (panel != 0) {
        dock_workspace_set_active_panel(ws, panel);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void dock_workspace_emit(FlDockWorkspace* ws) {
    if (g_ui_ctx == nullptr) {
        return; // the walk emits Clay elements, which need a live context
    }

    // Structural gestures touch the tree only here, before this frame's walk.
    //
    // Start: a tab the PREVIOUS frame's header reported as pulled out becomes an undock-and-drag, so
    // dock_drag_begin_undock snapshots the geometry that walk produced. The undock arms only past the travel
    // threshold and only while the button is still down.
    FlDockPanelId grab = ws->pending_grab;
    ws->pending_grab = 0;
    bool held = ui_mouse_down(FlMouseButton_Left);
    if (!held) {
        ws->grab_blocked = false;
    }
    if (grab != 0 && !ws->grab_blocked && !dock_drag_active(&ws->drag) && held && drag_past_arm_threshold()) {
        dock_drag_begin_undock(&ws->drag, ws->tree, grab, dock_workspace_panel_family(ws, grab), ws->family_filter);
    }

    // End: the dragged tab is gone from its strip by now, so the button - not a per-item signal - ends the gesture.
    // Cancel is one path with two triggers: Escape and a release outside the workspace, which resolves to no zone.
    if (dock_drag_active(&ws->drag)) {
        if (ui_key_pressed(FlKey_Escape)) {
            dock_gesture_finish(ws, /*cancel=*/true);
            ws->grab_blocked = true;
        } else if (!held) {
            dock_gesture_finish(ws, /*cancel=*/false);
        }
    }

    if (dock_drag_active(&ws->drag)) {
        // The drag layer tiles in place of the plain walk: the reserved-slot preview over the resolved target.
        dock_drag_emit(&ws->drag, ws->tree);
    } else {
        dock_layout_emit(ws->tree, &ws->pending_grab);
    }
    // After the walk: a press on a background tab re-selects it there, so the shown panel this reads is the one the
    // press just chose, not the one it replaced.
    activate_panel_under_press(ws);
    emit_drag_overlay(ws);

    // This frame's tiles are now in the layout, so a panel declared after this point has something to attach to.
    ws->emitted_frame = ws->frame;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlVec2 dock_workspace_panel_size(const FlDockWorkspace* ws, FlDockPanelId id) {
    // A panel that is not emitting has no room to report: absent, hidden, floating, or a background tab.
    if (!dock_workspace_panel_should_emit(ws, id)) {
        return (FlVec2) { 0.0f, 0.0f };
    }
    // The leaf's CONTENT area - the tile below the tab header.
    FlDockNode* leaf = dock_tree_find_leaf_with_panel(ws->tree, id);
    Clay_ElementData data = Clay_GetElementData((Clay_ElementId) { .id = dock_layout_content_element_id(leaf->id) });
    if (!data.found) {
        return (FlVec2) { 0.0f, 0.0f };
    }
    f32 scale = dock_ui_scale();
    // Clay boxes are physical px; every public geometry read-back is logical (authoring) px.
    FlVec2 size = { data.boundingBox.width / scale, data.boundingBox.height / scale };
    // An emitting panel whose tile is starved past the content floor also reports {0,0}.
    if (size.x < FL_DOCK_MIN_CONTENT_WIDTH || size.y < FL_DOCK_MIN_CONTENT_HEIGHT) {
        return (FlVec2) { 0.0f, 0.0f };
    }
    return size;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Macro glue - resolve the active window's workspace from its per-window binding.

FlDockWorkspace* dock_workspace_current(void) {
    FlUiCtx* ctx = ui_get_ctx();
    // Every fl_dock_* entry point resolves through here: calling the dock outside a live UI context is fatal.
    FL_ASSERT_FATAL(ctx != nullptr && ctx->dock_workspace != nullptr);
    return ctx->dock_workspace; // non-owning; the registry default at ui_create, rebound for promoted windows
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The clip container a panel's body lives in - the element whose viewport is the tile and whose content is
// whatever the body laid out at.
static FlLayoutId panel_clip_layout_id(FlDockPanelId id) {
    return (FlLayoutId) { .name = FL_S("fl_dock_panel_clip"), .id = (u32)id };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A body larger than its tile is clipped, so the overflow is reported once instead. Reads the previous frame's
// extent, like every other geometry read-back.
static void report_overflow_once(FlDockWorkspace* ws, FlDockPanelId id) {
    DeclRecord* rec = decl_find(ws, id);
    FL_ASSERT(rec != nullptr); // the declare this call sits behind added it
    if (rec->overflow_logs > 0) {
        return;
    }
    FlScrollContainerExtent extent = ui_get_scroll_container_extent(ui_layout_id_to_clay_id(panel_clip_layout_id(id)));
    // Half a physical pixel of slack: an exactly-filling body must not read as overflow.
    if (!extent.found
        || (extent.content_extent.x <= extent.viewport_size.x + 0.5f
            && extent.content_extent.y <= extent.viewport_size.y + 0.5f)) {
        return;
    }
    rec->overflow_logs++;
    log_error("dock_panel(%llu): body %.0fx%.0f overflows its %.0fx%.0f tile and is clipped — size the content "
              "from fl_dock_panel_size, not from the body's own rect",
              (unsigned long long)id, (double)extent.content_extent.x, (double)extent.content_extent.y,
              (double)extent.viewport_size.x, (double)extent.viewport_size.y);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Open a declared panel's body INSIDE the tile it is bound to. The tile was emitted earlier this frame by
// dock_workspace_emit, so the body cannot nest inline - instead it is a Clay floating element attached by id to
// that tile's content area, sized to grow so it fills the attached element exactly.
bool dock_panel_scope_begin(FlDockPanelId id, FlString title) {
    FlDockWorkspace* ws = dock_workspace_current();
    if (!dock_workspace_declare(ws, id, title)) {
        return false;
    }

    // No tiles this frame means the workspace has not been emitted yet, and attaching to a missing parent is fatal
    // in Clay. The gate is the workspace's own frame stamp, never a Clay element probe: Clay's element hashmap
    // outlives the frame that filled it, so it answers "found" for last frame's tile.
    if (ws->emitted_frame != ws->frame) {
        if (!ws->warned_missing_tile) {
            ws->warned_missing_tile = true;
            log_error("dock_panel(%llu): no tile to draw into — call dock_workspace_emit before declaring panels",
                      (unsigned long long)id);
        }
        return false;
    }

    // declare returned true, so the panel is the shown tab of a leaf.
    FlDockNode* leaf = dock_tree_find_leaf_with_panel(ws->tree, id);
    FL_ASSERT_FATAL(leaf != nullptr);
    u32 parent = dock_layout_content_element_id(leaf->id);

    report_overflow_once(ws, id);

    ws->open_panels++; // balance ledger; dock_workspace_end asserts it drained
    fl_floating_start((FlLayoutId) { .name = FL_S("fl_dock_panel"), .id = (u32)id });
    fl_floating_configure((FlFloatingConfig) {
        .attach_to = FlFloatingAttachTo_Element,
        .parent_id = parent,
        .z_index = UI_Z_DOCK_CONTENT,
        .sizing = { sizing_grow(), sizing_grow() },
    });
    // Clip to the tile. A floating element is sized to its attach parent, but Clay never shrinks a child below its
    // content's minimum, so a body wanting more room than its tile has would paint over its neighbours. Manual
    // scroll at the origin makes this a pure clip.
    fl_layout_start(panel_clip_layout_id(id));
    fl_layout_configure((FlLayoutConfig) {
        .sizing = { sizing_grow(), sizing_grow() },
        .clip = { .horizontal = true, .vertical = true, .manual_scroll = true },
    });
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void dock_panel_scope_end(void) {
    FlDockWorkspace* ws = dock_workspace_current();
    if (ws->open_panels > 0) {
        ws->open_panels--;
    }
    fl_layout_end(); // the clip container
    fl_layout_end(); // the floating element attached to the tile
}
