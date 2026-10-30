#include "utest/utest.h"
#include <core/core.h>
#include <core/string.h>
#include <core/arena.h>
#include <core/arena_alloc_tracker.h>
#include <core/os/os.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Thread test helper - must be defined outside UTEST macro (C11 no nested functions)

typedef struct {
    ArenaAllocTracker* tracker;
    int thread_id;
} TrackerThreadData;

static void* tracker_thread_func(void* arg) {
    TrackerThreadData* data = (TrackerThreadData*)arg;
    const char* files[] = { "thread0.c", "thread1.c", "thread2.c", "thread3.c" };
    const char* file = files[data->thread_id];
    for (int i = 0; i < 1000; i++) {
        arena_alloc_tracker_record(data->tracker, file, 1, 64);
    }
    return nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena_alloc_tracker, create_destroy) {
    ArenaAllocTracker* tracker = arena_alloc_tracker_create();
    ASSERT_NE(nullptr, tracker);
    arena_alloc_tracker_destroy(tracker);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena_alloc_tracker, record_and_query) {
    ArenaAllocTracker* tracker = arena_alloc_tracker_create();

    arena_alloc_tracker_record(tracker, "file_a.c", 10, 1000);
    arena_alloc_tracker_record(tracker, "file_b.c", 20, 2000);
    arena_alloc_tracker_record(tracker, "file_c.c", 30, 500);

    arena_scratch_auto(temp);
    ArenaAllocEntry* entries = nullptr;
    int count = arena_alloc_tracker_get_top(tracker, temp.arena, 10, &entries);

    ASSERT_EQ(3, count);
    ASSERT_NE(nullptr, entries);

    ASSERT_EQ(2000u, (unsigned)entries[0].total_bytes);
    ASSERT_EQ(1000u, (unsigned)entries[1].total_bytes);
    ASSERT_EQ(500u, (unsigned)entries[2].total_bytes);

    arena_alloc_tracker_destroy(tracker);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena_alloc_tracker, same_callsite_accumulates) {
    ArenaAllocTracker* tracker = arena_alloc_tracker_create();

    const char* file = "accum.c";
    arena_alloc_tracker_record(tracker, file, 42, 100);
    arena_alloc_tracker_record(tracker, file, 42, 200);
    arena_alloc_tracker_record(tracker, file, 42, 300);

    arena_scratch_auto(temp);
    ArenaAllocEntry* entries = nullptr;
    int count = arena_alloc_tracker_get_top(tracker, temp.arena, 10, &entries);

    ASSERT_EQ(1, count);
    ASSERT_EQ(600u, (unsigned)entries[0].total_bytes);
    ASSERT_EQ(3u, (unsigned)entries[0].alloc_count);

    arena_alloc_tracker_destroy(tracker);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena_alloc_tracker, reset_clears_entries) {
    ArenaAllocTracker* tracker = arena_alloc_tracker_create();

    arena_alloc_tracker_record(tracker, "reset.c", 1, 100);
    arena_alloc_tracker_record(tracker, "reset.c", 2, 200);

    arena_alloc_tracker_reset(tracker);

    arena_scratch_auto(temp);
    ArenaAllocEntry* entries = nullptr;
    int count = arena_alloc_tracker_get_top(tracker, temp.arena, 10, &entries);

    ASSERT_EQ(0, count);

    arena_alloc_tracker_destroy(tracker);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena_alloc_tracker, hash_collision_handling) {
    ArenaAllocTracker* tracker = arena_alloc_tracker_create();

    // Record from 600 different "callsites" (more than half the 1024-slot table)
    const char* file = "collision.c";
    for (int i = 0; i < 600; i++) {
        arena_alloc_tracker_record(tracker, file, i, (u64)(i + 1) * 10);
    }

    arena_scratch_auto(temp);
    ArenaAllocEntry* entries = nullptr;
    int count = arena_alloc_tracker_get_top(tracker, temp.arena, 600, &entries);

    ASSERT_EQ(600, count);

    ASSERT_EQ(6000u, (unsigned)entries[0].total_bytes);

    arena_alloc_tracker_destroy(tracker);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena_alloc_tracker, top_n_sorting) {
    ArenaAllocTracker* tracker = arena_alloc_tracker_create();

    const char* file = "topn.c";
    for (int i = 0; i < 20; i++) {
        arena_alloc_tracker_record(tracker, file, i, (u64)(i + 1) * 100);
    }

    arena_scratch_auto(temp);
    ArenaAllocEntry* entries = nullptr;
    int count = arena_alloc_tracker_get_top(tracker, temp.arena, 5, &entries);

    ASSERT_EQ(5, count);

    ASSERT_EQ(2000u, (unsigned)entries[0].total_bytes);
    ASSERT_EQ(1900u, (unsigned)entries[1].total_bytes);
    ASSERT_EQ(1800u, (unsigned)entries[2].total_bytes);
    ASSERT_EQ(1700u, (unsigned)entries[3].total_bytes);
    ASSERT_EQ(1600u, (unsigned)entries[4].total_bytes);

    arena_alloc_tracker_destroy(tracker);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena_alloc_tracker, thread_safety) {
    ArenaAllocTracker* tracker = arena_alloc_tracker_create();

    TrackerThreadData thread_data[4];
    Thread threads[4];

    for (int i = 0; i < 4; i++) {
        thread_data[i].tracker = tracker;
        thread_data[i].thread_id = i;
        ASSERT_TRUE(os_thread_create(&threads[i], tracker_thread_func, &thread_data[i]));
    }

    for (int i = 0; i < 4; i++) {
        os_thread_join(&threads[i]);
    }

    // Verify total: 4 threads * 1000 allocs * 64 bytes = 256000 total bytes
    arena_scratch_auto(temp);
    ArenaAllocEntry* entries = nullptr;
    int count = arena_alloc_tracker_get_top(tracker, temp.arena, 10, &entries);

    ASSERT_EQ(4, count);

    u64 total_bytes = 0;
    u64 total_count = 0;
    for (int i = 0; i < count; i++) {
        total_bytes += entries[i].total_bytes;
        total_count += entries[i].alloc_count;
    }

    ASSERT_EQ(256000u, (unsigned)total_bytes);
    ASSERT_EQ(4000u, (unsigned)total_count);

    arena_alloc_tracker_destroy(tracker);
}
