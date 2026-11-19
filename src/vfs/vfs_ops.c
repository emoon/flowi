// VFS file and directory operations: read, list, open.

#include <core/log.h>
#include <core/memory.h>
#include <core/path.h>
#include <core/profile.h>
#include <core/sprintf.h>
#include <core/string.h>
#include <core/jobsys.h>
#include <stdlib.h>
#include "vfs_private.h"

// Failure idiom for file operations: log the error, then publish the status with release semantics.
// The caller returns.

#define vfs_op_fail(op, status, fmt, ...)                                         \
    do {                                                                          \
        logc_error(VFS_ID, fmt, ##__VA_ARGS__);                                   \
        atomic_store_explicit(&(op)->error_status, status, memory_order_release); \
    } while (0)

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

void vfs_op_file_open(VfsOp* op, FlArena* scratch) {
    profile_function_auto_nc("vfs:vfs_op_file_open", PROFILE_COLOR_CYAN);

    mutex_lock_auto(&op->mount->driver_lock);
    file_open_locked(op, scratch);
}

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

void vfs_op_file_read(VfsOp* op) {
    profile_function_auto_nc("vfs:vfs_op_file_read", PROFILE_COLOR_CYAN);

    file_read_from_file(op);

    // Hands the file back as soon as the read has run.
    vfs_op_release(op->file_op);
    op->file_op = nullptr;
}

// A failed read still invokes the callback, with data == nullptr and size == 0, so it can release whatever its
// user_data owns and settle its own state. It runs before the result is published, so the op never reads ready
// while the callback is still running, and any data it returns is dropped: a failed result has no reader for it.
static void read_all_notify_failure(VfsOp* op) {
    if (op->callback) {
        op->callback(nullptr, 0, op->user_data);
    }
}

// What the driver half of a read-all produced. A set error_fmt is a failure; data is then nullptr.
typedef struct ReadAllData {
    u8* data;
    i64 size;
    VfsOpStatus status;
    const char* error_fmt;
} ReadAllData;

static void close_op_file(VfsOp* op) {
    VfsPluginEntry* driver = op->file_driver;
    driver->plugin->close(driver->plugin_instance, op->plugin_file_handle);
    op->plugin_file_handle = nullptr;
}

// Closes the opened file and reports the failure. Caller holds the mount's driver_lock.
static ReadAllData read_all_failed(VfsOp* op, VfsOpStatus status, const char* error_fmt) {
    close_op_file(op);
    return (ReadAllData) { .status = status, .error_fmt = error_fmt };
}

// A read-all that failed part way: drops the buffer and reports the failure. The file is closed by the caller.
static ReadAllData read_failure(u8* buffer, VfsOpStatus status, const char* error_fmt) {
    if (buffer) {
        mi_free(buffer);
    }
    return (ReadAllData) { .status = status, .error_fmt = error_fmt };
}

// The open file a read-all reads from.
typedef struct {
    const FlVfsPlugin* plugin;
    void* instance;
    void* handle;
} ReadSource;

static FlVfsReadResult read_source(const ReadSource* source, u8* buffer, i64 size) {
    return source->plugin->read(source->instance, source->handle, (FlString) { 0 }, buffer, size);
}

static ReadAllData read_sized(const ReadSource* source, u8* buffer, i64 size) {
    FlVfsReadResult read_result = read_source(source, buffer, size);
    if (read_result.bytes_read < 0) {
        return read_failure(buffer, VFS_ERROR_READ, "Read failed: %S");
    }

    return (ReadAllData) { .data = buffer, .size = read_result.bytes_read, .status = VFS_STATE_SUCCESS };
}

typedef struct {
    u8* buffer;
    i64 capacity;
    i64 total;
} GrowBuffer;

// False, with the buffer untouched, when it cannot grow.
static bool grow_buffer(GrowBuffer* grow) {
    i64 capacity = grow->capacity * 2;
    if (capacity > VFS_MAX_READ_SIZE) {
        capacity = VFS_MAX_READ_SIZE;
    }

    u8* new_buffer = mi_realloc(grow->buffer, capacity);
    if (!new_buffer) {
        return false;
    }

    grow->buffer = new_buffer;
    grow->capacity = capacity;
    return true;
}

// Reads chunks until EOF or the cap. A result without error_fmt is not a failure.
static ReadAllData read_chunks(const ReadSource* source, GrowBuffer* grow, bool* reached_eof) {
    while (grow->total < VFS_MAX_READ_SIZE) {
        if (grow->total >= grow->capacity && !grow_buffer(grow)) {
            return (ReadAllData) { .status = VFS_ERROR_READ, .error_fmt = "Out of memory: %S" };
        }

        FlVfsReadResult read_result = read_source(source, grow->buffer + grow->total, grow->capacity - grow->total);
        if (read_result.bytes_read < 0) {
            return (ReadAllData) { .status = VFS_ERROR_READ, .error_fmt = "Read failed: %S" };
        }

        if (read_result.bytes_read == 0) {
            *reached_eof = true;
            break;
        }

        grow->total += read_result.bytes_read;
    }

    return (ReadAllData) { 0 };
}

// The loop stopped at the cap. Probe one byte to tell an exactly-cap-sized file from a larger one.
static ReadAllData probe_past_cap(const ReadSource* source) {
    u8 probe;
    FlVfsReadResult probe_result = read_source(source, &probe, 1);

    if (probe_result.bytes_read < 0) {
        return (ReadAllData) { .status = VFS_ERROR_READ, .error_fmt = "Read failed: %S" };
    }

    if (probe_result.bytes_read > 0) {
        return (ReadAllData) { .status = VFS_ERROR_FILE_TOO_LARGE, .error_fmt = "File exceeds max read size: %S" };
    }

    return (ReadAllData) { 0 };
}

// The size is 0 or unknown (e.g. gzip raw compression): reads in chunks until EOF. Takes over initial_buffer.
static ReadAllData read_until_eof(const ReadSource* source, u8* initial_buffer) {
    mi_free(initial_buffer);

    GrowBuffer grow = { .capacity = 64 * 1024 };
    grow.buffer = mi_malloc(grow.capacity);

    if (!grow.buffer) {
        return read_failure(nullptr, VFS_ERROR_READ, "Out of memory: %S");
    }

    bool reached_eof = false;
    ReadAllData failure = read_chunks(source, &grow, &reached_eof);

    if (!failure.error_fmt && !reached_eof) {
        failure = probe_past_cap(source);
    }

    if (failure.error_fmt) {
        return read_failure(grow.buffer, failure.status, failure.error_fmt);
    }

    return (ReadAllData) { .data = grow.buffer, .size = grow.total, .status = VFS_STATE_SUCCESS };
}

// Opens the file, reads all of it and closes it, under the mount's driver_lock. The caller's callback runs after
// the lock is gone.
static ReadAllData read_all_from_driver(VfsOp* op, FlArena* scratch) {
    mutex_lock_auto(&op->mount->driver_lock);

    atomic_store(&op->open_flags, FlVfsOpenFlags_Read);
    VfsOpStatus open_status = file_open_locked(op, scratch);
    if (open_status != VFS_STATE_SUCCESS) {
        return (ReadAllData) { .status = open_status, .error_fmt = "Failed to read '%S': file not found" };
    }

    const ReadSource source = { .plugin = op->file_driver->plugin,
                                .instance = op->file_driver->plugin_instance,
                                .handle = op->plugin_file_handle };

    FlVfsSizeResult size_result = source.plugin->get_size(source.instance, source.handle, (FlString) { 0 });
    if (size_result.size < 0) {
        return read_all_failed(op, VFS_ERROR_READ, "Failed to get size: %S");
    }

    // An over-cap file is a read error, not a truncated success.
    if (size_result.size > VFS_MAX_READ_SIZE) {
        return read_all_failed(op, VFS_ERROR_FILE_TOO_LARGE, "File exceeds max read size: %S");
    }

    u8* buffer = mi_malloc(size_result.size > 0 ? size_result.size : 1);
    if (!buffer) {
        return read_all_failed(op, VFS_ERROR_READ, "Out of memory: %S");
    }

    ReadAllData read
        = size_result.size > 0 ? read_sized(&source, buffer, size_result.size) : read_until_eof(&source, buffer);

    close_op_file(op);
    return read;
}

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

// Directories first, then natural order by name.
static int list_entry_compare(const void* a, const void* b) {
    const FlVfsEntry* left = a;
    const FlVfsEntry* right = b;

    if (left->is_directory != right->is_directory) {
        return left->is_directory ? -1 : 1;
    }

    return fl_string_compare_natural(left->name, right->name);
}

// Failure path for directory listing.

static void vfs_list_fail(VfsOp* op, VfsOpStatus status, const char* fmt) {
    atomic_store_explicit(&op->error_status, status, memory_order_release);

    FlVfsFileList* error_result = mi_alloc_zero(FlVfsFileList);
    error_result->success = false;
    error_result->error_message = vfs_op_format_error(op, fmt, op->path);
    atomic_store_explicit(&op->result_data, error_result, memory_order_release);
}

void vfs_op_list(VfsState* self, VfsOp* op, FlArena* scratch) {
    profile_function_auto_nc("vfs:vfs_op_list", PROFILE_COLOR_CYAN);
    FlVfsMount* mount = op->mount;
    FlString path = op->path;

    atomic_store_explicit(&op->result_data, nullptr, memory_order_relaxed);

    if (!fl_vfs_mount_is_ready(mount)) {
        logc_error(VFS_ID, "No correct plugin set up for VFS mount list");
        vfs_list_fail(op, VFS_ERROR_MOUNT, "Failed to list '%S': Mount not ready");
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
        vfs_list_fail(op, VFS_ERROR_DRIVER, "Failed to list '%S': Plugin does not support listing");
        fl_arena_destroy(list_arena);
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

    // The release store publishes every write to file_list above.
    atomic_store_explicit(&op->result_data, file_list, memory_order_release);
    op->result_arena = list_arena; // Freed with the op
    atomic_store_explicit(&op->error_status, VFS_STATE_SUCCESS, memory_order_release);
}

// Mount operation: resolves the source once and publishes the result.

void vfs_op_mount(VfsState* self, VfsOp* op, FlArena* scratch) {
    profile_function_auto_nc("vfs:vfs_op_mount", PROFILE_COLOR_CYAN);
    FlVfsMount* mount = op->mount;

    // Resolving the full path handles nested archives and checks that it is a directory.
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

// Job scheduling

// Runs the op; shared by the async and sync paths. Ends by publishing done, after the result and status.
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

// Job entry point. The job's reference is returned last: it may be the final one, in which
// case the op is destroyed, and its release hook runs, right here on the worker.
void vfs_ops_do_job(void* data, FlJobsWorkerInfo info) {
    VfsOp* op = (VfsOp*)data;
    vfs_ops_execute(op, info.scratch);
    vfs_op_release(op);
}

// Synchronous execution, for a caller already inside a job.
void vfs_ops_execute_sync(VfsOp* op) {
    arena_scratch_auto(temp);
    vfs_ops_execute(op, temp.arena);
}

FlJobHandle vfs_ops_schedule_job(FlVfsMount* mount, FlJobsFunc func, void* user_data) {
    profile_function_auto_nc("vfs:vfs_ops_schedule_job", PROFILE_COLOR_CYAN);

    // Handle 0 means the mount completed synchronously, so schedule at once.
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
