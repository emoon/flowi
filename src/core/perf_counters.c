#include "perf_counters.h"

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Performance counters
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#if PLATFORM_LINUX
#include "os/perf_counters_linux.inc"
#else

// Linux is the only platform with a performance-counter seam. Nothing initializes here, so no
// session is ever handed out and the calls that take one have nothing but null to refuse.

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
