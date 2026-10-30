#include "pool_allocator.h"
#include "assert.h"
#include "memory.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct FreeListNode {
    struct FreeListNode* next;
} FreeListNode;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void pool_init_raw(PoolAllocatorBase* self, FlArena* arena, u64 item_size, u64 item_alignment) {
    FL_ASSERT(arena != nullptr);
    FL_ASSERT(item_size > 0);
    FL_ASSERT(item_alignment > 0);

    // pool_free_raw stores a pointer-sized FreeListNode in each freed item, so every slot must be
    // large enough and aligned enough to hold one. Clamp both up for tiny item types.
    if (item_size < sizeof(FreeListNode)) {
        item_size = sizeof(FreeListNode);
    }
    if (item_alignment < _Alignof(FreeListNode)) {
        item_alignment = _Alignof(FreeListNode);
    }

    self->arena = arena;
    self->free_list = nullptr;
    self->item_size = item_size;
    self->item_alignment = item_alignment;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void* pool_alloc_raw(PoolAllocatorBase* self) {
    FL_ASSERT(self->arena != nullptr);

    if (self->free_list != nullptr) {
        FreeListNode* free_node = (FreeListNode*)self->free_list;
        self->free_list = free_node->next;

        // Unpoison the entire item (was poisoned when freed)
        asan_unpoison_memory_region(free_node, self->item_size);

        memory_zero(free_node, self->item_size);
        return free_node;
    }

    return fl_arena_alloc_raw_zero(self->arena, self->item_size, self->item_alignment);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void pool_free_raw(PoolAllocatorBase* self, void* ptr) {
    FL_ASSERT(ptr != nullptr);

    FreeListNode* free_node = (FreeListNode*)ptr;
    free_node->next = (FreeListNode*)self->free_list;
    self->free_list = free_node;

    // Poison the freed memory except for the free list node header to detect use-after-free
    if (self->item_size > sizeof(FreeListNode)) {
        void* poison_start = (char*)ptr + sizeof(FreeListNode);
        u64 poison_size = self->item_size - sizeof(FreeListNode);
        asan_poison_memory_region(poison_start, poison_size);
    }
}