#pragma once

#include "arena.h"
#include "file_watcher.h"
#include "fixed_array.h"
#include "os/os.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define FILE_WATCHER_DEFAULT_MAX_CHANGES 1000
#define FILE_WATCHER_COALESCE_WINDOW_US 100000 // 100ms
#define FILE_WATCHER_MAX_CALLBACKS 32

// Directories one recursive watcher can map on a platform that watches each directory on its own
// (Linux inotify). A tree with more is refused.
#define FILE_WATCHER_MAX_RECURSIVE_DIRECTORIES 1024

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct CallbackRegistration {
    FileWatcherCallbackHandle handle;
    FlString file_path; // Specific file path to watch, allocated in the registry arena
    FileChangeCallback callback;
    void* user_data;
} CallbackRegistration;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Error codes for platform implementations

typedef enum FileWatcherError {
    FileWatcherError_Success = 0,
    FileWatcherError_InvalidPath,
    FileWatcherError_PermissionDenied,
    FileWatcherError_ResourceExhausted,
    FileWatcherError_SystemError,
} FileWatcherError;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FileWatcherState {
    FlFileWatcherHandle handle;
    FlString watch_path; // Copy of watched path, allocated in the registry arena
    FlFileWatcherConfig config;
    bool is_active;

    void* platform_data;

    // Change buffer (circular) - protected by changes_lock for thread safety
    // Appended from the macOS dispatch queue or from platform_poll_changes() on Linux/Windows
    Mutex changes_lock;
    FlArena* changes_arena; // Per-event path strings; rewound whenever the ring is drained
    FileChange* changes;    // Allocated from registry arena
    u32 change_capacity;
    u32 change_count;
    u32 change_head; // Next write position
    u32 change_tail; // Next read position
    bool has_overflow;

    CallbackRegistration callbacks[FILE_WATCHER_MAX_CALLBACKS];
    u32 active_callback_count;
    FileWatcherCallbackHandle next_callback_handle;
} FileWatcherState;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FileWatcherRegistry {
    FlArena* arena; // Protected by lock
    fixed_array(FileWatcherState) watchers;
    FlFileWatcherHandle next_handle;
    Mutex lock; // Protects all registry operations (handle allocation, arena, watcher array)
} FileWatcherRegistry;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal functions for platform implementations

// The registry, already brought up by file_watcher_init. Its arena is null until then.
FileWatcherRegistry* get_file_watcher_registry(void);

// Walks the watcher array under the registry lock and hands the slot back; slots are tombstoned
// rather than compacted, so the pointer stays valid after the lock drops.
//
// Valid, but not pinned: a stop of the same handle can still destroy the changes lock under a
// caller.
FileWatcherState* find_watcher_state(FlFileWatcherHandle handle);

void add_file_change(FileWatcherState* watcher, FlString path, FlString old_path, u32 change_types, bool is_directory);

// True when path is the watched root itself or one of its immediate children.
// Comparison is textual (no symlink resolution); trailing '/' separators are ignored.
bool file_watcher_path_is_direct_child(FlString watch_root, FlString path);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Platform-specific interface (implemented in platform files)

FileWatcherError platform_start_watching(FileWatcherState* state);

void platform_stop_watching(FileWatcherState* state);

FileWatcherError platform_poll_changes(FileWatcherState* state);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Map system error codes to FileWatcherError

#if PLATFORM_LINUX
FileWatcherError map_errno_to_error(int err);
#elif PLATFORM_WINDOWS
FileWatcherError map_windows_error(u32 win_error);
#endif
