#include "json_delta.h"
#include "arena.h"
#include "string.h"
#include "json_builder.h"
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool number_equals(const FlJsonNumber* a, const FlJsonNumber* b) {
    if (a->number_size != b->number_size) {
        return false;
    }
    return memcmp(a->number, b->number, a->number_size) == 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool string_value_equals(const FlJsonValue* a, const FlJsonValue* b) {
    FlString sa = fl_json_value_as_string((FlJsonValue*)a);
    FlString sb = fl_json_value_as_string((FlJsonValue*)b);
    return fl_string_equals(sa, sb);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool json_value_equals(const FlJsonValue* a, const FlJsonValue* b) {
    if (a == b) {
        return true;
    }
    if (!a || !b) {
        return false;
    }
    if (a->type != b->type) {
        return false;
    }

    switch ((FlJsonType)a->type) {
        case FlJsonType_Null:
        case FlJsonType_True:
        case FlJsonType_False:
            return true;

        case FlJsonType_String:
            return string_value_equals(a, b);

        case FlJsonType_Number: {
            const FlJsonNumber* na = (const FlJsonNumber*)a->payload;
            const FlJsonNumber* nb = (const FlJsonNumber*)b->payload;
            return number_equals(na, nb);
        }

        case FlJsonType_Array: {
            const FlJsonArray* aa = (const FlJsonArray*)a->payload;
            const FlJsonArray* ab = (const FlJsonArray*)b->payload;
            if (aa->length != ab->length) {
                return false;
            }
            FlJsonArrayElement* ea = aa->start;
            FlJsonArrayElement* eb = ab->start;
            while (ea && eb) {
                if (!json_value_equals(ea->value, eb->value)) {
                    return false;
                }
                ea = ea->next;
                eb = eb->next;
            }
            return true;
        }

        case FlJsonType_Object: {
            const FlJsonObject* oa = (const FlJsonObject*)a->payload;
            const FlJsonObject* ob = (const FlJsonObject*)b->payload;
            if (oa->length != ob->length) {
                return false;
            }
            for (FlJsonObjectElement* elem = oa->start; elem; elem = elem->next) {
                FlJsonValue* bval = fl_json_find_property(b, *elem->name);
                if (!bval || !json_value_equals(elem->value, bval)) {
                    return false;
                }
            }
            return true;
        }
    }

    return false;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlJsonValue* json_delta_merge(FlArena* arena, const FlJsonValue* base, const FlJsonValue* override_val) {
    // If override is not an object, it completely replaces base
    if (!override_val || override_val->type != FlJsonType_Object) {
        return override_val ? (FlJsonValue*)override_val : (FlJsonValue*)base;
    }

    if (!base || base->type != FlJsonType_Object) {
        return (FlJsonValue*)override_val;
    }

    FlJsonValue* merged = json_build_object(arena);

    for_each_json_property((FlJsonValue*)base, key, value) {
        FlJsonValue* override_prop = fl_json_find_property(override_val, *key);
        if (override_prop && override_prop->type == FlJsonType_Null) {
            continue;
        }
        json_build_set_value(arena, merged, *key, value);
    }

    for_each_json_property((FlJsonValue*)override_val, key, value) {
        if (value->type == FlJsonType_Null) {
            continue;
        }

        FlJsonValue* existing = fl_json_find_property(merged, *key);

        if (existing && existing->type == FlJsonType_Object && value->type == FlJsonType_Object) {
            FlJsonValue* merged_nested = json_delta_merge(arena, existing, value);
            json_build_set_value(arena, merged, *key, merged_nested);
        } else {
            json_build_set_value(arena, merged, *key, value);
        }
    }

    return merged;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlJsonValue* json_delta_diff(FlArena* arena, const FlJsonValue* base, const FlJsonValue* current) {
    if (!base || !current) {
        return nullptr;
    }

    if (base->type != FlJsonType_Object || current->type != FlJsonType_Object) {
        if (!json_value_equals(base, current)) {
            return (FlJsonValue*)current;
        }
        return nullptr;
    }

    FlJsonValue* diff = json_build_object(arena);
    int changed_count = 0;

    for_each_json_property((FlJsonValue*)current, key, value) {
        FlJsonValue* base_val = fl_json_find_property(base, *key);

        if (!base_val) {
            json_build_set_value(arena, diff, *key, value);
            changed_count++;
        } else if (base_val->type == FlJsonType_Object && value->type == FlJsonType_Object) {
            FlJsonValue* nested_diff = json_delta_diff(arena, base_val, value);
            if (nested_diff) {
                json_build_set_value(arena, diff, *key, nested_diff);
                changed_count++;
            }
        } else if (!json_value_equals(base_val, value)) {
            json_build_set_value(arena, diff, *key, value);
            changed_count++;
        }
    }

    for_each_json_property((FlJsonValue*)base, key, value) {
        UNUSED(value);
        if (!fl_json_find_property(current, *key)) {
            json_build_set_null(arena, diff, *key);
            changed_count++;
        }
    }

    return changed_count > 0 ? diff : nullptr;
}
