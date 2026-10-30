#pragma once

#include <core/string.h>
#include <core/jobsys.h>
#include <core/types.h>
#include <flowi/vfs/vfs_api.h>
#include <flowi/vfs/vfs_plugin.h>

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// VFS Operation Status - Hierarchical 32-bit error system (internal use only)

typedef u32 VfsOpStatus;

// Bits 0-7: Core VFS state (256 values)
#define VFS_STATE_MASK 0x000000FF
#define VFS_STATE_SUCCESS 0x00000000
#define VFS_STATE_PENDING 0x00000001
#define VFS_STATE_ERROR 0x00000002

// Bits 8-15: VFS subsystem errors (256 values)
#define VFS_SUBSYSTEM_MASK 0x0000FF00
#define VFS_MOUNT_ERROR 0x00000100
#define VFS_DRIVER_ERROR 0x00000200
#define VFS_NOT_FOUND_ERROR 0x00000300
#define VFS_READ_ERROR 0x00000400
#define VFS_TOO_LARGE_ERROR 0x00000500
#define VFS_THREADING_ERROR 0x00000600

// Bits 16-31: Available for callback/user errors (65536 values)
#define VFS_USER_SHIFT 16

#define VFS_ERROR_MOUNT (VFS_STATE_ERROR | VFS_MOUNT_ERROR)
#define VFS_ERROR_DRIVER (VFS_STATE_ERROR | VFS_DRIVER_ERROR)
#define VFS_ERROR_FILE_NOT_FOUND (VFS_STATE_ERROR | VFS_NOT_FOUND_ERROR)
#define VFS_ERROR_READ (VFS_STATE_ERROR | VFS_READ_ERROR)
#define VFS_ERROR_FILE_TOO_LARGE (VFS_STATE_ERROR | VFS_TOO_LARGE_ERROR)
// The op could only have proceeded by waiting on a job worker, which the job system forbids
#define VFS_ERROR_WOULD_BLOCK (VFS_STATE_ERROR | VFS_THREADING_ERROR)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Callback invoked once per discovered entry during directory listing

typedef void (*VfsListingCallback)(FlVfsEntry entry, void* user_data);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Initialize the VFS system. Registers the built-in LocalFS driver, which stays the catch-all fallback,
// tried after every driver registered later via vfs_register_driver().
// Returns true if this call initialized the VFS (the caller owns it and should call vfs_destroy),
// false if it was already initialized (first-caller-wins; this call was a no-op).
bool fl_vfs_init(struct FlArena* arena);

void vfs_destroy(void);

// Convenience macro for mounting with default options
#define vfs_mount(source_path) fl_vfs_mount_with_options(source_path, (FlVfsMountOptions) { 0 })

// Returns 0 if the mount completed synchronously
FlJobHandle fl_vfs_mount_get_job_handle(FlVfsMount* mount);

// Process-unique id of the mount instance; never reused, even after the mount closes.
u64 vfs_mount_get_id(const FlVfsMount* mount);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct VfsReadOptions {
    FlVfsReadCallback callback;
    void* user_data;
    FlVfsReleaseCallback release; // Runs once with user_data when the handle is freed (see FlVfsReleaseCallback)
    FlVfsHandle reuse_handle;     // Handle to reuse (or FL_VFS_HANDLE_INVALID/0)
} VfsReadOptions;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File Operations on Mounts

// Two-phase read: allocate handle without dispatching the job.
// Must be followed by vfs_dispatch() to actually start the operation.
FlVfsHandle vfs_mount_read_all_prepare(FlVfsMount* mount, FlString relative_path, FlVfsReadCallback callback,
                                       void* user_data, FlVfsReleaseCallback release, FlVfsHandle reuse_handle);

// Dispatch a previously prepared handle (from vfs_mount_read_all_prepare).
void vfs_dispatch(FlVfsHandle handle);

#define vfs_mount_read_all(mount, path, ...)                                                            \
    ({                                                                                                  \
        VfsReadOptions _opts = { __VA_ARGS__ };                                                         \
        fl_vfs_mount_read_all_with_options(mount, path, _opts.callback, _opts.user_data, _opts.release, \
                                           _opts.reuse_handle);                                         \
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle Operations
//
// Thread contract: fl_jobs_wait() does nothing on a job worker, because blocking one on a job that itself
// needs a worker can deadlock the pool. A worker can therefore only observe an operation that has already
// finished, and the calls below fail closed rather than pretend otherwise: fl_vfs_wait() logs and returns, and a
// read chained onto an unfinished predecessor completes with VFS_ERROR_WOULD_BLOCK instead of running
// against a file that is not open yet. Wait for, and chain onto, in-flight handles from the main thread.
// Handles a worker opened itself are unaffected - the job system runs those inline, so they are already
// finished.
//
// fl_vfs_close() never waits, on any thread, except for a fl_vfs_read handle (below). It gives up the id: the handle
// reads as absent from then on, and the id stays reserved - reuse_handle refuses it - until the operation has finished
// and been destroyed. An operation is reference counted, and the job that is still running holds a reference of its
// own, so the operation is released when the job ends - on the worker that ran it - and a read-all's release hook runs
// then, after its callback. The job itself runs to completion, so a read-all's user_data must stay valid past the
// close; the release hook is what tells its owner when it may go. The exception is a handle from fl_vfs_read(): it runs
// against memory the caller lent for the op and closing one is the caller's signal that the memory may go, so that
// close waits for the op first, and is refused on a worker, where it cannot wait.
//
// fl_vfs_mount_close() marks the mount closing, so it refuses new operations, and gives up the ids of the operations
// still open on it. On the main thread it then waits until the last of them has ended and run its release hook; on
// a worker it returns at once, and the mount is freed when the last operation holding it ends, so a buffer lent to
// a fl_vfs_read must outlive that read's job. A release hook must not close the mount its own operation belongs to from
// the main thread: the close waits for that operation to be destroyed. vfs_destroy() is main-thread teardown.

// Waits for the job of every live operation, including ones whose id was already closed. For startup and teardown
// on the main thread, where a mount's first job must have run before the frame loop issues work against it.
void vfs_wait_all(void);

VfsOpStatus vfs_get_status(FlVfsHandle handle);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline VfsOpStatus vfs_get_core_state(VfsOpStatus status) {
    return status & VFS_STATE_MASK;
}

static inline u32 vfs_get_subsystem_error(VfsOpStatus status) {
    return status & VFS_SUBSYSTEM_MASK;
}

static inline bool vfs_is_success(VfsOpStatus status) {
    return vfs_get_core_state(status) == VFS_STATE_SUCCESS;
}

static inline bool vfs_is_pending(VfsOpStatus status) {
    return vfs_get_core_state(status) == VFS_STATE_PENDING;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Driver Registration (for extending VFS)
//
// Hand a constructed driver to the VFS. Call after fl_vfs_init(). The plugin pointer must outlive the VFS.
// instance: Optional driver instance handle passed to every driver op (NULL for stateless drivers)
void vfs_register_driver(const FlVfsPlugin* plugin, void* instance);
