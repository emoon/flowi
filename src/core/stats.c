#include "stats.h"
#include "arena.h"
#include "core.h"
#include "assert.h"
#include "string.h"
#include "log.h"
#include "os/os.h"
#include "sprintf.h"
#include <stdio.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct StatsRegistry {
    StatCounter* counters;
    u32 counter_count;
    u32 counter_capacity;
    FlArena* arena;
    Mutex mutex; // Only for registration, not for increments
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

StatsRegistry* stats_create(FlArena* arena) {
    FL_ASSERT(arena != nullptr);

    StatsRegistry* self = arena_alloc(arena, StatsRegistry);
    self->arena = arena;
    self->counter_capacity = 256;
    self->counters = arena_alloc_array(arena, StatCounter, 256);
    self->counter_count = 0;
    mutex_init(&self->mutex);

    log_debug("Statistics system initialized with capacity for %u counters", self->counter_capacity);

    return self;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void stats_destroy(StatsRegistry* self) {
    FL_VALIDATE(self != nullptr);

    mutex_destroy(&self->mutex);

    log_debug("Statistics system destroyed");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u32 stats_register_counter(StatsRegistry* self, FlString name, FlString category) {
    FL_ASSERT(name.data != nullptr && name.length > 0);
    FL_ASSERT(category.data != nullptr && category.length > 0);

    mutex_lock(&self->mutex);

    if (self->counter_count >= self->counter_capacity) {
        mutex_unlock(&self->mutex);
        log_error("Stats registry full (%u counters), cannot register %S.%S", self->counter_capacity, category, name);
        return UINT32_MAX;
    }

    u32 id = self->counter_count++;

    StatCounter* counter = &self->counters[id];
    atomic_store(&counter->value, 0);
    counter->enabled = true;
    counter->name = fl_string_copy(self->arena, name);
    counter->category = fl_string_copy(self->arena, category);

    mutex_unlock(&self->mutex);

    log_debug("Registered counter %u: %S.%S", id, category, name);

    return id;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

StatCounter* stats_get_counter(StatsRegistry* self, u32 counter_id) {
    if (!self || counter_id >= self->counter_count) {
        return nullptr;
    }
    return &self->counters[counter_id];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u32 stats_find_counter(StatsRegistry* self, FlString name, FlString category) {
    for_count(i, self->counter_count) {
        StatCounter* counter = &self->counters[i];
        if (fl_string_equals(counter->name, name) && fl_string_equals(counter->category, category)) {
            return i;
        }
    }

    return UINT32_MAX; // Counter not found
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void stats_enable_counter(StatsRegistry* self, u32 counter_id, bool enabled) {
    StatCounter* counter = stats_get_counter(self, counter_id);
    if (counter) {
        counter->enabled = enabled;
        log_debug("Counter %u (%S.%S) %s", counter_id, counter->category, counter->name,
                  enabled ? "enabled" : "disabled");
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void stats_enable_category(StatsRegistry* self, FlString category, bool enabled) {
    u32 affected_count = 0;
    for_count(i, self->counter_count) {
        StatCounter* counter = &self->counters[i];
        if (fl_string_equals(counter->category, category)) {
            counter->enabled = enabled;
            affected_count++;
        }
    }

    log_debug("Category %S %s (%u counters affected)", category, enabled ? "enabled" : "disabled", affected_count);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u64 stats_get_value(StatsRegistry* self, u32 counter_id) {
    StatCounter* counter = stats_get_counter(self, counter_id);
    if (!counter) {
        return 0;
    }
    return atomic_load(&counter->value);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

u64 stats_get_value_by_name(StatsRegistry* self, FlString name, FlString category) {
    u32 counter_id = stats_find_counter(self, name, category);
    if (counter_id == UINT32_MAX) {
        return 0;
    }
    return stats_get_value(self, counter_id);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void stats_reset_counter(StatsRegistry* self, u32 counter_id) {
    StatCounter* counter = stats_get_counter(self, counter_id);
    if (counter) {
        atomic_store(&counter->value, 0);
        log_debug("Reset counter %u (%S.%S)", counter_id, counter->category, counter->name);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void stats_reset_counter_by_name(StatsRegistry* self, FlString name, FlString category) {
    u32 counter_id = stats_find_counter(self, name, category);
    if (counter_id != UINT32_MAX) {
        stats_reset_counter(self, counter_id);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void stats_print_category(StatsRegistry* self, FlString category) {
    log_debug("=== %S Stats ===", category);

    bool found_any = false;
    for_count(i, self->counter_count) {
        StatCounter* counter = &self->counters[i];
        if (fl_string_equals(counter->category, category)) {
            u64 value = atomic_load(&counter->value);
            log_debug("%S: %llu%s", counter->name, value, counter->enabled ? "" : " (disabled)");
            found_any = true;
        }
    }

    if (!found_any) {
        log_debug("No counters found for category %S", category);
    }
}
