#pragma once

#include "core.h"
#include "assert.h"
#include "string.h"
#include "os/os.h"
#include "types.h"
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>

// Re-typedefs the FlArena tag from the generated public header (C11-legal) to add the
// cache-aligned _Atomic body below. Must NOT redefine FlTempArena - the static_asserts
// below pin its layout instead.
#include <flowi/arena/arena.h>

#define ARENA_SIZE 128

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct CACHE_ALIGNED FlArena {
    // Cache line 1: Hot contested field + cold fields to fill 64 bytes
    _Atomic u64 pos;
    void* ptr;
    u64 reserved_size;
    u64 page_size;
    // Floor for fl_arena_rewind. Normally ARENA_SIZE (just past this header); arena_mt_new_
    // raises it past the FlArenaMt wrapper it allocates at the arena base, so a full
    // rewind can never clobber the wrapper.
    u64 rewind_pos;
    struct FlArena* next;
    const char* allocation_site_file;
    struct FlArena* next_tracked;
    struct FlArena* prev_tracked;
    struct ArenaAllocTracker* alloc_tracker;
    // Cache line 2: Mostly-read field (checked every allocation, rarely written)
    _Atomic u64 committed_size;
    _Atomic u64 peak_offset;
    u64 creation_timestamp_us;
    u32 owner_thread_id;
    int allocation_site_line;
} FlArena;

static_assert(sizeof(FlTempArena) == 16, "FlTempArena size drift vs <flowi/arena/arena.h>");
static_assert(offsetof(FlTempArena, arena) == 0, "FlTempArena.arena offset drift");
static_assert(offsetof(FlTempArena, pos) == 8, "FlTempArena.pos offset drift");

static_assert(sizeof(FlArena) <= ARENA_SIZE, "FlArena size exceeds ARENA_SIZE");

typedef struct FlArenaMt {
    FlArena* arena;
    _Atomic u64 atomic_pos;
    _Atomic u64 atomic_committed_size;
} FlArenaMt;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ArenaSettings {
    u64 reserved_size;
    const char* allocation_site_file;
    int allocation_site_line;
} ArenaSettings;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// FlArena functions

// Arena allocations never return NULL - the arena traps/aborts on OOM.
#if COMPILER_GCC || COMPILER_CLANG
#define ARENA_RETURNS_NONNULL __attribute__((returns_nonnull))
#else
#define ARENA_RETURNS_NONNULL
#endif

// Entry points that <flowi/arena/arena.h> also declares carry FL_API; the rest use DLL_EXPORT.

DLL_EXPORT FlArena* arena_new_(const ArenaSettings* settings);

FL_API FlArena* fl_arena_create(u64 reserved_size, const char* file, int line);

// Thread-safe allocation (default)
FL_API ARENA_RETURNS_NONNULL void* fl_arena_alloc_raw(FlArena* arena, u64 size, u64 alignment);
FL_API ARENA_RETURNS_NONNULL void* fl_arena_alloc_raw_zero(FlArena* arena, u64 size, u64 alignment);

// Single-threaded allocation - use only when arena is not shared between threads
FL_API ARENA_RETURNS_NONNULL void* fl_arena_alloc_raw_st(FlArena* arena, u64 size, u64 alignment);
FL_API ARENA_RETURNS_NONNULL void* fl_arena_alloc_raw_zero_st(FlArena* arena, u64 size, u64 alignment);

FL_API void fl_arena_align(FlArena* arena, u64 alignment);
FL_API void fl_arena_rewind(FlArena* self);
// Rewind to pos, a pointer previously handed out by fl_arena_current_pos on the same arena.
// Clamped to the rewind floor, so a header below the floor survives.
DLL_EXPORT void arena_rewind_to(FlArena* self, const void* pos);
FL_API void fl_arena_destroy(FlArena* self);

DLL_EXPORT FlArenaMt* arena_mt_new_(const ArenaSettings* settings);
DLL_EXPORT void* arena_mt_alloc_raw(FlArenaMt* arena, u64 size, u64 alignment);
DLL_EXPORT void arena_mt_destroy(FlArenaMt* self);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

DLL_EXPORT void arena_scratch_init(void);
DLL_EXPORT void arena_scratch_destroy(void);
FL_API FlTempArena fl_arena_scratch_begin(void);
FL_API FlTempArena fl_arena_scratch_begin_conflict(const FlArena* self);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Memory Tracking API

void arena_tracker_init(void);
void arena_tracker_destroy(void);
void arena_tracker_register(FlArena* self);
void arena_tracker_unregister(FlArena* self);

// Iterate over all arenas (thread-safe). The global tracker mutex is held across every callback
// invocation, so arenas stay alive for the walk and a callback must not create or destroy one -
// the tracker mutex is non-recursive and would self-deadlock. Scratch-arena use is safe.
typedef void (*ArenaIteratorCallback)(FlArena* arena, void* user_data);
void arena_tracker_iterate_all(ArenaIteratorCallback callback, void* user_data);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// A flat, by-value snapshot of one arena's occupancy and where it was created.
// site_file borrows the arena's creation-site literal, so it lives as long as the program does.
typedef struct FlArenaStats {
    const char* site_file;
    u64 pos;
    u64 committed;
    u64 reserved;
    i32 site_line;
} FlArenaStats;

// The three counters are read with relaxed atomics and are not sampled as one instant: a concurrent
// allocation can land between them, so treat the result as a snapshot for reporting, not as an
// invariant-bearing tuple. Safe to call from inside an arena_tracker_iterate_all callback - it takes
// no lock.
DLL_EXPORT void arena_stats(FlArena* arena, FlArenaStats* out);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Pin the current position as the rewind floor: everything allocated so far survives fl_arena_rewind.

static inline void arena_pin_rewind_floor(FlArena* self) {
    self->rewind_pos = atomic_load_explicit(&self->pos, memory_order_relaxed);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define arena_new(...)            \
    arena_new_(&(ArenaSettings) { \
        .reserved_size = GB(2), .allocation_site_file = __FILE__, .allocation_site_line = __LINE__, __VA_ARGS__ })

#if CPU_ARM64
#define arena_mt_new()                    \
    arena_mt_new_(&(ArenaSettings) {      \
        .reserved_size = MB(512),         \
        .allocation_site_file = __FILE__, \
        .allocation_site_line = __LINE__, \
    })
#else
#define arena_mt_new()                    \
    arena_mt_new_(&(ArenaSettings) {      \
        .reserved_size = GB(4),           \
        .allocation_site_file = __FILE__, \
        .allocation_site_line = __LINE__, \
    })
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/// Typed allocation and scoped-cleanup macros live in the public companion header so plugins built
/// against the public include root get the same spellings host code uses.
#include <flowi/arena/arena_macros.h>

// FlArenaMt wrapper macros (delegates to FlArena thread-safe functions)
#define arena_mt_alloc(arena, type) ((type*)arena_mt_alloc_raw(arena, sizeof(type), _Alignof(type)))

#define arena_mt_alloc_array(arena, type, count) \
    ((type*)arena_mt_alloc_raw(arena, (count) * sizeof(type), _Alignof(type)))

#if !(COMPILER_CLANG || COMPILER_GCC)
#error "Automatic cleanup requires GCC or Clang compiler. MSVC is not supported."
#endif
