#pragma once

// Types shared by VFS drivers (vfs_plugin.h) and VFS consumers (vfs_api.h).

#include <flowi/core/string.h>
#include <stdbool.h>
#include <stdint.h>

// Drivers stamp FlVfsPlugin.api_version with this.

#define FL_VFS_PLUGIN_API_VERSION 3

// A single file or directory entry, used in directory listings and metadata queries. The full layout is in
// <flowi/vfs/vfs_plugin.h>; this header only forward-declares the tag.

typedef struct FlVfsEntry FlVfsEntry;

// An entry-returning VFS function yields &fl_nil_vfs_entry, never null, when nothing matched, so the
// result is always safe to dereference; test against this address for validity. All fields are zeroed
// (empty name, no size, not a directory) and the storage is read-only: never write through it.

extern const FlVfsEntry fl_nil_vfs_entry;

// Optional mount-level parameters; they apply to all files accessed through the mount. The full layout
// is in <flowi/vfs/vfs_plugin.h>; include that header to construct one or pass it by value.

typedef struct FlVfsMountOptions FlVfsMountOptions;

typedef struct FlVfsMount FlVfsMount;

// FlVfsMountErrorStatus (detailed mount error codes) and FlVfsMountResult (the mount handle and status a
// mount operation returns) reach consumers through <flowi/vfs/vfs_api.h>.
