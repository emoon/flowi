// Dock Shared - the app-global workspace registry, the injected host seam, and the deferred promote queue. Reached
// from the entry points below via g_ui_ctx->shared->dock.

#include "dock_shared.h"

#include <core/arena.h>
#include <core/log.h>

#include "../ui_private.h"

// Default promoted-window size in logical pixels.
#define DOCK_PROMOTED_DEFAULT_W 480.0f
#define DOCK_PROMOTED_DEFAULT_H 320.0f

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A deferred promote request: move panel out of src into a fresh second workspace, then have the host open a
// window for it. Captured at dock_promote_panel, drained at the next dock_shared_flush_promotes.
typedef struct DockPromoteRequest {
    FlDockWorkspace* src;
    FlDockPanelId panel;
} DockPromoteRequest;

// A live promotion: a panel hosted in its own window. host_window is the host's opaque handle (the close key);
// dst is the second workspace hosting the subtree; src is where the panel demotes back to on close.
typedef struct DockPromotion {
    void* host_window;
    FlDockWorkspace* dst;
    FlDockWorkspace* src;
    FlDockPanelId panel;
} DockPromotion;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FlDockShared {
    FlArena* arena; // owns this record itself; freed in dock_shared_destroy

    // The workspace registry: the app-global set of trees. [0] is the default (main) workspace, created with the
    // tier; promoting adds another and demote dissolves it. Each entry is owned here; per-window contexts only
    // reference an entry (ctx->dock_workspace).
    FlDockWorkspace* registry[FL_DOCK_MAX_WORKSPACES];
    u32 registry_count;

    FlDockHost host; // injected window mechanics (zeroed = none registered; promote is then a no-op)

    DockPromoteRequest promote_queue[FL_DOCK_MAX_WORKSPACES];
    u32 promote_count;

    DockPromotion promotions[FL_DOCK_MAX_WORKSPACES];
    u32 promotion_count;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Lifecycle

FlDockShared* dock_shared_create(void) {
    FlArena* arena = arena_new();
    FlDockShared* ds = arena_alloc_zero(arena, FlDockShared);
    ds->arena = arena;
    ds->registry[0] = dock_workspace_create();
    ds->registry_count = 1;
    return ds;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void dock_shared_destroy(FlDockShared* ds) {
    if (ds == nullptr) {
        return;
    }
    for (u32 i = 0; i < ds->registry_count; ++i) {
        dock_workspace_destroy(ds->registry[i]);
    }
    fl_arena_destroy(ds->arena); // frees the record itself (allocated from this arena)
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlDockWorkspace* dock_shared_default(FlDockShared* ds) {
    return (ds != nullptr && ds->registry_count > 0) ? ds->registry[0] : nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The registry

// Null when there is no shared tier - a standalone workspace driven directly by a unit test - so the entry points
// below must no-op safely on it.
static FlDockShared* dock_shared_get(void) {
    FlUiCtx* ctx = ui_get_ctx();
    if (ctx == nullptr || ctx->shared == nullptr) {
        return nullptr;
    }
    return ctx->shared->dock;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlDockWorkspace* dock_registry_add(FlDockShared* ds) {
    if (ds->registry_count >= FL_DOCK_MAX_WORKSPACES) {
        log_error("dock: workspace registry full (%d)", FL_DOCK_MAX_WORKSPACES);
        return nullptr;
    }
    FlDockWorkspace* ws = dock_workspace_create();
    ds->registry[ds->registry_count++] = ws;
    return ws;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Remove ws from the registry and destroy it, compacting the array. Entry 0 (the default) is never removed.
static void dock_registry_remove(FlDockShared* ds, FlDockWorkspace* ws) {
    for (u32 i = 1; i < ds->registry_count; ++i) {
        if (ds->registry[i] == ws) {
            dock_workspace_destroy(ws);
            for (u32 j = i + 1; j < ds->registry_count; ++j) {
                ds->registry[j - 1] = ds->registry[j];
            }
            ds->registry_count--;
            return;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The host seam and the promote queue

void fl_dock_register_host(FlDockHost host) {
    FlDockShared* ds = dock_shared_get();
    if (ds != nullptr) {
        ds->host = host;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void dock_promote_panel(FlDockPanelId panel) {
    FlDockShared* ds = dock_shared_get();
    if (ds == nullptr || ds->host.open_promoted_window == nullptr) {
        return; // no host registered: promotion has no window to open
    }
    FlDockWorkspace* src = dock_workspace_current();
    if (!dock_tree_has_panel(src->tree, panel)) {
        return;
    }
    if (ds->promote_count >= FL_DOCK_MAX_WORKSPACES) {
        log_error("dock: promote queue full");
        return;
    }
    ds->promote_queue[ds->promote_count++] = (DockPromoteRequest) { .src = src, .panel = panel };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The host's open_promoted_window typically defers the actual OS-window open to the next tick boundary, so the
// workspace is created, bound, and handed over by pointer before that call returns.
void dock_shared_flush_promotes(void) {
    FlDockShared* ds = dock_shared_get();
    if (ds == nullptr) {
        return;
    }
    for (u32 i = 0; i < ds->promote_count; ++i) {
        DockPromoteRequest* req = &ds->promote_queue[i];
        FlDockWorkspace* dst = dock_registry_add(ds);
        if (dst == nullptr) {
            continue; // registry full - drop the request
        }
        if (!dock_workspace_promote(req->src, dst, req->panel)) {
            dock_registry_remove(ds, dst); // promote rejected (panel vanished since enqueue): undo the empty entry
            continue;
        }
        void* host_window
            = ds->host.open_promoted_window(ds->host.user_data, dst, DOCK_PROMOTED_DEFAULT_W, DOCK_PROMOTED_DEFAULT_H);
        if (host_window == nullptr) {
            // The host could not open a window: demote the panel straight back, and drop dst.
            dock_workspace_demote(req->src, dst, req->panel);
            dock_registry_remove(ds, dst);
            continue;
        }
        ds->promotions[ds->promotion_count++]
            = (DockPromotion) { .host_window = host_window, .dst = dst, .src = req->src, .panel = req->panel };
    }
    ds->promote_count = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_dock_notify_window_closed(void* host_window) {
    FlDockShared* ds = dock_shared_get();
    if (ds == nullptr || host_window == nullptr) {
        return;
    }
    for (u32 i = 0; i < ds->promotion_count; ++i) {
        if (ds->promotions[i].host_window != host_window) {
            continue;
        }
        DockPromotion p = ds->promotions[i];
        dock_workspace_demote(p.src, p.dst, p.panel); // rebind into the source by stable id
        dock_registry_remove(ds, p.dst);              // dissolve the now-empty second workspace + its entry
        for (u32 j = i + 1; j < ds->promotion_count; ++j) {
            ds->promotions[j - 1] = ds->promotions[j];
        }
        ds->promotion_count--;
        return;
    }
}
