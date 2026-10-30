#pragma once

#include "../string.h"
#include "../types.h"

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// OS Path conversion

typedef struct {
#if PLATFORM_WINDOWS
    wchar_t* wide_path;
#else
    char* cstr_path;
#endif
} OsPath;

OsPath string_to_os_path(struct FlArena* arena, FlString path);

#if PLATFORM_WINDOWS
#define os_path_str(os_path) ((os_path).wide_path)
#else
#define os_path_str(os_path) ((os_path).cstr_path)
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

i64 file_get_size_os(FlString path);
i64 file_read_os(u8* dest, FlString path, const i64 buffer_size);
i64 file_write_os(FlString path, const u8* data, i64 size);
i64 file_open_os(FlString path, const int flags);
i64 file_read_from_handle_os(u8* dest, const i64 fd, const i64 file_size, const i64 buffer_size);
void file_close_handle_os(i64 fd);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Read-only file memory mapping

typedef struct FileMmap {
    const u8* data;
    u64 size;
#if PLATFORM_WINDOWS
    void* file_handle;
    void* mapping_handle;
#else
    int fd;
#endif
} FileMmap;

// Returns FileMmap with data=nullptr on any failure (file not found, empty file, mmap failure)
FileMmap file_mmap_open_os(FlString path);

// Safe to call with nullptr or already-closed mmap
void file_mmap_close_os(FileMmap* mmap);

// append: append to an existing file; false truncates/creates.
// Returns a file descriptor (>= 0) on success, -1 on failure
i64 file_open_write_os(FlString path, bool append);

// Returns the number of bytes written, or -1 on failure
i64 file_write_to_handle_os(i64 fd, const u8* data, i64 size);

bool file_rename_os(FlString old_path, FlString new_path);
bool file_exists_os(FlString path);
bool file_directory_exists_os(FlString path);
bool file_create_directory_os(FlString path);
bool file_create_directory_recursive_os(FlString path);
bool file_remove_directory_os(FlString path);
bool file_remove_directory_recursive_os(FlString path);
void file_delete_os(FlString path);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File system information and directory operations

typedef struct FileStat {
    FlString name; // Only set for directory listings
    u64 size;
    u64 modification_time;
    bool is_directory;
    bool is_regular_file;
    bool is_symlink;
    bool is_valid; // false = end of directory or error
    u32 mode;
} FileStat;

FileStat file_stat_os(FlString path);
FlString file_build_path_os(struct FlArena* arena, FlString base_path, FlString relative_path);

typedef struct DirHandle {
    void* platform_data; // Allocated on the arena passed to dir_open_os
    FlString base_path;
    bool is_valid;
} DirHandle;

DirHandle dir_open_os(struct FlArena* arena, FlString path);
FileStat dir_read_next_os(struct FlArena* arena, DirHandle* dir);
void dir_close_os(DirHandle* dir);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void stderr_write_os(FlString str);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

i64 get_current_time_us_os(void);
u64 get_current_time_ms_os(void);
FlString timestamp_format_os(struct FlArena* arena, i64 timestamp_us);

// Returns nanoseconds since an arbitrary starting point (only for computing durations)
u64 get_monotonic_time_ns_os(void);

// Convert a monotonic tick count to nanoseconds without overflowing: splitting
// into whole seconds and remainder ticks keeps every intermediate below
// ticks_per_second * 1e9, which fits in u64 for any realistic clock frequency
// and multi-year uptimes.
static inline u64 monotonic_ticks_to_ns(u64 ticks, u64 ticks_per_second) {
    u64 seconds = ticks / ticks_per_second;
    u64 remainder = ticks % ticks_per_second;
    return seconds * 1000000000ULL + (remainder * 1000000000ULL) / ticks_per_second;
}

// Note: Actual precision depends on OS scheduler (typically 1-15ms on most systems)
void sleep_us(u64 microseconds);
void sleep_ms(u64 milliseconds);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Splits a duration into whole seconds and the remaining nanoseconds (below one second), the form nanosleep takes.
static inline void sleep_ms_split(u64 milliseconds, u64* out_seconds, u32* out_nanoseconds) {
    *out_seconds = milliseconds / 1000;
    *out_nanoseconds = (u32)((milliseconds % 1000) * 1000000);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct CACHE_ALIGNED Mutex {
    u8 data[128]; // Opaque platform storage
} Mutex;

void mutex_init(Mutex* mutex);
void mutex_lock(Mutex* mutex);
void mutex_unlock(Mutex* mutex);
void mutex_destroy(Mutex* mutex);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Auto-unlock mutex scope guard

static inline void mutex_unlock_cleanup(Mutex** mutex_ptr) {
    mutex_unlock(*mutex_ptr);
}

#define mutex_lock_auto(mutex) \
    mutex_lock(mutex);         \
    __attribute__((cleanup(mutex_unlock_cleanup))) Mutex* CONCAT(_mutex_guard_, __LINE__) = (mutex)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct CACHE_ALIGNED CondVar {
    u8 data[128]; // Opaque platform storage
} CondVar;

void condvar_init(CondVar* condvar);
void condvar_wait(CondVar* condvar, Mutex* mutex);
void condvar_signal(CondVar* condvar);
void condvar_broadcast(CondVar* condvar);
void condvar_destroy(CondVar* condvar);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Thread management

typedef struct Thread {
    u8 data[64];
} Thread;

typedef void* (*ThreadFunc)(void* user_data);

bool os_thread_create(Thread* thread, ThreadFunc func, void* user_data);

// Returns the value returned by the thread function
void* os_thread_join(Thread* thread);

u64 os_thread_get_current_id(void);

// name must be at most 15 characters on Linux (pthread_setname_np fails on longer names)
void os_thread_set_name(const char* name);

// thread: nullptr = current thread. True only when the requested affinity was applied; false on
// failure, on a cpu id outside the platform's range, and on platforms that perform no affinity work
// at all. Linux is the only one that performs any; an empty list there is a no-op that succeeds.
bool os_thread_set_affinity(Thread* thread, const int* cpu_ids, int count);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Memory management functions

typedef enum ArenaError {
    ARENA_SUCCESS = 0,
    ARENA_RESERVE_FAILED,
    ARENA_PROTECTION_FAILED,
    ARENA_OUT_OF_RESERVED_MEMORY,
    ARENA_TOO_LARGE_RESERVE
} ArenaError;

u64 get_page_size_os(void);
ArenaError reserve_range_os(u64 size, void** out_ptr);
ArenaError commit_memory_os(void* ptr, u64 size);
ArenaError decommit_memory_os(void* ptr, u64 size);
// Inverse of reserve_range_os: releases the reservation (address range + VMA) back to the OS.
ArenaError release_range_os(void* ptr, u64 size);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// System information and utilities

FlString get_temp_directory_os(struct FlArena* arena);

FlString get_current_directory_os(struct FlArena* arena);

u32 get_process_id_os(void);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Disk space information

typedef struct DiskSpaceInfo {
    u64 bytes_available; // Free space available to the current user (respects quotas)
    u64 bytes_total;
    u64 bytes_free; // Total free space (may include reserved blocks on Unix)
    bool is_valid;
} DiskSpaceInfo;

// The path can be a file or directory - the filesystem it resides on is queried
DiskSpaceInfo get_disk_space_os(FlString path);

#if !PLATFORM_WINDOWS
// Test-only: forces every statvfs() lookup inside get_disk_space_os to fail, so the directory
// walk-up can be driven all the way past the root path.
void os_disk_space_test_force_statvfs_failure(bool enable);
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Environment variables (cross-platform)
//
// WARNING: These functions are NOT thread-safe when used concurrently.
// Set environment variables before spawning threads that may read them.

// Returns pointer to value (do not modify), or nullptr if not found
const char* getenv_os(const char* name);

// overwrite: if non-zero, overwrite an existing value
// Returns 0 on success, non-zero on failure
int setenv_os(const char* name, const char* value, int overwrite);

// Returns 0 on success, non-zero on failure
int unsetenv_os(const char* name);
