// Listings of archives, and a directory mount that reaches the archives in it.

#include "vfs_test_support.h"

#if FLOWI_VFS_DRIVER_LIBARCHIVE

UTEST(vfs_libarchive, a_zip_lists_its_files_with_their_sizes_in_natural_order) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = vfs_mount_fixture("test.zip");
    ASSERT_NE(mount, nullptr);

    FlVfsDirHandle handle = fl_vfs_get_listing(mount, FL_S(""), 0);
    fl_vfs_wait(handle);
    FlVfsFileList list = fl_vfs_get_file_list(handle);
    ASSERT_TRUE(list.success);
    ASSERT_EQ(list.count, (u32)FIXTURE_ASYNC_COUNT);

    for_count(i, FIXTURE_ASYNC_COUNT) {
        ASSERT_TRUE(fl_string_equals(list.entries[i].name, string_from_cstr(k_fixture_async_names[i])));
        ASSERT_EQ(list.entries[i].size, (i64)strlen(k_fixture_async_contents[i]));
        ASSERT_FALSE(list.entries[i].is_directory);
        ASSERT_FALSE(list.entries[i].is_archive);
    }
    fl_vfs_close(handle);

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

UTEST(vfs_libarchive, a_zip_shows_its_directories_and_lists_into_them) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = vfs_mount_fixture("archive.zip");
    ASSERT_NE(mount, nullptr);

    FlVfsDirHandle handle = fl_vfs_get_listing(mount, FL_S(""), 0);
    fl_vfs_wait(handle);
    FlVfsFileList list = fl_vfs_get_file_list(handle);
    ASSERT_TRUE(list.success);
    ASSERT_EQ(list.count, 1u);
    ASSERT_TRUE(fl_string_equals(list.entries[0].name, FL_S("tmp")));
    ASSERT_TRUE(list.entries[0].is_directory);
    ASSERT_EQ(list.entries[0].size, 0);
    fl_vfs_close(handle);

    ASSERT_TRUE(vfs_expect_list(mount, FL_S("tmp"), "another.txt file_in_archive.txt"));
    ASSERT_TRUE(vfs_expect_read(mount, FL_S("tmp/file_in_archive.txt"), FL_S("File inside archive.zip\n")));
    ASSERT_TRUE(vfs_expect_read(mount, FL_S("tmp/another.txt"), FL_S("Another file\n")));

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

#define MANY_ENTRIES 300

UTEST(vfs_libarchive, a_zip_with_more_than_256_entries_in_one_directory_lists_every_one) {
    ASSERT_TRUE(vfs_test_begin());

    FlArena* arena = arena_new();
    ZipEntry* entries = arena_alloc_array(arena, ZipEntry, MANY_ENTRIES);
    for_count(i, MANY_ENTRIES) {
        entries[i] = (ZipEntry) { fl_string_to_cstr(arena, fl_sprintf_arena(arena, "file_%04d.txt", (int)i)),
                                  (const u8*)"x", 1 };
    }
    ByteWriter* zip = byte_writer_create(arena);
    zip_build(zip, entries, MANY_ENTRIES);

    FlString dir = vfs_test_temp_dir(arena, FL_S("unit_test_vfs_many_tmp"));
    ASSERT_TRUE(dir.length > 0);
    ASSERT_TRUE(vfs_test_write_file(arena, dir, FL_S("many.zip"), zip));

    FlVfsMount* mount = vfs_test_mount(file_build_path_os(arena, dir, FL_S("many.zip")));
    ASSERT_NE(mount, nullptr);

    FlVfsDirHandle handle = fl_vfs_get_listing(mount, FL_S(""), 0);
    fl_vfs_wait(handle);
    FlVfsFileList list = fl_vfs_get_file_list(handle);
    ASSERT_TRUE(list.success);
    ASSERT_EQ(list.count, (u32)MANY_ENTRIES);
    for_count(i, MANY_ENTRIES) {
        ASSERT_TRUE(fl_string_equals(list.entries[i].name, string_from_cstr(entries[i].name)));
        ASSERT_FALSE(list.entries[i].is_directory);
    }
    fl_vfs_close(handle);

    fl_vfs_mount_close(mount);
    vfs_destroy();
    file_remove_directory_recursive_os(dir);
    fl_arena_destroy(arena);
}

UTEST(vfs_libarchive, a_macosx_directory_entry_in_a_zip_does_not_nest_a_second_macosx_directory) {
    ASSERT_TRUE(vfs_test_begin());

    FlArena* arena = arena_new();
    ByteWriter* zip = byte_writer_create(arena);
    zip_build(zip,
              (ZipEntry[]) { ZIP_DIR("__MACOSX/"), ZIP_TEXT("__MACOSX/._a.png", "resource a"),
                             ZIP_TEXT("__MACOSX/._b.png", "resource b"), ZIP_TEXT("__MACOSX/._c.png", "resource c"),
                             ZIP_TEXT("a.png", "image a"), ZIP_TEXT("b.png", "image b"), ZIP_TEXT("c.png", "image c") },
              7);

    FlString dir = vfs_test_temp_dir(arena, FL_S("unit_test_vfs_macosx_tmp"));
    ASSERT_TRUE(dir.length > 0);
    ASSERT_TRUE(vfs_test_write_file(arena, dir, FL_S("party.zip"), zip));

    FlVfsMount* mount = vfs_test_mount(file_build_path_os(arena, dir, FL_S("party.zip")));
    ASSERT_NE(mount, nullptr);
    ASSERT_TRUE(vfs_expect_list(mount, FL_S(""), "__MACOSX/ a.png b.png c.png"));
    ASSERT_TRUE(vfs_expect_list(mount, FL_S("__MACOSX"), "._a.png ._b.png ._c.png"));
    ASSERT_TRUE(vfs_expect_read(mount, FL_S("__MACOSX/._b.png"), FL_S("resource b")));

    fl_vfs_mount_close(mount);
    vfs_destroy();
    file_remove_directory_recursive_os(dir);
    fl_arena_destroy(arena);
}

// A directory mount reaches the archives in it

UTEST(vfs_libarchive, a_directory_mount_lists_archives_flagged_and_reads_through_them) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = vfs_test_mount(FL_S(VFS_FIXTURE_DIR));
    ASSERT_NE(mount, nullptr);

    FlVfsDirHandle handle = fl_vfs_get_listing(mount, FL_S(""), 0);
    fl_vfs_wait(handle);
    FlVfsFileList list = fl_vfs_get_file_list(handle);
    ASSERT_TRUE(list.success);

    // Every entry of the fixture directory is a file, and it is flagged exactly when its name is an archive's.
    static const char* const archive_suffixes[] = {
        ".zip",
        ".gz",
#if FLOWI_VFS_DRIVER_UNLZX
        ".lzx",
#endif
    };
    bool saw_archive = false;
    bool saw_plain = false;
    for_count(i, list.count) {
        const FlVfsEntry* entry = &list.entries[i];
        bool named_like_archive = false;
        for_count(s, sizeof_array(archive_suffixes)) {
            named_like_archive |= fl_string_ends_with(entry->name, string_from_cstr(archive_suffixes[s]));
        }
        ASSERT_FALSE(entry->is_directory);
        ASSERT_EQ(entry->is_archive, named_like_archive);
        saw_archive |= named_like_archive;
        saw_plain |= !named_like_archive;
    }
    ASSERT_TRUE(saw_archive);
    ASSERT_TRUE(saw_plain);
    fl_vfs_close(handle);

    ASSERT_TRUE(vfs_expect_list(mount, FL_S("test.zip"), "async_test1.txt async_test2.txt async_test3.txt"));
    ASSERT_TRUE(vfs_expect_list(mount, FL_S("test_zipfs_concurrent.zip"), "file1.txt file2.txt file3.txt file4.txt"));
    ASSERT_TRUE(vfs_expect_read(mount, FL_S("test.zip/async_test2.txt"), FL_S("Content 2 is longer")));

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

#endif // FLOWI_VFS_DRIVER_LIBARCHIVE
