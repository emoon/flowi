#pragma once

// Built-in VFS drivers
//
// LocalFS is always present and fl_vfs_init() registers it as the catch-all fallback. The archive drivers
// (libarchive, lhasa, unlzx) are optional: each is compiled in only when its FLOWI_VFS_DRIVER_* cmake option is on.

#include <flowi/vfs/vfs_plugin.h>
#include <stdbool.h>

struct FlArena;

#ifdef __cplusplus
extern "C" {
#endif

// Register every archive driver compiled into this build, in specificity order. Call after fl_vfs_init();
// LocalFS stays the fallback and is tried last regardless. A build with every FLOWI_VFS_DRIVER_* off leaves this a
// no-op.
void fl_vfs_register_builtin_drivers(void);

// Create the process-wide VFS and register LocalFS, the catch-all fallback tried after every other driver. Returns true
// when this call created it (the caller must then call fl_vfs_destroy), false when it already existed.
bool fl_vfs_init(struct FlArena* arena);

// Tear the VFS down on the main thread, waiting for the operations still open on every mount. The VFS must exist:
// calling this before fl_vfs_init() or after it has run is a bug.
void fl_vfs_destroy(void);

// Wait for the job of every live operation, including ones whose handle was already closed. The VFS must exist.
void fl_vfs_wait_all(void);

// Hand a driver the host built to the VFS. Call it after fl_vfs_init() and before the first mount: the driver list is
// not locked, so registering while a mount resolves a path races it. Drivers registered this way are tried before
// LocalFS, in registration order. A plugin whose api_version is not FL_VFS_PLUGIN_API_VERSION is refused. The plugin
// must outlive the VFS. instance is passed to every driver call (nullptr for a stateless driver).
void fl_vfs_register_driver(const FlVfsPlugin* plugin, void* instance);

#ifdef __cplusplus
}
#endif
