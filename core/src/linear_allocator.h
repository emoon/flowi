#pragma once

#include "allocator.h"
#include "internal.h"
#include "types.h"

// TODO: VirtualAlloc based allocator
typedef struct LinearAllocator {
    const char* id;
    u8* start_data;
    u8* end_data;
    u8* current_data;
    FlAllocator* allocator;
    bool allow_realloc;
} LinearAllocator;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LinearAllocator_create(LinearAllocator* self, const char* name, u8* data, int len);
bool LinearAllocator_create_with_allocator(LinearAllocator* self, const char* name, FlAllocator* allocator, int len,
                                           bool allow_realloc);
void LinearAllocator_destroy(LinearAllocator* self);

void LinearAllocator_rewind(LinearAllocator* alloc);
void LinearAllocator_update_resize(LinearAllocator* alloc, u8* data, int new_len);

u8* LinearAllocator_internal_alloc(LinearAllocator* s, int size, int alignement);
u8* LinearAllocator_internal_alloc_zero(LinearAllocator* s, int size, int alignement);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get how much memory is left

FL_INLINE int LinearAllocator_memory_left(LinearAllocator* alloc) {
    uintptr_t memory_left = (uintptr_t)alloc->end_data - (uintptr_t)alloc->current_data;
    return (int)memory_left;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Get the current writing position in the memory stream

FL_INLINE int LinearAllocator_current_position(LinearAllocator* alloc) {
    uintptr_t pos = (uintptr_t)alloc->current_data - (uintptr_t)alloc->start_data;
    return (int)pos;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define LinearAllocator_alloc(state, type) (type*)LinearAllocator_internal_alloc(state, sizeof(type), FL_ALIGNOF(type))
#define LinearAllocator_alloc_array(state, type, count) \
    (type*)LinearAllocator_internal_alloc(state, sizeof(type) * count, FL_ALIGNOF(type))
#define LinearAllocator_alloc_zero(state, typecount) \
    (type*)LinearAllocator_internal_alloc_zero(state, sizeof(type), FL_ALIGNOF(type))
#define LinearAllocator_alloc_array_zero(state, type, count) \
    (type*)LinearAllocator_internal_alloc_zero(state, sizeof(type) * count, FL_ALIGNOF(type))
