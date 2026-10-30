#pragma once

#include "arena.h"
#include "string.h"
#include "fixed_array.h"
#include "os/os.h"
#include "types.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// FlString Allocator with Free List Support
//
// Arena-backed, with a free list per power-of-2 size class.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define STRING_ALLOCATOR_NUM_BUCKETS 16
#define STRING_ALLOCATOR_MIN_SIZE_LOG2 3 // 8 bytes minimum
#define STRING_ALLOCATOR_MAX_FREE_LIST_SIZE 256

typedef struct StringAllocator {
    FlArena* arena;
    fixed_array(void*) free_lists[STRING_ALLOCATOR_NUM_BUCKETS];
    u32 min_size_log2;
    Mutex lock; // Thread-safety for free list operations
} StringAllocator;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core API

StringAllocator* string_allocator_new(FlArena* arena);
FlString string_allocator_alloc(StringAllocator* self, u64 size);
void string_allocator_free(StringAllocator* self, FlString str);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Helper functions

FlString string_allocator_copy(StringAllocator* self, FlString src);
