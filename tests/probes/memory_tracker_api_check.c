///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compile-only parity guard: the api_gen-generated public memory-tracker surface (<flowi/core/memory_tracker.h>).
// This TU links nothing.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// FlString is includer-provided: the generated <flowi/core/memory_tracker.h> names it without including it, so define
// it first.
#include <core/string.h>
// Self-includes <flowi/arena/arena.h> for FlArena.
#include <flowi/core/memory_tracker.h>

#define FL_MT_FN_MATCHES(fn, ...) \
    _Static_assert(__builtin_types_compatible_p(__typeof__(fn), __VA_ARGS__), #fn " signature drift")

FL_MT_FN_MATCHES(fl_memory_tracker_init, void(void));
FL_MT_FN_MATCHES(fl_memory_tracker_destroy, void(void));
FL_MT_FN_MATCHES(fl_memory_tracker_dump_string, FlString(FlArena*));
FL_MT_FN_MATCHES(fl_memory_tracker_dump_to_file, bool(FlString));
FL_MT_FN_MATCHES(fl_memory_tracker_export_json_string, FlString(FlArena*));
FL_MT_FN_MATCHES(fl_memory_tracker_export_json_to_file, bool(FlString));
FL_MT_FN_MATCHES(fl_memory_tracker_get_stats, void(FlMemoryStats*));

#undef FL_MT_FN_MATCHES

// FlMemoryStats POD layout: the uint32 count then eight uint64s, 8-byte aligned (4 bytes of tail padding after the
// leading count).
_Static_assert(sizeof(FlMemoryStats) == 72, "FlMemoryStats size drift");
_Static_assert(_Alignof(FlMemoryStats) == 8, "FlMemoryStats align drift");
_Static_assert(offsetof(FlMemoryStats, live_arena_count) == 0, "FlMemoryStats.live_arena_count offset drift");
_Static_assert(offsetof(FlMemoryStats, total_arena_capacity) == 8, "FlMemoryStats.total_arena_capacity offset drift");
_Static_assert(offsetof(FlMemoryStats, mimalloc_free_count) == 64, "FlMemoryStats.mimalloc_free_count offset drift");
