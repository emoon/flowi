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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Memory Tracker Implementation
//
// Collects arena and mimalloc statistics on-demand and outputs formatted reports.

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_memory_tracker_init(void) {
    arena_tracker_init();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void fl_memory_tracker_destroy(void) {
    arena_tracker_destroy();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
    FlStringBuilder* sb;
    FlArena* output_arena;
    u32 current_thread_id;
    bool first_arena_in_thread;
} ArenaDumpData;

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

    {
        arena_scratch_auto_conflict(temp, data->output_arena);
        double percent = arena->reserved_size > 0 ? (arena->pos * 100.0) / arena->reserved_size : 0.0;

        FlString size_str;
        if (arena->pos >= GB(1)) {
            size_str = fl_sprintf_arena(temp.arena, "%.2f GB", arena->pos / (double)GB(1));
        } else if (arena->pos >= MB(1)) {
            size_str = fl_sprintf_arena(temp.arena, "%.2f MB", arena->pos / (double)MB(1));
        } else if (arena->pos >= KB(1)) {
            size_str = fl_sprintf_arena(temp.arena, "%.2f KB", arena->pos / (double)KB(1));
        } else {
            size_str = fl_sprintf_arena(temp.arena, "%llu bytes", arena->pos);
        }

        if (arena->pos >= KB(1)) {
            sb_append_line(data->sb, data->output_arena, "    Current:   %S (%llu bytes) [%.1f%%]\n", size_str,
                           arena->pos, percent);
        } else {
            sb_append_line(data->sb, data->output_arena, "    Current:   %S [%.1f%%]\n", size_str, percent);
        }
    }

    {
        arena_scratch_auto_conflict(temp, data->output_arena);
        u64 peak_offset = atomic_load_explicit(&arena->peak_offset, memory_order_relaxed);
        double percent = arena->reserved_size > 0 ? (peak_offset * 100.0) / arena->reserved_size : 0.0;

        FlString size_str;
        if (peak_offset >= GB(1)) {
            size_str = fl_sprintf_arena(temp.arena, "%.2f GB", peak_offset / (double)GB(1));
        } else if (peak_offset >= MB(1)) {
            size_str = fl_sprintf_arena(temp.arena, "%.2f MB", peak_offset / (double)MB(1));
        } else if (peak_offset >= KB(1)) {
            size_str = fl_sprintf_arena(temp.arena, "%.2f KB", peak_offset / (double)KB(1));
        } else {
            size_str = fl_sprintf_arena(temp.arena, "%llu bytes", peak_offset);
        }

        if (peak_offset >= KB(1)) {
            sb_append_line(data->sb, data->output_arena, "    Peak:      %S (%llu bytes) [%.1f%%]\n", size_str,
                           peak_offset, percent);
        } else {
            sb_append_line(data->sb, data->output_arena, "    Peak:      %S [%.1f%%]\n", size_str, percent);
        }
    }

    {
        arena_scratch_auto_conflict(temp, data->output_arena);
        i64 current_time_us = get_current_time_us_os();
        i64 age_us = current_time_us - (i64)arena->creation_timestamp_us;

        FlString age_str;
        if (age_us < 0) {
            age_str = FL_S("0.0s");
        } else {
            double age_seconds = age_us / 1000000.0;
            if (age_seconds >= 3600.0) {
                age_str = fl_sprintf_arena(temp.arena, "%.1fh", age_seconds / 3600.0);
            } else if (age_seconds >= 60.0) {
                age_str = fl_sprintf_arena(temp.arena, "%.1fm", age_seconds / 60.0);
            } else {
                age_str = fl_sprintf_arena(temp.arena, "%.1fs", age_seconds);
            }
        }

        sb_append_line(data->sb, data->output_arena, "    Age:       %S\n\n", age_str);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_memory_tracker_dump_string(FlArena* arena) {
    FL_VALIDATE_RET(arena != nullptr, string_empty());

    FlStringBuilder sb = fl_sb_create(arena);

    sb = fl_sb_append_cstr(sb, "=== Memory Snapshot ===\n");

    {
        arena_scratch_auto_conflict(temp, arena);
        i64 current_time_us = get_current_time_us_os();
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

    {
        arena_scratch_auto_conflict(temp, arena);
        double percent
            = stats.total_arena_capacity > 0 ? (stats.total_arena_used * 100.0) / stats.total_arena_capacity : 0.0;

        FlString size_str;
        if (stats.total_arena_used >= GB(1)) {
            size_str = fl_sprintf_arena(temp.arena, "%.2f GB", stats.total_arena_used / (double)GB(1));
        } else if (stats.total_arena_used >= MB(1)) {
            size_str = fl_sprintf_arena(temp.arena, "%.2f MB", stats.total_arena_used / (double)MB(1));
        } else if (stats.total_arena_used >= KB(1)) {
            size_str = fl_sprintf_arena(temp.arena, "%.2f KB", stats.total_arena_used / (double)KB(1));
        } else {
            size_str = fl_sprintf_arena(temp.arena, "%llu bytes", stats.total_arena_used);
        }

        if (stats.total_arena_used >= KB(1)) {
            sb_append_line(&sb, arena, "Total arena used: %S (%llu bytes) [%.1f%%]\n", size_str, stats.total_arena_used,
                           percent);
        } else {
            sb_append_line(&sb, arena, "Total arena used: %S [%.1f%%]\n", size_str, percent);
        }
    }

    return fl_sb_to_string(sb);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_memory_tracker_dump_to_file(FlString filepath) {
    FL_VALIDATE_RET(!string_is_empty(filepath), false);

    arena_scratch_auto(temp);
    FlString content = fl_memory_tracker_dump_string(temp.arena);

    i64 bytes_written = file_write_os(filepath, (const u8*)content.data, (i64)content.length);

    if (bytes_written < 0) {
        log_error("Failed to write memory snapshot to file: %S", filepath);
        return false;
    }

    return true;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct {
    FlStringBuilder* sb;
    FlArena* output_arena;
    bool first;
} JsonArenaDumpData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void dump_arena_json_callback(FlArena* arena, void* user_data) {
    JsonArenaDumpData* data = (JsonArenaDumpData*)user_data;

    if (!data->first) {
        *data->sb = fl_sb_append_cstr(*data->sb, ",\n");
    }
    data->first = false;

    u64 peak_offset = atomic_load_explicit(&arena->peak_offset, memory_order_relaxed);
    double utilization = arena->reserved_size > 0 ? (arena->pos * 100.0) / arena->reserved_size : 0.0;
    double peak_utilization = arena->reserved_size > 0 ? (peak_offset * 100.0) / arena->reserved_size : 0.0;

    i64 current_time_us = get_current_time_us_os();
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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FlString fl_memory_tracker_export_json_string(FlArena* arena) {
    FL_VALIDATE_RET(arena != nullptr, string_empty());

    FlStringBuilder sb = fl_sb_create(arena);

    MimallocStats mstats = mem_get_mimalloc_stats();
    FlMemoryStats stats;
    fl_memory_tracker_get_stats(&stats);

    u64 mimalloc_current = mstats.total_allocated - mstats.total_freed;

    i64 current_time_us = get_current_time_us_os();

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool fl_memory_tracker_export_json_to_file(FlString filepath) {
    FL_VALIDATE_RET(!string_is_empty(filepath), false);

    arena_scratch_auto(temp);
    FlString content = fl_memory_tracker_export_json_string(temp.arena);

    i64 bytes_written = file_write_os(filepath, (const u8*)content.data, (i64)content.length);

    if (bytes_written < 0) {
        log_error("Failed to write JSON snapshot to file: %S", filepath);
        return false;
    }

    return true;
}
