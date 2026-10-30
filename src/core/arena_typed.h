#pragma once

#include "arena.h"
#include "core.h"
#include "types.h"
#include <assert.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Type-safe arena implementation
//
// Usage:
//   arena_typed(i32) my_i32_arena;
//   arena_typed_init(&my_i32_arena, arena);
//   i32* value = arena_typed_alloc(&my_i32_arena);
//   u64 count = arena_typed_count(&my_i32_arena);
//   arena_typed_rewind(&my_i32_arena);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define arena_typed(element_type)    \
    struct {                         \
        FlArena* arena;              \
        void* start_pos;             \
        element_type* _element_type; \
    }

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Borrows an existing arena

#define arena_typed_init(typed_arena_ptr, arena_ptr)                    \
    do {                                                                \
        (typed_arena_ptr)->arena = (arena_ptr);                         \
        (typed_arena_ptr)->start_pos = fl_arena_current_pos(arena_ptr); \
        (typed_arena_ptr)->_element_type = nullptr;                     \
    } while (0)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define arena_typed_alloc(typed_arena_ptr)                                                                   \
    ({                                                                                                       \
        typedef __typeof__(*(typed_arena_ptr)->_element_type) _element_t;                                    \
        (_element_t*)fl_arena_alloc_raw((typed_arena_ptr)->arena, sizeof(_element_t), align_of(_element_t)); \
    })

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define arena_typed_rewind(typed_arena_ptr) arena_rewind_to((typed_arena_ptr)->arena, (typed_arena_ptr)->start_pos)

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Elements allocated since initialization

#define arena_typed_count(typed_arena_ptr)                                                                    \
    ({                                                                                                        \
        typedef __typeof__(*(typed_arena_ptr)->_element_type) _element_t;                                     \
        u64 _bytes_used                                                                                       \
            = (u64)((u8*)fl_arena_current_pos((typed_arena_ptr)->arena) - (u8*)(typed_arena_ptr)->start_pos); \
        _bytes_used / sizeof(_element_t);                                                                     \
    })
