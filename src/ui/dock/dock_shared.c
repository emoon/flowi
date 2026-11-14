// Dock Shared: the app-global workspace registry, the injected host seam, and the deferred promote queue. The entry
// points below reach it through the current context's shared->dock.

#include "dock_shared.h"

#include <core/arena.h>
#include <core/log.h>

#include "../ui_private.h"

// Default promoted-window size in logical pixels.
#define DOCK_PROMOTED_DEFAULT_W 480.0f
#define DOCK_PROMOTED_DEFAULT_H 320.0f

// A deferred promote request: move panel out of src into a fresh second workspace, then have the host open a
// window for it. Captured at dock_promote_panel, applied at the first dock_shared_flush_promotes that finds src
// with no drag in flight.
typedef struct DockPromoteRequest {
    FlDockWorkspace* src;
    FlDockPanelId panel;
} DockPromoteRequest;

// A live promotion: a workspace hosted in its own window. host_window is the host's opaque handle (the close key),
// nullptr while the host is still inside open_promoted_window; dst is that workspace; src is where its panels demote
// back to on close.
typedef struct DockPromotion {
    void* host_window;
    FlDockWorkspace* dst;
    FlDockWorkspace* src;
} DockPromotion;

struct FlDockShared {
    FlArena* arena; // owns this record itself

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

FlDockShared* dock_shared_create(void) {
    FlArena* arena = arena_new();
    FlDockShared* ds = arena_alloc_zero(arena, FlDockShared);
    ds->arena = arena;
    ds->registry[0] = dock_workspace_create();
    ds->registry_count = 1;
    return ds;
}

void dock_shared_destroy(FlDockShared* ds) {
    if (ds == nullptr) {
        return;
    }
    for_count(i, ds->registry_count) {
        dock_workspace_destroy(ds->registry[i]);
    }
    fl_arena_destroy(ds->arena);
}

FlDockWorkspace* dock_shared_default(FlDockShared* ds) {
    return (ds != nullptr && ds->registry_count > 0) ? ds->registry[0] : nullptr;
}

// nullptr when there is no shared tier (a standalone workspace driven directly by a unit test), so the entry points
// below must no-op on it.
static FlDockShared* dock_shared_get(void) {
    FlUiCtx* ctx = ui_get_ctx();
    if (ctx == nullptr || ctx->shared == nullptr) {
        return nullptr;
    }
    return ctx->shared->dock;
}

// A node id names the node's layout elements, and every window of a tick lays out into one id space. So each
// workspace mints its node ids from a block of its own. The default workspace has block 0. With 16 blocks of 2^20
// ids a leaf id stays under 2^24, which the tab ids need (dock_layout shifts it up by 8).
#define DOCK_NODE_ID_BLOCK_BITS 20
_Static_assert(FL_DOCK_MAX_WORKSPACES <= 16, "a leaf id must stay under 2^24");

static bool id_block_taken(const FlDockShared* ds, u32 block) {
    for_count(i, ds->registry_count) {
        if (ds->registry[i]->tree->next_node_id >> DOCK_NODE_ID_BLOCK_BITS == block) {
            return true;
        }
    }
    return false;
}

// The registry holds fewer workspaces than there are blocks whenever one more can be added, so a block is free.
static u32 free_id_block(const FlDockShared* ds) {
    u32 block = 1;
    while (id_block_taken(ds, block)) {
        block++;
    }
    return block;
}

static FlDockWorkspace* dock_registry_add(FlDockShared* ds) {
    if (ds->registry_count >= FL_DOCK_MAX_WORKSPACES) {
        log_error("dock: workspace registry full (%d)", FL_DOCK_MAX_WORKSPACES);
        return nullptr;
    }
    FlDockWorkspace* ws = dock_workspace_create();
    dock_workspace_set_tree(ws, dock_tree_create_from(free_id_block(ds) << DOCK_NODE_ID_BLOCK_BITS));
    ds->registry[ds->registry_count++] = ws;
    return ws;
}

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

// A promotion that would demote into ws demotes into heir instead.
static void reaim_promotions(FlDockShared* ds, const FlDockWorkspace* ws, FlDockWorkspace* heir) {
    for_count(i, ds->promotion_count) {
        if (ds->promotions[i].src == ws) {
            ds->promotions[i].src = heir;
        }
    }
}

// A queued promote out of ws goes with its source.
static void drop_queued_promotes(FlDockShared* ds, const FlDockWorkspace* ws) {
    u32 kept = 0;
    for_count(i, ds->promote_count) {
        if (ds->promote_queue[i].src != ws) {
            ds->promote_queue[kept++] = ds->promote_queue[i];
        }
    }
    ds->promote_count = kept;
}

// Take ws out of the app for good, once nothing points at it any more.
static void retire_workspace(FlDockShared* ds, FlDockWorkspace* ws, FlDockWorkspace* heir) {
    reaim_promotions(ds, ws, heir);
    drop_queued_promotes(ds, ws);
    dock_registry_remove(ds, ws);
}

// Move every panel dst holds into src, one demote at a time, leaving dst an empty floor.
static void demote_all(FlDockWorkspace* src, FlDockWorkspace* dst) {
    FlDockPanelId panel = 0;
    while (dock_tree_collect_panels(dst->tree, &panel, 1) > 0) {
        if (!dock_workspace_demote(src, dst, panel)) {
            return;
        }
    }
}

void fl_dock_register_host(FlDockHost host) {
    FlDockShared* ds = dock_shared_get();
    if (ds != nullptr) {
        ds->host = host;
    }
}

static bool promote_is_queued(const FlDockShared* ds, const FlDockWorkspace* src, FlDockPanelId panel) {
    for_count(i, ds->promote_count) {
        if (ds->promote_queue[i].src == src && ds->promote_queue[i].panel == panel) {
            return true;
        }
    }
    return false;
}

void dock_promote_panel(FlDockPanelId panel) {
    FlDockShared* ds = dock_shared_get();
    if (ds == nullptr || ds->host.open_promoted_window == nullptr) {
        return; // no host registered: promotion has no window to open
    }
    FlDockWorkspace* src = dock_workspace_current();
    if (dock_tree_find_leaf_with_panel(src->tree, panel) == nullptr) {
        return; // absent, or in flight on a drag that still owns it
    }
    if (promote_is_queued(ds, src, panel)) {
        return; // an app may ask every frame while a drag holds the request back
    }
    if (ds->promote_count >= FL_DOCK_MAX_WORKSPACES) {
        log_error("dock: promote queue full");
        return;
    }
    ds->promote_queue[ds->promote_count++] = (DockPromoteRequest) { .src = src, .panel = panel };
}

static i32 promotion_of_workspace(const FlDockShared* ds, const FlDockWorkspace* dst) {
    for_count(i, ds->promotion_count) {
        if (ds->promotions[i].dst == dst) {
            return (i32)i;
        }
    }
    return -1;
}

static DockPromotion take_promotion(FlDockShared* ds, u32 index) {
    DockPromotion taken = ds->promotions[index];
    for (u32 j = index + 1; j < ds->promotion_count; ++j) {
        ds->promotions[j - 1] = ds->promotions[j];
    }
    ds->promotion_count--;
    return taken;
}

// The host's open_promoted_window typically defers the actual OS-window open to the next tick boundary, so the
// workspace is created, bound, and handed over by pointer before that call returns.
//
// The promotion is recorded before the call. A host that reports the source's own window closed from inside it
// retires the source, and the record is then re-aimed like every other one. It is looked up again afterwards
// because that close also moves it.
static void apply_promote(FlDockShared* ds, const DockPromoteRequest* req) {
    FlDockWorkspace* dst = dock_registry_add(ds);
    if (dst == nullptr) {
        return; // registry full, so the request is dropped
    }
    if (!dock_workspace_promote(req->src, dst, req->panel)) {
        dock_registry_remove(ds, dst); // the panel vanished since enqueue, so remove the empty entry
        return;
    }
    ds->promotions[ds->promotion_count++] = (DockPromotion) { .dst = dst, .src = req->src };
    void* host_window
        = ds->host.open_promoted_window(ds->host.user_data, dst, DOCK_PROMOTED_DEFAULT_W, DOCK_PROMOTED_DEFAULT_H);
    i32 index = promotion_of_workspace(ds, dst);
    FL_ASSERT_FATAL(index >= 0); // only a close by handle removes a record, and this one has no handle yet
    if (host_window == nullptr) {
        // The host could not open a window: demote the panel straight back, and drop dst.
        DockPromotion failed = take_promotion(ds, (u32)index);
        dock_workspace_demote(failed.src, dst, req->panel);
        dock_registry_remove(ds, dst);
        return;
    }
    ds->promotions[index].host_window = host_window;
}

// A drag previews a copy of its workspace's tree, so a promote out of that workspace waits for the drag to end.
static bool promote_is_held(const DockPromoteRequest* req) {
    return dock_drag_active(&req->src->drag);
}

// The oldest request that can be applied now, or -1.
static i32 next_ready_promote(const FlDockShared* ds) {
    for_count(i, ds->promote_count) {
        if (!promote_is_held(&ds->promote_queue[i])) {
            return (i32)i;
        }
    }
    return -1;
}

static DockPromoteRequest take_promote(FlDockShared* ds, u32 index) {
    DockPromoteRequest taken = ds->promote_queue[index];
    for (u32 j = index + 1; j < ds->promote_count; ++j) {
        ds->promote_queue[j - 1] = ds->promote_queue[j];
    }
    ds->promote_count--;
    return taken;
}

// Each request leaves the queue before it is applied, and the queue is read again for the next one. A host that
// reports a window closed from inside its open callback can then drop queued requests without upsetting the loop.
void dock_shared_flush_promotes(void) {
    FlDockShared* ds = dock_shared_get();
    if (ds == nullptr) {
        return;
    }
    for (i32 ready = next_ready_promote(ds); ready >= 0; ready = next_ready_promote(ds)) {
        DockPromoteRequest req = take_promote(ds, (u32)ready);
        apply_promote(ds, &req);
    }
}

static i32 promotion_index(const FlDockShared* ds, const void* host_window) {
    for_count(i, ds->promotion_count) {
        if (ds->promotions[i].host_window == host_window) {
            return (i32)i;
        }
    }
    return -1;
}

void fl_dock_notify_window_closed(void* host_window) {
    FlDockShared* ds = dock_shared_get();
    if (ds == nullptr || host_window == nullptr) {
        return;
    }
    i32 index = promotion_index(ds, host_window);
    if (index < 0) {
        return;
    }
    DockPromotion p = take_promotion(ds, (u32)index);
    demote_all(p.src, p.dst); // rebind into the source by stable id
    retire_workspace(ds, p.dst, p.src);
}
