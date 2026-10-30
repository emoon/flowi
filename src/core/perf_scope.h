#pragma once

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Always-on frame profiler - internal include seam over the generated <flowi/core/perf_scope.h>.
//
// Scope trees are recorded per thread in thread-local storage. The main thread prints its tree
// automatically when the frame exceeds the budget below.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include "core.h"
#include "types.h"

#include <flowi/core/perf_scope.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define PERF_SCOPE_MAX_DEPTH 32
#define PERF_SCOPE_MAX_ENTRIES 512
#define PERF_SCOPE_FRAME_BUDGET_NS 17000000ULL // 17ms

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal data structures

// Completed timing entry (stored in pre-order in entries array)
typedef struct PerfScopeEntry {
    const char* name;
    u64 start_ns;
    u64 elapsed_ns;
    u16 depth;
    u16 parent_index;
} PerfScopeEntry;

typedef struct PerfScopeOpen {
    const char* name;
    u64 start_ns;
    u16 entry_index;
} PerfScopeOpen;

typedef struct PerfScopeThreadState {
    PerfScopeEntry entries[PERF_SCOPE_MAX_ENTRIES];
    PerfScopeOpen stack[PERF_SCOPE_MAX_DEPTH];
    u32 entry_count;
    u32 stack_depth;
    bool overflow;
    bool is_main_thread;
} PerfScopeThreadState;

// Cleanup guard for __attribute__((cleanup))
typedef struct PerfScopeGuard {
    u32 stack_index;
} PerfScopeGuard;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Internal accessor - returns the calling thread's profiler state.

DLL_EXPORT PerfScopeThreadState* perf_scope_get_thread_state(void);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline void perf_scope_guard_cleanup(PerfScopeGuard* guard) {
    fl_perf_scope_end(guard->stack_index);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// User-facing macros

#define perf_scope(name)                                                                             \
    __attribute__((cleanup(perf_scope_guard_cleanup))) PerfScopeGuard CONCAT(_perf_guard_, __LINE__) \
        = { .stack_index = fl_perf_scope_begin(name) }

#define perf_scope_function() perf_scope(__func__)
