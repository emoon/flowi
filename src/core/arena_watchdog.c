#include "arena_watchdog.h"
#include "arena.h"
#include "arena_alloc_tracker.h"
#include "assert.h"
#include "string.h"
#include "log.h"
#include "memory.h"
#include "os/os.h"
#include "sprintf.h"
#include <stdatomic.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Arena Growth Watchdog
//
// Uses mi_malloc for its own storage to avoid a circular dependency with arenas.

#define WATCHDOG_RING_SIZE 8
#define WATCHDOG_INITIAL_CAPACITY 64
#define WATCHDOG_DEFAULT_INTERVAL_US (30 * 1000000LL)
#define WATCHDOG_DEFAULT_GROWTH_THRESHOLD 3
#define WATCHDOG_DEFAULT_MIN_GROWTH_BYTES 4096

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
    const FlArena* arena;
    const char* file;
    int line;
    u64 creation_timestamp; // Identity check (detect arena reuse at same address)
    u64 samples[WATCHDOG_RING_SIZE];
    u32 sample_index;   // Current ring buffer write position
    u32 sample_count;   // Total samples collected (capped at WATCHDOG_RING_SIZE)
    u64 last_alert_pos; // Suppress duplicate alerts at same position
    bool seen;          // Marked true during iteration, false = stale
} WatchdogEntry;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
    WatchdogEntry* entries;
    u32 count;
    u32 capacity;
    i64 last_sample_time_us;
    i64 sample_interval_us;
    u32 growth_threshold;
    u64 min_growth_bytes;
    bool enabled;
    bool initialized;
} ArenaWatchdog;

static ArenaWatchdog s_watchdog = { 0 };

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static WatchdogEntry* find_entry(const FlArena* arena, u64 creation_timestamp) {
    for (u32 i = 0; i < s_watchdog.count; i++) {
        WatchdogEntry* entry = &s_watchdog.entries[i];
        if (entry->arena == arena && entry->creation_timestamp == creation_timestamp) {
            return entry;
        }
    }
    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static WatchdogEntry* add_entry(const FlArena* arena) {
    if (s_watchdog.count >= s_watchdog.capacity) {
        u32 new_capacity = s_watchdog.capacity * 2;
        WatchdogEntry* new_entries
            = (WatchdogEntry*)mi_realloc(s_watchdog.entries, new_capacity * sizeof(WatchdogEntry));
        if (new_entries == nullptr) {
            log_error("arena_watchdog: failed to grow entry array");
            return nullptr;
        }
        s_watchdog.entries = new_entries;
        s_watchdog.capacity = new_capacity;
    }

    WatchdogEntry* entry = &s_watchdog.entries[s_watchdog.count++];
    memory_zero(entry, sizeof(WatchdogEntry));
    entry->arena = arena;
    entry->file = arena->allocation_site_file;
    entry->line = arena->allocation_site_line;
    entry->creation_timestamp = arena->creation_timestamp_us;
    return entry;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void remove_entry(u32 index) {
    FL_ASSERT_DEBUG(index < s_watchdog.count);
    s_watchdog.entries[index] = s_watchdog.entries[s_watchdog.count - 1];
    s_watchdog.count--;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static bool check_monotonic_growth(const WatchdogEntry* entry, u32 threshold, u64 min_bytes) {
    if (entry->sample_count < threshold) {
        return false;
    }

    // The last 'threshold' samples in the ring buffer must be strictly increasing
    u32 ring_size = WATCHDOG_RING_SIZE;
    u32 start_offset = (entry->sample_index + ring_size - threshold) % ring_size;

    u64 first_sample = entry->samples[start_offset];
    u64 prev = first_sample;

    for (u32 i = 1; i < threshold; i++) {
        u32 idx = (start_offset + i) % ring_size;
        u64 current = entry->samples[idx];
        if (current <= prev) {
            return false;
        }
        prev = current;
    }

    u64 last_sample = prev;
    u64 total_growth = last_sample - first_sample;
    return total_growth >= min_bytes;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void sample_arena_callback(FlArena* arena, void* user_data) {
    (void)user_data;

    u64 pos = atomic_load_explicit(&arena->pos, memory_order_relaxed);
    u64 creation_ts = arena->creation_timestamp_us;

    WatchdogEntry* entry = find_entry(arena, creation_ts);
    if (entry == nullptr) {
        entry = add_entry(arena);
        if (entry == nullptr) {
            return;
        }
    }

    entry->samples[entry->sample_index] = pos;
    entry->sample_index = (entry->sample_index + 1) % WATCHDOG_RING_SIZE;
    if (entry->sample_count < WATCHDOG_RING_SIZE) {
        entry->sample_count++;
    }

    entry->seen = true;

    if (check_monotonic_growth(entry, s_watchdog.growth_threshold, s_watchdog.min_growth_bytes)) {
        u64 latest_pos = entry->samples[(entry->sample_index + WATCHDOG_RING_SIZE - 1) % WATCHDOG_RING_SIZE];

        if (latest_pos != entry->last_alert_pos) {
            u32 start_offset
                = (entry->sample_index + WATCHDOG_RING_SIZE - s_watchdog.growth_threshold) % WATCHDOG_RING_SIZE;
            u64 first_sample = entry->samples[start_offset];
            u64 growth = latest_pos - first_sample;

            log_warning("arena_watchdog: sustained growth detected in arena \"%s:%d\" "
                        "(+%llu bytes over %u samples, current pos: %llu)",
                        entry->file, entry->line, growth, s_watchdog.growth_threshold, latest_pos);

            entry->last_alert_pos = latest_pos;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void cleanup_stale_entries(void) {
    u32 i = 0;
    while (i < s_watchdog.count) {
        if (!s_watchdog.entries[i].seen) {
            remove_entry(i);
            // Don't increment i - swapped entry needs checking
        } else {
            s_watchdog.entries[i].seen = false;
            i++;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void arena_watchdog_init(void) {
    if (s_watchdog.initialized) {
        return;
    }

    s_watchdog.entries = (WatchdogEntry*)mi_zalloc(WATCHDOG_INITIAL_CAPACITY * sizeof(WatchdogEntry));
    if (s_watchdog.entries == nullptr) {
        log_error("arena_watchdog: failed to allocate entry array");
        return;
    }

    s_watchdog.capacity = WATCHDOG_INITIAL_CAPACITY;
    s_watchdog.count = 0;
    s_watchdog.last_sample_time_us = get_current_time_us_os();
    s_watchdog.sample_interval_us = WATCHDOG_DEFAULT_INTERVAL_US;
    s_watchdog.growth_threshold = WATCHDOG_DEFAULT_GROWTH_THRESHOLD;
    s_watchdog.min_growth_bytes = WATCHDOG_DEFAULT_MIN_GROWTH_BYTES;

#ifdef NDEBUG
    s_watchdog.enabled = false;
#else
    s_watchdog.enabled = true;
#endif

    s_watchdog.initialized = true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void arena_watchdog_destroy(void) {
    if (!s_watchdog.initialized) {
        return;
    }

    mi_free(s_watchdog.entries);
    memory_zero(&s_watchdog, sizeof(ArenaWatchdog));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void arena_watchdog_update(void) {
    if (!s_watchdog.initialized || !s_watchdog.enabled) {
        return;
    }

    i64 now = get_current_time_us_os();
    if (now - s_watchdog.last_sample_time_us < s_watchdog.sample_interval_us) {
        return;
    }

    s_watchdog.last_sample_time_us = now;

    arena_tracker_iterate_all(sample_arena_callback, nullptr);

    cleanup_stale_entries();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void arena_watchdog_set_enabled(bool enabled) {
    s_watchdog.enabled = enabled;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void arena_watchdog_set_interval_seconds(u32 seconds) {
    s_watchdog.sample_interval_us = (i64)seconds * 1000000LL;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void arena_watchdog_set_growth_threshold(u32 consecutive_samples) {
    if (consecutive_samples < 2) {
        consecutive_samples = 2;
    }
    if (consecutive_samples > WATCHDOG_RING_SIZE) {
        consecutive_samples = WATCHDOG_RING_SIZE;
    }
    s_watchdog.growth_threshold = consecutive_samples;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void arena_watchdog_set_min_growth_bytes(u64 bytes) {
    s_watchdog.min_growth_bytes = bytes;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void sb_append_line(FlStringBuilder* sb, FlArena* output_arena, const char* format, ...) {
    arena_scratch_auto_conflict(temp, output_arena);

    va_list args;
    va_start(args, format);
    FlString line = vsprintf_arena(temp.arena, format, args);
    va_end(args);

    *sb = fl_sb_append_string(*sb, line);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
    FlStringBuilder* sb;
    FlArena* output_arena;
    u32 growing_count;
    u32 total_count;
} ReportCallbackData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void build_report_entries(ReportCallbackData* data) {
    for (u32 i = 0; i < s_watchdog.count; i++) {
        const WatchdogEntry* entry = &s_watchdog.entries[i];
        data->total_count++;

        bool growing = check_monotonic_growth(entry, s_watchdog.growth_threshold, s_watchdog.min_growth_bytes);
        if (growing) {
            data->growing_count++;
        }

        const char* status = growing ? "GROWING" : "stable";

        if (entry->sample_count > 0) {
            u64 oldest_sample
                = entry->samples[(entry->sample_index + WATCHDOG_RING_SIZE - entry->sample_count) % WATCHDOG_RING_SIZE];
            u64 newest_sample = entry->samples[(entry->sample_index + WATCHDOG_RING_SIZE - 1) % WATCHDOG_RING_SIZE];
            i64 delta = (i64)newest_sample - (i64)oldest_sample;

            sb_append_line(data->sb, data->output_arena, "  [%s] %s:%d  pos=%llu  delta=%+lld  samples=%u\n", status,
                           entry->file, entry->line, newest_sample, delta, entry->sample_count);
        } else {
            sb_append_line(data->sb, data->output_arena, "  [%s] %s:%d  (no samples yet)\n", status, entry->file,
                           entry->line);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString arena_watchdog_report(FlArena* output_arena) {
    FL_VALIDATE_RET(output_arena != nullptr, string_empty());

    if (!s_watchdog.initialized) {
        return FL_S("Arena watchdog not initialized.\n");
    }

    FlStringBuilder sb = fl_sb_create(output_arena);
    sb = fl_sb_append_cstr(sb, "=== Arena Growth Watchdog Report ===\n");

    sb_append_line(&sb, output_arena, "Status: %s\n", s_watchdog.enabled ? "enabled" : "disabled");
    sb_append_line(&sb, output_arena, "Sample interval: %lld seconds\n", s_watchdog.sample_interval_us / 1000000LL);
    sb_append_line(&sb, output_arena, "Growth threshold: %u consecutive samples\n", s_watchdog.growth_threshold);
    sb_append_line(&sb, output_arena, "Min growth bytes: %llu\n\n", s_watchdog.min_growth_bytes);

    if (s_watchdog.count == 0) {
        sb = fl_sb_append_cstr(sb, "No arenas tracked yet.\n");
        return fl_sb_to_string(sb);
    }

    sb = fl_sb_append_cstr(sb, "Tracked arenas:\n");

    ReportCallbackData report_data = {
        .sb = &sb,
        .output_arena = output_arena,
        .growing_count = 0,
        .total_count = 0,
    };
    build_report_entries(&report_data);

    sb_append_line(&sb, output_arena, "\nSummary: %u/%u arenas showing sustained growth\n", report_data.growing_count,
                   report_data.total_count);

    return fl_sb_to_string(sb);
}
