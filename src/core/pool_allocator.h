#pragma once

#include "arena.h"
#include "core.h"

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pool allocator implementation with type safety using typeof
//
// Usage:
//   pool(Node) node_pool;
//   pool_new(&node_pool, arena);
//   Node* node = pool_alloc(&node_pool);
//   pool_free(&node_pool, node);
//
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Base pool structure (type-erased)

typedef struct PoolAllocatorBase {
    FlArena* arena;
    void* free_list;
    u64 item_size;
    u64 item_alignment;
} PoolAllocatorBase;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Type-safe pool declaration using typeof

#define pool(element_type)           \
    struct {                         \
        PoolAllocatorBase base;      \
        element_type* _element_type; \
    }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define pool_new(pool_ptr, arena_ptr)                                                          \
    do {                                                                                       \
        typedef __typeof__(*(pool_ptr)->_element_type) _element_t;                             \
        pool_init_raw(&(pool_ptr)->base, arena_ptr, sizeof(_element_t), _Alignof(_element_t)); \
        (pool_ptr)->_element_type = nullptr;                                                   \
    } while (0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define pool_alloc(pool_ptr)                                       \
    ({                                                             \
        typedef __typeof__(*(pool_ptr)->_element_type) _element_t; \
        (_element_t*)pool_alloc_raw(&(pool_ptr)->base);            \
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define pool_free(pool_ptr, ptr_val)                                                   \
    do {                                                                               \
        typedef __typeof__(*(pool_ptr)->_element_type) _element_t;                     \
        _Static_assert(__builtin_types_compatible_p(__typeof__(ptr_val), _element_t*), \
                       "pool_free: pointer type mismatch");                            \
        pool_free_raw(&(pool_ptr)->base, ptr_val);                                     \
    } while (0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Raw interface (used by macros)

void pool_init_raw(PoolAllocatorBase* self, FlArena* arena, u64 item_size, u64 item_alignment);
void* pool_alloc_raw(PoolAllocatorBase* self);
void pool_free_raw(PoolAllocatorBase* self, void* ptr);
