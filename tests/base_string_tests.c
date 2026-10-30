#include "utest/utest.h"
#include <core/arena.h>
#include <core/string.h>

UTEST(base_string, string_begins_with_basic) {
    FlString str = FL_S("hello world");
    FlString prefix1 = FL_S("hello");
    FlString prefix2 = FL_S("world");
    FlString prefix3 = FL_S("");
    FlString prefix4 = FL_S("hello world");
    FlString prefix5 = FL_S("hello world!");

    ASSERT_TRUE(fl_string_begins_with(str, prefix1));
    ASSERT_FALSE(fl_string_begins_with(str, prefix2));
    ASSERT_TRUE(fl_string_begins_with(str, prefix3));
    ASSERT_TRUE(fl_string_begins_with(str, prefix4));
    ASSERT_FALSE(fl_string_begins_with(str, prefix5));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(base_string, string_begins_with_protocols) {
    FlString http_url = FL_S("http://example.com");
    FlString https_url = FL_S("https://example.com");
    FlString ftp_url = FL_S("ftp://files.example.com");
    FlString file_path = FL_S("/home/user/file.txt");

    FlString http_prefix = FL_S("http://");
    FlString https_prefix = FL_S("https://");
    FlString ftp_prefix = FL_S("ftp://");

    ASSERT_TRUE(fl_string_begins_with(http_url, http_prefix));
    ASSERT_FALSE(fl_string_begins_with(http_url, https_prefix));
    ASSERT_FALSE(fl_string_begins_with(http_url, ftp_prefix));

    ASSERT_TRUE(fl_string_begins_with(https_url, https_prefix));
    ASSERT_FALSE(fl_string_begins_with(https_url, http_prefix));
    ASSERT_FALSE(fl_string_begins_with(https_url, ftp_prefix));

    ASSERT_TRUE(fl_string_begins_with(ftp_url, ftp_prefix));
    ASSERT_FALSE(fl_string_begins_with(ftp_url, http_prefix));
    ASSERT_FALSE(fl_string_begins_with(ftp_url, https_prefix));

    ASSERT_FALSE(fl_string_begins_with(file_path, http_prefix));
    ASSERT_FALSE(fl_string_begins_with(file_path, https_prefix));
    ASSERT_FALSE(fl_string_begins_with(file_path, ftp_prefix));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(base_string, string_begins_with_edge_cases) {
    FlString empty_str = FL_S("");
    FlString normal_str = FL_S("test");
    FlString empty_prefix = FL_S("");

    ASSERT_TRUE(fl_string_begins_with(empty_str, empty_prefix));
    ASSERT_FALSE(fl_string_begins_with(empty_str, normal_str));
    ASSERT_TRUE(fl_string_begins_with(normal_str, empty_prefix));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_builder, sb_create_and_length) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlStringBuilder sb = fl_sb_create(arena);

    ASSERT_EQ(fl_sb_length(sb), 0UL);
    ASSERT_EQ(sb.arena, arena);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_builder, fl_sb_append_cstr) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlStringBuilder sb = fl_sb_create(arena);

    sb = fl_sb_append_cstr(sb, "Hello");
    ASSERT_EQ(fl_sb_length(sb), 5UL);

    sb = fl_sb_append_cstr(sb, " World");
    ASSERT_EQ(fl_sb_length(sb), 11UL);

    FlString result = fl_sb_to_string(sb);
    ASSERT_EQ((u64)result.length, 11UL);
    ASSERT_TRUE(fl_string_begins_with(result, FL_S("Hello World")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_builder, fl_sb_append_string) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlStringBuilder sb = fl_sb_create(arena);

    FlString hello = FL_S("Hello");
    FlString world = FL_S(" World");

    sb = fl_sb_append_string(sb, hello);
    ASSERT_EQ(fl_sb_length(sb), 5UL);

    sb = fl_sb_append_string(sb, world);
    ASSERT_EQ(fl_sb_length(sb), 11UL);

    FlString result = fl_sb_to_string(sb);
    ASSERT_EQ((u64)result.length, 11UL);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_builder, fl_sb_append_char) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlStringBuilder sb = fl_sb_create(arena);

    sb = fl_sb_append_char(sb, 'A');
    sb = fl_sb_append_char(sb, 'B');
    sb = fl_sb_append_char(sb, 'C');

    ASSERT_EQ(fl_sb_length(sb), 3UL);

    FlString result = fl_sb_to_string(sb);
    ASSERT_EQ((u64)result.length, 3UL);
    ASSERT_TRUE(fl_string_begins_with(result, FL_S("ABC")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_builder, fl_sb_appendf) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlStringBuilder sb = fl_sb_create(arena);

    sb = fl_sb_appendf(sb, "Number: %d", 42);
    ASSERT_EQ(fl_sb_length(sb), 10UL);

    sb = fl_sb_appendf(sb, ", Float: %.2f", 3.14f);

    FlString result = fl_sb_to_string(sb);
    ASSERT_TRUE(fl_string_begins_with(result, FL_S("Number: 42")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// fl_sb_appendf discards the formatted FlString and takes its length from how far the arena moved, so
// the append has to bump by exactly the assembled length. A format wider than one stb chunk is where
// that can drift.
UTEST(string_builder, sb_appendf_spanning_several_chunks) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlStringBuilder sb = fl_sb_create(arena);

    char wide[2000];
    memset(wide, 'w', sizeof(wide) - 1);
    wide[sizeof(wide) - 1] = 0;

    sb = fl_sb_append_cstr(sb, "head:");
    sb = fl_sb_appendf(sb, "%s", wide);
    sb = fl_sb_append_cstr(sb, ":tail");

    FlString result = fl_sb_to_string(sb);
    ASSERT_EQ((u64)result.length, 5UL + (sizeof(wide) - 1) + 5UL);
    ASSERT_STREQ(fl_sb_to_cstr(sb) + 5 + (sizeof(wide) - 1), ":tail");
    for (size_t i = 0; i < sizeof(wide) - 1; ++i) {
        ASSERT_EQ(result.data[5 + i], 'w');
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_builder, fl_sb_clear) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlStringBuilder sb = fl_sb_create(arena);

    sb = fl_sb_append_cstr(sb, "Hello World");
    ASSERT_EQ(fl_sb_length(sb), 11UL);

    sb = fl_sb_clear(sb);
    ASSERT_EQ(fl_sb_length(sb), 0UL);

    sb = fl_sb_append_cstr(sb, "New");
    ASSERT_EQ(fl_sb_length(sb), 3UL);

    FlString result = fl_sb_to_string(sb);
    ASSERT_TRUE(fl_string_begins_with(result, FL_S("New")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_builder, fl_sb_to_cstr) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlStringBuilder sb = fl_sb_create(arena);

    sb = fl_sb_append_cstr(sb, "Test FlString");

    const char* cstr = fl_sb_to_cstr(sb);
    ASSERT_STREQ(cstr, "Test FlString");

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_builder, sb_mixed_operations) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlStringBuilder sb = fl_sb_create(arena);

    sb = fl_sb_append_cstr(sb, "Hello");
    sb = fl_sb_append_char(sb, ' ');
    sb = fl_sb_append_string(sb, FL_S("World"));
    sb = fl_sb_appendf(sb, "! Count: %d", 123);

    FlString result = fl_sb_to_string(sb);
    const char* cstr = fl_sb_to_cstr(sb);

    ASSERT_STREQ(cstr, "Hello World! Count: 123");
    ASSERT_EQ((u64)result.length, strlen("Hello World! Count: 123"));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_search, string_find_char_basic) {
    FlString str = FL_S("hello world");

    ASSERT_EQ(fl_string_find_char(str, 'h'), 0);
    ASSERT_EQ(fl_string_find_char(str, 'e'), 1);
    ASSERT_EQ(fl_string_find_char(str, 'l'), 2);
    ASSERT_EQ(fl_string_find_char(str, ' '), 5);
    ASSERT_EQ(fl_string_find_char(str, 'd'), 10);

    ASSERT_EQ(fl_string_find_char(str, 'x'), -1);
    ASSERT_EQ(fl_string_find_char(str, 'H'), -1);
    ASSERT_EQ(fl_string_find_char(str, '\0'), -1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_search, string_find_char_edge_cases) {
    FlString empty_str = FL_S("");
    FlString single_char = FL_S("a");

    ASSERT_EQ(fl_string_find_char(empty_str, 'a'), -1);

    ASSERT_EQ(fl_string_find_char(single_char, 'a'), 0);
    ASSERT_EQ(fl_string_find_char(single_char, 'b'), -1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_search, string_find_char_url_parsing) {
    FlString http_url = FL_S("https://example.com/path");

    ASSERT_EQ(fl_string_find_char(http_url, ':'), 5);
    ASSERT_EQ(fl_string_find_char(http_url, '/'), 6);
    ASSERT_EQ(fl_string_find_char(http_url, '.'), 15);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_search, string_find_substr_basic) {
    FlString str = FL_S("hello world, hello universe");

    ASSERT_EQ(fl_string_find_substr(str, FL_S("hello")), 0);
    ASSERT_EQ(fl_string_find_substr(str, FL_S("world")), 6);
    ASSERT_EQ(fl_string_find_substr(str, FL_S("universe")), 19);
    ASSERT_EQ(fl_string_find_substr(str, FL_S("o w")), 4);

    ASSERT_EQ(fl_string_find_substr(str, FL_S("goodbye")), -1);
    ASSERT_EQ(fl_string_find_substr(str, FL_S("Hello")), -1);
    ASSERT_EQ(fl_string_find_substr(str, FL_S("hello world, hello universe!")), -1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_search, string_find_substr_edge_cases) {
    FlString str = FL_S("test string");
    FlString empty_str = FL_S("");

    ASSERT_EQ(fl_string_find_substr(str, FL_S("")), 0);
    ASSERT_EQ(fl_string_find_substr(empty_str, FL_S("")), 0);

    ASSERT_EQ(fl_string_find_substr(empty_str, FL_S("test")), -1);

    FlString exact = FL_S("exact");
    ASSERT_EQ(fl_string_find_substr(exact, FL_S("exact")), 0);

    ASSERT_EQ(fl_string_find_substr(str, FL_S("t")), 0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_search, string_find_substr_protocols) {
    FlString http_url = FL_S("http://example.com");
    FlString https_url = FL_S("https://secure.example.com");
    FlString ftp_url = FL_S("ftp://files.example.com");

    ASSERT_EQ(fl_string_find_substr(http_url, FL_S("://")), 4);
    ASSERT_EQ(fl_string_find_substr(https_url, FL_S("://")), 5);
    ASSERT_EQ(fl_string_find_substr(ftp_url, FL_S("://")), 3);

    ASSERT_EQ(fl_string_find_substr(http_url, FL_S("example")), 7);
    ASSERT_EQ(fl_string_find_substr(https_url, FL_S("secure")), 8);
    ASSERT_EQ(fl_string_find_substr(ftp_url, FL_S("files")), 6);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_search, string_find_char_from_basic) {
    FlString str = FL_S("hello world hello");

    ASSERT_EQ(fl_string_find_char_from(str, 'l', 0), 2);
    ASSERT_EQ(fl_string_find_char_from(str, 'l', 3), 3);
    ASSERT_EQ(fl_string_find_char_from(str, 'l', 4), 9);
    ASSERT_EQ(fl_string_find_char_from(str, 'l', 10), 14);
    ASSERT_EQ(fl_string_find_char_from(str, 'l', 15), 15);

    ASSERT_EQ(fl_string_find_char_from(str, 'x', 0), -1);
    ASSERT_EQ(fl_string_find_char_from(str, 'h', 1), 12);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_search, string_find_char_from_edge_cases) {
    FlString str = FL_S("test");

    ASSERT_EQ(fl_string_find_char_from(str, 't', 4), -1);
    ASSERT_EQ(fl_string_find_char_from(str, 't', 5), -1);
    ASSERT_EQ(fl_string_find_char_from(str, 't', 3), 3);

    FlString empty = FL_S("");
    ASSERT_EQ(fl_string_find_char_from(empty, 'a', 0), -1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_search, string_find_char_from_url_parsing) {
    FlString url = FL_S("https://example.com/path/to/file.txt");

    i64 protocol_end = fl_string_find_substr(url, FL_S("://"));
    ASSERT_EQ(protocol_end, 5);

    i64 first_path_slash = fl_string_find_char_from(url, '/', protocol_end + 3);
    ASSERT_EQ(first_path_slash, 19);

    i64 second_path_slash = fl_string_find_char_from(url, '/', first_path_slash + 1);
    ASSERT_EQ(second_path_slash, 24);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_search, string_contains_basic) {
    FlString str = FL_S("The quick brown fox jumps over the lazy dog");

    ASSERT_TRUE(fl_string_contains(str, FL_S("quick")));
    ASSERT_TRUE(fl_string_contains(str, FL_S("brown fox")));
    ASSERT_TRUE(fl_string_contains(str, FL_S("The")));
    ASSERT_TRUE(fl_string_contains(str, FL_S("dog")));
    ASSERT_TRUE(fl_string_contains(str, FL_S("over the")));

    ASSERT_FALSE(fl_string_contains(str, FL_S("cat")));
    ASSERT_FALSE(fl_string_contains(str, FL_S("THE")));
    ASSERT_FALSE(fl_string_contains(str, FL_S("elephant")));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_search, string_contains_edge_cases) {
    FlString str = FL_S("test string");
    FlString empty_str = FL_S("");

    ASSERT_TRUE(fl_string_contains(str, FL_S("")));
    ASSERT_TRUE(fl_string_contains(empty_str, FL_S("")));

    ASSERT_FALSE(fl_string_contains(empty_str, FL_S("test")));

    ASSERT_TRUE(fl_string_contains(str, str));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_search, string_contains_url_components) {
    FlString url = FL_S("https://api.example.com:8443/v1/users?id=123&format=json");

    ASSERT_TRUE(fl_string_contains(url, FL_S("https://")));
    ASSERT_FALSE(fl_string_contains(url, FL_S("http://")));

    ASSERT_TRUE(fl_string_contains(url, FL_S("api.example.com")));
    ASSERT_TRUE(fl_string_contains(url, FL_S("example")));

    ASSERT_TRUE(fl_string_contains(url, FL_S(":8443")));

    ASSERT_TRUE(fl_string_contains(url, FL_S("/v1/users")));
    ASSERT_TRUE(fl_string_contains(url, FL_S("id=123")));
    ASSERT_TRUE(fl_string_contains(url, FL_S("format=json")));

    ASSERT_FALSE(fl_string_contains(url, FL_S(":80")));
    ASSERT_FALSE(fl_string_contains(url, FL_S("admin")));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(string_search, real_world_url_parsing) {
    FlString base_url1 = FL_S("https://example.com");
    FlString base_url2 = FL_S("https://api.github.com/");
    FlString file_url = FL_S("https://example.com/get/parties/2025/demo.zip");
    FlString local_path = FL_S("/home/user/file.txt");

    i64 protocol_pos1 = fl_string_find_substr(base_url1, FL_S("://"));
    i64 path_pos1 = fl_string_find_char_from(base_url1, '/', protocol_pos1 + 3);
    ASSERT_EQ(protocol_pos1, 5);
    ASSERT_EQ(path_pos1, -1); // No path, should be detected as base URL

    i64 protocol_pos2 = fl_string_find_substr(base_url2, FL_S("://"));
    i64 path_pos2 = fl_string_find_char_from(base_url2, '/', protocol_pos2 + 3);
    ASSERT_EQ(protocol_pos2, 5);
    ASSERT_NE(path_pos2, -1);
    u64 path_len2 = base_url2.length - (u64)path_pos2;
    ASSERT_EQ(path_len2, (u64)1); // Just trailing slash, still base URL

    i64 protocol_pos3 = fl_string_find_substr(file_url, FL_S("://"));
    i64 path_pos3 = fl_string_find_char_from(file_url, '/', protocol_pos3 + 3);
    ASSERT_EQ(protocol_pos3, 5);
    ASSERT_NE(path_pos3, -1);
    u64 path_len3 = file_url.length - (u64)path_pos3;
    ASSERT_GT(path_len3, (u64)1); // Has significant path, not a base URL

    ASSERT_EQ(fl_string_find_substr(local_path, FL_S("://")), -1);
}
