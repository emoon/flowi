#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// VFS lifecycle for hosts
//
// The process-wide VFS is created and torn down by the host (fl_host_create does this for an embedded host).
// A host that wires its own bring-up calls these directly. Consumers that only mount and read use
// <flowi/vfs/vfs_api.h>.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <flowi/fl_api.h>
#include <flowi/vfs/vfs_plugin.h>
#include <stdbool.h>

typedef struct FlArena FlArena;

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the VFS. Registers the built-in LocalFS driver, which stays the catch-all fallback, tried after
// every driver registered later with fl_vfs_register_driver. Returns true when this call initialized the VFS (the
// caller owns it and must call fl_vfs_destroy), false when it was already initialized (first caller wins; this
// call did nothing).
FL_API bool fl_vfs_init(FlArena* arena);

// Tear the VFS down on the main thread. Waits for the operations still open on every mount.
FL_API void fl_vfs_destroy(void);

// Wait for the job of every live operation, including ones whose handle was already closed. For startup and
// teardown on the main thread, where a mount's first job must have run before the frame loop issues work against
// it.
FL_API void fl_vfs_wait_all(void);

// Hand a constructed driver to the VFS. Call after fl_vfs_init. The plugin pointer must outlive the VFS. instance
// is an optional driver instance handle passed to every driver op (nullptr for stateless drivers).
FL_API void fl_vfs_register_driver(const FlVfsPlugin* plugin, void* instance);

#ifdef __cplusplus
}
#endif
