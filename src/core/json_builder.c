#include "json_builder.h"
#include "arena.h"
#include "string.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void object_append(FlJsonObject* obj, FlJsonObjectElement* elem) {
    if (!obj->start) {
        obj->start = elem;
    } else {
        FlJsonObjectElement* last = obj->start;
        while (last->next)
            last = last->next;
        last->next = elem;
    }
    obj->length++;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void object_set(FlArena* arena, FlJsonValue* obj, FlString key, FlJsonValue* value) {
    FL_ASSERT_DEBUG(obj && obj->type == FlJsonType_Object);
    FlJsonObject* object = (FlJsonObject*)obj->payload;

    FlJsonObjectElement* elem = object->start;
    while (elem) {
        if (fl_string_equals(*elem->name, key)) {
            elem->value = value;
            return;
        }
        elem = elem->next;
    }

    FlJsonObjectElement* new_elem = arena_alloc_zero(arena, FlJsonObjectElement);
    new_elem->name = arena_alloc(arena, FlString);
    *new_elem->name = fl_string_copy(arena, key);
    new_elem->value = value;
    object_append(object, new_elem);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlJsonValue* create_string_value(FlArena* arena, FlString str) {
    FlJsonValue* val = json_value_alloc_zero(arena);
    val->type = FlJsonType_String;
    FlString* payload = arena_alloc(arena, FlString);
    *payload = fl_string_copy(arena, str);
    val->payload = payload;
    return val;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlJsonValue* create_number_value(FlArena* arena, const char* num_str, size_t len) {
    FlJsonValue* val = json_value_alloc_zero(arena);
    val->type = FlJsonType_Number;
    FlJsonNumber* num = arena_alloc_zero(arena, FlJsonNumber);
    char* buf = arena_alloc_array(arena, char, len + 1);
    memcpy(buf, num_str, len);
    buf[len] = '\0';
    num->number = buf;
    num->number_size = len;
    val->payload = num;
    return val;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlJsonValue* json_build_object(FlArena* arena) {
    FL_ASSERT_DEBUG(arena);
    FlJsonObject* obj = arena_alloc_zero(arena, FlJsonObject);
    FlJsonValue* val = json_value_alloc_zero(arena);
    val->type = FlJsonType_Object;
    val->payload = obj;
    return val;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlJsonValue* json_build_array(FlArena* arena) {
    FL_ASSERT_DEBUG(arena);
    FlJsonArray* arr = arena_alloc_zero(arena, FlJsonArray);
    FlJsonValue* val = json_value_alloc_zero(arena);
    val->type = FlJsonType_Array;
    val->payload = arr;
    return val;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void json_build_set_string(FlArena* arena, FlJsonValue* obj, FlString key, FlString value) {
    FL_ASSERT_DEBUG(arena && obj && obj->type == FlJsonType_Object);
    object_set(arena, obj, key, create_string_value(arena, value));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void json_build_set_int(FlArena* arena, FlJsonValue* obj, FlString key, i64 value) {
    FL_ASSERT_DEBUG(arena && obj && obj->type == FlJsonType_Object);
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%lld", (long long)value);
    object_set(arena, obj, key, create_number_value(arena, buf, (size_t)len));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void json_build_set_float(FlArena* arena, FlJsonValue* obj, FlString key, f64 value) {
    FL_ASSERT_DEBUG(arena && obj && obj->type == FlJsonType_Object);
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "%.17g", value);
    object_set(arena, obj, key, create_number_value(arena, buf, (size_t)len));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void json_build_set_bool(FlArena* arena, FlJsonValue* obj, FlString key, bool value) {
    FL_ASSERT_DEBUG(arena && obj && obj->type == FlJsonType_Object);
    FlJsonValue* val = json_value_alloc_zero(arena);
    val->type = value ? FlJsonType_True : FlJsonType_False;
    val->payload = nullptr;
    object_set(arena, obj, key, val);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void json_build_set_null(FlArena* arena, FlJsonValue* obj, FlString key) {
    FL_ASSERT_DEBUG(arena && obj && obj->type == FlJsonType_Object);
    FlJsonValue* val = json_value_alloc_zero(arena);
    val->type = FlJsonType_Null;
    object_set(arena, obj, key, val);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void json_build_set_object(FlArena* arena, FlJsonValue* obj, FlString key, FlJsonValue* child) {
    FL_ASSERT_DEBUG(arena && obj && obj->type == FlJsonType_Object);
    FL_ASSERT_DEBUG(child);
    object_set(arena, obj, key, child);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void json_build_set_value(FlArena* arena, FlJsonValue* obj, FlString key, FlJsonValue* value) {
    FL_ASSERT_DEBUG(arena && obj && obj->type == FlJsonType_Object);
    FL_ASSERT_DEBUG(value);
    object_set(arena, obj, key, value);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void json_build_array_push(FlArena* arena, FlJsonValue* arr, FlJsonValue* value) {
    FL_ASSERT_DEBUG(arena && arr && arr->type == FlJsonType_Array);
    FL_ASSERT_DEBUG(value);

    FlJsonArray* array = (FlJsonArray*)arr->payload;
    FlJsonArrayElement* elem = arena_alloc_zero(arena, FlJsonArrayElement);
    elem->value = value;

    if (!array->start) {
        array->start = elem;
    } else {
        FlJsonArrayElement* last = array->start;
        while (last->next)
            last = last->next;
        last->next = elem;
    }
    array->length++;
}
