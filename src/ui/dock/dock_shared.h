#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Dock Shared - the app-global docking state above the per-workspace layer (dock_workspace.h): the registry of dock
// workspaces (a per-window FlUiCtx points at the one it hosts), the injected OS-window host seam, and the deferred
// promote queue plus the live promotion records. One per app, owned by UiShared.
//
// flowi owns the promote DECISION, the workspace<->window LIFECYCLE, and demote-on-close; the runner provides the
// window MECHANICS through the injected vtable. flowi never opens a window itself and never sees a global
// coordinate (on Wayland the compositor places the window).
//
// The record is opaque; the entry-0 accessor and the promote/notify API below are the whole surface.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "dock_tree.h"
#include "dock_workspace.h"

#ifdef __cplusplus
extern "C" {
#endif

// Upper bound on live dock workspaces - one per window, so it tracks fl_app's window cap (FL_APP_MAX_WINDOWS = 16).
#define FL_DOCK_MAX_WORKSPACES 16

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// OS-window promotion seam. The host opens/closes windows; flowi decides when.

typedef struct FlDockHost {
    void* user_data;
    // Open an OS window hosting ws (rendered via the app's dock frame fn the host already holds). Returns an
    // opaque host handle identifying the window (used as the demote-on-close key), or null on failure.
    void* (*open_promoted_window)(void* ud, FlDockWorkspace* ws, f32 w, f32 h);
    // Close a window previously returned by open_promoted_window (the flowi-initiated teardown path; the
    // user/WM-initiated path runs through fl_dock_notify_window_closed instead).
    void (*close_promoted_window)(void* ud, void* host_window);
} FlDockHost;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FlDockShared FlDockShared;

FlDockShared* dock_shared_create(void);
void dock_shared_destroy(FlDockShared* ds);

// The default (main-window) workspace - registry entry 0, created with the tier. ui_create binds a fresh context's
// dock_workspace to this.
FlDockWorkspace* dock_shared_default(FlDockShared* ds);

// Register the host that provides window mechanics for promotion (stored in the shared tier; pass a zeroed host to
// clear). fl_app_create registers one; a bare flowi app may register its own or none. With no
// host, dock_promote_panel is a no-op.
void fl_dock_register_host(FlDockHost host);

// Request promoting panel (in the CURRENT window's workspace) into its own OS window. Deferred: enqueued now and
// applied at the next dock_shared_flush_promotes safe point (never mid-walk) - create a second workspace, move the
// panel's subtree into it (dock_workspace_promote), then ask the host to open a window hosting it. No-op if no host
// is registered, if the panel is absent, or if the registry is full.
void dock_promote_panel(FlDockPanelId panel);

// Drain the deferred promote queue. Called from dock_workspace_begin - the docking safe point, never mid-walk, so a
// source tree may be mutated here even though that source is about to emit. No-op without a shared tier.
void dock_shared_flush_promotes(void);

// The host calls this when a promoted window closes (user / window-manager / injected close): demote the panel back
// into its source workspace by stable id (degrading to the floor/root rule if the old leaf is gone), then dissolve
// the second workspace and drop its registry entry. host_window is the opaque handle open_promoted_window
// returned. No-op if the handle is unknown.
void fl_dock_notify_window_closed(void* host_window);

#ifdef __cplusplus
}
#endif
