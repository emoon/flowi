#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dock Workspace - a logical dock tree together with the per-frame panel-declaration bookkeeping that binds
// immediately-declared Detachable Panels to the retained leaves they live in (CONTEXT.md "Workspace",
// "Detachable Panel"). The app re-declares each panel every frame by its stable id, and the workspace binds that
// id to the leaf already holding it.
//
// The tree is the retained source of truth (topology + ratios; dock_tree.{h,c}); the workspace adds only the
// transient per-frame layer:
//   - declared-this-frame tracking, so a panel present in the tree but NOT declared this frame keeps its slot
//     reserved (the tree is never mutated on absence - this just makes the reservation queryable);
//   - the render decision: a panel's content emits this frame iff it is bound to a leaf AND it is that leaf's
//     selected tab. dock_panel(id, title){...} (ui/dock.h) is the app-facing scope that consults this.
//
// The set of workspaces lives in the shared tier (dock_shared.h) so it is app-global and a panel's state
// survives a promote/demote; a single-window app has exactly one (registry entry 0). Each tree belongs to one
// workspace hosted by one window - it does not span windows. Plain fields are read and written directly; the
// functions below are the operations that are more than a field access.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <flowi/core/string.h>

#include "dock_drag.h"
#include "dock_tree.h"

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A workspace owns its tree and a per-panel record of the last frame each panel was declared: "declared this
// frame" is last_declared == frame.

typedef struct DeclRecord {
    FlDockPanelId id;
    u64 last_declared; // the frame value at the most recent dock_workspace_declare(id)
    u32 family;        // the panel's dock-family tag (0 = general); gates which workspaces accept it
    FlString title;    // the tab's label, copied on change
    bool hidden;       // set by dock_workspace_set_panel_visible; no tab, no content, slot kept
    u32 overflow_logs; // overflow errors emitted for this panel's body; the report path emits at most one
} DeclRecord;

typedef struct FlDockWorkspace {
    FlArena* arena;
    FlDockTree* tree;
    u64 frame;         // bumped each dock_workspace_begin; starts at 1, so a record's 0 never reads as declared
    u64 emitted_frame; // the frame value at the most recent dock_workspace_emit; == frame means the tiles are live
                       // and 0 means never emitted
    DeclRecord* decls;
    u32 decl_count;
    u32 decl_cap;
    // Persisted attributes. The host SIZE is persisted; the OS window POSITION never is (Wayland cannot restore it).
    u64 id;            // the host this workspace belongs to, so a multi-window layout reconciles on load; 0 = single
    u32 family_filter; // accepted dock family, 0 = any; enforced in hit_test_drop_zones (dock_drag.h)
    f32 host_width;    // logical px; 0,0 = unknown
    f32 host_height;
    // pending_grab is written during the emit walk (a tab pulled out of its header) and consumed at the NEXT
    // frame's safe point, before the walk.
    FlDockDrag drag;
    FlDockPanelId pending_grab;
    // Set when Escape cancels a drag, suppressing the grab the tab strip keeps re-reporting until the button
    // comes up.
    bool grab_blocked;
    // One-shot latch for the declare-without-emit diagnostic: at most one message however many frames it repeats.
    bool warned_missing_tile;
    // The panel a press last activated. Only dock_workspace_set_active_panel writes it, and every change it makes
    // blurs the keyboard focus ring.
    FlDockPanelId active_panel;
    // Open dock_panel scopes. A scope opens two layout elements, so a return or break out of the body leaves them
    // open and desyncs the frame's layout stack; dock_workspace_end asserts this drained to 0.
    u32 open_panels;
} FlDockWorkspace;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Lifecycle. A fresh workspace owns a fresh dock tree (a single empty root leaf - the floor).

FlDockWorkspace* dock_workspace_create(void);
void dock_workspace_destroy(FlDockWorkspace* ws);

// Replace the workspace's dock tree with tree, destroying the previous tree and adopting ownership of tree.
// The path an app installs an authored first-run layout: build + validate one with dock_tree_build, then hand it
// here. Per-frame declaration records are unaffected - they key off the stable panel id. A null tree (or tree
// already installed) is a no-op, so a failed dock_tree_build keeps the existing floor.
void dock_workspace_set_tree(FlDockWorkspace* ws, FlDockTree* tree);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Promotion - the headless half of detaching a panel to its own window, WITHOUT the OS seam. Promoting a panel is
// a SUBTREE MOVE BETWEEN TWO IN-PROCESS WORKSPACES, not a copy: the panel leaves its source workspace's tree
// entirely and re-hosts as the root of a second workspace, conserving the panel id across the pair. No node is
// copied and no per-panel state is migrated - the shared id-keyed widget store follows the stable id. The unit
// moved is a single Detachable Panel (a one-panel leaf).

// Move panel out of src and re-host it as the sole tab of dst's root leaf. dst must be an empty floor - a
// freshly-created second workspace - so the promoted panel becomes its root. The panel's dock-family tag travels
// with it. Both trees stay invariant-clean and the panel id is conserved across the pair (it leaves src's universe
// and enters dst's - never in both, never lost). Returns false with no change if panel is absent from src,
// dst is not an empty floor, or src == dst.
bool dock_workspace_promote(FlDockWorkspace* src, FlDockWorkspace* dst, FlDockPanelId panel);

// The inverse of dock_workspace_promote: move panel out of dst and re-dock it into src, rebinding by its
// stable id (the panel's scroll/text/tab state is intact). It re-enters as a tab of src's root leaf when that
// root is a leaf, else as an outer dock of src's root. The family tag travels back; dst returns to an empty
// floor when panel was its only content. Returns false with no change if panel is absent from dst or
// src == dst.
bool dock_workspace_demote(FlDockWorkspace* src, FlDockWorkspace* dst, FlDockPanelId panel);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-panel attributes.

// A Detachable Panel's dock-family tag (0 = general; CONTEXT.md "Detachable Panel"). The tag gates which workspaces
// accept the panel via the family filter above. Stored per panel id in the same per-panel record the declaration
// window keys off, so it survives across frames and a promote/demote; an untagged panel reads back 0 (general).
// Not persisted with the layout; the app re-asserts it each session.
void dock_workspace_set_panel_family(FlDockWorkspace* ws, FlDockPanelId id, u32 family);
u32 dock_workspace_panel_family(const FlDockWorkspace* ws, FlDockPanelId id);

// A Detachable Panel's visibility. A hidden panel keeps its slot and tab index in the tree but renders no tab and
// emits no content, and a leaf whose panels are all hidden is not emitted (dock_layout). Stored in
// the same per-panel record as the family tag, so a panel the workspace has never seen reads back visible. Not
// persisted; the app re-asserts it each session.
//
// Showing a panel that is absent from the tree - closed, or never docked - seats it on the floor, the same re-home
// every op falls back to.
void dock_workspace_set_panel_visible(FlDockWorkspace* ws, FlDockPanelId id, bool visible);
bool dock_workspace_panel_visible(const FlDockWorkspace* ws, FlDockPanelId id);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The active panel - one panel id per workspace, set by a press inside a tile and never by hover. The setter is the
// only writer, and every change it makes blurs the keyboard focus ring. Not persisted, and no chrome renders it.
//
// The id must always name a panel that emits: dock_workspace_end drops it to 0 once the active panel is hidden,
// closed, reserved or a background tab. A panel a gesture has in flight keeps it - a dragged panel still emits.

void dock_workspace_set_active_panel(FlDockWorkspace* ws, FlDockPanelId id);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Persistence hooks. Save serializes this workspace's tree + family filter + host size + version to an
// opaque JSON blob written into settings_dir as dock_layout.json. Load reads it back, applies the
// rebind-on-load rules against this session (see dock_persist.h), and adopts the restored tree + attributes.
// known_panels is the roster of panel ids this session intends to declare: a roster panel absent from the saved
// tree is docked in, and a saved panel the roster does not name is closed. Pass null/0 for no
// roster - then nothing is pruned and every saved panel is kept.
//
// Save returns false on a serialization/write failure. Load returns false when there is no saved layout or the
// blob is a bad version - the caller then keeps its code-defined default layout.

bool dock_workspace_save(FlDockWorkspace* ws, FlString settings_dir);
bool dock_workspace_load(FlDockWorkspace* ws, FlString settings_dir, const FlDockPanelId* known_panels,
                         u32 known_count);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Per-frame declaration window. Bracket each frame's panel declarations with begin/end: begin opens a fresh
// "declared this frame" generation, so panels not re-declared this frame fall back to reserved.

void dock_workspace_begin(FlDockWorkspace* ws);
void dock_workspace_end(FlDockWorkspace* ws);

// Mark id declared this frame under title and return whether its content should emit now - true iff id is
// bound to a leaf AND is that leaf's selected tab. This is the primitive the dock_panel(id, title) scope macro
// consults; tests call it directly. Must be called between begin and end. title is copied into the panel's
// record; pass the empty string for an untitled panel.
bool dock_workspace_declare(FlDockWorkspace* ws, FlDockPanelId id, FlString title);

// The tab label id was last declared under, or empty. The title is retained, not borrowed per frame: the emit
// walk runs BEFORE this frame's declarations, and a reserved panel has no declaration at all.
FlString dock_workspace_panel_title(const FlDockWorkspace* ws, FlDockPanelId id);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Queries (binding + reservation). Pure reads - no per-frame side effects.

// The id of the leaf node currently holding id, or 0 ("no node") if id is floating or absent from the tree.
FlDockNodeId dock_workspace_panel_leaf(const FlDockWorkspace* ws, FlDockPanelId id);

// True if id was declared during the current frame's begin/end window.
bool dock_workspace_panel_declared(const FlDockWorkspace* ws, FlDockPanelId id);

// True if id has a slot in the tree (sits in some leaf) but was NOT declared this frame - i.e. its slot is
// reserved (kept, not collapsed) pending a future declaration.
bool dock_workspace_panel_reserved(const FlDockWorkspace* ws, FlDockPanelId id);

// True if id's content should emit this frame: bound to a leaf AND the tab that leaf shows as selected, and
// not hidden. A floating (mid-drag) panel never emits. Independent of whether it was declared (a leaf's selected tab is
// selected whether or not the app declared it this frame).
bool dock_workspace_panel_should_emit(const FlDockWorkspace* ws, FlDockPanelId id);

// How many overflow errors have been emitted for id's body - at most one per panel, however many frames it
// overflows for.
u32 dock_workspace_panel_overflow_logs(const FlDockWorkspace* ws, FlDockPanelId id);

// The panel leaf shows as its selected tab: its logical selection when that panel is visible, else the first
// visible panel in tab order; 0 when every panel in the leaf is hidden. The logical selection is never rewritten,
// so re-showing a hidden panel restores the leaf exactly.
FlDockPanelId dock_workspace_leaf_shown_panel(const FlDockWorkspace* ws, const FlDockNode* leaf);

// The room id's body has to lay out in, in logical px, read back from the previous frame's geometry. {0,0} when
// the panel is not emitting, has no measured tile yet, or is starved past the content floor.
FlVec2 dock_workspace_panel_size(const FlDockWorkspace* ws, FlDockPanelId id);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Render entry. Emit the workspace's tile geometry for this frame by walking the tree (delegates to the dock
// layout emit; Clay computes every rect). Call inside a UI frame (between ui_update_begin and ui_update_end).
// The selected panel's content is emitted by the app via dock_panel(id, title){...}. The workspace is non-const:
// the emit walk drives interaction (divider drag → splitter-resize op; tab click → re-select op), both applied
// through dock_tree_apply.
//
// This is also where drag-to-dock runs - there is no drag API to call. Pulling a tab FL_DOCK_DRAG_THRESHOLD px out
// of its leaf header undocks the panel and arms the drag; while it is live the provisional tree replaces the plain
// tiles and makes space where the panel would land, and an overlay carrying the panel's title fills that gap; the
// release commits the drop, while Escape or a release outside the workspace cancels it (dock_drag.h). Every
// structural step happens HERE, before the walk. The dragged panel does not emit while it is in flight: its body
// draws nowhere until the gesture ends.

void dock_workspace_emit(FlDockWorkspace* ws);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Macro glue (ui/dock.h). Resolve the active window's workspace from its per-window binding (ctx->dock_workspace,
// which points into the shared registry).

FlDockWorkspace* dock_workspace_current(void);

// dock_panel scope begin: declare id against the current workspace under title, returning whether to run the
// body. On true it also OPENS the panel's content element - a floating element attached to the tile id is bound
// to - so the body's widgets land inside that tile.
bool dock_panel_scope_begin(FlDockPanelId id, FlString title);

// dock_panel scope end: close the element opened by a scope_begin that returned true. Calling it after a
// scope_begin that returned false closes somebody else's element - the dock_panel macro's latch is what
// guarantees the pairing.
void dock_panel_scope_end(void);

#ifdef __cplusplus
}
#endif
