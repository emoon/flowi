#include "memory_tracker.h"
#include "arena.h"
#include "core.h"
#include "assert.h"
#include "string.h"
#include "log.h"
#include "memory.h"
#include "os/os.h"
#include "sprintf.h"
#include <stdarg.h>

// Collects arena and mimalloc statistics on demand and formats them into reports.

static void sb_append_line(FlStringBuilder* sb, FlArena* output_arena, const char* format, ...) {
    arena_scratch_auto_conflict(temp, output_arena);

    va_list args;
    va_start(args, format);
    FlString line = vsprintf_arena(temp.arena, format, args);
    va_end(args);

    *sb = fl_sb_append_string(*sb, line);
}

static void sb_append_size(FlStringBuilder* sb, FlArena* output_arena, const char* label, u64 bytes) {
    arena_scratch_auto_conflict(temp, output_arena);

    FlString size_str;
    if (bytes >= GB(1)) {
        size_str = fl_sprintf_arena(temp.arena, "%.2f GB", bytes / (double)GB(1));
    } else if (bytes >= MB(1)) {
        size_str = fl_sprintf_arena(temp.arena, "%.2f MB", bytes / (double)MB(1));
    } else if (bytes >= KB(1)) {
        size_str = fl_sprintf_arena(temp.arena, "%.2f KB", bytes / (double)KB(1));
    } else {
        size_str = fl_sprintf_arena(temp.arena, "%llu bytes", bytes);
    }

    if (bytes >= KB(1)) {
        sb_append_line(sb, output_arena, "%s%S (%llu bytes)\n", label, size_str, bytes);
    } else {
        sb_append_line(sb, output_arena, "%s%S\n", label, size_str);
    }
}

static void sb_append_number(FlStringBuilder* sb, FlArena* output_arena, const char* label, u64 number) {
    arena_scratch_auto_conflict(temp, output_arena);

    FlString num_str;
    if (number < 1000) {
        num_str = fl_sprintf_arena(temp.arena, "%llu", number);
    } else {
        // Format with commas
        FlString temp_str = fl_sprintf_arena(temp.arena, "%llu", number);
        i32 len = (i32)temp_str.length;
        i32 comma_count = (len - 1) / 3;
        i32 result_len = len + comma_count;

        char* buffer = arena_alloc_array(temp.arena, char, result_len + 1);
        i32 src = len - 1;
        i32 dst = result_len - 1;
        i32 count = 0;

        buffer[dst + 1] = '\0';

        while (src >= 0) {
            if (count == 3) {
                buffer[dst--] = ',';
                count = 0;
            }
            buffer[dst--] = temp_str.data[src--];
            count++;
        }

        num_str = (FlString) { .data = buffer, .length = result_len };
    }

    sb_append_line(sb, output_arena, "%s%S\n", label, num_str);
}

void fl_memory_tracker_init(void) {
    arena_tracker_init();
}

void fl_memory_tracker_destroy(void) {
    arena_tracker_destroy();
}

typedef struct {
    u32 count;
    u64 total_capacity;
    u64 total_used;
    u64 total_peak;
} ArenaStatsCollector;

static void collect_arena_stats_callback(FlArena* arena, void* user_data) {
    ArenaStatsCollector* collector = (ArenaStatsCollector*)user_data;

    collector->count++;
    collector->total_capacity += arena->reserved_size;
    collector->total_used += arena->pos;
    collector->total_peak += atomic_load_explicit(&arena->peak_offset, memory_order_relaxed);
}

void fl_memory_tracker_get_stats(FlMemoryStats* stats) {
    FL_VALIDATE(stats != nullptr);

    memory_zero(stats, sizeof(FlMemoryStats));

    MimallocStats mstats = mem_get_mimalloc_stats();

    stats->mimalloc_total_allocated = mstats.total_allocated;
    stats->mimalloc_total_freed = mstats.total_freed;
    stats->mimalloc_current = mstats.total_allocated - mstats.total_freed;
    stats->mimalloc_alloc_count = mstats.alloc_count;
    stats->mimalloc_free_count = mstats.free_count;

    ArenaStatsCollector collector = { 0 };
    arena_tracker_iterate_all(collect_arena_stats_callback, &collector);

    stats->live_arena_count = collector.count;
    stats->total_arena_capacity = collector.total_capacity;
    stats->total_arena_used = collector.total_used;
    stats->total_arena_peak = collector.total_peak;
}

typedef struct {
    FlStringBuilder* sb;
    FlArena* output_arena;
    u32 current_thread_id;
    bool first_arena_in_thread;
} ArenaDumpData;

static FlString format_size_text(FlArena* arena, u64 bytes) {
    if (bytes >= GB(1)) {
        return fl_sprintf_arena(arena, "%.2f GB", bytes / (double)GB(1));
    }
    if (bytes >= MB(1)) {
        return fl_sprintf_arena(arena, "%.2f MB", bytes / (double)MB(1));
    }
    if (bytes >= KB(1)) {
        return fl_sprintf_arena(arena, "%.2f KB", bytes / (double)KB(1));
    }
    return fl_sprintf_arena(arena, "%llu bytes", bytes);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static FlString format_age_text(FlArena* arena, i64 age_us) {
    if (age_us < 0) {
        return FL_S("0.0s");
    }

    double age_seconds = age_us / 1000000.0;
    if (age_seconds >= 3600.0) {
        return fl_sprintf_arena(arena, "%.1fh", age_seconds / 3600.0);
    }
    if (age_seconds >= 60.0) {
        return fl_sprintf_arena(arena, "%.1fm", age_seconds / 60.0);
    }
    return fl_sprintf_arena(arena, "%.1fs", age_seconds);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
    const char* label;
    u64 used;
    u64 capacity;
} UsageLine;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void append_usage_line(FlStringBuilder* sb, FlArena* output_arena, UsageLine line) {
    arena_scratch_auto_conflict(temp, output_arena);
    double percent = line.capacity > 0 ? (line.used * 100.0) / line.capacity : 0.0;
    FlString size_str = format_size_text(temp.arena, line.used);

    if (line.used >= KB(1)) {
        sb_append_line(sb, output_arena, "%s%S (%llu bytes) [%.1f%%]\n", line.label, size_str, line.used, percent);
    } else {
        sb_append_line(sb, output_arena, "%s%S [%.1f%%]\n", line.label, size_str, percent);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void append_arena_age_line(FlStringBuilder* sb, FlArena* output_arena, const FlArena* arena) {
    arena_scratch_auto_conflict(temp, output_arena);
    i64 age_us = fl_os_time_us() - (i64)arena->creation_timestamp_us;
    FlString age_str = format_age_text(temp.arena, age_us);

    sb_append_line(sb, output_arena, "    Age:       %S\n\n", age_str);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void dump_arena_callback(FlArena* arena, void* user_data) {
    ArenaDumpData* data = (ArenaDumpData*)user_data;

    if (arena->owner_thread_id != data->current_thread_id) {
        if (data->current_thread_id != 0) {
            *data->sb = fl_sb_append_cstr(*data->sb, "\n");
        }
        sb_append_line(data->sb, data->output_arena, "Thread %u:\n", arena->owner_thread_id);
        data->current_thread_id = arena->owner_thread_id;
        data->first_arena_in_thread = true;
    }

    sb_append_line(data->sb, data->output_arena, "  Arena \"%s:%d\":\n", arena->allocation_site_file,
                   arena->allocation_site_line);

    sb_append_size(data->sb, data->output_arena, "    Capacity:  ", arena->reserved_size);

    append_usage_line(data->sb, data->output_arena, (UsageLine) { "    Current:   ", arena->pos, arena->reserved_size });

    u64 peak_offset = atomic_load_explicit(&arena->peak_offset, memory_order_relaxed);
    append_usage_line(data->sb, data->output_arena, (UsageLine) { "    Peak:      ", peak_offset, arena->reserved_size });

    append_arena_age_line(data->sb, data->output_arena, arena);
}

FlString fl_memory_tracker_dump_string(FlArena* arena) {
    FL_VALIDATE_RET(arena != nullptr, string_empty());

    FlStringBuilder sb = fl_sb_create(arena);

    sb = fl_sb_append_cstr(sb, "=== Memory Snapshot ===\n");

    {
        arena_scratch_auto_conflict(temp, arena);
        i64 current_time_us = fl_os_time_us();
        FlString timestamp_str = timestamp_format_os(temp.arena, current_time_us);
        sb_append_line(&sb, arena, "Generated: %S\n\n", timestamp_str);
    }

    MimallocStats mstats = mem_get_mimalloc_stats();
    sb = fl_sb_append_cstr(sb, "--- mimalloc Stats ---\n");
    sb_append_size(&sb, arena, "Total Allocated:  ", mstats.total_allocated);
    sb_append_size(&sb, arena, "Total Freed:      ", mstats.total_freed);

    u64 mimalloc_current = mstats.total_allocated - mstats.total_freed;
    sb_append_size(&sb, arena, "Current:          ", mimalloc_current);

    sb_append_number(&sb, arena, "Allocation count: ", mstats.alloc_count);
    sb_append_number(&sb, arena, "Free count:       ", mstats.free_count);

    i64 potential_leaks = (i64)mstats.alloc_count - (i64)mstats.free_count;
    if (potential_leaks > 0) {
        sb_append_number(&sb, arena, "Potential leaks:  ", (u64)potential_leaks);
    }

    sb = fl_sb_append_cstr(sb, "\n--- Live Arenas ---\n");

    ArenaDumpData dump_data
        = { .sb = &sb, .output_arena = arena, .current_thread_id = 0, .first_arena_in_thread = true };
    arena_tracker_iterate_all(dump_arena_callback, &dump_data);

    FlMemoryStats stats;
    fl_memory_tracker_get_stats(&stats);

    sb = fl_sb_append_cstr(sb, "--- Summary ---\n");
    sb_append_number(&sb, arena, "Total live arenas: ", stats.live_arena_count);
    sb_append_size(&sb, arena, "Total arena capacity: ", stats.total_arena_capacity);

    append_usage_line(&sb, arena, (UsageLine) { "Total arena used: ", stats.total_arena_used, stats.total_arena_capacity });

    return fl_sb_to_string(sb);
}

static bool write_snapshot_file(FlString filepath, FlString content, const char* what) {
    i64 bytes_written = file_write_os(filepath, (const u8*)content.data, (i64)content.length);

    if (bytes_written < 0) {
        log_error("Failed to write %s to file: %S", what, filepath);
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_memory_tracker_dump_to_file(FlString filepath) {
    FL_VALIDATE_RET(!string_is_empty(filepath), false);

    arena_scratch_auto(temp);
    FlString content = fl_memory_tracker_dump_string(temp.arena);

    return write_snapshot_file(filepath, content, "memory snapshot");
}

typedef struct {
    FlStringBuilder* sb;
    FlArena* output_arena;
    bool first;
} JsonArenaDumpData;

static void dump_arena_json_callback(FlArena* arena, void* user_data) {
    JsonArenaDumpData* data = (JsonArenaDumpData*)user_data;

    if (!data->first) {
        *data->sb = fl_sb_append_cstr(*data->sb, ",\n");
    }
    data->first = false;

    u64 peak_offset = atomic_load_explicit(&arena->peak_offset, memory_order_relaxed);
    double utilization = arena->reserved_size > 0 ? (arena->pos * 100.0) / arena->reserved_size : 0.0;
    double peak_utilization = arena->reserved_size > 0 ? (peak_offset * 100.0) / arena->reserved_size : 0.0;

    i64 current_time_us = fl_os_time_us();
    i64 age_us = current_time_us - (i64)arena->creation_timestamp_us;
    double age_seconds = age_us >= 0 ? age_us / 1000000.0 : 0.0;

    arena_scratch_auto_conflict(temp, data->output_arena);
    FlString entry
        = fl_sprintf_arena(temp.arena,
                           "    {\n"
                           "      \"location\": \"%s:%d\",\n"
                           "      \"thread_id\": %u,\n"
                           "      \"capacity\": %llu,\n"
                           "      \"current\": %llu,\n"
                           "      \"peak\": %llu,\n"
                           "      \"utilization\": %.1f,\n"
                           "      \"peak_utilization\": %.1f,\n"
                           "      \"age_seconds\": %.1f\n"
                           "    }",
                           arena->allocation_site_file, arena->allocation_site_line, arena->owner_thread_id,
                           arena->reserved_size, arena->pos, peak_offset, utilization, peak_utilization, age_seconds);

    *data->sb = fl_sb_append_string(*data->sb, entry);
}

FlString fl_memory_tracker_export_json_string(FlArena* arena) {
    FL_VALIDATE_RET(arena != nullptr, string_empty());

    FlStringBuilder sb = fl_sb_create(arena);

    MimallocStats mstats = mem_get_mimalloc_stats();
    FlMemoryStats stats;
    fl_memory_tracker_get_stats(&stats);

    u64 mimalloc_current = mstats.total_allocated - mstats.total_freed;

    i64 current_time_us = fl_os_time_us();

    {
        arena_scratch_auto_conflict(temp, arena);
        FlString timestamp_str = timestamp_format_os(temp.arena, current_time_us);
        FlString header = fl_sprintf_arena(temp.arena,
                                           "{\n"
                                           "  \"timestamp\": \"%S\",\n"
                                           "  \"mimalloc\": {\n"
                                           "    \"total_allocated\": %llu,\n"
                                           "    \"total_freed\": %llu,\n"
                                           "    \"current\": %llu,\n"
                                           "    \"alloc_count\": %llu,\n"
                                           "    \"free_count\": %llu\n"
                                           "  },\n"
                                           "  \"arenas\": [\n",
                                           timestamp_str, mstats.total_allocated, mstats.total_freed, mimalloc_current,
                                           mstats.alloc_count, mstats.free_count);

        sb = fl_sb_append_string(sb, header);
    }

    JsonArenaDumpData dump_data = { .sb = &sb, .output_arena = arena, .first = true };
    arena_tracker_iterate_all(dump_arena_json_callback, &dump_data);

    {
        arena_scratch_auto_conflict(temp, arena);
        double total_utilization
            = stats.total_arena_capacity > 0 ? (stats.total_arena_used * 100.0) / stats.total_arena_capacity : 0.0;

        FlString footer = fl_sprintf_arena(temp.arena,
                                           "\n  ],\n"
                                           "  \"summary\": {\n"
                                           "    \"total_arenas\": %u,\n"
                                           "    \"total_capacity\": %llu,\n"
                                           "    \"total_used\": %llu,\n"
                                           "    \"total_utilization\": %.3f\n"
                                           "  }\n"
                                           "}\n",
                                           stats.live_arena_count, stats.total_arena_capacity, stats.total_arena_used,
                                           total_utilization / 100.0);

        sb = fl_sb_append_string(sb, footer);
    }

    return fl_sb_to_string(sb);
}

bool fl_memory_tracker_export_json_to_file(FlString filepath) {
    FL_VALIDATE_RET(!string_is_empty(filepath), false);

    arena_scratch_auto(temp);
    FlString content = fl_memory_tracker_export_json_string(temp.arena);

    return write_snapshot_file(filepath, content, "JSON snapshot");
}
