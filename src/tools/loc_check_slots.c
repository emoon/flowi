#include "loc_check_slots.h"
#include <core/json.h>
#include <ui/localization.h>

// Highest {n} argument index text references, or -1 when it references none. Walks the same placeholder
// grammar the runtime substitutes through, so the two cannot disagree about what a slot is.

static i64 max_slot_index(FlString text) {
    i64 highest = -1;
    LocalizationToken token = { 0 };
    u64 cursor = 0;

    while (localization_next_token(text, &cursor, &token)) {
        u32 index = 0;
        if (!token.is_slot || !localization_slot_index(token.body, &index)) {
            continue;
        }
        if ((i64)index > highest) {
            highest = (i64)index;
        }
    }

    return highest;
}

// The same, across every form a value can take: a plural-variant object references whatever its widest variant does.

i64 loc_check_value_max_slot_index(FlJsonValue* value) {
    if (value->type == FlJsonType_String) {
        return max_slot_index(fl_json_value_as_string(value));
    }
    if (value->type != FlJsonType_Object) {
        return -1;
    }

    i64 highest = -1;
    for_each_json_property(value, variant, variant_value) {
        (void)variant;
        if (variant_value->type != FlJsonType_String) {
            continue;
        }
        const i64 index = max_slot_index(fl_json_value_as_string(variant_value));
        if (index > highest) {
            highest = index;
        }
    }
    return highest;
}
