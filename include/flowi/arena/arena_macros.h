#pragma once

// Typed allocation and scoped-cleanup macros over the arena API - the public convenience surface
// a C caller uses instead of the raw byte/alignment entry points. Every macro expands to a call
// declared in the generated <flowi/arena/arena.h>; nothing here is a symbol of its own, so a
// plugin that only links the shared library gets all of it.
//
// C only: the typed macros lean on `_Alignof`, and C++ callers (plain-C++ plugins) use the raw
// entry points directly. The header is still safe to include from C++; it just defines nothing.

#include <flowi/arena/arena.h>
#include <flowi/core/platform.h>
#include <stdint.h>

#ifndef __cplusplus

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Element count to byte count
//
// Saturating, not wrapping: a count whose byte size does not fit asks for UINT64_MAX bytes, which the
// allocator rejects as a size overflow. A wrapped product would instead pass the allocator's checks and
// hand back a block far shorter than the `count` elements the caller goes on to write.

static inline uint64_t arena_array_bytes(uint64_t count, uint64_t element_size) {
    return count > UINT64_MAX / element_size ? UINT64_MAX : count * element_size;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Typed allocation (thread-safe by default)

#define arena_alloc(arena, type) ((type*)fl_arena_alloc_raw(arena, sizeof(type), _Alignof(type)))

#define arena_pos_type(arena, type) ((type*)fl_arena_aligned_pos(arena, _Alignof(type)))

#define arena_alloc_array(arena, type, count) \
    ((type*)fl_arena_alloc_raw(arena, arena_array_bytes((count), sizeof(type)), _Alignof(type)))

#define arena_alloc_zero(arena, type) ((type*)fl_arena_alloc_raw_zero(arena, sizeof(type), _Alignof(type)))

#define arena_alloc_array_zero(arena, type, count) \
    ((type*)fl_arena_alloc_raw_zero(arena, arena_array_bytes((count), sizeof(type)), _Alignof(type)))

// Single-threaded variants - only for an arena no other thread touches.
#define arena_alloc_st(arena, type) ((type*)fl_arena_alloc_raw_st(arena, sizeof(type), _Alignof(type)))

#define arena_alloc_array_st(arena, type, count) \
    ((type*)fl_arena_alloc_raw_st(arena, arena_array_bytes((count), sizeof(type)), _Alignof(type)))

#define arena_alloc_zero_st(arena, type) ((type*)fl_arena_alloc_raw_zero_st(arena, sizeof(type), _Alignof(type)))

#define arena_alloc_array_zero_st(arena, type, count) \
    ((type*)fl_arena_alloc_raw_zero_st(arena, arena_array_bytes((count), sizeof(type)), _Alignof(type)))

#define arena_compact(arena, type, count_to_release) \
    fl_arena_compact_bytes(arena, arena_array_bytes((count_to_release), sizeof(type)))

// Complete elements of `type` between start_ptr and the arena's current position.
#define arena_count(arena, start_ptr, type) fl_arena_count_elements(arena, start_ptr, sizeof(type))

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Scoped cleanup (GCC/Clang): a temp or scratch scope that ends itself when the enclosing block exits.

#if COMPILER_CLANG || COMPILER_GCC

static inline void arena_scratch_cleanup(FlTempArena* temp) {
    if (temp && temp->arena) {
        fl_arena_scratch_end(*temp);
    }
}

static inline void arena_temp_cleanup(FlTempArena* temp) {
    if (temp && temp->arena) {
        fl_arena_temp_end(*temp);
    }
}

#define arena_scratch_auto(name) \
    __attribute__((cleanup(arena_scratch_cleanup))) FlTempArena name = fl_arena_scratch_begin()

#define arena_scratch_auto_conflict(name, conflict_arena) \
    __attribute__((cleanup(arena_scratch_cleanup))) FlTempArena name = fl_arena_scratch_begin_conflict(conflict_arena)

#define arena_temp_auto(name, arena) \
    __attribute__((cleanup(arena_temp_cleanup))) FlTempArena name = fl_arena_temp_begin(arena)

#endif // COMPILER_CLANG || COMPILER_GCC

#endif // !__cplusplus
