#include "arena.h"
#include "assert.h"
#include "file_watcher_internal.h"
#include "fixed_array.h"
#include "log.h"
#include "memory.h"
#include "os/os.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static LogChannelId FW_ID = LOG_CHANNEL_INVALID;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FileWatcherRegistry g_registry = { 0 };

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void file_watcher_init(void) {
    if (g_registry.arena) {
        return;
    }

    FW_ID = fl_log_register_channel(FL_S("FILE_WATCHER"));

    g_registry.arena = arena_new();
    fixed_array_new(&g_registry.watchers, g_registry.arena, 16);
    g_registry.next_handle = 1; // Start from 1, 0 is invalid
    mutex_init(&g_registry.lock);

    logc_info(FW_ID, "File watcher system initialized");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileWatcherRegistry* get_file_watcher_registry(void) {
    return &g_registry;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileWatcherState* find_watcher_state(FlFileWatcherHandle handle) {
    if (handle == FL_FILE_WATCHER_INVALID) {
        return nullptr;
    }

    FileWatcherRegistry* registry = get_file_watcher_registry();

    // No registry means fl_init has not run, so there is nothing to look in and no lock to take.
    if (!registry->arena) {
        return nullptr;
    }

    mutex_lock_auto(&registry->lock);

    for_count(i, fixed_array_length(&registry->watchers)) {
        FileWatcherState* watcher = fixed_array_get_ptr(&registry->watchers, i);
        ANALYZER_ASSUME_NONNULL(watcher);
        if (watcher->handle == handle) {
            return watcher;
        }
    }

    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Must be called with the registry lock held: the tombstone it leaves behind is what lets
// file_watcher_start hand the slot out again.
static void destroy_watcher_state(FileWatcherState* watcher) {
    // Platform cleanup first (stops callbacks from being invoked)
    if (watcher->platform_data) {
        platform_stop_watching(watcher);
    }

    mutex_destroy(&watcher->changes_lock);
    fl_arena_destroy(watcher->changes_arena);
    watcher->changes_arena = nullptr;

    // The slot is tombstoned, not compacted: platform layers retain the FileWatcherState* for the
    // lifetime of the watch, so a watcher's state never moves.
    watcher->handle = FL_FILE_WATCHER_INVALID;
    watcher->is_active = false;
    watcher->platform_data = nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void remove_watcher_state(FlFileWatcherHandle handle) {
    FileWatcherRegistry* registry = get_file_watcher_registry();

    mutex_lock_auto(&registry->lock);

    for_count(i, fixed_array_length(&registry->watchers)) {
        FileWatcherState* watcher = fixed_array_get_ptr(&registry->watchers, i);
        ANALYZER_ASSUME_NONNULL(watcher);
        if (watcher->handle == handle) {
            destroy_watcher_state(watcher);
            return;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void file_watcher_destroy(void) {
    if (!g_registry.arena) {
        return;
    }

    mutex_lock(&g_registry.lock);

    for_count(i, fixed_array_length(&g_registry.watchers)) {
        FileWatcherState* watcher = fixed_array_get_ptr(&g_registry.watchers, i);
        ANALYZER_ASSUME_NONNULL(watcher);
        if (watcher->handle != FL_FILE_WATCHER_INVALID) {
            destroy_watcher_state(watcher);
        }
    }

    mutex_unlock(&g_registry.lock);

    mutex_destroy(&g_registry.lock);
    fl_arena_destroy(g_registry.arena);
    g_registry = (FileWatcherRegistry) { 0 };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Copy the callbacks registered for path into out (caller-provided, FILE_WATCHER_MAX_CALLBACKS slots).
// Must be called with watcher->changes_lock held. The snapshot lets callers invoke callbacks
// after releasing the lock, avoiding deadlock if a callback re-enters the watcher API.
static u32 snapshot_matching_callbacks(const FileWatcherState* watcher, FlString path, CallbackRegistration* out) {
    u32 count = 0;
    for_count(i, watcher->active_callback_count) {
        if (fl_string_equals(watcher->callbacks[i].file_path, path)) {
            out[count++] = watcher->callbacks[i];
        }
    }
    return count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Length of s with trailing '/' separators stripped, keeping a lone "/" root intact.
static u64 length_without_trailing_slashes(FlString s) {
    u64 length = s.length;
    while (length > 1 && s.data[length - 1] == '/') {
        length--;
    }
    return length;
}

bool file_watcher_path_is_direct_child(FlString watch_root, FlString path) {
    u64 root_length = length_without_trailing_slashes(watch_root);
    u64 path_length = length_without_trailing_slashes(path);

    if (root_length == 0 || path_length < root_length) {
        return false;
    }

    if (memory_compare(path.data, watch_root.data, root_length) != 0) {
        return false;
    }

    // The watched root itself counts (e.g. attribute changes on the directory).
    if (path_length == root_length) {
        return true;
    }

    // Skip the separator between root and child; a root of "/" already ends with one.
    u64 name_start = root_length;
    if (watch_root.data[root_length - 1] != '/') {
        if (path.data[root_length] != '/') {
            return false; // Sibling with the root as a name prefix, e.g. "/a/bc" under "/a/b"
        }
        name_start = root_length + 1;
    }

    if (name_start >= path_length) {
        return false;
    }

    // Direct child: no further separator inside the name component
    for (u64 i = name_start; i < path_length; ++i) {
        if (path.data[i] == '/') {
            return false;
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void add_file_change(FileWatcherState* watcher, FlString path, FlString old_path, u32 change_types, bool is_directory) {
    if (!watcher || !watcher->is_active) {
        return;
    }

    // A moved file also counts as modified: editor safe-save replaces files via rename
    if ((change_types & FlFileChangeType_Moved) && !is_directory) {
        change_types |= FlFileChangeType_Modified;
    }

    u64 timestamp = get_current_time_us_os();

    // This function is called from platform threads: dispatch queue on macOS, etc.
    mutex_lock(&watcher->changes_lock);

    // Try to coalesce with recent changes to the same path
    for_count(i, watcher->change_count) {
        u32 idx = (watcher->change_tail + i) % watcher->change_capacity;
        FileChange* existing = &watcher->changes[idx];

        if (fl_string_equals(existing->path, path)
            && (timestamp - existing->timestamp) < FILE_WATCHER_COALESCE_WINDOW_US) {
            existing->change_types |= change_types;
            existing->timestamp = timestamp;

            FileChange callback_change = *existing;
            CallbackRegistration matched[FILE_WATCHER_MAX_CALLBACKS];
            u32 matched_count = snapshot_matching_callbacks(watcher, path, matched);
            mutex_unlock(&watcher->changes_lock);

            // Invoke callbacks outside lock to avoid deadlock
            for_count(j, matched_count) {
                matched[j].callback(&callback_change, matched[j].user_data);
            }
            return;
        }
    }

    if (watcher->change_count >= watcher->change_capacity) {
        if (!watcher->has_overflow) {
            watcher->has_overflow = true;
            logc_error(FW_ID, "FileWatcher[%u]: Change buffer overflow - some events lost", watcher->handle);
        }
        mutex_unlock(&watcher->changes_lock);
        return;
    }

    FileChange* change = &watcher->changes[watcher->change_head];

    // Event strings live in the per-watcher arena so they can be reclaimed when the ring
    // drains; changes_lock (already held) guards both the ring and the arena.
    change->path = fl_string_copy(watcher->changes_arena, path);
    change->old_path = fl_string_copy(watcher->changes_arena, old_path);

    change->change_types = change_types;
    change->timestamp = timestamp;
    change->is_directory = is_directory;

    watcher->change_head = (watcher->change_head + 1) % watcher->change_capacity;
    watcher->change_count++;

    FileChange callback_change = *change;
    CallbackRegistration matched[FILE_WATCHER_MAX_CALLBACKS];
    u32 matched_count = snapshot_matching_callbacks(watcher, path, matched);
    mutex_unlock(&watcher->changes_lock);

    // Invoke callbacks outside lock to avoid deadlock
    for_count(i, matched_count) {
        matched[i].callback(&callback_change, matched[i].user_data);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlFileWatcherHandle file_watcher_start(FlString path, FlFileWatcherConfig config) {
    FileWatcherRegistry* registry = get_file_watcher_registry();

    // The registry comes up with the foundation, so a watch started before fl_init has no arena
    // to allocate from and no lock to take.
    FL_VALIDATE_RET(registry->arena != nullptr, FL_FILE_WATCHER_INVALID);

    if (!file_exists_os(path) && !file_directory_exists_os(path)) {
        logc_error(FW_ID, "Path does not exist: %.*s", (int)path.length, path.data);
        return FL_FILE_WATCHER_INVALID;
    }

    // The lock protects handle allocation, arena allocations, and watcher array modification
    mutex_lock(&registry->lock);

    // Reuse a tombstoned slot (handle == FL_FILE_WATCHER_INVALID) before growing the array.
    FileWatcherState* watcher = nullptr;
    for_count(i, fixed_array_length(&registry->watchers)) {
        FileWatcherState* slot = fixed_array_get_ptr(&registry->watchers, i);
        ANALYZER_ASSUME_NONNULL(slot);
        if (slot->handle == FL_FILE_WATCHER_INVALID) {
            watcher = slot;
            break;
        }
    }

    if (!watcher) {
        if (fixed_array_is_full(&registry->watchers)) {
            u32 capacity = fixed_array_capacity(&registry->watchers);
            mutex_unlock(&registry->lock);
            logc_error(FW_ID, "Cannot start watcher: registry full (%u active), path: %.*s", capacity, (int)path.length,
                       path.data);
            return FL_FILE_WATCHER_INVALID;
        }
        FileWatcherState empty = { 0 };
        fixed_array_add(&registry->watchers, empty);
        watcher = fixed_array_get_ptr(&registry->watchers, fixed_array_length(&registry->watchers) - 1);
        ANALYZER_ASSUME_NONNULL(watcher);
    }

    *watcher = (FileWatcherState) { 0 };
    watcher->handle = registry->next_handle++;

    if (registry->next_handle == FL_FILE_WATCHER_INVALID) {
        registry->next_handle = 1;
    }

    watcher->watch_path = fl_string_copy(registry->arena, path);
    watcher->config = config;
    watcher->is_active = true;
    watcher->platform_data = nullptr;

    mutex_init(&watcher->changes_lock);
    u32 max_changes = config.max_changes > 0 ? config.max_changes : FILE_WATCHER_DEFAULT_MAX_CHANGES;
    watcher->changes = arena_alloc_array(registry->arena, FileChange, max_changes);
    watcher->change_capacity = max_changes;

    // Per-event path strings get their own arena so draining the ring can reclaim them;
    // the registry arena only holds watcher-lifetime allocations.
    watcher->changes_arena = arena_new();

    watcher->next_callback_handle = 1; // Start from 1, 0 is invalid

    FlFileWatcherHandle handle = watcher->handle;

    mutex_unlock(&registry->lock);

    FileWatcherError error = platform_start_watching(watcher);
    if (error != FileWatcherError_Success) {
        logc_error(FW_ID, "FileWatcher[%u]: Failed to start platform watching: %d", handle, error);
        remove_watcher_state(handle);
        return FL_FILE_WATCHER_INVALID;
    }

    logc_info(FW_ID, "FileWatcher[%u]: Started watching path: %.*s", handle, (int)path.length, path.data);
    return handle;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void file_watcher_stop(FlFileWatcherHandle handle) {
    if (handle == FL_FILE_WATCHER_INVALID) {
        return;
    }

    FileWatcherState* watcher = find_watcher_state(handle);
    if (watcher) {
        logc_info(FW_ID, "FileWatcher[%u]: Stopping watcher for path: %.*s", handle, (int)watcher->watch_path.length,
                  watcher->watch_path.data);
        remove_watcher_state(handle);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool file_watcher_is_active(FlFileWatcherHandle handle) {
    FileWatcherState* watcher = find_watcher_state(handle);
    return watcher && watcher->is_active;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileWatcherCallbackHandle file_watcher_register_callback(FlFileWatcherHandle handle, FlString file_path,
                                                         FileChangeCallback callback, void* user_data) {
    FileWatcherState* watcher = find_watcher_state(handle);
    if (!watcher) {
        logc_error(FW_ID, "FileWatcher: Invalid handle %u for callback registration", handle);
        return FILE_WATCHER_CALLBACK_INVALID;
    }

    if (!callback) {
        logc_error(FW_ID, "FileWatcher[%u]: Cannot register null callback", handle);
        return FILE_WATCHER_CALLBACK_INVALID;
    }

    // The callback array is read by add_file_change on platform threads under changes_lock;
    // take it here so registration never races the dispatch snapshot.
    mutex_lock(&watcher->changes_lock);

    if (watcher->active_callback_count >= FILE_WATCHER_MAX_CALLBACKS) {
        mutex_unlock(&watcher->changes_lock);
        logc_error(FW_ID, "FileWatcher[%u]: No free callback slots (max %u)", handle, FILE_WATCHER_MAX_CALLBACKS);
        return FILE_WATCHER_CALLBACK_INVALID;
    }

    CallbackRegistration* reg = &watcher->callbacks[watcher->active_callback_count];

    // The registry arena allocates lock-free, so the path copy needs no registry lock. Taking one
    // here would nest it inside changes_lock, and teardown holds the registry lock across
    // platform_stop_watching, which can wait for a platform callback blocked on changes_lock.
    FileWatcherRegistry* registry = get_file_watcher_registry();

    reg->handle = watcher->next_callback_handle++;
    reg->file_path = fl_string_copy(registry->arena, file_path);
    reg->callback = callback;
    reg->user_data = user_data;

    if (watcher->next_callback_handle == FILE_WATCHER_CALLBACK_INVALID) {
        watcher->next_callback_handle = 1;
    }

    watcher->active_callback_count++;

    FileWatcherCallbackHandle callback_handle = reg->handle;
    mutex_unlock(&watcher->changes_lock);

    logc_debug(FW_ID, "FileWatcher[%u]: Callback[%u] registered for file: %.*s", handle, callback_handle,
               (int)file_path.length, file_path.data);

    return callback_handle;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void file_watcher_unregister_callback(FlFileWatcherHandle handle, FileWatcherCallbackHandle callback_handle) {
    FileWatcherState* watcher = find_watcher_state(handle);
    if (!watcher) {
        logc_error(FW_ID, "FileWatcher: Invalid handle %u for callback unregistration", handle);
        return;
    }

    // The callback array is read by add_file_change on platform threads under changes_lock;
    // take it here so removal never races the dispatch snapshot.
    mutex_lock(&watcher->changes_lock);

    for_count(i, watcher->active_callback_count) {
        CallbackRegistration* reg = &watcher->callbacks[i];

        if (reg->handle == callback_handle) {
            FlString file_path = reg->file_path;

            u32 last_idx = watcher->active_callback_count - 1;
            if (i != last_idx) {
                watcher->callbacks[i] = watcher->callbacks[last_idx];
            }

            watcher->active_callback_count--;
            mutex_unlock(&watcher->changes_lock);

            logc_debug(FW_ID, "FileWatcher[%u]: Callback[%u] unregistered for file: %.*s", handle, callback_handle,
                       (int)file_path.length, file_path.data);
            return;
        }
    }

    mutex_unlock(&watcher->changes_lock);

    logc_warning(FW_ID, "FileWatcher[%u]: Callback handle %u not found", handle, callback_handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool file_watcher_has_changes(FlFileWatcherHandle handle) {
    FileWatcherState* watcher = find_watcher_state(handle);
    if (!watcher || !watcher->is_active) {
        return false;
    }

    platform_poll_changes(watcher);

    // change_count is written by the callback thread
    mutex_lock(&watcher->changes_lock);
    bool has_changes = watcher->change_count > 0;
    u32 count = watcher->change_count;
    mutex_unlock(&watcher->changes_lock);

    if (has_changes) {
        logc_debug(FW_ID, "FileWatcher[%u]: Has %u pending changes", handle, count);
    }

    return has_changes;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void file_watcher_clear_changes(FlFileWatcherHandle handle) {
    FileWatcherState* watcher = find_watcher_state(handle);
    if (!watcher) {
        return;
    }

    mutex_lock(&watcher->changes_lock);
    u32 cleared_count = watcher->change_count;
    watcher->change_count = 0;
    watcher->change_head = 0;
    watcher->change_tail = 0;
    watcher->has_overflow = false;
    fl_arena_rewind(watcher->changes_arena); // Ring is empty; reclaim the evicted event strings
    mutex_unlock(&watcher->changes_lock);

    if (cleared_count > 0) {
        logc_debug(FW_ID, "FileWatcher[%u]: Cleared %u pending changes", handle, cleared_count);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileChangeIterator file_watcher_get_changes(FlFileWatcherHandle handle, FlArena* arena) {
    FileChangeIterator iter = { 0 };

    FileWatcherState* watcher = find_watcher_state(handle);
    if (!watcher || !watcher->is_active) {
        return iter;
    }

    platform_poll_changes(watcher);

    mutex_lock(&watcher->changes_lock);

    if (watcher->change_count == 0) {
        mutex_unlock(&watcher->changes_lock);
        return iter;
    }

    logc_debug(FW_ID, "FileWatcher[%u]: Returning %u changes (overflow: %s)", handle, watcher->change_count,
               watcher->has_overflow ? "yes" : "no");

    iter.changes = arena_alloc_array(arena, FileChange, watcher->change_count);
    iter.count = watcher->change_count;
    iter.current = 0;
    iter.has_overflow = watcher->has_overflow;

    for_count(i, watcher->change_count) {
        u32 src_idx = (watcher->change_tail + i) % watcher->change_capacity;
        FileChange* src = &watcher->changes[src_idx];
        FileChange* dst = &iter.changes[i];

        dst->path = fl_string_copy(arena, src->path);
        dst->old_path = fl_string_copy(arena, src->old_path);
        dst->change_types = src->change_types;
        dst->timestamp = src->timestamp;
        dst->is_directory = src->is_directory;
    }

    // Clear the changes after copying (inline to avoid double-locking)
    watcher->change_count = 0;
    watcher->change_head = 0;
    watcher->change_tail = 0;
    watcher->has_overflow = false;
    fl_arena_rewind(watcher->changes_arena); // Strings were copied to the caller's arena above

    mutex_unlock(&watcher->changes_lock);

    return iter;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool file_change_iterator_has_next(const FileChangeIterator* iter) {
    return iter && iter->current < iter->count;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileChange file_change_iterator_next(FileChangeIterator* iter) {
    FileChange empty = { 0 };

    if (!iter || iter->current >= iter->count) {
        return empty;
    }

    return iter->changes[iter->current++];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void file_change_iterator_reset(FileChangeIterator* iter) {
    if (iter) {
        iter->current = 0;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Public API forwarders.
//
// Intra-library calls use the internal file_watcher_* names, never the exported fl_* ones.

u32 fl_file_watcher_start(FlString path, const FlFileWatcherConfig* config) {
    // An all-zero config watches neither files nor directories, so a null config cannot mean "defaults".
    if (!config) {
        return 0;
    }

    return file_watcher_start(path, *config);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_file_watcher_stop(u32 handle) {
    file_watcher_stop(handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_file_watcher_is_active(u32 handle) {
    return file_watcher_is_active(handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_file_watcher_has_changes(u32 handle) {
    return file_watcher_has_changes(handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlFileChanges fl_file_watcher_take_changes(u32 handle, FlArena* arena) {
    FileChangeIterator iter = file_watcher_get_changes(handle, arena);

    return (FlFileChanges) {
        .changes = iter.changes,
        .count = iter.count,
        .has_overflow = iter.has_overflow,
    };
}
