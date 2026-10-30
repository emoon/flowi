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
    FlJsonParseFlags_Default = 0,

    // allow trailing commas in objects and arrays.
    FlJsonParseFlags_AllowTrailingComma = 0x1,

    // allow unquoted keys for objects.
    FlJsonParseFlags_AllowUnquotedKeys = 0x2,

    // allow a global unbracketed object. For example, a : null, b : true, c : {}
    FlJsonParseFlags_AllowGlobalObject = 0x4,

    // allow objects to use '=' instead of ':' between key/value pairs.
    FlJsonParseFlags_AllowEqualsInObject = 0x8,

    // allow that objects don't have to have comma separators between key/value
    // pairs.
    FlJsonParseFlags_AllowNoCommas = 0x10,

    // allow c-style comments (either variants) to be ignored in the input JSON
    // file.
    FlJsonParseFlags_AllowCStyleComments = 0x20,

    // record location information for each value.
    FlJsonParseFlags_AllowLocationInformation = 0x80,

    // allow strings to be 'single quoted'.
    FlJsonParseFlags_AllowSingleQuotedStrings = 0x100,

    // allow numbers to be hexadecimal.
    FlJsonParseFlags_AllowHexadecimalNumbers = 0x200,

    // allow numbers like +123 to be parsed.
    FlJsonParseFlags_AllowLeadingPlusSign = 0x400,

    // allow numbers like .0123 or 123. to be parsed.
    FlJsonParseFlags_AllowLeadingOrTrailingDecimalPoint = 0x800,

    // allow Infinity, -Infinity, NaN, -NaN.
    FlJsonParseFlags_AllowInfAndNan = 0x1000,

    // allow multi line string by ignoring new line characters.
    FlJsonParseFlags_AllowMultiLineStrings = 0x2000,

    // allow simplified JSON to be parsed. Simplified JSON is an enabling of a set
    // of other parsing options.
    FlJsonParseFlags_AllowSimplifiedJson = (FlJsonParseFlags_AllowTrailingComma | FlJsonParseFlags_AllowUnquotedKeys
                                            | FlJsonParseFlags_AllowGlobalObject | FlJsonParseFlags_AllowEqualsInObject
                                            | FlJsonParseFlags_AllowNoCommas),

    // allow JSON5 to be parsed. JSON5 is an enabling of a set of other parsing options.
    FlJsonParseFlags_AllowJson5 = (
        FlJsonParseFlags_AllowTrailingComma | FlJsonParseFlags_AllowUnquotedKeys | FlJsonParseFlags_AllowCStyleComments
        | FlJsonParseFlags_AllowSingleQuotedStrings | FlJsonParseFlags_AllowHexadecimalNumbers
        | FlJsonParseFlags_AllowLeadingPlusSign | FlJsonParseFlags_AllowLeadingOrTrailingDecimalPoint
        | FlJsonParseFlags_AllowInfAndNan | FlJsonParseFlags_AllowMultiLineStrings | FlJsonParseFlags_AllowGlobalObject)
} FlJsonParseFlags;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Allocate a zeroed value node. Every value reachable through the fl_json_* API carries the parser's
// internal location fields, so anything building a DOM by hand must allocate through this rather
// than sizeof(FlJsonValue) - the position accessors read those fields unconditionally.

struct FlJsonValue* json_value_alloc_zero(struct FlArena* arena);

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
