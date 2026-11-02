#include "utest/utest.h"
#include <core/arena.h>
#include <core/os/os.h>
#include <core/sprintf.h>
#include <core/string.h>
#include <stdio.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString create_test_name(FlArena* arena, const char* base_name) {
    u32 pid = fl_os_process_id();
    return fl_sprintf_arena(arena, "%s_%u", base_name, pid);
}

static FlString create_test_file_path(FlArena* arena, const char* test_name) {
    FlString temp_dir = fl_os_temp_directory(arena);
    FlString unique_name = create_test_name(arena, test_name);
    return fl_os_build_path(arena, temp_dir, unique_name);
}

static FlString create_test_dir_path(FlArena* arena, const char* test_name) {
    FlString temp_dir = fl_os_temp_directory(arena);
    FlString unique_name = create_test_name(arena, test_name);
    return fl_os_build_path(arena, temp_dir, unique_name);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, get_temp_directory_basic) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString temp_dir = fl_os_temp_directory(arena);

    ASSERT_GT((u64)temp_dir.length, 0ULL);
    ASSERT_NE(temp_dir.data, nullptr);

    ASSERT_TRUE(temp_dir.data[0] != '\0');

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, get_temp_directory_consistency) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString temp_dir1 = fl_os_temp_directory(arena);
    FlString temp_dir2 = fl_os_temp_directory(arena);

    ASSERT_TRUE(fl_string_equals(temp_dir1, temp_dir2));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, get_process_id_basic) {
    u32 pid = fl_os_process_id();

    ASSERT_GT(pid, 0U);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, get_process_id_consistency) {
    u32 pid1 = fl_os_process_id();
    u32 pid2 = fl_os_process_id();

    ASSERT_EQ(pid1, pid2);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, file_write_basic) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString file_path = create_test_file_path(arena, "file_write_basic");
    FlString test_content = FL_S("Hello, OS API test!");

    i64 bytes_written = fl_os_file_write(file_path, (const u8*)test_content.data, (i64)test_content.length);
    ASSERT_EQ(bytes_written, (i64)test_content.length);

    ASSERT_TRUE(fl_os_file_exists(file_path));

    fl_os_file_delete(file_path);
    ASSERT_FALSE(fl_os_file_exists(file_path));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, file_write_empty) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString file_path = create_test_file_path(arena, "file_write_empty");

    i64 bytes_written = fl_os_file_write(file_path, nullptr, 0);
    ASSERT_EQ(bytes_written, 0);

    ASSERT_TRUE(fl_os_file_exists(file_path));

    fl_os_file_delete(file_path);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, file_write_large_content) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString file_path = create_test_file_path(arena, "file_write_large");

    const u64 content_size = 4096;
    u8* large_content = arena_alloc_array(arena, u8, content_size);
    for (u64 i = 0; i < content_size; i++) {
        large_content[i] = (u8)(i % 256);
    }

    i64 bytes_written = fl_os_file_write(file_path, large_content, (i64)content_size);
    ASSERT_EQ(bytes_written, (i64)content_size);

    ASSERT_TRUE(fl_os_file_exists(file_path));

    fl_os_file_delete(file_path);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, file_write_overwrite) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString file_path = create_test_file_path(arena, "file_write_overwrite");
    FlString content1 = FL_S("First content");
    FlString content2 = FL_S("Second content");

    i64 bytes1 = fl_os_file_write(file_path, (const u8*)content1.data, (i64)content1.length);
    ASSERT_EQ(bytes1, (i64)content1.length);
    ASSERT_TRUE(fl_os_file_exists(file_path));

    i64 bytes2 = fl_os_file_write(file_path, (const u8*)content2.data, (i64)content2.length);
    ASSERT_EQ(bytes2, (i64)content2.length);
    ASSERT_TRUE(fl_os_file_exists(file_path));

    fl_os_file_delete(file_path);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, directory_create_and_remove) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString dir_path = create_test_dir_path(arena, "dir_create_remove");

    ASSERT_FALSE(fl_os_directory_exists(dir_path));

    ASSERT_TRUE(fl_os_create_directory(dir_path));
    ASSERT_TRUE(fl_os_directory_exists(dir_path));

    ASSERT_TRUE(fl_os_remove_directory(dir_path));
    ASSERT_FALSE(fl_os_directory_exists(dir_path));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, directory_create_existing) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString dir_path = create_test_dir_path(arena, "dir_create_existing");

    ASSERT_TRUE(fl_os_create_directory(dir_path));
    ASSERT_TRUE(fl_os_directory_exists(dir_path));

    // Platform behavior varies on an already-existing directory, but must not crash.
    fl_os_create_directory(dir_path);
    ASSERT_TRUE(fl_os_directory_exists(dir_path));

    fl_os_remove_directory(dir_path);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, directory_recursive_remove_with_files) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString base_dir = create_test_dir_path(arena, "dir_recursive");
    FlString sub_dir = fl_os_build_path(arena, base_dir, FL_S("subdir"));
    FlString file1 = fl_os_build_path(arena, base_dir, FL_S("file1.txt"));
    FlString file2 = fl_os_build_path(arena, sub_dir, FL_S("file2.txt"));
    FlString test_content = FL_S("Test file content");

    ASSERT_TRUE(fl_os_create_directory(base_dir));
    ASSERT_TRUE(fl_os_create_directory(sub_dir));

    ASSERT_EQ(fl_os_file_write(file1, (const u8*)test_content.data, (i64)test_content.length), (i64)test_content.length);
    ASSERT_EQ(fl_os_file_write(file2, (const u8*)test_content.data, (i64)test_content.length), (i64)test_content.length);

    ASSERT_TRUE(fl_os_directory_exists(base_dir));
    ASSERT_TRUE(fl_os_directory_exists(sub_dir));
    ASSERT_TRUE(fl_os_file_exists(file1));
    ASSERT_TRUE(fl_os_file_exists(file2));

    ASSERT_TRUE(fl_os_remove_directory_recursive(base_dir));

    ASSERT_FALSE(fl_os_directory_exists(base_dir));
    ASSERT_FALSE(fl_os_directory_exists(sub_dir));
    ASSERT_FALSE(fl_os_file_exists(file1));
    ASSERT_FALSE(fl_os_file_exists(file2));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, directory_recursive_remove_nested) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString base_dir = create_test_dir_path(arena, "dir_nested");
    FlString level1 = fl_os_build_path(arena, base_dir, FL_S("level1"));
    FlString level2 = fl_os_build_path(arena, level1, FL_S("level2"));
    FlString level3 = fl_os_build_path(arena, level2, FL_S("level3"));

    ASSERT_TRUE(fl_os_create_directory(base_dir));
    ASSERT_TRUE(fl_os_create_directory(level1));
    ASSERT_TRUE(fl_os_create_directory(level2));
    ASSERT_TRUE(fl_os_create_directory(level3));

    ASSERT_TRUE(fl_os_directory_exists(base_dir));
    ASSERT_TRUE(fl_os_directory_exists(level1));
    ASSERT_TRUE(fl_os_directory_exists(level2));
    ASSERT_TRUE(fl_os_directory_exists(level3));

    ASSERT_TRUE(fl_os_remove_directory_recursive(base_dir));

    ASSERT_FALSE(fl_os_directory_exists(base_dir));
    ASSERT_FALSE(fl_os_directory_exists(level1));
    ASSERT_FALSE(fl_os_directory_exists(level2));
    ASSERT_FALSE(fl_os_directory_exists(level3));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, directory_create_recursive_nested) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString base_dir = create_test_dir_path(arena, "recursive_create");
    FlString level1 = fl_os_build_path(arena, base_dir, FL_S("level1"));
    FlString level2 = fl_os_build_path(arena, level1, FL_S("level2"));
    FlString level3 = fl_os_build_path(arena, level2, FL_S("level3"));

    ASSERT_FALSE(fl_os_directory_exists(base_dir));

    ASSERT_TRUE(fl_os_create_directory_recursive(level3));

    ASSERT_TRUE(fl_os_directory_exists(base_dir));
    ASSERT_TRUE(fl_os_directory_exists(level1));
    ASSERT_TRUE(fl_os_directory_exists(level2));
    ASSERT_TRUE(fl_os_directory_exists(level3));

    fl_os_remove_directory_recursive(base_dir);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, directory_create_recursive_existing) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString dir_path = create_test_dir_path(arena, "recursive_existing");

    ASSERT_TRUE(fl_os_create_directory_recursive(dir_path));
    ASSERT_TRUE(fl_os_directory_exists(dir_path));

    ASSERT_TRUE(fl_os_create_directory_recursive(dir_path));
    ASSERT_TRUE(fl_os_directory_exists(dir_path));

    fl_os_remove_directory(dir_path);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, directory_create_recursive_empty_path) {
    ASSERT_FALSE(fl_os_create_directory_recursive(string_empty()));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, directory_create_recursive_single_level) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString dir_path = create_test_dir_path(arena, "recursive_single");

    ASSERT_FALSE(fl_os_directory_exists(dir_path));

    ASSERT_TRUE(fl_os_create_directory_recursive(dir_path));
    ASSERT_TRUE(fl_os_directory_exists(dir_path));

    fl_os_remove_directory(dir_path);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, full_workflow_integration) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString base_dir = create_test_dir_path(arena, "integration_test");
    FlString sub_dir = fl_os_build_path(arena, base_dir, FL_S("data"));
    FlString config_file = fl_os_build_path(arena, base_dir, FL_S("config.txt"));
    FlString data_file = fl_os_build_path(arena, sub_dir, FL_S("data.bin"));

    FlString config_content = FL_S("# Test configuration file\nversion=1.0\ndebug=true\n");

    const u64 data_size = 256;
    u8* binary_data = arena_alloc_array(arena, u8, data_size);
    for (u64 i = 0; i < data_size; i++) {
        binary_data[i] = (u8)(i ^ 0xAA);
    }

    ASSERT_TRUE(fl_os_create_directory(base_dir));
    ASSERT_TRUE(fl_os_create_directory(sub_dir));

    ASSERT_EQ(fl_os_file_write(config_file, (const u8*)config_content.data, (i64)config_content.length),
              (i64)config_content.length);
    ASSERT_EQ(fl_os_file_write(data_file, binary_data, (i64)data_size), (i64)data_size);

    ASSERT_TRUE(fl_os_directory_exists(base_dir));
    ASSERT_TRUE(fl_os_directory_exists(sub_dir));
    ASSERT_TRUE(fl_os_file_exists(config_file));
    ASSERT_TRUE(fl_os_file_exists(data_file));

    ASSERT_TRUE(fl_os_remove_directory_recursive(base_dir));

    ASSERT_FALSE(fl_os_directory_exists(base_dir));
    ASSERT_FALSE(fl_os_directory_exists(sub_dir));
    ASSERT_FALSE(fl_os_file_exists(config_file));
    ASSERT_FALSE(fl_os_file_exists(data_file));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, cross_platform_path_handling) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString temp_dir = fl_os_temp_directory(arena);
    FlString test_name = create_test_name(arena, "path_test");
    FlString test_path = fl_os_build_path(arena, temp_dir, test_name);

    ASSERT_TRUE(fl_os_create_directory(test_path));
    ASSERT_TRUE(fl_os_directory_exists(test_path));

    FlString file_name = FL_S("test_file.txt");
    FlString file_path = fl_os_build_path(arena, test_path, file_name);
    FlString content = FL_S("Cross-platform path test");

    ASSERT_EQ(fl_os_file_write(file_path, (const u8*)content.data, (i64)content.length), (i64)content.length);
    ASSERT_TRUE(fl_os_file_exists(file_path));

    ASSERT_TRUE(fl_os_remove_directory_recursive(test_path));
    ASSERT_FALSE(fl_os_directory_exists(test_path));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, file_write_invalid_path) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

#ifdef _WIN32
    FlString invalid_path = FL_S("Z:\\non\\existent\\directory\\file.txt");
#else
    FlString invalid_path = FL_S("/non/existent/directory/file.txt");
#endif
    FlString content = FL_S("Should fail");

    i64 result = fl_os_file_write(invalid_path, (const u8*)content.data, (i64)content.length);
    ASSERT_LT(result, (i64)content.length);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, directory_remove_nonexistent) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString nonexistent_dir = create_test_dir_path(arena, "nonexistent_dir_12345");

    ASSERT_FALSE(fl_os_remove_directory(nonexistent_dir));
    ASSERT_FALSE(fl_os_remove_directory_recursive(nonexistent_dir));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, get_disk_space_temp_directory) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString temp_dir = fl_os_temp_directory(arena);
    DiskSpaceInfo info = get_disk_space_os(temp_dir);

    ASSERT_TRUE(info.is_valid);

    ASSERT_GT(info.bytes_total, 0ULL);
    ASSERT_GT(info.bytes_free, 0ULL);
    // bytes_available excludes blocks reserved for root/quota, so it never exceeds bytes_free.
    ASSERT_LE(info.bytes_available, info.bytes_free);

    ASSERT_LE(info.bytes_free, info.bytes_total);
    ASSERT_LE(info.bytes_available, info.bytes_total);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, get_disk_space_current_directory) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString current_dir = fl_os_current_directory(arena);
    DiskSpaceInfo info = get_disk_space_os(current_dir);

    ASSERT_TRUE(info.is_valid);
    ASSERT_GT(info.bytes_total, 0ULL);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, get_disk_space_file_path) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString file_path = create_test_file_path(arena, "disk_space_file_test");
    FlString content = FL_S("Test content for disk space check");

    i64 bytes_written = fl_os_file_write(file_path, (const u8*)content.data, (i64)content.length);
    ASSERT_EQ(bytes_written, (i64)content.length);

    // A file path reports the filesystem containing it.
    DiskSpaceInfo info = get_disk_space_os(file_path);
    ASSERT_TRUE(info.is_valid);
    ASSERT_GT(info.bytes_total, 0ULL);

    fl_os_file_delete(file_path);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, get_disk_space_nonexistent_file_with_valid_parent) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString nonexistent_file;

    // A nonexistent path resolves by walking up to the first parent that exists.
#ifdef _WIN32
    // Use the cwd's drive letter, not a hardcoded C:\ - CI runners may use a different drive.
    FlString cwd = fl_os_current_directory(arena);
    char drive_path[64];
    snprintf(drive_path, sizeof(drive_path), "%c:\\this\\path\\does\\not\\exist", cwd.data[0]);
    nonexistent_file = string_from_cstr(drive_path);
#else
    nonexistent_file = FL_S("/this/path/does/not/exist/at/all");
#endif
    DiskSpaceInfo info = get_disk_space_os(nonexistent_file);

    ASSERT_TRUE(info.is_valid);
    ASSERT_GT(info.bytes_total, 0ULL);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, get_disk_space_empty_path) {
    FlString empty_path = FL_S("");
    DiskSpaceInfo info = get_disk_space_os(empty_path);

    ASSERT_FALSE(info.is_valid);
    ASSERT_EQ(info.bytes_total, 0ULL);
    ASSERT_EQ(info.bytes_free, 0ULL);
    ASSERT_EQ(info.bytes_available, 0ULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, get_disk_space_consistency) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString temp_dir = fl_os_temp_directory(arena);

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, file_open_write_basic) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString file_path = create_test_file_path(arena, "open_write_basic");
    FlString test_content = FL_S("Hello, file handle test!");

    i64 fd = fl_os_file_open_write(file_path, false);
    ASSERT_GE(fd, 0);

    i64 bytes_written = fl_os_file_write_to_handle(fd, (const u8*)test_content.data, (i64)test_content.length);
    ASSERT_EQ(bytes_written, (i64)test_content.length);

    fl_os_file_close_handle(fd);

    ASSERT_TRUE(fl_os_file_exists(file_path));
    ASSERT_EQ(fl_os_file_size(file_path), (i64)test_content.length);

    fl_os_file_delete(file_path);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, file_open_write_truncate) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString file_path = create_test_file_path(arena, "open_write_truncate");
    FlString content1 = FL_S("First content that is quite long");
    FlString content2 = FL_S("Short");

    i64 fd1 = fl_os_file_open_write(file_path, false);
    ASSERT_GE(fd1, 0);
    fl_os_file_write_to_handle(fd1, (const u8*)content1.data, (i64)content1.length);
    fl_os_file_close_handle(fd1);
    ASSERT_EQ(fl_os_file_size(file_path), (i64)content1.length);

    i64 fd2 = fl_os_file_open_write(file_path, false);
    ASSERT_GE(fd2, 0);
    fl_os_file_write_to_handle(fd2, (const u8*)content2.data, (i64)content2.length);
    fl_os_file_close_handle(fd2);

    ASSERT_EQ(fl_os_file_size(file_path), (i64)content2.length);

    fl_os_file_delete(file_path);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, file_open_write_append) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString file_path = create_test_file_path(arena, "open_write_append");
    FlString content1 = FL_S("First part");
    FlString content2 = FL_S(" - Second part");

    i64 fd1 = fl_os_file_open_write(file_path, false);
    ASSERT_GE(fd1, 0);
    fl_os_file_write_to_handle(fd1, (const u8*)content1.data, (i64)content1.length);
    fl_os_file_close_handle(fd1);

    i64 fd2 = fl_os_file_open_write(file_path, true);
    ASSERT_GE(fd2, 0);
    fl_os_file_write_to_handle(fd2, (const u8*)content2.data, (i64)content2.length);
    fl_os_file_close_handle(fd2);

    i64 expected_size = (i64)(content1.length + content2.length);
    ASSERT_EQ(fl_os_file_size(file_path), expected_size);

    fl_os_file_delete(file_path);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, file_open_write_multiple_writes) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString file_path = create_test_file_path(arena, "open_write_multi");

    i64 fd = fl_os_file_open_write(file_path, false);
    ASSERT_GE(fd, 0);

    FlString part1 = FL_S("Line 1\n");
    FlString part2 = FL_S("Line 2\n");
    FlString part3 = FL_S("Line 3\n");

    i64 written1 = fl_os_file_write_to_handle(fd, (const u8*)part1.data, (i64)part1.length);
    i64 written2 = fl_os_file_write_to_handle(fd, (const u8*)part2.data, (i64)part2.length);
    i64 written3 = fl_os_file_write_to_handle(fd, (const u8*)part3.data, (i64)part3.length);

    ASSERT_EQ(written1, (i64)part1.length);
    ASSERT_EQ(written2, (i64)part2.length);
    ASSERT_EQ(written3, (i64)part3.length);

    fl_os_file_close_handle(fd);

    i64 expected_size = (i64)(part1.length + part2.length + part3.length);
    ASSERT_EQ(fl_os_file_size(file_path), expected_size);

    fl_os_file_delete(file_path);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, file_write_to_handle_invalid_fd) {
    FlString content = FL_S("This should fail");
    i64 result = fl_os_file_write_to_handle(-1, (const u8*)content.data, (i64)content.length);
    ASSERT_LT(result, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, file_write_to_handle_null_data) {
    i64 result = fl_os_file_write_to_handle(1, nullptr, 100);
    ASSERT_LT(result, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, file_write_to_handle_zero_size) {
    FlString content = FL_S("Content");
    i64 result = fl_os_file_write_to_handle(1, (const u8*)content.data, 0);
    ASSERT_LE(result, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, file_open_write_invalid_path) {
#ifdef _WIN32
    FlString invalid_path = FL_S("Z:\\non\\existent\\directory\\file.txt");
#else
    FlString invalid_path = FL_S("/non/existent/directory/file.txt");
#endif
    i64 fd = fl_os_file_open_write(invalid_path, false);
    ASSERT_LT(fd, 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, file_rename_basic) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString old_path = create_test_file_path(arena, "rename_old");
    FlString new_path = create_test_file_path(arena, "rename_new");
    FlString content = FL_S("File content for rename test");

    i64 bytes = fl_os_file_write(old_path, (const u8*)content.data, (i64)content.length);
    ASSERT_EQ(bytes, (i64)content.length);
    ASSERT_TRUE(fl_os_file_exists(old_path));
    ASSERT_FALSE(fl_os_file_exists(new_path));

    ASSERT_TRUE(file_rename_os(old_path, new_path));

    ASSERT_FALSE(fl_os_file_exists(old_path));
    ASSERT_TRUE(fl_os_file_exists(new_path));

    ASSERT_EQ(fl_os_file_size(new_path), (i64)content.length);

    fl_os_file_delete(new_path);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, file_rename_overwrite) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString source_path = create_test_file_path(arena, "rename_src");
    FlString dest_path = create_test_file_path(arena, "rename_dst");
    FlString source_content = FL_S("Source file content");
    FlString dest_content = FL_S("Destination file content - longer");

    fl_os_file_write(source_path, (const u8*)source_content.data, (i64)source_content.length);
    fl_os_file_write(dest_path, (const u8*)dest_content.data, (i64)dest_content.length);

    ASSERT_TRUE(fl_os_file_exists(source_path));
    ASSERT_TRUE(fl_os_file_exists(dest_path));

    ASSERT_TRUE(file_rename_os(source_path, dest_path));

    ASSERT_FALSE(fl_os_file_exists(source_path));
    ASSERT_TRUE(fl_os_file_exists(dest_path));
    ASSERT_EQ(fl_os_file_size(dest_path), (i64)source_content.length);

    fl_os_file_delete(dest_path);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, file_rename_nonexistent_source) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString nonexistent = create_test_file_path(arena, "rename_nonexistent_12345");
    FlString dest = create_test_file_path(arena, "rename_dest");

    ASSERT_FALSE(file_rename_os(nonexistent, dest));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, file_rename_to_invalid_path) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString source_path = create_test_file_path(arena, "rename_valid_src");
#ifdef _WIN32
    FlString invalid_dest = FL_S("Z:\\non\\existent\\directory\\file.txt");
#else
    FlString invalid_dest = FL_S("/non/existent/directory/file.txt");
#endif
    FlString content = FL_S("Test content");

    fl_os_file_write(source_path, (const u8*)content.data, (i64)content.length);
    ASSERT_TRUE(fl_os_file_exists(source_path));

    ASSERT_FALSE(file_rename_os(source_path, invalid_dest));

    ASSERT_TRUE(fl_os_file_exists(source_path));

    fl_os_file_delete(source_path);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(os_api, file_rename_same_directory) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString temp_dir = fl_os_temp_directory(arena);
    FlString old_name = create_test_name(arena, "rename_same_old");
    FlString new_name = create_test_name(arena, "rename_same_new");
    FlString old_path = fl_os_build_path(arena, temp_dir, old_name);
    FlString new_path = fl_os_build_path(arena, temp_dir, new_name);
    FlString content = FL_S("Same directory rename test");

    fl_os_file_write(old_path, (const u8*)content.data, (i64)content.length);
    ASSERT_TRUE(fl_os_file_exists(old_path));

    ASSERT_TRUE(file_rename_os(old_path, new_path));

    ASSERT_FALSE(fl_os_file_exists(old_path));
    ASSERT_TRUE(fl_os_file_exists(new_path));

    fl_os_file_delete(new_path);

    fl_arena_destroy(arena);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if !defined(_WIN32)
UTEST(os_api, directory_recursive_remove_does_not_follow_symlinks) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString outside_dir = create_test_dir_path(arena, "symlink_outside");
    FlString outside_file = fl_os_build_path(arena, outside_dir, FL_S("keep.txt"));
    FlString base_dir = create_test_dir_path(arena, "symlink_base");
    FlString link_path = fl_os_build_path(arena, base_dir, FL_S("link"));
    FlString content = FL_S("outside content");

    ASSERT_TRUE(fl_os_create_directory(outside_dir));
    ASSERT_EQ(fl_os_file_write(outside_file, (const u8*)content.data, (i64)content.length), (i64)content.length);
    ASSERT_TRUE(fl_os_create_directory(base_dir));

    arena_scratch_auto(temp);
    ASSERT_EQ(symlink(fl_string_to_cstr(temp.arena, outside_dir), fl_string_to_cstr(temp.arena, link_path)), 0);

    FlString file_link_path = fl_os_build_path(arena, base_dir, FL_S("file_link"));
    ASSERT_EQ(symlink(fl_string_to_cstr(temp.arena, outside_file), fl_string_to_cstr(temp.arena, file_link_path)), 0);
    FlOsFileStat file_link_stat = fl_os_file_stat(file_link_path);
    ASSERT_TRUE(file_link_stat.is_symlink);
    ASSERT_TRUE(file_link_stat.is_regular_file);

    FlOsFileStat link_stat = fl_os_file_stat(link_path);
    ASSERT_TRUE(link_stat.is_valid);
    ASSERT_TRUE(link_stat.is_symlink);
    ASSERT_TRUE(link_stat.is_directory);

    FlOsFileStat outside_stat = fl_os_file_stat(outside_dir);
    ASSERT_TRUE(outside_stat.is_valid);
    ASSERT_FALSE(outside_stat.is_symlink);

    ASSERT_TRUE(fl_os_remove_directory_recursive(base_dir));

    ASSERT_FALSE(fl_os_directory_exists(base_dir));
    ASSERT_TRUE(fl_os_directory_exists(outside_dir));
    ASSERT_TRUE(fl_os_file_exists(outside_file));

    ASSERT_TRUE(fl_os_remove_directory_recursive(outside_dir));
    fl_arena_destroy(arena);
}
#endif
