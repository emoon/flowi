#pragma once

#include "types.h"

struct FlAllocator;

#ifdef __cplusplus
extern "C" {
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct Handles {
    struct FlAllocator* allocator;
    u8* objects;
    u32* free_slots;
    int len;
    int capacity;
    int object_size;
    int free_slots_count;
    u32 next_inner_id;
} Handles;

bool Handles_create_impl(Handles* self, struct FlAllocator* allocator, int capacity, int entry_size);

void* Handles_create_handle(Handles* self);
void* Handles_get_data(Handles* self, u64 handle);
void Handles_remove_handle(Handles* self, u64 handle);
bool Handles_is_valid(Handles* self, uint64_t id);
void Handles_destroy(Handles* self);

#define Handles_create(self, allocator, cap, Type) Handles_create_impl(self, allocator, cap, sizeof(Type))

#ifdef __cplusplus
}
#endif
