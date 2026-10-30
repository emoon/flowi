#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dock Persistence - serialize workspace layouts to an opaque JSON blob and restore them across sessions
// (CONTEXT.md "Workspace", "Docking").
//
// Persisted per workspace: the tree (topology, split axes, ratios, each leaf's panel-id list and selected tab),
// the dock-family filter, and the host SIZE - never the position, which Wayland cannot restore. The blob also
// carries a top-level version field.
//
// Parsing and reconciling are separate steps: dock_persist_from_json parses a blob, dock_persist_reconcile
// applies the rebind-on-load rules against this session's live workspaces and declared panels. The file
// read/write lives in the dock_workspace hooks, not here.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "dock_tree.h"

#ifdef __cplusplus
extern "C" {
#endif

struct FlArena;
struct FlJsonValue;

// Blob format version. Bump on any incompatible change to the serialized shape; a mismatch triggers the
// bad-version-to-default rebind rule.
#define FL_DOCK_PERSIST_VERSION 1

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// One workspace's persisted state.

typedef struct FlDockPersistWorkspace {
    u64 id;            // identifies the host this workspace belongs to (0 = unset / single-window default)
    u32 family_filter; // per-workspace dock-family filter (0 = general)
    f32 host_width;    // host size only - the OS window position is never persisted
    f32 host_height;
    FlDockTree* tree; // the tree (borrowed on save; owned by the caller on load - dock_tree_destroy)
} FlDockPersistWorkspace;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialize count workspaces into an opaque JSON blob allocated from arena. The trees are read, never
// mutated or freed. Returns null only if workspaces is null (a zero count yields an empty-workspaces blob).

struct FlJsonValue* dock_persist_to_json(const FlDockPersistWorkspace* workspaces, u32 count, struct FlArena* arena);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Result of loading a blob. workspaces (and the array itself) are allocated from the arena passed to
// dock_persist_from_json, but each .tree is a standalone FlDockTree owning its own arena - release every
// surviving tree with dock_tree_destroy. ok is false on a version mismatch or a malformed blob.

typedef struct FlDockPersistLoad {
    bool ok;
    FlDockPersistWorkspace* workspaces;
    u32 count;
} FlDockPersistLoad;

// Parse a blob produced by dock_persist_to_json. Validates each restored tree through the dock_tree invariant
// chokepoint; if the version differs or any tree is malformed, returns {.ok = false} after releasing whatever it
// built (so the caller never adopts a holed tree).
FlDockPersistLoad dock_persist_from_json(const struct FlJsonValue* blob, struct FlArena* arena);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Apply the rebind-on-load rules to a freshly-loaded layout, reconciling the saved world against this session:
//
//   - dangling-workspace-reparent - any loaded workspace whose id is not in live_ws_ids is dissolved; its
//     docked panels are reparented into the surviving root workspace and its loose panels are parked there. Its
//     tree is destroyed and the array is compacted.
//   - stale-panel-prune - when declared_count is non-zero, any panel in a surviving workspace that is
//     not in declared is closed, one info line per drop. A zero declared_count prunes nothing.
//   - unknown-panel-to-default - any id in declared not present in any surviving workspace is added to the
//     surviving root workspace.
//   - floating-drains-on-load - last: every surviving workspace's floating set is docked into that workspace's
//     root region, so reconcile always returns with no panel parked.
//
// The "surviving root workspace" is the first loaded workspace whose id is live (or, defensively, index 0 if none
// matched). Mutates load in place (may shrink count). A no-op when `load->ok` is false or count is 0.
// live_ws_ids/declared may be null when their counts are 0.
void dock_persist_reconcile(FlDockPersistLoad* load, const u64* live_ws_ids, u32 live_count,
                            const FlDockPanelId* declared, u32 declared_count);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Structural equality of two trees: same topology, split axes, ratios (within a small epsilon - ratios round-trip
// through JSON floats), leaf panel lists + selected tab, and floating sets (order-sensitive). Ignores node ids.

bool dock_persist_tree_equal(const FlDockTree* a, const FlDockTree* b);

#ifdef __cplusplus
}
#endif
