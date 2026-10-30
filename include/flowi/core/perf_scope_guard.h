#pragma once

#include <flowi/core/perf_scope.h>

#define FL_PERF_SCOPE_CONCAT_(a, b) a##b
#define FL_PERF_SCOPE_CONCAT(a, b) FL_PERF_SCOPE_CONCAT_(a, b)

typedef struct FlPerfScopeGuard {
    uint32_t handle;
} FlPerfScopeGuard;

static inline void fl_perf_scope_guard_cleanup(FlPerfScopeGuard* guard) {
    fl_perf_scope_end(guard->handle);
}

#if defined(__clang__) || defined(__GNUC__)
#define fl_perf_scope(name)                                                                      \
    __attribute__((cleanup(fl_perf_scope_guard_cleanup))) FlPerfScopeGuard FL_PERF_SCOPE_CONCAT( \
        _fl_perf_scope_guard_, __LINE__) = { .handle = fl_perf_scope_begin(name) }
#define fl_perf_scope_function() fl_perf_scope(__func__)
#else
#error "Automatic performance-scope cleanup requires GCC or Clang"
#endif
