#pragma once

#include "core.h"
#include "types.h"

struct FlArena;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Performance counters - hardware performance monitoring
//
// Instructions retired is the only event here. Linux reads it through perf_event_open; no other
// platform has a seam for it, so counters report unavailable there and every call below refuses.
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef enum PerfCounterEvent {
    PERF_COUNTER_INSTRUCTIONS,
} PerfCounterEvent;

// Matches hardware counter width
typedef u64 PerfCounterValue;

typedef struct PerfCounterSession PerfCounterSession;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Core API

// Must succeed before anything else below does anything. False when no counter could be opened: no
// PMU, no permission to read one, or a platform with no seam at all.
bool perf_counters_init(void);

// Returns the subsystem to its uninitialized state; a later init may probe again.
void perf_counters_shutdown(void);

// Whether a counter can be opened right now. False until a successful perf_counters_init().
bool perf_counters_available(void);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Session
//
// A session owns one hardware counter. Null unless perf_counters_init() has succeeded.

PerfCounterSession* perf_counter_session_create(struct FlArena* arena);

// Closes the counter. Null-safe, and the arena still owns the session memory afterwards.
void perf_counter_session_destroy(PerfCounterSession* session);

// Opens the counter for event, replacing any counter the session already held.
bool perf_counter_session_configure(PerfCounterSession* session, PerfCounterEvent event);

// Zeroes the counter and starts counting. Fails on an unconfigured or already-running session.
bool perf_counter_session_start(PerfCounterSession* session);

// Stops counting and writes the count. On failure the session still stops and *out_value is
// left untouched.
bool perf_counter_session_stop(PerfCounterSession* session, PerfCounterValue* out_value);

#if PLATFORM_LINUX
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Event mapping (Linux)

// The two perf_event_attr fields that select an event: type is a PERF_TYPE_* value and config the
// event code within that type.
typedef struct PerfCounterPlatformEvent {
    u32 type;
    u64 config;
} PerfCounterPlatformEvent;

// Decodes event into the pair that selects it, or returns false for an event with no mapping.
bool perf_counter_event_to_platform_event(PerfCounterEvent event, PerfCounterPlatformEvent* out_event);
#endif
