#include "utest/utest.h"
#include <core/arena.h>
#include <core/os/os.h>
#include <core/sprintf.h>
#include <core/string.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString mmap_test_file_path(FlArena* arena, const char* name) {
    FlString temp_dir = fl_os_temp_directory(arena);
    u32 pid = fl_os_process_id();
    FlString filename = fl_sprintf_arena(arena, "mmap_test_%s_%u", name, pid);
    return fl_os_build_path(arena, temp_dir, filename);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(mmap, open_valid_file) {
    FlArena* arena = arena_new(.reserved_size = MB(1));

    FlString path = mmap_test_file_path(arena, "valid");
    const u8 data[] = "Hello, mmap world!";
    fl_os_file_write(path, data, (i64)sizeof(data));

    FileMmap m = file_mmap_open_os(path);
    ASSERT_NE(m.data, nullptr);
    ASSERT_EQ(m.size, sizeof(data));
    ASSERT_EQ(memcmp(m.data, data, sizeof(data)), 0);

    file_mmap_close_os(&m);
    ASSERT_EQ(m.data, nullptr);

    fl_os_file_delete(path);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(mmap, open_nonexistent) {
    FileMmap m = file_mmap_open_os(FL_S("/tmp/nonexistent_mmap_test_file_that_does_not_exist"));
    ASSERT_EQ(m.data, nullptr);
    ASSERT_EQ(m.size, 0ULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(mmap, open_empty_file) {
    FlArena* arena = arena_new(.reserved_size = MB(1));

    FlString path = mmap_test_file_path(arena, "empty");
    fl_os_file_write(path, (const u8*)"", 0);

    FileMmap m = file_mmap_open_os(path);
    ASSERT_EQ(m.data, nullptr);
    ASSERT_EQ(m.size, 0ULL);

    fl_os_file_delete(path);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(mmap, close_null_safe) {
    file_mmap_close_os(nullptr);

    // Double close should not crash
    FileMmap m = { .data = nullptr, .size = 0 };
    file_mmap_close_os(&m);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(mmap, read_binary_data) {
    FlArena* arena = arena_new(.reserved_size = MB(1));

    FlString path = mmap_test_file_path(arena, "binary");

    u8 pattern[1024];
    for (int i = 0; i < 1024; i++) {
        pattern[i] = (u8)(i & 0xFF);
    }
    fl_os_file_write(path, pattern, 1024);

    FileMmap m = file_mmap_open_os(path);
    ASSERT_NE(m.data, nullptr);
    ASSERT_EQ(m.size, 1024ULL);

    ASSERT_EQ(m.data[0], 0);
    ASSERT_EQ(m.data[255], 255);
    ASSERT_EQ(m.data[256], 0);
    ASSERT_EQ(m.data[511], 255);
    ASSERT_EQ(m.data[1023], 255);

    file_mmap_close_os(&m);
    fl_os_file_delete(path);
    fl_arena_destroy(arena);
}
