#include "vfs.h"
#include <core/arena.h>
#include <core/assert.h>
#include <core/log.h>
#include <core/profile.h>
#include <core/sprintf.h>
#include <core/string_allocator.h>
#include <core/string.h>
#include <core/jobsys.h>
#include <core/os/os.h>
#include "vfs_internal.h"
#include "vfs_private.h"
#include "drivers/localfs.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Global state (exported to vfs_resolve.c and vfs_ops.c)

LogChannelId VFS_ID = LOG_CHANNEL_INVALID;
VfsState* g_vfs_state = nullptr;
void (*g_vfs_test_after_read_scheduled)(FlVfsHandle read) = nullptr;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Nil VFS Entry (Read-Only Global)
//
// Represents invalid/empty VFS entry. Functions return pointers to this instead of nullptr.

const FlVfsEntry fl_nil_vfs_entry = { 0 };

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_vfs_init(FlArena* arena) {
    profile_function_auto_nc("vfs:fl_vfs_init", PROFILE_COLOR_CYAN);
    VFS_ID = fl_log_register_channel(FL_S("VFS"));

    VfsState* self = g_vfs_state;

    if (self != nullptr) {
        return false;
    }

    const bool owns_arena = (arena == nullptr);
    if (owns_arena) {
        arena = arena_new();
    }

    self = arena_alloc_zero(arena, VfsState);

    self->main_arena = arena;
    self->owns_main_arena = owns_arena;

    mutex_init(&self->handle_lock);
    self->next_handle_id = 1;

    hashmap_new(&self->slots, self->main_arena, 256);
    hashmap_new(&self->retiring, self->main_arena, 64);

    g_vfs_state = self;

    // The built-in LocalFS driver is the catch-all fallback and stays last (see builtin_fallback).
    vfs_register_driver(localfs_plugin(), nullptr);
    self->builtin_fallback = self->plugin_last;

    logc_info(VFS_ID, "VFS initialized");

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void vfs_register_driver(const FlVfsPlugin* plugin, void* instance) {
    profile_function_auto_nc("vfs:vfs_register_driver", PROFILE_COLOR_CYAN);
    VfsState* self = g_vfs_state;

    if (self == nullptr) {
        logc_error(VFS_ID, "vfs_register_driver called before fl_vfs_init");
        return;
    }
    if (plugin == nullptr) {
        return;
    }

    VfsPluginEntry* entry = arena_alloc_zero(self->main_arena, VfsPluginEntry);
    entry->plugin = plugin;
    entry->plugin_instance = instance;

    VfsPluginEntry* fallback = self->builtin_fallback;
    if (fallback != nullptr && plugin != fallback->plugin) {
        // Inserted immediately before the LocalFS fallback.
        entry->next = fallback;
        entry->prev = fallback->prev;
        if (fallback->prev != nullptr) {
            fallback->prev->next = entry;
        } else {
            self->plugin_first = entry;
        }
        fallback->prev = entry;
    } else {
        // Append at the tail (the fallback itself during fl_vfs_init, or before any fallback exists).
        entry->next = nullptr;
        entry->prev = self->plugin_last;
        if (self->plugin_last != nullptr) {
            self->plugin_last->next = entry;
        } else {
            self->plugin_first = entry;
        }
        self->plugin_last = entry;
    }

    logc_info(VFS_ID, "VFS driver registered: %S", plugin->plugin_name);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Reference counting

// Drops one reference to the mount. The last one closes every node handle, children first, and frees it, on whichever
// thread that is.
static void vfs_mount_release(FlVfsMount* mount) {
    if (atomic_fetch_sub_explicit(&mount->refs, 1, memory_order_acq_rel) != 1) {
        return;
    }

    vfs_tree_cleanup_node(mount->root_node);
    fl_arena_destroy(mount->nodes_arena);

    mutex_destroy(&mount->driver_lock);

    // StringAllocator is allocated within the arena, so destroying the arena frees both
    fl_arena_destroy(mount->arena);

    // Unlisted last.
    VfsState* self = g_vfs_state;
    mutex_lock(&self->handle_lock);
    sll_queue_remove(self->mount_first, self->mount_last, mount);
    mutex_unlock(&self->handle_lock);

    mi_free(mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// An error_message needs no freeing here: the op owns that storage (see vfs_op_format_error).
static void vfs_free_data_result(FlVfsData* result, bool free_data_buffer) {
    if (free_data_buffer && result->data) {
        mi_free((void*)result->data);
    }
    mi_free(result);
}

// Frees what the op's job produced.
static void vfs_op_free_result(VfsOp* op) {
    FlVfsData* result = atomic_load_explicit(&op->result_data, memory_order_acquire);

    switch (op->op_type) {
        case VfsOp_Mount: {
            // Owns nothing of its own: a mount's storage belongs to the FlVfsMount, not to this op.
            break;
        }

        case VfsOp_MountList: {
            if (result) {
                FlVfsFileList* list = (FlVfsFileList*)result;
                // With a result_arena the listing lives in that arena, destroyed below; without one this is
                // the error path, where the struct was mi_alloc'd on its own.
                if (!op->result_arena) {
                    mi_free(list);
                }
            }
            break;
        }

        case VfsOp_FileOpen: {
            // The driver handle is closed by vfs_op_destroy, outside every lock.
            break;
        }

        case VfsOp_FileRead: {
            if (result) {
                // result->data points at the caller-provided read buffer (fl_vfs_read rejects a null buffer),
                // so the data is never ours to free - only the result struct.
                vfs_free_data_result(result, false);
            }
            break;
        }

        case VfsOp_ReadAll: {
            if (result) {
                vfs_free_data_result(result, !op->callback_owns_data);
            }
            break;
        }
    }

    if (op->result_arena) {
        fl_arena_destroy(op->result_arena);
    }

    fl_string_free(op->error_text);
    op->error_text = (FlString) { 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Runs when the last reference goes: the slot is closed (or never existed), the job has run (or never will) and no
// read is chained onto the op. Holds no lock while it works.
static void vfs_op_destroy(VfsOp* op) {
    VfsState* self = g_vfs_state;
    FlVfsMount* mount = op->mount;

    if (op->op_type == VfsOp_FileOpen && op->plugin_file_handle) {
        op->file_driver->plugin->close(op->file_driver->plugin_instance, op->plugin_file_handle);
    }

    vfs_op_free_result(op);

    if (op->release) {
        op->release(op->user_data);
    }

    // Only a read that never ran still has its file; one that ran handed it back already.
    if (op->file_op) {
        vfs_op_release(op->file_op);
    }

    // Frees the path from the mount's strings.
    string_allocator_free(mount->strings, op->path);

    // Leaves the mount's list and the retiring set last.
    mutex_lock(&self->handle_lock);
    dll_remove(mount->ops_first, mount->ops_last, op);
    mount->ops_count--;
    hashmap_remove(&self->retiring, op->id);
    mutex_unlock(&self->handle_lock);

    mi_free(op);

    // After the op: this may be the reference that frees the mount.
    vfs_mount_release(mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Takes another reference on an op the caller already holds one of.
static void vfs_op_retain(VfsOp* op) {
    atomic_fetch_add_explicit(&op->refs, 1, memory_order_relaxed);
}

void vfs_op_release(VfsOp* op) {
    if (atomic_fetch_sub_explicit(&op->refs, 1, memory_order_acq_rel) == 1) {
        vfs_op_destroy(op);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Slot table

VfsOp* vfs_op_acquire(FlVfsHandle id) {
    VfsState* self = g_vfs_state;

    if (!self || id == FL_VFS_HANDLE_INVALID) {
        return nullptr;
    }

    // The slot holds a reference for as long as it lists the op, so the count cannot be zero here.
    mutex_lock_auto(&self->handle_lock);
    VfsOp** slot = hashmap_get(&self->slots, id);
    if (!slot) {
        return nullptr;
    }

    atomic_fetch_add_explicit(&(*slot)->refs, 1, memory_order_relaxed);
    return *slot;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Caller holds handle_lock. An id is in use while an open op holds it and while a closed one is still running.
static bool vfs_id_in_use_locked(VfsState* self, FlVfsHandle id) {
    return hashmap_get(&self->slots, id) || hashmap_get(&self->retiring, id);
}

// Caller holds handle_lock. Picks the op's id and lists it, taking the slot's reference. A caller-chosen id is refused
// while it is in use.
static bool vfs_slot_insert_locked(VfsState* self, VfsOp* op, FlVfsHandle reuse_handle) {
    if (reuse_handle != FL_VFS_HANDLE_INVALID) {
        if (vfs_id_in_use_locked(self, reuse_handle)) {
            return false;
        }
        op->id = reuse_handle;
    } else {
        do {
            op->id = self->next_handle_id++;
        } while (op->id == FL_VFS_HANDLE_INVALID || vfs_id_in_use_locked(self, op->id));
    }

    atomic_fetch_add_explicit(&op->refs, 1, memory_order_relaxed);
    hashmap_insert(&self->slots, op->id, op);
    return true;
}

// Caller holds handle_lock. Unlists op if its id still names it, and reports whether it did: the slot's reference is
// then the caller's to release, outside the lock. The id stays reserved until the op is destroyed. False means a
// concurrent close or mount close unlisted it first, so exactly one of them drops the slot's reference.
static bool vfs_slot_remove_locked(VfsState* self, const VfsOp* op) {
    VfsOp** slot = hashmap_get(&self->slots, op->id);
    if (!slot || *slot != op) {
        return false;
    }

    hashmap_remove(&self->slots, op->id);
    const bool retiring = true;
    hashmap_insert(&self->retiring, op->id, retiring);
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Op creation and scheduling

// Everything an op is born with, filled with designated initializers at the call site. A zeroed field is the "not
// applicable" case for every one of them - FL_VFS_HANDLE_INVALID is 0, as is an empty FlString.
typedef struct VfsOpParams {
    FlVfsMount* mount;
    FlString path;
    VfsOperationType op_type;
    int depth;
    FlVfsReadCallback callback;
    void* user_data;
    FlVfsReleaseCallback release;
    FlVfsHandle reuse_handle;
    u32 open_flags;
    bool unlisted; // No id: nothing can name the op, only its job holds it (the mount's own)
} VfsOpParams;

// Allocates an op and registers it: on its mount's list and, unless it is unlisted, in the slot table under a new id.
// It returns holding the caller's reference, plus the slot's, but not yet scheduled. Refused (nullptr) when the mount
// is closing or a caller-chosen id is still in use.
//
// Until it is registered the op is private to this thread, so its fields need no lock.
static VfsOp* vfs_op_create(VfsState* self, const VfsOpParams* params) {
    FlVfsMount* mount = params->mount;

    VfsOp* op = mi_alloc_zero(VfsOp);
    atomic_init(&op->refs, 1);

    // The path is copied into the mount's thread-safe StringAllocator; an empty or static string copies to itself
    // without allocating.
    op->path = string_allocator_copy(mount->strings, params->path);

    op->mount = mount;
    op->op_type = params->op_type;
    op->callback = params->callback;
    op->user_data = params->user_data;
    op->release = params->release;
    op->depth = params->depth;

    atomic_init(&op->done, false);
    atomic_init(&op->scheduled, false);
    atomic_init(&op->job_handle, 0);
    atomic_init(&op->last_job, 0);
    atomic_init(&op->result_data, nullptr);
    atomic_init(&op->error_status, VFS_STATE_PENDING);
    atomic_init(&op->open_flags, params->open_flags);

    const char* refusal = nullptr;

    mutex_lock(&self->handle_lock);
    if (mount->closing) {
        refusal = "the mount is closing";
    } else if (!params->unlisted && !vfs_slot_insert_locked(self, op, params->reuse_handle)) {
        refusal = "the handle is still in use (open, or closed with its job not finished)";
    } else {
        atomic_fetch_add_explicit(&mount->refs, 1, memory_order_relaxed);
        dll_push_back(mount->ops_first, mount->ops_last, op);
        mount->ops_count++;
    }
    mutex_unlock(&self->handle_lock);

    if (refusal) {
        logc_warning(VFS_ID, "Operation on mount %p refused: %s", (void*)mount, refusal);
        string_allocator_free(mount->strings, op->path);
        mi_free(op);
        return nullptr;
    }

    return op;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Hands op to the job system, once, behind chain_after when that is set and behind its mount's own job otherwise. The
// job gets a reference of its own, taken first because a job scheduled from a worker runs inline and may finish, and
// release it, before this returns. The caller's reference is held throughout.
static void vfs_op_schedule(VfsOp* op, FlJobHandle chain_after) {
    if (atomic_exchange_explicit(&op->scheduled, true, memory_order_acq_rel)) {
        return;
    }

    atomic_fetch_add_explicit(&op->refs, 1, memory_order_relaxed);
    FlJobHandle job = chain_after ? fl_jobs_add_job_with_dependency(vfs_ops_do_job, op, chain_after)
                                  : vfs_ops_schedule_job(op->mount, vfs_ops_do_job, op);
    atomic_store_explicit(&op->job_handle, job, memory_order_release);
    atomic_store_explicit(&op->last_job, job, memory_order_release);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Create, schedule and hand back the id. The id is the caller's only reference: the creator's is dropped here.
static FlVfsHandle allocate_op(VfsState* self, const VfsOpParams* params) {
    VfsOp* op = vfs_op_create(self, params);
    if (!op) {
        return FL_VFS_HANDLE_INVALID; // Refused, and logged, by vfs_op_create
    }

    FlVfsHandle id = op->id;
    vfs_op_schedule(op, FL_JOB_HANDLE_INVALID);
    vfs_op_release(op);

    return id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Waiting on the jobs of live ops

typedef struct VfsJobList {
    FlJobHandle* jobs;
    u64 count;
} VfsJobList;

// Caller holds handle_lock. Collects the jobs the ops of mount still name, or of every listed mount when mount is
// null, including ops whose id was closed but whose job has not finished. The handles are plain values: no op pointer
// leaves the lock.
static VfsJobList vfs_collect_jobs_locked(VfsState* self, FlVfsMount* mount, FlArena* arena) {
    FlVfsMount* first = mount ? mount : self->mount_first;

    u64 capacity = 0;
    for (FlVfsMount* m = first; m != nullptr; m = mount ? nullptr : m->next) {
        capacity += 2ull * m->ops_count;
    }

    VfsJobList list = { .jobs = arena_alloc_array(arena, FlJobHandle, capacity) };
    for (FlVfsMount* m = first; m != nullptr; m = mount ? nullptr : m->next) {
        for_each_list(op, m->ops_first) {
            FlJobHandle job = atomic_load_explicit(&op->job_handle, memory_order_acquire);
            FlJobHandle last = atomic_load_explicit(&op->last_job, memory_order_acquire);
            if (job) {
                list.jobs[list.count++] = job;
            }
            if (last && last != job) {
                list.jobs[list.count++] = last;
            }
        }
    }

    return list;
}

// Waits, without any lock, for the jobs of one mount's ops or of all of them (mount null).
static void vfs_wait_jobs(VfsState* self, FlVfsMount* mount) {
    arena_scratch_auto(temp);

    mutex_lock(&self->handle_lock);
    VfsJobList list = vfs_collect_jobs_locked(self, mount, temp.arena);
    mutex_unlock(&self->handle_lock);

    for (u64 i = 0; i < list.count; ++i) {
        fl_jobs_wait(list.jobs[i]);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mount management

static _Atomic u64 s_next_mount_id = 1;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlVfsMountResult fl_vfs_mount_with_options(FlString source_path, FlVfsMountOptions options) {
    logc_debug(VFS_ID, "mounting %S", source_path);

    profile_function_auto_nc("vfs:fl_vfs_mount_with_options", PROFILE_COLOR_CYAN);
    VfsState* self = g_vfs_state;

    FlVfsMountResult result
        = { .mount = nullptr, .status = FlVfsMountErrorStatus_Success, .error_message = string_empty() };

    if (self == nullptr) {
        result.status = FlVfsMountErrorStatus_NotInitialized;
        result.error_message = FL_S("VFS system not initialized");
        logc_error(VFS_ID, "VFS mount failed: %S", result.error_message);
        return result;
    }

    if (string_is_empty(source_path)) {
        result.status = FlVfsMountErrorStatus_InvalidPath;
        result.error_message = FL_S("Source path is empty");
        logc_error(VFS_ID, "VFS mount failed: %S", result.error_message);
        return result;
    }

    FlVfsMount* mount = mi_alloc_zero(FlVfsMount);

    atomic_init(&mount->status, FlVfsMountStatus_Pending);
    atomic_init(&mount->refs, 1);
    mount->arena = arena_new();
    mount->strings = string_allocator_new(mount->arena);
    mutex_init(&mount->driver_lock);
    mount->nodes_arena = arena_new();
    mount->root_node = vfs_tree_create_node(mount, nullptr, FL_S(""));
    mount->root_node->is_directory = true;

    mount->source_path = string_allocator_copy(mount->strings, source_path);
    mount->id = atomic_fetch_add(&s_next_mount_id, 1);

    mount->options.cache_validation = options.cache_validation;
    if (options.cache_dir.length > 0) {
        mount->options.cache_dir = string_allocator_copy(mount->strings, options.cache_dir);
    }

    // Resolving the source runs a driver, so it happens after the lock.
    mutex_lock(&self->handle_lock);
    sll_queue_push(self->mount_first, self->mount_last, mount);
    mutex_unlock(&self->handle_lock);

    VfsOp* op = vfs_op_create(self, &(VfsOpParams) {
                                        .mount = mount,
                                        .path = source_path,
                                        .op_type = VfsOp_Mount,
                                        .unlisted = true,
                                    });

    if (!fl_jobs_is_main_thread()) {
        vfs_ops_execute_sync(op);
    } else {
        vfs_op_schedule(op, FL_JOB_HANDLE_INVALID);
        mount->job_handle = atomic_load_explicit(&op->job_handle, memory_order_acquire);
    }
    vfs_op_release(op);

    result.mount = mount;
    result.status = FlVfsMountErrorStatus_Success;

    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Public API

void fl_vfs_set_priority(FlVfsHandle handle, FlVfsLoadPriority priority) {
    profile_function_auto_nc("vfs:fl_vfs_set_priority", PROFILE_COLOR_CYAN);

    // An invalid handle is ignored.
    VfsOp* op = vfs_op_acquire(handle);
    if (!op) {
        return;
    }

    FlJobHandle job = atomic_load_explicit(&op->job_handle, memory_order_acquire);
    if (job) {
        // Map VFS priority to job priority (clamp Highest to High)
        int prio = (int)priority;
        JobPriority job_priority = (prio > (int)JobPriority_High) ? JobPriority_High : (JobPriority)prio;
        fl_jobs_set_priority(job, job_priority);
    }

    vfs_op_release(op);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlVfsDirHandle fl_vfs_get_listing(FlVfsMount* mount, FlString relative_path, int depth) {
    profile_function_auto_nc("vfs:fl_vfs_get_listing", PROFILE_COLOR_CYAN);

    FL_VALIDATE_LOG(mount != nullptr, FL_VFS_DIR_HANDLE_INVALID, logc_error(VFS_ID, "Invalid mount for get_listing"));

    return allocate_op(g_vfs_state, &(VfsOpParams) {
                                        .mount = mount,
                                        .path = relative_path,
                                        .op_type = VfsOp_MountList,
                                        .depth = depth,
                                    });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool vfs_op_is_done(const VfsOp* op) {
    return atomic_load_explicit(&op->done, memory_order_acquire);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_vfs_wait(FlVfsHandle handle) {
    profile_function_auto_nc("vfs:fl_vfs_wait", PROFILE_COLOR_CYAN);
    VfsOp* op = vfs_op_acquire(handle);
    if (!op) {
        return;
    }

    // Wait lock-free. last_job covers reads chained onto a file handle after its open job finished.
    FlJobHandle job = atomic_load_explicit(&op->job_handle, memory_order_acquire);
    FlJobHandle last = atomic_load_explicit(&op->last_job, memory_order_acquire);
    if (job) {
        fl_jobs_wait(job);
    }
    if (last && last != job) {
        fl_jobs_wait(last);
    }

    // fl_jobs_wait() is a no-op on a job worker, which is the one place the op can still be running here.
    if (!fl_jobs_is_main_thread() && !vfs_op_is_done(op)) {
        logc_error(VFS_ID,
                   "fl_vfs_wait(%u) from a job worker cannot block; the operation is still running. Wait for an "
                   "in-flight handle from the main thread.",
                   handle);
    }

    vfs_op_release(op);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void vfs_wait_all(void) {
    profile_function_auto_nc("vfs:vfs_wait_all", PROFILE_COLOR_CYAN);
    VfsState* self = g_vfs_state;

    FL_VALIDATE(self != nullptr);

    // Ops published after the collection belong to a later wait_all call.
    vfs_wait_jobs(self, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_vfs_is_ready(FlVfsHandle handle) {
    profile_function_auto_nc("vfs:fl_vfs_is_ready", PROFILE_COLOR_CYAN);
    VfsOp* op = vfs_op_acquire(handle);
    if (!op) {
        return false;
    }

    bool ready = vfs_op_is_done(op);
    vfs_op_release(op);
    return ready;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// The result payload of a finished read/list op, or nullptr when the op is not finished or finished without one. Then
// *success is the op's outcome: false while unfinished, else its error-status success. Shared empty-result path of
// fl_vfs_get_data / fl_vfs_get_file_list. The payload stays valid until fl_vfs_close.
static void* vfs_op_payload(const VfsOp* op, bool* success) {
    *success = false;

    if (!vfs_op_is_done(op)) {
        return nullptr;
    }

    void* payload = atomic_load_explicit(&op->result_data, memory_order_acquire);
    if (!payload) {
        *success = vfs_is_success(atomic_load_explicit(&op->error_status, memory_order_acquire));
    }
    return payload;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlVfsData fl_vfs_get_data(FlVfsHandle handle) {
    profile_function_auto_nc("vfs:fl_vfs_get_data", PROFILE_COLOR_CYAN);
    FlVfsData data = { 0 };

    VfsOp* op = vfs_op_acquire(handle);
    if (!op) {
        return data;
    }

    FlVfsData* payload = vfs_op_payload(op, &data.success);
    if (payload) {
        data = *payload;
    }

    vfs_op_release(op);
    return data;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

VfsOpStatus vfs_get_status(FlVfsHandle handle) {
    profile_function_auto_nc("vfs:vfs_get_status", PROFILE_COLOR_CYAN);
    VfsOp* op = vfs_op_acquire(handle);
    if (!op) {
        return VFS_STATE_ERROR;
    }

    VfsOpStatus status = VFS_STATE_PENDING;
    if (vfs_op_is_done(op)) {
        status = atomic_load_explicit(&op->error_status, memory_order_acquire);
    }

    vfs_op_release(op);
    return status;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlVfsFileList fl_vfs_get_file_list(FlVfsDirHandle handle) {
    profile_function_auto_nc("vfs:fl_vfs_get_file_list", PROFILE_COLOR_CYAN);
    FlVfsFileList list = { 0 };

    VfsOp* op = vfs_op_acquire(handle);
    if (!op) {
        return list;
    }

    FlVfsFileList* payload = vfs_op_payload(op, &list.success);
    if (payload) {
        list = *payload;
    }

    vfs_op_release(op);
    return list;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Error handling

FlString vfs_op_format_error(VfsOp* op, const char* fmt, ...) {
    profile_function_auto_nc("vfs:vfs_op_format_error", PROFILE_COLOR_CYAN);
    arena_scratch_auto(temp);

    va_list args;
    va_start(args, fmt);
    FlString formatted = vsprintf_arena(temp.arena, fmt, args);
    va_end(args);

    fl_string_free(op->error_text);
    op->error_text = fl_string_copy_malloc(formatted);
    return op->error_text;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// VFS close

// The ops that run against memory the caller lent for their duration: fl_vfs_read's buffer.
static bool vfs_op_borrows_caller_memory(VfsOperationType op_type) {
    return op_type == VfsOp_FileRead;
}

void fl_vfs_close(FlVfsHandle handle) {
    profile_function_auto_nc("vfs:fl_vfs_close", PROFILE_COLOR_CYAN);
    VfsState* self = g_vfs_state;

    VfsOp* op = vfs_op_acquire(handle);
    if (!op) {
        return;
    }

    // The one close that waits: the job must be done with the caller's memory first. On a job worker the handle stays
    // open.
    if (vfs_op_borrows_caller_memory(op->op_type) && !vfs_op_is_done(op)) {
        if (!fl_jobs_is_main_thread()) {
            logc_error(VFS_ID,
                       "fl_vfs_close(%u) from a job worker cannot wait out a read into caller memory; the handle stays "
                       "open. Close an in-flight fl_vfs_read handle from the main thread.",
                       handle);
            vfs_op_release(op);
            return;
        }
        fl_jobs_wait(atomic_load_explicit(&op->job_handle, memory_order_acquire));
    }

    mutex_lock(&self->handle_lock);
    bool listed = vfs_slot_remove_locked(self, op);
    mutex_unlock(&self->handle_lock);

    // The slot's reference, unless a concurrent close already took it. If the job is still running, its own reference
    // keeps the op alive and the last one to drop - the job's - destroys it.
    if (listed) {
        vfs_op_release(op);
    }
    vfs_op_release(op);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlVfsHandle fl_vfs_mount_read_all_with_options(FlVfsMount* mount, FlString relative_path, FlVfsReadCallback callback,
                                               void* user_data, FlVfsReleaseCallback release,
                                               FlVfsHandle reuse_handle) {
    FL_VALIDATE_RET(mount != nullptr, FL_VFS_HANDLE_INVALID);

    return allocate_op(g_vfs_state, &(VfsOpParams) {
                                        .mount = mount,
                                        .path = relative_path,
                                        .op_type = VfsOp_ReadAll,
                                        .callback = callback,
                                        .user_data = user_data,
                                        .release = release,
                                        .reuse_handle = reuse_handle,
                                    });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Two-phase read: allocate handle first, dispatch later.

FlVfsHandle vfs_mount_read_all_prepare(FlVfsMount* mount, FlString relative_path, FlVfsReadCallback callback,
                                       void* user_data, FlVfsReleaseCallback release, FlVfsHandle reuse_handle) {
    FL_VALIDATE_RET(mount != nullptr, FL_VFS_HANDLE_INVALID);

    VfsOp* op = vfs_op_create(g_vfs_state, &(VfsOpParams) {
                                               .mount = mount,
                                               .path = relative_path,
                                               .op_type = VfsOp_ReadAll,
                                               .callback = callback,
                                               .user_data = user_data,
                                               .release = release,
                                               .reuse_handle = reuse_handle,
                                           });
    if (!op) {
        return FL_VFS_HANDLE_INVALID; // Refused, and logged, by vfs_op_create
    }

    FlVfsHandle id = op->id;
    vfs_op_release(op);

    return id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void vfs_dispatch(FlVfsHandle handle) {
    VfsOp* op = vfs_op_acquire(handle);
    if (!op) {
        return;
    }

    vfs_op_schedule(op, FL_JOB_HANDLE_INVALID);
    vfs_op_release(op);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlVfsHandle fl_vfs_mount_open(FlVfsMount* mount, FlString relative_path, u32 flags) {
    FL_VALIDATE_RET(mount != nullptr, FL_VFS_HANDLE_INVALID);

    return allocate_op(g_vfs_state, &(VfsOpParams) {
                                        .mount = mount,
                                        .path = relative_path,
                                        .op_type = VfsOp_FileOpen,
                                        .open_flags = flags,
                                    });
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Reads through an open file, chained after the last op on it. The read takes its own reference on the file, so
// closing the file id while the read is queued or running leaves the file open until the read has run.
FlVfsHandle fl_vfs_read(FlVfsHandle file_handle, void* buffer, i64 size) {
    VfsState* self = g_vfs_state;

    if (file_handle == FL_VFS_HANDLE_INVALID || !buffer || size <= 0) {
        return FL_VFS_HANDLE_INVALID;
    }

    FL_VALIDATE_RET(self != nullptr, FL_VFS_HANDLE_INVALID);

    VfsOp* file = vfs_op_acquire(file_handle);
    if (!file || file->op_type != VfsOp_FileOpen) {
        if (file) {
            vfs_op_release(file);
        }
        logc_error(VFS_ID, "Invalid file handle for read operation");
        return FL_VFS_HANDLE_INVALID;
    }

    // A chained read carries no path and no callback of its own: it is named by the file it chains onto.
    VfsOp* op = vfs_op_create(self, &(VfsOpParams) {
                                        .mount = file->mount,
                                        .op_type = VfsOp_FileRead,
                                    });
    if (!op) {
        vfs_op_release(file);
        return FL_VFS_HANDLE_INVALID;
    }

    // The op gets a reference of its own, which it hands back when it has run or vfs_op_destroy does. The one
    // vfs_op_acquire gave us stays ours until the last access to file below: the read may run, and the file id be
    // closed, before this function is done with it.
    vfs_op_retain(file);
    op->file_op = file;
    op->read_buffer = buffer;
    op->read_size = size;

    FlJobHandle chain_after = atomic_load_explicit(&file->last_job, memory_order_acquire);

    if (fl_jobs_is_main_thread()) {
        vfs_op_schedule(op, chain_after);
        if (g_vfs_test_after_read_scheduled) {
            g_vfs_test_after_read_scheduled(op->id);
        }
        // The next operation (or a wait) on the file chains after this one.
        atomic_store_explicit(&file->last_job, atomic_load_explicit(&op->job_handle, memory_order_acquire),
                              memory_order_release);
    } else {
        // Neither branch goes through the job system, so the op counts as scheduled: a vfs_dispatch must not run it
        // a second time.
        atomic_store_explicit(&op->scheduled, true, memory_order_relaxed);

        // A worker runs the op inline, which is only correct once chain_after has finished.
        if (chain_after != 0 && fl_jobs_is_finished(chain_after) == FlJobsResult_NotFinished) {
            logc_error(VFS_ID,
                       "fl_vfs_read from a job worker cannot wait for the operation it chains onto; issue it from "
                       "the main thread");
            atomic_store_explicit(&op->error_status, VFS_ERROR_WOULD_BLOCK, memory_order_release);
            atomic_store_explicit(&op->done, true, memory_order_release);
        } else {
            vfs_ops_execute_sync(op);
        }
    }

    FlVfsHandle id = op->id;
    vfs_op_release(op);
    vfs_op_release(file);

    return id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mount close and lifecycle

// Caller holds handle_lock. Marks a listed mount closing and reports whether it was open. A mount that is not listed,
// or already closing, is a double-close or a use-after-free. It stays listed until its last reference goes, so
// vfs_wait_all and vfs_destroy still see the ops it has running.
static bool vfs_mount_begin_close_locked(VfsState* self, FlVfsMount* mount) {
    for_each_list(m, self->mount_first) {
        if (m == mount) {
            if (m->closing) {
                return false;
            }
            m->closing = true;
            return true;
        }
    }
    return false;
}

// Waits until the mount's operations are gone, so every release hook has run when the close returns. Only the main
// thread waits, since only it can: fl_jobs_wait() does nothing on a job worker. A close from a worker returns at once
// and the last operation frees the mount when its job ends.
static void vfs_mount_drain(VfsState* self, FlVfsMount* mount) {
    // One reference is the owner's, which fl_vfs_mount_close drops after this.
    while (atomic_load_explicit(&mount->refs, memory_order_acquire) > 1) {
        vfs_wait_jobs(self, mount);

        // An op between scheduling and recording its job, or between its last job and its destruction, has nothing
        // waitable, so this is a poll: back off rather than spin on it.
        if (atomic_load_explicit(&mount->refs, memory_order_acquire) > 1) {
            sleep_us(100);
        }
    }
}

void fl_vfs_mount_close(FlVfsMount* mount) {
    profile_function_auto_nc("vfs:fl_vfs_mount_close", PROFILE_COLOR_CYAN);
    VfsState* self = g_vfs_state;

    FL_VALIDATE(self != nullptr);
    FL_VALIDATE(mount != nullptr);

    arena_scratch_auto(temp);

    mutex_lock(&self->handle_lock);

    // From here the mount refuses new operations. The ones it has are unlisted from the slot table, so no id reaches
    // them, and the references the slot held are released below, outside the lock.
    if (!vfs_mount_begin_close_locked(self, mount)) {
        mutex_unlock(&self->handle_lock);
        logc_error(VFS_ID, "fl_vfs_mount_close: mount %p not open (double-close or use-after-free)", (void*)mount);
        return;
    }

    VfsOp** unlisted = arena_alloc_array(temp.arena, VfsOp*, mount->ops_count);
    u32 unlisted_count = 0;
    for_each_list(op, mount->ops_first) {
        if (vfs_slot_remove_locked(self, op)) {
            unlisted[unlisted_count++] = op;
        }
    }

    mutex_unlock(&self->handle_lock);

    for (u32 i = 0; i < unlisted_count; i++) {
        vfs_op_release(unlisted[i]);
    }

    if (fl_jobs_is_main_thread()) {
        vfs_mount_drain(self, mount);
    }

    vfs_mount_release(mount);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// VFS destroy

// The first listed mount that nobody has closed yet, or nullptr when every remaining mount is closing.
static FlVfsMount* vfs_first_open_mount(VfsState* self) {
    mutex_lock_auto(&self->handle_lock);
    for_each_list(m, self->mount_first) {
        if (!m->closing) {
            return m;
        }
    }
    return nullptr;
}

static bool vfs_has_mounts(VfsState* self) {
    mutex_lock_auto(&self->handle_lock);
    return self->mount_first != nullptr;
}

void vfs_destroy(void) {
    profile_function_auto_nc("vfs:vfs_destroy", PROFILE_COLOR_CYAN);
    VfsState* self = g_vfs_state;

    FL_VALIDATE(self != nullptr);

    vfs_wait_all();

    for (FlVfsMount* mount = vfs_first_open_mount(self); mount != nullptr; mount = vfs_first_open_mount(self)) {
        fl_vfs_mount_close(mount);
    }

    // A mount closed from a worker is freed by its last op, so it leaves the list on its own once the jobs are done.
    while (vfs_has_mounts(self)) {
        vfs_wait_jobs(self, nullptr);
        sleep_us(100);
    }

    // The hashmaps are allocated from main_arena
    hashmap_clear(&self->slots);
    hashmap_clear(&self->retiring);

    // All mounts are closed and no jobs remain, so nothing can take this lock again; destroy it before the state
    // becomes unreachable.
    mutex_destroy(&self->handle_lock);

    g_vfs_state = nullptr;

    // Destroys the arena VfsState itself lives in.
    if (self->owns_main_arena) {
        fl_arena_destroy(self->main_arena);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mount status and info

FlVfsMountStatus fl_vfs_mount_get_status(FlVfsMount* mount) {
    profile_function_auto_nc("vfs:fl_vfs_mount_get_status", PROFILE_COLOR_CYAN);
    FL_VALIDATE_RET(mount != nullptr, FlVfsMountStatus_Error);

    return (FlVfsMountStatus)atomic_load_explicit(&mount->status, memory_order_acquire);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlVfsMountInfo fl_vfs_mount_get_info(FlVfsMount* mount) {
    FL_VALIDATE_RET(mount != nullptr, ((FlVfsMountInfo) {
                                          .path = FL_S(""),
                                          .is_ready = false,
                                      }));

    return (FlVfsMountInfo) {
        .path = mount->source_path,
        .is_ready = (fl_vfs_mount_get_status(mount) == FlVfsMountStatus_Ready),
    };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_vfs_mount_is_ready(FlVfsMount* mount) {
    profile_function_auto_nc("vfs:fl_vfs_mount_is_ready", PROFILE_COLOR_CYAN);
    return fl_vfs_mount_get_status(mount) == FlVfsMountStatus_Ready;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlJobHandle fl_vfs_mount_get_job_handle(FlVfsMount* mount) {
    FL_VALIDATE_RET(mount != nullptr, 0);
    return mount->job_handle;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u64 vfs_mount_get_id(const FlVfsMount* mount) {
    FL_VALIDATE_RET(mount != nullptr, 0);
    return mount->id;
}
