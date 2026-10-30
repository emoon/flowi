// VFS reads and listings through real archives. The small archives are checked-in fixtures made by other zip tools;
// the large ones are generated here. Everything runs through the libarchive driver, so the cases exist only when it
// is built in.

#include "vfs_test_support.h"

#if FLOWI_VFS_DRIVER_LIBARCHIVE

static FlVfsMount* mount_fixture(const char* name) {
    return vfs_test_mount(vfs_fixture_path(s_arena, string_from_cstr(name)));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool expect_reads(FlVfsMount* mount, const char* const* names, const char* const* contents, int count) {
    for (int i = 0; i < count; i++) {
        if (!vfs_expect_read(mount, string_from_cstr(names[i]), string_from_cstr(contents[i]))) {
            return false;
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Mounting an archive and reading what is in it

UTEST(vfs_libarchive, a_zip_mounts_and_its_files_read_back_with_their_exact_content) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = vfs_mount(vfs_fixture_path(s_arena, FL_S("test.zip"))).mount;
    ASSERT_NE(mount, nullptr);
    vfs_wait_all();
    ASSERT_TRUE(fl_vfs_mount_is_ready(mount));
    ASSERT_EQ((int)fl_vfs_mount_get_status(mount), (int)FlVfsMountStatus_Ready);

    ASSERT_TRUE(expect_reads(mount, k_fixture_async_names, k_fixture_async_contents, FIXTURE_ASYNC_COUNT));

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_libarchive, a_zip_made_by_another_tool_reads_files_at_every_depth) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = mount_fixture("test_zipfs_valid.zip");
    ASSERT_NE(mount, nullptr);
    static const char* const names[] = { "file1.txt", "file2.txt", "subdir/file3.txt" };
    static const char* const contents[]
        = { "Content of file 1\n", "Content of file 2\n", "Content of file 3 in subdir\n" };
    ASSERT_TRUE(expect_reads(mount, names, contents, 3));
    fl_vfs_mount_close(mount);

    mount = mount_fixture("test_zipfs_nested.zip");
    ASSERT_NE(mount, nullptr);
    static const char* const deep_names[]
        = { "root.txt", "level1/file1.txt", "level1/level2/file2.txt", "level1/level2/level3/deep.txt" };
    static const char* const deep_contents[]
        = { "Root level file\n", "Level 1 file\n", "Level 2 file\n", "Deep nested file\n" };
    ASSERT_TRUE(expect_reads(mount, deep_names, deep_contents, 4));
    ASSERT_TRUE(vfs_expect_list(mount, FL_S(""), "level1/ root.txt"));
    ASSERT_TRUE(vfs_expect_list(mount, FL_S("level1"), "level2/ file1.txt"));
    ASSERT_TRUE(vfs_expect_list(mount, FL_S("level1/level2"), "level3/ file2.txt"));
    ASSERT_TRUE(vfs_expect_list(mount, FL_S("level1/level2/level3"), "deep.txt"));
    fl_vfs_mount_close(mount);

    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_libarchive, all_files_of_a_zip_can_be_in_flight_at_once) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = mount_fixture("test_zipfs_concurrent.zip");
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_libarchive, a_standalone_gzip_mounts_as_one_file_without_the_gz_suffix) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = mount_fixture("test_single_file.txt.gz");
    ASSERT_NE(mount, nullptr);
    ASSERT_TRUE(vfs_expect_list(mount, FL_S(""), "test_single_file.txt"));
    ASSERT_TRUE(vfs_expect_read(mount, FL_S("test_single_file.txt"),
                                FL_S("This is test content for single-file compression testing.\n")));

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Failures

UTEST(vfs_libarchive, a_tar_gz_mounts_as_its_members_not_as_one_file) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = mount_fixture("test_tarball.tar.gz");
    ASSERT_NE(mount, nullptr);
    ASSERT_TRUE(vfs_expect_list(mount, FL_S(""), "sub/ top.txt"));
    ASSERT_TRUE(vfs_expect_list(mount, FL_S("sub"), "inner.txt"));
    ASSERT_TRUE(vfs_expect_read(mount, FL_S("top.txt"), FL_S("Top level of the tarball\n")));
    ASSERT_TRUE(vfs_expect_read(mount, FL_S("sub/inner.txt"), FL_S("Inside a directory of the tarball\n")));

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_libarchive, a_file_that_is_not_in_the_zip_fails_to_read) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = mount_fixture("test_zipfs_missing_file.zip");
    ASSERT_NE(mount, nullptr);
    ASSERT_TRUE(vfs_expect_read(mount, FL_S("exists.txt"), FL_S("This file exists\n")));
    ASSERT_TRUE(vfs_expect_read_fails(mount, FL_S("nonexistent.txt")));

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_libarchive, listing_a_directory_that_is_not_in_the_zip_gives_an_empty_listing) {
    // An archive has no directory to be missing from: a path that names nothing lists as no entries, not as an error.
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = mount_fixture("test_zipfs_valid.zip");
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Listings

UTEST(vfs_libarchive, a_zip_lists_its_files_with_their_sizes_in_natural_order) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = mount_fixture("test.zip");
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_libarchive, a_zip_shows_its_directories_and_lists_into_them) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = mount_fixture("archive.zip");
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define MANY_ENTRIES 300

UTEST(vfs_libarchive, a_zip_with_more_than_256_entries_in_one_directory_lists_every_one) {
    // The archive driver used to cap its duplicate-tracking set at 256 and drop the rest without a word.
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Archives made by other tools that sit inside each other. The generated nested archives are covered in
// unit_tests_vfs.c; these have the extra fields and directory entries a real zip tool writes.

UTEST(vfs_libarchive, a_zip_inside_a_zip_mounts_lists_and_reads) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = mount_fixture("outer.zip");
    ASSERT_NE(mount, nullptr);

    ASSERT_TRUE(vfs_expect_list(mount, FL_S(""), "tmp/"));
    ASSERT_TRUE(vfs_expect_list(mount, FL_S("tmp"), "inner.zip*"));

    // The inner archive is a file as well as a directory: its raw bytes read back...
    FlVfsHandle raw = vfs_mount_read_all(mount, FL_S("tmp/inner.zip"));
    fl_vfs_wait(raw);
    FlVfsData raw_data = fl_vfs_get_data(raw);
    ASSERT_TRUE(raw_data.success);
    ASSERT_EQ(raw_data.size, 192);
    ASSERT_EQ(memcmp(raw_data.data, "PK\x03\x04", 4), 0);
    fl_vfs_close(raw);

    // ... and its contents list and read through the same path.
    ASSERT_TRUE(vfs_expect_list(mount, FL_S("tmp/inner.zip"), "tmp/"));
    ASSERT_TRUE(vfs_expect_list(mount, FL_S("tmp/inner.zip/tmp"), "deep.txt"));
    ASSERT_TRUE(vfs_expect_read(mount, FL_S("tmp/inner.zip/tmp/deep.txt"), FL_S("Deep file content\n")));
    ASSERT_TRUE(vfs_expect_read_fails(mount, FL_S("tmp/inner.zip/tmp/missing.txt")));

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_libarchive, three_levels_of_zips_read_through_one_mount) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = mount_fixture("nested.zip");
    ASSERT_NE(mount, nullptr);

    ASSERT_TRUE(vfs_expect_list(mount, FL_S("tmp"), "level1_file.txt level2.zip*"));
    ASSERT_TRUE(vfs_expect_read(mount, FL_S("tmp/level1_file.txt"), FL_S("Content of level1 file\n")));
    ASSERT_TRUE(vfs_expect_list(mount, FL_S("tmp/level2.zip/tmp"), "level2_file.txt level3.zip*"));
    ASSERT_TRUE(vfs_expect_read(mount, FL_S("tmp/level2.zip/tmp/level2_file.txt"), FL_S("Content of level2 file\n")));
    ASSERT_TRUE(vfs_expect_list(mount, FL_S("tmp/level2.zip/tmp/level3.zip/tmp"), "level3_file.txt"));
    ASSERT_TRUE(vfs_expect_read(mount, FL_S("tmp/level2.zip/tmp/level3.zip/tmp/level3_file.txt"),
                                FL_S("Content of level3 file\n")));
    ASSERT_TRUE(vfs_expect_read_fails(mount, FL_S("tmp/level2.zip/tmp/level3.zip/tmp/level4_file.txt")));

    fl_vfs_mount_close(mount);
    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_libarchive, a_nested_zip_path_mounts_directly) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* inner = vfs_test_mount(vfs_fixture_path(s_arena, FL_S("outer.zip/tmp/inner.zip")));
    ASSERT_NE(inner, nullptr);
    ASSERT_TRUE(vfs_expect_read(inner, FL_S("tmp/deep.txt"), FL_S("Deep file content\n")));
    fl_vfs_mount_close(inner);

    FlVfsMount* level3 = vfs_test_mount(vfs_fixture_path(s_arena, FL_S("nested.zip/tmp/level2.zip/tmp/level3.zip")));
    ASSERT_NE(level3, nullptr);
    ASSERT_TRUE(vfs_expect_read(level3, FL_S("tmp/level3_file.txt"), FL_S("Content of level3 file\n")));
    fl_vfs_mount_close(level3);

    vfs_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// A large member and many archives read at once. The pattern is checked end to end, so a torn or reordered read is
// seen.

#define LARGE_FILE_SIZE (1024 * 1024)

static u8 large_file_byte(size_t position) {
    return (u8)('A' + (position % 26));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool large_file_matches(const FlVfsData* data) {
    if (!data->success || data->size != LARGE_FILE_SIZE) {
        return false;
    }
    for_count(i, LARGE_FILE_SIZE) {
        if (data->data[i] != large_file_byte(i)) {
            return false;
        }
    }
    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlVfsMount* mount_generated_large_zip(FlArena* arena, FlString* dir_out) {
    u8* content = arena_alloc_array(arena, u8, LARGE_FILE_SIZE);
    for_count(i, LARGE_FILE_SIZE) {
        content[i] = large_file_byte(i);
    }
    ByteWriter* zip = byte_writer_create(arena);
    zip_build(zip, (ZipEntry[]) { { "large.txt", content, LARGE_FILE_SIZE } }, 1);

    *dir_out = vfs_test_temp_dir(arena, FL_S("unit_test_vfs_large_tmp"));
    if (dir_out->length == 0 || !vfs_test_write_file(arena, *dir_out, FL_S("large.zip"), zip)) {
        return nullptr;
    }
    return vfs_test_mount(file_build_path_os(arena, *dir_out, FL_S("large.zip")));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_libarchive, a_one_megabyte_member_reads_back_byte_for_byte) {
    ASSERT_TRUE(vfs_test_begin());

    FlArena* arena = arena_new();
    FlString dir;
    FlVfsMount* mount = mount_generated_large_zip(arena, &dir);
    ASSERT_NE(mount, nullptr);

    FlVfsHandle handle = vfs_mount_read_all(mount, FL_S("large.txt"));
    fl_vfs_wait(handle);
    FlVfsData data = fl_vfs_get_data(handle);
    ASSERT_TRUE(large_file_matches(&data));
    fl_vfs_close(handle);

    fl_vfs_mount_close(mount);
    vfs_destroy();
    file_remove_directory_recursive_os(dir);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define SAME_FILE_READS 8
#define SAME_FILE_ROUNDS 5

UTEST(vfs_libarchive, many_reads_of_one_large_member_in_flight_each_get_the_whole_member) {
    ASSERT_TRUE(vfs_test_begin());

    FlArena* arena = arena_new();
    FlString dir;
    FlVfsMount* mount = mount_generated_large_zip(arena, &dir);
    ASSERT_NE(mount, nullptr);

    for (int round = 0; round < SAME_FILE_ROUNDS; round++) {
        FlVfsHandle handles[SAME_FILE_READS];
        for_count(i, SAME_FILE_READS) {
            handles[i] = vfs_mount_read_all(mount, FL_S("large.txt"));
            ASSERT_NE(handles[i], FL_VFS_HANDLE_INVALID);
        }
        for_count(i, SAME_FILE_READS) {
            fl_vfs_wait(handles[i]);
            FlVfsData data = fl_vfs_get_data(handles[i]);
            ASSERT_TRUE(large_file_matches(&data));
            fl_vfs_close(handles[i]);
        }
    }

    fl_vfs_mount_close(mount);
    vfs_destroy();
    file_remove_directory_recursive_os(dir);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ArchiveReads {
    const char* archive;
    const char* const* names;
    const char* const* contents;
    int count;
} ArchiveReads;

static const char* const k_valid_names[] = { "file1.txt", "file2.txt", "subdir/file3.txt" };
static const char* const k_valid_contents[]
    = { "Content of file 1\n", "Content of file 2\n", "Content of file 3 in subdir\n" };
static const char* const k_concurrent_names[] = { "file1.txt", "file2.txt", "file3.txt", "file4.txt" };
static const char* const k_concurrent_contents[] = { "Content 1\n", "Content 2\n", "Content 3\n", "Content 4\n" };
static const char* const k_deep_names[]
    = { "root.txt", "level1/file1.txt", "level1/level2/file2.txt", "level1/level2/level3/deep.txt" };
static const char* const k_deep_contents[]
    = { "Root level file\n", "Level 1 file\n", "Level 2 file\n", "Deep nested file\n" };

static const ArchiveReads k_archive_reads[] = {
    { "test_zipfs_valid.zip", k_valid_names, k_valid_contents, 3 },
    { "test_zipfs_concurrent.zip", k_concurrent_names, k_concurrent_contents, 4 },
    { "test_zipfs_nested.zip", k_deep_names, k_deep_contents, 4 },
};

#define ARCHIVE_COUNT sizeof_array(k_archive_reads)
#define MULTI_ARCHIVE_ROUNDS 10
#define MULTI_ARCHIVE_DUPLICATES 2
#define MULTI_ARCHIVE_MAX_READS (ARCHIVE_COUNT * 4 * MULTI_ARCHIVE_DUPLICATES)

UTEST(vfs_libarchive, reads_across_several_archives_in_flight_together_return_the_right_bytes) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mounts[ARCHIVE_COUNT];
    for_count(a, ARCHIVE_COUNT) {
        mounts[a] = mount_fixture(k_archive_reads[a].archive);
        ASSERT_NE(mounts[a], nullptr);
    }

    for (int round = 0; round < MULTI_ARCHIVE_ROUNDS; round++) {
        FlVfsHandle handles[MULTI_ARCHIVE_MAX_READS];
        const char* expected[MULTI_ARCHIVE_MAX_READS];
        int count = 0;
        for_count(a, ARCHIVE_COUNT) {
            for (int f = 0; f < k_archive_reads[a].count; f++) {
                for (int dup = 0; dup < MULTI_ARCHIVE_DUPLICATES; dup++) {
                    handles[count] = vfs_mount_read_all(mounts[a], string_from_cstr(k_archive_reads[a].names[f]));
                    expected[count] = k_archive_reads[a].contents[f];
                    ASSERT_NE(handles[count], FL_VFS_HANDLE_INVALID);
                    count++;
                }
            }
        }
        for (int i = 0; i < count; i++) {
            fl_vfs_wait(handles[i]);
            FlVfsData data = fl_vfs_get_data(handles[i]);
            ASSERT_TRUE(data.success);
            ASSERT_EQ((size_t)data.size, strlen(expected[i]));
            ASSERT_EQ(memcmp(data.data, expected[i], strlen(expected[i])), 0);
            fl_vfs_close(handles[i]);
        }
    }

    for_count(a, ARCHIVE_COUNT) {
        fl_vfs_mount_close(mounts[a]);
    }
    vfs_destroy();
}

#endif // FLOWI_VFS_DRIVER_LIBARCHIVE
