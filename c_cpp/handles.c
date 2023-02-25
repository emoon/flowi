#include "handles.h"
#include "allocator.h"
#include "types.h"

#define HANDLE_SHIFT (32)
#define HANDLE_BITS (1LL << HANDLE_SHIFT)
#define HANDLE_MASK (HANDLE_BITS - 1)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static u32 handle_index(u64 handle) {
    return (u32)((handle) >> HANDLE_SHIFT);
}

FL_INLINE u32 handle_inner(u64 handle) {
    return (u32)((handle)&HANDLE_MASK);
}

FL_INLINE u64 make_handle(u32 index, u32 inner) {
    return (u64)((((u64)index) << HANDLE_SHIFT) | inner);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Handles_create_impl(Handles* self, FlAllocator* allocator, int capacity, int entry_size) {
    u8* objs_free_slots = NULL;
    capacity = capacity == 0 ? 16 : capacity;
    const int object_size = entry_size * capacity;
    const int free_slot_size = sizeof(u32) * capacity;

    // Lock_create(self->lock);

    objs_free_slots = FlAllocator_alloc(allocator, object_size + free_slot_size);

    self->allocator = allocator;
    self->objects = objs_free_slots;
    self->free_slots = (u32*)(objs_free_slots + object_size);
    self->len = 0;
    self->capacity = capacity;
    self->object_size = entry_size;
    self->free_slots_count = 0;
    self->next_inner_id = 1;

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Handles_destroy(Handles* self) {
    FlAllocator_free(self->allocator, self->objects);
    self->objects = NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool Handles_is_valid(Handles* self, uint64_t id) {
    const u32 index = handle_index(id);
    const u32 inner = handle_inner(id);
    u64 handle = *(u64*)(self->objects + (self->object_size * index));
    return handle_inner(handle) == inner;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// It's assumed that all objects allocated has id first thus this returns a pointer to the
// correct entry in the array with u64 at the start filled in, but each object is of the
// size created when allocatding the handles (Handles_create)

void* Handles_create_handle(Handles* self) {
    u64* handle_ptr = NULL;
    // Lock_lock(self->lock);
    u32 inner_id = self->next_inner_id++;
    const u32 free_slots_count = self->free_slots_count;
    const u32 object_size = self->object_size;

    if (free_slots_count > 0) {
        u32 index = free_slots_count - 1;
        u32 free_index = self->free_slots[index];

        handle_ptr = (u64*)(self->objects + (free_index * object_size));
        *handle_ptr = make_handle(free_index, inner_id);

        self->free_slots_count = index;
    } else {
        int offset = self->len;
        int len = self->len + 1;

        // Realloc if we are out of space
        if (len > self->capacity) {
            u8* objs_free_slots = NULL;
            const int cap = self->capacity * 2;
            const int object_size = self->object_size * cap;
            const int free_slots_size = sizeof(u32) * cap;
            const int size = object_size + free_slots_size;

            objs_free_slots = FlAllocator_realloc(self->allocator, self->objects, size);

            self->objects = objs_free_slots;
            self->free_slots = (u32*)(objs_free_slots + object_size);
            self->capacity = cap;
        }

        handle_ptr = (u64*)(self->objects + (self->object_size * offset));
        *handle_ptr = make_handle(offset, inner_id);

        self->len = len;
    }

    // Lock_unlock(self->lock);
    return handle_ptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* Handles_get_data(Handles* self, u64 id) {
    const int index = (int)handle_index(id);
    const u32 inner = handle_inner(id);

    // Lock_lock(self->lock);
    if (index >= 0 && index < self->len) {
        u64* data = (u64*)(self->objects + (self->object_size * index));
        if (handle_inner(*data) == inner) {
            // Lock_unlock(flowi_ctx->global_state->lock);
            return data;
        }
    }
    // Lock_unlock(self->lock);

    return NULL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void Handles_remove_handle(Handles* self, u64 id) {
    const int index = (int)handle_index(id);
    const u32 inner = handle_inner(id);

    // Lock_lock(self->lock);
    if (index >= 0 && index < self->len) {
        u64 handle = *(u64*)(self->objects + (self->object_size * index));
        // Validate that handle is valid before removing it
        if (handle_inner(handle) == inner) {
            // mark the index as invalid
            *(u64*)(self->objects + (self->object_size * index)) = 0;
            // add index to free slots
            u32 offset = self->free_slots_count++;
            self->free_slots[offset] = index;
        }
    }
    // Lock_unlock(self->lock);
}
