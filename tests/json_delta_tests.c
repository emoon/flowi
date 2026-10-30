#include "utest/utest.h"
#include <core/arena.h>
#include <core/json.h>
#include <core/json_builder.h>
#include <core/json_delta.h>
#include <core/string.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, equals_null_values) {
    FlArena* arena = arena_new();
    FlJsonValue* a = fl_json_parse(arena, "null", 4);
    FlJsonValue* b = fl_json_parse(arena, "null", 4);
    ASSERT_TRUE(json_value_equals(a, b));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, equals_bool_values) {
    FlArena* arena = arena_new();
    FlJsonValue* t1 = fl_json_parse(arena, "true", 4);
    FlJsonValue* t2 = fl_json_parse(arena, "true", 4);
    FlJsonValue* f1 = fl_json_parse(arena, "false", 5);

    ASSERT_TRUE(json_value_equals(t1, t2));
    ASSERT_FALSE(json_value_equals(t1, f1));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, equals_number_values) {
    FlArena* arena = arena_new();
    FlJsonValue* a = fl_json_parse(arena, "42", 2);
    FlJsonValue* b = fl_json_parse(arena, "42", 2);
    FlJsonValue* c = fl_json_parse(arena, "99", 2);

    ASSERT_TRUE(json_value_equals(a, b));
    ASSERT_FALSE(json_value_equals(a, c));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, equals_string_values) {
    FlArena* arena = arena_new();
    const char* json_a = "\"hello\"";
    const char* json_b = "\"hello\"";
    const char* json_c = "\"world\"";

    FlJsonValue* a = fl_json_parse(arena, json_a, strlen(json_a));
    FlJsonValue* b = fl_json_parse(arena, json_b, strlen(json_b));
    FlJsonValue* c = fl_json_parse(arena, json_c, strlen(json_c));

    ASSERT_TRUE(json_value_equals(a, b));
    ASSERT_FALSE(json_value_equals(a, c));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, equals_type_mismatch) {
    FlArena* arena = arena_new();
    FlJsonValue* num = fl_json_parse(arena, "42", 2);
    FlJsonValue* str = fl_json_parse(arena, "\"42\"", 4);

    ASSERT_FALSE(json_value_equals(num, str));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, equals_objects) {
    FlArena* arena = arena_new();
    const char* json_a = "{\"x\":1,\"y\":2}";
    const char* json_b = "{\"y\":2,\"x\":1}";
    const char* json_c = "{\"x\":1,\"y\":3}";

    FlJsonValue* a = fl_json_parse(arena, json_a, strlen(json_a));
    FlJsonValue* b = fl_json_parse(arena, json_b, strlen(json_b));
    FlJsonValue* c = fl_json_parse(arena, json_c, strlen(json_c));

    ASSERT_TRUE(json_value_equals(a, b));
    ASSERT_FALSE(json_value_equals(a, c));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, equals_arrays) {
    FlArena* arena = arena_new();
    const char* json_a = "[1,2,3]";
    const char* json_b = "[1,2,3]";
    const char* json_c = "[3,2,1]";

    FlJsonValue* a = fl_json_parse(arena, json_a, strlen(json_a));
    FlJsonValue* b = fl_json_parse(arena, json_b, strlen(json_b));
    FlJsonValue* c = fl_json_parse(arena, json_c, strlen(json_c));

    ASSERT_TRUE(json_value_equals(a, b));
    ASSERT_FALSE(json_value_equals(a, c));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, equals_nullptr_handling) {
    FlArena* arena = arena_new();
    FlJsonValue* a = fl_json_parse(arena, "42", 2);

    ASSERT_TRUE(json_value_equals(nullptr, nullptr));
    ASSERT_FALSE(json_value_equals(a, nullptr));
    ASSERT_FALSE(json_value_equals(nullptr, a));
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, merge_flat_objects) {
    FlArena* arena = arena_new();
    const char* base_json = "{\"a\":1,\"b\":2}";
    const char* over_json = "{\"b\":99,\"c\":3}";

    FlJsonValue* base = fl_json_parse(arena, base_json, strlen(base_json));
    FlJsonValue* over = fl_json_parse(arena, over_json, strlen(over_json));
    FlJsonValue* merged = json_delta_merge(arena, base, over);

    ASSERT_NE(merged, nullptr);
    ASSERT_EQ(fl_json_get_u32_prop(merged, FL_S("a")), 1u);
    ASSERT_EQ(fl_json_get_u32_prop(merged, FL_S("b")), 99u);
    ASSERT_EQ(fl_json_get_u32_prop(merged, FL_S("c")), 3u);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, merge_nested_objects) {
    FlArena* arena = arena_new();
    const char* base_json = "{\"outer\":{\"a\":1,\"b\":2}}";
    const char* over_json = "{\"outer\":{\"b\":99,\"c\":3}}";

    FlJsonValue* base = fl_json_parse(arena, base_json, strlen(base_json));
    FlJsonValue* over = fl_json_parse(arena, over_json, strlen(over_json));
    FlJsonValue* merged = json_delta_merge(arena, base, over);

    ASSERT_NE(merged, nullptr);
    FlJsonValue* outer = fl_json_get_object_prop(merged, FL_S("outer"));
    ASSERT_NE(outer, nullptr);
    ASSERT_EQ(fl_json_get_u32_prop(outer, FL_S("a")), 1u);
    ASSERT_EQ(fl_json_get_u32_prop(outer, FL_S("b")), 99u);
    ASSERT_EQ(fl_json_get_u32_prop(outer, FL_S("c")), 3u);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, merge_override_replaces_non_object) {
    FlArena* arena = arena_new();
    const char* base_json = "{\"a\":{\"nested\":1}}";
    const char* over_json = "{\"a\":42}";

    FlJsonValue* base = fl_json_parse(arena, base_json, strlen(base_json));
    FlJsonValue* over = fl_json_parse(arena, over_json, strlen(over_json));
    FlJsonValue* merged = json_delta_merge(arena, base, over);

    ASSERT_NE(merged, nullptr);
    ASSERT_EQ(fl_json_get_u32_prop(merged, FL_S("a")), 42u);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, merge_nullptr_handling) {
    FlArena* arena = arena_new();
    const char* json = "{\"a\":1}";
    FlJsonValue* val = fl_json_parse(arena, json, strlen(json));

    FlJsonValue* result = json_delta_merge(arena, val, nullptr);
    ASSERT_EQ(result, val);

    result = json_delta_merge(arena, nullptr, val);
    ASSERT_EQ(result, val);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, diff_identical_objects_returns_nullptr) {
    FlArena* arena = arena_new();
    const char* json = "{\"a\":1,\"b\":\"hello\",\"c\":true}";
    FlJsonValue* base = fl_json_parse(arena, json, strlen(json));
    FlJsonValue* current = fl_json_parse(arena, json, strlen(json));

    FlJsonValue* diff = json_delta_diff(arena, base, current);
    ASSERT_EQ(diff, nullptr);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, diff_single_field_change) {
    FlArena* arena = arena_new();
    const char* base_json = "{\"a\":1,\"b\":2}";
    const char* curr_json = "{\"a\":1,\"b\":99}";

    FlJsonValue* base = fl_json_parse(arena, base_json, strlen(base_json));
    FlJsonValue* curr = fl_json_parse(arena, curr_json, strlen(curr_json));
    FlJsonValue* diff = json_delta_diff(arena, base, curr);

    ASSERT_NE(diff, nullptr);
    FlJsonObject* obj = fl_json_value_as_object(diff);
    ASSERT_EQ(obj->length, 1u);
    ASSERT_EQ(fl_json_get_u32_prop(diff, FL_S("b")), 99u);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, diff_multiple_changes) {
    FlArena* arena = arena_new();
    const char* base_json = "{\"a\":1,\"b\":2,\"c\":3}";
    const char* curr_json = "{\"a\":10,\"b\":2,\"c\":30}";

    FlJsonValue* base = fl_json_parse(arena, base_json, strlen(base_json));
    FlJsonValue* curr = fl_json_parse(arena, curr_json, strlen(curr_json));
    FlJsonValue* diff = json_delta_diff(arena, base, curr);

    ASSERT_NE(diff, nullptr);
    FlJsonObject* obj = fl_json_value_as_object(diff);
    ASSERT_EQ(obj->length, 2u);
    ASSERT_EQ(fl_json_get_u32_prop(diff, FL_S("a")), 10u);
    ASSERT_EQ(fl_json_get_u32_prop(diff, FL_S("c")), 30u);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, diff_nested_objects) {
    FlArena* arena = arena_new();
    const char* base_json = "{\"outer\":{\"a\":1,\"b\":2}}";
    const char* curr_json = "{\"outer\":{\"a\":1,\"b\":99}}";

    FlJsonValue* base = fl_json_parse(arena, base_json, strlen(base_json));
    FlJsonValue* curr = fl_json_parse(arena, curr_json, strlen(curr_json));
    FlJsonValue* diff = json_delta_diff(arena, base, curr);

    ASSERT_NE(diff, nullptr);
    FlJsonValue* outer = fl_json_get_object_prop(diff, FL_S("outer"));
    ASSERT_NE(outer, nullptr);
    FlJsonObject* outer_obj = fl_json_value_as_object(outer);
    ASSERT_EQ(outer_obj->length, 1u);
    ASSERT_EQ(fl_json_get_u32_prop(outer, FL_S("b")), 99u);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, diff_new_property) {
    FlArena* arena = arena_new();
    const char* base_json = "{\"a\":1}";
    const char* curr_json = "{\"a\":1,\"b\":2}";

    FlJsonValue* base = fl_json_parse(arena, base_json, strlen(base_json));
    FlJsonValue* curr = fl_json_parse(arena, curr_json, strlen(curr_json));
    FlJsonValue* diff = json_delta_diff(arena, base, curr);

    ASSERT_NE(diff, nullptr);
    FlJsonObject* obj = fl_json_value_as_object(diff);
    ASSERT_EQ(obj->length, 1u);
    ASSERT_EQ(fl_json_get_u32_prop(diff, FL_S("b")), 2u);
    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, builder_object_with_all_types) {
    FlArena* arena = arena_new();
    FlJsonValue* obj = json_build_object(arena);

    json_build_set_string(arena, obj, FL_S("name"), FL_S("test"));
    json_build_set_int(arena, obj, FL_S("count"), 42);
    json_build_set_float(arena, obj, FL_S("ratio"), 3.14);
    json_build_set_bool(arena, obj, FL_S("enabled"), true);
    json_build_set_bool(arena, obj, FL_S("disabled"), false);

    FlJsonValue* child = json_build_object(arena);
    json_build_set_int(arena, child, FL_S("nested"), 1);
    json_build_set_object(arena, obj, FL_S("child"), child);

    FlString name = fl_json_get_string_prop(obj, FL_S("name"));
    ASSERT_TRUE(fl_string_equals(name, FL_S("test")));
    ASSERT_EQ(fl_json_get_u32_prop(obj, FL_S("count")), 42u);
    ASSERT_TRUE(fl_json_get_bool_prop(obj, FL_S("enabled")));
    ASSERT_FALSE(fl_json_get_bool_prop(obj, FL_S("disabled")));

    FlJsonValue* child_prop = fl_json_get_object_prop(obj, FL_S("child"));
    ASSERT_NE(child_prop, nullptr);
    ASSERT_EQ(fl_json_get_u32_prop(child_prop, FL_S("nested")), 1u);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, builder_round_trip) {
    FlArena* arena = arena_new();
    FlJsonValue* obj = json_build_object(arena);

    json_build_set_string(arena, obj, FL_S("str"), FL_S("hello"));
    json_build_set_int(arena, obj, FL_S("num"), 123);
    json_build_set_bool(arena, obj, FL_S("flag"), true);

    FlString json_str = fl_json_write_minified(arena, obj);
    size_t json_len = json_str.length;
    ASSERT_GT(json_len, 0u);

    FlJsonValue* parsed = fl_json_parse(arena, json_str.data, json_str.length);
    ASSERT_NE(parsed, nullptr);

    FlString str = fl_json_get_string_prop(parsed, FL_S("str"));
    ASSERT_TRUE(fl_string_equals(str, FL_S("hello")));
    ASSERT_EQ(fl_json_get_u32_prop(parsed, FL_S("num")), 123u);
    ASSERT_TRUE(fl_json_get_bool_prop(parsed, FL_S("flag")));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, builder_overwrite_existing_key) {
    FlArena* arena = arena_new();
    FlJsonValue* obj = json_build_object(arena);

    json_build_set_int(arena, obj, FL_S("x"), 1);
    json_build_set_int(arena, obj, FL_S("x"), 99);

    ASSERT_EQ(fl_json_get_u32_prop(obj, FL_S("x")), 99u);

    FlJsonObject* raw = fl_json_value_as_object(obj);
    ASSERT_EQ(raw->length, 1u);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, builder_array) {
    FlArena* arena = arena_new();
    FlJsonValue* arr = json_build_array(arena);

    FlJsonValue* elem1 = json_build_object(arena);
    json_build_set_int(arena, elem1, FL_S("val"), 1);
    json_build_array_push(arena, arr, elem1);

    FlJsonValue* elem2 = json_build_object(arena);
    json_build_set_int(arena, elem2, FL_S("val"), 2);
    json_build_array_push(arena, arr, elem2);

    FlJsonArray* raw = fl_json_value_as_fixed_array(arr);
    ASSERT_EQ(raw->length, 2u);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, merge_diff_round_trip) {
    FlArena* arena = arena_new();
    const char* base_json = "{\"a\":1,\"b\":2,\"c\":3}";
    const char* delta_json = "{\"b\":99}";

    FlJsonValue* base = fl_json_parse(arena, base_json, strlen(base_json));
    FlJsonValue* delta = fl_json_parse(arena, delta_json, strlen(delta_json));

    FlJsonValue* merged = json_delta_merge(arena, base, delta);

    FlJsonValue* recovered_delta = json_delta_diff(arena, base, merged);
    ASSERT_NE(recovered_delta, nullptr);
    ASSERT_TRUE(json_value_equals(delta, recovered_delta));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, diff_records_removed_property_as_null) {
    FlArena* arena = arena_new();
    const char* base_json = "{\"a\":1,\"b\":2}";
    const char* current_json = "{\"a\":1}";

    FlJsonValue* base = fl_json_parse(arena, base_json, strlen(base_json));
    FlJsonValue* current = fl_json_parse(arena, current_json, strlen(current_json));

    FlJsonValue* delta = json_delta_diff(arena, base, current);
    ASSERT_NE(delta, nullptr);

    FlJsonValue* removed = fl_json_find_property(delta, FL_S("b"));
    ASSERT_NE(removed, nullptr);
    ASSERT_TRUE(fl_json_value_is_null(removed));
    ASSERT_EQ(fl_json_find_property(delta, FL_S("a")), nullptr);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, merge_applies_removal_from_diff) {
    FlArena* arena = arena_new();
    const char* base_json = "{\"a\":1,\"b\":2}";
    const char* current_json = "{\"a\":1}";

    FlJsonValue* base = fl_json_parse(arena, base_json, strlen(base_json));
    FlJsonValue* current = fl_json_parse(arena, current_json, strlen(current_json));

    FlJsonValue* delta = json_delta_diff(arena, base, current);
    FlJsonValue* merged = json_delta_merge(arena, base, delta);

    ASSERT_EQ(fl_json_find_property(merged, FL_S("b")), nullptr);
    ASSERT_TRUE(json_value_equals(current, merged));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(json_delta, diff_of_removal_is_empty_when_nothing_else_changed) {
    FlArena* arena = arena_new();
    const char* json = "{\"a\":1,\"b\":2}";

    FlJsonValue* base = fl_json_parse(arena, json, strlen(json));
    FlJsonValue* current = fl_json_parse(arena, json, strlen(json));

    ASSERT_EQ(json_delta_diff(arena, base, current), nullptr);

    fl_arena_destroy(arena);
}
