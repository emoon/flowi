#include "arena_alloc_tracker.h"
#include "arena.h"
#include "string.h"
#include "log.h"
#include "memory.h"
#include "os/os.h"
#include "sprintf.h"

#define TRACKER_TABLE_SIZE 1024

typedef struct {
    const char* file;
    int line;
    u64 total_bytes;
    u64 alloc_count;
    bool occupied;
} TrackerSlot;

struct ArenaAllocTracker {
    TrackerSlot slots[TRACKER_TABLE_SIZE];
    Mutex mutex;
    u32 entry_count;
};

static u32 tracker_hash(const char* file, int line) {
    u64 h = (u64)(uintptr_t)file;
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= (u64)(u32)line;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return (u32)(h & (TRACKER_TABLE_SIZE - 1));
}

ArenaAllocTracker* arena_alloc_tracker_create(void) {
    ArenaAllocTracker* tracker = (ArenaAllocTracker*)mi_zalloc(sizeof(ArenaAllocTracker));
    FL_VALIDATE_RET(tracker != nullptr, nullptr);
    mutex_init(&tracker->mutex);
    return tracker;
}

void arena_alloc_tracker_destroy(ArenaAllocTracker* tracker) {
    FL_VALIDATE(tracker != nullptr);
    mutex_destroy(&tracker->mutex);
    mi_free(tracker);
}

void arena_alloc_tracker_record(ArenaAllocTracker* tracker, const char* file, int line, u64 size) {
    FL_VALIDATE(tracker != nullptr);

    mutex_lock(&tracker->mutex);

    u32 idx = tracker_hash(file, line);

    for (u32 i = 0; i < TRACKER_TABLE_SIZE; i++) {
        u32 slot_idx = (idx + i) & (TRACKER_TABLE_SIZE - 1);
        TrackerSlot* slot = &tracker->slots[slot_idx];

        if (!slot->occupied) {
            slot->file = file;
            slot->line = line;
            slot->total_bytes = size;
            slot->alloc_count = 1;
            slot->occupied = true;
            tracker->entry_count++;
            mutex_unlock(&tracker->mutex);
            return;
        }

        if (slot->file == file && slot->line == line) {
            slot->total_bytes += size;
            slot->alloc_count++;
            mutex_unlock(&tracker->mutex);
            return;
        }
    }

    // The table is full, so the record is dropped.
    mutex_unlock(&tracker->mutex);
}

void arena_alloc_tracker_reset(ArenaAllocTracker* tracker) {
    FL_VALIDATE(tracker != nullptr);

    mutex_lock(&tracker->mutex);
    memory_zero(tracker->slots, sizeof(tracker->slots));
    tracker->entry_count = 0;
    mutex_unlock(&tracker->mutex);
}

static int top_insert_pos(const ArenaAllocEntry* result, int result_count, int capacity, u64 total_bytes) {
    for (int i = 0; i < result_count; i++) {
        if (total_bytes > result[i].total_bytes) {
            return i;
        }
    }

    return result_count < capacity ? result_count : result_count - 1;
}

static int top_insert(ArenaAllocEntry* result, int result_count, int capacity, const TrackerSlot* slot) {
    bool has_room = result_count < capacity;

    if (!has_room && slot->total_bytes <= result[result_count - 1].total_bytes) {
        return result_count;
    }

    int insert_pos = top_insert_pos(result, result_count, capacity, slot->total_bytes);
    int shift_end = has_room ? result_count : capacity - 1;

    for (int j = shift_end; j > insert_pos; j--) {
        result[j] = result[j - 1];
    }

    result[insert_pos] = (ArenaAllocEntry) {
        .file = slot->file,
        .line = slot->line,
        .total_bytes = slot->total_bytes,
        .alloc_count = slot->alloc_count,
    };

    return has_room ? result_count + 1 : result_count;
}

static int top_collect(const ArenaAllocTracker* tracker, ArenaAllocEntry* result, int capacity) {
    int result_count = 0;

    for (u32 i = 0; i < TRACKER_TABLE_SIZE; i++) {
        const TrackerSlot* slot = &tracker->slots[i];
        if (slot->occupied) {
            result_count = top_insert(result, result_count, capacity, slot);
        }
    }

    return result_count;
}

int arena_alloc_tracker_get_top(ArenaAllocTracker* tracker, FlArena* output_arena, int max_entries,
                                ArenaAllocEntry** out_entries) {
    if (out_entries == nullptr) {
        return 0;
    }

    *out_entries = nullptr;

    if (tracker == nullptr || output_arena == nullptr) {
        return 0;
    }

    if (max_entries <= 0) {
        return 0;
    }

    mutex_lock(&tracker->mutex);

    int count = (int)tracker->entry_count;
    if (count == 0) {
        mutex_unlock(&tracker->mutex);
        return 0;
    }

    int collect_count = count < max_entries ? count : max_entries;
    ArenaAllocEntry* result = arena_alloc_array(output_arena, ArenaAllocEntry, collect_count);

    int result_count = top_collect(tracker, result, collect_count);

    mutex_unlock(&tracker->mutex);

    *out_entries = result;
    return result_count;
}

void arena_alloc_tracker_dump_log(ArenaAllocTracker* tracker, const char* arena_name, int max_entries) {
    FL_VALIDATE(tracker != nullptr);

    arena_scratch_auto(temp);
    ArenaAllocEntry* entries = nullptr;
    int count = arena_alloc_tracker_get_top(tracker, temp.arena, max_entries, &entries);

    if (count == 0) {
        log_info("arena_alloc_tracker: no allocations recorded for arena \"%s\"", arena_name);
        return;
    }

    log_info("arena_alloc_tracker: top %d callsites for arena \"%s\":", count, arena_name);
    for (int i = 0; i < count; i++) {
        ArenaAllocEntry* e = &entries[i];
        log_info("  #%d  %s:%d  total=%.2f MB  count=%llu", i + 1, e->file, e->line, e->total_bytes / (1024.0 * 1024.0),
                 (unsigned long long)e->alloc_count);
    }
}
