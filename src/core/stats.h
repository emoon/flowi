#pragma once

#include "arena.h"
#include "core.h"
#include "string.h"
#include "os/os.h"
#include "types.h"
#include <stdatomic.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct StatCounter {
    _Atomic u64 value;
    bool enabled;
    FlString name;
    FlString category;
} StatCounter;

typedef struct StatsRegistry StatsRegistry;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Returns a registry allocated from the arena
StatsRegistry* stats_create(FlArena* arena);
void stats_destroy(StatsRegistry* self);

// Register a new counter (thread-safe, call at startup)
u32 stats_register_counter(StatsRegistry* self, FlString name, FlString category);

// Get counter by ID (fast, use this in hot paths)
StatCounter* stats_get_counter(StatsRegistry* self, u32 counter_id);

// Find counter ID by name and category (slower, use for setup/testing)
u32 stats_find_counter(StatsRegistry* self, FlString name, FlString category);

void stats_enable_counter(StatsRegistry* self, u32 counter_id, bool enabled);

void stats_enable_category(StatsRegistry* self, FlString category, bool enabled);

u64 stats_get_value(StatsRegistry* self, u32 counter_id);

u64 stats_get_value_by_name(StatsRegistry* self, FlString name, FlString category);

void stats_reset_counter(StatsRegistry* self, u32 counter_id);

void stats_reset_counter_by_name(StatsRegistry* self, FlString name, FlString category);

void stats_print_category(StatsRegistry* self, FlString category);

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Increment counter (thread-safe)
#define stats_inc(registry, counter_id)                                  \
    do {                                                                 \
        StatCounter* _counter = stats_get_counter(registry, counter_id); \
        if (FL_LIKELY(_counter && _counter->enabled)) {                  \
            atomic_fetch_add(&_counter->value, 1);                       \
        }                                                                \
    } while (0)
