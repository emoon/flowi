#include "utest/utest.h"
#include <core/arena.h>
#include <core/string.h>

static void expect_begins_with(int* utest_result, FlString str, FlString prefix, bool want) {
    ASSERT_EQ(fl_string_begins_with(str, prefix), want);
}

static void expect_contains(int* utest_result, FlString str, FlString needle, bool want) {
    ASSERT_EQ(fl_string_contains(str, needle), want);
}

static void expect_find_char(int* utest_result, FlString str, char c, i64 want) {
    ASSERT_EQ(fl_string_find_char(str, c), want);
}

static void expect_index(int* utest_result, i64 got, i64 want) {
    ASSERT_EQ(got, want);
}

static void expect_find_substr(int* utest_result, FlString str, FlString needle, i64 want) {
    ASSERT_EQ(fl_string_find_substr(str, needle), want);
}

static void expect_string(int* utest_result, FlString got, FlString want) {
    ASSERT_TRUE(fl_string_equals(got, want));
}

static void expect_string_length(int* utest_result, FlString got, u64 want) {
    ASSERT_EQ((u64)got.length, want);
}

static void expect_sb_text(int* utest_result, FlStringBuilder sb, FlString prefix, u64 length) {
    FlString result = fl_sb_to_string(sb);
    ASSERT_EQ((u64)result.length, length);
    ASSERT_TRUE(fl_string_begins_with(result, prefix));
}

static void expect_sb_length(int* utest_result, FlStringBuilder sb, u64 want) {
    ASSERT_EQ(fl_sb_length(sb), want);
}

UTEST(base_string, string_begins_with_basic) {
    FlString str = FL_S("hello world");
    FlString prefix1 = FL_S("hello");
    FlString prefix2 = FL_S("world");
    FlString prefix3 = FL_S("");
    FlString prefix4 = FL_S("hello world");
    FlString prefix5 = FL_S("hello world!");

    expect_begins_with(utest_result, str, prefix1, true);
    expect_begins_with(utest_result, str, prefix2, false);
    expect_begins_with(utest_result, str, prefix3, true);
    expect_begins_with(utest_result, str, prefix4, true);
    expect_begins_with(utest_result, str, prefix5, false);
}

UTEST(base_string, string_begins_with_protocols) {
    FlString http_url = FL_S("http://example.com");
    FlString https_url = FL_S("https://example.com");
    FlString ftp_url = FL_S("ftp://files.example.com");
    FlString file_path = FL_S("/home/user/file.txt");

    FlString http_prefix = FL_S("http://");
    FlString https_prefix = FL_S("https://");
    FlString ftp_prefix = FL_S("ftp://");

    expect_begins_with(utest_result, http_url, http_prefix, true);
    expect_begins_with(utest_result, http_url, https_prefix, false);
    expect_begins_with(utest_result, http_url, ftp_prefix, false);

    expect_begins_with(utest_result, https_url, https_prefix, true);
    expect_begins_with(utest_result, https_url, http_prefix, false);
    expect_begins_with(utest_result, https_url, ftp_prefix, false);

    expect_begins_with(utest_result, ftp_url, ftp_prefix, true);
    expect_begins_with(utest_result, ftp_url, http_prefix, false);
    expect_begins_with(utest_result, ftp_url, https_prefix, false);

    expect_begins_with(utest_result, file_path, http_prefix, false);
    expect_begins_with(utest_result, file_path, https_prefix, false);
    expect_begins_with(utest_result, file_path, ftp_prefix, false);
}

UTEST(base_string, string_begins_with_edge_cases) {
    FlString empty_str = FL_S("");
    FlString normal_str = FL_S("test");
    FlString empty_prefix = FL_S("");

    expect_begins_with(utest_result, empty_str, empty_prefix, true);
    expect_begins_with(utest_result, empty_str, normal_str, false);
    expect_begins_with(utest_result, normal_str, empty_prefix, true);
}

UTEST(string_builder, sb_create_and_length) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlStringBuilder sb = fl_sb_create(arena);

    expect_sb_length(utest_result, sb, 0UL);
    ASSERT_EQ(sb.arena, arena);

    fl_arena_destroy(arena);
}

UTEST(string_builder, fl_sb_append_cstr) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlStringBuilder sb = fl_sb_create(arena);

    sb = fl_sb_append_cstr(sb, "Hello");
    expect_sb_length(utest_result, sb, 5UL);

    sb = fl_sb_append_cstr(sb, " World");
    expect_sb_length(utest_result, sb, 11UL);

    expect_sb_text(utest_result, sb, FL_S("Hello World"), 11UL);

    fl_arena_destroy(arena);
}

UTEST(string_builder, fl_sb_append_string) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlStringBuilder sb = fl_sb_create(arena);

    FlString hello = FL_S("Hello");
    FlString world = FL_S(" World");

    sb = fl_sb_append_string(sb, hello);
    expect_sb_length(utest_result, sb, 5UL);

    sb = fl_sb_append_string(sb, world);
    expect_sb_length(utest_result, sb, 11UL);

    expect_string_length(utest_result, fl_sb_to_string(sb), 11UL);

    fl_arena_destroy(arena);
}

UTEST(string_builder, fl_sb_append_char) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlStringBuilder sb = fl_sb_create(arena);

    sb = fl_sb_append_char(sb, 'A');
    sb = fl_sb_append_char(sb, 'B');
    sb = fl_sb_append_char(sb, 'C');

    expect_sb_length(utest_result, sb, 3UL);

    expect_sb_text(utest_result, sb, FL_S("ABC"), 3UL);

    fl_arena_destroy(arena);
}

UTEST(string_builder, fl_sb_appendf) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlStringBuilder sb = fl_sb_create(arena);

    sb = fl_sb_appendf(sb, "Number: %d", 42);
    expect_sb_length(utest_result, sb, 10UL);

    sb = fl_sb_appendf(sb, ", Float: %.2f", 3.14f);

    FlString result = fl_sb_to_string(sb);
    ASSERT_TRUE(fl_string_begins_with(result, FL_S("Number: 42")));

    fl_arena_destroy(arena);
}

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

UTEST(string_builder, fl_sb_clear) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlStringBuilder sb = fl_sb_create(arena);

    sb = fl_sb_append_cstr(sb, "Hello World");
    expect_sb_length(utest_result, sb, 11UL);

    sb = fl_sb_clear(sb);
    expect_sb_length(utest_result, sb, 0UL);

    sb = fl_sb_append_cstr(sb, "New");
    expect_sb_length(utest_result, sb, 3UL);

    FlString result = fl_sb_to_string(sb);
    ASSERT_TRUE(fl_string_begins_with(result, FL_S("New")));

    fl_arena_destroy(arena);
}

UTEST(string_builder, fl_sb_to_cstr) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlStringBuilder sb = fl_sb_create(arena);

    sb = fl_sb_append_cstr(sb, "Test FlString");

    const char* cstr = fl_sb_to_cstr(sb);
    ASSERT_STREQ(cstr, "Test FlString");

    fl_arena_destroy(arena);
}

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

UTEST(string_search, string_find_char_basic) {
    FlString str = FL_S("hello world");

    expect_find_char(utest_result, str, 'h', 0);
    expect_find_char(utest_result, str, 'e', 1);
    expect_find_char(utest_result, str, 'l', 2);
    expect_find_char(utest_result, str, ' ', 5);
    expect_find_char(utest_result, str, 'd', 10);

    expect_find_char(utest_result, str, 'x', -1);
    expect_find_char(utest_result, str, 'H', -1);
    expect_find_char(utest_result, str, '\0', -1);
}

UTEST(string_search, string_find_char_edge_cases) {
    FlString empty_str = FL_S("");
    FlString single_char = FL_S("a");

    expect_find_char(utest_result, empty_str, 'a', -1);

    expect_find_char(utest_result, single_char, 'a', 0);
    expect_find_char(utest_result, single_char, 'b', -1);
}

UTEST(string_search, string_find_char_url_parsing) {
    FlString http_url = FL_S("https://example.com/path");

    expect_find_char(utest_result, http_url, ':', 5);
    expect_find_char(utest_result, http_url, '/', 6);
    expect_find_char(utest_result, http_url, '.', 15);
}

UTEST(string_search, string_find_substr_basic) {
    FlString str = FL_S("hello world, hello universe");

    expect_find_substr(utest_result, str, FL_S("hello"), 0);
    expect_find_substr(utest_result, str, FL_S("world"), 6);
    expect_find_substr(utest_result, str, FL_S("universe"), 19);
    expect_find_substr(utest_result, str, FL_S("o w"), 4);

    expect_find_substr(utest_result, str, FL_S("goodbye"), -1);
    expect_find_substr(utest_result, str, FL_S("Hello"), -1);
    expect_find_substr(utest_result, str, FL_S("hello world, hello universe!"), -1);
}

UTEST(string_search, string_find_substr_edge_cases) {
    FlString str = FL_S("test string");
    FlString empty_str = FL_S("");

    expect_find_substr(utest_result, str, FL_S(""), 0);
    expect_find_substr(utest_result, empty_str, FL_S(""), 0);

    expect_find_substr(utest_result, empty_str, FL_S("test"), -1);

    FlString exact = FL_S("exact");
    expect_find_substr(utest_result, exact, FL_S("exact"), 0);

    expect_find_substr(utest_result, str, FL_S("t"), 0);
}

UTEST(string_search, string_find_substr_protocols) {
    FlString http_url = FL_S("http://example.com");
    FlString https_url = FL_S("https://secure.example.com");
    FlString ftp_url = FL_S("ftp://files.example.com");

    expect_find_substr(utest_result, http_url, FL_S("://"), 4);
    expect_find_substr(utest_result, https_url, FL_S("://"), 5);
    expect_find_substr(utest_result, ftp_url, FL_S("://"), 3);

    expect_find_substr(utest_result, http_url, FL_S("example"), 7);
    expect_find_substr(utest_result, https_url, FL_S("secure"), 8);
    expect_find_substr(utest_result, ftp_url, FL_S("files"), 6);
}

UTEST(string_search, string_find_char_from_basic) {
    FlString str = FL_S("hello world hello");

    expect_index(utest_result, fl_string_find_char_from(str, 'l', 0), 2);
    expect_index(utest_result, fl_string_find_char_from(str, 'l', 3), 3);
    expect_index(utest_result, fl_string_find_char_from(str, 'l', 4), 9);
    expect_index(utest_result, fl_string_find_char_from(str, 'l', 10), 14);
    expect_index(utest_result, fl_string_find_char_from(str, 'l', 15), 15);

    expect_index(utest_result, fl_string_find_char_from(str, 'x', 0), -1);
    expect_index(utest_result, fl_string_find_char_from(str, 'h', 1), 12);
}

UTEST(string_search, string_find_char_from_edge_cases) {
    FlString str = FL_S("test");

    expect_index(utest_result, fl_string_find_char_from(str, 't', 4), -1);
    expect_index(utest_result, fl_string_find_char_from(str, 't', 5), -1);
    expect_index(utest_result, fl_string_find_char_from(str, 't', 3), 3);

    FlString empty = FL_S("");
    expect_index(utest_result, fl_string_find_char_from(empty, 'a', 0), -1);
}

UTEST(string_search, string_find_char_from_url_parsing) {
    FlString url = FL_S("https://example.com/path/to/file.txt");

    i64 protocol_end = fl_string_find_substr(url, FL_S("://"));
    ASSERT_EQ(protocol_end, 5);

    i64 first_path_slash = fl_string_find_char_from(url, '/', protocol_end + 3);
    ASSERT_EQ(first_path_slash, 19);

    i64 second_path_slash = fl_string_find_char_from(url, '/', first_path_slash + 1);
    ASSERT_EQ(second_path_slash, 24);
}

UTEST(string_search, string_contains_basic) {
    FlString str = FL_S("The quick brown fox jumps over the lazy dog");

    expect_contains(utest_result, str, FL_S("quick"), true);
    expect_contains(utest_result, str, FL_S("brown fox"), true);
    expect_contains(utest_result, str, FL_S("The"), true);
    expect_contains(utest_result, str, FL_S("dog"), true);
    expect_contains(utest_result, str, FL_S("over the"), true);

    expect_contains(utest_result, str, FL_S("cat"), false);
    expect_contains(utest_result, str, FL_S("THE"), false);
    expect_contains(utest_result, str, FL_S("elephant"), false);
}

UTEST(string_search, string_contains_edge_cases) {
    FlString str = FL_S("test string");
    FlString empty_str = FL_S("");

    expect_contains(utest_result, str, FL_S(""), true);
    expect_contains(utest_result, empty_str, FL_S(""), true);

    expect_contains(utest_result, empty_str, FL_S("test"), false);

    expect_contains(utest_result, str, str, true);
}

UTEST(string_search, string_contains_url_components) {
    FlString url = FL_S("https://api.example.com:8443/v1/users?id=123&format=json");

    expect_contains(utest_result, url, FL_S("https://"), true);
    expect_contains(utest_result, url, FL_S("http://"), false);

    expect_contains(utest_result, url, FL_S("api.example.com"), true);
    expect_contains(utest_result, url, FL_S("example"), true);

    expect_contains(utest_result, url, FL_S(":8443"), true);

    expect_contains(utest_result, url, FL_S("/v1/users"), true);
    expect_contains(utest_result, url, FL_S("id=123"), true);
    expect_contains(utest_result, url, FL_S("format=json"), true);

    expect_contains(utest_result, url, FL_S(":80"), false);
    expect_contains(utest_result, url, FL_S("admin"), false);
}

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

    expect_find_substr(utest_result, local_path, FL_S("://"), -1);
}

UTEST(string_substr, clamps_to_the_end) {
    FlString str = FL_S("hello world");

    expect_string(utest_result, fl_string_substr(str, 0, 5), FL_S("hello"));
    expect_string(utest_result, fl_string_substr(str, 6, 5), FL_S("world"));
    expect_string(utest_result, fl_string_substr(str, 6, 100), FL_S("world"));
    expect_string_length(utest_result, fl_string_substr(str, 11, 1), 0);
    expect_string_length(utest_result, fl_string_substr(str, 3, 0), 0);
}

UTEST(string_substr, length_near_u64_max_clamps) {
    FlString str = FL_S("hello world");

    // start + length wraps around u64 here, which must still clamp to the end of the string
    expect_string(utest_result, fl_string_substr(str, 6, UINT64_MAX), FL_S("world"));
    expect_string(utest_result, fl_string_substr(str, 2, UINT64_MAX - 1), FL_S("llo world"));
    expect_string(utest_result, fl_string_substr(str, 10, UINT64_MAX), FL_S("d"));
}
