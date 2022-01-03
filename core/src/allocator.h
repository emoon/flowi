#pragma once

#include "types.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FlAllocator {
    void* (*alloc)(void* user_data, u64 bytes);
    void* (*memalign)(void* user_data, u64 align, u64 bytes);
    void* (*realloc)(void* user_data, void* ptr, u64 size);
    void (*free)(void* user_data, void* ptr);
    void* user_data;
} FlAllocator;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define FlAllocator_alloc(self, size) self->alloc(self->user_data, size)
#define FlAllocator_memalign(self, align, size) self->memalign(self->user_data, align, size)
#define FlAllocator_realloc(self, ptr, size) self->realloc(self->user_data, ptr, size)
#define FlAllocator_free(self, ptr) self->free(self->user_data, ptr)

#define FlAllocator_alloc_type(self, type) (type*)self->alloc(self->user_data, sizeof(type))
#define FlAllocator_alloc_array_type(self, count, type) (type*)self->alloc(self->user_data, count * sizeof(type))

