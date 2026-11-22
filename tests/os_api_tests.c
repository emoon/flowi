#include "utest/utest.h"
#include <core/arena.h>
#include <core/os/os.h>
#include <core/sprintf.h>
#include <core/string.h>
#include <stdio.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

static FlArena* test_arena(void) {
    return arena_new(.reserved_size = MB(64));
}

static FlString create_test_name(FlArena* arena, const char* base_name) {
    u32 pid = get_process_id_os();
    return fl_sprintf_arena(arena, "%s_%u", base_name, pid);
}

static FlString create_test_file_path(FlArena* arena, const char* test_name) {
    FlString temp_dir = get_temp_directory_os(arena);
    FlString unique_name = create_test_name(arena, test_name);
    return file_build_path_os(arena, temp_dir, unique_name);
}

static FlString create_test_dir_path(FlArena* arena, const char* test_name) {
    FlString temp_dir = get_temp_directory_os(arena);
    FlString unique_name = create_test_name(arena, test_name);
    return file_build_path_os(arena, temp_dir, unique_name);
}

static FlString invalid_os_path(void) {
#ifdef _WIN32
    return FL_S("Z:\\non\\existent\\directory\\file.txt");
#else
    return FL_S("/non/existent/directory/file.txt");
#endif
}

static i64 write_text(FlString path, FlString content) {
    return file_write_os(path, (const u8*)content.data, (i64)content.length);
}

static void expect_write_bytes(int* utest_result, FlString path, const u8* data, i64 size) {
    ASSERT_EQ(file_write_os(path, data, size), size);
}

static void expect_write_text(int* utest_result, FlString path, FlString content) {
    expect_write_bytes(utest_result, path, (const u8*)content.data, (i64)content.length);
}

static void expect_dirs_exist(int* utest_result, const FlString* paths, u32 count, bool want) {
    for (u32 i = 0; i < count; i++) {
        ASSERT_EQ(file_directory_exists_os(paths[i]), want);
    }
}

static void expect_files_exist(int* utest_result, const FlString* paths, u32 count, bool want) {
    for (u32 i = 0; i < count; i++) {
        ASSERT_EQ(file_exists_os(paths[i]), want);
    }
}

static void expect_file(int* utest_result, FlString path, bool want) {
    ASSERT_EQ(file_exists_os(path), want);
}

static void expect_dir(int* utest_result, FlString path, bool want) {
    ASSERT_EQ(file_directory_exists_os(path), want);
}

static void expect_disk_space_bounds(int* utest_result, DiskSpaceInfo info) {
    ASSERT_LE(info.bytes_available, info.bytes_free);
    ASSERT_LE(info.bytes_free, info.bytes_total);
    ASSERT_LE(info.bytes_available, info.bytes_total);
}

static void expect_disk_space_empty(int* utest_result, DiskSpaceInfo info) {
    ASSERT_FALSE(info.is_valid);
    ASSERT_EQ(info.bytes_total, 0ULL);
    ASSERT_EQ(info.bytes_free, 0ULL);
    ASSERT_EQ(info.bytes_available, 0ULL);
}

static void expect_dir_created(int* utest_result, FlString path) {
    ASSERT_TRUE(file_create_directory_os(path));
    ASSERT_TRUE(file_directory_exists_os(path));
}

static void expect_dir_created_recursive(int* utest_result, FlString path) {
    ASSERT_TRUE(file_create_directory_recursive_os(path));
    ASSERT_TRUE(file_directory_exists_os(path));
}

static void expect_handle_write(int* utest_result, FlString path, FlString content, bool append) {
    i64 fd = file_open_write_os(path, append);
    ASSERT_GE(fd, 0);
    file_write_to_handle_os(fd, (const u8*)content.data, (i64)content.length);
    file_close_handle_os(fd);
}

static void expect_disk_space_valid(int* utest_result, DiskSpaceInfo info) {
    ASSERT_TRUE(info.is_valid);
    ASSERT_GT(info.bytes_total, 0ULL);
}

UTEST(os_api, get_temp_directory_basic) {
    FlArena* arena = test_arena();

    FlString temp_dir = get_temp_directory_os(arena);

    ASSERT_GT((u64)temp_dir.length, 0ULL);
    ASSERT_NE(temp_dir.data, nullptr);

    ASSERT_TRUE(temp_dir.data[0] != '\0');

    fl_arena_destroy(arena);
}

UTEST(os_api, get_temp_directory_consistency) {
    FlArena* arena = test_arena();

    FlString temp_dir1 = get_temp_directory_os(arena);
    FlString temp_dir2 = get_temp_directory_os(arena);

    ASSERT_TRUE(fl_string_equals(temp_dir1, temp_dir2));

    fl_arena_destroy(arena);
}

UTEST(os_api, get_process_id_basic) {
    u32 pid = get_process_id_os();

    ASSERT_GT(pid, 0U);
}

UTEST(os_api, get_process_id_consistency) {
    u32 pid1 = get_process_id_os();
    u32 pid2 = get_process_id_os();

    ASSERT_EQ(pid1, pid2);
}

UTEST(os_api, file_write_basic) {
    FlArena* arena = test_arena();

    FlString file_path = create_test_file_path(arena, "file_write_basic");
    FlString test_content = FL_S("Hello, OS API test!");

    expect_write_text(utest_result, file_path, test_content);

    expect_file(utest_result, file_path, true);

    file_delete_os(file_path);
    expect_file(utest_result, file_path, false);

    fl_arena_destroy(arena);
}

UTEST(os_api, file_write_empty) {
    FlArena* arena = test_arena();

    FlString file_path = create_test_file_path(arena, "file_write_empty");

    i64 bytes_written = file_write_os(file_path, nullptr, 0);
    ASSERT_EQ(bytes_written, 0);

    expect_file(utest_result, file_path, true);

    file_delete_os(file_path);

    fl_arena_destroy(arena);
}

UTEST(os_api, file_write_large_content) {
    FlArena* arena = test_arena();

    FlString file_path = create_test_file_path(arena, "file_write_large");

    const u64 content_size = 4096;
    u8* large_content = arena_alloc_array(arena, u8, content_size);
    for (u64 i = 0; i < content_size; i++) {
        large_content[i] = (u8)(i % 256);
    }

    expect_write_bytes(utest_result, file_path, large_content, (i64)content_size);

    expect_file(utest_result, file_path, true);

    file_delete_os(file_path);

    fl_arena_destroy(arena);
}

UTEST(os_api, file_write_overwrite) {
    FlArena* arena = test_arena();

    FlString file_path = create_test_file_path(arena, "file_write_overwrite");
    FlString content1 = FL_S("First content");
    FlString content2 = FL_S("Second content");

    expect_write_text(utest_result, file_path, content1);
    expect_file(utest_result, file_path, true);

    expect_write_text(utest_result, file_path, content2);
    expect_file(utest_result, file_path, true);

    file_delete_os(file_path);

    fl_arena_destroy(arena);
}

UTEST(os_api, directory_create_and_remove) {
    FlArena* arena = test_arena();

    FlString dir_path = create_test_dir_path(arena, "dir_create_remove");

    expect_dir(utest_result, dir_path, false);

    expect_dir_created(utest_result, dir_path);

    ASSERT_TRUE(file_remove_directory_os(dir_path));
    expect_dir(utest_result, dir_path, false);

    fl_arena_destroy(arena);
}

UTEST(os_api, directory_create_existing) {
    FlArena* arena = test_arena();

    FlString dir_path = create_test_dir_path(arena, "dir_create_existing");

    expect_dir_created(utest_result, dir_path);

    // Platform behavior varies on an already-existing directory, but must not crash.
    file_create_directory_os(dir_path);
    expect_dir(utest_result, dir_path, true);

    file_remove_directory_os(dir_path);

    fl_arena_destroy(arena);
}

UTEST(os_api, directory_recursive_remove_with_files) {
    FlArena* arena = test_arena();

    FlString base_dir = create_test_dir_path(arena, "dir_recursive");
    FlString sub_dir = file_build_path_os(arena, base_dir, FL_S("subdir"));
    FlString file1 = file_build_path_os(arena, base_dir, FL_S("file1.txt"));
    FlString file2 = file_build_path_os(arena, sub_dir, FL_S("file2.txt"));
    FlString test_content = FL_S("Test file content");
    const FlString dirs[] = { base_dir, sub_dir };
    const FlString files[] = { file1, file2 };

    ASSERT_TRUE(file_create_directory_os(base_dir));
    ASSERT_TRUE(file_create_directory_os(sub_dir));

    expect_write_text(utest_result, file1, test_content);
    expect_write_text(utest_result, file2, test_content);

    expect_dirs_exist(utest_result, dirs, 2, true);
    expect_files_exist(utest_result, files, 2, true);

    ASSERT_TRUE(file_remove_directory_recursive_os(base_dir));

    expect_dirs_exist(utest_result, dirs, 2, false);
    expect_files_exist(utest_result, files, 2, false);

    fl_arena_destroy(arena);
}

UTEST(os_api, directory_recursive_remove_nested) {
    FlArena* arena = test_arena();

    FlString base_dir = create_test_dir_path(arena, "dir_nested");
    FlString level1 = file_build_path_os(arena, base_dir, FL_S("level1"));
    FlString level2 = file_build_path_os(arena, level1, FL_S("level2"));
    FlString level3 = file_build_path_os(arena, level2, FL_S("level3"));
    const FlString levels[] = { base_dir, level1, level2, level3 };

    for (u32 i = 0; i < 4; i++) {
        ASSERT_TRUE(file_create_directory_os(levels[i]));
    }

    expect_dirs_exist(utest_result, levels, 4, true);

    ASSERT_TRUE(file_remove_directory_recursive_os(base_dir));

    expect_dirs_exist(utest_result, levels, 4, false);

    fl_arena_destroy(arena);
}

UTEST(os_api, directory_create_recursive_nested) {
    FlArena* arena = test_arena();

    FlString base_dir = create_test_dir_path(arena, "recursive_create");
    FlString level1 = file_build_path_os(arena, base_dir, FL_S("level1"));
    FlString level2 = file_build_path_os(arena, level1, FL_S("level2"));
    FlString level3 = file_build_path_os(arena, level2, FL_S("level3"));
    const FlString levels[] = { base_dir, level1, level2, level3 };

    expect_dir(utest_result, base_dir, false);

    ASSERT_TRUE(file_create_directory_recursive_os(level3));

    expect_dirs_exist(utest_result, levels, 4, true);

    file_remove_directory_recursive_os(base_dir);

    fl_arena_destroy(arena);
}

UTEST(os_api, directory_create_recursive_existing) {
    FlArena* arena = test_arena();

    FlString dir_path = create_test_dir_path(arena, "recursive_existing");

    expect_dir_created_recursive(utest_result, dir_path);
    expect_dir_created_recursive(utest_result, dir_path);

    file_remove_directory_os(dir_path);

    fl_arena_destroy(arena);
}

UTEST(os_api, directory_create_recursive_empty_path) {
    ASSERT_FALSE(file_create_directory_recursive_os(string_empty()));
}

UTEST(os_api, directory_create_recursive_single_level) {
    FlArena* arena = test_arena();

    FlString dir_path = create_test_dir_path(arena, "recursive_single");

    expect_dir(utest_result, dir_path, false);

    expect_dir_created_recursive(utest_result, dir_path);

    file_remove_directory_os(dir_path);

    fl_arena_destroy(arena);
}

UTEST(os_api, full_workflow_integration) {
    FlArena* arena = test_arena();

    FlString base_dir = create_test_dir_path(arena, "integration_test");
    FlString sub_dir = file_build_path_os(arena, base_dir, FL_S("data"));
    FlString config_file = file_build_path_os(arena, base_dir, FL_S("config.txt"));
    FlString data_file = file_build_path_os(arena, sub_dir, FL_S("data.bin"));
    const FlString dirs[] = { base_dir, sub_dir };
    const FlString files[] = { config_file, data_file };

    FlString config_content = FL_S("# Test configuration file\nversion=1.0\ndebug=true\n");

    const u64 data_size = 256;
    u8* binary_data = arena_alloc_array(arena, u8, data_size);
    for (u64 i = 0; i < data_size; i++) {
        binary_data[i] = (u8)(i ^ 0xAA);
    }

    ASSERT_TRUE(file_create_directory_os(base_dir));
    ASSERT_TRUE(file_create_directory_os(sub_dir));

    expect_write_text(utest_result, config_file, config_content);
    expect_write_bytes(utest_result, data_file, binary_data, (i64)data_size);

    expect_dirs_exist(utest_result, dirs, 2, true);
    expect_files_exist(utest_result, files, 2, true);

    ASSERT_TRUE(file_remove_directory_recursive_os(base_dir));

    expect_dirs_exist(utest_result, dirs, 2, false);
    expect_files_exist(utest_result, files, 2, false);

    fl_arena_destroy(arena);
}

UTEST(os_api, cross_platform_path_handling) {
    FlArena* arena = test_arena();

    FlString temp_dir = get_temp_directory_os(arena);
    FlString test_name = create_test_name(arena, "path_test");
    FlString test_path = file_build_path_os(arena, temp_dir, test_name);

    expect_dir_created(utest_result, test_path);

    FlString file_name = FL_S("test_file.txt");
    FlString file_path = file_build_path_os(arena, test_path, file_name);
    FlString content = FL_S("Cross-platform path test");

    expect_write_text(utest_result, file_path, content);
    expect_file(utest_result, file_path, true);

    ASSERT_TRUE(file_remove_directory_recursive_os(test_path));
    expect_dir(utest_result, test_path, false);

    fl_arena_destroy(arena);
}

UTEST(os_api, file_write_invalid_path) {
    FlArena* arena = test_arena();

    FlString invalid_path = invalid_os_path();
    FlString content = FL_S("Should fail");

    i64 result = write_text(invalid_path, content);
    ASSERT_LT(result, (i64)content.length);

    fl_arena_destroy(arena);
}

UTEST(os_api, directory_remove_nonexistent) {
    FlArena* arena = test_arena();

    FlString nonexistent_dir = create_test_dir_path(arena, "nonexistent_dir_12345");

    ASSERT_FALSE(file_remove_directory_os(nonexistent_dir));
    ASSERT_FALSE(file_remove_directory_recursive_os(nonexistent_dir));

    fl_arena_destroy(arena);
}

UTEST(os_api, get_disk_space_temp_directory) {
    FlArena* arena = test_arena();

    FlString temp_dir = get_temp_directory_os(arena);
    DiskSpaceInfo info = get_disk_space_os(temp_dir);

    expect_disk_space_valid(utest_result, info);
    ASSERT_GT(info.bytes_free, 0ULL);
    // bytes_available excludes blocks reserved for root/quota, so it never exceeds bytes_free.
    expect_disk_space_bounds(utest_result, info);

    fl_arena_destroy(arena);
}

UTEST(os_api, get_disk_space_current_directory) {
    FlArena* arena = test_arena();

    FlString current_dir = get_current_directory_os(arena);
    DiskSpaceInfo info = get_disk_space_os(current_dir);

    expect_disk_space_valid(utest_result, info);

    fl_arena_destroy(arena);
}

UTEST(os_api, get_disk_space_file_path) {
    FlArena* arena = test_arena();

    FlString file_path = create_test_file_path(arena, "disk_space_file_test");
    FlString content = FL_S("Test content for disk space check");

    expect_write_text(utest_result, file_path, content);

    // A file path reports the filesystem containing it.
    DiskSpaceInfo info = get_disk_space_os(file_path);
    expect_disk_space_valid(utest_result, info);

    file_delete_os(file_path);

    fl_arena_destroy(arena);
}

UTEST(os_api, get_disk_space_nonexistent_file_with_valid_parent) {
    FlArena* arena = test_arena();
    FlString nonexistent_file;

    // A nonexistent path resolves by walking up to the first parent that exists.
#ifdef _WIN32
    // Use the cwd's drive letter, because CI runners may not run on C:.
    FlString cwd = get_current_directory_os(arena);
    char drive_path[64];
    snprintf(drive_path, sizeof(drive_path), "%c:\\this\\path\\does\\not\\exist", cwd.data[0]);
    nonexistent_file = string_from_cstr(drive_path);
#else
    nonexistent_file = FL_S("/this/path/does/not/exist/at/all");
#endif
    DiskSpaceInfo info = get_disk_space_os(nonexistent_file);

    expect_disk_space_valid(utest_result, info);

    fl_arena_destroy(arena);
}

UTEST(os_api, get_disk_space_empty_path) {
    FlString empty_path = FL_S("");
    DiskSpaceInfo info = get_disk_space_os(empty_path);

    expect_disk_space_empty(utest_result, info);
}

UTEST(os_api, get_disk_space_consistency) {
    FlArena* arena = test_arena();

    FlString temp_dir = get_temp_directory_os(arena);

    DiskSpaceInfo info1 = get_disk_space_os(temp_dir);
    DiskSpaceInfo info2 = get_disk_space_os(temp_dir);

    ASSERT_TRUE(info1.is_valid);
    ASSERT_TRUE(info2.is_valid);

    // Filesystem size does not change between the two calls.
    ASSERT_EQ(info1.bytes_total, info2.bytes_total);

    // Free space drifts with system activity: tolerate 1% of total, or 10MB, whichever is larger.
    u64 tolerance = info1.bytes_total / 100;
    if (tolerance < 10 * 1024 * 1024) {
        tolerance = 10 * 1024 * 1024;
    }

    i64 free_diff = (i64)info1.bytes_free - (i64)info2.bytes_free;
    if (free_diff < 0)
        free_diff = -free_diff;
    ASSERT_LT((u64)free_diff, tolerance);

    fl_arena_destroy(arena);
}

UTEST(os_api, file_open_write_basic) {
    FlArena* arena = test_arena();

    FlString file_path = create_test_file_path(arena, "open_write_basic");
    FlString test_content = FL_S("Hello, file handle test!");

    i64 fd = file_open_write_os(file_path, false);
    ASSERT_GE(fd, 0);

    i64 bytes_written = file_write_to_handle_os(fd, (const u8*)test_content.data, (i64)test_content.length);
    ASSERT_EQ(bytes_written, (i64)test_content.length);

    file_close_handle_os(fd);

    expect_file(utest_result, file_path, true);
    ASSERT_EQ(file_get_size_os(file_path), (i64)test_content.length);

    file_delete_os(file_path);

    fl_arena_destroy(arena);
}

UTEST(os_api, file_open_write_truncate) {
    FlArena* arena = test_arena();

    FlString file_path = create_test_file_path(arena, "open_write_truncate");
    FlString content1 = FL_S("First content that is quite long");
    FlString content2 = FL_S("Short");

    expect_handle_write(utest_result, file_path, content1, false);
    ASSERT_EQ(file_get_size_os(file_path), (i64)content1.length);

    expect_handle_write(utest_result, file_path, content2, false);

    ASSERT_EQ(file_get_size_os(file_path), (i64)content2.length);

    file_delete_os(file_path);

    fl_arena_destroy(arena);
}

UTEST(os_api, file_open_write_append) {
    FlArena* arena = test_arena();

    FlString file_path = create_test_file_path(arena, "open_write_append");
    FlString content1 = FL_S("First part");
    FlString content2 = FL_S(" - Second part");

    expect_handle_write(utest_result, file_path, content1, false);
    expect_handle_write(utest_result, file_path, content2, true);

    i64 expected_size = (i64)(content1.length + content2.length);
    ASSERT_EQ(file_get_size_os(file_path), expected_size);

    file_delete_os(file_path);

    fl_arena_destroy(arena);
}

UTEST(os_api, file_open_write_multiple_writes) {
    FlArena* arena = test_arena();

    FlString file_path = create_test_file_path(arena, "open_write_multi");

    i64 fd = file_open_write_os(file_path, false);
    ASSERT_GE(fd, 0);

    const FlString parts[] = { FL_S("Line 1\n"), FL_S("Line 2\n"), FL_S("Line 3\n") };
    i64 expected_size = 0;

    for (u32 i = 0; i < 3; i++) {
        i64 written = file_write_to_handle_os(fd, (const u8*)parts[i].data, (i64)parts[i].length);
        ASSERT_EQ(written, (i64)parts[i].length);
        expected_size += (i64)parts[i].length;
    }

    file_close_handle_os(fd);

    ASSERT_EQ(file_get_size_os(file_path), expected_size);

    file_delete_os(file_path);

    fl_arena_destroy(arena);
}

UTEST(os_api, file_write_to_handle_invalid_fd) {
    FlString content = FL_S("This should fail");
    i64 result = file_write_to_handle_os(-1, (const u8*)content.data, (i64)content.length);
    ASSERT_LT(result, 0);
}

UTEST(os_api, file_write_to_handle_null_data) {
    i64 result = file_write_to_handle_os(1, nullptr, 100);
    ASSERT_LT(result, 0);
}

UTEST(os_api, file_write_to_handle_zero_size) {
    FlString content = FL_S("Content");
    i64 result = file_write_to_handle_os(1, (const u8*)content.data, 0);
    ASSERT_LE(result, 0);
}

UTEST(os_api, file_open_write_invalid_path) {
    FlString invalid_path = invalid_os_path();
    i64 fd = file_open_write_os(invalid_path, false);
    ASSERT_LT(fd, 0);
}

UTEST(os_api, file_rename_basic) {
    FlArena* arena = test_arena();

    FlString old_path = create_test_file_path(arena, "rename_old");
    FlString new_path = create_test_file_path(arena, "rename_new");
    FlString content = FL_S("File content for rename test");

    expect_write_text(utest_result, old_path, content);
    expect_file(utest_result, old_path, true);
    expect_file(utest_result, new_path, false);

    ASSERT_TRUE(file_rename_os(old_path, new_path));

    expect_file(utest_result, old_path, false);
    expect_file(utest_result, new_path, true);

    ASSERT_EQ(file_get_size_os(new_path), (i64)content.length);

    file_delete_os(new_path);

    fl_arena_destroy(arena);
}

UTEST(os_api, file_rename_overwrite) {
    FlArena* arena = test_arena();

    FlString source_path = create_test_file_path(arena, "rename_src");
    FlString dest_path = create_test_file_path(arena, "rename_dst");
    FlString source_content = FL_S("Source file content");
    FlString dest_content = FL_S("Destination file content - longer");

    write_text(source_path, source_content);
    write_text(dest_path, dest_content);

    expect_file(utest_result, source_path, true);
    expect_file(utest_result, dest_path, true);

    ASSERT_TRUE(file_rename_os(source_path, dest_path));

    expect_file(utest_result, source_path, false);
    expect_file(utest_result, dest_path, true);
    ASSERT_EQ(file_get_size_os(dest_path), (i64)source_content.length);

    file_delete_os(dest_path);

    fl_arena_destroy(arena);
}

UTEST(os_api, file_rename_nonexistent_source) {
    FlArena* arena = test_arena();

    FlString nonexistent = create_test_file_path(arena, "rename_nonexistent_12345");
    FlString dest = create_test_file_path(arena, "rename_dest");

    ASSERT_FALSE(file_rename_os(nonexistent, dest));

    fl_arena_destroy(arena);
}

UTEST(os_api, file_rename_to_invalid_path) {
    FlArena* arena = test_arena();

    FlString source_path = create_test_file_path(arena, "rename_valid_src");
    FlString invalid_dest = invalid_os_path();
    FlString content = FL_S("Test content");

    write_text(source_path, content);
    expect_file(utest_result, source_path, true);

    ASSERT_FALSE(file_rename_os(source_path, invalid_dest));

    expect_file(utest_result, source_path, true);

    file_delete_os(source_path);

    fl_arena_destroy(arena);
}

UTEST(os_api, file_rename_same_directory) {
    FlArena* arena = test_arena();

    FlString temp_dir = get_temp_directory_os(arena);
    FlString old_name = create_test_name(arena, "rename_same_old");
    FlString new_name = create_test_name(arena, "rename_same_new");
    FlString old_path = file_build_path_os(arena, temp_dir, old_name);
    FlString new_path = file_build_path_os(arena, temp_dir, new_name);
    FlString content = FL_S("Same directory rename test");

    write_text(old_path, content);
    expect_file(utest_result, old_path, true);

    ASSERT_TRUE(file_rename_os(old_path, new_path));

    expect_file(utest_result, old_path, false);
    expect_file(utest_result, new_path, true);

    file_delete_os(new_path);

    fl_arena_destroy(arena);
}

#if !defined(_WIN32)
static void make_symlink(int* utest_result, FlArena* scratch, FlString target, FlString link_path) {
    ASSERT_EQ(symlink(fl_string_to_cstr(scratch, target), fl_string_to_cstr(scratch, link_path)), 0);
}

UTEST(os_api, directory_recursive_remove_does_not_follow_symlinks) {
    FlArena* arena = test_arena();

    FlString outside_dir = create_test_dir_path(arena, "symlink_outside");
    FlString outside_file = file_build_path_os(arena, outside_dir, FL_S("keep.txt"));
    FlString base_dir = create_test_dir_path(arena, "symlink_base");
    FlString link_path = file_build_path_os(arena, base_dir, FL_S("link"));
    FlString content = FL_S("outside content");

    ASSERT_TRUE(file_create_directory_os(outside_dir));
    expect_write_text(utest_result, outside_file, content);
    ASSERT_TRUE(file_create_directory_os(base_dir));

    arena_scratch_auto(temp);
    make_symlink(utest_result, temp.arena, outside_dir, link_path);

    FlString file_link_path = file_build_path_os(arena, base_dir, FL_S("file_link"));
    make_symlink(utest_result, temp.arena, outside_file, file_link_path);
    FileStat file_link_stat = file_stat_os(file_link_path);
    ASSERT_TRUE(file_link_stat.is_symlink);
    ASSERT_TRUE(file_link_stat.is_regular_file);

    FileStat link_stat = file_stat_os(link_path);
    ASSERT_TRUE(link_stat.is_valid);
    ASSERT_TRUE(link_stat.is_symlink);
    ASSERT_TRUE(link_stat.is_directory);

    FileStat outside_stat = file_stat_os(outside_dir);
    ASSERT_TRUE(outside_stat.is_valid);
    ASSERT_FALSE(outside_stat.is_symlink);

    ASSERT_TRUE(file_remove_directory_recursive_os(base_dir));

    expect_dir(utest_result, base_dir, false);
    expect_dir(utest_result, outside_dir, true);
    expect_file(utest_result, outside_file, true);

    ASSERT_TRUE(file_remove_directory_recursive_os(outside_dir));
    fl_arena_destroy(arena);
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Public fl_os_* surface

static void* public_thread_double(void* user_data) {
    int* value = (int*)user_data;
    *value *= 2;
    return user_data;
}

UTEST(os_public, process_and_time_queries_are_sane) {
    ASSERT_EQ(fl_os_process_id(), get_process_id_os());
    ASSERT_GT(fl_os_time_us(), 0);

    u64 before = fl_os_monotonic_ns();
    fl_os_sleep_ms(1);
    fl_os_sleep_us(1);
    ASSERT_GE(fl_os_monotonic_ns(), before);
}

UTEST(os_public, getenv_finds_set_and_misses_unset) {
    ASSERT_TRUE(fl_os_getenv("PATH") != nullptr);
    ASSERT_TRUE(fl_os_getenv("FLOWI_OS_PUBLIC_TEST_UNSET_VARIABLE") == nullptr);
}

UTEST(os_public, thread_runs_and_returns_its_result) {
    int value = 21;
    FlOsThread thread;
    ASSERT_TRUE(fl_os_thread_create(&thread, public_thread_double, &value));
    ASSERT_TRUE(fl_os_thread_join(&thread) == &value);
    ASSERT_EQ(value, 42);
}

UTEST(os_public, file_round_trip_through_the_public_calls) {
    FlArena* arena = test_arena();
    FlString path = create_test_file_path(arena, "os_public_file");
    FlString content = FL_S("hello flowi");

    ASSERT_EQ(fl_os_file_write(path, (const u8*)content.data, (i64)content.length), (i64)content.length);
    ASSERT_TRUE(fl_os_file_exists(path));

    u8 buffer[32] = { 0 };
    ASSERT_EQ(file_read_os(buffer, path, sizeof(buffer)), (i64)content.length);
    ASSERT_EQ(memcmp(buffer, content.data, content.length), 0);

    fl_os_file_delete(path);
    ASSERT_FALSE(fl_os_file_exists(path));
    fl_arena_destroy(arena);
}

UTEST(os_public, write_handle_appends) {
    FlArena* arena = test_arena();
    FlString path = create_test_file_path(arena, "os_public_handle");

    for (int pass = 0; pass < 2; pass++) {
        i64 fd = fl_os_file_open_write(path, pass > 0);
        ASSERT_GE(fd, 0);
        ASSERT_EQ(fl_os_file_write_to_handle(fd, (const u8*)"ab", 2), 2);
        fl_os_file_close_handle(fd);
    }

    u8 buffer[8] = { 0 };
    ASSERT_EQ(file_read_os(buffer, path, sizeof(buffer)), 4);
    ASSERT_EQ(memcmp(buffer, "abab", 4), 0);
    fl_os_file_delete(path);
    fl_arena_destroy(arena);
}

UTEST(os_public, directories_create_iterate_and_remove) {
    FlArena* arena = test_arena();
    FlString root = create_test_dir_path(arena, "os_public_dir");
    FlString nested = fl_os_build_path(arena, fl_os_build_path(arena, root, FL_S("a")), FL_S("b"));
    FlString file = fl_os_build_path(arena, root, FL_S("file.txt"));

    ASSERT_TRUE(fl_os_create_directory_recursive(nested));
    ASSERT_TRUE(fl_os_directory_exists(nested));
    ASSERT_EQ(fl_os_file_write(file, (const u8*)"x", 1), 1);

    u32 files = 0;
    u32 directories = 0;
    FlOsDir dir = fl_os_dir_open(arena, root);
    ASSERT_TRUE(dir.is_valid);
    for (FlOsFileStat entry = fl_os_dir_read_next(arena, &dir); entry.is_valid;
         entry = fl_os_dir_read_next(arena, &dir)) {
        files += entry.is_regular_file;
        directories += entry.is_directory;
    }
    fl_os_dir_close(&dir);
    ASSERT_EQ(files, 1u);
    ASSERT_EQ(directories, 1u);

    ASSERT_TRUE(fl_os_remove_directory_recursive(root));
    ASSERT_FALSE(fl_os_directory_exists(root));
    fl_arena_destroy(arena);
}

UTEST(os_public, temp_directory_exists) {
    FlArena* arena = test_arena();
    ASSERT_TRUE(fl_os_directory_exists(fl_os_temp_directory(arena)));
    fl_arena_destroy(arena);
}
