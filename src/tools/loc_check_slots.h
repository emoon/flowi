#pragma once

#include <core/json.h>
#include <core/types.h>

// Highest {n} argument index a String Table value references, or -1 when it references none. A plural-variant
// object references whatever its widest variant does; any other value type references none.

i64 loc_check_value_max_slot_index(FlJsonValue* value);
