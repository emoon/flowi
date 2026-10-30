#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// VFS API for consumers
//
// Mount archives, directories or URLs at a mount point and read them back through one path space.
// Operations are async: poll a handle with fl_vfs_is_ready before touching its data. Implementing a
// driver instead of consuming one: <flowi/vfs/vfs_plugin.h>.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <flowi/vfs/vfs.h>
#include <flowi/vfs/vfs_types.h>
#include <stdint.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// VFS Handles
//
// Opaque handles for async file/directory operations. Check with fl_vfs_is_ready() before accessing data.

typedef uint32_t FlVfsHandle;
typedef FlVfsHandle FlVfsDirHandle;

#define FL_VFS_HANDLE_INVALID ((FlVfsHandle)0)
#define FL_VFS_DIR_HANDLE_INVALID ((FlVfsDirHandle)0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Convenience: mount with default options (fl_vfs_mount_with_options takes options explicitly).

#define fl_vfs_mount(source) fl_vfs_mount_with_options(source, (FlVfsMountOptions) { 0 })
