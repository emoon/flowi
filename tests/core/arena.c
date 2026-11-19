// arena, pool allocator, tlsf and heap tests

#include "unit_tests_common.h"

#include <core/arena_typed.h>

#if defined(__linux__)
#include <sys/wait.h>
#include <unistd.h>
#endif

UTEST(arena, destroy_releases_reservation_no_vma_leak) {
    // A leak would grow /proc/self/maps by one region per iteration.
    const int iterations = 2000;

    // Warm up so lazy one-time mappings are in place before the baseline count.
    for (int i = 0; i < 8; ++i) {
        FlArena* a = arena_new();
        fl_arena_destroy(a);
    }

#if defined(__linux__)
    u64 maps_before = count_proc_self_maps();
#endif

    for (int i = 0; i < iterations; ++i) {
        FlArena* a = arena_new();
        // Use the arena each cycle.
        int* p = arena_alloc(a, int);
        *p = i;
        EXPECT_EQ(*p, i);
        fl_arena_destroy(a);
    }

#if defined(__linux__)
    u64 maps_after = count_proc_self_maps();
    // The slack covers unrelated allocator activity. A real leak would add about 2000 regions.
    EXPECT_LE(maps_after, maps_before + 8);
#endif
}

// Both arena allocation paths reject a size that overflows the position arithmetic (aligned_pos + size) with a
// fatal error. Without the check they would return a wrapped pointer that overlaps live memory.

#if defined(__linux__)
// The guard aborts the child, which gives a non-zero exit. Without the guard the child gets a bogus pointer and
// reaches _exit(0).
static int arena_overflow_child_status(bool single_threaded) {
    pid_t pid = fork();
    if (pid == 0) {
        FlArena* a = arena_new();
        // aligned_pos is small (ARENA_SIZE), so a size near UINT64_MAX wraps aligned_pos + size.
        if (single_threaded) {
            fl_arena_alloc_raw_st(a, UINT64_MAX - 16, 16);
        } else {
            fl_arena_alloc_raw(a, UINT64_MAX - 16, 16);
        }
        _exit(0); // reached only if the guard did not fire
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return status;
}
#endif

UTEST(arena, alloc_raw_overflow_guard_rejects) {
#if defined(__linux__)
    int cas_status = arena_overflow_child_status(false);
    ASSERT_TRUE(WIFEXITED(cas_status));
    ASSERT_NE(WEXITSTATUS(cas_status), 0); // the CAS path aborted

    int st_status = arena_overflow_child_status(true);
    ASSERT_TRUE(WIFEXITED(st_status));
    ASSERT_NE(WEXITSTATUS(st_status), 0); // the single-threaded path aborted
#else
    UTEST_SKIP("fork-based arena overflow death test is Linux-only");
#endif
}

// fl_arena_align commits memory on demand when the aligned position crosses a page-commit boundary inside the
// reservation, as fl_arena_alloc_raw does.

UTEST(arena, align_commits_across_page_boundary) {
    FlArena* a = arena_new();

    // A new arena commits a single page. Aligning to a multiple of the page size moves pos past the committed
    // region while staying far inside the reservation.
    const u64 page_size = get_page_size_os();
    const u64 alignment = page_size * 2;
    u64 committed_before = atomic_load_explicit(&a->committed_size, memory_order_relaxed);
    ASSERT_LT(committed_before, alignment);

    fl_arena_align(a, alignment);

    u64 pos_after = atomic_load_explicit(&a->pos, memory_order_relaxed);
    u64 committed_after = atomic_load_explicit(&a->committed_size, memory_order_relaxed);
    EXPECT_EQ(pos_after % alignment, (u64)0);
    EXPECT_GT(pos_after, committed_before);
    EXPECT_GE(committed_after, pos_after);

    // The newly committed region is writable.
    char* p = (char*)fl_arena_alloc_raw(a, 64, 16);
    p[0] = 'x';
    EXPECT_EQ(p[0], 'x');

    fl_arena_destroy(a);
}

// fl_arena_rewind poisons the region it frees, [ARENA_SIZE, old pos), so ASAN catches a use after rewind through
// a stale pointer.

#if defined(ENABLE_ASAN) || defined(__SANITIZE_ADDRESS__) || __has_feature(address_sanitizer)
#define ARENA_ASAN_TEST 1
int __asan_address_is_poisoned(void const volatile* addr);

// Reads *p in a forked child so the ASAN report kills the child and the test runner survives. True when the child
// read it without being stopped.
static bool asan_read_in_child_survives(const volatile char* p) {
#if defined(__linux__)
    pid_t pid = fork();
    if (pid == 0) {
        volatile char c = *p;
        (void)c;
        _exit(0); // reached only if ASAN did not catch it
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#else
    (void)p;
    return false;
#endif
}

static bool asan_use_is_trapped(const void* p) {
    return __asan_address_is_poisoned(p) && !asan_read_in_child_survives((const volatile char*)p);
}
#endif

UTEST(arena, rewind_poisons_freed_region) {
#ifdef ARENA_ASAN_TEST
    FlArena* a = arena_new();
    char* p = (char*)fl_arena_alloc_raw(a, 64, 16);
    p[0] = 'x';
    ASSERT_FALSE(__asan_address_is_poisoned(p));

    fl_arena_rewind(a);

    ASSERT_TRUE(__asan_address_is_poisoned(p + 63));
    ASSERT_TRUE(asan_use_is_trapped(p));

    char* q = (char*)fl_arena_alloc_raw(a, 64, 16);
    q[0] = 'y';
    ASSERT_FALSE(__asan_address_is_poisoned(q));

    fl_arena_destroy(a);
#else
    UTEST_SKIP("requires an ASAN-instrumented build");
#endif
}

// arena_typed_rewind goes through arena_rewind_to, which poisons the region it releases so ASAN catches a use
// after rewind through a stale element pointer.

UTEST(arena, typed_rewind_poisons_freed_region) {
#ifdef ARENA_ASAN_TEST
    FlArena* a = arena_new();

    arena_typed(u64) values;
    arena_typed_init(&values, a);

    u64* first = arena_typed_alloc(&values);
    *first = 1;
    ASSERT_FALSE(__asan_address_is_poisoned(first));

    arena_typed_rewind(&values);

    ASSERT_TRUE(asan_use_is_trapped(first));

    u64* reused = arena_typed_alloc(&values);
    *reused = 2;
    ASSERT_FALSE(__asan_address_is_poisoned(reused));

    fl_arena_destroy(a);
#else
    UTEST_SKIP("requires an ASAN-instrumented build");
#endif
}

// The FlArenaMt wrapper lives at the base of the arena it wraps. A full fl_arena_rewind of the underlying arena
// stops at the rewind floor above the wrapper.

UTEST(arena, mt_rewind_preserves_wrapper) {
    FlArenaMt* mt = arena_mt_new();
    FlArena* arena = mt->arena;

    char* p = (char*)arena_mt_alloc_raw(mt, 256, 16);
    memset(p, 0xab, 256);

    fl_arena_rewind(arena);

    ASSERT_EQ(arena, mt->arena);
    char* q = (char*)arena_mt_alloc_raw(mt, 64, 16);
    ASSERT_TRUE(q >= (char*)(mt + 1)); // new allocations land above the wrapper
    q[0] = 'y';

#ifdef ARENA_ASAN_TEST
    ASSERT_FALSE(__asan_address_is_poisoned(mt)); // the wrapper is below the poisoned region
#endif

    arena_mt_destroy(mt);
}

// arena_tracker_iterate_all holds the global tracker mutex across callbacks, so it creates the calling thread's
// scratch arenas before locking. A first scratch use inside a callback would call arena_tracker_register and take
// the same non-recursive mutex again.

static void iterate_scratch_callback(FlArena* arena, void* user_data) {
    (void)arena;
    arena_scratch_auto(temp);
    char* p = (char*)fl_arena_alloc_raw(temp.arena, 64, 16);
    p[0] = 'x';
    (*(int*)user_data)++;
}

static void* iterate_fresh_thread(void* user_data) {
    // This thread has never used a scratch arena, and only a first scratch use registers with the tracker.
    arena_tracker_iterate_all(iterate_scratch_callback, user_data);
    arena_scratch_destroy();
    return nullptr;
}

UTEST(arena, tracker_iterate_scratch_callback_no_self_deadlock) {
    // At least one arena has to be registered for the callback to run.
    FlArena* a = arena_new();

    int visited = 0;
    Thread thread;
    ASSERT_TRUE(os_thread_create(&thread, iterate_fresh_thread, &visited));
    os_thread_join(&thread);

    ASSERT_GT(visited, 0);
    fl_arena_destroy(a);
}

// pool_free_raw stores a pointer-sized free-list node inside each freed item, so pool_init_raw raises item_size and
// item_alignment to FreeListNode's. A pool of 1-byte items still hands out distinct, pointer-aligned, zeroed
// slots across a free and reallocate cycle.

static bool pool_item_is_aligned(const void* item) {
    return (uintptr_t)item % _Alignof(void*) == 0;
}

static bool pool_item_is_zeroed(const char* item, u64 size) {
    for (u64 b = 0; b < size; ++b) {
        if (item[b] != 0) {
            return false;
        }
    }
    return true;
}

// True when item differs from each of the first count entries of items.
static bool pool_item_is_distinct(void* const* items, int count, const void* item) {
    for (int j = 0; j < count; ++j) {
        if (item == items[j]) {
            return false;
        }
    }
    return true;
}

static bool pool_item_is_fresh(void* const* items, int count, const char* item, u64 size) {
    return pool_item_is_aligned(item) && pool_item_is_zeroed(item, size) && pool_item_is_distinct(items, count, item);
}

static bool pool_items_are_all_distinct(void* const* items, int count) {
    for (int i = 0; i < count; ++i) {
        if (!pool_item_is_distinct(items + i + 1, count - i - 1, items[i])) {
            return false;
        }
    }
    return true;
}

UTEST(pool_allocator, sub_pointer_item_size_clamped) {
    FlArena* arena = arena_new();

    PoolAllocatorBase pool;
    pool_init_raw(&pool, arena, 1, 1);
    ASSERT_GE(pool.item_size, sizeof(void*));
    ASSERT_GE(pool.item_alignment, _Alignof(void*));

    enum { COUNT = 8 };
    void* items[COUNT];
    for (int i = 0; i < COUNT; ++i) {
        items[i] = pool_alloc_raw(&pool);
        ASSERT_TRUE(items[i] != nullptr);
        ASSERT_TRUE(pool_item_is_aligned(items[i]));
        ASSERT_TRUE(pool_item_is_distinct(items, i, items[i]));
    }

    for (int i = 0; i < COUNT; ++i) {
        pool_free_raw(&pool, items[i]);
    }

    for (int i = 0; i < COUNT; ++i) {
        char* p = (char*)pool_alloc_raw(&pool);
        ASSERT_TRUE(p != nullptr);
        ASSERT_TRUE(pool_item_is_fresh(items, i, p, pool.item_size));
        items[i] = p;
    }
    ASSERT_TRUE(pool_items_are_all_distinct(items, COUNT));

    fl_arena_destroy(arena);
}

// A tlsf realloc that grows to a size the allocator can never satisfy returns nullptr and leaves the original
// allocation (size, contents, pool integrity) untouched.

static bool tlsf_pool_is_intact(tlsf_t tlsf) {
    return tlsf_check(tlsf) == 0 && tlsf_check_pool(tlsf_get_pool(tlsf)) == 0;
}

static bool tlsf_block_is_untouched(const unsigned char* p, size_t block, unsigned char fill, int size) {
    if (tlsf_block_size((void*)p) != block) {
        return false;
    }
    for (int b = 0; b < size; ++b) {
        if (p[b] != fill) {
            return false;
        }
    }
    return true;
}

UTEST(tlsf, realloc_oversize_fails_without_corruption) {
    static _Alignas(16) unsigned char mem[64 * 1024];
    tlsf_t tlsf = tlsf_create_with_pool(mem, sizeof(mem));
    ASSERT_TRUE(tlsf != nullptr);

    enum { ALLOC_SIZE = 256 };
    unsigned char* p = (unsigned char*)tlsf_malloc(tlsf, ALLOC_SIZE);
    ASSERT_TRUE(p != nullptr);
    memset(p, 0xAB, ALLOC_SIZE);
    const size_t block = tlsf_block_size(p);

    // Sizes beyond the maximum block size, and sizes near SIZE_MAX where the internal align-up wraps. Both fail
    // without touching the block.
    const size_t oversize[] = { tlsf_block_size_max(), tlsf_block_size_max() + 1, SIZE_MAX - 1, SIZE_MAX };
    for (size_t i = 0; i < sizeof_array(oversize); ++i) {
        ASSERT_TRUE(tlsf_realloc(tlsf, p, oversize[i]) == nullptr);
        ASSERT_TRUE(tlsf_block_is_untouched(p, block, 0xAB, ALLOC_SIZE));
        ASSERT_TRUE(tlsf_pool_is_intact(tlsf));
    }

    void* q = tlsf_malloc(tlsf, 512);
    ASSERT_TRUE(q != nullptr);
    tlsf_free(tlsf, q);
    tlsf_free(tlsf, p);
    ASSERT_TRUE(tlsf_pool_is_intact(tlsf));
}

// fl_heap_alloc_aligned accepts a size that is not a multiple of the alignment. The C11 aligned_alloc path
// (sanitizer and non-mimalloc builds) is undefined when size % alignment != 0, and the ASan interceptor aborts on
// it, so the wrapper rounds the size up.

UTEST(heap, alloc_aligned_size_not_multiple_of_alignment) {
    const u64 alignments[] = { 8, 16, 32, 64, 128, 256 };
    for (u64 i = 0; i < sizeof_array(alignments); ++i) {
        const u64 alignment = alignments[i];
        const u64 size = alignment + 1; // not a multiple of alignment
        void* p = fl_heap_alloc_aligned(alignment, size);
        ASSERT_TRUE(p != nullptr);
        ASSERT_EQ((uintptr_t)p % alignment, (uintptr_t)0);
        memset(p, 0xCD, size); // the whole requested range is writable
        fl_heap_free_aligned(p);
    }
}

// mimalloc stats: alloc_count and free_count are block counts of one population, so their difference is the
// number of live blocks.

#if USE_MIMALLOC
#include <mimalloc.h>

static void mimalloc_stats_merge_job(void* user_data, FlJobsWorkerInfo worker_info) {
    (void)user_data;
    (void)worker_info;
    mi_stats_merge();
}

// A block a worker allocated and the test thread freed is credited as an allocation to the worker and a free to the
// test thread, and the worker's side reaches the process totals only when the worker merges.
static void mimalloc_stats_settle_workers(void) {
    fl_jobs_wait(fl_jobs_add_job(mimalloc_stats_merge_job, nullptr));
}

static u64 mimalloc_live_blocks(MimallocStats stats) {
    return stats.alloc_count - stats.free_count;
}

static bool mimalloc_stats_are_consistent(MimallocStats stats) {
    return stats.free_count <= stats.alloc_count;
}

static MimallocStats mimalloc_stats_settled(void) {
    mimalloc_stats_settle_workers();
    return mem_get_mimalloc_stats();
}

static bool mimalloc_alloc_blocks(void** blocks, int count, u64 size) {
    for (int i = 0; i < count; i++) {
        blocks[i] = fl_heap_alloc(size);
        if (blocks[i] == nullptr) {
            return false;
        }
    }
    return true;
}

static void mimalloc_free_blocks(void** blocks, int count) {
    for (int i = 0; i < count; i++) {
        fl_heap_free(blocks[i]);
    }
}

UTEST(mimalloc_stats, live_blocks_track_allocations_and_frees) {
    enum { BLOCK_COUNT = 4096 };
    static void* blocks[BLOCK_COUNT];

    MimallocStats before = mimalloc_stats_settled();
    ASSERT_TRUE(mimalloc_stats_are_consistent(before));

    ASSERT_TRUE(mimalloc_alloc_blocks(blocks, BLOCK_COUNT, 64));

    MimallocStats held = mem_get_mimalloc_stats();
    ASSERT_TRUE(mimalloc_stats_are_consistent(held));
    ASSERT_GE(mimalloc_live_blocks(held), mimalloc_live_blocks(before) + BLOCK_COUNT / 2);

    mimalloc_free_blocks(blocks, BLOCK_COUNT);

    MimallocStats after = mem_get_mimalloc_stats();
    ASSERT_TRUE(mimalloc_stats_are_consistent(after));
    ASSERT_LT(mimalloc_live_blocks(after), mimalloc_live_blocks(held) - BLOCK_COUNT / 2);
}

UTEST(mimalloc_stats, huge_blocks_count_as_allocations_and_frees) {
    enum { HUGE_COUNT = 8 };
    void* blocks[HUGE_COUNT];

    MimallocStats before = mimalloc_stats_settled();
    ASSERT_TRUE(mimalloc_stats_are_consistent(before));

    ASSERT_TRUE(mimalloc_alloc_blocks(blocks, HUGE_COUNT, MB(8)));

    MimallocStats held = mem_get_mimalloc_stats();
    ASSERT_TRUE(mimalloc_stats_are_consistent(held));
    ASSERT_EQ(held.alloc_count - before.alloc_count, (u64)HUGE_COUNT);
    ASSERT_EQ(held.free_count, before.free_count);
    ASSERT_EQ(mimalloc_live_blocks(held) - mimalloc_live_blocks(before), (u64)HUGE_COUNT);

    mimalloc_free_blocks(blocks, HUGE_COUNT);

    MimallocStats after = mem_get_mimalloc_stats();
    ASSERT_TRUE(mimalloc_stats_are_consistent(after));
    ASSERT_EQ(after.alloc_count, held.alloc_count);
    ASSERT_EQ(after.free_count - held.free_count, (u64)HUGE_COUNT);
    ASSERT_EQ(mimalloc_live_blocks(after), mimalloc_live_blocks(before));
}

typedef struct CrossThreadFree {
    void** blocks;
    int count;
} CrossThreadFree;

static void* cross_thread_free_worker(void* user_data) {
    CrossThreadFree* job = (CrossThreadFree*)user_data;
    mimalloc_free_blocks(job->blocks, job->count);
    return nullptr;
}

UTEST(mimalloc_stats, huge_blocks_freed_on_another_thread_count_as_freed) {
    enum { HUGE_COUNT = 8 };
    void* blocks[HUGE_COUNT];

    MimallocStats before = mimalloc_stats_settled();

    ASSERT_TRUE(mimalloc_alloc_blocks(blocks, HUGE_COUNT, MB(8)));

    MimallocStats held = mem_get_mimalloc_stats();
    ASSERT_EQ(mimalloc_live_blocks(held) - mimalloc_live_blocks(before), (u64)HUGE_COUNT);

    CrossThreadFree job = { .blocks = blocks, .count = HUGE_COUNT };
    Thread thread;
    ASSERT_TRUE(os_thread_create(&thread, cross_thread_free_worker, &job));
    os_thread_join(&thread);

    MimallocStats after = mem_get_mimalloc_stats();
    ASSERT_TRUE(mimalloc_stats_are_consistent(after));
    ASSERT_EQ(after.free_count - held.free_count, (u64)HUGE_COUNT);
    ASSERT_EQ(mimalloc_live_blocks(after), mimalloc_live_blocks(before));
}
#endif
