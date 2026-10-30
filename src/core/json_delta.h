#pragma once

#include "json.h"
#include <stdbool.h>

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Deep merge: override's properties take precedence over base's.
// For nested objects, merges recursively. For other types, override replaces base.
// Both inputs are treated as const (not modified). Result is allocated from arena.
// If override is nullptr, returns base. If base is nullptr, returns override.
// A JSON null in override removes the property instead of setting it to null (RFC 7386 merge-patch
// semantics), which is how json_delta_diff encodes a deletion.

struct FlJsonValue* json_delta_merge(struct FlArena* arena, const struct FlJsonValue* base,
                                     const struct FlJsonValue* override_val);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Produce delta: returns JSON object containing only properties from current
// that differ from base. Recurses into nested objects.
// A property present in base and absent from current is recorded as a JSON null, which
// json_delta_merge reads back as a removal. A current property that really is null is therefore
// indistinguishable from a deletion.
// Returns nullptr if objects are identical (nothing changed).

struct FlJsonValue* json_delta_diff(struct FlArena* arena, const struct FlJsonValue* base,
                                    const struct FlJsonValue* current);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Deep equality comparison of two JSON values.
// For objects, compares all properties (order-independent).
// For arrays, compares element-by-element (order-dependent).

bool json_value_equals(const struct FlJsonValue* a, const struct FlJsonValue* b);
