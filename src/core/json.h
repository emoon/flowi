#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// JSON parser / DOM - internal include seam over the generated <flowi/core/json.h>.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// FlString is includer-provided by convention (api_gen does not author it); the generated header below names it by
// value and by pointer, so its full definition must be in scope first.
#include "string.h"
#include "types.h"

#include <flowi/core/json.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Parse flag bitset passed to fl_json_parse_with_flags as a plain size_t.

typedef enum FlJsonParseFlags {
    // strict JSON.
    FlJsonParseFlags_Default = 0,

    // JSON5: comments, unquoted and single-quoted keys, single-quoted strings, multi-line strings, trailing commas,
    // hexadecimal numbers, leading '+', leading or trailing decimal points, Infinity and NaN, and a top-level object
    // without braces.
    FlJsonParseFlags_AllowJson5 = 0x1,
} FlJsonParseFlags;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// String escapes shared by the parser and the writer: JSON_ESCAPE_CODES[i] follows the backslash for the character
// JSON_ESCAPE_VALUES[i].

#define JSON_ESCAPE_CODES "\"\\bfnrt"
#define JSON_ESCAPE_VALUES "\"\\\b\f\n\r\t"

static inline int json_hex_digit_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// JSON iteration helper macros

// Iterate over object properties
#define for_each_json_property(json_obj, name_var, value_var)                                               \
    for (FlJsonObjectElement* _elem = fl_json_value_as_object(json_obj)->start; _elem; _elem = _elem->next) \
        if (_elem->name && _elem->value)                                                                    \
            for (int _once = 1; _once; _once = 0)                                                           \
                for (FlString* name_var = _elem->name; _once; _once = 0)                                    \
                    for (FlJsonValue* value_var = _elem->value; _once; _once = 0)

// Iterate over array items
#define for_each_json_array_item(json_array, value_var, index_var)               \
    for (                                                                        \
        struct {                                                                 \
            FlJsonArrayElement* elem;                                            \
            u32 index;                                                           \
        } _it = { (json_array)->start, 0 };                                      \
        _it.elem; _it.elem = _it.elem->next, _it.index++)                        \
        if (_it.elem->value)                                                     \
            for (int _once = 1; _once; _once = 0)                                \
                for (FlJsonValue* value_var = _it.elem->value; _once; _once = 0) \
                    for (u32 index_var = _it.index; _once; _once = 0)
