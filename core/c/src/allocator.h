#pragma once

#include <stdlib.h>
#include <string.h>
#include "types.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// These flags will determine what happens when
typedef enum FlAllocatorError {
    /// After calling error_callback it will terminate the program (exit(1))
    FlAllocatorError_Exit,
    /// Continue execution. DANGER! Will likely lead to bad memory writes
    FlAllocatorError_ContinueExecution,
} FlAllocatorError;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FlAllocator {
    /// How to exit the program in case of error
    FlAllocatorError error_flags;
    /// User data passed to each of the functions above
    void* user_data;
    /// This will be called if alloc/memalign/realloc call is failes when required
    void (*memory_error)(void* user_data, const char* error_text, int text_size);
    /// Allocate memory given size (no alignment required)
    void* (*alloc)(void* user_data, u64 bytes);
    /// Allocate memory with correct alignment
    void* (*memalign)(void* user_data, u64 align, u64 bytes);
    /// Reallocate a chunk of memory
    void* (*realloc)(void* user_data, void* ptr, u64 size);
    /// Free memory
    void (*free)(void* user_data, void* ptr);
} FlAllocator;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FL_INLINE void* FlAllocator_handle_oom(FlAllocator* self, void* mem) {
    if (FL_UNLIKELY(!mem)) {
        self->memory_error(self->user_data, NULL, 0);
        if (self->error_flags == FlAllocatorError_ContinueExecution) {
            return 0;
        }

        exit(1);
    }

    return mem;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FL_INLINE void* FLAllocator_alloc_internal(FlAllocator* self, u64 size) {
    return FlAllocator_handle_oom(self, self->alloc(self->user_data, size));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FL_INLINE void* FlAllocator_memalign_internal(FlAllocator* self, u64 size, u32 alignment) {
    return FlAllocator_handle_oom(self, self->memalign(self->user_data, alignment, size));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FL_INLINE void* FlAllocator_realloc_internal(FlAllocator* self, void* ptr, u64 size) {
    return FlAllocator_handle_oom(self, self->realloc(self->user_data, ptr, size));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FL_INLINE void* FlAllocator_alloc_zero(FlAllocator* self, int size) {
    void* mem = FLAllocator_alloc_internal(self, size);
    memset(mem, 0, size);
    return mem;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define FlAllocator_alloc(self, size) FLAllocator_alloc_internal(self, size)
#define FlAllocator_memalign(self, size, align) FLAllocator_alloc_internal(self, size, align)
#define FlAllocator_realloc(self, ptr, size) FlAllocator_realloc_internal(self, ptr, size)
#define FlAllocator_free(self, ptr) self->free(self->user_data, ptr)

#define FlAllocator_alloc_type(self, type) (type*)FLAllocator_alloc_internal(self, sizeof(type))
#define FlAllocator_alloc_array_type(self, count, type) (type*)FLAllocator_alloc_internal(self, count * sizeof(type))

#define FlAllocator_alloc_zero_type(self, type) (type*)FlAllocator_alloc_zero(self, sizeof(type))
