// Mounting archives, reading what is in them, and the failures. The small archives are checked-in fixtures made
// by other zip tools.

#include "vfs_test_support.h"

#if FLOWI_VFS_DRIVER_LIBARCHIVE

UTEST(vfs_libarchive, a_zip_mounts_and_its_files_read_back_with_their_exact_content) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = vfs_mount(vfs_fixture_path(s_arena, FL_S("test.zip"))).mount;
    ASSERT_NE(mount, nullptr);
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount));
    ASSERT_EQ((int)fl_vfs_mount_get_status(mount), (int)FlVfsMountStatus_Ready);

    ASSERT_TRUE(vfs_expect_reads(mount, k_fixture_async_names, k_fixture_async_contents, FIXTURE_ASYNC_COUNT));

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

UTEST(vfs_libarchive, a_zip_made_by_another_tool_reads_files_at_every_depth) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = vfs_mount_fixture("test_zipfs_valid.zip");
    ASSERT_NE(mount, nullptr);
    static const char* const names[] = { "file1.txt", "file2.txt", "subdir/file3.txt" };
    static const char* const contents[]
        = { "Content of file 1\n", "Content of file 2\n", "Content of file 3 in subdir\n" };
    ASSERT_TRUE(vfs_expect_reads(mount, names, contents, 3));
    fl_vfs_mount_close(mount);

    mount = vfs_mount_fixture("test_zipfs_nested.zip");
    ASSERT_NE(mount, nullptr);
    static const char* const deep_names[]
        = { "root.txt", "level1/file1.txt", "level1/level2/file2.txt", "level1/level2/level3/deep.txt" };
    static const char* const deep_contents[]
        = { "Root level file\n", "Level 1 file\n", "Level 2 file\n", "Deep nested file\n" };
    ASSERT_TRUE(vfs_expect_reads(mount, deep_names, deep_contents, 4));
    ASSERT_TRUE(vfs_expect_list(mount, FL_S(""), "level1/ root.txt"));
    ASSERT_TRUE(vfs_expect_list(mount, FL_S("level1"), "level2/ file1.txt"));
    ASSERT_TRUE(vfs_expect_list(mount, FL_S("level1/level2"), "level3/ file2.txt"));
    ASSERT_TRUE(vfs_expect_list(mount, FL_S("level1/level2/level3"), "deep.txt"));
    fl_vfs_mount_close(mount);

    vfs_destroy();
}

UTEST(vfs_libarchive, all_files_of_a_zip_can_be_in_flight_at_once) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = vfs_mount_fixture("test_zipfs_concurrent.zip");
    ASSERT_NE(mount, nullptr);

    static const char* const names[] = { "file1.txt", "file2.txt", "file3.txt", "file4.txt" };
    static const char* const contents[] = { "Content 1\n", "Content 2\n", "Content 3\n", "Content 4\n" };
    FlVfsHandle handles[sizeof_array(names)];
    for_count(i, sizeof_array(names)) {
        handles[i] = vfs_mount_read_all(mount, string_from_cstr(names[i]));
        ASSERT_NE(handles[i], FL_VFS_HANDLE_INVALID);
    }
    for_count(i, sizeof_array(names)) {
        fl_vfs_wait(handles[i]);
        FlVfsData data = fl_vfs_get_data(handles[i]);
        ASSERT_TRUE(data.success);
        ASSERT_EQ((size_t)data.size, strlen(contents[i]));
        ASSERT_EQ(memcmp(data.data, contents[i], strlen(contents[i])), 0);
        fl_vfs_close(handles[i]);
    }

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

UTEST(vfs_libarchive, a_standalone_gzip_mounts_as_one_file_without_the_gz_suffix) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = vfs_mount_fixture("test_single_file.txt.gz");
    ASSERT_NE(mount, nullptr);
    ASSERT_TRUE(vfs_expect_list(mount, FL_S(""), "test_single_file.txt"));
    ASSERT_TRUE(vfs_expect_read(mount, FL_S("test_single_file.txt"),
                                FL_S("This is test content for single-file compression testing.\n")));

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

UTEST(vfs_libarchive, a_tar_gz_mounts_as_its_members_not_as_one_file) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = vfs_mount_fixture("test_tarball.tar.gz");
    ASSERT_NE(mount, nullptr);
    ASSERT_TRUE(vfs_expect_list(mount, FL_S(""), "sub/ top.txt"));
    ASSERT_TRUE(vfs_expect_list(mount, FL_S("sub"), "inner.txt"));
    ASSERT_TRUE(vfs_expect_read(mount, FL_S("top.txt"), FL_S("Top level of the tarball\n")));
    ASSERT_TRUE(vfs_expect_read(mount, FL_S("sub/inner.txt"), FL_S("Inside a directory of the tarball\n")));

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

UTEST(vfs_libarchive, a_file_that_is_not_in_the_zip_fails_to_read) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = vfs_mount_fixture("test_zipfs_missing_file.zip");
    ASSERT_NE(mount, nullptr);
    ASSERT_TRUE(vfs_expect_read(mount, FL_S("exists.txt"), FL_S("This file exists\n")));
    ASSERT_TRUE(vfs_expect_read_fails(mount, FL_S("nonexistent.txt")));

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

UTEST(vfs_libarchive, files_named_zip_that_are_not_archives_end_in_a_mount_error) {
    ASSERT_TRUE(vfs_test_begin());

    static const char* const names[] = { "test_zipfs_not_zip.zip", "test_zipfs_corrupt.zip" };
    for_count(i, sizeof_array(names)) {
        FlVfsMount* mount = vfs_mount(vfs_fixture_path(s_arena, string_from_cstr(names[i]))).mount;
        ASSERT_NE(mount, nullptr);
        vfs_wait_all();
        ASSERT_EQ((int)fl_vfs_mount_get_status(mount), (int)FlVfsMountStatus_Error);
        ASSERT_FALSE(fl_vfs_mount_is_ready(mount));
        ASSERT_TRUE(vfs_expect_read_fails(mount, FL_S("anything.txt")));
        fl_vfs_mount_close(mount);
    }

    vfs_destroy();
}

UTEST(vfs_libarchive, listing_a_directory_that_is_not_in_the_zip_gives_an_empty_listing) {
    // In an archive a path that names nothing lists as no entries. It is not an error.
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = vfs_mount_fixture("test_zipfs_valid.zip");
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

#endif // FLOWI_VFS_DRIVER_LIBARCHIVE
