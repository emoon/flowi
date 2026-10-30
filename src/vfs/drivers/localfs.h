#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LocalFS - flowi's built-in VFS driver for the native filesystem.
//
// LocalFS is the built-in driver, registered during fl_vfs_init(). Drivers added via vfs_register_driver() are tried
// ahead of it, so it is the catch-all fallback for plain filesystem paths.
//
// Every open handle carries its own absolute OS path plus (for files) an OS file descriptor.

struct FlVfsPlugin;

// The singleton LocalFS driver.
const struct FlVfsPlugin* localfs_plugin(void);
