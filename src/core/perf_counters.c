#include "perf_counters.h"

#if PLATFORM_LINUX
#include "os/perf_counters_linux.inc"
#else

// Only Linux implements performance counters. Here init fails, no session is ever created, and
// the calls that take one refuse nullptr.

bool perf_counters_init(void) {
    return false;
}

void perf_counters_shutdown(void) {}

bool perf_counters_available(void) {
    return false;
}

PerfCounterSession* perf_counter_session_create(struct FlArena* arena) {
    (void)arena;
    return nullptr;
}

void perf_counter_session_destroy(PerfCounterSession* session) {
    (void)session;
}

bool perf_counter_session_configure(PerfCounterSession* session, PerfCounterEvent event) {
    (void)session;
    (void)event;
    return false;
}

bool perf_counter_session_start(PerfCounterSession* session) {
    (void)session;
    return false;
}

bool perf_counter_session_stop(PerfCounterSession* session, PerfCounterValue* out_value) {
    (void)session;
    (void)out_value;
    return false;
}

#endif
