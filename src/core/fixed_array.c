#include "fixed_array.h"
#include "arena.h"
#include "core.h"
#include "assert.h"
#include "memory.h"
#include "types.h"
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fixed_array_init_raw(ArrayBase* array, struct FlArena* arena, u32 capacity, u32 element_size) {
    FL_ASSERT(array != nullptr);
    FL_ASSERT(arena != nullptr);
    FL_ASSERT(capacity > 0);
    FL_ASSERT(element_size > 0);

    u64 total_size = (u64)capacity * (u64)element_size;
    void* data = fl_arena_alloc_raw(arena, total_size, 8);

    array->capacity = capacity;
    array->length = 0;
    array->data = data;
    array->element_size = element_size;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* fixed_array_remove_swap_raw(ArrayBase* array, u32 index, u32 element_size) {
    FL_ASSERT(array != nullptr);
    FL_ASSERT(element_size == array->element_size);

    if (index >= array->length) {
        return nullptr;
    }

    void* element_to_remove = (u8*)array->data + (index * element_size);

    array->length--;

    if (index < array->length) {
        void* last_element = (u8*)array->data + (array->length * element_size);
        memory_copy(element_to_remove, element_size, last_element, element_size);
    }

    return element_to_remove;
}
