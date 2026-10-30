///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compile-only parity guard: the api_gen-generated public JSON surface (<flowi/core/json.h>). This TU links nothing.
//
// The FlJsonParseFlags bitset enum and the for_each_json_* iteration macros are a deliberate carve-out kept in
// src/core/json.h, so they are not part of this generated surface.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// FlString is includer-provided: the generated <flowi/core/json.h> names it without including it, so define it first.
#include <core/string.h>
// Self-includes <flowi/arena/arena.h> for FlArena.
#include <flowi/core/json.h>

#define FL_JSON_FN_MATCHES(fn, ...) \
    _Static_assert(__builtin_types_compatible_p(__typeof__(fn), __VA_ARGS__), #fn " signature drift")

FL_JSON_FN_MATCHES(fl_json_parse, FlJsonValue*(FlArena*, const void*, size_t));
FL_JSON_FN_MATCHES(fl_json_parse_no_location, FlJsonValue*(FlArena*, const void*, size_t));
FL_JSON_FN_MATCHES(fl_json_parse_with_flags, FlJsonValue*(FlArena*, const void*, size_t, size_t, FlJsonParseResult*));
FL_JSON_FN_MATCHES(fl_json_extract_value, FlJsonValue*(FlArena*, const FlJsonValue*));
FL_JSON_FN_MATCHES(fl_json_write_minified, FlString(FlArena*, const FlJsonValue*));
FL_JSON_FN_MATCHES(fl_json_write_pretty, FlString(FlArena*, const FlJsonValue*, const char*, const char*));
FL_JSON_FN_MATCHES(fl_json_value_as_string, FlString(FlJsonValue*));
FL_JSON_FN_MATCHES(fl_json_value_as_number, FlJsonNumber*(FlJsonValue*));
FL_JSON_FN_MATCHES(fl_json_value_as_object, FlJsonObject*(FlJsonValue*));
FL_JSON_FN_MATCHES(fl_json_value_as_fixed_array, FlJsonArray*(FlJsonValue*));
FL_JSON_FN_MATCHES(fl_json_value_is_true, int32_t(const FlJsonValue*));
FL_JSON_FN_MATCHES(fl_json_value_is_false, int32_t(const FlJsonValue*));
FL_JSON_FN_MATCHES(fl_json_value_is_null, int32_t(const FlJsonValue*));
FL_JSON_FN_MATCHES(fl_json_value_is_bool, int32_t(const FlJsonValue*));
FL_JSON_FN_MATCHES(fl_json_value_get_bool, int32_t(const FlJsonValue*));
FL_JSON_FN_MATCHES(fl_json_value_get_u64, uint64_t(const FlJsonValue*));
FL_JSON_FN_MATCHES(fl_json_value_get_i32, int32_t(const FlJsonValue*));
FL_JSON_FN_MATCHES(fl_json_value_get_f64, double(const FlJsonValue*));
FL_JSON_FN_MATCHES(fl_json_find_property, FlJsonValue*(const FlJsonValue*, FlString));
FL_JSON_FN_MATCHES(fl_json_get_string_prop, FlString(const FlJsonValue*, FlString));
FL_JSON_FN_MATCHES(fl_json_get_u32_prop, uint32_t(const FlJsonValue*, FlString));
FL_JSON_FN_MATCHES(fl_json_get_f64_prop, double(const FlJsonValue*, FlString));
FL_JSON_FN_MATCHES(fl_json_get_bool_prop, int32_t(const FlJsonValue*, FlString));
FL_JSON_FN_MATCHES(fl_json_get_object_prop, FlJsonValue*(const FlJsonValue*, FlString));
FL_JSON_FN_MATCHES(fl_json_get_array_prop, FlJsonArray*(const FlJsonValue*, FlString));
FL_JSON_FN_MATCHES(fl_json_get_line_number, size_t(const FlJsonValue*));
FL_JSON_FN_MATCHES(fl_json_get_column_number, size_t(const FlJsonValue*));
FL_JSON_FN_MATCHES(fl_json_has_position_info, int32_t(const FlJsonValue*));
FL_JSON_FN_MATCHES(fl_json_format_parse_error, FlString(FlArena*, const FlJsonParseResult*, const char*, size_t));
FL_JSON_FN_MATCHES(fl_json_get_error_description, FlString(FlJsonParseError));
FL_JSON_FN_MATCHES(fl_json_format_value_error, FlString(FlArena*, const FlJsonValue*, FlString, FlString, FlString));
FL_JSON_FN_MATCHES(fl_json_get_path, FlJsonValue*(const FlJsonValue*, FlString));

#undef FL_JSON_FN_MATCHES

_Static_assert(sizeof(FlJsonType) == 4, "FlJsonType size drift");
_Static_assert(FlJsonType_String == 0 && FlJsonType_Null == 6, "FlJsonType value drift");
_Static_assert(sizeof(FlJsonParseError) == 4, "FlJsonParseError size drift");
_Static_assert(FlJsonParseError_None == 0 && FlJsonParseError_TooDeep == 12, "FlJsonParseError value drift");

// DOM struct layouts (LP64).
_Static_assert(sizeof(FlJsonNumber) == 16 && _Alignof(FlJsonNumber) == 8, "FlJsonNumber layout drift");
_Static_assert(offsetof(FlJsonNumber, number) == 0 && offsetof(FlJsonNumber, number_size) == 8,
               "FlJsonNumber offset drift");

_Static_assert(sizeof(FlJsonObjectElement) == 24 && _Alignof(FlJsonObjectElement) == 8,
               "FlJsonObjectElement layout drift");
_Static_assert(offsetof(FlJsonObjectElement, name) == 0 && offsetof(FlJsonObjectElement, value) == 8
                   && offsetof(FlJsonObjectElement, next) == 16,
               "FlJsonObjectElement offset drift");

_Static_assert(sizeof(FlJsonObject) == 16 && _Alignof(FlJsonObject) == 8, "FlJsonObject layout drift");
_Static_assert(offsetof(FlJsonObject, start) == 0 && offsetof(FlJsonObject, length) == 8, "FlJsonObject offset drift");

_Static_assert(sizeof(FlJsonArrayElement) == 16 && _Alignof(FlJsonArrayElement) == 8,
               "FlJsonArrayElement layout drift");
_Static_assert(offsetof(FlJsonArrayElement, value) == 0 && offsetof(FlJsonArrayElement, next) == 8,
               "FlJsonArrayElement offset drift");

_Static_assert(sizeof(FlJsonArray) == 16 && _Alignof(FlJsonArray) == 8, "FlJsonArray layout drift");
_Static_assert(offsetof(FlJsonArray, start) == 0 && offsetof(FlJsonArray, length) == 8, "FlJsonArray offset drift");

_Static_assert(sizeof(FlJsonValue) == 16 && _Alignof(FlJsonValue) == 8, "FlJsonValue layout drift");
_Static_assert(offsetof(FlJsonValue, payload) == 0 && offsetof(FlJsonValue, type) == 8, "FlJsonValue offset drift");

_Static_assert(sizeof(FlJsonParseResult) == 32 && _Alignof(FlJsonParseResult) == 8, "FlJsonParseResult layout drift");
_Static_assert(offsetof(FlJsonParseResult, error) == 0 && offsetof(FlJsonParseResult, error_offset) == 8
                   && offsetof(FlJsonParseResult, error_line_no) == 16
                   && offsetof(FlJsonParseResult, error_row_no) == 24,
               "FlJsonParseResult offset drift");
