#pragma once

// Dock Shared: the app-global docking state above the per-workspace layer (dock_workspace.h): the registry of dock
// workspaces (a per-window FlUiCtx points at the one it hosts), the injected OS-window host seam, and the deferred
// promote queue plus the live promotion records. One per app, owned by UiShared.
//
// flowi decides when to promote, owns the workspace<->window lifecycle and demotes on close. The runner opens and
// closes the windows through the injected vtable. flowi never opens a window itself and never sees a global
// coordinate (on Wayland the compositor places the window).
//
// The record is opaque; the entry-0 accessor and the promote/notify API below are the whole surface.

#include "dock_tree.h"
#include "dock_workspace.h"

#ifdef __cplusplus
extern "C" {
#endif

// Upper bound on live dock workspaces. There is one per window, so it tracks fl_app's window cap
// (FL_APP_MAX_WINDOWS = 16).
#define FL_DOCK_MAX_WORKSPACES 16

// OS-window promotion seam. The host opens/closes windows; flowi decides when.

typedef struct FlDockHost {
    void* user_data;
    // Open an OS window hosting ws (rendered via the app's dock frame fn the host already holds). Returns an
    // opaque host handle identifying the window (used as the demote-on-close key), or nullptr on failure. The host
    // may report another promoted window closed from inside this call, but not the window whose frame is running.
    void* (*open_promoted_window)(void* ud, FlDockWorkspace* ws, f32 w, f32 h);
    // Close a window previously returned by open_promoted_window (the flowi-initiated teardown path; the
    // user/WM-initiated path runs through fl_dock_notify_window_closed instead).
    void (*close_promoted_window)(void* ud, void* host_window);
} FlDockHost;

typedef struct FlDockShared FlDockShared;

FlDockShared* dock_shared_create(void);
void dock_shared_destroy(FlDockShared* ds);

// The default (main-window) workspace: registry entry 0, created with the tier. ui_create binds a fresh context's
// dock_workspace to this.
FlDockWorkspace* dock_shared_default(FlDockShared* ds);

// Register the host that provides window mechanics for promotion (stored in the shared tier; pass a zeroed host to
// clear). fl_app_create registers one; a bare flowi app may register its own or none. With no
// host, dock_promote_panel is a no-op.
void fl_dock_register_host(FlDockHost host);

// Request promoting panel (in the current window's workspace) into its own OS window. Deferred: enqueued now and
// applied at the next dock_shared_flush_promotes safe point (never mid-walk) at which that workspace has no drag in
// flight. Applying it creates a second workspace, moves the panel's subtree into it (dock_workspace_promote), then
// asks the host to open a window hosting it. No-op if no host is registered, if the panel is not docked (absent, or
// mid-drag), or if the registry is full.
void dock_promote_panel(FlDockPanelId panel);

// Apply the deferred promote requests. Called from dock_workspace_begin, the docking safe point outside any walk,
// so a source tree may be mutated here even though that source is about to emit. A request whose source workspace
// has a drag in flight stays queued for a later call. No-op without a shared tier.
void dock_shared_flush_promotes(void);

// The host calls this when a promoted window closes (user / window-manager / injected close): demote every panel the
// window holds back into its source workspace by stable id, then dissolve the second workspace and drop its registry
// entry. A drag in flight in the source workspace is cancelled. A panel promoted onward out of this window now demotes
// into this window's source. host_window is the opaque handle open_promoted_window returned. No-op if the handle is
// unknown.
void fl_dock_notify_window_closed(void* host_window);

#ifdef __cplusplus
}
#endif
