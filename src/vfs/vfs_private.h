#pragma once

// Internal declarations shared by the vfs_*.c files

#include <core/log.h>
#include <core/path.h>
#include "vfs_internal.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Largest file the VFS reads whole into memory: a read-all, or an archive opened from memory (512 MB)
#define VFS_MAX_READ_SIZE (512 * 1024 * 1024)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared type definitions

// Result of resolving a mount's source
typedef struct {
    VfsPluginEntry* plugin_entry; // Final plugin that can handle the resolved path (nullptr on error)
    void* handle;                 // Handle to the final resource (nullptr on error)
    VfsResolveError error;
} FinalDriver;

typedef struct {
    VfsPluginEntry* plugin_entry;
    void* handle;
    int path_index;
} DriverHandle;

typedef struct {
    int error;
    int path_split_offset;
    void* handle;
    i64 size;
    FlString loaded_path;
    bool is_directory;
} TryLoadData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Global state access (defined in vfs.c)

extern LogChannelId VFS_ID;
extern VfsState* g_vfs_state;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mount path resolution (defined in vfs_resolve.c)

// Resolves a mount's source path to a driver and one live root handle, descending through archives nested in the
// path (a.zip/inner.lha). The caller owns the returned handle.
FinalDriver vfs_resolve_mount_path(VfsState* self, FlString path, FlArena* scratch);

// Consumes root_handle and every container handle it opens while descending. On success exactly one
// handle is live, the one returned in FinalDriver.handle, and the caller owns it; on error no handle is
// live. root_handle is only left open when it is itself the returned handle.
FinalDriver vfs_resolve_from_root(VfsState* self, VfsPluginEntry* root_entry, void* root_handle,
                                  PathComponents* components, int path_start, FlArena* scratch);

// Returns a freshly opened handle the caller owns, or a zeroed DriverHandle when no plugin matched.
DriverHandle vfs_resolve_find_root_driver(VfsState* self, FlArena* temp, const PathComponents* comp, int start);

// On success the plugin has taken ownership of data and frees it when the returned handle is closed, and the
// caller owns that handle. On failure data is untouched and still the caller's to free.
DriverHandle vfs_resolve_find_memory_driver(VfsState* self, FlString org_path, u8* data, u64 size);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Archives below a mount's root (defined in vfs_resolve.c)

// path_reconstruct_from_segments, except that a range that does not start at the first segment is relative to the
// archive before it, so it never gets the leading '/' of an absolute path.
FlString vfs_path_range(const PathComponents* components, int start, u32 end, FlArena* arena);

// Opens the file at path below parent_handle, size bytes long, as an archive: in place when it is a file on disk,
// otherwise from a copy in memory. Returns a zeroed DriverHandle when it is not an archive or is corrupt.
DriverHandle vfs_resolve_open_archive(VfsState* self, VfsPluginEntry* parent, void* parent_handle, FlString path,
                                      i64 size, FlArena* scratch);

// True when an archive driver claims the name. LocalFS, which claims everything, does not count.
bool vfs_name_is_archive(VfsState* self, FlString name);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The tree of a mount (defined in vfs_tree.c). All of it runs under the mount's driver_lock, except the creation
// and cleanup of a mount, which nothing else can reach.

// Where a path lives: the driver and handle of the nearest node that carries one, and the path below it.
typedef struct VfsLocation {
    VfsPluginEntry* driver;
    void* handle;
    FlString path;
} VfsLocation;

VfsTreeNode* vfs_tree_create_node(FlVfsMount* mount, VfsTreeNode* parent, FlString name);

// Finds where path lives in mount. Each segment of the path is a node; one that is a file an archive driver opens
// becomes an archive node, and the path below it goes to that archive. is_directory says whether the whole path is
// entered (a listing) or its last segment is a file (an open) that stays with the node before it.
VfsLocation vfs_tree_locate(VfsState* self, FlVfsMount* mount, FlString path, bool is_directory, FlArena* scratch);

// Opens the file at path below parent_handle as an archive and makes it the driver of node. False, with the node
// untouched, when it does not open.
bool vfs_tree_try_load_as_archive(VfsState* self, VfsTreeNode* node, VfsPluginEntry* parent, void* parent_handle,
                                  FlString path, i64 size, FlArena* scratch);

// Closes the node's handle, then every child's, children first.
void vfs_tree_cleanup_node(VfsTreeNode* node);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Operations functions (defined in vfs_ops.c)

void vfs_op_mount(VfsState* self, VfsOp* op, FlArena* scratch);
void vfs_op_list(VfsState* self, VfsOp* op, FlArena* scratch);
void vfs_op_file_open(VfsOp* op, FlArena* scratch);
void vfs_op_file_read(VfsOp* op);

// Job scheduling. vfs_ops_do_job is the job entry: it runs the op, then returns the job's reference.
// vfs_ops_execute_sync only runs it, for a caller that holds the op already.
void vfs_ops_do_job(void* data, FlJobsWorkerInfo info);
void vfs_ops_execute_sync(VfsOp* op);
FlJobHandle vfs_ops_schedule_job(FlVfsMount* mount, FlJobsFunc func, void* user_data);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core VFS functions (defined in vfs.c)

// Test seam, null in production: fl_vfs_read calls it on the main thread right after it has scheduled a read, with the
// read's id, before it records the read on the file. Lets a test run the read and close the file in that gap.
extern void (*g_vfs_test_after_read_scheduled)(FlVfsHandle read);

// Formats an error message owned by the op: it stays valid until the op is destroyed and is freed with it. A second
// call replaces the first.
FlString vfs_op_format_error(VfsOp* op, const char* fmt, ...);
