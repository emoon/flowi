#pragma once

// Operating-system services for hosts and tools built on flowi: time and sleeping, threads, the process
// environment, and plain file and directory access. Paths are UTF-8 FlStrings. Applications that read
// content should go through the VFS (<flowi/vfs/vfs.h>); this header is for the cases the VFS does not
// cover, such as temp files, directory scans and process-level setup.

#include <flowi/core/string.h>
#include <flowi/fl_api.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct FlArena FlArena;

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Time and sleeping

// Wall-clock time since the Unix epoch.
FL_API int64_t fl_os_time_us(void);
FL_API uint64_t fl_os_time_ms(void);

// Nanoseconds since an arbitrary starting point; only differences are meaningful.
FL_API uint64_t fl_os_monotonic_ns(void);

// Suspend the calling thread. Precision depends on the OS scheduler (typically 1-15 ms).
FL_API void fl_os_sleep_us(uint64_t microseconds);
FL_API void fl_os_sleep_ms(uint64_t milliseconds);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Threads

// Opaque storage for a platform thread handle.
typedef struct FlOsThread {
    uint8_t data[64];
} FlOsThread;

typedef void* (*FlOsThreadFunc)(void* user_data);

// Start a thread running func(user_data). False when the thread could not be created.
FL_API bool fl_os_thread_create(FlOsThread* thread, FlOsThreadFunc func, void* user_data);

// Wait for the thread to finish and return the value its function returned.
FL_API void* fl_os_thread_join(FlOsThread* thread);

// Name the calling thread. At most 15 characters on Linux; longer names are rejected by the OS.
FL_API void fl_os_thread_set_name(const char* name);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Process environment

FL_API uint32_t fl_os_process_id(void);

// The system temp directory.
FL_API FlString fl_os_temp_directory(FlArena* arena);

// The process working directory.
FL_API FlString fl_os_current_directory(FlArena* arena);

// The value of an environment variable (do not modify it), or nullptr when it is not set. Not thread-safe
// against concurrent changes to the environment.
FL_API const char* fl_os_getenv(const char* name);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Files

FL_API bool fl_os_file_exists(FlString path);
FL_API bool fl_os_directory_exists(FlString path);

// The size in bytes, or -1 when the file cannot be inspected.
FL_API int64_t fl_os_file_size(FlString path);

// Read up to buffer_size bytes into dest. Returns the number of bytes read, or -1 on failure.
FL_API int64_t fl_os_file_read(uint8_t* dest, FlString path, int64_t buffer_size);

// Create or truncate path and write size bytes. Returns the number of bytes written, or -1 on failure.
FL_API int64_t fl_os_file_write(FlString path, const uint8_t* data, int64_t size);

// Open path for writing; append keeps existing content, otherwise the file is truncated or created.
// Returns a descriptor (>= 0), or -1 on failure.
FL_API int64_t fl_os_file_open_write(FlString path, bool append);

// Returns the number of bytes written, or -1 on failure.
FL_API int64_t fl_os_file_write_to_handle(int64_t fd, const uint8_t* data, int64_t size);

FL_API void fl_os_file_close_handle(int64_t fd);

FL_API void fl_os_file_delete(FlString path);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Directories

FL_API bool fl_os_create_directory(FlString path);

// Create path and any missing parents.
FL_API bool fl_os_create_directory_recursive(FlString path);

FL_API bool fl_os_remove_directory(FlString path);

// Remove path and everything under it.
FL_API bool fl_os_remove_directory_recursive(FlString path);

// base_path joined with relative_path using the platform separator.
FL_API FlString fl_os_build_path(FlArena* arena, FlString base_path, FlString relative_path);

// What a stat or a directory read reports about one entry.
typedef struct FlOsFileStat {
    // The entry name; only set for directory listings.
    FlString name;
    uint64_t size;
    uint64_t modification_time;
    bool is_directory;
    bool is_regular_file;
    bool is_symlink;
    // False at the end of a directory or on error.
    bool is_valid;
    uint32_t mode;
} FlOsFileStat;

FL_API FlOsFileStat fl_os_file_stat(FlString path);

// An open directory iteration.
typedef struct FlOsDir {
    // Allocated on the arena passed to fl_os_dir_open.
    void* platform_data;
    FlString base_path;
    bool is_valid;
} FlOsDir;

// Open path for iteration; check is_valid on the result.
FL_API FlOsDir fl_os_dir_open(FlArena* arena, FlString path);

// The next entry (skipping "." and ".."), or a stat with is_valid false at the end.
FL_API FlOsFileStat fl_os_dir_read_next(FlArena* arena, FlOsDir* dir);

FL_API void fl_os_dir_close(FlOsDir* dir);

#ifdef __cplusplus
}
#endif
