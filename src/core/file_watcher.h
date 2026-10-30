#pragma once

#include "core.h"
#include "string.h"
#include <stdbool.h>
#include <stdint.h>

#include <flowi/core/file_watcher.h> // @generated FlFileChangeType, FlFileWatcherConfig, FlFileChange, FlFileChanges

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File watcher foundational types

// File watcher handle - opaque
typedef uint32_t FlFileWatcherHandle;
#define FL_FILE_WATCHER_INVALID ((FlFileWatcherHandle)0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File Watcher API - Iterator-based file system change detection
//
// Designed for debug purposes and single-threaded operation on the main thread.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal types (not in public API)

typedef FlFileChange FileChange;

typedef struct FileChangeIterator {
    FileChange* changes;
    u32 count;
    u32 current;
    bool has_overflow; // True if some changes were lost due to buffer limits
} FileChangeIterator;

// The FileChange pointer is only valid for the duration of the callback
typedef void (*FileChangeCallback)(const FileChange* change, void* user_data);

typedef u32 FileWatcherCallbackHandle;
#define FILE_WATCHER_CALLBACK_INVALID ((FileWatcherCallbackHandle)0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Registry lifecycle - called by fl_init / fl_destroy, never by a watcher caller

// Brings the process-wide watcher registry up. Runs before any thread can start a watch, so the
// registry lock exists by the time two mounts race for it.
void file_watcher_init(void);

// Stops every watcher still running and releases the registry.
void file_watcher_destroy(void);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Watch Management

FlFileWatcherHandle file_watcher_start(FlString path, FlFileWatcherConfig config);

void file_watcher_stop(FlFileWatcherHandle handle);

// Check if watcher is still active (may fail due to path deletion, etc.)
bool file_watcher_is_active(FlFileWatcherHandle handle);

// Register a callback for a specific file path within the watched directory.
// Returns a callback handle for unregistering, or FILE_WATCHER_CALLBACK_INVALID on error.
FileWatcherCallbackHandle file_watcher_register_callback(FlFileWatcherHandle handle, FlString file_path,
                                                         FileChangeCallback callback, void* user_data);

void file_watcher_unregister_callback(FlFileWatcherHandle handle, FileWatcherCallbackHandle callback_handle);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Change Detection

// Get iterator for pending changes - allocates change data in provided arena
FileChangeIterator file_watcher_get_changes(FlFileWatcherHandle handle, struct FlArena* arena);

// Check if there are pending changes without allocating
bool file_watcher_has_changes(FlFileWatcherHandle handle);

void file_watcher_clear_changes(FlFileWatcherHandle handle);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Iterator Usage

bool file_change_iterator_has_next(const FileChangeIterator* iter);
FileChange file_change_iterator_next(FileChangeIterator* iter);
void file_change_iterator_reset(FileChangeIterator* iter);
