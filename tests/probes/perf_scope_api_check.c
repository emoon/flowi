///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Compile-only parity guard: the api_gen-generated public per-frame-profiler surface (<flowi/core/perf_scope.h>).
//
// This TU links nothing. It proves at compile time that the generated header is self-contained valid C against only
// the standard headers below, and that every fl_perf_scope_* prototype keeps its exact signature.
//
// The thread-local profiler buffers, the PerfScopeGuard cleanup struct, perf_scope_get_thread_state and the
// PERF_SCOPE_* constants stay internal (src/core/perf_scope.h), so there is no generated struct layout to
// guard here.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <flowi/core/perf_scope.h>
#include <flowi/core/perf_scope_guard.h>

// Per-prototype signature identity: a reordered/retyped arg or changed return fails the build.
#define FL_PERF_SCOPE_FN_MATCHES(fn, ...) \
    _Static_assert(__builtin_types_compatible_p(__typeof__(fn), __VA_ARGS__), #fn " signature drift")

FL_PERF_SCOPE_FN_MATCHES(fl_perf_scope_init, void(void));
FL_PERF_SCOPE_FN_MATCHES(fl_perf_scope_destroy, void(void));
FL_PERF_SCOPE_FN_MATCHES(fl_perf_scope_begin, uint32_t(const char*));
FL_PERF_SCOPE_FN_MATCHES(fl_perf_scope_end, void(uint32_t));
FL_PERF_SCOPE_FN_MATCHES(fl_perf_scope_frame_begin, void(void));
FL_PERF_SCOPE_FN_MATCHES(fl_perf_scope_frame_end, void(void));

static void __attribute__((unused)) perf_scope_guard_compiles(void) {
    fl_perf_scope_function();
    fl_perf_scope("compile-check");
}

#undef FL_PERF_SCOPE_FN_MATCHES
