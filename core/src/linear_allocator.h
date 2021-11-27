#pragma once

#include <stdalign.h>
#include "types.h"
#include "internal.h"

// TODO: VirtualAlloc based allocator
typedef struct LinearAllocator {
    const char* id;
    u8* start_data;
    u8* end_data;
    u8* current_data;
} LinearAllocator;

void LinearAllocator_init(LinearAllocator* alloc, const char* name, u8* data, int len);
void LinearAllocator_rewind(LinearAllocator* alloc);
u8* LinearAllocator_internal_alloc(LinearAllocator* s, int size, int alignement);
u8* LinearAllocator_internal_alloc_zero(LinearAllocator* s, int size, int alignement);

#define LinearAllocator_alloc(state, type) \
    (type*)LinearAllocator_internal_alloc(state, sizeof(type), FL_ALIGNOF(type))
#define LinearAllocator_alloc_array(state, type, count) \
    (type*)LinearAllocator_internal_alloc(state, sizeof(type) * count, FL_ALIGNOF(type))
#define LinearAllocator_alloc_zero(state, typecount) \
    (type*)LinearAllocator_internal_alloc_zero(state, sizeof(type), FL_ALIGNOF(type))
#define LinearAllocator_alloc_array_zero(state, type, count) \
    (type*)LinearAllocator_internal_alloc_zero(state, sizeof(type) * count, FL_ALIGNOF(type))

