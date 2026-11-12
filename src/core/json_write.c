#include "arena.h"
#include "json.h"
#include "string.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSON_INFINITY_REPLACEMENT "1.7976931348623158e308"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct JsonWriter {
    char* dst;
    size_t length;
    const char* indent;
    const char* newline;
    bool malformed;
} JsonWriter;

typedef struct JsonNumberText {
    FlString digits;
    bool negative;
} JsonNumberText;

static void json_write_value(JsonWriter* w, const FlJsonValue* value, size_t depth);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_put(JsonWriter* w, char c) {
    if (w->dst) {
        w->dst[w->length] = c;
    }
    w->length++;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_put_text(JsonWriter* w, const char* text, size_t length) {
    for (size_t i = 0; i < length; i++) {
        json_put(w, text[i]);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_put_cstr(JsonWriter* w, const char* text) {
    json_put_text(w, text, strlen(text));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_write_line_break(JsonWriter* w, size_t depth) {
    json_put_cstr(w, w->newline);
    for (size_t i = 0; i < depth; i++) {
        json_put_cstr(w, w->indent);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static char json_string_escape(char c) {
    const char* value = c != '\0' ? strchr(JSON_ESCAPE_VALUES, c) : nullptr;
    return value ? JSON_ESCAPE_CODES[value - JSON_ESCAPE_VALUES] : '\0';
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_write_string(JsonWriter* w, const FlString* string) {
    json_put(w, '"');
    for (u64 i = 0; i < string->length; i++) {
        const char escape = json_string_escape(string->data[i]);
        if (escape) {
            json_put(w, '\\');
            json_put(w, escape);
        } else {
            json_put(w, string->data[i]);
        }
    }
    json_put(w, '"');
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_starts_with(FlString text, const char* prefix) {
    const size_t length = strlen(prefix);
    return text.length >= length && memcmp(text.data, prefix, length) == 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_write_hex_as_decimal(JsonWriter* w, FlString text) {
    char digits[24];
    const int length = snprintf(digits, sizeof(digits), "%" PRIu64, (u64)strtoull(text.data, nullptr, 0));
    json_put_text(w, digits, (size_t)length);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static JsonNumberText json_split_sign(const FlJsonNumber* number) {
    JsonNumberText text = { .digits = { .data = number->number, .length = number->number_size } };
    text.negative = json_starts_with(text.digits, "-");
    if (text.negative || json_starts_with(text.digits, "+")) {
        text.digits = fl_string_substr(text.digits, 1, text.digits.length - 1);
    }
    return text;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool json_write_special_number(JsonWriter* w, const JsonNumberText* text) {
    if (json_starts_with(text->digits, "Infinity")) {
        json_put_cstr(w, text->negative ? "-" JSON_INFINITY_REPLACEMENT : JSON_INFINITY_REPLACEMENT);
        return true;
    }
    if (json_starts_with(text->digits, "NaN")) {
        json_put(w, '0');
        return true;
    }
    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_write_decimal_number(JsonWriter* w, const JsonNumberText* text) {
    if (text->negative) {
        json_put(w, '-');
    }
    if (json_starts_with(text->digits, ".")) {
        json_put(w, '0');
    }
    json_put_text(w, text->digits.data, (size_t)text->digits.length);
    if (text->digits.length > 0 && text->digits.data[text->digits.length - 1] == '.') {
        json_put(w, '0');
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_write_number(JsonWriter* w, const FlJsonNumber* number) {
    const FlString raw = { .data = number->number, .length = number->number_size };
    if (json_starts_with(raw, "0x") || json_starts_with(raw, "0X")) {
        json_write_hex_as_decimal(w, raw);
        return;
    }
    const JsonNumberText text = json_split_sign(number);
    if (!json_write_special_number(w, &text)) {
        json_write_decimal_number(w, &text);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_write_item_start(JsonWriter* w, bool first, size_t depth) {
    if (!first) {
        json_put(w, ',');
    }
    json_write_line_break(w, depth + 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_write_list_end(JsonWriter* w, size_t count, size_t depth, char close) {
    if (count > 0) {
        json_write_line_break(w, depth);
    }
    json_put(w, close);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_write_array(JsonWriter* w, const FlJsonArray* array, size_t depth) {
    json_put(w, '[');
    for (const FlJsonArrayElement* element = array->start; element; element = element->next) {
        json_write_item_start(w, element == array->start, depth);
        json_write_value(w, element->value, depth + 1);
    }
    json_write_list_end(w, array->length, depth, ']');
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_write_object(JsonWriter* w, const FlJsonObject* object, size_t depth) {
    json_put(w, '{');
    for (const FlJsonObjectElement* element = object->start; element; element = element->next) {
        json_write_item_start(w, element == object->start, depth);
        json_write_string(w, element->name);
        json_put_cstr(w, " : ");
        json_write_value(w, element->value, depth + 1);
    }
    json_write_list_end(w, object->length, depth, '}');
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* json_literal_text(FlJsonType type) {
    switch (type) {
        case FlJsonType_True:
            return "true";
        case FlJsonType_False:
            return "false";
        case FlJsonType_Null:
            return "null";
        default:
            return nullptr;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void json_write_value(JsonWriter* w, const FlJsonValue* value, size_t depth) {
    const char* literal = json_literal_text(value->type);
    if (literal) {
        json_put_cstr(w, literal);
        return;
    }
    switch (value->type) {
        case FlJsonType_Number:
            json_write_number(w, (const FlJsonNumber*)value->payload);
            break;
        case FlJsonType_String:
            json_write_string(w, (const FlString*)value->payload);
            break;
        case FlJsonType_Array:
            json_write_array(w, (const FlJsonArray*)value->payload, depth);
            break;
        case FlJsonType_Object:
            json_write_object(w, (const FlJsonObject*)value->payload, depth);
            break;
        default:
            w->malformed = true;
            break;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_json_write_pretty(FlArena* arena, const FlJsonValue* value, const char* indent, const char* newline) {
    if (!value) {
        return (FlString) { 0 };
    }
    JsonWriter measure = { .indent = indent ? indent : "  ", .newline = newline ? newline : "\n" };
    json_write_value(&measure, value, 0);
    if (measure.malformed) {
        return (FlString) { 0 };
    }
    JsonWriter write = measure;
    write.length = 0;
    write.dst = arena_alloc_array(arena, char, measure.length);
    json_write_value(&write, value, 0);
    return (FlString) { .data = write.dst, .length = write.length };
}
