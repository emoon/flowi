// VFS reads, listings and mounts of real files on disk through the built-in local driver. These cases need no archive
// driver, so they run in every build.

#include "vfs_test_support.h"

#define TEST_TXT_CONTENT "Test file content for VFS testing.\nLine 2\nLine 3"
#define README_TXT_CONTENT                                                                     \
    "This is a simple test file for VFS path resolver tests.\nIt contains plain text content " \
    "that can be read by the VFS system."

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlVfsMount* fixture_mount(void) {
    return vfs_test_mount(FL_S(VFS_FIXTURE_DIR));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_local, a_directory_mount_becomes_ready_and_reports_its_path) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = vfs_mount(FL_S(VFS_FIXTURE_DIR)).mount;
    ASSERT_NE(mount, nullptr);
    vfs_wait_all();

    ASSERT_TRUE(fl_vfs_mount_is_ready(mount));
    ASSERT_EQ((int)fl_vfs_mount_get_status(mount), (int)FlVfsMountStatus_Ready);

    FlVfsMountInfo info = fl_vfs_mount_get_info(mount);
    ASSERT_TRUE(info.is_ready);
    ASSERT_TRUE(fl_string_equals(info.path, FL_S(VFS_FIXTURE_DIR)));

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_local, files_read_back_with_their_exact_content) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = fixture_mount();
    ASSERT_NE(mount, nullptr);
    ASSERT_TRUE(vfs_expect_read(mount, FL_S("test.txt"), FL_S(TEST_TXT_CONTENT)));
    ASSERT_TRUE(vfs_expect_read(mount, FL_S("readme.txt"), FL_S(README_TXT_CONTENT)));

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_local, a_read_issued_right_after_the_mount_waits_for_it) {
    // No wait on the mount: the read is chained behind the mount job and still sees the file.
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = vfs_mount(FL_S(VFS_FIXTURE_DIR)).mount;
    ASSERT_NE(mount, nullptr);
    ASSERT_TRUE(vfs_expect_read(mount, FL_S("test.txt"), FL_S(TEST_TXT_CONTENT)));

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_local, reads_in_flight_together_each_return_their_own_file) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = fixture_mount();
    ASSERT_NE(mount, nullptr);

    FlVfsHandle handles[FIXTURE_ASYNC_COUNT];
    for_count(i, FIXTURE_ASYNC_COUNT) {
        handles[i] = vfs_mount_read_all(mount, string_from_cstr(k_fixture_async_names[i]));
        ASSERT_NE(handles[i], FL_VFS_HANDLE_INVALID);
    }
    for_count(i, FIXTURE_ASYNC_COUNT) {
        fl_vfs_wait(handles[i]);
        ASSERT_TRUE(fl_vfs_is_ready(handles[i]));
        FlVfsData data = fl_vfs_get_data(handles[i]);
        ASSERT_TRUE(data.success);
        ASSERT_EQ((size_t)data.size, strlen(k_fixture_async_contents[i]));
        ASSERT_EQ(memcmp(data.data, k_fixture_async_contents[i], strlen(k_fixture_async_contents[i])), 0);
        fl_vfs_close(handles[i]);
    }

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_local, a_missing_file_fails_with_a_message_naming_it) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = fixture_mount();
    ASSERT_NE(mount, nullptr);

    FlVfsHandle handle = vfs_mount_read_all(mount, FL_S("nonexistent_file.txt"));
    ASSERT_NE(handle, FL_VFS_HANDLE_INVALID);
    fl_vfs_wait(handle);

    FlVfsData data = fl_vfs_get_data(handle);
    ASSERT_FALSE(data.success);
    ASSERT_EQ(data.data, nullptr);
    ASSERT_EQ(data.size, 0);
    ASSERT_TRUE(fl_string_equals(data.error_message, FL_S("Failed to read 'nonexistent_file.txt': file not found")));
    fl_vfs_close(handle);

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_local, a_successful_read_has_no_error_message) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = fixture_mount();
    ASSERT_NE(mount, nullptr);

    FlVfsHandle handle = vfs_mount_read_all(mount, FL_S("test.txt"));
    fl_vfs_wait(handle);
    FlVfsData data = fl_vfs_get_data(handle);
    ASSERT_TRUE(data.success);
    ASSERT_TRUE(data.error_message.length == 0);
    fl_vfs_close(handle);

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A read-all callback sees the bytes the driver read and may transform them in place; its release hook runs once.

typedef struct UppercaseStats {
    _Atomic int calls;
    _Atomic int releases;
} UppercaseStats;

static FlVfsData uppercase_callback(void* data, i64 size, void* user_data) {
    UppercaseStats* stats = user_data;
    atomic_fetch_add(&stats->calls, 1);
    u8* bytes = data;
    for_count(i, size) {
        if (bytes[i] >= 'a' && bytes[i] <= 'z') {
            bytes[i] = (u8)(bytes[i] - 'a' + 'A');
        }
    }
    return (FlVfsData) { .data = bytes, .size = size, .success = true };
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void uppercase_release(void* user_data) {
    UppercaseStats* stats = user_data;
    atomic_fetch_add(&stats->releases, 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_local, a_read_callback_transforms_the_data_and_its_release_hook_runs_once) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = fixture_mount();
    ASSERT_NE(mount, nullptr);

    UppercaseStats stats = { 0 };
    FlVfsHandle handle = vfs_mount_read_all(mount, FL_S("test.txt"), .callback = uppercase_callback,
                                            .user_data = &stats, .release = uppercase_release);
    ASSERT_NE(handle, FL_VFS_HANDLE_INVALID);
    fl_vfs_wait(handle);

    FlVfsData data = fl_vfs_get_data(handle);
    ASSERT_TRUE(data.success);
    ASSERT_EQ((size_t)data.size, strlen(TEST_TXT_CONTENT));
    ASSERT_EQ(memcmp(data.data, "TEST FILE CONTENT FOR VFS TESTING.\nLINE 2\nLINE 3", strlen(TEST_TXT_CONTENT)), 0);
    ASSERT_EQ(atomic_load(&stats.calls), 1);
    ASSERT_EQ(atomic_load(&stats.releases), 0);

    fl_vfs_close(handle);
    ASSERT_EQ(atomic_load(&stats.releases), 1);

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_local, every_priority_level_still_delivers_the_right_bytes) {
    // Highest maps onto High in the job system, so all four levels have to work.
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = fixture_mount();
    ASSERT_NE(mount, nullptr);

    static const FlVfsLoadPriority levels[]
        = { FlVfsLoadPriority_Low, FlVfsLoadPriority_Normal, FlVfsLoadPriority_High, FlVfsLoadPriority_Highest };
    FlVfsHandle handles[sizeof_array(levels)];
    for_count(i, sizeof_array(levels)) {
        handles[i] = vfs_mount_read_all(mount, FL_S("test.txt"));
        ASSERT_NE(handles[i], FL_VFS_HANDLE_INVALID);
        fl_vfs_set_priority(handles[i], levels[i]);
    }
    for_count(i, sizeof_array(levels)) {
        fl_vfs_wait(handles[i]);
        FlVfsData data = fl_vfs_get_data(handles[i]);
        ASSERT_TRUE(data.success);
        ASSERT_EQ((size_t)data.size, strlen(TEST_TXT_CONTENT));
        ASSERT_EQ(memcmp(data.data, TEST_TXT_CONTENT, strlen(TEST_TXT_CONTENT)), 0);
        fl_vfs_close(handles[i]);
    }

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_local, two_mounts_of_one_directory_are_independent) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* first = fixture_mount();
    FlVfsMount* second = fixture_mount();
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    ASSERT_NE(first, second);

    fl_vfs_mount_close(first);
    ASSERT_TRUE(vfs_expect_read(second, FL_S("test.txt"), FL_S(TEST_TXT_CONTENT)));

    fl_vfs_mount_close(second);
    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_local, sources_that_do_not_exist_mount_to_distinct_mounts_that_end_in_error) {
    ASSERT_TRUE(vfs_test_begin());

    static const char* const sources[] = { "/vfs_test_missing/archive.zip", "/vfs_test_missing/archive.lzx",
                                           "/vfs_test_missing/document.txt", "/vfs_test_missing/directory" };
    FlVfsMount* mounts[sizeof_array(sources)];
    for_count(i, sizeof_array(sources)) {
        mounts[i] = vfs_mount(string_from_cstr(sources[i])).mount;
        ASSERT_NE(mounts[i], nullptr);
    }
    vfs_wait_all();
    for_count(i, sizeof_array(sources)) {
        ASSERT_EQ((int)fl_vfs_mount_get_status(mounts[i]), (int)FlVfsMountStatus_Error);
        ASSERT_FALSE(fl_vfs_mount_is_ready(mounts[i]));
        for_count(j, i) {
            ASSERT_NE(mounts[i], mounts[j]);
        }
    }
    for_count(i, sizeof_array(sources)) {
        fl_vfs_mount_close(mounts[i]);
    }

    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Listings of a directory made on the fly, so the order does not depend on what the fixture directory holds.

UTEST(vfs_local, a_directory_lists_directories_first_then_files_in_natural_order) {
    ASSERT_TRUE(vfs_test_begin());

    FlArena* arena = arena_new();
    FlString dir = vfs_test_temp_dir(arena, FL_S("unit_test_vfs_local_list_tmp"));
    ASSERT_TRUE(dir.length > 0);
    ASSERT_TRUE(file_create_directory_os(file_build_path_os(arena, dir, FL_S("b_dir"))));
    ASSERT_TRUE(file_create_directory_os(file_build_path_os(arena, dir, FL_S("a_dir"))));
    static const char* const files[] = { "file10.txt", "file2.txt", "file1.txt" };
    for_count(i, sizeof_array(files)) {
        FlString path = file_build_path_os(arena, dir, string_from_cstr(files[i]));
        ASSERT_EQ(file_write_os(path, (const u8*)"x", 1), 1);
    }

    FlVfsMount* mount = vfs_test_mount(dir);
    ASSERT_NE(mount, nullptr);
    ASSERT_TRUE(vfs_expect_list(mount, FL_S(""), "a_dir/ b_dir/ file1.txt file2.txt file10.txt"));

    FlVfsDirHandle handle = fl_vfs_get_listing(mount, FL_S(""), 0);
    fl_vfs_wait(handle);
    FlVfsFileList list = fl_vfs_get_file_list(handle);
    ASSERT_TRUE(list.success);
    ASSERT_EQ(list.count, 5u);
    ASSERT_EQ(list.entries[0].size, 0);
    ASSERT_EQ(list.entries[2].size, 1);
    fl_vfs_close(handle);

    fl_vfs_mount_close(mount);
    vfs_destroy();
    file_remove_directory_recursive_os(dir);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_local, listing_a_directory_that_is_not_there_gives_an_empty_listing) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = fixture_mount();
    ASSERT_NE(mount, nullptr);

    FlVfsDirHandle handle = fl_vfs_get_listing(mount, FL_S("nonexistent_directory"), 0);
    fl_vfs_wait(handle);
    FlVfsFileList list = fl_vfs_get_file_list(handle);
    ASSERT_TRUE(list.success);
    ASSERT_EQ(list.count, 0u);
    fl_vfs_close(handle);

    fl_vfs_mount_close(mount);
    vfs_destroy();
}
