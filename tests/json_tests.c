#include "utest/utest.h"
#include <core/arena.h>
#include <core/json.h>
#include <core/json_builder.h>
#include <core/string.h>
#include <string.h>

#define ASSERT_ERROR_EQ(actual, expected) ASSERT_EQ((int)(actual), (int)(expected))
#define ASSERT_ERROR_NE(actual, expected) ASSERT_NE((int)(actual), (int)(expected))
#define ASSERT_STR_EQ(actual, expected) \
    ASSERT_TRUE(fl_string_equals((actual), FL_S(expected)))

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlJsonValue* parse_strict(FlArena* arena, const char* text) {
    return fl_json_parse_with_flags(arena, string_from_cstr(text), FlJsonParseFlags_Default, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlJsonValue* parse_json5(FlArena* arena, const char* text) {
    return fl_json_parse_with_flags(arena, string_from_cstr(text), FlJsonParseFlags_AllowJson5, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString write_pretty(FlArena* arena, const FlJsonValue* value) {
    return fl_json_write_pretty(arena, value, "  ", "\n");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool is_rejected(FlArena* arena, const char* text, size_t flags) {
    FlJsonParseResult result;
    FlJsonValue* value = fl_json_parse_with_flags(arena, string_from_cstr(text), flags, &result);
    return value == nullptr && result.error != FlJsonParseError_None;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool is_accepted(FlArena* arena, const char* text, size_t flags) {
    FlJsonParseResult result;
    FlJsonValue* value = fl_json_parse_with_flags(arena, string_from_cstr(text), flags, &result);
    return value != nullptr && result.error == FlJsonParseError_None;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, parse_basic_types) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    ASSERT_EQ((int)parse_strict(arena, "null")->type, (int)FlJsonType_Null);
    ASSERT_EQ((int)parse_strict(arena, "true")->type, (int)FlJsonType_True);
    ASSERT_EQ((int)parse_strict(arena, "false")->type, (int)FlJsonType_False);
    ASSERT_EQ((int)parse_strict(arena, "  42  ")->type, (int)FlJsonType_Number);

    FlJsonValue* text = parse_strict(arena, "\"hello\"");
    ASSERT_EQ((int)text->type, (int)FlJsonType_String);
    ASSERT_STR_EQ(fl_json_value_as_string(text), "hello");

    FlJsonValue* number = parse_strict(arena, "-12.5e3");
    FlJsonNumber* payload = (FlJsonNumber*)number->payload;
    ASSERT_EQ((int)payload->number_size, 7);
    ASSERT_EQ(0, strcmp(payload->number, "-12.5e3"));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, parse_arrays_and_objects) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* root = parse_strict(arena, "{\"list\": [1, \"two\", [], {}], \"name\": \"x\"}");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ((int)fl_json_value_as_object(root)->length, 2);

    FlJsonArray* list = fl_json_get_array_prop(root, FL_S("list"));
    ASSERT_NE(list, nullptr);
    ASSERT_EQ((int)list->length, 4);

    u32 visited = 0;
    for_each_json_array_item(list, item, index) {
        ASSERT_EQ((u32)index, visited);
        visited++;
        (void)item;
    }
    ASSERT_EQ(visited, 4u);

    u32 members = 0;
    for_each_json_property(root, name, value) {
        members++;
        (void)name;
        (void)value;
    }
    ASSERT_EQ(members, 2u);

    ASSERT_EQ(fl_json_get_array_prop(root, FL_S("name")), nullptr);
    ASSERT_EQ(fl_json_get_object_prop(root, FL_S("list")), nullptr);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, strict_rejects_malformed_input) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* cases[] = {
        "{\"key\": }",       "{\"key\" \"value\"}", "{\"key\": \"value\" \"extra\"}", "\"unclosed string",
        "{\"key\": 123.45.67}", "{\"key\": \"value\"", "{} extra content",          "invalid",
        "",                  "[1, 2,}",           "{\"a\": 1]",                    "[1, 2, 3",
        "{\"a\": 1, \"b\"",  "[,]",               "{,}",                           "[1,, 2]",
        "{\"a\":, \"b\": 1}", "{\"a\": 1 \"b\": 2}", "[1,]",                         "{\"a\": 1,}",
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ASSERT_TRUE(is_rejected(arena, cases[i], FlJsonParseFlags_Default));
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, strict_rejects_malformed_strings) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* cases[] = {
        "\"\\\"", "\"\\u123\"", "\"\\u123G\"", "\"line1\nline2\"", "\"\\q\"", "\"\\ud83d\"", "\"\\udd25\"", "'single'",
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ASSERT_TRUE(is_rejected(arena, cases[i], FlJsonParseFlags_Default));
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, strict_rejects_malformed_numbers) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* cases[] = {
        "123.456.789", "123e", "123e+", "123e-", "123.e", ".123.456", "123ee10", "1.2.3e4",
        "01",          "-",    "+1",    ".5",    "5.",    "0xFF",     "NaN",     "Infinity",
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ASSERT_TRUE(is_rejected(arena, cases[i], FlJsonParseFlags_Default));
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, accepts_edge_cases) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    ASSERT_TRUE(is_accepted(arena, "123456789012345678901234567890.123456789", FlJsonParseFlags_Default));
    ASSERT_TRUE(is_accepted(arena, "[[[[[[[]]]]]]]", FlJsonParseFlags_Default));
    ASSERT_TRUE(is_accepted(arena, "\"\"", FlJsonParseFlags_Default));
    ASSERT_TRUE(is_accepted(arena, "{\"a\": [ ] , \"b\" : { } }", FlJsonParseFlags_Default));

    ASSERT_EQ((u64)fl_json_value_as_string(parse_strict(arena, "\"\""))
                  .length,
              0u);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, decodes_string_escapes) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* simple = parse_strict(arena, "\"a\\n\\t\\\"\\\\\\/\\b\\f\\r\"");
    ASSERT_EQ((int)fl_json_value_as_string(simple).length, 9);
    ASSERT_EQ(0, memcmp(fl_json_value_as_string(simple).data, "a\n\t\"\\/\b\f\r", 9));

    FlJsonValue* unicode = parse_strict(arena, "\"\\u0048\\u00e9\\u20ac\\ud83d\\udd25\"");
    FlString decoded = fl_json_value_as_string(unicode);
    const char expected[] = "H\xc3\xa9\xe2\x82\xac\xf0\x9f\x94\xa5";
    ASSERT_EQ((int)decoded.length, (int)(sizeof(expected) - 1));
    ASSERT_EQ(0, memcmp(decoded.data, expected, sizeof(expected) - 1));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, json5_accepts_extensions) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    ASSERT_TRUE(is_accepted(arena, "{a: [1, 2, 3,]}", FlJsonParseFlags_AllowJson5));
    ASSERT_TRUE(is_accepted(arena, "{a: 1, b_2: 2,}", FlJsonParseFlags_AllowJson5));
    ASSERT_TRUE(is_accepted(arena, "{'a': 'single', \"b\": 'its'}", FlJsonParseFlags_AllowJson5));
    ASSERT_TRUE(is_accepted(arena, "// line\n/* block\n */ {\"a\": 1 // tail\n}", FlJsonParseFlags_AllowJson5));
    ASSERT_TRUE(is_accepted(arena, "{\"a\": \"line1\nline2\"}", FlJsonParseFlags_AllowJson5));
    ASSERT_TRUE(is_accepted(arena, "{\"a\": [0xFF, +123, .5, 5., -.5, Infinity, -Infinity, NaN]}", FlJsonParseFlags_AllowJson5));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, json5_still_rejects_malformed_input) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* cases[] = {
        "{a 1}", "{a: }", "{: 1}", "{\"a\": 1 \"b\": 2}", "[1 2]", "{a: 0x}", "{a: 1e}", "{a: NaN1}", "{a: .}", "{a: 'x}",
        "{a: 1} trailing", "{a: /* unterminated",
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ASSERT_TRUE(is_rejected(arena, cases[i], FlJsonParseFlags_AllowJson5));
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, json5_parses_a_global_object) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* root = parse_json5(arena, "// config\nname: \"test\", value: 42,\ntheme: { accent: [255, 107, 53] }\n");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ((int)root->type, (int)FlJsonType_Object);
    ASSERT_STR_EQ(fl_json_get_string_prop(root, FL_S("name")), "test");
    ASSERT_EQ(fl_json_get_u32_prop(root, FL_S("value")), 42u);
    ASSERT_NE(fl_json_get_object_prop(root, FL_S("theme")), nullptr);

    FlJsonValue* empty = parse_json5(arena, "  // nothing\n");
    ASSERT_NE(empty, nullptr);
    ASSERT_EQ((int)fl_json_value_as_object(empty)->length, 0);

    FlJsonValue* braced = parse_json5(arena, "{\"a\": 1}");
    ASSERT_EQ((int)fl_json_value_as_object(braced)->length, 1);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, reports_the_failure_position) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlJsonParseResult result;

    const char* text = "{\n  \"a\": 1,\n  \"b\" 2\n}";
    ASSERT_EQ(fl_json_parse_with_flags(arena, string_from_cstr(text), FlJsonParseFlags_Default, &result), nullptr);
    ASSERT_ERROR_EQ(result.error, FlJsonParseError_ExpectedColon);
    ASSERT_EQ((int)result.error_line_no, 3);
    ASSERT_EQ((int)result.error_row_no, 7);

    const char* trailing = "[] x";
    ASSERT_EQ(fl_json_parse_with_flags(arena, string_from_cstr(trailing), FlJsonParseFlags_Default, &result), nullptr);
    ASSERT_ERROR_EQ(result.error, FlJsonParseError_UnexpectedTrailingCharacters);
    ASSERT_EQ((int)result.error_offset, 3);

    ASSERT_EQ(fl_json_parse_with_flags(arena, string_empty(), FlJsonParseFlags_Default, &result), nullptr);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, failed_parse_releases_its_allocations) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    void* before = fl_arena_aligned_pos(arena, 1);
    ASSERT_EQ(parse_strict(arena, "{\"a\": [1, 2, {\"b\": \"c\"}], \"d\": nope}"), nullptr);
    ASSERT_EQ(fl_arena_aligned_pos(arena, 1), before);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, find_property_matches_the_full_key) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* root = parse_strict(arena, "{\"a\\u0000c\": 1, \"a\\u0000b\": 2, \"key\": \"v\"}");
    ASSERT_NE(root, nullptr);

    FlString nul_key = { .data = "a\0b", .length = 3 };
    FlJsonValue* found = fl_json_find_property(root, nul_key);
    ASSERT_NE(found, nullptr);
    ASSERT_EQ(((FlJsonNumber*)found->payload)->number[0], '2');
    ASSERT_EQ(fl_json_find_property(root, FL_S("a")), nullptr);
    ASSERT_EQ(fl_json_find_property(nullptr, FL_S("key")), nullptr);
    ASSERT_EQ(fl_json_find_property(found, FL_S("key")), nullptr);
    ASSERT_STR_EQ(fl_json_get_string_prop(root, FL_S("key")), "v");
    ASSERT_EQ((int)fl_json_get_string_prop(root, FL_S("a")).length, 0);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, typed_number_properties) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* root = parse_json5(arena, "hex: 0x1F, neg: -7, big: 99999999999, huge: 99999999999999999999999, "
                                           "plus: +3, real: 2.5, text: \"9\"");
    ASSERT_NE(root, nullptr);

    ASSERT_EQ(fl_json_get_u32_prop(root, FL_S("hex")), 31u);
    ASSERT_EQ(fl_json_get_u32_prop(root, FL_S("plus")), 3u);
    ASSERT_EQ(fl_json_get_u32_prop(root, FL_S("neg")), 0u);
    ASSERT_EQ(fl_json_get_u32_prop(root, FL_S("big")), 0u);
    ASSERT_EQ(fl_json_get_u32_prop(root, FL_S("huge")), 0u);
    ASSERT_EQ(fl_json_get_u32_prop(root, FL_S("text")), 0u);
    ASSERT_EQ(fl_json_get_u32_prop(root, FL_S("missing")), 0u);

    ASSERT_EQ(fl_json_get_f64_prop(root, FL_S("real")), 2.5);
    ASSERT_EQ(fl_json_get_f64_prop(root, FL_S("neg")), -7.0);
    ASSERT_EQ(fl_json_get_f64_prop(root, FL_S("hex")), 31.0);
    ASSERT_EQ(fl_json_get_f64_prop(root, FL_S("text")), 0.0);
    ASSERT_EQ(fl_json_get_f64_prop(root, FL_S("missing")), 0.0);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, number_accessors_are_bounded_by_number_size) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char text[] = "1234";
    FlJsonNumber number = { .number = text, .number_size = 2 };
    FlJsonValue value = { .payload = &number, .type = FlJsonType_Number };
    FlJsonObjectElement member = { .name = &(FlString) { .data = "n", .length = 1 }, .value = &value };
    FlJsonObject members = { .start = &member, .length = 1 };
    FlJsonValue object = { .payload = &members, .type = FlJsonType_Object };

    ASSERT_EQ(fl_json_get_u32_prop(&object, FL_S("n")), 12u);
    ASSERT_EQ(fl_json_get_f64_prop(&object, FL_S("n")), 12.0);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, error_descriptions_are_distinct) {
    ASSERT_STR_EQ(fl_json_get_error_description(FlJsonParseError_None), "No error");
    ASSERT_STR_EQ(fl_json_get_error_description(FlJsonParseError_ExpectedColon), "Expected colon after object key");
    ASSERT_STR_EQ(fl_json_get_error_description(FlJsonParseError_InvalidNumberFormat), "Invalid number format");
    ASSERT_STR_EQ(fl_json_get_error_description(FlJsonParseError_TooDeep), "JSON nesting exceeds maximum depth");
    ASSERT_STR_EQ(fl_json_get_error_description((FlJsonParseError)999), "Unknown JSON parse error");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_writer, pretty_output_layout) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* root = parse_strict(arena, "{\"a\":[1,true,null],\"b\":{},\"c\":[],\"d\":\"x\\ny\"}");
    FlString out = write_pretty(arena, root);
    const char expected[] = "{\n"
                            "  \"a\" : [\n"
                            "    1,\n"
                            "    true,\n"
                            "    null\n"
                            "  ],\n"
                            "  \"b\" : {},\n"
                            "  \"c\" : [],\n"
                            "  \"d\" : \"x\\ny\"\n"
                            "}";
    ASSERT_EQ((int)out.length, (int)(sizeof(expected) - 1));
    ASSERT_EQ(0, memcmp(out.data, expected, sizeof(expected) - 1));

    FlString tabbed = fl_json_write_pretty(arena, parse_strict(arena, "[1]"), "\t", "\r\n");
    ASSERT_EQ(0, memcmp(tabbed.data, "[\r\n\t1\r\n]", tabbed.length));

    FlString defaults = fl_json_write_pretty(arena, parse_strict(arena, "[1]"), nullptr, nullptr);
    ASSERT_EQ(0, memcmp(defaults.data, "[\n  1\n]", defaults.length));

    ASSERT_EQ((int)fl_json_write_pretty(arena, nullptr, "  ", "\n").length, 0);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_writer, normalizes_json5_numbers) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* root = parse_json5(arena, "n: [0x0, 0xFF, +5, .5, -.5, 5., Infinity, -Infinity, NaN, 1.5e3]");
    FlString out = fl_json_write_pretty(arena, root, "", " ");
    const char expected[] = "{ \"n\" : [ 0, 255, 5, 0.5, -0.5, 5.0, 1.7976931348623158e308, -1.7976931348623158e308, 0, 1.5e3 ] }";
    ASSERT_EQ((int)out.length, (int)(sizeof(expected) - 1));
    ASSERT_EQ(0, memcmp(out.data, expected, sizeof(expected) - 1));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_writer, output_round_trips_through_the_parser) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* original = parse_json5(arena, "name: 'a\"b\\\\c', list: [1, 2.5, [true, false, null]], nested: { k: 'v' }");
    FlString first = write_pretty(arena, original);
    FlJsonValue* reparsed = fl_json_parse_with_flags(arena, first, FlJsonParseFlags_Default, nullptr);
    ASSERT_NE(reparsed, nullptr);
    FlString second = write_pretty(arena, reparsed);
    ASSERT_TRUE(fl_string_equals(first, second));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_writer, writes_builder_trees) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* root = json_build_object(arena);
    json_build_set_int(arena, root, FL_S("count"), -3);
    json_build_set_float(arena, root, FL_S("ratio"), 0.5);
    json_build_set_bool(arena, root, FL_S("on"), true);
    json_build_set_null(arena, root, FL_S("none"));
    json_build_set_string(arena, root, FL_S("name"), FL_S("flowi"));
    FlJsonValue* list = json_build_array(arena);
    json_build_array_push(arena, list, json_build_object(arena));
    json_build_set_value(arena, root, FL_S("list"), list);

    FlString written = write_pretty(arena, root);
    FlJsonValue* parsed
        = fl_json_parse_with_flags(arena, written, FlJsonParseFlags_Default, nullptr);
    ASSERT_NE(parsed, nullptr);
    ASSERT_STR_EQ(fl_json_get_string_prop(parsed, FL_S("name")), "flowi");
    ASSERT_EQ(fl_json_get_f64_prop(parsed, FL_S("ratio")), 0.5);
    ASSERT_EQ((int)fl_json_get_array_prop(parsed, FL_S("list"))->length, 1);

    fl_arena_destroy(arena);
}
