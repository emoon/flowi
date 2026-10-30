#pragma once

#include <core/arena.h>
#include <core/core.h>
#include <core/string.h>
#include <core/hashmap.h>
#include <core/jobsys.h>
#include <core/os/os.h>
#include "vfs.h"
#include <flowi/vfs/vfs_plugin.h>
#include <stdatomic.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Forward declarations

typedef struct VfsState VfsState;
typedef struct VfsOp VfsOp;
typedef struct StringAllocator StringAllocator;

// VFS Plugin Entry - a registered driver plus its (optional) instance handle, linked into VfsState's list.
typedef struct VfsPluginEntry {
    struct VfsPluginEntry* next;
    struct VfsPluginEntry* prev;
    const FlVfsPlugin* plugin;
    void* plugin_instance;
} VfsPluginEntry;

typedef enum VfsResolveError {
    VfsResolveError_None = 0,
    VfsResolveError_FileNotFound,
    VfsResolveError_NoDriver,
    VfsResolveError_InvalidPath,
    VfsResolveError_InvalidFileSize, // The read returned a different size than the stat
    VfsResolveError_NotMountable,    // Path resolves to a regular file (not a directory or archive)
    VfsResolveError_FileTooLarge,    // Larger than VFS_MAX_READ_SIZE, the most that is read into memory
    VfsResolveError_OutOfMemory,
} VfsResolveError;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Handle data for operations

typedef enum VfsOperationType {
    VfsOp_Mount,
    VfsOp_MountList,
    VfsOp_FileOpen,
    VfsOp_FileRead,
    VfsOp_ReadAll, // Monolithic read: open + read entire file + close (no separate file handle)
} VfsOperationType;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// One path segment met while resolving paths below a mount. A node that carries a driver is the root of an archive
// (or the mount root itself): paths below it are handed to that driver, and a path below a node without one walks up
// to the nearest ancestor that has one. Nodes are only created and edited under the mount's driver_lock, and live
// until the mount is freed.
typedef struct VfsTreeNode {
    struct VfsTreeNode* parent;
    struct VfsTreeNode* first_child;
    struct VfsTreeNode* next_sibling;

    FlString name;
    bool is_directory; // The path goes on below this node as a plain path: a directory, or a file that is no archive

    VfsPluginEntry* plugin_entry; // Driver for this node and everything below it (nullptr: inherit the parent's)
    void* handle;                 // That driver's single handle for the node
} VfsTreeNode;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct FlVfsMount {
    u64 id; // Unique for the life of the process, assigned at creation and never reused
    // FlVfsMountStatus, published once by the mount job. root_node carries the driver and handle, written before it.
    _Atomic u32 status;
    struct FlVfsMount* next;

    // One reference for the owner until fl_vfs_mount_close, plus one per live operation. The last release closes
    // every node handle and frees the mount.
    _Atomic u32 refs;

    // Guarded by VfsState::handle_lock. closing refuses new operations; ops_* list every operation not yet
    // destroyed, addressable or not, so a close can find the ones to release and the jobs to wait for.
    bool closing;
    VfsOp* ops_first;
    VfsOp* ops_last;
    u32 ops_count;

    struct FlArena* arena;           // Backing storage for strings
    struct StringAllocator* strings; // Thread-safe allocator for the paths of this mount's operations

    FlString source_path; // The actual source path (e.g., "/home/user/data" or "https://example.com/file.zip")

    // Mount-level options (applied to all file operations from this mount)
    FlVfsMountOptions options;
    FlJobHandle job_handle;

    // The tree of nodes met while resolving paths. The root carries the driver and handle the source resolved to; the
    // mount job sets them before it publishes the status. Everything else is guarded by driver_lock.
    VfsTreeNode* root_node;
    struct FlArena* nodes_arena; // Backing storage for the nodes, only touched under driver_lock

    // The one lock of a mount. It serializes the driver calls that use the tree or an open file (open, read, get_size
    // and list, as run by the job workers) together with every edit of the tree, so a node's handle can never be
    // swapped while another worker is using it. Drivers are not required to be thread-safe. Never held
    // together with another lock, and never across a callback into caller code. Closing a file handle does not take it,
    // so drivers must allow that close alongside other calls on the same mount.
    Mutex driver_lock;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// One asynchronous operation. It owns its path, result arena, status and job handle, and is reference counted: the
// slot table (while the caller's id is open), the operation's own job (until it has run) and, for a chained read, the
// file it reads through each hold one. It is destroyed when the last reference goes, which is where its release hook
// runs. A mount holds no reference to an operation; the operation holds its mount.
struct VfsOp {
    _Atomic u32 refs;
    VfsOp* next; // Mount's live-operation list, guarded by VfsState::handle_lock
    VfsOp* prev;

    FlVfsMount* mount;
    FlString path;
    VfsOperationType op_type;
    u32 id;    // Key in the slot table; FL_VFS_HANDLE_INVALID for the mount's own operation, which has none
    int depth; // Depth for directory listing operations

    // Set by the job once its result and error_status are published; what fl_vfs_is_ready reports.
    _Atomic bool done;
    // Set when the job is handed to the job system, so a repeated vfs_dispatch schedules nothing twice.
    _Atomic bool scheduled;
    _Atomic FlJobHandle job_handle;
    // The last job scheduled on this file, which the next read chains after. Equals job_handle for any other op.
    _Atomic FlJobHandle last_job;

    _Atomic(void*) result_data;       // Points to FlVfsData, FlVfsFileList, etc. (atomic for thread safety)
    FlArena* result_arena;            // FlArena for result data (destroyed with the op for list operations)
    FlString error_text;              // Heap storage behind the result's error_message, freed with the op
    _Atomic VfsOpStatus error_status; // Hierarchical VFS status for errors (atomic for thread safety)

    FlVfsReadCallback callback; // Callback for custom processing (callback mode)
    void* user_data;
    FlVfsReleaseCallback release; // Runs once with user_data when the op is destroyed, after its job

    // File operation fields (for VfsOp_FileOpen)
    void* plugin_file_handle;    // Driver handle from open(), a child of the handle of a tree node
    VfsPluginEntry* file_driver; // The driver that handle belongs to
    _Atomic u32 open_flags;      // Flags used to open the file

    // File read fields (for VfsOp_FileRead)
    u8* read_buffer; // Buffer to read into (caller-provided)
    i64 read_size;

    // The open file a read is chained onto. The read owns this reference from creation until it has run, so the file
    // outlives its own close while a read can still reach it.
    VfsOp* file_op;

    // Set if callback owns the result data (VFS should not free it)
    bool callback_owns_data;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Main VFS state

typedef struct VfsState {
    FlVfsMount* mount_first;
    FlVfsMount* mount_last;

    struct FlArena* main_arena; // The arena VfsState lives in; owned by VFS only when owns_main_arena is set
    bool owns_main_arena;       // fl_vfs_init created main_arena itself, so vfs_destroy destroys it

    // Registered drivers, in registration order. Handed in directly via vfs_register_driver().
    VfsPluginEntry* plugin_first;
    VfsPluginEntry* plugin_last;

    // The built-in LocalFS driver, registered by fl_vfs_init(). It is the catch-all fallback and stays last:
    // vfs_register_driver() inserts other drivers ahead of it.
    VfsPluginEntry* builtin_fallback;

    // The one lock around the registry: the mount list, each mount's operation list and closing flag, the slot table
    // and the id counter. A leaf: it is held for list and table edits only, never across a driver call, a callback, a
    // release hook, a wait or another lock.
    Mutex handle_lock;
    u32 next_handle_id;

    // Turns a caller's u32 into the operation it names. Holds one reference per operation it lists.
    hashmap(u32, VfsOp*) slots;

    // Ids whose operation was closed but has not been destroyed yet (its job is still queued or running). They are
    // off the slot table, so nothing resolves them, and they are not handed out again until the operation has
    // finished.
    hashmap(u32, bool) retiring;

} VfsState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal functions

// Slot table. vfs_op_acquire returns the operation id names with a reference of its own, or nullptr when the id is
// stale, closed or invalid; the caller hands it back with vfs_op_release. No raw operation pointer outlives the lock
// without such a reference.
VfsOp* vfs_op_acquire(FlVfsHandle id);
void vfs_op_release(VfsOp* op);
