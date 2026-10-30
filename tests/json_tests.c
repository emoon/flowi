#include "utest/utest.h"
#include <core/arena.h>
#include <core/json.h>
#include <core/json_builder.h>
#include <core/string.h>
#include <string.h>
#if !defined(_WIN32)
#include <sys/mman.h>
#include <unistd.h>
#endif

#define ASSERT_ERROR_EQ(actual, expected) ASSERT_EQ((int)(actual), (int)(expected))
#define ASSERT_ERROR_NE(actual, expected) ASSERT_NE((int)(actual), (int)(expected))

UTEST(json_parser, parse_basic_types) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* null_val = fl_json_parse(arena, "null", 4);
    ASSERT_NE(null_val, nullptr);
    ASSERT_TRUE(fl_json_value_is_null(null_val));

    FlJsonValue* true_val = fl_json_parse(arena, "true", 4);
    ASSERT_NE(true_val, nullptr);
    ASSERT_TRUE(fl_json_value_is_true(true_val));

    FlJsonValue* false_val = fl_json_parse(arena, "false", 5);
    ASSERT_NE(false_val, nullptr);
    ASSERT_TRUE(fl_json_value_is_false(false_val));

    FlJsonValue* str_val = fl_json_parse(arena, "\"hello\"", 7);
    ASSERT_NE(str_val, nullptr);
    FlString str = fl_json_value_as_string(str_val);
    ASSERT_GT((u64)str.length, 0u);
    ASSERT_EQ((u64)str.length, 5u);

    FlJsonValue* num_val = fl_json_parse(arena, "42.5", 4);
    ASSERT_NE(num_val, nullptr);
    FlJsonNumber* num = fl_json_value_as_number(num_val);
    ASSERT_NE(num, nullptr);
    ASSERT_EQ(num->number_size, 4u);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, parse_arrays_objects) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* empty_arr = fl_json_parse(arena, "[]", 2);
    ASSERT_NE(empty_arr, nullptr);
    FlJsonArray* arr = fl_json_value_as_fixed_array(empty_arr);
    ASSERT_NE(arr, nullptr);
    ASSERT_EQ(arr->length, 0u);

    FlJsonValue* arr_val = fl_json_parse(arena, "[1, 2, 3]", 9);
    ASSERT_NE(arr_val, nullptr);
    arr = fl_json_value_as_fixed_array(arr_val);
    ASSERT_NE(arr, nullptr);
    ASSERT_EQ(arr->length, 3u);

    FlJsonValue* empty_obj = fl_json_parse(arena, "{}", 2);
    ASSERT_NE(empty_obj, nullptr);
    FlJsonObject* obj = fl_json_value_as_object(empty_obj);
    ASSERT_NE(obj, nullptr);
    ASSERT_EQ(obj->length, 0u);

    FlJsonValue* obj_val = fl_json_parse(arena, "{\"key\": \"value\"}", 16);
    ASSERT_NE(obj_val, nullptr);
    obj = fl_json_value_as_object(obj_val);
    ASSERT_NE(obj, nullptr);
    ASSERT_EQ(obj->length, 1u);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, parse_flags_trailing_comma) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlJsonParseResult result;

    FlJsonValue* val = fl_json_parse_with_flags(arena, "[1, 2,]", 7,
                                                FlJsonParseFlags_Default | FlJsonParseFlags_AllowLocationInformation
                                                    | FlJsonParseFlags_AllowLocationInformation,
                                                &result);
    ASSERT_EQ(val, nullptr);
    ASSERT_ERROR_NE(result.error, FlJsonParseError_None);

    val = fl_json_parse_with_flags(
        arena, "[1, 2,]", 7, FlJsonParseFlags_AllowTrailingComma | FlJsonParseFlags_AllowLocationInformation, &result);
    ASSERT_NE(val, nullptr);
    ASSERT_ERROR_EQ(result.error, FlJsonParseError_None);

    val = fl_json_parse_with_flags(arena, "{\"a\": 1,}", 9,
                                   FlJsonParseFlags_AllowTrailingComma | FlJsonParseFlags_AllowLocationInformation,
                                   &result);
    ASSERT_NE(val, nullptr);
    ASSERT_ERROR_EQ(result.error, FlJsonParseError_None);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, parse_flags_unquoted_keys) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlJsonParseResult result;

    const char* json = "{key: \"value\"}";
    FlJsonValue* val = fl_json_parse_with_flags(
        arena, json, strlen(json), FlJsonParseFlags_AllowUnquotedKeys | FlJsonParseFlags_AllowLocationInformation,
        &result);
    ASSERT_NE(val, nullptr);
    ASSERT_ERROR_EQ(result.error, FlJsonParseError_None);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, parse_flags_comments) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlJsonParseResult result;

    const char* json = "{\n"
                       "  // Single line comment\n"
                       "  \"key\": /* inline comment */ \"value\"\n"
                       "}";

    FlJsonValue* val = fl_json_parse_with_flags(
        arena, json, strlen(json), FlJsonParseFlags_AllowCStyleComments | FlJsonParseFlags_AllowLocationInformation,
        &result);
    ASSERT_NE(val, nullptr);
    ASSERT_ERROR_EQ(result.error, FlJsonParseError_None);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, parse_flags_single_quotes) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlJsonParseResult result;

    const char* json = "{'key': 'value'}";
    FlJsonValue* val
        = fl_json_parse_with_flags(arena, json, strlen(json), FlJsonParseFlags_AllowSingleQuotedStrings, &result);
    ASSERT_NE(val, nullptr);
    ASSERT_ERROR_EQ(result.error, FlJsonParseError_None);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, parse_flags_json5) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlJsonParseResult result;

    const char* json = "{\n"
                       "  // Comment\n"
                       "  'key': 0xFF,\n"
                       "  unquoted: true,\n"
                       "}";

    FlJsonValue* val = fl_json_parse_with_flags(arena, json, strlen(json), FlJsonParseFlags_AllowJson5, &result);
    ASSERT_NE(val, nullptr);
    ASSERT_ERROR_EQ(result.error, FlJsonParseError_None);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, error_conditions_basic) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlJsonParseResult result;

    const char* error_cases[] = {
        "{\"key\": }",
        "{\"key\" \"value\"}",
        "{\"key\": \"value\" \"extra\"}",
        "\"unclosed string",
        "{\"key\": 123.45.67}",
        "\"invalid\\z escape\"",
        "{\"key\": \"value\"",
        "\"unterminated",
        "{} extra content",
        "invalid",
        "",
    };

    for (size_t i = 0; i < sizeof(error_cases) / sizeof(error_cases[0]); i++) {
        FlJsonValue* val
            = fl_json_parse_with_flags(arena, error_cases[i], strlen(error_cases[i]),
                                       FlJsonParseFlags_Default | FlJsonParseFlags_AllowLocationInformation, &result);
        ASSERT_EQ(val, nullptr);
        ASSERT_ERROR_NE(result.error, FlJsonParseError_None);
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, error_conditions_strings) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlJsonParseResult result;

    const char* bad_strings[] = {
        "\"\\\"",           // Incomplete escape at end
        "\"\\u123\"",       // Incomplete unicode escape
        "\"\\u123G\"",      // Invalid unicode escape
        "\"line1\nline2\"", // Unescaped newline (without multiline flag)
        "\"\\q\"",          // Invalid escape sequence
    };

    for (size_t i = 0; i < sizeof(bad_strings) / sizeof(bad_strings[0]); i++) {
        FlJsonValue* val
            = fl_json_parse_with_flags(arena, bad_strings[i], strlen(bad_strings[i]),
                                       FlJsonParseFlags_Default | FlJsonParseFlags_AllowLocationInformation, &result);
        ASSERT_EQ(val, nullptr);
        ASSERT_ERROR_NE(result.error, FlJsonParseError_None);
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, error_conditions_numbers) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlJsonParseResult result;

    const char* bad_numbers[] = {
        "123.456.789", "123e", "123e+", "123e-", "123.e", ".123.456", "123ee10", "1.2.3e4",
    };

    for (size_t i = 0; i < sizeof(bad_numbers) / sizeof(bad_numbers[0]); i++) {
        FlJsonValue* val
            = fl_json_parse_with_flags(arena, bad_numbers[i], strlen(bad_numbers[i]),
                                       FlJsonParseFlags_Default | FlJsonParseFlags_AllowLocationInformation, &result);
        ASSERT_EQ(val, nullptr);
        ASSERT_ERROR_NE(result.error, FlJsonParseError_None);
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, error_conditions_structure) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlJsonParseResult result;

    const char* bad_structures[] = {
        "[1, 2,}", "{\"a\": 1]", "[1, 2, 3",           "{\"a\": 1, \"b\"",    "[,]",
        "{,}",     "[1,, 2]",    "{\"a\":, \"b\": 1}", "{\"a\": 1 \"b\": 2}",
    };

    for (size_t i = 0; i < sizeof(bad_structures) / sizeof(bad_structures[0]); i++) {
        FlJsonValue* val
            = fl_json_parse_with_flags(arena, bad_structures[i], strlen(bad_structures[i]),
                                       FlJsonParseFlags_Default | FlJsonParseFlags_AllowLocationInformation, &result);
        ASSERT_EQ(val, nullptr);
        ASSERT_ERROR_NE(result.error, FlJsonParseError_None);
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, edge_cases) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* big_num = "123456789012345678901234567890.123456789";
    FlJsonValue* val = fl_json_parse(arena, big_num, strlen(big_num));
    ASSERT_NE(val, nullptr);

    const char* nested = "[[[[[[[]]]]]]]";
    val = fl_json_parse(arena, nested, strlen(nested));
    ASSERT_NE(val, nullptr);

    const char* unicode = "\"\\u0048\\u0065\\u006C\\u006C\\u006F\""; // "Hello"
    val = fl_json_parse(arena, unicode, strlen(unicode));
    ASSERT_NE(val, nullptr);

    const char* empty_str = "\"\"";
    val = fl_json_parse(arena, empty_str, strlen(empty_str));
    ASSERT_NE(val, nullptr);
    FlString str = fl_json_value_as_string(val);
    ASSERT_EQ((u64)str.length, 0u);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, utility_functions) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* json = "{"
                       "\"str_prop\": \"hello\","
                       "\"num_prop\": 42,"
                       "\"bool_prop\": true,"
                       "\"false_prop\": false,"
                       "\"null_prop\": null,"
                       "\"obj_prop\": {\"nested\": \"value\"},"
                       "\"arr_prop\": [1, 2, 3]"
                       "}";

    FlJsonValue* root = fl_json_parse(arena, json, strlen(json));
    ASSERT_NE(root, nullptr);

    FlString str_val = fl_json_get_string_prop(root, FL_S("str_prop"));
    ASSERT_TRUE(fl_string_equals(str_val, FL_S("hello")));

    unsigned int num_val = fl_json_get_u32_prop(root, FL_S("num_prop"));
    ASSERT_EQ(num_val, 42u);

    double f64_val = fl_json_get_f64_prop(root, FL_S("num_prop"));
    ASSERT_EQ(f64_val, 42.0);

    int bool_val = fl_json_get_bool_prop(root, FL_S("bool_prop"));
    ASSERT_EQ(bool_val, 1);

    int false_val = fl_json_get_bool_prop(root, FL_S("false_prop"));
    ASSERT_EQ(false_val, 0);

    FlJsonValue* obj_val = fl_json_get_object_prop(root, FL_S("obj_prop"));
    ASSERT_NE(obj_val, nullptr);

    FlJsonArray* arr_val = fl_json_get_array_prop(root, FL_S("arr_prop"));
    ASSERT_NE(arr_val, nullptr);
    ASSERT_EQ(arr_val->length, 3u);

    FlString missing_str = fl_json_get_string_prop(root, FL_S("missing"));
    ASSERT_EQ((u64)missing_str.length, 0u);

    unsigned int missing_num = fl_json_get_u32_prop(root, FL_S("missing"));
    ASSERT_EQ(missing_num, 0u);

    FlJsonValue* missing_obj = fl_json_get_object_prop(root, FL_S("missing"));
    ASSERT_EQ(missing_obj, nullptr);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, array_object_navigation) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* json = "{"
                       "\"users\": ["
                       "  {\"name\": \"Alice\", \"age\": 30},"
                       "  {\"name\": \"Bob\", \"age\": 25}"
                       "]"
                       "}";

    FlJsonValue* root = fl_json_parse(arena, json, strlen(json));
    ASSERT_NE(root, nullptr);

    FlJsonArray* users = fl_json_get_array_prop(root, FL_S("users"));
    ASSERT_NE(users, nullptr);
    ASSERT_EQ(users->length, 2u);

    FlJsonArrayElement* elem = users->start;
    ASSERT_NE(elem, nullptr);

    FlString first_name = fl_json_get_string_prop(elem->value, FL_S("name"));
    ASSERT_TRUE(fl_string_equals(first_name, FL_S("Alice")));

    unsigned int first_age = fl_json_get_u32_prop(elem->value, FL_S("age"));
    ASSERT_EQ(first_age, 30u);

    elem = elem->next;
    ASSERT_NE(elem, nullptr);

    FlString second_name = fl_json_get_string_prop(elem->value, FL_S("name"));
    ASSERT_TRUE(fl_string_equals(second_name, FL_S("Bob")));

    unsigned int second_age = fl_json_get_u32_prop(elem->value, FL_S("age"));
    ASSERT_EQ(second_age, 25u);

    ASSERT_EQ(elem->next, nullptr);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, json_writing) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* original = "{\"key\":\"value\",\"number\":42}";
    FlJsonValue* val = fl_json_parse(arena, original, strlen(original));
    ASSERT_NE(val, nullptr);

    FlString minified = fl_json_write_minified(arena, val);
    ASSERT_GT((u64)minified.length, 0u);

    FlString pretty = fl_json_write_pretty(arena, val, "  ", "\n");
    ASSERT_GT((u64)pretty.length, 0u);
    ASSERT_GT((u64)pretty.length, (u64)minified.length);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, special_numbers) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlJsonParseResult result;

    FlJsonValue* hex = fl_json_parse_with_flags(arena, "0xFF", 4, FlJsonParseFlags_AllowHexadecimalNumbers, &result);
    ASSERT_NE(hex, nullptr);
    ASSERT_ERROR_EQ(result.error, FlJsonParseError_None);

    FlJsonValue* plus = fl_json_parse_with_flags(arena, "+123", 4, FlJsonParseFlags_AllowLeadingPlusSign, &result);
    ASSERT_NE(plus, nullptr);
    ASSERT_ERROR_EQ(result.error, FlJsonParseError_None);

    FlJsonValue* leading_dot
        = fl_json_parse_with_flags(arena, ".123", 4, FlJsonParseFlags_AllowLeadingOrTrailingDecimalPoint, &result);
    ASSERT_NE(leading_dot, nullptr);
    ASSERT_ERROR_EQ(result.error, FlJsonParseError_None);

    FlJsonValue* trailing_dot
        = fl_json_parse_with_flags(arena, "123.", 4, FlJsonParseFlags_AllowLeadingOrTrailingDecimalPoint, &result);
    ASSERT_NE(trailing_dot, nullptr);
    ASSERT_ERROR_EQ(result.error, FlJsonParseError_None);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, multiline_strings) {
    FlArena* arena = arena_new(.reserved_size = MB(64));
    FlJsonParseResult result;

    const char* multiline = "\"line1\nline2\nline3\"";
    FlJsonValue* val = fl_json_parse_with_flags(arena, multiline, strlen(multiline),
                                                FlJsonParseFlags_AllowMultiLineStrings, &result);
    ASSERT_NE(val, nullptr);
    ASSERT_ERROR_EQ(result.error, FlJsonParseError_None);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, fl_json_find_property) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* json = "{\"key1\": \"value1\", \"key2\": 42}";
    FlJsonValue* root = fl_json_parse(arena, json, strlen(json));
    ASSERT_NE(root, nullptr);

    FlJsonValue* found = fl_json_find_property(root, FL_S("key1"));
    ASSERT_NE(found, nullptr);
    FlString str = fl_json_value_as_string(found);
    ASSERT_GT((u64)str.length, 0u);

    FlJsonValue* not_found = fl_json_find_property(root, FL_S("nonexistent"));
    ASSERT_EQ(not_found, nullptr);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, type_checking) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* json = "{"
                       "\"str\": \"hello\","
                       "\"num\": 42,"
                       "\"bool_true\": true,"
                       "\"bool_false\": false,"
                       "\"null_val\": null,"
                       "\"obj\": {},"
                       "\"arr\": []"
                       "}";

    FlJsonValue* root = fl_json_parse(arena, json, strlen(json));
    ASSERT_NE(root, nullptr);

    FlJsonValue* str_prop = fl_json_find_property(root, FL_S("str"));
    ASSERT_NE(str_prop, nullptr);
    FlString str_test = fl_json_value_as_string(str_prop);
    ASSERT_GT((u64)str_test.length, 0u);
    ASSERT_EQ(fl_json_value_as_number(str_prop), nullptr);
    ASSERT_EQ(fl_json_value_as_object(str_prop), nullptr);
    ASSERT_EQ(fl_json_value_as_fixed_array(str_prop), nullptr);

    FlJsonValue* num_prop = fl_json_find_property(root, FL_S("num"));
    ASSERT_NE(num_prop, nullptr);
    ASSERT_NE(fl_json_value_as_number(num_prop), nullptr);
    FlString empty_str = fl_json_value_as_string(num_prop);
    ASSERT_EQ((u64)empty_str.length, 0u);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, position_tracking_basic) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* root = fl_json_parse(arena, "42", 2);
    ASSERT_NE(root, nullptr);
    ASSERT_TRUE(fl_json_has_position_info(root));
    ASSERT_EQ(fl_json_get_line_number(root), 1u);
    ASSERT_EQ(fl_json_get_column_number(root), 1u);

    root = fl_json_parse(arena, "\"hello\"", 7);
    ASSERT_NE(root, nullptr);
    ASSERT_TRUE(fl_json_has_position_info(root));
    ASSERT_EQ(fl_json_get_line_number(root), 1u);
    ASSERT_EQ(fl_json_get_column_number(root), 1u);

    root = fl_json_parse(arena, "true", 4);
    ASSERT_NE(root, nullptr);
    ASSERT_TRUE(fl_json_has_position_info(root));
    ASSERT_EQ(fl_json_get_line_number(root), 1u);
    ASSERT_EQ(fl_json_get_column_number(root), 1u);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, position_tracking_multiline) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* json = "{\n"                      // Line 1
                       "  \"key1\": 123,\n"       // Line 2, key1 at column 3, value at column 12
                       "  \"key2\": \"value\",\n" // Line 3, key2 at column 3, value at column 12
                       "  \"key3\": true\n"       // Line 4, key3 at column 3, value at column 12
                       "}";                       // Line 5

    FlJsonValue* root = fl_json_parse(arena, json, strlen(json));
    ASSERT_NE(root, nullptr);
    ASSERT_TRUE(fl_json_has_position_info(root));
    ASSERT_EQ(fl_json_get_line_number(root), 1u);

    FlJsonValue* key1 = fl_json_find_property(root, FL_S("key1"));
    ASSERT_NE(key1, nullptr);
    ASSERT_TRUE(fl_json_has_position_info(key1));
    ASSERT_EQ(fl_json_get_line_number(key1), 2u);

    FlJsonValue* key2 = fl_json_find_property(root, FL_S("key2"));
    ASSERT_NE(key2, nullptr);
    ASSERT_TRUE(fl_json_has_position_info(key2));
    ASSERT_EQ(fl_json_get_line_number(key2), 3u);

    FlJsonValue* key3 = fl_json_find_property(root, FL_S("key3"));
    ASSERT_NE(key3, nullptr);
    ASSERT_TRUE(fl_json_has_position_info(key3));
    ASSERT_EQ(fl_json_get_line_number(key3), 4u);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, position_tracking_arrays) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* json = "[\n"           // Line 1
                       "  123,\n"      // Line 2, element at column 3
                       "  \"test\",\n" // Line 3, element at column 3
                       "  true\n"      // Line 4, element at column 3
                       "]";            // Line 5

    FlJsonValue* root = fl_json_parse(arena, json, strlen(json));
    ASSERT_NE(root, nullptr);
    ASSERT_TRUE(fl_json_has_position_info(root));
    ASSERT_EQ(fl_json_get_line_number(root), 1u);

    FlJsonArray* array = fl_json_value_as_fixed_array(root);
    ASSERT_NE(array, nullptr);

    FlJsonArrayElement* elem = array->start;
    u32 expected_line = 2;

    while (elem) {
        ASSERT_TRUE(fl_json_has_position_info(elem->value));
        ASSERT_EQ(fl_json_get_line_number(elem->value), expected_line);
        elem = elem->next;
        expected_line++;
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, position_tracking_nested) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* json = "{\n"                // Line 1
                       "  \"outer\": {\n"   // Line 2
                       "    \"inner\": [\n" // Line 3
                       "      42,\n"        // Line 4
                       "      \"nested\"\n" // Line 5
                       "    ]\n"            // Line 6
                       "  }\n"              // Line 7
                       "}";                 // Line 8

    FlJsonValue* root = fl_json_parse(arena, json, strlen(json));
    ASSERT_NE(root, nullptr);
    ASSERT_TRUE(fl_json_has_position_info(root));

    FlJsonValue* outer = fl_json_find_property(root, FL_S("outer"));
    ASSERT_NE(outer, nullptr);
    ASSERT_TRUE(fl_json_has_position_info(outer));
    ASSERT_EQ(fl_json_get_line_number(outer), 2u);

    FlJsonValue* inner = fl_json_find_property(outer, FL_S("inner"));
    ASSERT_NE(inner, nullptr);
    ASSERT_TRUE(fl_json_has_position_info(inner));
    ASSERT_EQ(fl_json_get_line_number(inner), 3u);

    FlJsonArray* array = fl_json_value_as_fixed_array(inner);
    ASSERT_NE(array, nullptr);

    FlJsonArrayElement* elem = array->start;
    ASSERT_NE(elem, nullptr);
    ASSERT_TRUE(fl_json_has_position_info(elem->value));
    ASSERT_EQ(fl_json_get_line_number(elem->value), 4u);

    ASSERT_NE(elem->next, nullptr);
    ASSERT_TRUE(fl_json_has_position_info(elem->next->value));
    ASSERT_EQ(fl_json_get_line_number(elem->next->value), 5u);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, position_tracking_invalid_values) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    ASSERT_EQ(fl_json_get_line_number(nullptr), 0u);
    ASSERT_EQ(fl_json_get_column_number(nullptr), 0u);
    ASSERT_FALSE(fl_json_has_position_info(nullptr));

    FlJsonValue* root = fl_json_parse(arena, "null", 4);
    ASSERT_NE(root, nullptr);

    size_t line = fl_json_get_line_number(root);
    size_t column = fl_json_get_column_number(root);
    int has_info = fl_json_has_position_info(root);

    if (has_info) {
        ASSERT_GT(line, 0u);
        ASSERT_GT(column, 0u);
    } else {
        ASSERT_EQ(line, 0u);
        ASSERT_EQ(column, 0u);
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, error_description_function) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString desc = fl_json_get_error_description(FlJsonParseError_None);
    ASSERT_TRUE(fl_string_equals(desc, FL_S("No error")));

    desc = fl_json_get_error_description(FlJsonParseError_ExpectedColon);
    ASSERT_TRUE(fl_string_equals(desc, FL_S("Expected colon after object key")));

    desc = fl_json_get_error_description(FlJsonParseError_InvalidNumberFormat);
    ASSERT_TRUE(fl_string_equals(desc, FL_S("Invalid number format")));

    desc = fl_json_get_error_description((FlJsonParseError)999);
    ASSERT_TRUE(fl_string_equals(desc, FL_S("Unknown JSON parse error")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, format_parse_error_basic) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonParseResult result = { 0 };
    result.error = FlJsonParseError_ExpectedColon;
    result.error_line_no = 3;
    result.error_row_no = 15;

    const char* source = "{\n  \"key\" 123\n}";
    FlString formatted = fl_json_format_parse_error(arena, &result, source, strlen(source));

    ASSERT_TRUE(fl_string_contains(formatted, FL_S("error: Expected colon")));
    ASSERT_TRUE(fl_string_contains(formatted, FL_S("3 | }"))); // Line 3 content
    ASSERT_TRUE(fl_string_contains(formatted, FL_S("^")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, format_parse_error_no_location) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonParseResult result = { 0 };
    result.error = FlJsonParseError_AllocatorFailed;
    result.error_line_no = 0; // No location info

    FlString formatted = fl_json_format_parse_error(arena, &result, "", 0);

    ASSERT_TRUE(fl_string_equals(formatted, FL_S("JSON Parse Error: Memory allocation failed")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, format_value_error_full_rust_style) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* json = "{\n  \"test\": \"invalid_value\"\n}";
    FlJsonValue* root = fl_json_parse(arena, json, strlen(json));
    ASSERT_NE(root, nullptr);

    FlJsonValue* test_value = fl_json_find_property(root, FL_S("test"));
    ASSERT_NE(test_value, nullptr);

    FlString error_msg = FL_S("expected number, found string");
    FlString help_text = FL_S("use `42` instead");
    FlString formatted = fl_json_format_value_error(arena, test_value, error_msg, FL_S("config.json"), help_text);

    // Verify exact Rust compiler style format:
    // error: expected number, found string
    //  --> config.json:2:11
    //   |
    // 2 |   "test": "invalid_value"
    //   |           ^ help: use 42 instead
    ASSERT_TRUE(fl_string_contains(formatted, FL_S("error: expected number, found string")));
    ASSERT_TRUE(fl_string_contains(formatted, FL_S("--> config.json:2:")));
    ASSERT_TRUE(fl_string_contains(formatted, FL_S("2 |   \"test\": \"invalid_value\"")));
    ASSERT_TRUE(fl_string_contains(formatted, FL_S("^ help: use `42` instead")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, format_value_error_no_help_text) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* json = "{\n  \"bad\": null\n}";
    FlJsonValue* root = fl_json_parse(arena, json, strlen(json));
    ASSERT_NE(root, nullptr);

    FlJsonValue* bad_value = fl_json_find_property(root, FL_S("bad"));
    ASSERT_NE(bad_value, nullptr);

    FlString error_msg = FL_S("Null values not allowed");
    FlString help_text = { 0 };
    FlString formatted = fl_json_format_value_error(arena, bad_value, error_msg, FL_S("theme.json"), help_text);

    ASSERT_TRUE(fl_string_contains(formatted, FL_S("error: Null values not allowed")));
    ASSERT_TRUE(fl_string_contains(formatted, FL_S(" --> theme.json:")));
    ASSERT_TRUE(fl_string_contains(formatted, FL_S("null")));
    ASSERT_TRUE(fl_string_contains(formatted, FL_S("^")));
    ASSERT_FALSE(fl_string_contains(formatted, FL_S("help:")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, format_value_error_no_file_path) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* json = "{ \"key\": 123 }";
    FlJsonValue* root = fl_json_parse(arena, json, strlen(json));
    ASSERT_NE(root, nullptr);

    FlJsonValue* key_value = fl_json_find_property(root, FL_S("key"));
    ASSERT_NE(key_value, nullptr);

    FlString error_msg = FL_S("Invalid value");
    FlString help_text = FL_S("try a different value");
    FlString formatted = fl_json_format_value_error(arena, key_value, error_msg, (FlString) { 0 }, help_text);

    ASSERT_TRUE(fl_string_contains(formatted, FL_S("error: Invalid value")));
    ASSERT_TRUE(fl_string_contains(formatted, FL_S("^ help: try a different value")));
    ASSERT_FALSE(fl_string_contains(formatted, FL_S(" -->")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, format_value_error_with_position_info) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* json = "{\"test\": 42}";
    FlJsonParseResult result = { 0 };
    FlJsonValue* value = fl_json_parse_with_flags(
        arena, json, strlen(json), FlJsonParseFlags_AllowJson5 | FlJsonParseFlags_AllowLocationInformation, &result);
    ASSERT_NE(value, nullptr);

    FlJsonValue* test_value = fl_json_find_property(value, FL_S("test"));
    ASSERT_NE(test_value, nullptr);

    FlString error_msg = FL_S("Test error");
    FlString help_text = FL_S("test help");
    FlString formatted = fl_json_format_value_error(arena, test_value, error_msg, FL_S("test.json"), help_text);

    // Validate Rust-style format:
    // error: Test error
    //  --> test.json:1:10
    //   |
    // 1 | {"test": 42}
    //   |          ^ help: test help
    ASSERT_TRUE(fl_string_contains(formatted, FL_S("error: Test error\n")));
    ASSERT_TRUE(fl_string_contains(formatted, FL_S("--> test.json:1:")));
    ASSERT_TRUE(fl_string_contains(formatted, FL_S("1 | {\"test\": 42}")));
    ASSERT_TRUE(fl_string_contains(formatted, FL_S("^ help: test help")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, format_error_with_actual_parse_errors) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* bad_jsons[] = {
        "{ \"key\": }",
        "{ key: 123 }", // unquoted key: only an error because JSON5 is off here
        "{ \"key\": 123", "\"unclosed string", "{ \"key\": 123.456.789 }",
    };

    for (size_t i = 0; i < sizeof(bad_jsons) / sizeof(bad_jsons[0]); i++) {
        FlJsonParseResult result = { 0 };
        FlJsonValue* value = fl_json_parse_with_flags(arena, bad_jsons[i], strlen(bad_jsons[i]),
                                                      FlJsonParseFlags_AllowLocationInformation, &result);

        ASSERT_EQ(value, nullptr);
        ASSERT_NE((int)result.error, (int)FlJsonParseError_None);

        FlString formatted = fl_json_format_parse_error(arena, &result, bad_jsons[i], strlen(bad_jsons[i]));
        ASSERT_GT((u64)formatted.length, 0u);
        ASSERT_TRUE(fl_string_contains(formatted, FL_S("error:")));

        if (result.error_line_no > 0) {
            ASSERT_TRUE(fl_string_contains(formatted, FL_S(" | ")));
        }
    }

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, parse_global_object_with_json5) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* json_global = "// This is a global object without outer braces\n"
                              "theme: {\n"
                              "  name: \"Dark Theme\",\n"
                              "  version: \"1.0\",\n"
                              "  colors: {\n"
                              "    accent: [255, 107, 53],\n"
                              "    background: [45, 45, 45]\n"
                              "  }\n"
                              "},\n"
                              "settings: {\n"
                              "  enabled: true\n"
                              "}";

    // Parse with JSON5 flags (which include AllowGlobalObject)
    FlJsonParseResult result = { 0 };
    FlJsonValue* root
        = fl_json_parse_with_flags(arena, json_global, strlen(json_global),
                                   FlJsonParseFlags_AllowJson5 | FlJsonParseFlags_AllowLocationInformation, &result);

    ASSERT_NE(root, nullptr);
    ASSERT_EQ((int)result.error, (int)FlJsonParseError_None);

    ASSERT_EQ((int)root->type, (int)FlJsonType_Object);

    FlJsonValue* theme = fl_json_find_property(root, FL_S("theme"));
    ASSERT_NE(theme, nullptr);
    ASSERT_EQ((int)theme->type, (int)FlJsonType_Object);

    FlJsonValue* settings = fl_json_find_property(root, FL_S("settings"));
    ASSERT_NE(settings, nullptr);
    ASSERT_EQ((int)settings->type, (int)FlJsonType_Object);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, parse_global_object_simple) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* json_simple = "name: \"test\", value: 42";

    FlJsonParseResult result = { 0 };
    FlJsonValue* root
        = fl_json_parse_with_flags(arena, json_simple, strlen(json_simple),
                                   FlJsonParseFlags_AllowJson5 | FlJsonParseFlags_AllowLocationInformation, &result);

    ASSERT_NE(root, nullptr);
    ASSERT_EQ((int)result.error, (int)FlJsonParseError_None);

    ASSERT_EQ((int)root->type, (int)FlJsonType_Object);

    FlJsonValue* name = fl_json_find_property(root, FL_S("name"));
    ASSERT_NE(name, nullptr);
    ASSERT_EQ((int)name->type, (int)FlJsonType_String);

    FlJsonValue* value = fl_json_find_property(root, FL_S("value"));
    ASSERT_NE(value, nullptr);
    ASSERT_EQ((int)value->type, (int)FlJsonType_Number);

    FlJsonNumber* num = fl_json_value_as_number(value);
    ASSERT_NE(num, nullptr);
    ASSERT_EQ((int)atoi(num->number), 42);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, get_path_nested) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* src = "{\"metadata\": {\"shortname\": \"demo\", \"nested\": {\"n\": 7}}}";
    FlJsonValue* root = fl_json_parse(arena, src, strlen(src));
    ASSERT_NE(root, nullptr);

    FlJsonValue* shortname = fl_json_get_path(root, FL_S("metadata.shortname"));
    ASSERT_NE(shortname, nullptr);
    ASSERT_EQ((int)shortname->type, (int)FlJsonType_String);

    FlJsonValue* n = fl_json_get_path(root, FL_S("metadata.nested.n"));
    ASSERT_NE(n, nullptr);
    ASSERT_EQ((int)n->type, (int)FlJsonType_Number);

    FlJsonValue* meta = fl_json_get_path(root, FL_S("metadata"));
    ASSERT_NE(meta, nullptr);
    ASSERT_EQ((int)meta->type, (int)FlJsonType_Object);

    ASSERT_EQ(fl_json_get_path(root, FL_S("metadata.missing")), nullptr);
    ASSERT_EQ(fl_json_get_path(root, FL_S("metadata.shortname.x")), nullptr);
    ASSERT_EQ(fl_json_get_path(root, FL_S("")), nullptr);
    ASSERT_EQ(fl_json_get_path(root, FL_S("metadata..shortname")), nullptr);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, array_iteration_macro) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* src = "[10, 20, 30]";
    FlJsonValue* root = fl_json_parse(arena, src, strlen(src));
    ASSERT_NE(root, nullptr);

    FlJsonArray* array = fl_json_value_as_fixed_array(root);
    ASSERT_NE(array, nullptr);

    u32 visited = 0;
    for_each_json_array_item(array, item, index) {
        ASSERT_EQ(index, visited);
        ASSERT_EQ((int)item->type, (int)FlJsonType_Number);
        visited++;
    }

    ASSERT_EQ(visited, 3u);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, position_info_absent_without_location_flag) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* src = "{\"a\": 1}";
    FlJsonValue* root = fl_json_parse_no_location(arena, src, strlen(src));
    ASSERT_NE(root, nullptr);

    ASSERT_FALSE(fl_json_has_position_info(root));
    ASSERT_EQ(fl_json_get_line_number(root), 0u);
    ASSERT_EQ(fl_json_get_column_number(root), 0u);

    FlJsonValue* prop = fl_json_find_property(root, FL_S("a"));
    ASSERT_NE(prop, nullptr);
    ASSERT_FALSE(fl_json_has_position_info(prop));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, position_info_absent_for_built_and_extracted_values) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlJsonValue* built = json_build_object(arena);
    json_build_set_int(arena, built, FL_S("a"), 1);
    ASSERT_FALSE(fl_json_has_position_info(built));
    ASSERT_FALSE(fl_json_has_position_info(fl_json_find_property(built, FL_S("a"))));

    const char* src = "{\"a\": 1}";
    FlJsonValue* parsed = fl_json_parse(arena, src, strlen(src));
    ASSERT_TRUE(fl_json_has_position_info(parsed));

    FlJsonValue* extracted = fl_json_extract_value(arena, parsed);
    ASSERT_NE(extracted, nullptr);
    ASSERT_FALSE(fl_json_has_position_info(extracted));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, extracted_number_is_terminated_and_does_not_read_next_datum) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* src = "[12, 34, 5.5]";
    FlJsonValue* parsed = fl_json_parse(arena, src, strlen(src));
    ASSERT_NE(parsed, nullptr);

    FlJsonValue* extracted = fl_json_extract_value(arena, parsed);
    ASSERT_NE(extracted, nullptr);

    FlJsonArrayElement* first = fl_json_value_as_fixed_array(extracted)->start;
    FlJsonArrayElement* second = first->next;
    FlJsonArrayElement* third = second->next;

    ASSERT_EQ(fl_json_value_get_i32(first->value), 12);
    ASSERT_EQ(fl_json_value_get_u64(first->value), 12u);
    ASSERT_EQ(fl_json_value_get_i32(second->value), 34);
    ASSERT_EQ(fl_json_value_get_f64(third->value), 5.5);
    ASSERT_EQ(first->value->type, (size_t)FlJsonType_Number);
    ASSERT_EQ(fl_json_value_as_number(first->value)->number[2], '\0');

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, number_accessors_are_bounded_by_number_size) {
    const char text[] = "1234";
    FlJsonNumber number = { .number = text, .number_size = 2 };
    FlJsonValue value = { .payload = &number, .type = FlJsonType_Number };

    ASSERT_EQ(fl_json_value_get_i32(&value), 12);
    ASSERT_EQ(fl_json_value_get_u64(&value), 12u);
    ASSERT_EQ(fl_json_value_get_f64(&value), 12.0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, number_accessors_share_base_and_overflow_behaviour) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* src = "{\"hex\": 0x1F, \"neg\": -7, \"big\": 99999999999, \"huge\": 99999999999999999999999, "
                      "\"plus\": +3}";
    FlJsonValue* root = fl_json_parse(arena, src, strlen(src));
    ASSERT_NE(root, nullptr);

    FlJsonValue* hex = fl_json_find_property(root, FL_S("hex"));
    FlJsonValue* neg = fl_json_find_property(root, FL_S("neg"));
    FlJsonValue* big = fl_json_find_property(root, FL_S("big"));
    FlJsonValue* huge = fl_json_find_property(root, FL_S("huge"));
    FlJsonValue* plus = fl_json_find_property(root, FL_S("plus"));

    ASSERT_EQ(fl_json_value_get_u64(hex), 31u);
    ASSERT_EQ(fl_json_value_get_i32(hex), 31);
    ASSERT_EQ(fl_json_get_u32_prop(root, FL_S("hex")), 31u);
    ASSERT_EQ(fl_json_value_get_i32(neg), -7);
    ASSERT_EQ(fl_json_value_get_u64(neg), 0u);
    ASSERT_EQ(fl_json_value_get_i32(plus), 3);

    ASSERT_EQ(fl_json_value_get_u64(big), 99999999999u);
    ASSERT_EQ(fl_json_value_get_i32(big), 0);
    ASSERT_EQ(fl_json_get_u32_prop(root, FL_S("big")), 0u);
    ASSERT_EQ(fl_json_value_get_u64(huge), 0u);
    ASSERT_EQ(fl_json_value_get_i32(huge), 0);

    ASSERT_EQ(fl_json_value_get_i32(nullptr), 0);
    ASSERT_EQ(fl_json_get_f64_prop(root, FL_S("missing")), 0.0);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, find_property_matches_the_full_key_including_embedded_nul) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* src = "{\"a\\u0000c\": 1, \"a\\u0000b\": 2}";
    FlJsonValue* root = fl_json_parse(arena, src, strlen(src));
    ASSERT_NE(root, nullptr);

    FlString nul_key = { .data = "a\0b", .length = 3 };
    FlJsonValue* found = fl_json_find_property(root, nul_key);
    ASSERT_NE(found, nullptr);
    ASSERT_EQ(fl_json_value_get_i32(found), 2);
    ASSERT_EQ(fl_json_find_property(root, FL_S("a")), nullptr);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, parse_entry_points_differ_only_in_location_tracking) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const char* global_object = "a: 1, b: 2";
    FlJsonValue* with_location = fl_json_parse(arena, global_object, strlen(global_object));
    FlJsonValue* without_location = fl_json_parse_no_location(arena, global_object, strlen(global_object));
    ASSERT_EQ(with_location, nullptr);
    ASSERT_EQ(without_location, nullptr);

    const char* plain = "{\"a\": 1}";
    ASSERT_NE(fl_json_parse(arena, plain, strlen(plain)), nullptr);
    ASSERT_NE(fl_json_parse_no_location(arena, plain, strlen(plain)), nullptr);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if !defined(_WIN32)
UTEST(json_parser, extract_number_reads_only_the_declared_span) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    const long page = sysconf(_SC_PAGESIZE);
    char* mapping = mmap(nullptr, (size_t)page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_TRUE(mapping != MAP_FAILED);
    ASSERT_EQ(mprotect(mapping + page, (size_t)page, PROT_NONE), 0);

    char* text = mapping + page - 2;
    text[0] = '1';
    text[1] = '2';
    FlJsonNumber number = { .number = text, .number_size = 2 };
    FlJsonValue value = { .payload = &number, .type = FlJsonType_Number };

    u8* scratch = arena_alloc_array(arena, u8, KB(16));
    memset(scratch, 0xAA, KB(16));
    arena_rewind_to(arena, scratch);

    FlJsonValue* extracted = fl_json_extract_value(arena, &value);
    ASSERT_NE(extracted, nullptr);
    ASSERT_EQ(fl_json_value_get_i32(extracted), 12);
    ASSERT_EQ(fl_json_value_as_number(extracted)->number[2], '\0');

    munmap(mapping, (size_t)page * 2);
    fl_arena_destroy(arena);
}
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_parser, number_accessors_handle_long_representable_numbers) {
    char long_float[80];
    memcpy(long_float, "1.5", 3);
    memset(long_float + 3, '0', 70);
    FlJsonNumber float_number = { .number = long_float, .number_size = 73 };
    FlJsonValue float_value = { .payload = &float_number, .type = FlJsonType_Number };
    ASSERT_EQ(fl_json_value_get_f64(&float_value), 1.5);

    char padded[80];
    memset(padded, '0', 60);
    padded[60] = '1';
    padded[61] = '2';
    FlJsonNumber int_number = { .number = padded, .number_size = 62 };
    FlJsonValue int_value = { .payload = &int_number, .type = FlJsonType_Number };
    ASSERT_EQ(fl_json_value_get_i32(&int_value), 12);
    ASSERT_EQ(fl_json_value_get_u64(&int_value), 12u);
}
