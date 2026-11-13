#include "utest/utest.h"
#include <core/arena.h>
#include <core/json.h>
#include <core/json_builder.h>
#include <core/string.h>
#include <string.h>

#define ASSERT_ERROR_EQ(actual, expected) ASSERT_EQ((int)(actual), (int)(expected))
#define ASSERT_ERROR_NE(actual, expected) ASSERT_NE((int)(actual), (int)(expected))
#define ASSERT_STR_EQ(actual, expected) ASSERT_TRUE(fl_string_equals((actual), S(expected)))

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlJsonValue* parse_strict(FlArena* arena, FlString text) {
    return fl_json_parse_with_flags(arena, text, FlJsonParseFlags_Default, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlJsonValue* parse_json5(FlArena* arena, FlString text) {
    return fl_json_parse_with_flags(arena, text, FlJsonParseFlags_AllowJson5, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString write_pretty(FlArena* arena, const FlJsonValue* value) {
    return fl_json_write_pretty(arena, value, "  ", "\n");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlJsonParseResult parse_error(FlArena* arena, FlString text, size_t flags) {
    FlJsonParseResult result;
    FlJsonValue* value = fl_json_parse_with_flags(arena, text, flags, &result);
    if (value != nullptr) {
        result.error = FlJsonParseError_None;
    }
    return result;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool is_rejected(FlArena* arena, FlString text, size_t flags) {
    FlJsonParseResult result;
    FlJsonValue* value = fl_json_parse_with_flags(arena, text, flags, &result);
    return value == nullptr && result.error != FlJsonParseError_None;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool is_accepted(FlArena* arena, FlString text, size_t flags) {
    FlJsonParseResult result;
    FlJsonValue* value = fl_json_parse_with_flags(arena, text, flags, &result);
    return value != nullptr && result.error == FlJsonParseError_None;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, parse_basic_types) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const struct {
        FlString text;
        FlJsonType type;
    } scalars[] = {
        { S_("null"), FlJsonType_Null },
        { S_("true"), FlJsonType_True },
        { S_("false"), FlJsonType_False },
        { S_("  42  "), FlJsonType_Number },
    };
    for (size_t i = 0; i < sizeof_array(scalars); i++) {
        ASSERT_EQ((int)parse_strict(arena, scalars[i].text)->type, (int)scalars[i].type);
    }

    FlJsonValue* text = parse_strict(arena, S("\"hello\""));
    ASSERT_EQ((int)text->type, (int)FlJsonType_String);
    ASSERT_STR_EQ(fl_json_value_as_string(text), "hello");

    FlJsonValue* number = parse_strict(arena, S("-12.5e3"));
    FlJsonNumber* payload = (FlJsonNumber*)number->payload;
    ASSERT_EQ((int)payload->number_size, 7);
    ASSERT_EQ(0, strcmp(payload->number, "-12.5e3"));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, parse_arrays_and_objects) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* root = parse_strict(arena, S("{\"list\": [1, \"two\", [], {}], \"name\": \"x\"}"));
    ASSERT_NE(root, nullptr);
    ASSERT_EQ((int)fl_json_value_as_object(root)->length, 2);

    FlJsonArray* list = fl_json_get_array_prop(root, S("list"));
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

    ASSERT_EQ(fl_json_get_array_prop(root, S("name")), nullptr);
    ASSERT_EQ(fl_json_get_object_prop(root, S("list")), nullptr);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, strict_rejects_malformed_input) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString cases[] = {
        S_("{\"key\": }"),
        S_("{\"key\" \"value\"}"),
        S_("{\"key\": \"value\" \"extra\"}"),
        S_("\"unclosed string"),
        S_("{\"key\": 123.45.67}"),
        S_("{\"key\": \"value\""),
        S_("{} extra content"),
        S_("invalid"),
        S_(""),
        S_("[1, 2,}"),
        S_("{\"a\": 1]"),
        S_("[1, 2, 3"),
        S_("{\"a\": 1, \"b\""),
        S_("[,]"),
        S_("{,}"),
        S_("[1,, 2]"),
        S_("{\"a\":, \"b\": 1}"),
        S_("{\"a\": 1 \"b\": 2}"),
        S_("[1,]"),
        S_("{\"a\": 1,}"),
    };
    for (size_t i = 0; i < sizeof_array(cases); i++) {
        ASSERT_TRUE(is_rejected(arena, cases[i], FlJsonParseFlags_Default));
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, strict_rejects_malformed_strings) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString cases[] = {
        S_("\"\\\""),  S_("\"\\u123\""),  S_("\"\\u123G\""), S_("\"line1\nline2\""),
        S_("\"\\q\""), S_("\"\\ud83d\""), S_("\"\\udd25\""), S_("'single'"),
    };
    for (size_t i = 0; i < sizeof_array(cases); i++) {
        ASSERT_TRUE(is_rejected(arena, cases[i], FlJsonParseFlags_Default));
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, strict_rejects_malformed_numbers) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString cases[] = {
        S_("123.456.789"), S_("123e"),    S_("123e+"), S_("123e-"),    S_("123.e"), S_(".123.456"),
        S_("123ee10"),     S_("1.2.3e4"), S_("01"),    S_("-"),        S_("+1"),    S_(".5"),
        S_("5."),          S_("0xFF"),    S_("NaN"),   S_("Infinity"),
    };
    for (size_t i = 0; i < sizeof_array(cases); i++) {
        ASSERT_TRUE(is_rejected(arena, cases[i], FlJsonParseFlags_Default));
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, accepts_edge_cases) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString cases[] = {
        S_("123456789012345678901234567890.123456789"),
        S_("[[[[[[[]]]]]]]"),
        S_("\"\""),
        S_("{\"a\": [ ] , \"b\" : { } }"),
    };
    for (size_t i = 0; i < sizeof_array(cases); i++) {
        ASSERT_TRUE(is_accepted(arena, cases[i], FlJsonParseFlags_Default));
    }

    ASSERT_EQ((u64)fl_json_value_as_string(parse_strict(arena, S("\"\""))).length, 0u);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, decodes_string_escapes) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* simple = parse_strict(arena, S("\"a\\n\\t\\\"\\\\\\/\\b\\f\\r\""));
    ASSERT_EQ((int)fl_json_value_as_string(simple).length, 9);
    ASSERT_EQ(0, memcmp(fl_json_value_as_string(simple).data, "a\n\t\"\\/\b\f\r", 9));

    FlJsonValue* unicode = parse_strict(arena, S("\"\\u0048\\u00e9\\u20ac\\ud83d\\udd25\""));
    FlString decoded = fl_json_value_as_string(unicode);
    const char expected[] = "H\xc3\xa9\xe2\x82\xac\xf0\x9f\x94\xa5";
    ASSERT_EQ((int)decoded.length, (int)(sizeof(expected) - 1));
    ASSERT_EQ(0, memcmp(decoded.data, expected, sizeof(expected) - 1));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, json5_accepts_extensions) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString cases[] = {
        S_("{a: [1, 2, 3,]}"),
        S_("{a: 1, b_2: 2,}"),
        S_("{'a': 'single', \"b\": 'its'}"),
        S_("// line\n/* block\n */ {\"a\": 1 // tail\n}"),
        S_("{\"a\": \"line1\nline2\"}"),
        S_("{\"a\": [0xFF, +123, .5, 5., -.5, Infinity, -Infinity, NaN]}"),
    };
    for (size_t i = 0; i < sizeof_array(cases); i++) {
        ASSERT_TRUE(is_accepted(arena, cases[i], FlJsonParseFlags_AllowJson5));
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, json5_still_rejects_malformed_input) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString cases[] = {
        S_("{a 1}"),  S_("{a: }"),   S_("{: 1}"),           S_("{\"a\": 1 \"b\": 2}"),
        S_("[1 2]"),  S_("{a: 0x}"), S_("{a: 1e}"),         S_("{a: NaN1}"),
        S_("{a: .}"), S_("{a: 'x}"), S_("{a: 1} trailing"), S_("{a: /* unterminated"),
    };
    for (size_t i = 0; i < sizeof_array(cases); i++) {
        ASSERT_TRUE(is_rejected(arena, cases[i], FlJsonParseFlags_AllowJson5));
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, json5_parses_a_global_object) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* root
        = parse_json5(arena, S("// config\nname: \"test\", value: 42,\ntheme: { accent: [255, 107, 53] }\n"));
    ASSERT_NE(root, nullptr);
    ASSERT_EQ((int)root->type, (int)FlJsonType_Object);
    ASSERT_STR_EQ(fl_json_get_string_prop(root, S("name")), "test");

    FlJsonValue* empty = parse_json5(arena, S("  // nothing\n"));
    ASSERT_NE(empty, nullptr);
    ASSERT_EQ((int)fl_json_value_as_object(empty)->length, 0);

    FlJsonValue* braced = parse_json5(arena, S("{\"a\": 1}"));
    ASSERT_EQ((int)fl_json_value_as_object(braced)->length, 1);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, json5_global_object_members) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* root = parse_json5(arena, S("value: 42, theme: { accent: [255, 107, 53] }"));
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(fl_json_get_u32_prop(root, S("value")), 42u);
    ASSERT_NE(fl_json_get_object_prop(root, S("theme")), nullptr);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, reports_the_failure_position) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonParseResult colon = parse_error(arena, S("{\n  \"a\": 1,\n  \"b\" 2\n}"), FlJsonParseFlags_Default);
    ASSERT_ERROR_EQ(colon.error, FlJsonParseError_ExpectedColon);
    ASSERT_EQ((int)colon.error_line_no, 3);
    ASSERT_EQ((int)colon.error_row_no, 7);

    FlJsonParseResult trailing = parse_error(arena, S("[] x"), FlJsonParseFlags_Default);
    ASSERT_ERROR_EQ(trailing.error, FlJsonParseError_UnexpectedTrailingCharacters);
    ASSERT_EQ((int)trailing.error_offset, 3);

    ASSERT_TRUE(is_rejected(arena, S(""), FlJsonParseFlags_Default));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, failed_parse_releases_its_allocations) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    void* before = fl_arena_aligned_pos(arena, 1);
    ASSERT_EQ(parse_strict(arena, S("{\"a\": [1, 2, {\"b\": \"c\"}], \"d\": nope}")), nullptr);
    ASSERT_EQ(fl_arena_aligned_pos(arena, 1), before);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, find_property_matches_the_full_key) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* root = parse_strict(arena, S("{\"a\\u0000c\": 1, \"a\\u0000b\": 2, \"key\": \"v\"}"));

    FlString nul_key = { .data = "a\0b", .length = 3 };
    FlJsonValue* found = fl_json_find_property(root, nul_key);
    ASSERT_NE(found, nullptr);
    ASSERT_EQ(((FlJsonNumber*)found->payload)->number[0], '2');
    ASSERT_EQ(fl_json_find_property(root, S("a")), nullptr);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, find_property_tolerates_non_objects) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    ASSERT_EQ(fl_json_find_property(nullptr, S("key")), nullptr);
    ASSERT_EQ(fl_json_find_property(parse_strict(arena, S("7")), S("key")), nullptr);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, string_property_defaults_to_empty) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* root = parse_strict(arena, S("{\"a\": 1, \"key\": \"v\"}"));
    ASSERT_STR_EQ(fl_json_get_string_prop(root, S("key")), "v");
    ASSERT_EQ((int)fl_json_get_string_prop(root, S("a")).length, 0);
    ASSERT_EQ((int)fl_json_get_string_prop(root, S("missing")).length, 0);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, typed_number_properties) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* root = parse_json5(arena, S("hex: 0x1F, neg: -7, big: 99999999999, huge: 99999999999999999999999, "
                                             "plus: +3, real: 2.5, text: \"9\""));
    ASSERT_NE(root, nullptr);

    const struct {
        FlString name;
        u32 expected;
    } as_u32[] = {
        { S_("hex"), 31u }, { S_("plus"), 3u }, { S_("neg"), 0u },     { S_("big"), 0u },
        { S_("huge"), 0u }, { S_("text"), 0u }, { S_("missing"), 0u },
    };
    for (size_t i = 0; i < sizeof_array(as_u32); i++) {
        ASSERT_EQ(fl_json_get_u32_prop(root, as_u32[i].name), as_u32[i].expected);
    }

    const struct {
        FlString name;
        f64 expected;
    } as_f64[] = {
        { S_("real"), 2.5 }, { S_("neg"), -7.0 }, { S_("hex"), 31.0 }, { S_("text"), 0.0 }, { S_("missing"), 0.0 },
    };
    for (size_t i = 0; i < sizeof_array(as_f64); i++) {
        ASSERT_EQ(fl_json_get_f64_prop(root, as_f64[i].name), as_f64[i].expected);
    }

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

    ASSERT_EQ(fl_json_get_u32_prop(&object, S("n")), 12u);
    ASSERT_EQ(fl_json_get_f64_prop(&object, S("n")), 12.0);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, error_descriptions_are_distinct) {
    const struct {
        FlJsonParseError error;
        FlString description;
    } cases[] = {
        { FlJsonParseError_None, S_("No error") },
        { FlJsonParseError_ExpectedColon, S_("Expected colon after object key") },
        { FlJsonParseError_InvalidNumberFormat, S_("Invalid number format") },
        { FlJsonParseError_TooDeep, S_("JSON nesting exceeds maximum depth") },
        { (FlJsonParseError)999, S_("Unknown JSON parse error") },
    };
    for (size_t i = 0; i < sizeof_array(cases); i++) {
        ASSERT_TRUE(fl_string_equals(fl_json_get_error_description(cases[i].error), cases[i].description));
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_writer, pretty_output_layout) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* root = parse_strict(arena, S("{\"a\":[1,true,null],\"b\":{},\"c\":[],\"d\":\"x\\ny\"}"));
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

    FlString tabbed = fl_json_write_pretty(arena, parse_strict(arena, S("[1]")), "\t", "\r\n");
    ASSERT_EQ(0, memcmp(tabbed.data, "[\r\n\t1\r\n]", tabbed.length));

    FlString defaults = fl_json_write_pretty(arena, parse_strict(arena, S("[1]")), nullptr, nullptr);
    ASSERT_EQ(0, memcmp(defaults.data, "[\n  1\n]", defaults.length));

    ASSERT_EQ((int)fl_json_write_pretty(arena, nullptr, "  ", "\n").length, 0);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_writer, normalizes_json5_numbers) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* root = parse_json5(arena, S("n: [0x0, 0xFF, +5, .5, -.5, 5., Infinity, -Infinity, NaN, 1.5e3]"));
    FlString out = fl_json_write_pretty(arena, root, "", " ");
    const char expected[]
        = "{ \"n\" : [ 0, 255, 5, 0.5, -0.5, 5.0, 1.7976931348623158e308, -1.7976931348623158e308, 0, 1.5e3 ] }";
    ASSERT_EQ((int)out.length, (int)(sizeof(expected) - 1));
    ASSERT_EQ(0, memcmp(out.data, expected, sizeof(expected) - 1));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_writer, output_round_trips_through_the_parser) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* original
        = parse_json5(arena, S("name: 'a\"b\\\\c', list: [1, 2.5, [true, false, null]], nested: { k: 'v' }"));
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
    json_build_set_int(arena, root, S("count"), -3);
    json_build_set_float(arena, root, S("ratio"), 0.5);
    json_build_set_bool(arena, root, S("on"), true);
    json_build_set_null(arena, root, S("none"));
    json_build_set_string(arena, root, S("name"), S("flowi"));
    FlJsonValue* list = json_build_array(arena);
    json_build_array_push(arena, list, json_build_object(arena));
    json_build_set_value(arena, root, S("list"), list);

    FlString written = write_pretty(arena, root);
    FlJsonValue* parsed = fl_json_parse_with_flags(arena, written, FlJsonParseFlags_Default, nullptr);
    ASSERT_NE(parsed, nullptr);
    ASSERT_STR_EQ(fl_json_get_string_prop(parsed, S("name")), "flowi");
    ASSERT_EQ(fl_json_get_f64_prop(parsed, S("ratio")), 0.5);

    FlJsonArray* items = fl_json_get_array_prop(parsed, S("list"));
    ASSERT_NE(items, nullptr);
    ASSERT_EQ((int)items->length, 1);

    fl_arena_destroy(arena);
}
