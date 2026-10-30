#pragma once

#include "types.h"

struct FlArena;
struct FlString;

/// Must be called after arena_tracker_init().
void arena_watchdog_init(void);

/// Must be called before arena_tracker_destroy().
void arena_watchdog_destroy(void);

/// Call from main loop every frame. Internally throttles to sample interval.
void arena_watchdog_update(void);

void arena_watchdog_set_enabled(bool enabled);

/// Set the sampling interval in seconds (default: 30).
void arena_watchdog_set_interval_seconds(u32 seconds);

/// Set how many consecutive growing samples trigger an alert (default: 3).
void arena_watchdog_set_growth_threshold(u32 consecutive_samples);

/// Set minimum total growth in bytes to trigger an alert (default: 4096).
void arena_watchdog_set_min_growth_bytes(u64 bytes);

/// Generate a report string (allocates into provided arena).
struct FlString arena_watchdog_report(struct FlArena* output_arena);
