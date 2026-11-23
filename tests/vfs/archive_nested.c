// Nested archives made by other tools, and large members and many archives read at once.

#include "vfs_test_support.h"

#if FLOWI_VFS_DRIVER_LIBARCHIVE

// Archives made by other tools that sit inside each other. The generated nested archives are covered in
// tests/vfs/nested.c; these have the extra fields and directory entries a real zip tool writes.

UTEST(vfs_libarchive, a_zip_inside_a_zip_mounts_lists_and_reads) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = vfs_mount_fixture("outer.zip");
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

UTEST(vfs_libarchive, three_levels_of_zips_read_through_one_mount) {
    ASSERT_TRUE(vfs_test_begin());

    FlVfsMount* mount = vfs_mount_fixture("nested.zip");
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

// A large member and many archives read at once. The pattern is checked end to end, so a torn or reordered read is
// seen.

#define LARGE_FILE_SIZE (1024 * 1024)

static u8 large_file_byte(size_t position) {
    return (u8)('A' + (position % 26));
}

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
        mounts[a] = vfs_mount_fixture(k_archive_reads[a].archive);
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
