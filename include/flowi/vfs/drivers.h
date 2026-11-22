#pragma once

// Built-in VFS drivers
//
// LocalFS is always present and fl_vfs_init() registers it as the catch-all fallback. The archive drivers
// (libarchive, lhasa, unlzx) are optional: each is compiled in only when its FLOWI_VFS_DRIVER_* cmake option is on.

#include <flowi/vfs/vfs_plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

// Register every archive driver compiled into this build, in specificity order. Call after fl_vfs_init();
// LocalFS stays the fallback and is tried last regardless. A build with every FLOWI_VFS_DRIVER_* off leaves this a
// no-op.
void fl_vfs_register_builtin_drivers(void);

// Hand a driver the host built to the VFS. Call after fl_vfs_init(). Drivers registered this way are tried before
// LocalFS, in registration order. The plugin must outlive the VFS. instance is passed to every driver call (nullptr
// for a stateless driver).
void fl_vfs_register_driver(const FlVfsPlugin* plugin, void* instance);

#ifdef __cplusplus
}
#endif
