// The Lhasa driver: an LHA archive on disk through the VFS, and the driver called directly where the VFS would hide
// the case.

#include "vfs_test_support.h"

#if FLOWI_VFS_DRIVER_LHASA

#define LHA_TEST_NAME "test.txt"
#define LHA_TEST_CONTENT "Hello from LHA test archive!\n"
#define LHA_FIRST_PIECE_SIZE 10

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_lhasa, an_lha_on_disk_reads_whole_and_in_pieces_of_any_size) {
    ASSERT_TRUE(vfs_test_begin());

    FlArena* arena = arena_new();
    ByteWriter* lha = byte_writer_create(arena);
    lha_build(lha, LHA_TEST_NAME, LHA_TEST_CONTENT);
    FlString dir = vfs_test_temp_dir(arena, FL_S("unit_test_vfs_lhasa_tmp"));
    ASSERT_TRUE(dir.length > 0);
    ASSERT_TRUE(vfs_test_write_file(arena, dir, FL_S("song.lha"), lha));

    FlVfsMount* mount = vfs_test_mount(file_build_path_os(arena, dir, FL_S("song.lha")));
    ASSERT_NE(mount, nullptr);

    FlString content = FL_S(LHA_TEST_CONTENT);
    ASSERT_TRUE(vfs_expect_list(mount, FL_S(""), LHA_TEST_NAME));
    ASSERT_TRUE(vfs_expect_read(mount, FL_S(LHA_TEST_NAME), content));
    ASSERT_TRUE(vfs_expect_read_fails(mount, FL_S("missing.txt")));
    ASSERT_TRUE(vfs_expect_chunked_read(mount, FL_S(LHA_TEST_NAME), content, LHA_FIRST_PIECE_SIZE));
    ASSERT_TRUE(vfs_expect_chunked_read(mount, FL_S(LHA_TEST_NAME), content, content.length - 1));
    ASSERT_TRUE(vfs_expect_chunked_read(mount, FL_S(LHA_TEST_NAME), content, content.length));
    ASSERT_TRUE(vfs_expect_chunked_read(mount, FL_S(LHA_TEST_NAME), content, content.length + 5));

    fl_vfs_mount_close(mount);
    vfs_destroy();
    file_remove_directory_recursive_os(dir);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_lhasa, asking_for_the_size_between_two_reads_keeps_the_read_position) {
    // get_size must not rewind the handle to the start of its entry.
    const FlVfsPlugin* plugin = vfs_driver_lhasa();
    void* instance = nullptr;

    FlArena* arena = arena_new();
    ByteWriter* lha = byte_writer_create(arena);
    lha_build(lha, LHA_TEST_NAME, LHA_TEST_CONTENT);

    // On success the driver owns the buffer, so it comes from the heap and is not freed here.
    u8* owned = fl_heap_alloc(lha->size);
    memcpy(owned, lha->bytes, lha->size);
    FlVfsOpenResult root = plugin->open_memory(instance, owned, lha->size, FL_S("test.lha"));
    ASSERT_NE(root.handle, nullptr);
    FlVfsOpenResult file = plugin->open(instance, root.handle, FL_S(LHA_TEST_NAME), 0, (FlVfsMountOptions) { 0 });
    ASSERT_NE(file.handle, nullptr);

    const i64 content_size = (i64)strlen(LHA_TEST_CONTENT);
    u8 buffer[64] = { 0 };

    FlVfsReadResult first = plugin->read(instance, file.handle, (FlString) { 0 }, buffer, LHA_FIRST_PIECE_SIZE);
    ASSERT_EQ(first.error.vfs_status, FlVfsStatus_Success);
    ASSERT_EQ(first.bytes_read, (i64)LHA_FIRST_PIECE_SIZE);

    FlVfsSizeResult size = plugin->get_size(instance, file.handle, (FlString) { 0 });
    ASSERT_EQ(size.error.vfs_status, FlVfsStatus_Success);
    ASSERT_EQ(size.size, content_size);

    FlVfsReadResult rest = plugin->read(instance, file.handle, (FlString) { 0 }, buffer + LHA_FIRST_PIECE_SIZE,
                                        (i64)sizeof(buffer) - LHA_FIRST_PIECE_SIZE);
    ASSERT_EQ(rest.error.vfs_status, FlVfsStatus_Success);
    ASSERT_EQ(rest.bytes_read, content_size - LHA_FIRST_PIECE_SIZE);
    ASSERT_EQ(memcmp(buffer, LHA_TEST_CONTENT, (size_t)content_size), 0);

    FlVfsReadResult end = plugin->read(instance, file.handle, (FlString) { 0 }, buffer, (i64)sizeof(buffer));
    ASSERT_EQ(end.error.vfs_status, FlVfsStatus_Success);
    ASSERT_EQ(end.bytes_read, 0);

    plugin->close(instance, file.handle);
    plugin->close(instance, root.handle);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_lhasa, a_directory_entry_lists_as_a_directory_and_an_empty_stored_file_as_a_file) {
    ASSERT_TRUE(vfs_test_begin());

    FlArena* arena = arena_new();
    ByteWriter* lha = byte_writer_create(arena);
    lha_put_entry(lha, "docs/", "", LHA_METHOD_DIRECTORY, LHA_ATTRIBUTE_DIRECTORY);
    lha_put_entry(lha, "vacant/", "", LHA_METHOD_DIRECTORY, LHA_ATTRIBUTE_DIRECTORY);
    lha_put_entry(lha, "empty.txt", "", LHA_METHOD_STORED, LHA_ATTRIBUTE_FILE);
    lha_put_entry(lha, "docs/note.txt", "a note\n", LHA_METHOD_STORED, LHA_ATTRIBUTE_FILE);
    lha_put_end(lha);
    FlString dir = vfs_test_temp_dir(arena, FL_S("unit_test_vfs_lhasa_dirs_tmp"));
    ASSERT_TRUE(dir.length > 0);
    ASSERT_TRUE(vfs_test_write_file(arena, dir, FL_S("dirs.lha"), lha));

    FlVfsMount* mount = vfs_test_mount(file_build_path_os(arena, dir, FL_S("dirs.lha")));
    ASSERT_NE(mount, nullptr);
    ASSERT_TRUE(vfs_expect_list(mount, FL_S(""), "docs/ vacant/ empty.txt"));
    ASSERT_TRUE(vfs_expect_list(mount, FL_S("docs"), "note.txt"));

    fl_vfs_mount_close(mount);
    vfs_destroy();
    file_remove_directory_recursive_os(dir);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(vfs_lhasa, opening_a_member_that_is_not_in_the_archive_fails_at_open) {
    const FlVfsPlugin* plugin = vfs_driver_lhasa();
    void* instance = nullptr;

    FlArena* arena = arena_new();
    ByteWriter* lha = byte_writer_create(arena);
    lha_build(lha, LHA_TEST_NAME, LHA_TEST_CONTENT);

    u8* owned = fl_heap_alloc(lha->size);
    memcpy(owned, lha->bytes, lha->size);
    FlVfsOpenResult root = plugin->open_memory(instance, owned, lha->size, FL_S("test.lha"));
    ASSERT_NE(root.handle, nullptr);

    FlVfsOpenResult missing = plugin->open(instance, root.handle, FL_S("missing.txt"), 0, (FlVfsMountOptions) { 0 });
    ASSERT_EQ(missing.handle, nullptr);
    ASSERT_EQ(missing.error.vfs_status, FlVfsStatus_FileNotFound);

    FlVfsOpenResult present = plugin->open(instance, root.handle, FL_S(LHA_TEST_NAME), 0, (FlVfsMountOptions) { 0 });
    ASSERT_NE(present.handle, nullptr);

    plugin->close(instance, present.handle);
    plugin->close(instance, root.handle);
    fl_arena_destroy(arena);
}

#endif // FLOWI_VFS_DRIVER_LHASA
