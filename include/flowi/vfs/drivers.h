#pragma once

// Built-in VFS drivers
//
// LocalFS is always present and the host registers it as the catch-all fallback. The archive drivers
// (libarchive, lhasa, unlzx) are optional: each is compiled in only when its FLOWI_VFS_DRIVER_* cmake option is on.

#include <flowi/vfs/vfs_plugin.h>

#ifdef __cplusplus
extern "C" {
#endif

// Register every archive driver compiled into this build, in specificity order. Call after the host exists;
// LocalFS stays the fallback and is tried last regardless. A build with every FLOWI_VFS_DRIVER_* off leaves this a
// no-op.
void fl_vfs_register_builtin_drivers(void);

// Hand a driver the host built to the VFS. Call it once the host exists, because the host owns the VFS, and before
// the first mount: the driver list is not locked, so registering while a mount resolves a path races it. A host
// created with mount_working_directory starts a mount during creation, so keep that off if drivers must be in place
// first. Drivers registered this way are tried before LocalFS, in registration order. The plugin must outlive the VFS.
// instance is passed to every driver call (nullptr for a stateless driver).
//
// Returns false when the driver was refused: the VFS does not exist, plugin is nullptr, or its api_version is not
// FL_VFS_PLUGIN_API_VERSION.
bool fl_vfs_register_driver(const FlVfsPlugin* plugin, void* instance);

#ifdef __cplusplus
}
#endif
