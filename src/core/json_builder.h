#pragma once

#include "json.h"

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Programmatic JSON tree construction (arena-allocated).
// All functions allocate from the provided arena. The caller owns the arena lifetime.

struct FlJsonValue* json_build_object(struct FlArena* arena);
struct FlJsonValue* json_build_array(struct FlArena* arena);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Object property setters - add or replace a property on a JSON object

void json_build_set_string(struct FlArena* arena, struct FlJsonValue* obj, struct FlString key, struct FlString value);
void json_build_set_int(struct FlArena* arena, struct FlJsonValue* obj, struct FlString key, i64 value);
void json_build_set_float(struct FlArena* arena, struct FlJsonValue* obj, struct FlString key, f64 value);
void json_build_set_bool(struct FlArena* arena, struct FlJsonValue* obj, struct FlString key, bool value);
void json_build_set_null(struct FlArena* arena, struct FlJsonValue* obj, struct FlString key);
void json_build_set_object(struct FlArena* arena, struct FlJsonValue* obj, struct FlString key,
                           struct FlJsonValue* child);
void json_build_set_value(struct FlArena* arena, struct FlJsonValue* obj, struct FlString key,
                          struct FlJsonValue* value);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void json_build_array_push(struct FlArena* arena, struct FlJsonValue* arr, struct FlJsonValue* value);
