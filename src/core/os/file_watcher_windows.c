#include "../core.h"
#include "../file_watcher_internal.h"
#include "../log.h"
#include "../path.h"
#include "os.h"

#if PLATFORM_WINDOWS

#include <windows.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
static LogChannelId FW_ID = LOG_CHANNEL_INVALID;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct WindowsWatcherData {
    HANDLE dir_handle;
    HANDLE event_handle;
    OVERLAPPED overlapped;
    u8 buffer[64 * 1024];
    DWORD bytes_returned;
    bool read_pending;
} WindowsWatcherData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileWatcherError map_windows_error(u32 win_error) {
    switch (win_error) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
            return FileWatcherError_InvalidPath;
        case ERROR_ACCESS_DENIED:
            return FileWatcherError_PermissionDenied;
        case ERROR_TOO_MANY_OPEN_FILES:
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
            return FileWatcherError_ResourceExhausted;
        default:
            return FileWatcherError_SystemError;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static DWORD get_notify_filter(FlFileWatcherConfig* config) {
    DWORD filter = 0;

    if (config->watch_files) {
        filter |= FILE_NOTIFY_CHANGE_FILE_NAME; // File creation/deletion/rename
        filter |= FILE_NOTIFY_CHANGE_SIZE;
        filter |= FILE_NOTIFY_CHANGE_LAST_WRITE;
    }

    if (config->watch_directories) {
        filter |= FILE_NOTIFY_CHANGE_DIR_NAME; // Directory creation/deletion/rename
        // Also watch for last write changes, as the parent directory's timestamp
        // is updated when subdirectories are created/deleted
        filter |= FILE_NOTIFY_CHANGE_LAST_WRITE;
    }

    // Windows requires at least one filter flag to be set
    if (filter == 0) {
        filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE;
    }

    return filter;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static u32 map_windows_action_to_change_types(DWORD action) {
    switch (action) {
        case FILE_ACTION_ADDED:
            return FlFileChangeType_Created;
        case FILE_ACTION_REMOVED:
            return FlFileChangeType_Deleted;
        case FILE_ACTION_MODIFIED:
            return FlFileChangeType_Modified;
        case FILE_ACTION_RENAMED_OLD_NAME:
        case FILE_ACTION_RENAMED_NEW_NAME:
            return FlFileChangeType_Moved;
        default:
            return 0;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool start_directory_watch(WindowsWatcherData* data, DWORD filter, bool recursive) {
    HANDLE event = data->overlapped.hEvent;
    memset(&data->overlapped, 0, sizeof(OVERLAPPED));
    data->overlapped.hEvent = event;

    // Reset event to non-signaled state (required for manual-reset events)
    ResetEvent(data->overlapped.hEvent);

    BOOL result = ReadDirectoryChangesW(data->dir_handle, data->buffer, sizeof(data->buffer),
                                        recursive ? TRUE : FALSE, // Watch subtree
                                        filter,
                                        nullptr, // Must be nullptr for overlapped I/O (use GetOverlappedResult instead)
                                        &data->overlapped,
                                        nullptr // No completion routine
    );

    DWORD error = GetLastError();
    if (!result && error != ERROR_IO_PENDING) {
        logc_error(FW_ID, "ReadDirectoryChangesW failed immediately: error=0x%x, filter=0x%x, recursive=%d", error,
                   filter, recursive);
        return false;
    }

    data->read_pending = true;
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileWatcherError platform_start_watching(FileWatcherState* state) {
    if (FW_ID == LOG_CHANNEL_INVALID) {
        FW_ID = fl_log_register_channel(FL_S("FILE_WATCHER"));
    }

    OsPath os_path = string_to_os_path(get_file_watcher_registry()->arena, state->watch_path);

    // FILE_FLAG_BACKUP_SEMANTICS is required to open a directory handle
    HANDLE dir_handle
        = CreateFileW(os_path_str(os_path), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);

    if (dir_handle == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        logc_error(FW_ID, "FileWatcher[%u]: Failed to open directory: 0x%x", state->handle, error);
        return map_windows_error(error);
    }

    WindowsWatcherData* data = arena_alloc_zero(get_file_watcher_registry()->arena, WindowsWatcherData);
    data->dir_handle = dir_handle;
    data->read_pending = false;

    // Create manual-reset event for overlapped I/O
    data->event_handle = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (data->event_handle == nullptr) {
        DWORD error = GetLastError();
        logc_error(FW_ID, "FileWatcher[%u]: Failed to create event: 0x%x", state->handle, error);
        CloseHandle(dir_handle);
        return map_windows_error(error);
    }
    data->overlapped.hEvent = data->event_handle;

    DWORD filter = get_notify_filter(&state->config);

    logc_debug(FW_ID, "FileWatcher[%u]: Starting watch with filter: 0x%x (files: %d, dirs: %d, recursive: %d)",
               state->handle, filter, state->config.watch_files, state->config.watch_directories,
               state->config.recursive);

    if (!start_directory_watch(data, filter, state->config.recursive)) {
        DWORD error = GetLastError();
        logc_error(FW_ID, "FileWatcher[%u]: Failed to start directory watch: 0x%x", state->handle, error);
        CloseHandle(data->event_handle);
        CloseHandle(dir_handle);
        return map_windows_error(error);
    }

    state->platform_data = data;

    logc_debug(FW_ID, "FileWatcher[%u]: Windows watcher initialized (recursive: %d)", state->handle,
               state->config.recursive);

    return FileWatcherError_Success;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void platform_stop_watching(FileWatcherState* state) {
    if (!state || !state->platform_data) {
        return;
    }

    WindowsWatcherData* data = (WindowsWatcherData*)state->platform_data;

    // CancelIo only requests cancellation: the kernel can still write into the OVERLAPPED structure and
    // the read buffer until the completion is delivered, so the handles stay open until GetOverlappedResult
    // returns. That wait normally completes immediately with ERROR_OPERATION_ABORTED.
    if (data->read_pending && data->dir_handle != INVALID_HANDLE_VALUE) {
        CancelIo(data->dir_handle);
        DWORD bytes_transferred = 0;
        GetOverlappedResult(data->dir_handle, &data->overlapped, &bytes_transferred, TRUE);
        data->read_pending = false;
    }

    if (data->event_handle != nullptr && data->event_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(data->event_handle);
        data->event_handle = nullptr;
    }

    if (data->dir_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(data->dir_handle);
        data->dir_handle = INVALID_HANDLE_VALUE;
    }

    state->platform_data = nullptr;

    logc_debug(FW_ID, "FileWatcher[%u]: Windows watcher stopped", state->handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileWatcherError platform_poll_changes(FileWatcherState* state) {
    if (!state || !state->platform_data || !state->is_active) {
        return FileWatcherError_Success;
    }

    WindowsWatcherData* data = (WindowsWatcherData*)state->platform_data;

    int poll_iterations = 0;
    while (data->read_pending) {
        DWORD wait_result = WaitForSingleObject(data->event_handle, 0);
        if (wait_result != WAIT_OBJECT_0) {
            if (poll_iterations > 0) {
                logc_debug(FW_ID, "FileWatcher[%u]: Processed %d event batches", state->handle, poll_iterations);
            }
            return FileWatcherError_Success;
        }

        poll_iterations++;

        DWORD bytes_transferred = 0;
        BOOL result = GetOverlappedResult(data->dir_handle, &data->overlapped, &bytes_transferred,
                                          FALSE // Don't wait (event already signaled)
        );

        if (!result) {
            DWORD error = GetLastError();
            if (error == ERROR_IO_INCOMPLETE) {
                return FileWatcherError_Success;
            } else {
                logc_error(FW_ID, "FileWatcher[%u]: GetOverlappedResult failed: 0x%x", state->handle, error);
                state->is_active = false;
                return FileWatcherError_SystemError;
            }
        }

        data->read_pending = false;

        logc_debug(FW_ID, "FileWatcher[%u]: ReadDirectoryChanges completed, %lu bytes transferred", state->handle,
                   bytes_transferred);

        if (bytes_transferred == 0) {
            logc_warning(FW_ID, "FileWatcher[%u]: Change buffer overflow or watcher closed", state->handle);

            DWORD filter = get_notify_filter(&state->config);
            if (!start_directory_watch(data, filter, state->config.recursive)) {
                state->is_active = false;
                return FileWatcherError_SystemError;
            }
            return FileWatcherError_Success;
        }

        FILE_NOTIFY_INFORMATION* fni = (FILE_NOTIFY_INFORMATION*)data->buffer;

        while (true) {
            WCHAR* filename_w = fni->FileName;
            DWORD filename_len = fni->FileNameLength / sizeof(WCHAR); // Length is in bytes

            int utf8_len = WideCharToMultiByte(CP_UTF8, 0, filename_w, filename_len, nullptr, 0, nullptr, nullptr);
            if (utf8_len > 0) {
                FlArena* temp_arena = arena_new();
                char* utf8_buffer = arena_alloc_array(temp_arena, char, utf8_len + 1);
                WideCharToMultiByte(CP_UTF8, 0, filename_w, filename_len, utf8_buffer, utf8_len, nullptr, nullptr);
                utf8_buffer[utf8_len] = '\0';

                FlString filename = string_from_cstr(utf8_buffer);
                FlString full_path = fl_path_join(temp_arena, state->watch_path, filename);

                u32 change_types = map_windows_action_to_change_types(fni->Action);

                if (change_types != 0) {
                    // GetFileAttributesW works for created/modified files, but not for deleted ones
                    bool is_directory = false;
                    if (!(change_types & FlFileChangeType_Deleted)) {
                        OsPath full_path_os = string_to_os_path(temp_arena, full_path);
                        DWORD attributes = GetFileAttributesW(os_path_str(full_path_os));
                        if (attributes != INVALID_FILE_ATTRIBUTES) {
                            is_directory = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                        }
                    }

                    logc_debug(FW_ID, "FileWatcher[%u]: %S (types: 0x%x, dir: %d)", state->handle, full_path,
                               change_types, is_directory);
                    add_file_change(state, full_path, FL_S(""), change_types, is_directory);
                }

                fl_arena_destroy(temp_arena);
            }

            if (fni->NextEntryOffset == 0) {
                break;
            }
            fni = (FILE_NOTIFY_INFORMATION*)((u8*)fni + fni->NextEntryOffset);
        }

        DWORD filter = get_notify_filter(&state->config);
        if (!start_directory_watch(data, filter, state->config.recursive)) {
            logc_error(FW_ID, "FileWatcher[%u]: Failed to restart directory watch", state->handle);
            state->is_active = false;
            return FileWatcherError_SystemError;
        }
    }

    return FileWatcherError_Success;
}

#endif // PLATFORM_WINDOWS
