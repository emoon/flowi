#pragma once

#include "arena.h"
#include "core.h"
#include "assert.h"
#include "memory.h"
#include "types.h"
#include <stddef.h>
#include <string.h>

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Fixed-size array implementation using arena allocators with type safety
//
// Usage:
//   fixed_array(int) int_array;
//   fixed_array_new(&int_array, arena, 10);
//   fixed_array_add(&int_array, 42);
//   int* value_ptr = fixed_array_get_ptr(&int_array, 0);
//   for_each_fixed_array(value, &int_array) { ... }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Base array structure (type-erased)

typedef struct ArrayBase {
    u32 capacity;
    u32 length;
    void* data;
    u32 element_size;
} ArrayBase;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Type-safe array declaration

#define fixed_array(element_type)    \
    struct {                         \
        ArrayBase base;              \
        element_type* _element_type; \
    }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Initialize an array; initial_capacity is the maximum number of elements it can ever hold.

#define fixed_array_new(array_ptr, arena_ptr, initial_capacity)                                    \
    do {                                                                                           \
        typedef __typeof__(*(array_ptr)->_element_type) _element_t;                                \
        fixed_array_init_raw(&(array_ptr)->base, arena_ptr, initial_capacity, sizeof(_element_t)); \
        (array_ptr)->_element_type = nullptr;                                                      \
    } while (0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Returns a copy of the element at index, or a zero-initialized value if out of bounds.

#define fixed_array_get(array_ptr, index)                                                            \
    ({                                                                                               \
        typedef __typeof__(*(array_ptr)->_element_type) _element_t;                                  \
        const void* _ptr = fixed_array_get_raw_const(&(array_ptr)->base, index, sizeof(_element_t)); \
        _ptr ? *(const _element_t*)_ptr : (_element_t) { 0 };                                        \
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Returns a pointer to the element at index, or nullptr if out of bounds.

#define fixed_array_get_ptr(array_ptr, index)                                                  \
    ({                                                                                         \
        typedef __typeof__(*(array_ptr)->_element_type) _element_t;                            \
        (_element_t*)fixed_array_get_raw_const(&(array_ptr)->base, index, sizeof(_element_t)); \
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Adds to the end of the array. Returns the added element, or a zero-initialized element if full.

#define fixed_array_add(array_ptr, element_val)                                                                 \
    ({                                                                                                          \
        typedef __typeof__(*(array_ptr)->_element_type) _element_t;                                             \
        _Static_assert(__builtin_types_compatible_p(__typeof__(element_val), _element_t),                       \
                       "fixed_array_add: element type mismatch");                                               \
        _element_t _element = element_val;                                                                      \
        _element_t* _ptr = (_element_t*)fixed_array_add_raw(&(array_ptr)->base, &_element, sizeof(_element_t)); \
        _ptr ? *_ptr : (_element_t) { 0 };                                                                      \
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Set element at index. If index >= length, extends the array length to index + 1.

#define fixed_array_set(array_ptr, index, element_val)                                    \
    do {                                                                                  \
        typedef __typeof__(*(array_ptr)->_element_type) _element_t;                       \
        _Static_assert(__builtin_types_compatible_p(__typeof__(element_val), _element_t), \
                       "fixed_array_set: element type mismatch");                         \
        _element_t _element = element_val;                                                \
        fixed_array_set_raw(&(array_ptr)->base, index, &_element, sizeof(_element_t));    \
    } while (0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Swapback removal: O(1) but does not preserve order. Returns the removed element.

#define fixed_array_remove_swap(array_ptr, index)                                       \
    ({                                                                                  \
        typedef __typeof__(*(array_ptr)->_element_type) _element_t;                     \
        _element_t _result = { 0 };                                                     \
        if ((index) < (array_ptr)->base.length) {                                       \
            _result = *((_element_t*)(array_ptr)->base.data + (index));                 \
            fixed_array_remove_swap_raw(&(array_ptr)->base, index, sizeof(_element_t)); \
        }                                                                               \
        _result;                                                                        \
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Clear array (reset length to 0, keeps capacity)

#define fixed_array_clear(array_ptr) fixed_array_clear_raw(&(array_ptr)->base)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define fixed_array_is_empty(array_ptr) ((array_ptr)->base.length == 0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define fixed_array_is_full(array_ptr) ((array_ptr)->base.length >= (array_ptr)->base.capacity)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define fixed_array_length(array_ptr) ((array_ptr)->base.length)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define fixed_array_capacity(array_ptr) ((array_ptr)->base.capacity)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Array iteration macro; element_var is a copy of the element, not a pointer.

#define for_each_fixed_array(element_var, array_ptr)                                                                  \
    for (u32 _i = 0; _i < fixed_array_length(array_ptr); _i++)                                                        \
        for (__typeof__(*(array_ptr)->_element_type) element_var = fixed_array_get(array_ptr, _i), *_once = (void*)1; \
             _once; _once = 0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Raw interface (used by macros)

DLL_EXPORT void fixed_array_init_raw(ArrayBase* array, struct FlArena* arena, u32 capacity, u32 element_size);
DLL_EXPORT void* fixed_array_remove_swap_raw(ArrayBase* array, u32 index, u32 element_size);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Inline implementations

static inline void* fixed_array_get_raw(ArrayBase* array, u32 index, u32 element_size) {
    FL_ASSERT(array != nullptr);
    FL_ASSERT(element_size == array->element_size);

    if (index >= array->length) {
        return nullptr;
    }

    return (u8*)array->data + (index * element_size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline const void* fixed_array_get_raw_const(const ArrayBase* array, u32 index, u32 element_size) {
    FL_ASSERT(array != nullptr);
    FL_ASSERT(element_size == array->element_size);

    if (index >= array->length) {
        return nullptr;
    }

    return (const u8*)array->data + (index * element_size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void* fixed_array_add_raw(ArrayBase* array, const void* element, u32 element_size) {
    FL_ASSERT(array != nullptr);
    FL_ASSERT(element != nullptr);
    FL_ASSERT(element_size == array->element_size);

    if (array->length >= array->capacity) {
        return nullptr;
    }

    void* destination = (u8*)array->data + (array->length * element_size);
    memory_copy(destination, element_size, element, element_size);
    array->length++;

    return destination;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void fixed_array_set_raw(ArrayBase* array, u32 index, const void* element, u32 element_size) {
    FL_ASSERT(array != nullptr);
    FL_ASSERT(element != nullptr);
    FL_ASSERT(element_size == array->element_size);

    if (index >= array->capacity) {
        return;
    }

    void* destination = (u8*)array->data + (index * element_size);
    memory_copy(destination, element_size, element, element_size);

    if (index >= array->length) {
        array->length = index + 1;
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void fixed_array_clear_raw(ArrayBase* array) {
    FL_ASSERT(array != nullptr);
    array->length = 0;
}
