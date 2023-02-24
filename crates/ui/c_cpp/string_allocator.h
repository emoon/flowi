#pragma once

#include <flowi/manual.h>
#include "linear_allocator.h"

struct FlAllocator;

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct StringAllocator {
    struct FlAllocator* allocator;
    LinearAllocator* frame_allocator;
    LinearAllocator tracking;
    int string_count;
} StringAllocator;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool StringAllocator_create(StringAllocator* self, struct FlAllocator* allocator, LinearAllocator* frame_allocator);
void StringAllocator_destroy(StringAllocator* self);

/// Frame based allocator. FlString will be invalid after next update
FlString StringAllocator_copy_cstr_frame(StringAllocator* self, const char* str);
FlString StringAllocator_copy_string_frame(StringAllocator* self, FlString str);
FlString StringAllocator_copy_cstr(StringAllocator* self, const char* str);
FlString StringAllocator_copy_string(StringAllocator* self, FlString str);

// Notice that this string is only valid for the local scope of the calling code
const char* StringAllocator_temp_string_to_cstr(StringAllocator* self, char* temp_buffer, int temp_len, FlString str);

// Ideally this should not be called unless if some really large buffer is being removed as the tracking is slow
void StringAllocator_free_string(StringAllocator* ctx, FlString str);

#ifdef __cplusplus
}   
#endif
