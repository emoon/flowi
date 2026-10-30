#include "../file_watcher_internal.h"
#include "../fixed_array.h"
#include "../log.h"
#include "../path.h"
#include "os.h"

#if PLATFORM_LINUX

#include <errno.h>
#include <sys/inotify.h>
#include <unistd.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static LogChannelId FW_ID = LOG_CHANNEL_INVALID;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct WatchEntry {
    int wd;
    FlString path;
} WatchEntry;

typedef struct LinuxWatcherData {
    int inotify_fd;
    u32 notify_mask;
    bool recursive;
    fixed_array(WatchEntry) watches;
} LinuxWatcherData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileWatcherError map_errno_to_error(int err) {
    switch (err) {
        case EACCES:
            return FileWatcherError_PermissionDenied;
        case ENOENT:
        case ENOTDIR:
            return FileWatcherError_InvalidPath;
        case EMFILE:
        case ENFILE:
        case ENOSPC:
            return FileWatcherError_ResourceExhausted;
        default:
            return FileWatcherError_SystemError;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Removes every inotify watch this watcher holds and empties the wd -> path table. Used by normal
// teardown and by the failure paths in platform_start_watching, where the watches added before the
// failure must not outlive the watcher that was rejected.
static void remove_all_watches(LinuxWatcherData* data) {
    for (u32 i = 0; i < fixed_array_length(&data->watches); i++) {
        WatchEntry* entry = fixed_array_get_ptr(&data->watches, i);
        if (entry) {
            inotify_rm_watch(data->inotify_fd, entry->wd);
        }
    }

    fixed_array_clear(&data->watches);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString find_path_for_wd(LinuxWatcherData* data, int wd) {
    for (u32 i = 0; i < fixed_array_length(&data->watches); i++) {
        WatchEntry* entry = fixed_array_get_ptr(&data->watches, i);
        if (entry && entry->wd == wd) {
            return entry->path;
        }
    }
    return string_empty();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Returns 0 on success, otherwise an errno the caller maps through map_errno_to_error(). That is
// either the errno of a failing inotify_add_watch or ENOSPC when the wd -> path table is full.
//
// The table has a fixed number of slots. The capacity is checked before inotify_add_watch, so a
// full table is refused without creating a descriptor.
static int add_watch_for_directory(LinuxWatcherData* data, FlString path, FlArena* arena) {
    if (fixed_array_is_full(&data->watches)) {
        logc_warning(FW_ID, "Watch table full (%u slots), refusing to watch %S", fixed_array_capacity(&data->watches),
                     path);
        return ENOSPC;
    }

    OsPath os_path = string_to_os_path(arena, path);
    int wd = inotify_add_watch(data->inotify_fd, os_path_str(os_path), data->notify_mask);
    if (wd == -1) {
        const int add_watch_errno = errno;
        logc_warning(FW_ID, "Failed to add watch for %S: %d", path, add_watch_errno);
        return add_watch_errno;
    }

    WatchEntry entry = { .wd = wd, .path = fl_string_copy(get_file_watcher_registry()->arena, path) };
    fixed_array_add(&data->watches, entry);

    logc_debug(FW_ID, "Added watch (wd: %d) for: %S", wd, path);
    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Returns 0 when every directory under base_path was watched, otherwise the errno that stopped the
// walk. Stops at the first failure. An unreadable directory is skipped, not treated as a failure.
static int add_watches_recursive(LinuxWatcherData* data, FlString base_path, FlArena* temp_arena) {
    DirHandle dir = dir_open_os(temp_arena, base_path);
    if (!dir.is_valid) {
        return 0;
    }

    int result = 0;

    FileStat entry;
    while ((entry = dir_read_next_os(temp_arena, &dir)).is_valid) {
        if (!entry.is_directory) {
            continue;
        }
        if (entry.name.length > 0 && entry.name.data[0] == '.') {
            continue;
        }

        FlString subdir_path = fl_path_join(temp_arena, base_path, entry.name);

        result = add_watch_for_directory(data, subdir_path, temp_arena);
        if (result != 0) {
            break;
        }

        result = add_watches_recursive(data, subdir_path, temp_arena);
        if (result != 0) {
            break;
        }
    }

    dir_close_os(&dir);
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileWatcherError platform_start_watching(FileWatcherState* state) {
    if (FW_ID == LOG_CHANNEL_INVALID) {
        FW_ID = fl_log_register_channel(FL_S("FILE_WATCHER"));
    }

    int fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd == -1) {
        return map_errno_to_error(errno);
    }

    u32 mask = 0;
    if (state->config.watch_files) {
        mask |= IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO;
    }
    if (state->config.watch_directories || state->config.recursive) {
        // Need directory events to track new/deleted subdirectories for recursive watching
        mask |= IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO;
    }

    LinuxWatcherData* data = arena_alloc(get_file_watcher_registry()->arena, LinuxWatcherData);
    data->inotify_fd = fd;
    data->notify_mask = mask;
    data->recursive = state->config.recursive;
    fixed_array_new(&data->watches, get_file_watcher_registry()->arena, FILE_WATCHER_MAX_RECURSIVE_DIRECTORIES);

    const int watch_errno = add_watch_for_directory(data, state->watch_path, get_file_watcher_registry()->arena);
    if (watch_errno != 0) {
        remove_all_watches(data);
        close(fd);
        return map_errno_to_error(watch_errno);
    }

    if (state->config.recursive) {
        arena_scratch_auto(temp);
        const int recursive_errno = add_watches_recursive(data, state->watch_path, temp.arena);
        if (recursive_errno != 0) {
            // The whole watcher fails: a handle that watches part of its tree would report some
            // changes and silently miss others, which is worse than not starting.
            logc_error(FW_ID, "FileWatcher[%u]: Cannot watch '%S' recursively (%u of at most %u directories): %d",
                       state->handle, state->watch_path, fixed_array_length(&data->watches),
                       fixed_array_capacity(&data->watches), recursive_errno);
            remove_all_watches(data);
            close(fd);
            return map_errno_to_error(recursive_errno);
        }

        logc_info(FW_ID, "FileWatcher[%u]: Added %u watches for recursive monitoring", state->handle,
                  fixed_array_length(&data->watches));
    }

    state->platform_data = data;

    logc_debug(FW_ID, "FileWatcher[%u]: Linux inotify watcher initialized (fd: %d, watches: %u)", state->handle, fd,
               fixed_array_length(&data->watches));

    return FileWatcherError_Success;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void platform_stop_watching(FileWatcherState* state) {
    if (!state || !state->platform_data) {
        return;
    }

    LinuxWatcherData* data = (LinuxWatcherData*)state->platform_data;

    remove_all_watches(data);

    if (data->inotify_fd != -1) {
        close(data->inotify_fd);
    }

    state->platform_data = nullptr;

    logc_debug(FW_ID, "FileWatcher[%u]: Linux inotify watcher stopped", state->handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static u32 map_inotify_mask_to_change_types(u32 mask) {
    u32 change_types = 0;

    if (mask & IN_CREATE) {
        change_types |= FlFileChangeType_Created;
    }
    if (mask & IN_MODIFY) {
        change_types |= FlFileChangeType_Modified;
    }
    if (mask & IN_DELETE) {
        change_types |= FlFileChangeType_Deleted;
    }
    if (mask & (IN_MOVED_FROM | IN_MOVED_TO)) {
        change_types |= FlFileChangeType_Moved;
    }

    return change_types;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileWatcherError platform_poll_changes(FileWatcherState* state) {
    if (!state || !state->platform_data || !state->is_active) {
        return FileWatcherError_Success;
    }

    LinuxWatcherData* data = (LinuxWatcherData*)state->platform_data;

    char buffer[4096];

    while (true) {
        ssize_t length = read(data->inotify_fd, buffer, sizeof(buffer));

        if (length == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            } else {
                logc_error(FW_ID, "FileWatcher[%u]: inotify read error: %d", state->handle, errno);
                state->is_active = false;
                return FileWatcherError_SystemError;
            }
        }

        if (length == 0) {
            break;
        }

        char* ptr = buffer;
        while (ptr < buffer + length) {
            struct inotify_event* event = (struct inotify_event*)ptr;

            // The kernel dropped this watch (directory deleted/moved/unmounted). Prune the
            // wd -> path entry so a later kernel-reused wd can't resolve to the stale path.
            // IN_IGNORED carries no name, so it must be handled before the len check below.
            if (event->mask & IN_IGNORED) {
                for (u32 i = 0; i < fixed_array_length(&data->watches); i++) {
                    WatchEntry* entry = fixed_array_get_ptr(&data->watches, i);
                    if (entry && entry->wd == event->wd) {
                        fixed_array_remove_swap(&data->watches, i);
                        break;
                    }
                }
                ptr += sizeof(struct inotify_event) + event->len;
                continue;
            }

            // Skip events without names
            if (event->len > 0) {
                FlString watch_path = find_path_for_wd(data, event->wd);
                if (string_is_empty(watch_path)) {
                    ptr += sizeof(struct inotify_event) + event->len;
                    continue;
                }

                FlArena* temp_arena = arena_new();
                FlString filename = string_from_cstr(event->name);
                FlString full_path = fl_path_join(temp_arena, watch_path, filename);

                u32 change_types = map_inotify_mask_to_change_types(event->mask);
                bool is_directory = (event->mask & IN_ISDIR) != 0;

                if (data->recursive && is_directory && (event->mask & (IN_CREATE | IN_MOVED_TO))) {
                    int add_errno = add_watch_for_directory(data, full_path, temp_arena);
                    if (add_errno == 0 && (event->mask & IN_MOVED_TO)) {
                        // Unlike a fresh mkdir, a moved-in directory can already have subdirectories
                        add_errno = add_watches_recursive(data, full_path, temp_arena);
                    }

                    if (add_errno != 0) {
                        // A directory appeared that this watcher cannot map: deactivate the watcher.
                        logc_error(FW_ID,
                                   "FileWatcher[%u]: Cannot extend watch to '%S' (%u of at most %u directories): %d "
                                   "- deactivating watcher",
                                   state->handle, full_path, fixed_array_length(&data->watches),
                                   fixed_array_capacity(&data->watches), add_errno);
                        fl_arena_destroy(temp_arena);
                        state->is_active = false;
                        return map_errno_to_error(add_errno);
                    }
                }

                if (change_types != 0) {
                    logc_debug(FW_ID, "FileWatcher[%u]: %S (types: 0x%x, dir: %d)", state->handle, full_path,
                               change_types, is_directory);
                    add_file_change(state, full_path, FL_S(""), change_types, is_directory);
                }

                fl_arena_destroy(temp_arena);
            }

            ptr += sizeof(struct inotify_event) + event->len;
        }
    }

    return FileWatcherError_Success;
}

#endif // PLATFORM_LINUX