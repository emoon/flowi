#include "arena.h"
#include "core.h"
#include "json.h"
#include "memory.h"
#include "string.h"

#include <errno.h>
#include <math.h>
#include <stdlib.h>

FlString fl_json_value_as_string(FlJsonValue* value) {
    if (!value || value->type != FlJsonType_String) {
        return (FlString) { 0 };
    }
    const FlString* string = (const FlString*)value->payload;
    return string && string->data ? *string : (FlString) { 0 };
}

FlJsonObject* fl_json_value_as_object(FlJsonValue* value) {
    return value && value->type == FlJsonType_Object ? (FlJsonObject*)value->payload : nullptr;
}

FlJsonValue* fl_json_find_property(const FlJsonValue* object, FlString key) {
    if (!object || object->type != FlJsonType_Object) {
        return nullptr;
    }
    const FlJsonObject* members = (const FlJsonObject*)object->payload;
    for (const FlJsonObjectElement* element = members->start; element; element = element->next) {
        if (element->name && fl_string_equals(*element->name, key)) {
            return element->value;
        }
    }
    return nullptr;
}

static bool has_hex_prefix(FlString text) {
    if (text.length < 2) {
        return false;
    }
    return text.data[0] == '0' && (text.data[1] | 0x20) == 'x';
}

static u64 strip_radix_prefix(FlString* text) {
    if (has_hex_prefix(*text)) {
        *text = fl_string_substr(*text, 2, text->length - 2);
        return 16;
    }
    return 10;
}

static int digit_value(char c, u64 base) {
    const int digit = json_hex_digit_value(c);
    return digit >= 0 && (u64)digit < base ? digit : -1;
}

static bool parse_magnitude(FlString text, u64* out) {
    const u64 base = strip_radix_prefix(&text);
    if (text.length == 0) {
        return false;
    }
    u64 value = 0;
    for (u64 i = 0; i < text.length; i++) {
        const int digit = digit_value(text.data[i], base);
        if (digit < 0 || value > (UINT64_MAX - (u64)digit) / base) {
            return false;
        }
        value = value * base + (u64)digit;
    }
    *out = value;
    return true;
}

static u64 number_to_u64(const FlJsonNumber* number) {
    FlString text = { .data = number->number, .length = number->number_size };
    if (text.length > 0 && text.data[0] == '+') {
        text = fl_string_substr(text, 1, text.length - 1);
    }
    u64 value = 0;
    return parse_magnitude(text, &value) ? value : 0;
}

static f64 number_to_f64(const FlJsonNumber* number) {
    arena_scratch_auto(temp);
    char* text = arena_alloc_array(temp.arena, char, number->number_size + 1);
    memory_copy(text, number->number_size, number->number, number->number_size);
    text[number->number_size] = '\0';
    errno = 0;
    char* end;
    const f64 value = strtod(text, &end);
    const bool overflow = errno == ERANGE && (value == HUGE_VAL || value == -HUGE_VAL);
    return (end == text || *end != '\0' || overflow) ? 0.0 : value;
}

static const FlJsonNumber* number_of(const FlJsonValue* value) {
    return value && value->type == FlJsonType_Number ? (const FlJsonNumber*)value->payload : nullptr;
}

FlString fl_json_get_string_prop(const FlJsonValue* object, FlString key) {
    return fl_json_value_as_string(fl_json_find_property(object, key));
}

u32 fl_json_get_u32_prop(const FlJsonValue* object, FlString key) {
    const FlJsonNumber* number = number_of(fl_json_find_property(object, key));
    const u64 value = number ? number_to_u64(number) : 0;
    return value <= UINT32_MAX ? (u32)value : 0;
}

f64 fl_json_get_f64_prop(const FlJsonValue* object, FlString key) {
    const FlJsonNumber* number = number_of(fl_json_find_property(object, key));
    return number ? number_to_f64(number) : 0.0;
}

FlJsonValue* fl_json_get_object_prop(const FlJsonValue* object, FlString key) {
    FlJsonValue* property = fl_json_find_property(object, key);
    return property && property->type == FlJsonType_Object ? property : nullptr;
}

FlJsonArray* fl_json_get_array_prop(const FlJsonValue* object, FlString key) {
    FlJsonValue* property = fl_json_find_property(object, key);
    return property && property->type == FlJsonType_Array ? (FlJsonArray*)property->payload : nullptr;
}

static const FlString k_error_descriptions[] = {
    [FlJsonParseError_None] = S_("No error"),
    [FlJsonParseError_ExpectedCommaOrClosingBracket] = S_("Expected comma or closing bracket"),
    [FlJsonParseError_ExpectedColon] = S_("Expected colon after object key"),
    [FlJsonParseError_ExpectedOpeningQuote] = S_("Expected opening quote for string"),
    [FlJsonParseError_InvalidStringEscapeSequence] = S_("Invalid string escape sequence"),
    [FlJsonParseError_InvalidNumberFormat] = S_("Invalid number format"),
    [FlJsonParseError_InvalidValue] = S_("Invalid JSON value"),
    [FlJsonParseError_PrematureEndOfBuffer] = S_("Unexpected end of file"),
    [FlJsonParseError_InvalidString] = S_("Invalid string"),
    [FlJsonParseError_AllocatorFailed] = S_("Memory allocation failed"),
    [FlJsonParseError_UnexpectedTrailingCharacters] = S_("Unexpected characters after JSON"),
    [FlJsonParseError_TooDeep] = S_("JSON nesting exceeds maximum depth"),
};

FlString fl_json_get_error_description(FlJsonParseError error) {
    const bool known = (u32)error < sizeof_array(k_error_descriptions) && k_error_descriptions[error].data;
    return known ? k_error_descriptions[error] : S("Unknown JSON parse error");
}
