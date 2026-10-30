#include "utest/utest.h"
#include <core/arena.h>
#include <core/file_watcher.h>
#include <core/file_watcher_internal.h>
#include <core/os/os.h>
#include <core/path.h>
#include <core/sprintf.h>
#include <core/string.h>
#include <stdio.h>

#if !PLATFORM_WINDOWS
#include <unistd.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool create_temp_directory(const char* path) {
    FlString path_str = string_from_cstr(path);
    return file_create_directory_os(path_str);
}

static bool create_test_file(const char* path, const char* content) {
    FILE* file = fopen(path, "w");
    if (!file) {
        return false;
    }

    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, file);
    fclose(file);

    return written == len;
}

static void cleanup_path(const char* path) {
    remove(path);
}

static FlString get_test_temp_dir(FlArena* arena) {
    return get_temp_directory_os(arena);
}

static FlString create_unique_test_dir(FlArena* arena, const char* base_name) {
    FlString temp_dir = get_test_temp_dir(arena);
    u32 pid = get_process_id_os();
    FlString unique_name = fl_sprintf_arena(arena, "%s_%u", base_name, pid);
    return file_build_path_os(arena, temp_dir, unique_name);
}

static FlString build_temp_path_fw(FlArena* arena, const char* relative_path) {
    FlString temp_dir = get_temp_directory_os(arena);
    FlString rel_str;
    rel_str.data = relative_path;
    rel_str.length = strlen(relative_path);
    rel_str.is_static = 0;
    return fl_path_join(arena, temp_dir, rel_str);
}

static const char* build_temp_cstr_fw(FlArena* arena, const char* relative_path) {
    FlString path = build_temp_path_fw(arena, relative_path);
    return fl_string_to_cstr(arena, path);
}

static bool wait_for_file_watcher_changes(FlFileWatcherHandle handle, int max_polls, u32 poll_sleep_us) {
    for (int i = 0; i < max_polls; i++) {
        if (file_watcher_has_changes(handle)) {
            return true;
        }
        sleep_us(poll_sleep_us);
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(file_watcher, create_and_destroy) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString test_dir_path = create_unique_test_dir(arena, "file_watcher_test_create");

    bool created = file_create_directory_os(test_dir_path);
    ASSERT_TRUE(created);

    FlFileWatcherConfig config
        = { .recursive = false, .watch_files = true, .watch_directories = true, .max_changes = 100 };

    FlFileWatcherHandle handle = file_watcher_start(test_dir_path, config);
    ASSERT_NE(handle, FL_FILE_WATCHER_INVALID);
    ASSERT_TRUE(file_watcher_is_active(handle));

    file_watcher_stop(handle);
    ASSERT_FALSE(file_watcher_is_active(handle));

    file_remove_directory_os(test_dir_path);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(file_watcher, invalid_path) {
    FlFileWatcherConfig config
        = { .recursive = false, .watch_files = true, .watch_directories = true, .max_changes = 100 };

    FlFileWatcherHandle handle = file_watcher_start(FL_S("/nonexistent/path"), config);
    ASSERT_EQ(handle, FL_FILE_WATCHER_INVALID);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(file_watcher, detect_file_creation) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* test_dir = build_temp_cstr_fw(arena, "file_watcher_test_creation");
    FlString test_file_path = build_temp_path_fw(arena, "file_watcher_test_creation/test_file.txt");
    const char* test_file = fl_string_to_cstr(arena, test_file_path);

    // Clean up any leftover files from previous failed runs
    cleanup_path(test_file);
    cleanup_path(test_dir);

    create_temp_directory(test_dir);

    FlFileWatcherConfig config
        = { .recursive = false, .watch_files = true, .watch_directories = false, .max_changes = 10 };

    FlFileWatcherHandle handle = file_watcher_start(string_from_cstr(test_dir), config);
    ASSERT_NE(handle, FL_FILE_WATCHER_INVALID);

    ASSERT_TRUE(create_test_file(test_file, "test content"));

    // Poll until the change is detected (up to 2s, handles slow ARM CI hardware)
    ASSERT_TRUE(wait_for_file_watcher_changes(handle, 200, 10000));

    FileChangeIterator changes = file_watcher_get_changes(handle, arena);
    ASSERT_GT(changes.count, 0U);

    ASSERT_TRUE(file_change_iterator_has_next(&changes));
    FileChange change = file_change_iterator_next(&changes);
    ASSERT_TRUE((change.change_types & FlFileChangeType_Created) != 0);
    ASSERT_FALSE(change.is_directory);

    file_watcher_stop(handle);
    cleanup_path(test_file);
    cleanup_path(test_dir);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(file_watcher, detect_file_modification) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* test_dir = build_temp_cstr_fw(arena, "file_watcher_test_modification");
    FlString test_file_path = build_temp_path_fw(arena, "file_watcher_test_modification/test_file.txt");
    const char* test_file = fl_string_to_cstr(arena, test_file_path);

    create_temp_directory(test_dir);
    create_test_file(test_file, "initial content");

    FlFileWatcherConfig config
        = { .recursive = false, .watch_files = true, .watch_directories = false, .max_changes = 10 };

    FlFileWatcherHandle handle = file_watcher_start(string_from_cstr(test_dir), config);
    ASSERT_NE(handle, FL_FILE_WATCHER_INVALID);

    file_watcher_clear_changes(handle);

    ASSERT_TRUE(create_test_file(test_file, "modified content"));

    ASSERT_TRUE(wait_for_file_watcher_changes(handle, 200, 10000));

    FileChangeIterator changes = file_watcher_get_changes(handle, arena);
    ASSERT_GT(changes.count, 0U);

    file_change_iterator_reset(&changes);
    bool found_modify = false;
    while (file_change_iterator_has_next(&changes)) {
        FileChange change = file_change_iterator_next(&changes);
        if (change.change_types & FlFileChangeType_Modified) {
            found_modify = true;
            break;
        }
    }
    ASSERT_TRUE(found_modify);

    file_watcher_stop(handle);
    cleanup_path(test_file);
    cleanup_path(test_dir);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(file_watcher, clear_changes) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* test_dir = build_temp_cstr_fw(arena, "file_watcher_test_clear");
    FlString test_file_path = build_temp_path_fw(arena, "file_watcher_test_clear/test_file.txt");
    const char* test_file = fl_string_to_cstr(arena, test_file_path);

    create_temp_directory(test_dir);

    FlFileWatcherConfig config
        = { .recursive = false, .watch_files = true, .watch_directories = false, .max_changes = 10 };

    FlFileWatcherHandle handle = file_watcher_start(string_from_cstr(test_dir), config);
    ASSERT_NE(handle, FL_FILE_WATCHER_INVALID);

    create_test_file(test_file, "test");

    ASSERT_TRUE(wait_for_file_watcher_changes(handle, 200, 10000));

    file_watcher_clear_changes(handle);
    ASSERT_FALSE(file_watcher_has_changes(handle));

    file_watcher_stop(handle);
    cleanup_path(test_file);
    cleanup_path(test_dir);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(file_watcher, iterator_functions) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FileChangeIterator iter = { 0 };
    iter.count = 3;
    iter.current = 0;
    iter.changes = arena_alloc_array(arena, FileChange, 3);

    for (u32 i = 0; i < 3; i++) {
        iter.changes[i].path = FL_S("test_path");
        iter.changes[i].old_path = FL_S("");
        iter.changes[i].change_types = FlFileChangeType_Created;
        iter.changes[i].timestamp = i;
        iter.changes[i].is_directory = false;
    }

    ASSERT_TRUE(file_change_iterator_has_next(&iter));
    ASSERT_EQ(iter.current, 0U);

    FileChange change1 = file_change_iterator_next(&iter);
    ASSERT_EQ(change1.timestamp, 0ULL);
    ASSERT_EQ(iter.current, 1U);

    FileChange change2 = file_change_iterator_next(&iter);
    ASSERT_EQ(change2.timestamp, 1ULL);

    FileChange change3 = file_change_iterator_next(&iter);
    ASSERT_EQ(change3.timestamp, 2ULL);
    ASSERT_EQ(iter.current, 3U);

    ASSERT_FALSE(file_change_iterator_has_next(&iter));

    file_change_iterator_reset(&iter);
    ASSERT_EQ(iter.current, 0U);
    ASSERT_TRUE(file_change_iterator_has_next(&iter));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(file_watcher, detect_file_deletion) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* test_dir = build_temp_cstr_fw(arena, "file_watcher_test_deletion");
    FlString test_file_path = build_temp_path_fw(arena, "file_watcher_test_deletion/test_file.txt");
    const char* test_file = fl_string_to_cstr(arena, test_file_path);

    create_temp_directory(test_dir);
    create_test_file(test_file, "content to be deleted");

    FlFileWatcherConfig config
        = { .recursive = false, .watch_files = true, .watch_directories = false, .max_changes = 10 };

    FlFileWatcherHandle handle = file_watcher_start(string_from_cstr(test_dir), config);
    ASSERT_NE(handle, FL_FILE_WATCHER_INVALID);

    file_watcher_clear_changes(handle);

    cleanup_path(test_file);

    ASSERT_TRUE(wait_for_file_watcher_changes(handle, 200, 10000));

    FileChangeIterator changes = file_watcher_get_changes(handle, arena);
    ASSERT_GT(changes.count, 0U);

    file_change_iterator_reset(&changes);
    bool found_delete = false;
    while (file_change_iterator_has_next(&changes)) {
        FileChange change = file_change_iterator_next(&changes);
        if (change.change_types & FlFileChangeType_Deleted) {
            found_delete = true;
            ASSERT_FALSE(change.is_directory);
            break;
        }
    }
    ASSERT_TRUE(found_delete);

    file_watcher_stop(handle);
    cleanup_path(test_dir);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(file_watcher, detect_directory_changes) {
#if PLATFORM_WINDOWS
    // Windows ReadDirectoryChangesW with only FILE_NOTIFY_CHANGE_DIR_NAME does not reliably detect
    // directory creation. No production code watches directories without also watching files.
    UTEST_SKIP("Directory-only watching not supported on Windows");
#else
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* test_dir = build_temp_cstr_fw(arena, "file_watcher_test_dir_changes");
    FlString test_subdir_path = build_temp_path_fw(arena, "file_watcher_test_dir_changes/subdir");
    const char* test_subdir = fl_string_to_cstr(arena, test_subdir_path);

    create_temp_directory(test_dir);

    FlFileWatcherConfig config
        = { .recursive = false, .watch_files = false, .watch_directories = true, .max_changes = 10 };

    FlFileWatcherHandle handle = file_watcher_start(string_from_cstr(test_dir), config);
    ASSERT_NE(handle, FL_FILE_WATCHER_INVALID);

    file_watcher_clear_changes(handle);

    create_temp_directory(test_subdir);

    ASSERT_TRUE(wait_for_file_watcher_changes(handle, 200, 10000));

    FileChangeIterator changes = file_watcher_get_changes(handle, arena);
    ASSERT_GT(changes.count, 0U);

    file_change_iterator_reset(&changes);
    bool found_dir_create = false;
    while (file_change_iterator_has_next(&changes)) {
        FileChange change = file_change_iterator_next(&changes);
        if ((change.change_types & FlFileChangeType_Created) && change.is_directory) {
            found_dir_create = true;
            break;
        }
    }
    ASSERT_TRUE(found_dir_create);

    file_watcher_stop(handle);
    cleanup_path(test_subdir);
    cleanup_path(test_dir);
    fl_arena_destroy(arena);
#endif
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(file_watcher, multiple_file_operations) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* test_dir = build_temp_cstr_fw(arena, "file_watcher_test_multiple");
    FlString test_file1_path = build_temp_path_fw(arena, "file_watcher_test_multiple/file1.txt");
    FlString test_file2_path = build_temp_path_fw(arena, "file_watcher_test_multiple/file2.txt");
    FlString test_file3_path = build_temp_path_fw(arena, "file_watcher_test_multiple/file3.txt");
    const char* test_file1 = fl_string_to_cstr(arena, test_file1_path);
    const char* test_file2 = fl_string_to_cstr(arena, test_file2_path);
    const char* test_file3 = fl_string_to_cstr(arena, test_file3_path);

    create_temp_directory(test_dir);

    FlFileWatcherConfig config
        = { .recursive = false, .watch_files = true, .watch_directories = false, .max_changes = 20 };

    FlFileWatcherHandle handle = file_watcher_start(string_from_cstr(test_dir), config);
    ASSERT_NE(handle, FL_FILE_WATCHER_INVALID);

    file_watcher_clear_changes(handle);

    create_test_file(test_file1, "content 1");
    create_test_file(test_file2, "content 2");
    create_test_file(test_file3, "content 3");

    create_test_file(test_file1, "modified content 1");

    cleanup_path(test_file3);

    ASSERT_TRUE(wait_for_file_watcher_changes(handle, 200, 10000));

    FileChangeIterator changes = file_watcher_get_changes(handle, arena);
    ASSERT_GT(changes.count, 0U);

    u32 create_count = 0;
    u32 modify_count = 0;
    u32 delete_count = 0;

    file_change_iterator_reset(&changes);
    while (file_change_iterator_has_next(&changes)) {
        FileChange change = file_change_iterator_next(&changes);
        if (change.change_types & FlFileChangeType_Created)
            create_count++;
        if (change.change_types & FlFileChangeType_Modified)
            modify_count++;
        if (change.change_types & FlFileChangeType_Deleted)
            delete_count++;
    }

    // Exact counts depend on how the OS coalesces and reports events.
    ASSERT_GT(create_count, 0U);

    (void)modify_count;
    (void)delete_count;

    file_watcher_stop(handle);
    cleanup_path(test_file1);
    cleanup_path(test_file2);
    cleanup_path(test_dir);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(file_watcher, event_coalescing_rapid_changes) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* test_dir = build_temp_cstr_fw(arena, "file_watcher_test_coalescing");
    FlString test_file_path = build_temp_path_fw(arena, "file_watcher_test_coalescing/rapid_change.txt");
    const char* test_file = fl_string_to_cstr(arena, test_file_path);

    create_temp_directory(test_dir);

    FlFileWatcherConfig config
        = { .recursive = false, .watch_files = true, .watch_directories = false, .max_changes = 10 };

    FlFileWatcherHandle handle = file_watcher_start(string_from_cstr(test_dir), config);
    ASSERT_NE(handle, FL_FILE_WATCHER_INVALID);

    file_watcher_clear_changes(handle);

    // Rapid changes to the same file; the 5ms gaps stay well inside the coalescing window.
    create_test_file(test_file, "version 1");
    sleep_us(5000);
    create_test_file(test_file, "version 2");
    sleep_us(5000);
    create_test_file(test_file, "version 3");
    sleep_us(5000);
    create_test_file(test_file, "version 4");

    ASSERT_TRUE(wait_for_file_watcher_changes(handle, 200, 10000));

    FileChangeIterator changes = file_watcher_get_changes(handle, arena);

    // 8 = 4 creates + 3 modifies, the count if nothing coalesced.
    ASSERT_GT(changes.count, 0U);
    ASSERT_LE(changes.count, 8U);

    file_change_iterator_reset(&changes);
    bool found_coalesced = false;

    while (file_change_iterator_has_next(&changes)) {
        FileChange change = file_change_iterator_next(&changes);
        u32 type_count = 0;
        if (change.change_types & FlFileChangeType_Created)
            type_count++;
        if (change.change_types & FlFileChangeType_Modified)
            type_count++;
        if (change.change_types & FlFileChangeType_Deleted)
            type_count++;
        if (change.change_types & FlFileChangeType_Moved)
            type_count++;
        if (change.change_types & FlFileChangeType_Renamed)
            type_count++;

        if (type_count > 1) {
            found_coalesced = true;
        }
    }

    ASSERT_TRUE(found_coalesced || changes.count <= 2);

    file_watcher_stop(handle);
    cleanup_path(test_file);
    cleanup_path(test_dir);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(file_watcher, buffer_overflow_handling) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* test_dir = build_temp_cstr_fw(arena, "file_watcher_test_overflow");

    create_temp_directory(test_dir);

    // Use very small buffer to trigger overflow
    FlFileWatcherConfig config
        = { .recursive = false, .watch_files = true, .watch_directories = false, .max_changes = 2 };

    FlFileWatcherHandle handle = file_watcher_start(string_from_cstr(test_dir), config);
    ASSERT_NE(handle, FL_FILE_WATCHER_INVALID);

    file_watcher_clear_changes(handle);

    for (int i = 0; i < 5; i++) {
        char filename[512];
        FlString file_path = build_temp_path_fw(arena, "file_watcher_test_overflow");
        const char* base_dir = fl_string_to_cstr(arena, file_path);
        snprintf(filename, sizeof(filename), "%s/file%d.txt", base_dir, i);
        create_test_file(filename, "content");
        sleep_us(50000); // 50ms between files
    }

    ASSERT_TRUE(wait_for_file_watcher_changes(handle, 200, 10000));

    FileChangeIterator changes = file_watcher_get_changes(handle, arena);

    ASSERT_LE(changes.count, 2U);
    ASSERT_TRUE(changes.has_overflow);

    file_watcher_stop(handle);

    for (int i = 0; i < 5; i++) {
        char filename[512];
        FlString file_path = build_temp_path_fw(arena, "file_watcher_test_overflow");
        const char* base_dir = fl_string_to_cstr(arena, file_path);
        snprintf(filename, sizeof(filename), "%s/file%d.txt", base_dir, i);
        cleanup_path(filename);
    }
    cleanup_path(test_dir);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(file_watcher, invalid_handle_operations) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlFileWatcherHandle invalid_handle = FL_FILE_WATCHER_INVALID;

    ASSERT_FALSE(file_watcher_is_active(invalid_handle));
    ASSERT_FALSE(file_watcher_has_changes(invalid_handle));

    FileChangeIterator changes = file_watcher_get_changes(invalid_handle, arena);
    ASSERT_EQ(changes.count, 0U);
    ASSERT_FALSE(changes.has_overflow);

    // These should not crash
    file_watcher_clear_changes(invalid_handle);
    file_watcher_stop(invalid_handle);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(file_watcher, watcher_cleanup_on_path_deletion) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* test_dir = build_temp_cstr_fw(arena, "file_watcher_test_path_deletion");

    create_temp_directory(test_dir);

    FlFileWatcherConfig config
        = { .recursive = false, .watch_files = true, .watch_directories = true, .max_changes = 10 };

    FlFileWatcherHandle handle = file_watcher_start(string_from_cstr(test_dir), config);
    ASSERT_NE(handle, FL_FILE_WATCHER_INVALID);
    ASSERT_TRUE(file_watcher_is_active(handle));

    cleanup_path(test_dir);

    wait_for_file_watcher_changes(handle, 200, 10000);

    // The watcher may still report active; operations must handle the missing path without crashing.
    FileChangeIterator changes = file_watcher_get_changes(handle, arena);
    (void)changes;

    file_watcher_stop(handle);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(file_watcher, config_variations) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* test_dir = build_temp_cstr_fw(arena, "file_watcher_test_config");

    create_temp_directory(test_dir);

    FlFileWatcherConfig files_only
        = { .recursive = false, .watch_files = true, .watch_directories = false, .max_changes = 10 };

    FlFileWatcherHandle handle1 = file_watcher_start(string_from_cstr(test_dir), files_only);
    ASSERT_NE(handle1, FL_FILE_WATCHER_INVALID);

    FlFileWatcherConfig dirs_only
        = { .recursive = false, .watch_files = false, .watch_directories = true, .max_changes = 10 };

    FlFileWatcherHandle handle2 = file_watcher_start(string_from_cstr(test_dir), dirs_only);
    ASSERT_NE(handle2, FL_FILE_WATCHER_INVALID);

    FlFileWatcherConfig default_buffer = {
        .recursive = false,
        .watch_files = true,
        .watch_directories = true,
        .max_changes = 0 // Should use default
    };

    FlFileWatcherHandle handle3 = file_watcher_start(string_from_cstr(test_dir), default_buffer);
    ASSERT_NE(handle3, FL_FILE_WATCHER_INVALID);

    ASSERT_TRUE(file_watcher_is_active(handle1));
    ASSERT_TRUE(file_watcher_is_active(handle2));
    ASSERT_TRUE(file_watcher_is_active(handle3));

    file_watcher_stop(handle1);
    file_watcher_stop(handle2);
    file_watcher_stop(handle3);

    cleanup_path(test_dir);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The Linux backend maps each watched directory's inotify descriptor back to its path through a
// fixed-capacity table. A directory that does not fit cannot be attributed, so the watcher refuses
// to start rather than silently watching part of the tree. These cover both sides of that cap.
//
// Linux-only: the macOS (FSEvents) and Windows (ReadDirectoryChangesW) backends watch a tree
// without a per-directory descriptor table, so they have no equivalent limit.

#if PLATFORM_LINUX

#define FW_WATCH_TABLE_CAPACITY FILE_WATCHER_MAX_RECURSIVE_DIRECTORIES

// Creates `count` sibling subdirectories under `root`, which must already exist.
static bool create_subdirectories(FlArena* arena, FlString root, u32 count) {
    for (u32 i = 0; i < count; i++) {
        FlString name = fl_sprintf_arena(arena, "sub_%u", i);
        if (!file_create_directory_os(fl_path_join(arena, root, name))) {
            return false;
        }
    }
    return true;
}

static void remove_subdirectories(FlArena* arena, FlString root, u32 count) {
    for (u32 i = 0; i < count; i++) {
        FlString name = fl_sprintf_arena(arena, "sub_%u", i);
        file_remove_directory_os(fl_path_join(arena, root, name));
    }
    file_remove_directory_os(root);
}

static u32 count_open_descriptors(void) {
    FlArena* arena = arena_new(.reserved_size = MB(1));
    u32 count = 0;
    DirHandle dir = dir_open_os(arena, FL_S("/proc/self/fd"));
    if (dir.is_valid) {
        FileStat entry;
        while ((entry = dir_read_next_os(arena, &dir)).is_valid) {
            count++;
        }
        dir_close_os(&dir);
    }
    fl_arena_destroy(arena);
    return count;
}

UTEST(file_watcher, recursive_watch_at_capacity_starts) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString root = create_unique_test_dir(arena, "file_watcher_test_at_capacity");
    ASSERT_TRUE(file_create_directory_os(root));

    // The root takes one slot, so the tree exactly fills the table.
    const u32 subdir_count = FW_WATCH_TABLE_CAPACITY - 1;
    ASSERT_TRUE(create_subdirectories(arena, root, subdir_count));

    FlFileWatcherConfig config
        = { .recursive = true, .watch_files = true, .watch_directories = true, .max_changes = 100 };

    FlFileWatcherHandle handle = file_watcher_start(root, config);
    ASSERT_NE(handle, FL_FILE_WATCHER_INVALID);
    ASSERT_TRUE(file_watcher_is_active(handle));

    // A tree that fits is watched for real, not merely accepted: a change in the last subdirectory
    // still has a mapped descriptor behind it.
    file_watcher_clear_changes(handle);

    FlString last_dir = fl_path_join(arena, root, fl_sprintf_arena(arena, "sub_%u", subdir_count - 1));
    FlString probe = fl_path_join(arena, last_dir, FL_S("probe.txt"));
    ASSERT_TRUE(create_test_file(fl_string_to_cstr(arena, probe), "at capacity"));

    ASSERT_TRUE(wait_for_file_watcher_changes(handle, 200, 10000));

    file_watcher_stop(handle);

    cleanup_path(fl_string_to_cstr(arena, probe));
    remove_subdirectories(arena, root, subdir_count);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(file_watcher, recursive_watch_over_capacity_fails) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString root = create_unique_test_dir(arena, "file_watcher_test_over_capacity");
    ASSERT_TRUE(file_create_directory_os(root));

    // Comfortably past the cap, so the walk cannot finish whatever order it reads directories in.
    const u32 subdir_count = FW_WATCH_TABLE_CAPACITY + 8;
    ASSERT_TRUE(create_subdirectories(arena, root, subdir_count));

    FlFileWatcherConfig config
        = { .recursive = true, .watch_files = true, .watch_directories = true, .max_changes = 100 };

    // The whole watcher fails. It must not come back as a live handle that watches the directories
    // that fit and silently ignores the rest.
    FlFileWatcherHandle handle = file_watcher_start(root, config);
    ASSERT_EQ(handle, FL_FILE_WATCHER_INVALID);
    ASSERT_FALSE(file_watcher_is_active(handle));

    // Non-recursive is unaffected: it only ever needs the one slot for the root.
    FlFileWatcherConfig shallow
        = { .recursive = false, .watch_files = true, .watch_directories = true, .max_changes = 100 };

    FlFileWatcherHandle shallow_handle = file_watcher_start(root, shallow);
    ASSERT_NE(shallow_handle, FL_FILE_WATCHER_INVALID);
    file_watcher_stop(shallow_handle);

    remove_subdirectories(arena, root, subdir_count);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(file_watcher, recursive_watch_over_capacity_leaves_no_descriptors) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString root = create_unique_test_dir(arena, "file_watcher_test_over_capacity_fds");
    ASSERT_TRUE(file_create_directory_os(root));

    const u32 subdir_count = FW_WATCH_TABLE_CAPACITY + 8;
    ASSERT_TRUE(create_subdirectories(arena, root, subdir_count));

    FlFileWatcherConfig config
        = { .recursive = true, .watch_files = true, .watch_directories = true, .max_changes = 100 };

    // One failed start first, so any one-off allocation the first call makes is already accounted
    // for and does not read as a leak below.
    ASSERT_EQ(file_watcher_start(root, config), FL_FILE_WATCHER_INVALID);

    const u32 before = count_open_descriptors();

    for (u32 i = 0; i < 8; i++) {
        ASSERT_EQ(file_watcher_start(root, config), FL_FILE_WATCHER_INVALID);
    }

    // Each rejected start must close its inotify instance. A watcher that kept the descriptor, or
    // the kernel watches it had already added, would show up here.
    ASSERT_EQ(count_open_descriptors(), before);

    remove_subdirectories(arena, root, subdir_count);
    fl_arena_destroy(arena);
}

#endif // PLATFORM_LINUX

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(file_watcher, multiple_watchers_same_path) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* test_dir = build_temp_cstr_fw(arena, "file_watcher_test_multiple_watchers");
    FlString test_file_path = build_temp_path_fw(arena, "file_watcher_test_multiple_watchers/shared_file.txt");
    const char* test_file = fl_string_to_cstr(arena, test_file_path);

    create_temp_directory(test_dir);

    FlFileWatcherConfig config
        = { .recursive = false, .watch_files = true, .watch_directories = false, .max_changes = 10 };

    FlFileWatcherHandle handle1 = file_watcher_start(string_from_cstr(test_dir), config);
    FlFileWatcherHandle handle2 = file_watcher_start(string_from_cstr(test_dir), config);

    ASSERT_NE(handle1, FL_FILE_WATCHER_INVALID);
    ASSERT_NE(handle2, FL_FILE_WATCHER_INVALID);
    ASSERT_NE(handle1, handle2);

    ASSERT_TRUE(file_watcher_is_active(handle1));
    ASSERT_TRUE(file_watcher_is_active(handle2));

    file_watcher_clear_changes(handle1);
    file_watcher_clear_changes(handle2);

    create_test_file(test_file, "shared content");

    ASSERT_TRUE(wait_for_file_watcher_changes(handle1, 200, 10000));
    ASSERT_TRUE(wait_for_file_watcher_changes(handle2, 200, 10000));

    FileChangeIterator changes1 = file_watcher_get_changes(handle1, arena);
    FileChangeIterator changes2 = file_watcher_get_changes(handle2, arena);

    ASSERT_GT(changes1.count, 0U);
    ASSERT_GT(changes2.count, 0U);

    file_watcher_stop(handle1);
    file_watcher_stop(handle2);

    cleanup_path(test_file);
    cleanup_path(test_dir);
    fl_arena_destroy(arena);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// The registry is process-wide, so starting, polling and stopping distinct watchers from several threads all walk the
// same watcher array. Four workers churn their own watcher while the main thread keeps polling one of its own, which
// is the shape of a host that starts and stops watchers on job threads while the main thread polls.

#define FW_CONCURRENT_THREADS 4

// Below the registry's 16 slots even with the main thread's watcher live, so a round never fails for want of one.
#define FW_CONCURRENT_ROUNDS 8

typedef struct FwConcurrentWorker {
    const char* dir;
    int id;
    bool ok;
} FwConcurrentWorker;

static _Atomic u32 s_fw_concurrent_ready;
static _Atomic u32 s_fw_concurrent_running;

static void* fw_concurrent_worker(void* user) {
    FwConcurrentWorker* w = (FwConcurrentWorker*)user;
    FlArena* arena = arena_new(.reserved_size = MB(4));
    FlFileWatcherConfig config
        = { .recursive = false, .watch_files = true, .watch_directories = false, .max_changes = 16 };

    // Every worker is inside the registry at the same time, not one after another.
    atomic_fetch_add(&s_fw_concurrent_ready, 1);
    while (atomic_load(&s_fw_concurrent_ready) < FW_CONCURRENT_THREADS) {
        sleep_us(100);
    }

    for (int round = 0; round < FW_CONCURRENT_ROUNDS; ++round) {
        FlFileWatcherHandle handle = file_watcher_start(string_from_cstr(w->dir), config);
        if (handle == FL_FILE_WATCHER_INVALID || !file_watcher_is_active(handle)) {
            w->ok = false;
            atomic_fetch_sub(&s_fw_concurrent_running, 1);
            fl_arena_destroy(arena);
            return nullptr;
        }

        FlString file_path = fl_sprintf_arena(arena, "%s/file_%d", w->dir, round);
        create_test_file(fl_string_to_cstr(arena, file_path), "payload");

        // The change may or may not have landed yet; either way the poll walks the array.
        if (file_watcher_has_changes(handle)) {
            file_watcher_clear_changes(handle);
        }

        file_watcher_stop(handle);
        cleanup_path(fl_string_to_cstr(arena, file_path));
    }

    w->ok = true;
    atomic_fetch_sub(&s_fw_concurrent_running, 1);
    fl_arena_destroy(arena);
    return nullptr;
}

UTEST(file_watcher, concurrent_start_poll_stop) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* root = build_temp_cstr_fw(arena, "file_watcher_test_concurrent");
    create_temp_directory(root);

    FlFileWatcherConfig config
        = { .recursive = false, .watch_files = true, .watch_directories = false, .max_changes = 16 };

    // The main thread's own watcher, polled throughout.
    FlFileWatcherHandle poller = file_watcher_start(string_from_cstr(root), config);
    ASSERT_NE(poller, FL_FILE_WATCHER_INVALID);

    atomic_store(&s_fw_concurrent_ready, 0);
    atomic_store(&s_fw_concurrent_running, FW_CONCURRENT_THREADS);

    FwConcurrentWorker workers[FW_CONCURRENT_THREADS];
    Thread threads[FW_CONCURRENT_THREADS];
    for (int t = 0; t < FW_CONCURRENT_THREADS; ++t) {
        FlString dir = fl_sprintf_arena(arena, "%s/%d", root, t);
        const char* dir_cstr = fl_string_to_cstr(arena, dir);
        ASSERT_TRUE(create_temp_directory(dir_cstr));

        workers[t] = (FwConcurrentWorker) { .dir = dir_cstr, .id = t, .ok = false };
        ASSERT_TRUE(os_thread_create(&threads[t], fw_concurrent_worker, &workers[t]));
    }

    while (atomic_load(&s_fw_concurrent_running) > 0) {
        ASSERT_TRUE_MSG(file_watcher_is_active(poller), "the main thread's watcher went missing under the churn");
        if (file_watcher_has_changes(poller)) {
            file_watcher_clear_changes(poller);
        }
        sleep_us(100);
    }

    for (int t = 0; t < FW_CONCURRENT_THREADS; ++t) {
        os_thread_join(&threads[t]);
        ASSERT_TRUE_MSG(workers[t].ok, "a worker failed to start or activate a watcher of its own");
    }

    ASSERT_TRUE(file_watcher_is_active(poller));
    file_watcher_stop(poller);
    ASSERT_FALSE(file_watcher_is_active(poller));

    for (int t = 0; t < FW_CONCURRENT_THREADS; ++t) {
        FlString dir = fl_sprintf_arena(arena, "%s/%d", root, t);
        cleanup_path(fl_string_to_cstr(arena, dir));
    }
    cleanup_path(root);

    fl_arena_destroy(arena);
}
