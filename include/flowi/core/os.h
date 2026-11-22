#pragma once

// Operating-system services for hosts built on flowi: time and sleeping, the process environment, and
// directory scans. Paths are UTF-8 FlStrings. Applications that read content should go through the VFS
// (<flowi/vfs/vfs.h>); this header is for the cases the VFS does not cover.

#include <flowi/core/string.h>
#include <flowi/fl_api.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct FlArena FlArena;

#ifdef __cplusplus
extern "C" {
#endif

// Time and sleeping

// Wall-clock time since the Unix epoch.
FL_API int64_t fl_os_time_us(void);

// Nanoseconds since an arbitrary starting point; only differences are meaningful.
FL_API uint64_t fl_os_monotonic_ns(void);

// Suspend the calling thread. Precision depends on the OS scheduler (typically 1-15 ms).
FL_API void fl_os_sleep_us(uint64_t microseconds);

// Process environment

FL_API uint32_t fl_os_process_id(void);

// The system temp directory.
FL_API FlString fl_os_temp_directory(FlArena* arena);

// Files and directories

FL_API bool fl_os_directory_exists(FlString path);

FL_API void fl_os_file_delete(FlString path);

// Create path and any missing parents.
FL_API bool fl_os_create_directory_recursive(FlString path);

// What a directory read reports about one entry.
typedef struct FlOsFileStat {
    // The entry name; only set for directory listings.
    FlString name;
    uint64_t size;
    // Milliseconds since the Unix epoch.
    uint64_t modification_time;
    bool is_directory;
    bool is_regular_file;
    bool is_symlink;
    // False at the end of a directory or on error.
    bool is_valid;
    uint32_t mode;
} FlOsFileStat;

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
