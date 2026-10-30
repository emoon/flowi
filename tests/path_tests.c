#include "utest/utest.h"
#include <core/arena.h>
#include <core/core.h>
#include <core/memory.h>
#include <core/path.h>
#include <core/string.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, normalize_empty) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_normalize(arena, (FlString) { 0 });
    ASSERT_EQ(0UL, (u64)result.length);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, normalize_simple) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("path/to/file.txt");
    FlString result = fl_path_normalize(arena, input);
    ASSERT_EQ((u64)input.length, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "path/to/file.txt", result.length));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, normalize_backslashes) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("path\\to\\file.txt");
    FlString result = fl_path_normalize(arena, input);
    ASSERT_EQ(16UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "path/to/file.txt", result.length));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, normalize_mixed_slashes) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("path\\to/file.txt");
    FlString result = fl_path_normalize(arena, input);
    ASSERT_EQ(16UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "path/to/file.txt", result.length));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, normalize_duplicate_slashes) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("path//to///file.txt");
    FlString result = fl_path_normalize(arena, input);
    ASSERT_EQ(16UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "path/to/file.txt", result.length));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, normalize_trailing_slash) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("path/to/dir/");
    FlString result = fl_path_normalize(arena, input);
    ASSERT_EQ(11UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "path/to/dir", result.length));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, normalize_root_slash) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("/");
    FlString result = fl_path_normalize(arena, input);
    ASSERT_EQ(1UL, (u64)result.length);
    ASSERT_EQ('/', result.data[0]);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, normalize_absolute_path) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("/path/to/file");
    FlString result = fl_path_normalize(arena, input);
    ASSERT_EQ(13UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "/path/to/file", result.length));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, split_empty) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    StringArray result = path_split(arena, (FlString) { 0 });
    ASSERT_EQ(0, result.count);
    ASSERT_EQ(NULL, result.items);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, split_single) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    StringArray result = path_split(arena, FL_S("file.txt"));
    ASSERT_EQ(1, result.count);
    ASSERT_EQ(8UL, (u64)result.items[0].length);
    ASSERT_EQ(0, memcmp(result.items[0].data, "file.txt", 8));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, split_multiple) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    StringArray result = path_split(arena, FL_S("path/to/file.txt"));
    ASSERT_EQ(3, result.count);
    ASSERT_EQ(4UL, (u64)result.items[0].length);
    ASSERT_EQ(0, memcmp(result.items[0].data, "path", 4));
    ASSERT_EQ(2UL, (u64)result.items[1].length);
    ASSERT_EQ(0, memcmp(result.items[1].data, "to", 2));
    ASSERT_EQ(8UL, (u64)result.items[2].length);
    ASSERT_EQ(0, memcmp(result.items[2].data, "file.txt", 8));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, split_absolute) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    StringArray result = path_split(arena, FL_S("/path/to/file"));
    ASSERT_EQ(3, result.count);
    ASSERT_EQ(4UL, (u64)result.items[0].length);
    ASSERT_EQ(0, memcmp(result.items[0].data, "path", 4));
    ASSERT_EQ(2UL, (u64)result.items[1].length);
    ASSERT_EQ(0, memcmp(result.items[1].data, "to", 2));
    ASSERT_EQ(4UL, (u64)result.items[2].length);
    ASSERT_EQ(0, memcmp(result.items[2].data, "file", 4));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, split_trailing_slash) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    StringArray result = path_split(arena, FL_S("path/to/"));
    ASSERT_EQ(2, result.count);
    ASSERT_EQ(4UL, (u64)result.items[0].length);
    ASSERT_EQ(0, memcmp(result.items[0].data, "path", 4));
    ASSERT_EQ(2UL, (u64)result.items[1].length);
    ASSERT_EQ(0, memcmp(result.items[1].data, "to", 2));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, join_empty_base) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_join(arena, (FlString) { 0 }, FL_S("file.txt"));
    ASSERT_EQ(8UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "file.txt", 8));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, join_empty_relative) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_join(arena, FL_S("path/to"), (FlString) { 0 });
    ASSERT_EQ(7UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "path/to", 7));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, join_simple) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_join(arena, FL_S("path/to"), FL_S("file.txt"));
    ASSERT_EQ(16UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "path/to/file.txt", 16));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, join_with_trailing_slash) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_join(arena, FL_S("path/to/"), FL_S("file.txt"));
    ASSERT_EQ(16UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "path/to/file.txt", 16));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, join_with_leading_slash) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_join(arena, FL_S("path/to"), FL_S("/file.txt"));
    ASSERT_EQ(16UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "path/to/file.txt", 16));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, join_both_slashes) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_join(arena, FL_S("path/to/"), FL_S("/file.txt"));
    ASSERT_EQ(16UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "path/to/file.txt", 16));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, join_absolute_relative) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_join(arena, FL_S("path/to"), FL_S("/absolute/path"));
    ASSERT_EQ(14UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "/absolute/path", 14));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, extension_empty) {
    FlString ext = fl_path_get_extension((FlString) { 0 });
    ASSERT_EQ(0UL, (u64)ext.length);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, extension_simple) {
    FlString ext = fl_path_get_extension(FL_S("file.txt"));
    ASSERT_EQ(3UL, (u64)ext.length);
    ASSERT_EQ(0, memcmp(ext.data, "txt", 3));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, extension_no_extension) {
    FlString ext = fl_path_get_extension(FL_S("file"));
    ASSERT_EQ(0UL, (u64)ext.length);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, extension_multiple_dots) {
    FlString ext = fl_path_get_extension(FL_S("file.tar.gz"));
    ASSERT_EQ(2UL, (u64)ext.length);
    ASSERT_EQ(0, memcmp(ext.data, "gz", 2));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, extension_hidden_file) {
    FlString ext = fl_path_get_extension(FL_S(".gitignore"));
    ASSERT_EQ(0UL, (u64)ext.length);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, extension_with_path) {
    FlString ext = fl_path_get_extension(FL_S("path/to/file.txt"));
    ASSERT_EQ(3UL, (u64)ext.length);
    ASSERT_EQ(0, memcmp(ext.data, "txt", 3));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, extension_dot_in_path) {
    FlString ext = fl_path_get_extension(FL_S("path.dir/file.txt"));
    ASSERT_EQ(3UL, (u64)ext.length);
    ASSERT_EQ(0, memcmp(ext.data, "txt", 3));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, filename_prefix_empty) {
    FlString prefix = path_get_filename_prefix((FlString) { 0 });
    ASSERT_EQ(0UL, (u64)prefix.length);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, filename_prefix_simple) {
    // TFMX format: "mdat.songname"
    FlString prefix = path_get_filename_prefix(FL_S("mdat.turrican"));
    ASSERT_EQ(4UL, (u64)prefix.length);
    ASSERT_EQ(0, memcmp(prefix.data, "mdat", 4));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, filename_prefix_with_path) {
    FlString prefix = path_get_filename_prefix(FL_S("/music/tfmx/mdat.turrican"));
    ASSERT_EQ(4UL, (u64)prefix.length);
    ASSERT_EQ(0, memcmp(prefix.data, "mdat", 4));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, filename_prefix_smpl) {
    // TFMX companion file: "smpl.songname"
    FlString prefix = path_get_filename_prefix(FL_S("smpl.turrican"));
    ASSERT_EQ(4UL, (u64)prefix.length);
    ASSERT_EQ(0, memcmp(prefix.data, "smpl", 4));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, filename_prefix_no_dot) {
    FlString prefix = path_get_filename_prefix(FL_S("filename"));
    ASSERT_EQ(0UL, (u64)prefix.length);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, filename_prefix_hidden_file) {
    // Hidden files (starting with dot) should not treat leading dot as separator
    FlString prefix = path_get_filename_prefix(FL_S(".gitignore"));
    ASSERT_EQ(0UL, (u64)prefix.length);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, filename_prefix_hidden_file_with_extension) {
    // The prefix should be ".config" (part before the second dot)
    FlString prefix = path_get_filename_prefix(FL_S(".config.json"));
    ASSERT_EQ(7UL, (u64)prefix.length);
    ASSERT_EQ(0, memcmp(prefix.data, ".config", 7));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, filename_prefix_regular_extension) {
    FlString prefix = path_get_filename_prefix(FL_S("song.mod"));
    ASSERT_EQ(4UL, (u64)prefix.length);
    ASSERT_EQ(0, memcmp(prefix.data, "song", 4));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, filename_prefix_multiple_dots) {
    // Prefix is everything before FIRST dot: "file"
    FlString prefix = path_get_filename_prefix(FL_S("file.tar.gz"));
    ASSERT_EQ(4UL, (u64)prefix.length);
    ASSERT_EQ(0, memcmp(prefix.data, "file", 4));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, filename_empty) {
    FlString name = fl_path_get_filename((FlString) { 0 });
    ASSERT_EQ(0UL, (u64)name.length);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, filename_simple) {
    FlString name = fl_path_get_filename(FL_S("file.txt"));
    ASSERT_EQ(8UL, (u64)name.length);
    ASSERT_EQ(0, memcmp(name.data, "file.txt", 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, filename_with_path) {
    FlString name = fl_path_get_filename(FL_S("path/to/file.txt"));
    ASSERT_EQ(8UL, (u64)name.length);
    ASSERT_EQ(0, memcmp(name.data, "file.txt", 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, filename_trailing_slash) {
    FlString name = fl_path_get_filename(FL_S("path/to/"));
    ASSERT_EQ(0UL, (u64)name.length);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, filename_backslash) {
    FlString name = fl_path_get_filename(FL_S("path\\to\\file.txt"));
    ASSERT_EQ(8UL, (u64)name.length);
    ASSERT_EQ(0, memcmp(name.data, "file.txt", 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, directory_empty) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString dir = fl_path_get_directory(arena, (FlString) { 0 });
    ASSERT_EQ(0UL, (u64)dir.length);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, directory_no_path) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString dir = fl_path_get_directory(arena, FL_S("file.txt"));
    ASSERT_EQ(0UL, (u64)dir.length);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, directory_simple) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString dir = fl_path_get_directory(arena, FL_S("path/to/file.txt"));
    ASSERT_EQ(7UL, (u64)dir.length);
    ASSERT_EQ(0, memcmp(dir.data, "path/to", 7));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, directory_absolute) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString dir = fl_path_get_directory(arena, FL_S("/path/to/file.txt"));
    ASSERT_EQ(8UL, (u64)dir.length);
    ASSERT_EQ(0, memcmp(dir.data, "/path/to", 8));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, directory_root_file) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString dir = fl_path_get_directory(arena, FL_S("/file.txt"));
    ASSERT_EQ(1UL, (u64)dir.length);
    ASSERT_EQ('/', dir.data[0]);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, basename_empty) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString base = fl_path_get_basename(arena, (FlString) { 0 });
    ASSERT_EQ(0UL, (u64)base.length);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, basename_simple) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString base = fl_path_get_basename(arena, FL_S("file.txt"));
    ASSERT_EQ(4UL, (u64)base.length);
    ASSERT_EQ(0, memcmp(base.data, "file", 4));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, basename_no_extension) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString base = fl_path_get_basename(arena, FL_S("file"));
    ASSERT_EQ(4UL, (u64)base.length);
    ASSERT_EQ(0, memcmp(base.data, "file", 4));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, basename_with_path) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString base = fl_path_get_basename(arena, FL_S("path/to/file.txt"));
    ASSERT_EQ(4UL, (u64)base.length);
    ASSERT_EQ(0, memcmp(base.data, "file", 4));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, basename_multiple_dots) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString base = fl_path_get_basename(arena, FL_S("file.tar.gz"));
    ASSERT_EQ(8UL, (u64)base.length);
    ASSERT_EQ(0, memcmp(base.data, "file.tar", 8));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, is_absolute_empty) {
    ASSERT_FALSE(fl_path_is_absolute((FlString) { 0 }));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, is_absolute_relative) {
    ASSERT_FALSE(fl_path_is_absolute(FL_S("path/to/file")));
    ASSERT_FALSE(fl_path_is_absolute(FL_S("./path")));
    ASSERT_FALSE(fl_path_is_absolute(FL_S("../path")));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, is_absolute_unix) {
    ASSERT_TRUE(fl_path_is_absolute(FL_S("/path/to/file")));
    ASSERT_TRUE(fl_path_is_absolute(FL_S("/")));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, is_absolute_windows_drive) {
    ASSERT_TRUE(fl_path_is_absolute(FL_S("C:/path/file")));
    ASSERT_TRUE(fl_path_is_absolute(FL_S("D:\\path\\file")));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, is_absolute_unc) {
    ASSERT_TRUE(fl_path_is_absolute(FL_S("\\\\server\\share")));
    ASSERT_TRUE(fl_path_is_absolute(FL_S("//server/share")));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, is_absolute_url) {
    ASSERT_TRUE(fl_path_is_absolute(FL_S("http://example.com/file")));
    ASSERT_TRUE(fl_path_is_absolute(FL_S("https://example.com/file")));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, is_relative) {
    ASSERT_TRUE(fl_path_is_relative(FL_S("path/to/file")));
    ASSERT_TRUE(fl_path_is_relative(FL_S("./path")));
    ASSERT_TRUE(fl_path_is_relative(FL_S("../path")));
    ASSERT_FALSE(fl_path_is_relative(FL_S("/absolute/path")));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, make_absolute_already) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_make_absolute(arena, FL_S("/base"), FL_S("/already/absolute"));
    ASSERT_EQ(17UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "/already/absolute", 17));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, make_absolute_relative) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_make_absolute(arena, FL_S("/base/path"), FL_S("relative/file"));
    ASSERT_EQ(24UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "/base/path/relative/file", 24));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, equals_empty) {
    ASSERT_TRUE(fl_path_equals((FlString) { 0 }, (FlString) { 0 }));
    ASSERT_FALSE(fl_path_equals(FL_S("path"), (FlString) { 0 }));
    ASSERT_FALSE(fl_path_equals((FlString) { 0 }, FL_S("path")));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, equals_same) {
    ASSERT_TRUE(fl_path_equals(FL_S("path/to/file"), FL_S("path/to/file")));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, equals_different_separators) {
    ASSERT_TRUE(fl_path_equals(FL_S("path/to/file"), FL_S("path\\to\\file")));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, equals_different) {
    ASSERT_FALSE(fl_path_equals(FL_S("path/to/file1"), FL_S("path/to/file2")));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, has_extension_match) {
    ASSERT_TRUE(fl_path_has_extension(FL_S("file.txt"), FL_S("txt")));
    ASSERT_TRUE(fl_path_has_extension(FL_S("file.TXT"), FL_S("txt")));
    ASSERT_TRUE(fl_path_has_extension(FL_S("file.txt"), FL_S("TXT")));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, has_extension_no_match) {
    ASSERT_FALSE(fl_path_has_extension(FL_S("file.txt"), FL_S("doc")));
    ASSERT_FALSE(fl_path_has_extension(FL_S("file"), FL_S("txt")));
    ASSERT_FALSE(fl_path_has_extension((FlString) { 0 }, FL_S("txt")));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, remove_trailing_separator_present) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_remove_trailing_separator(arena, FL_S("path/to/"));
    ASSERT_EQ(7UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "path/to", 7));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, remove_trailing_separator_absent) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_remove_trailing_separator(arena, FL_S("path/to"));
    ASSERT_EQ(7UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "path/to", 7));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, remove_trailing_separator_root) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_remove_trailing_separator(arena, FL_S("/"));
    ASSERT_EQ(1UL, (u64)result.length);
    ASSERT_EQ('/', result.data[0]);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, ensure_trailing_separator_absent) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_ensure_trailing_separator(arena, FL_S("path/to"));
    ASSERT_EQ(8UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "path/to/", 8));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, ensure_trailing_separator_present) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_ensure_trailing_separator(arena, FL_S("path/to/"));
    ASSERT_EQ(8UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "path/to/", 8));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, ensure_trailing_separator_empty) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_ensure_trailing_separator(arena, (FlString) { 0 });
    ASSERT_EQ(1UL, (u64)result.length);
    ASSERT_EQ('/', result.data[0]);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, get_parent_simple) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_get_parent(arena, FL_S("path/to/file"));
    ASSERT_EQ(7UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "path/to", 7));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, get_parent_trailing_slash) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_get_parent(arena, FL_S("path/to/dir/"));
    ASSERT_EQ(7UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "path/to", 7));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, get_parent_no_parent) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_get_parent(arena, FL_S("file"));
    ASSERT_EQ(0UL, (u64)result.length);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, get_parent_root_file) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_get_parent(arena, FL_S("/file"));
    ASSERT_EQ(1UL, (u64)result.length);
    ASSERT_EQ('/', result.data[0]);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, has_separator_yes) {
    ASSERT_TRUE(fl_path_has_separator(FL_S("path/to/file")));
    ASSERT_TRUE(fl_path_has_separator(FL_S("path\\to\\file")));
    ASSERT_TRUE(fl_path_has_separator(FL_S("/")));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, has_separator_no) {
    ASSERT_FALSE(fl_path_has_separator(FL_S("file")));
    ASSERT_FALSE(fl_path_has_separator(FL_S("file.txt")));
    ASSERT_FALSE(fl_path_has_separator((FlString) { 0 }));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, to_forward_slashes) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_to_forward_slashes(arena, FL_S("path\\to\\file"));
    ASSERT_EQ(12UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "path/to/file", 12));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, to_forward_slashes_mixed) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_to_forward_slashes(arena, FL_S("path\\to/file"));
    ASSERT_EQ(12UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "path/to/file", 12));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, to_forward_slashes_already) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = fl_path_to_forward_slashes(arena, FL_S("path/to/file"));
    ASSERT_EQ(12UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "path/to/file", 12));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, edge_case_long_path) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    char long_path[1024];
    int len = snprintf(long_path, sizeof(long_path),
                       "very/long/path/with/many/segments/that/goes/on/and/on/and/on/"
                       "even/more/segments/here/to/test/buffer/handling/and/memory/"
                       "allocation/in/our/path/handling/code/file.txt");

    FlString input = (FlString) { .data = long_path, .length = len };
    FlString normalized = fl_path_normalize(arena, input);
    ASSERT_GT((u64)normalized.length, 0UL);

    FlString filename = fl_path_get_filename(input);
    ASSERT_EQ(8UL, (u64)filename.length);
    ASSERT_EQ(0, memcmp(filename.data, "file.txt", 8));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, edge_case_unicode) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("path/to/文件.txt");
    FlString normalized = fl_path_normalize(arena, input);
    ASSERT_GT((u64)normalized.length, 0UL);

    FlString ext = fl_path_get_extension(input);
    ASSERT_EQ(3UL, (u64)ext.length);
    ASSERT_EQ(0, memcmp(ext.data, "txt", 3));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, edge_case_special_chars) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("path/to/file (1).txt");
    FlString normalized = fl_path_normalize(arena, input);
    ASSERT_EQ(20UL, (u64)normalized.length);

    FlString filename = fl_path_get_filename(input);
    ASSERT_EQ(12UL, (u64)filename.length);
    ASSERT_EQ(0, memcmp(filename.data, "file (1).txt", 12));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, edge_case_dots) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("path.with.dots/file.name.with.dots.txt");
    FlString ext = fl_path_get_extension(input);
    ASSERT_EQ(3UL, (u64)ext.length);
    ASSERT_EQ(0, memcmp(ext.data, "txt", 3));

    FlString basename = fl_path_get_basename(arena, input);
    ASSERT_EQ(19UL, (u64)basename.length);
    ASSERT_EQ(0, memcmp(basename.data, "file.name.with.dots", 19));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, edge_case_spaces) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("path with spaces/file name.txt");
    FlString normalized = fl_path_normalize(arena, input);
    ASSERT_EQ(30UL, (u64)normalized.length);

    FlString filename = fl_path_get_filename(input);
    ASSERT_EQ(13UL, (u64)filename.length);
    ASSERT_EQ(0, memcmp(filename.data, "file name.txt", 13));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, normalize_empty_path) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("");
    FlString result = fl_path_normalize(arena, input);
    ASSERT_EQ(0UL, (u64)result.length);
    ASSERT_TRUE(result.data == NULL);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, normalize_null_path) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = { .data = NULL, .length = 0 };
    FlString result = fl_path_normalize(arena, input);
    ASSERT_EQ(0UL, (u64)result.length);
    ASSERT_TRUE(result.data == NULL);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, normalize_multiple_slashes) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("path////to\\\\file.txt");
    FlString result = fl_path_normalize(arena, input);
    ASSERT_EQ(16UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "path/to/file.txt", result.length));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, normalize_only_slashes) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("//////");
    FlString result = fl_path_normalize(arena, input);
    ASSERT_EQ(1UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "/", 1));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, split_empty_path) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    StringArray result = path_split(arena, FL_S(""));
    ASSERT_EQ(0, result.count);
    ASSERT_TRUE(result.items == NULL);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, split_single_segment) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("file.txt");
    StringArray result = path_split(arena, input);
    ASSERT_EQ(1, result.count);
    ASSERT_EQ(8UL, (u64)result.items[0].length);
    ASSERT_EQ(0, memcmp(result.items[0].data, "file.txt", 8));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, split_multiple_segments_with_mixed_separators) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString input = FL_S("path\\to//file.txt");
    StringArray result = path_split(arena, input);
    ASSERT_EQ(3, result.count);
    ASSERT_EQ(4UL, (u64)result.items[0].length);
    ASSERT_EQ(2UL, (u64)result.items[1].length);
    ASSERT_EQ(8UL, (u64)result.items[2].length);
    ASSERT_EQ(0, memcmp(result.items[0].data, "path", 4));
    ASSERT_EQ(0, memcmp(result.items[1].data, "to", 2));
    ASSERT_EQ(0, memcmp(result.items[2].data, "file.txt", 8));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, join_empty_base) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString base = FL_S("");
    FlString relative = FL_S("file.txt");
    FlString result = fl_path_join(arena, base, relative);
    ASSERT_EQ(8UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "file.txt", 8));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, join_absolute_relative) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString base = FL_S("/base/path");
    FlString relative = FL_S("file.txt");
    FlString result = fl_path_join(arena, base, relative);
    ASSERT_EQ(19UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "/base/path/file.txt", 19));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, join_with_trailing_separator) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString base = FL_S("/base/path/");
    FlString relative = FL_S("file.txt");
    FlString result = fl_path_join(arena, base, relative);
    ASSERT_EQ(19UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "/base/path/file.txt", 19));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, get_extension_no_extension) {
    FlString path = FL_S("file");
    FlString result = fl_path_get_extension(path);
    ASSERT_EQ(0UL, (u64)result.length);
    ASSERT_TRUE(result.data == NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, get_extension_with_dot) {
    FlString path = FL_S("file.txt");
    FlString result = fl_path_get_extension(path);
    ASSERT_EQ(3UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "txt", 3));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, get_extension_empty_path) {
    FlString path = FL_S("");
    FlString result = fl_path_get_extension(path);
    ASSERT_EQ(0UL, (u64)result.length);
    ASSERT_TRUE(result.data == NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, get_filename_empty_path) {
    FlString path = FL_S("");
    FlString result = fl_path_get_filename(path);
    ASSERT_EQ(0UL, (u64)result.length);
    ASSERT_TRUE(result.data == NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, get_filename_with_separator) {
    FlString path = FL_S("/path/to/file.txt");
    FlString result = fl_path_get_filename(path);
    ASSERT_EQ(8UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "file.txt", 8));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, get_filename_ends_with_separator) {
    FlString path = FL_S("/path/to/");
    FlString result = fl_path_get_filename(path);
    ASSERT_EQ(0UL, (u64)result.length);
    ASSERT_TRUE(result.data == NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, get_directory_empty_path) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString path = FL_S("");
    FlString result = fl_path_get_directory(arena, path);
    ASSERT_EQ(0UL, (u64)result.length);
    ASSERT_TRUE(result.data == NULL);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, get_directory_single_segment) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString path = FL_S("file.txt");
    FlString result = fl_path_get_directory(arena, path);
    ASSERT_EQ(0UL, (u64)result.length);
    ASSERT_TRUE(result.data == NULL);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, get_directory_with_path) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString path = FL_S("/path/to/file.txt");
    FlString result = fl_path_get_directory(arena, path);
    ASSERT_EQ(8UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "/path/to", 8));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, get_basename_no_extension) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString path = FL_S("/path/to/file");
    FlString result = fl_path_get_basename(arena, path);
    ASSERT_EQ(4UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "file", 4));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, get_basename_with_extension) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString path = FL_S("/path/to/file.txt");
    FlString result = fl_path_get_basename(arena, path);
    ASSERT_EQ(4UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "file", 4));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, is_absolute_unix) {
    FlString path = FL_S("/path/to/file");
    bool result = fl_path_is_absolute(path);
    ASSERT_TRUE(result);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, is_absolute_windows) {
    FlString path = FL_S("C:\\path\\to\\file");
    bool result = fl_path_is_absolute(path);
    ASSERT_TRUE(result);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, is_absolute_relative) {
    FlString path = FL_S("path/to/file");
    bool result = fl_path_is_absolute(path);
    ASSERT_FALSE(result);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, is_relative_absolute) {
    FlString path = FL_S("/path/to/file");
    bool result = fl_path_is_relative(path);
    ASSERT_FALSE(result);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, is_relative_path) {
    FlString path = FL_S("path/to/file");
    bool result = fl_path_is_relative(path);
    ASSERT_TRUE(result);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, make_absolute_relative) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString base = FL_S("/base/path");
    FlString path = FL_S("file.txt");
    FlString result = fl_path_make_absolute(arena, base, path);
    ASSERT_EQ(19UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "/base/path/file.txt", 19));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, make_absolute_already_absolute) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString base = FL_S("/base/path");
    FlString path = FL_S("/absolute/path");
    FlString result = fl_path_make_absolute(arena, base, path);
    ASSERT_EQ(14UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "/absolute/path", 14));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, path_equals_different_separators) {
    FlString path1 = FL_S("/path/to/file");
    FlString path2 = FL_S("\\path\\to\\file");
    bool result = fl_path_equals(path1, path2);
    ASSERT_TRUE(result);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, path_equals_different_paths) {
    FlString path1 = FL_S("/path/to/file");
    FlString path2 = FL_S("/path/to/other");
    bool result = fl_path_equals(path1, path2);
    ASSERT_FALSE(result);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, has_extension_case_insensitive) {
    FlString path = FL_S("file.TXT");
    FlString extension = FL_S("txt");
    bool result = fl_path_has_extension(path, extension);
    ASSERT_TRUE(result);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, has_extension_no_match) {
    FlString path = FL_S("file.txt");
    FlString extension = FL_S("jpg");
    bool result = fl_path_has_extension(path, extension);
    ASSERT_FALSE(result);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, remove_trailing_separator) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString path = FL_S("/path/to/");
    FlString result = fl_path_remove_trailing_separator(arena, path);
    ASSERT_EQ(8UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "/path/to", 8));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, remove_trailing_separator_no_separator) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString path = FL_S("/path/to");
    FlString result = fl_path_remove_trailing_separator(arena, path);
    ASSERT_EQ(8UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "/path/to", 8));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, ensure_trailing_separator) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString path = FL_S("/path/to");
    FlString result = fl_path_ensure_trailing_separator(arena, path);
    ASSERT_EQ(9UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "/path/to/", 9));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, ensure_trailing_separator_already_present) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString path = FL_S("/path/to/");
    FlString result = fl_path_ensure_trailing_separator(arena, path);
    ASSERT_EQ(9UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "/path/to/", 9));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, get_parent_no_parent) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString path = FL_S("file.txt");
    FlString result = fl_path_get_parent(arena, path);
    ASSERT_EQ(0UL, (u64)result.length);
    ASSERT_TRUE(result.data == NULL);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, get_parent_with_path) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString path = FL_S("/path/to/file.txt");
    FlString result = fl_path_get_parent(arena, path);
    ASSERT_EQ(8UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "/path/to", 8));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, has_separator_true) {
    FlString path = FL_S("/path/to/file");
    bool result = fl_path_has_separator(path);
    ASSERT_TRUE(result);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, has_separator_false) {
    FlString path = FL_S("file");
    bool result = fl_path_has_separator(path);
    ASSERT_FALSE(result);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, to_forward_slashes) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString path = FL_S("\\path\\to\\file");
    FlString result = fl_path_to_forward_slashes(arena, path);
    ASSERT_EQ(13UL, (u64)result.length);
    ASSERT_EQ(0, memcmp(result.data, "/path/to/file", 13));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, is_separator_slash) {
    bool result = path_is_separator('/');
    ASSERT_TRUE(result);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, is_separator_backslash) {
    bool result = path_is_separator('\\');
    ASSERT_TRUE(result);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path_utils, is_separator_non_separator) {
    bool result = path_is_separator('a');
    ASSERT_FALSE(result);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, get_current_directory) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = path_get_current_directory(arena);

    ASSERT_GT((u64)result.length, 0UL);
    ASSERT_TRUE(fl_path_is_absolute(result));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, get_current_directory_not_null) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlString result = path_get_current_directory(arena);

    if (result.length > 0) {
        ASSERT_TRUE(result.data != NULL);
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, find_directory_nonexistent) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString result = path_find_directory(arena, FL_S("nonexistent_directory_12345"));

    ASSERT_EQ(0UL, (u64)result.length);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, find_directory_current_existing) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString test_dir = FL_S("test_find_dir");
    bool created = file_create_directory_os(test_dir);

    if (created) {
        FlString result = path_find_directory(arena, test_dir);

        ASSERT_GT((u64)result.length, 0UL);
        ASSERT_TRUE(file_directory_exists_os(result));

        file_remove_directory_os(test_dir);
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, find_directory_relative_vs_absolute) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString current_dir = path_get_current_directory(arena);
    if (current_dir.length > 0) {
        FlString current_name = fl_path_get_filename(current_dir);

        if (current_name.length > 0) {
            FlString parent_dir = fl_path_get_parent(arena, current_dir);

            if (parent_dir.length > 0 && fl_path_is_absolute(parent_dir)) {
                FlString result = path_find_directory(arena, current_name);

                // Result should either be empty (not found) or a valid path
                if (result.length > 0) {
                    ASSERT_TRUE(file_directory_exists_os(result));
                }
            }
        }
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, find_directory_empty_name) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString result = path_find_directory(arena, FL_S(""));

    ASSERT_EQ(0UL, (u64)result.length);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(path, find_directory_with_separators) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString result = path_find_directory(arena, FL_S("test/with/separators"));

    // A multi-segment name is searched verbatim against the cwd and each ancestor; no such directory exists, so
    // the miss must report the empty string rather than a partial match on the first segment.
    ASSERT_EQ(0UL, (u64)result.length);

    fl_arena_destroy(arena);
}
