#include "../core.h"
#include "os.h"

#if PLATFORM_WINDOWS
#include "../arena.h"
#include "../memory.h"
#include "../sprintf.h"
#include <stdio.h>
#include <time.h>
#include <windows.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// OS Path conversion - Windows uses UTF-16 wide strings

// Never returns nullptr; empty input and conversion failure both yield L"".
static wchar_t* string_to_wide(struct FlArena* arena, FlString str) {
    int required_size = str.length > 0 ? MultiByteToWideChar(CP_UTF8, 0, str.data, (int)str.length, nullptr, 0) : 0;
    if (required_size <= 0) {
        wchar_t* empty = arena_alloc_array(arena, wchar_t, 1);
        empty[0] = L'\0';
        return empty;
    }

    wchar_t* wide_str = arena_alloc_array(arena, wchar_t, required_size + 1);

    MultiByteToWideChar(CP_UTF8, 0, str.data, (int)str.length, wide_str, required_size);
    wide_str[required_size] = L'\0';

    return wide_str;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

OsPath string_to_os_path(struct FlArena* arena, FlString path) {
    OsPath result;
    result.wide_path = string_to_wide(arena, path);
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Shared Win32 helpers
//
// FILETIME counts 100-nanosecond intervals since January 1, 1601; Unix epoch is
// January 1, 1970. These convert to milliseconds / microseconds since the Unix epoch.

static u64 filetime_to_unix_ms(FILETIME ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    const u64 EPOCH_DIFFERENCE = 11644473600000ULL; // milliseconds between 1601 and 1970
    return (uli.QuadPart / 10000ULL) - EPOCH_DIFFERENCE;
}

static i64 filetime_to_unix_us(FILETIME ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    const i64 EPOCH_DIFFERENCE = 11644473600000000LL; // microseconds between 1601 and 1970
    return (i64)(uli.QuadPart / 10LL) - EPOCH_DIFFERENCE;
}

// Returns an empty string on conversion failure.
static FlString wide_to_string(struct FlArena* arena, const wchar_t* wide) {
    int utf8_size = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (utf8_size <= 0) {
        return string_empty();
    }

    char* utf8 = arena_alloc_array(arena, char, utf8_size);
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, utf8_size, nullptr, nullptr);
    return string_from_cstr(utf8);
}

// The caller owns result.name and result.is_valid.
static void filestat_from_attributes(FileStat* result, DWORD file_attributes, DWORD size_low, DWORD size_high,
                                     FILETIME write_time) {
    LARGE_INTEGER size;
    size.LowPart = size_low;
    size.HighPart = size_high;
    result->size = (u64)size.QuadPart;
    result->modification_time = filetime_to_unix_ms(write_time);
    result->is_directory = (file_attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    result->is_regular_file = !result->is_directory;
    result->is_symlink = (file_attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    result->mode = file_attributes;
}

// Returns INVALID_HANDLE_VALUE on failure.
static HANDLE open_read_handle(OsPath os_path) {
    // FILE_SHARE_WRITE as well as FILE_SHARE_READ: a reader must tolerate a writer's access, or opening a file
    // something else already has open for writing - a live log file, say - fails outright.
    return CreateFileW(os_path_str(os_path), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, nullptr);
}

static void std_write(DWORD which, FlString str) {
    HANDLE handle = GetStdHandle(which);
    if (handle != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(handle, str.data, (DWORD)str.length, &written, nullptr);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

i64 file_get_size_os(FlString path) {
    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);

    HANDLE file = open_read_handle(os_path);
    if (file == INVALID_HANDLE_VALUE) {
        return -1;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(file, &size)) {
        CloseHandle(file);
        return -1;
    }

    CloseHandle(file);
    return size.QuadPart;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool file_rename_os(FlString old_path, FlString new_path) {
    arena_scratch_auto(temp);
    OsPath old_os_path = string_to_os_path(temp.arena, old_path);
    OsPath new_os_path = string_to_os_path(temp.arena, new_path);

    // Use MoveFileExW with MOVEFILE_REPLACE_EXISTING to match POSIX rename() behavior
    return MoveFileExW(os_path_str(old_os_path), os_path_str(new_os_path), MOVEFILE_REPLACE_EXISTING) != 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

i64 file_read_from_handle_os(u8* dest, const i64 handle, const i64 file_size, const i64 buffer_size) {
    HANDLE file = (HANDLE)handle;
    const i64 read_size = (buffer_size < file_size) ? buffer_size : file_size;
    // A negative size (e.g. file_get_size_os's -1 failure sentinel) would be reinterpreted
    // as a huge DWORD by ReadFile, turning it into an unbounded over-read of dest.
    if (read_size < 0) {
        return -1;
    }

    DWORD bytes_read;
    if (!ReadFile(file, dest, (DWORD)read_size, &bytes_read, nullptr)) {
        return -1;
    }

    return bytes_read;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

i64 file_read_os(u8* dest, FlString path, const i64 buffer_size) {
    const i64 file_handle = file_open_os(path, 0);
    if (file_handle < 0) {
        return -1;
    }

    const i64 file_size = file_get_size_os(path);
    if (file_size < 0) {
        file_close_handle_os(file_handle);
        return -1;
    }

    const i64 ret_size = file_read_from_handle_os(dest, file_handle, file_size, buffer_size);

    file_close_handle_os(file_handle);
    return ret_size;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

i64 file_open_os(FlString path, const int flags) {
    (void)flags;
    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);

    HANDLE file = open_read_handle(os_path);

    if (file == INVALID_HANDLE_VALUE) {
        return -1;
    }
    return (i64)file;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void file_close_handle_os(const i64 handle) {
    CloseHandle((HANDLE)handle);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileMmap file_mmap_open_os(FlString path) {
    FileMmap result = { .data = nullptr, .size = 0, .file_handle = nullptr, .mapping_handle = nullptr };

    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);

    HANDLE file = open_read_handle(os_path);
    if (file == INVALID_HANDLE_VALUE) {
        return result;
    }

    LARGE_INTEGER file_size;
    if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart <= 0) {
        CloseHandle(file);
        return result;
    }

    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping == nullptr) {
        CloseHandle(file);
        return result;
    }

    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (view == nullptr) {
        CloseHandle(mapping);
        CloseHandle(file);
        return result;
    }

    result.data = (const u8*)view;
    result.size = (u64)file_size.QuadPart;
    result.file_handle = file;
    result.mapping_handle = mapping;
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void file_mmap_close_os(FileMmap* mmap) {
    if (mmap == nullptr || mmap->data == nullptr) {
        return;
    }

    UnmapViewOfFile(mmap->data);
    CloseHandle(mmap->mapping_handle);
    CloseHandle(mmap->file_handle);
    mmap->data = nullptr;
    mmap->size = 0;
    mmap->file_handle = nullptr;
    mmap->mapping_handle = nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

i64 file_open_write_os(FlString path, bool append) {
    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);

    DWORD access = GENERIC_WRITE;
    DWORD creation = append ? OPEN_ALWAYS : CREATE_ALWAYS;

    // FILE_SHARE_READ so a log file stays readable while the logger holds it open.
    HANDLE file
        = CreateFileW(os_path_str(os_path), access, FILE_SHARE_READ, nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return -1;
    }

    if (append) {
        SetFilePointer(file, 0, nullptr, FILE_END);
    }

    return (i64)file;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

i64 file_write_to_handle_os(i64 fd, const u8* data, i64 size) {
    if (fd < 0 || data == nullptr || size <= 0) {
        return -1;
    }

    DWORD written;
    if (!WriteFile((HANDLE)fd, data, (DWORD)size, &written, nullptr)) {
        return -1;
    }

    return (i64)written;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void stderr_write_os(FlString str) {
    std_write(STD_ERROR_HANDLE, str);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

i64 get_current_time_us_os(void) {
    // The Precise variant, not GetSystemTimeAsFileTime: the latter ticks at the ~15 ms scheduler interval, so
    // callers that use the value to tell two events apart - arena identity in the watchdog - see them collide.
    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    return filetime_to_unix_us(ft);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u64 get_monotonic_time_ns_os(void) {
    LARGE_INTEGER counter, frequency;

    if (!QueryPerformanceCounter(&counter)) {
        return 0;
    }

    if (!QueryPerformanceFrequency(&frequency)) {
        return 0;
    }

    return monotonic_ticks_to_ns((u64)counter.QuadPart, (u64)frequency.QuadPart);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString timestamp_format_os(struct FlArena* arena, i64 timestamp_us) {
    time_t seconds = (time_t)(timestamp_us / 1000000LL);
    i64 microseconds = timestamp_us % 1000000LL;

    struct tm local_time;

    if (localtime_s(&local_time, &seconds) != 0) {
        return string_empty();
    }

    return fl_sprintf_arena(arena, "%04d:%02d:%02d %02d:%02d:%02d.%06d", local_time.tm_year + 1900,
                            local_time.tm_mon + 1, local_time.tm_mday, local_time.tm_hour, local_time.tm_min,
                            local_time.tm_sec, (int)microseconds);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static_assert(sizeof(CRITICAL_SECTION) <= sizeof(((Mutex*)0)->data), "CRITICAL_SECTION too large for Mutex struct");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void mutex_init(Mutex* mutex) {
    CRITICAL_SECTION* cs = (CRITICAL_SECTION*)mutex->data;
    InitializeCriticalSection(cs);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void mutex_lock(Mutex* mutex) {
    CRITICAL_SECTION* cs = (CRITICAL_SECTION*)mutex->data;
    EnterCriticalSection(cs);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void mutex_unlock(Mutex* mutex) {
    CRITICAL_SECTION* cs = (CRITICAL_SECTION*)mutex->data;
    LeaveCriticalSection(cs);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void mutex_destroy(Mutex* mutex) {
    CRITICAL_SECTION* cs = (CRITICAL_SECTION*)mutex->data;
    DeleteCriticalSection(cs);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static_assert(sizeof(CONDITION_VARIABLE) <= sizeof(((CondVar*)0)->data),
              "CONDITION_VARIABLE too large for CondVar struct");

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void condvar_init(CondVar* condvar) {
    CONDITION_VARIABLE* cv = (CONDITION_VARIABLE*)condvar->data;
    InitializeConditionVariable(cv);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void condvar_wait(CondVar* condvar, Mutex* mutex) {
    CONDITION_VARIABLE* cv = (CONDITION_VARIABLE*)condvar->data;
    CRITICAL_SECTION* cs = (CRITICAL_SECTION*)mutex->data;
    SleepConditionVariableCS(cv, cs, INFINITE);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void condvar_signal(CondVar* condvar) {
    CONDITION_VARIABLE* cv = (CONDITION_VARIABLE*)condvar->data;
    WakeConditionVariable(cv);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void condvar_broadcast(CondVar* condvar) {
    CONDITION_VARIABLE* cv = (CONDITION_VARIABLE*)condvar->data;
    WakeAllConditionVariable(cv);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void condvar_destroy(CondVar* condvar) {
    // CONDITION_VARIABLE doesn't need explicit cleanup on Windows
    (void)condvar;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Memory management functions

u64 get_page_size_os(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (u64)si.dwPageSize;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ArenaError reserve_range_os(u64 size, void** out_ptr) {
    void* ptr = VirtualAlloc(nullptr, (SIZE_T)size, MEM_RESERVE, PAGE_READWRITE);
    if (!ptr) {
        return ARENA_RESERVE_FAILED;
    }
    *out_ptr = ptr;
    return ARENA_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ArenaError commit_memory_os(void* ptr, u64 size) {
    void* result = VirtualAlloc(ptr, (SIZE_T)size, MEM_COMMIT, PAGE_READWRITE);
    if (!result) {
        return ARENA_PROTECTION_FAILED;
    }
    return ARENA_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ArenaError decommit_memory_os(void* ptr, u64 size) {
    if (!VirtualFree(ptr, (SIZE_T)size, MEM_DECOMMIT)) {
        return ARENA_PROTECTION_FAILED;
    }
    return ARENA_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

ArenaError release_range_os(void* ptr, u64 size) {
    // MEM_RELEASE requires the size to be 0 and ptr to be the base returned by the reserving VirtualAlloc.
    (void)size;
    if (!VirtualFree(ptr, 0, MEM_RELEASE)) {
        return ARENA_PROTECTION_FAILED;
    }
    return ARENA_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File system operations

bool file_exists_os(FlString path) {
    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);
    DWORD attrs = GetFileAttributesW(os_path_str(os_path));
    return attrs != INVALID_FILE_ATTRIBUTES;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool file_directory_exists_os(FlString path) {
    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);
    DWORD attrs = GetFileAttributesW(os_path_str(os_path));
    return (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool file_create_directory_os(FlString path) {
    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);
    BOOL success = CreateDirectoryW(os_path_str(os_path), nullptr);
    return success != 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool file_create_directory_recursive_os(FlString path) {
    if (string_is_empty(path)) {
        return false;
    }

    if (file_directory_exists_os(path)) {
        return true;
    }

    arena_scratch_auto(temp);

    char* path_copy = arena_alloc_array(temp.arena, char, path.length + 1);
    memcpy(path_copy, path.data, path.length);
    path_copy[path.length] = '\0';

    char* p = path_copy;

    // Skip drive letter for Windows (e.g., "C:\")
    if (path.length >= 3 && path_copy[1] == ':' && (path_copy[2] == '\\' || path_copy[2] == '/')) {
        p += 3;
    }
    // Skip leading slash/backslash for UNC or rooted paths
    else if (*p == '/' || *p == '\\') {
        p++;
    }

    while (*p != '\0') {
        while (*p != '\0' && *p != '/' && *p != '\\') {
            p++;
        }

        char saved = *p;
        *p = '\0';

        // A concurrent creator can make the component between the existence check and the create
        // call; the component existing is the desired outcome, so only a create failure with the
        // directory still absent is an error.
        FlString component = (FlString) { .data = path_copy, .length = (u64)(p - path_copy) };
        if (!file_directory_exists_os(component)) {
            if (!file_create_directory_os(component) && !file_directory_exists_os(component)) {
                return false;
            }
        }

        *p = saved;
        if (*p != '\0') {
            p++;
        }
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void file_delete_os(FlString path) {
    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);
    DeleteFileW(os_path_str(os_path));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

i64 file_write_os(FlString path, const u8* data, i64 size) {
    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);

    HANDLE file
        = CreateFileW(os_path_str(os_path), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return -1;
    }

    DWORD bytes_written;
    BOOL success = WriteFile(file, data, (DWORD)size, &bytes_written, nullptr);
    CloseHandle(file);

    if (!success) {
        return -1;
    }

    return bytes_written;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool file_remove_directory_os(FlString path) {
    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);
    BOOL success = RemoveDirectoryW(os_path_str(os_path));
    return success != 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool file_remove_directory_recursive_os(FlString path) {
    arena_scratch_auto(temp);

    FlString search_pattern = fl_string_concat(temp.arena, path, FL_S("\\*"));
    OsPath search_os_path = string_to_os_path(temp.arena, search_pattern);

    WIN32_FIND_DATAW find_data;
    HANDLE find_handle = FindFirstFileW(os_path_str(search_os_path), &find_data);

    if (find_handle != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(find_data.cFileName, L".") == 0 || wcscmp(find_data.cFileName, L"..") == 0) {
                continue;
            }

            FlString item_name = wide_to_string(temp.arena, find_data.cFileName);
            if (!string_is_empty(item_name)) {
                FlString item_path = file_build_path_os(temp.arena, path, item_name);

                bool is_directory = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                bool is_link = (find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                if (is_directory && is_link) {
                    file_remove_directory_os(item_path);
                } else if (is_directory) {
                    file_remove_directory_recursive_os(item_path);
                } else {
                    file_delete_os(item_path);
                }
            }
        } while (FindNextFileW(find_handle, &find_data));

        FindClose(find_handle);
    }

    bool success = file_remove_directory_os(path);
    return success;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u64 get_current_time_ms_os(void) {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return filetime_to_unix_ms(ft);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// File system information and directory operations

FileStat file_stat_os(FlString path) {
    FileStat result = { 0 };

    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);

    WIN32_FILE_ATTRIBUTE_DATA attrs;
    if (GetFileAttributesExW(os_path_str(os_path), GetFileExInfoStandard, &attrs) != 0) {
        result.name = (FlString) { 0 }; // Not used for single file stat
        filestat_from_attributes(&result, attrs.dwFileAttributes, attrs.nFileSizeLow, attrs.nFileSizeHigh,
                                 attrs.ftLastWriteTime);
        result.is_valid = true;
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString file_build_path_os(struct FlArena* arena, FlString base_path, FlString relative_path) {
    if (base_path.length == 0) {
        return fl_string_copy(arena, relative_path);
    }
    if (relative_path.length == 0) {
        return fl_string_copy(arena, base_path);
    }

    // Check if we need a separator (Windows uses backslash, but also accepts forward slash)
    bool base_ends_with_sep
        = (base_path.data[base_path.length - 1] == '\\' || base_path.data[base_path.length - 1] == '/');
    bool relative_starts_with_sep = (relative_path.data[0] == '\\' || relative_path.data[0] == '/');

    if (base_ends_with_sep && relative_starts_with_sep) {
        FlString relative_without_sep = { relative_path.data + 1, relative_path.length - 1 };
        return fl_string_concat(arena, base_path, relative_without_sep);
    } else if (!base_ends_with_sep && !relative_starts_with_sep) {
        return fl_string_concat(arena, fl_string_concat(arena, base_path, FL_S("\\")), relative_path);
    } else {
        return fl_string_concat(arena, base_path, relative_path);
    }
}

typedef struct WindowsDirData {
    HANDLE find_handle;
    WIN32_FIND_DATAW find_data;
    bool first_call;
    char utf8_filename[MAX_PATH * 3];
} WindowsDirData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DirHandle dir_open_os(FlArena* arena, FlString path) {
    DirHandle handle = { 0 };

    FlString search_pattern = fl_string_concat(arena, path, FL_S("\\*"));
    OsPath os_path = string_to_os_path(arena, search_pattern);

    WIN32_FIND_DATAW find_data;
    HANDLE find_handle = FindFirstFileW(os_path_str(os_path), &find_data);

    if (find_handle != INVALID_HANDLE_VALUE) {
        WindowsDirData* dir_data = arena_alloc(arena, WindowsDirData);
        dir_data->find_handle = find_handle;
        dir_data->find_data = find_data;
        dir_data->first_call = true;

        handle.platform_data = dir_data;
        handle.base_path = path;
        handle.is_valid = true;
    }

    return handle;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FileStat dir_read_next_os(FlArena* arena, DirHandle* dir_handle) {
    FileStat result = { 0 };
    result.is_valid = false;

    if (!dir_handle || !dir_handle->is_valid) {
        return result;
    }

    WindowsDirData* dir_data = (WindowsDirData*)dir_handle->platform_data;
    if (dir_data->find_handle == INVALID_HANDLE_VALUE) {
        return result;
    }

    BOOL found;
    if (dir_data->first_call) {
        found = TRUE; // We already have the first result from FindFirstFile
        dir_data->first_call = false;
    } else {
        found = FindNextFileW(dir_data->find_handle, &dir_data->find_data);
    }

    while (found) {
        // Skip . and ..
        if ((dir_data->find_data.cFileName[0] == L'.' && dir_data->find_data.cFileName[1] == L'\0')
            || (dir_data->find_data.cFileName[0] == L'.' && dir_data->find_data.cFileName[1] == L'.'
                && dir_data->find_data.cFileName[2] == L'\0')) {
            found = FindNextFileW(dir_data->find_handle, &dir_data->find_data);
            continue;
        }

        int name_len = WideCharToMultiByte(CP_UTF8, 0, dir_data->find_data.cFileName, -1, dir_data->utf8_filename,
                                           sizeof(dir_data->utf8_filename), nullptr, nullptr);
        if (name_len <= 0) {
            // An entry with no representable name cannot be joined onto a path, so it must not be
            // surfaced as valid - callers loop on is_valid and would build a path from nothing.
            found = FindNextFileW(dir_data->find_handle, &dir_data->find_data);
            continue;
        }

        result.name = fl_string_copy(arena, string_from_cstr(dir_data->utf8_filename));
        result.is_valid = true;

        filestat_from_attributes(&result, dir_data->find_data.dwFileAttributes, dir_data->find_data.nFileSizeLow,
                                 dir_data->find_data.nFileSizeHigh, dir_data->find_data.ftLastWriteTime);

        break; // Found valid entry
    }

    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void dir_close_os(DirHandle* dir_handle) {
    if (dir_handle && dir_handle->is_valid) {
        WindowsDirData* dir_data = (WindowsDirData*)dir_handle->platform_data;
        if (dir_data->find_handle != INVALID_HANDLE_VALUE) {
            FindClose(dir_data->find_handle);
            dir_data->find_handle = INVALID_HANDLE_VALUE;
        }
        dir_handle->is_valid = false;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// System information and utilities

FlString get_temp_directory_os(struct FlArena* arena) {
    wchar_t temp_path[MAX_PATH];
    DWORD result = GetTempPathW(MAX_PATH, temp_path);
    if (result == 0 || result > MAX_PATH) {
        return fl_string_copy(arena, FL_S("C:\\Windows\\Temp"));
    }

    FlString temp_dir = wide_to_string(arena, temp_path);
    if (string_is_empty(temp_dir)) {
        return fl_string_copy(arena, FL_S("C:\\Windows\\Temp"));
    }

    if (temp_dir.length > 0 && temp_dir.data[temp_dir.length - 1] == '\\') {
        temp_dir.length--;
    }

    return temp_dir;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString get_current_directory_os(struct FlArena* arena) {
    // Windows extended path support allows up to 32,767 characters
    const DWORD initial_size = 32768;
    wchar_t* buffer = arena_alloc_array(arena, wchar_t, initial_size);

    DWORD result = GetCurrentDirectoryW(initial_size, buffer);

    if (result == 0) {
        return (FlString) { 0 };
    }

    if (result > initial_size) {
        buffer = arena_alloc_array(arena, wchar_t, result);
        DWORD second_result = GetCurrentDirectoryW(result, buffer);
        if (second_result == 0 || second_result > result) {
            return (FlString) { 0 };
        }
    }

    return wide_to_string(arena, buffer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u32 get_process_id_os(void) {
    return GetCurrentProcessId();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Thread management - Windows implementation

typedef struct {
    HANDLE handle;
    DWORD thread_id;
} WindowsThread;

_Static_assert(sizeof(WindowsThread) <= sizeof(Thread), "Thread structure too small for Windows thread handle");

typedef struct {
    ThreadFunc func;
    void* user_data;
} ThreadStartData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static DWORD WINAPI thread_entry_adapter(LPVOID param) {
    ThreadStartData* data = (ThreadStartData*)param;
    ThreadFunc func = data->func;
    void* user_data = data->user_data;

    // Free the adapter data (allocated by thread_create)
    free(data);

    void* result = func(user_data);

    // Windows threads return DWORD, not void*: the pointer is truncated on 64-bit
    return (DWORD)(uintptr_t)result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool os_thread_create(Thread* thread, ThreadFunc func, void* user_data) {
    WindowsThread* wt = (WindowsThread*)thread;

    // Allocate adapter data (freed by thread_entry_adapter)
    ThreadStartData* data = (ThreadStartData*)malloc(sizeof(ThreadStartData));
    if (!data) {
        return false;
    }

    data->func = func;
    data->user_data = user_data;

    wt->handle = CreateThread(nullptr,              // Default security attributes
                              0,                    // Default stack size
                              thread_entry_adapter, // Thread function
                              data,                 // Thread parameter
                              0,                    // Default creation flags
                              &wt->thread_id        // Returns thread ID
    );

    if (wt->handle == nullptr) {
        free(data);
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* os_thread_join(Thread* thread) {
    WindowsThread* wt = (WindowsThread*)thread;

    WaitForSingleObject(wt->handle, INFINITE);

    DWORD exit_code = 0;
    GetExitCodeThread(wt->handle, &exit_code);

    CloseHandle(wt->handle);
    wt->handle = nullptr;

    // Exit code as void* (limited to 32 bits)
    return (void*)(uintptr_t)exit_code;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u64 os_thread_get_current_id(void) {
    return (u64)GetCurrentThreadId();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void os_thread_set_name(const char* name) {
    wchar_t wide_name[64];
    int len = MultiByteToWideChar(CP_UTF8, 0, name, -1, wide_name, 64);
    if (len > 0) {
        SetThreadDescription(GetCurrentThread(), wide_name);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool os_thread_set_affinity(Thread* thread, const int* cpu_ids, int count) {
    (void)thread;
    (void)cpu_ids;
    (void)count;
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Sleep functions

void sleep_us(u64 microseconds) {
    // Windows Sleep() only supports millisecond precision
    DWORD ms = (DWORD)((microseconds + 999) / 1000); // Round up
    if (ms == 0 && microseconds > 0) {
        ms = 1;
    }
    Sleep(ms);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void sleep_ms(u64 milliseconds) {
    Sleep((DWORD)milliseconds);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Environment variables

const char* getenv_os(const char* name) {
    return getenv(name);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int setenv_os(const char* name, const char* value, int overwrite) {
    (void)overwrite; // Windows _putenv_s always overwrites
    return _putenv_s(name, value);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int unsetenv_os(const char* name) {
    // On Windows, setting to empty string removes the variable
    return _putenv_s(name, "");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DiskSpaceInfo get_disk_space_os(FlString path) {
    DiskSpaceInfo result = { 0 };

    if (string_is_empty(path)) {
        return result;
    }

    arena_scratch_auto(temp);
    OsPath os_path = string_to_os_path(temp.arena, path);

    const wchar_t* orig_path = os_path_str(os_path);

    ULARGE_INTEGER free_bytes_available;
    ULARGE_INTEGER total_bytes;
    ULARGE_INTEGER total_free_bytes;

    size_t path_len = wcslen(orig_path);
    wchar_t* path_copy = arena_alloc_array(temp.arena, wchar_t, path_len + 1);
    wcscpy(path_copy, orig_path);

    // Walk up the directory tree until an existing path is found: the target file may not exist yet
    bool tried_drive_root = false;
    while (path_copy[0] != L'\0') {
        if (GetDiskFreeSpaceExW(path_copy, &free_bytes_available, &total_bytes, &total_free_bytes)) {
            result.bytes_available = free_bytes_available.QuadPart;
            result.bytes_total = total_bytes.QuadPart;
            result.bytes_free = total_free_bytes.QuadPart;
            result.is_valid = true;
            break;
        }

        wchar_t* last_sep = wcsrchr(path_copy, L'\\');
        if (!last_sep) {
            last_sep = wcsrchr(path_copy, L'/');
        }
        if (last_sep == nullptr) {
            break;
        }

        // At drive root (e.g., "C:\") - ensure trailing backslash and try one more time
        if (last_sep == path_copy + 2 && path_copy[1] == L':') {
            if (tried_drive_root) {
                break; // Already tried drive root, give up
            }
            tried_drive_root = true;
            path_copy[3] = L'\0';
        } else {
            *last_sep = L'\0';
        }
    }

    return result;
}

#endif // PLATFORM_WINDOWS