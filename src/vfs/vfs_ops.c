// VFS Operations
// File and directory operations (read, list, open)

#include <core/log.h>
#include <core/memory.h>
#include <core/path.h>
#include <core/profile.h>
#include <core/sprintf.h>
#include <core/string.h>
#include <core/jobsys.h>
#include <stdlib.h>
#include "vfs_private.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Failure idiom for file operations: log the error, then publish the status with release semantics.
// The caller returns.

#define vfs_op_fail(op, status, fmt, ...)                                         \
    do {                                                                          \
        logc_error(VFS_ID, fmt, ##__VA_ARGS__);                                   \
        atomic_store_explicit(&(op)->error_status, status, memory_order_release); \
    } while (0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File operations

// Caller holds the mount's driver_lock. Stores the op's status and returns it.
static VfsOpStatus file_open_locked(VfsOp* op, FlArena* scratch) {
    FlVfsMount* mount = op->mount;
    FlString path = op->path;

    atomic_store_explicit(&op->result_data, nullptr, memory_order_relaxed);

    if (!fl_vfs_mount_is_ready(mount)) {
        vfs_op_fail(op, VFS_ERROR_MOUNT, "No correct plugin set up for VFS");
        return VFS_ERROR_MOUNT;
    }

    VfsLocation location = vfs_tree_locate(g_vfs_state, mount, path, false, scratch);
    VfsPluginEntry* driver = location.driver;
    FlVfsOpenResult open_result = driver->plugin->open(driver->plugin_instance, location.handle, location.path,
                                                       atomic_load(&op->open_flags), mount->options);

    if (!open_result.handle) {
        vfs_op_fail(op, VFS_ERROR_FILE_NOT_FOUND, "Failed to open file: %S", path);
        return VFS_ERROR_FILE_NOT_FOUND;
    }

    op->plugin_file_handle = open_result.handle;
    op->file_driver = driver;
    atomic_store_explicit(&op->error_status, VFS_STATE_SUCCESS, memory_order_release);
    return VFS_STATE_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void vfs_op_file_open(VfsOp* op, FlArena* scratch) {
    profile_function_auto_nc("vfs:vfs_op_file_open", PROFILE_COLOR_CYAN);

    mutex_lock_auto(&op->mount->driver_lock);
    file_open_locked(op, scratch);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Reads through the file the op is chained onto. Failures publish their status and return.
static void file_read_from_file(VfsOp* op) {
    atomic_store_explicit(&op->result_data, nullptr, memory_order_relaxed);

    VfsOp* file = op->file_op;
    if (!file->plugin_file_handle) {
        vfs_op_fail(op, VFS_ERROR_DRIVER, "File not opened");
        return;
    }

    if (!op->read_buffer || op->read_size <= 0) {
        vfs_op_fail(op, VFS_ERROR_DRIVER, "Invalid read buffer or size");
        return;
    }

    FlVfsMount* mount = op->mount;
    FlVfsReadResult read_result;
    {
        mutex_lock_auto(&mount->driver_lock);

        // An empty path reads the open file.
        read_result = file->file_driver->plugin->read(file->file_driver->plugin_instance, file->plugin_file_handle,
                                                      (FlString) { 0 }, op->read_buffer, op->read_size);
    }

    if (read_result.bytes_read < 0) {
        vfs_op_fail(op, VFS_ERROR_READ, "Read operation failed");
        return;
    }

    FlVfsData* ret_data = mi_alloc_zero(FlVfsData);
    ret_data->data = op->read_buffer;
    ret_data->size = read_result.bytes_read;
    ret_data->success = true;

    atomic_store_explicit(&op->result_data, ret_data, memory_order_release);
    atomic_store_explicit(&op->error_status, VFS_STATE_SUCCESS, memory_order_release);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void vfs_op_file_read(VfsOp* op) {
    profile_function_auto_nc("vfs:vfs_op_file_read", PROFILE_COLOR_CYAN);

    file_read_from_file(op);

    // Hands the file back as soon as the read has run.
    vfs_op_release(op->file_op);
    op->file_op = nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A failed read still invokes the callback, with data == nullptr and size == 0, so it can release whatever its
// user_data owns and settle its own state. It runs before the result is published, so the op never reads ready
// while the callback is still running, and any data it returns is dropped - a failed result has no reader for it.
static void read_all_notify_failure(VfsOp* op) {
    if (op->callback) {
        op->callback(nullptr, 0, op->user_data);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// What the driver half of a read-all produced. A set error_fmt is a failure; data is then null.
typedef struct ReadAllData {
    u8* data;
    i64 size;
    VfsOpStatus status;
    const char* error_fmt;
} ReadAllData;

// Closes the opened file, drops the buffer, and reports the failure. Caller holds the mount's driver_lock.
static ReadAllData read_all_failed(VfsOp* op, u8* buffer, VfsOpStatus status, const char* error_fmt) {
    if (buffer) {
        mi_free(buffer);
    }
    VfsPluginEntry* driver = op->file_driver;
    driver->plugin->close(driver->plugin_instance, op->plugin_file_handle);
    op->plugin_file_handle = nullptr;
    return (ReadAllData) { .status = status, .error_fmt = error_fmt };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Opens the file, reads all of it and closes it, under the mount's driver_lock. The caller's callback runs after
// the lock is gone.
static ReadAllData read_all_from_driver(VfsOp* op, FlArena* scratch) {
    mutex_lock_auto(&op->mount->driver_lock);

    atomic_store(&op->open_flags, FlVfsOpenFlags_Read);
    VfsOpStatus open_status = file_open_locked(op, scratch);
    if (open_status != VFS_STATE_SUCCESS) {
        return (ReadAllData) { .status = open_status, .error_fmt = "Failed to read '%S': file not found" };
    }

    VfsPluginEntry* driver = op->file_driver;
    const FlVfsPlugin* plugin = driver->plugin;
    void* file_handle = op->plugin_file_handle;
    void* instance = driver->plugin_instance;

    FlVfsSizeResult size_result = plugin->get_size(instance, file_handle, (FlString) { 0 });
    if (size_result.size < 0) {
        return read_all_failed(op, nullptr, VFS_ERROR_READ, "Failed to get size: %S");
    }

    // An over-cap file is a read error, not a truncated success.
    if (size_result.size > VFS_MAX_READ_SIZE) {
        return read_all_failed(op, nullptr, VFS_ERROR_FILE_TOO_LARGE, "File exceeds max read size: %S");
    }

    u8* buffer = mi_malloc(size_result.size > 0 ? size_result.size : 1);
    if (!buffer) {
        return read_all_failed(op, nullptr, VFS_ERROR_READ, "Out of memory: %S");
    }

    i64 total_read = 0;

    if (size_result.size > 0) {
        FlVfsReadResult read_result = plugin->read(instance, file_handle, (FlString) { 0 }, buffer, size_result.size);
        if (read_result.bytes_read < 0) {
            return read_all_failed(op, buffer, VFS_ERROR_READ, "Read failed: %S");
        }
        total_read = read_result.bytes_read;
    } else {
        // Size is 0/unknown (e.g., gzip raw compression) - read in chunks until EOF
        mi_free(buffer);

        i64 capacity = 64 * 1024;
        buffer = mi_malloc(capacity);

        if (!buffer) {
            return read_all_failed(op, nullptr, VFS_ERROR_READ, "Out of memory: %S");
        }

        bool reached_eof = false;

        while (total_read < VFS_MAX_READ_SIZE) {
            i64 chunk_size = capacity - total_read;
            if (chunk_size <= 0) {
                capacity *= 2;
                if (capacity > VFS_MAX_READ_SIZE) {
                    capacity = VFS_MAX_READ_SIZE;
                }
                u8* new_buffer = mi_realloc(buffer, capacity);
                if (!new_buffer) {
                    return read_all_failed(op, buffer, VFS_ERROR_READ, "Out of memory: %S");
                }
                buffer = new_buffer;
                chunk_size = capacity - total_read;
            }

            FlVfsReadResult read_result
                = plugin->read(instance, file_handle, (FlString) { 0 }, buffer + total_read, chunk_size);
            if (read_result.bytes_read < 0) {
                return read_all_failed(op, buffer, VFS_ERROR_READ, "Read failed: %S");
            }

            if (read_result.bytes_read == 0) {
                reached_eof = true;
                break;
            }

            total_read += read_result.bytes_read;
        }

        if (!reached_eof) {
            // The loop stopped at the cap. Probe one byte to tell an exactly-cap-sized file from a larger one.
            u8 probe;
            FlVfsReadResult probe_result = plugin->read(instance, file_handle, (FlString) { 0 }, &probe, 1);
            if (probe_result.bytes_read < 0) {
                return read_all_failed(op, buffer, VFS_ERROR_READ, "Read failed: %S");
            }
            if (probe_result.bytes_read > 0) {
                return read_all_failed(op, buffer, VFS_ERROR_FILE_TOO_LARGE, "File exceeds max read size: %S");
            }
        }
    }

    plugin->close(instance, file_handle);
    op->plugin_file_handle = nullptr;

    return (ReadAllData) { .data = buffer, .size = total_read, .status = VFS_STATE_SUCCESS };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void vfs_op_read_all(VfsOp* op, FlArena* scratch) {
    profile_function_auto_nc("vfs:vfs_op_read_all", PROFILE_COLOR_CYAN);

    FlVfsData* result = mi_alloc_zero(FlVfsData);

    ReadAllData read = read_all_from_driver(op, scratch);
    if (read.error_fmt) {
        result->error_message = vfs_op_format_error(op, read.error_fmt, op->path);
        read_all_notify_failure(op);
        atomic_store_explicit(&op->result_data, result, memory_order_release);
        atomic_store_explicit(&op->error_status, read.status, memory_order_release);
        return;
    }

    result->data = read.data;
    result->size = read.size;

    if (op->callback) {
        FlVfsData callback_result = op->callback((void*)result->data, result->size, op->user_data);
        if (callback_result.data != result->data) {
            mi_free((void*)result->data);
            result->data = callback_result.data;
            op->callback_owns_data = true;
        }
        result->size = callback_result.size;
        result->success = callback_result.success;
    } else {
        result->success = true;
    }

    atomic_store_explicit(&op->result_data, result, memory_order_release);
    atomic_store_explicit(&op->error_status, VFS_STATE_SUCCESS, memory_order_release);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Directory listing

typedef struct ListEntryNode {
    struct ListEntryNode* next;
    FlVfsEntry entry;
} ListEntryNode;

// Gathers the driver's entries while the listing runs. Names are copied into the listing's arena; the nodes that
// chain the entries together live in scratch, only until the sorted array is built.
typedef struct ListCollector {
    FlArena* list_arena;
    FlArena* scratch;
    VfsState* state;
    ListEntryNode* first;
    ListEntryNode* last;
    u32 count;
} ListCollector;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool list_collect_entry(void* user_data, const FlVfsEntry* entry) {
    profile_function_auto_nc("vfs:list_collect_entry", PROFILE_COLOR_CYAN);
    ListCollector* collector = user_data;

    // The "." and ".." entries are dropped.
    if (fl_string_equals(entry->name, FL_S(".")) || fl_string_equals(entry->name, FL_S(".."))) {
        return true; // Continue listing
    }

    ListEntryNode* node = arena_alloc_zero(collector->scratch, ListEntryNode);
    node->entry = *entry;
    node->entry.name = fl_string_copy(collector->list_arena, entry->name);

    if (!entry->is_directory && !entry->is_archive) {
        node->entry.is_archive = vfs_name_is_archive(collector->state, entry->name);
    }

    sll_queue_push(collector->first, collector->last, node);
    collector->count++;

    return true; // Continue listing
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Directories first, then natural order by name.
static int list_entry_compare(const void* a, const void* b) {
    const FlVfsEntry* left = a;
    const FlVfsEntry* right = b;

    if (left->is_directory != right->is_directory) {
        return left->is_directory ? -1 : 1;
    }

    return fl_string_compare_natural(left->name, right->name);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Failure path for directory listing. Destroys list_arena when non-null.

static void vfs_list_fail(VfsOp* op, FlArena* list_arena, VfsOpStatus status, const char* fmt, FlString path) {
    atomic_store_explicit(&op->error_status, status, memory_order_release);

    FlVfsFileList* error_result = mi_alloc_zero(FlVfsFileList);
    error_result->success = false;
    error_result->error_message = vfs_op_format_error(op, fmt, path);
    atomic_store_explicit(&op->result_data, error_result, memory_order_release);

    if (list_arena) {
        fl_arena_destroy(list_arena);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void vfs_op_list(VfsState* self, VfsOp* op, FlArena* scratch) {
    profile_function_auto_nc("vfs:vfs_op_list", PROFILE_COLOR_CYAN);
    FlVfsMount* mount = op->mount;
    FlString path = op->path;

    atomic_store_explicit(&op->result_data, nullptr, memory_order_relaxed);

    if (!fl_vfs_mount_is_ready(mount)) {
        logc_error(VFS_ID, "No correct plugin set up for VFS mount list");
        vfs_list_fail(op, nullptr, VFS_ERROR_MOUNT, "Failed to list '%S': Mount not ready", path);
        return;
    }

    // Dedicated arena for listing (destroyed on fl_vfs_close)
    FlArena* list_arena = arena_new();

    ListCollector collector = {
        .list_arena = list_arena,
        .scratch = scratch,
        .state = self,
    };

    bool listed = false;
    {
        mutex_lock_auto(&mount->driver_lock);

        VfsLocation location = vfs_tree_locate(self, mount, path, true, scratch);
        const FlVfsPlugin* plugin = location.driver->plugin;

        if (plugin->list_directory) {
            plugin->list_directory(location.driver->plugin_instance, location.handle, location.path, list_collect_entry,
                                   &collector);
            listed = true;
        } else {
            logc_error(VFS_ID, "Plugin %S does not support directory listing", plugin->plugin_name);
        }
    }

    if (!listed) {
        vfs_list_fail(op, list_arena, VFS_ERROR_DRIVER, "Failed to list '%S': Plugin does not support listing", path);
        return;
    }

    FlVfsEntry* entries = nullptr;
    if (collector.count > 0) {
        entries = arena_alloc_array(list_arena, FlVfsEntry, collector.count);

        u32 idx = 0;
        for_each_list(node, collector.first) {
            entries[idx++] = node->entry;
        }

        qsort(entries, collector.count, sizeof(FlVfsEntry), list_entry_compare);
    }

    FlVfsFileList* file_list = arena_alloc_zero(list_arena, FlVfsFileList);
    file_list->entries = entries;
    file_list->count = collector.count;
    file_list->success = true;

    // Use atomic store with release semantics to ensure all writes to file_list are visible
    atomic_store_explicit(&op->result_data, file_list, memory_order_release);
    op->result_arena = list_arena; // Freed with the op
    atomic_store_explicit(&op->error_status, VFS_STATE_SUCCESS, memory_order_release);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mount operation: resolves the source once and publishes the result.

void vfs_op_mount(VfsState* self, VfsOp* op, FlArena* scratch) {
    profile_function_auto_nc("vfs:vfs_op_mount", PROFILE_COLOR_CYAN);
    FlVfsMount* mount = op->mount;

    // Resolve the full path - this handles nested archives and validates it's a directory
    FinalDriver result = vfs_resolve_mount_path(self, op->path, scratch);

    if (result.error != VfsResolveError_None) {
        atomic_store_explicit(&mount->status, FlVfsMountStatus_Error, memory_order_release);
        atomic_store_explicit(&op->error_status, VFS_ERROR_MOUNT, memory_order_release);
        return;
    }

    mount->root_node->plugin_entry = result.plugin_entry;
    mount->root_node->handle = result.handle;
    atomic_store_explicit(&mount->status, FlVfsMountStatus_Ready, memory_order_release);
    atomic_store_explicit(&op->error_status, VFS_STATE_SUCCESS, memory_order_release);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Job scheduling

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Core execution logic - shared by both async and sync paths. Ends by publishing done, after the result and status.
static void vfs_ops_execute(VfsOp* op, FlArena* scratch) {
    profile_function_auto_nc("vfs:vfs_ops_execute", PROFILE_COLOR_CYAN);
    VfsState* self = g_vfs_state;

    switch (op->op_type) {
        case VfsOp_Mount:
            vfs_op_mount(self, op, scratch);
            break;

        case VfsOp_MountList:
            vfs_op_list(self, op, scratch);
            break;

        case VfsOp_FileOpen:
            vfs_op_file_open(op, scratch);
            break;

        case VfsOp_FileRead:
            vfs_op_file_read(op);
            break;

        case VfsOp_ReadAll:
            vfs_op_read_all(op, scratch);
            break;
    }

    atomic_store_explicit(&op->done, true, memory_order_release);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Async job wrapper - called by job system. The job's reference is returned last: it may be the final one, in which
// case the op is destroyed, and its release hook runs, right here on the worker.
void vfs_ops_do_job(void* data, FlJobsWorkerInfo info) {
    VfsOp* op = (VfsOp*)data;
    vfs_ops_execute(op, info.scratch);
    vfs_op_release(op);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Synchronous execution - called when already in job context
void vfs_ops_execute_sync(VfsOp* op) {
    arena_scratch_auto(temp);
    vfs_ops_execute(op, temp.arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlJobHandle vfs_ops_schedule_job(FlVfsMount* mount, FlJobsFunc func, void* user_data) {
    profile_function_auto_nc("vfs:vfs_ops_schedule_job", PROFILE_COLOR_CYAN);

    // Handle 0 means mount was completed synchronously - schedule immediately
    if (mount->job_handle == 0) {
        return fl_jobs_add_job(func, user_data);
    }

    FlJobsResult mount_status = fl_jobs_is_finished(mount->job_handle);

    if (mount_status == FlJobsResult_NotFinished) {
        // Use job dependency to automatically schedule when mount completes
        return fl_jobs_add_job_with_dependency(func, user_data, mount->job_handle);
    }

    return fl_jobs_add_job(func, user_data);
}
