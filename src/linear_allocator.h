#pragma once

#include "types.h"

struct FlAllocator;

#ifdef __cplusplus
extern "C" {
#endif

// TODO: VirtualAlloc based allocator
typedef struct LinearAllocator {
    u8* start_data;
    u8* end_data;
    u8* current_data;
    struct FlAllocator* allocator;
    const char* id;
    bool allow_realloc;
} LinearAllocator;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void LinearAllocator_create(LinearAllocator* self, const char* name, u8* data, int len);
bool LinearAllocator_create_with_allocator(LinearAllocator* self, const char* name, struct FlAllocator* allocator,
                                           int len, bool allow_realloc);
void LinearAllocator_destroy(LinearAllocator* self);
void LinearAllocator_update_resize(LinearAllocator* self, u8* data, int new_len);

u8* LinearAllocator_internal_alloc(LinearAllocator* self, int size, int alignement);
u8* LinearAllocator_internal_alloc_zero(LinearAllocator* self, int size, int alignement);

u8* LinearAllocator_internal_alloc_unaligned(LinearAllocator* self, int size);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FL_INLINE void LinearAllocator_rewind(LinearAllocator* self) {
    self->current_data = self->start_data;
}

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
#define LinearAllocator_alloc_zero(state, type) \
    (type*)LinearAllocator_internal_alloc_zero(state, sizeof(type), FL_ALIGNOF(type))
#define LinearAllocator_alloc_array_zero(state, type, count) \
    (type*)LinearAllocator_internal_alloc_zero(state, sizeof(type) * count, FL_ALIGNOF(type))

#define LinearAllocator_alloc_unaligend(state, type) \
    (type*)LinearAllocator_internal_alloc_unaligned(state, sizeof(type))

#ifdef __cplusplus
}
#endif
