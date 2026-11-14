// core container, allocator, string and job-system tests

#include "unit_tests_common.h"
#include "scoped_env.h"

#include <core/arena_typed.h>

#if defined(__linux__)
#include <linux/perf_event.h>
#include <unistd.h>
#endif

// main already ran fl_init(1), and fl_app_create runs it again for every app. A repeat call adopts the live
// services and does not initialize them again.

UTEST(core_init, a_repeat_fl_init_returns_the_live_main_arena) {
    FlArena* again = fl_init(1);
    ASSERT_TRUE(again == s_arena);

    // A different job_threads count does not restart the job system.
    FlArena* third = fl_init(4);
    ASSERT_TRUE(third == s_arena);
}

typedef struct DllTestNode {
    int value;
    struct DllTestNode* next;
    struct DllTestNode* prev;
} DllTestNode;

static bool dll_test_check_list(DllTestNode* f, DllTestNode* l, const int* expected, int count) {
    if (count == 0) {
        return f == nullptr && l == nullptr;
    }
    DllTestNode* node = f;
    for (int i = 0; i < count; ++i) {
        if (node == nullptr || node->value != expected[i]) {
            return false;
        }
        node = node->next;
    }
    if (node != nullptr || f->prev != nullptr) {
        return false;
    }
    node = l;
    for (int i = count - 1; i >= 0; --i) {
        if (node == nullptr || node->value != expected[i]) {
            return false;
        }
        node = node->prev;
    }
    return node == nullptr && l->next == nullptr;
}

// min, max and clamp from <flowi/core/utils.h> are plain ternaries. They evaluate their arguments more than once,
// so callers pass expressions without side effects, and each keeps the argument's own type.

static int count_bound_calls(int* calls) {
    (*calls)++;
    return 4;
}

UTEST(core_utils, min_max_pick_the_right_side) {
    EXPECT_EQ(3, min(3, 7));
    EXPECT_EQ(3, min(7, 3));
    EXPECT_EQ(7, max(3, 7));
    EXPECT_EQ(7, max(7, 3));

    EXPECT_EQ(5, min(5, 5));
    EXPECT_EQ(5, max(5, 5));

    EXPECT_EQ(-9, min(-9, -2));
    EXPECT_EQ(-2, max(-9, -2));
}

UTEST(core_utils, clamp_bounds) {
    EXPECT_EQ(5, clamp(5, 0, 10));
    EXPECT_EQ(0, clamp(-3, 0, 10));
    EXPECT_EQ(10, clamp(42, 0, 10));
    EXPECT_EQ(0, clamp(0, 0, 10));
    EXPECT_EQ(10, clamp(10, 0, 10));

    EXPECT_NEAR(0.25f, clamp(0.25f, 0.0f, 1.0f), 0.0001f);
    EXPECT_NEAR(0.0f, clamp(-1.5f, 0.0f, 1.0f), 0.0001f);
    EXPECT_NEAR(1.0f, clamp(2.5f, 0.0f, 1.0f), 0.0001f);
}

UTEST(core_utils, clamp_preserves_the_argument_type) {
    // 2^24 + 1 is the smallest integer f32 cannot represent.
    const u64 big = 16777217ULL;
    EXPECT_EQ(big, clamp(big, 0ULL, 1ULL << 40));

    const i64 negative = -16777217LL;
    EXPECT_EQ(negative, clamp(negative, -(1LL << 40), 0LL));

    const f64 precise = 0.1234567890123456;
    EXPECT_EQ(precise, clamp(precise, 0.0, 1.0));
}

UTEST(core_utils, sizeof_array_counts_elements_not_bytes) {
    const u32 words[] = { 1, 2, 3, 4, 5 };
    EXPECT_EQ(5u, (u32)sizeof_array(words));

    const u8 bytes[3] = { 0 };
    EXPECT_EQ(3u, (u32)sizeof_array(bytes));
}

UTEST(core_utils, for_count_evaluates_its_bound_once) {
    int calls = 0;
    int iterations = 0;

    for_count(i, count_bound_calls(&calls)) {
        (void)i;
        iterations++;
    }

    EXPECT_EQ(4, iterations);
    EXPECT_EQ(1, calls);
}

UTEST(core_dll, insert_into_empty_list) {
    DllTestNode a = { .value = 1 };
    DllTestNode* f = nullptr;
    DllTestNode* l = nullptr;
    dll_insert(f, l, (DllTestNode*)nullptr, &a);
    const int expected[] = { 1 };
    EXPECT_TRUE(dll_test_check_list(f, l, expected, 1));
}

UTEST(core_dll, insert_null_p_prepends_to_non_empty_list) {
    // A nullptr p on a non-empty list inserts at the front.
    DllTestNode a = { .value = 1 };
    DllTestNode b = { .value = 2 };
    DllTestNode* f = nullptr;
    DllTestNode* l = nullptr;
    dll_push_back(f, l, &a);
    dll_insert(f, l, (DllTestNode*)nullptr, &b);
    const int expected[] = { 2, 1 };
    EXPECT_TRUE(dll_test_check_list(f, l, expected, 2));
}

UTEST(core_dll, insert_after_head_of_multi_node_list) {
    DllTestNode a = { .value = 1 };
    DllTestNode b = { .value = 2 };
    DllTestNode c = { .value = 3 };
    DllTestNode* f = nullptr;
    DllTestNode* l = nullptr;
    dll_push_back(f, l, &a);
    dll_push_back(f, l, &b);
    dll_insert(f, l, &a, &c); // p == f with f != l takes the general insert-after branch
    const int expected[] = { 1, 3, 2 };
    EXPECT_TRUE(dll_test_check_list(f, l, expected, 3));
}

UTEST(core_dll, insert_after_last_and_middle) {
    DllTestNode a = { .value = 1 };
    DllTestNode b = { .value = 2 };
    DllTestNode c = { .value = 3 };
    DllTestNode* f = nullptr;
    DllTestNode* l = nullptr;
    dll_push_back(f, l, &a);
    dll_insert(f, l, &a, &b); // p == l takes the back-insert branch
    dll_insert(f, l, &a, &c); // p is no longer the last node, so this takes the general branch
    const int expected[] = { 1, 3, 2 };
    EXPECT_TRUE(dll_test_check_list(f, l, expected, 3));
}

UTEST(core_dll, push_front_and_push_back) {
    DllTestNode a = { .value = 1 };
    DllTestNode b = { .value = 2 };
    DllTestNode c = { .value = 3 };
    DllTestNode* f = nullptr;
    DllTestNode* l = nullptr;
    dll_push_front(f, l, &a);
    dll_push_front(f, l, &b);
    dll_push_back(f, l, &c);
    const int expected[] = { 2, 1, 3 };
    EXPECT_TRUE(dll_test_check_list(f, l, expected, 3));
}

UTEST(core_dll, remove_head_middle_tail) {
    DllTestNode a = { .value = 1 };
    DllTestNode b = { .value = 2 };
    DllTestNode c = { .value = 3 };
    DllTestNode* f = nullptr;
    DllTestNode* l = nullptr;
    dll_push_back(f, l, &a);
    dll_push_back(f, l, &b);
    dll_push_back(f, l, &c);
    dll_remove(f, l, &b);
    const int expected_after_mid[] = { 1, 3 };
    EXPECT_TRUE(dll_test_check_list(f, l, expected_after_mid, 2));
    dll_remove(f, l, &a);
    const int expected_after_head[] = { 3 };
    EXPECT_TRUE(dll_test_check_list(f, l, expected_after_head, 1));
    dll_remove(f, l, &c);
    EXPECT_TRUE(dll_test_check_list(f, l, nullptr, 0));
}

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
#include <sys/wait.h>
#include <unistd.h>

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
#endif

UTEST(arena, rewind_poisons_freed_region) {
#ifdef ARENA_ASAN_TEST
    FlArena* a = arena_new();
    char* p = (char*)fl_arena_alloc_raw(a, 64, 16);
    p[0] = 'x';
    ASSERT_FALSE(__asan_address_is_poisoned(p));

    fl_arena_rewind(a);

    ASSERT_TRUE(__asan_address_is_poisoned(p));
    ASSERT_TRUE(__asan_address_is_poisoned(p + 63));

#if defined(__linux__)
    // Fork so the ASAN report kills the child and the test runner survives.
    pid_t pid = fork();
    if (pid == 0) {
        volatile char c = *p; // use after rewind: ASAN aborts here
        (void)c;
        _exit(0); // reached only if ASAN did not catch it
    }
    int status = 0;
    waitpid(pid, &status, 0);
    ASSERT_FALSE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
#endif

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

    ASSERT_TRUE(__asan_address_is_poisoned(first));

#if defined(__linux__)
    // Fork so the ASAN report kills the child and the test runner survives.
    pid_t pid = fork();
    if (pid == 0) {
        volatile u64 v = *first; // use after rewind: ASAN aborts here
        (void)v;
        _exit(0); // reached only if ASAN did not catch it
    }
    int status = 0;
    waitpid(pid, &status, 0);
    ASSERT_FALSE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
#endif

    u64* reused = arena_typed_alloc(&values);
    *reused = 2;
    ASSERT_FALSE(__asan_address_is_poisoned(reused));

    fl_arena_destroy(a);
#else
    UTEST_SKIP("requires an ASAN-instrumented build");
#endif
}

// The hashmap copies a string key into its own arena, so the key does not depend on the caller's arena.
UTEST(hashmap, string_key_survives_source_arena_rewind) {
    FlArena* map_arena = arena_new();
    FlArena* key_arena = arena_new();

    hashmap(FlString, u32) map;
    hashmap_new(&map, map_arena, 16);

    // With a multi-byte key the equality memcmp reads a run of the stored bytes, so a dangling key is caught.
    const char* text = "list_container_id_1234567";
    const u64 len = strlen(text);
    char* key_bytes = arena_alloc_array(key_arena, char, len);
    memcpy(key_bytes, text, len);
    FlString key = string_from_cstr_len(key_bytes, len);
    ASSERT_FALSE(key.is_static); // a static key is not copied, which would void the test

    ASSERT_TRUE(hashmap_string_insert(&map, key, 42u) != nullptr);

    // Allocate again before overwriting. That unpoisons the region under ASAN, and the allocation lands exactly
    // on the original key bytes.
    fl_arena_rewind(key_arena);
    char* stomp = arena_alloc_array(key_arena, char, len);
    ASSERT_EQ((void*)stomp, (void*)key_bytes);
    memset(stomp, 0xEE, len);

    // A stored key that still pointed into key_arena would now compare against 0xEE bytes and miss, or trip ASAN.
    char other_bytes[64];
    memcpy(other_bytes, text, len);
    FlString probe = string_from_cstr_len(other_bytes, len);
    u32* found = hashmap_string_get(&map, probe);
    ASSERT_TRUE(found != nullptr);
    ASSERT_EQ(*found, 42u);

    fl_arena_destroy(key_arena);
    fl_arena_destroy(map_arena);
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
        ASSERT_EQ((uintptr_t)items[i] % _Alignof(void*), (uintptr_t)0);
        for (int j = 0; j < i; ++j) {
            ASSERT_NE(items[i], items[j]);
        }
    }

    for (int i = 0; i < COUNT; ++i) {
        pool_free_raw(&pool, items[i]);
    }

    for (int i = 0; i < COUNT; ++i) {
        char* p = (char*)pool_alloc_raw(&pool);
        ASSERT_TRUE(p != nullptr);
        ASSERT_EQ((uintptr_t)p % _Alignof(void*), (uintptr_t)0);
        for (u64 b = 0; b < pool.item_size; ++b) {
            ASSERT_EQ(p[b], 0);
        }
        for (int j = 0; j < i; ++j) {
            ASSERT_NE((void*)p, items[j]);
        }
        items[i] = p;
    }
    for (int i = 0; i < COUNT; ++i) {
        for (int j = i + 1; j < COUNT; ++j) {
            ASSERT_NE(items[i], items[j]);
        }
    }

    fl_arena_destroy(arena);
}

// A tlsf realloc that grows to a size the allocator can never satisfy returns nullptr and leaves the original
// allocation (size, contents, pool integrity) untouched.

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
        ASSERT_EQ(tlsf_block_size(p), block);
        for (int b = 0; b < ALLOC_SIZE; ++b) {
            ASSERT_EQ(p[b], 0xAB);
        }
        ASSERT_EQ(tlsf_check(tlsf), 0);
        ASSERT_EQ(tlsf_check_pool(tlsf_get_pool(tlsf)), 0);
    }

    void* q = tlsf_malloc(tlsf, 512);
    ASSERT_TRUE(q != nullptr);
    tlsf_free(tlsf, q);
    tlsf_free(tlsf, p);
    ASSERT_EQ(tlsf_check(tlsf), 0);
    ASSERT_EQ(tlsf_check_pool(tlsf_get_pool(tlsf)), 0);
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

// Concurrent formatting of messages longer than one stb chunk (about 512 chars) on a shared FlArenaMt returns a
// contiguous FlString that holds only its own bytes. Each worker formats one fill character per thread, so a
// foreign byte between the internal 512-char chunks shows at any offset.

#define SPRINTF_MT_THREADS 4

#define SPRINTF_MT_ITERS 128

typedef struct SprintfMtWorker {
    FlArenaMt* arena;
    int id;
    bool ok;
} SprintfMtWorker;

static void* sprintf_mt_worker(void* user) {
    SprintfMtWorker* w = (SprintfMtWorker*)user;
    const char fill = (char)('A' + w->id);

    for (int i = 0; i < SPRINTF_MT_ITERS; ++i) {
        const int len = 600 + (i % 300); // always over 512, so it spans more than one stb chunk
        char expected[1024];
        for (int j = 0; j < len; ++j) {
            expected[j] = fill;
        }
        expected[len] = 0;

        FlString s = sprintf_arena_mt(w->arena, "%s", expected);
        if ((int)s.length != len || memcmp(s.data, expected, (size_t)len) != 0) {
            w->ok = false;
            return nullptr;
        }
    }

    w->ok = true;
    return nullptr;
}

UTEST(sprintf, arena_mt_concurrent_contiguous) {
    FlArenaMt* arena = arena_mt_new();

    SprintfMtWorker workers[SPRINTF_MT_THREADS];
    Thread threads[SPRINTF_MT_THREADS];
    for (int t = 0; t < SPRINTF_MT_THREADS; ++t) {
        workers[t] = (SprintfMtWorker) { .arena = arena, .id = t, .ok = false };
        ASSERT_TRUE(os_thread_create(&threads[t], sprintf_mt_worker, &workers[t]));
    }

    for (int t = 0; t < SPRINTF_MT_THREADS; ++t) {
        os_thread_join(&threads[t]);
        ASSERT_TRUE_MSG(workers[t].ok, "a worker saw a corrupt/non-contiguous sprintf_arena_mt result");
    }

    arena_mt_destroy(arena);
}

// stbsp_snprintf with count == 0 and a non-null buffer measures the required length: it writes nothing and
// returns the full formatted length. A sentinel byte just before the buffer catches a write one byte too early.

extern int stbsp_snprintf(char* buf, int count, char const* fmt, ...);

UTEST(sprintf, snprintf_count_zero_writes_nothing) {
    char guarded[2] = { 0x5A, 0x5A };

    const int len = stbsp_snprintf(&guarded[1], 0, "hello %d", 1234);

    ASSERT_EQ(len, 10);          // "hello 1234"
    ASSERT_EQ(guarded[0], 0x5A); // byte before buf untouched
    ASSERT_EQ(guarded[1], 0x5A); // buf itself untouched
}

UTEST(sprintf, snprintf_positive_count_still_truncates_and_terminates) {
    char buf[6];
    memset(buf, 0x5A, sizeof buf);

    const int len = stbsp_snprintf(buf, 4, "hello %d", 1234);

    ASSERT_EQ(len, 10);       // the full length, not clamped to count
    ASSERT_STREQ(buf, "hel"); // 3 chars + terminator within count == 4
    ASSERT_EQ(buf[4], 0x5A);  // nothing written past count
}

// A FlString with null data formatted with %S prints the literal "null", whatever length it carries.
UTEST(sprintf, format_S_null_data_ignores_stale_length) {
    char buf[32];
    FlString bogus = (FlString) { .data = nullptr, .length = 100 };

    const int len = stbsp_snprintf(buf, sizeof buf, "%S", bogus);

    ASSERT_EQ(len, 4);
    ASSERT_STREQ(buf, "null");
}

// An unrecognized %v vector spec is echoed literally, with every digit in its original order.
UTEST(sprintf, unknown_vector_format_echoes_literally) {
    char buf[64];

    int len = stbsp_snprintf(buf, sizeof buf, "%v3f");
    ASSERT_EQ(len, 3);
    ASSERT_STREQ(buf, "v3f");

    len = stbsp_snprintf(buf, sizeof buf, "%v10x");
    ASSERT_EQ(len, 4);
    ASSERT_STREQ(buf, "v10x");

    len = stbsp_snprintf(buf, sizeof buf, "%vq");
    ASSERT_EQ(len, 2);
    ASSERT_STREQ(buf, "vq");
}

// Puts the fallback's writes across the boundary of the 512-byte internal callback buffer. The bounds checks
// flush before writing past it.
UTEST(sprintf, unknown_vector_format_at_callback_boundary) {
    char fmt[532];
    memset(fmt, 'x', 510);
    memcpy(fmt + 510, "%v10z", 6);

    char out[1024];
    const int len = stbsp_snprintf(out, sizeof out, fmt);

    ASSERT_EQ(len, 514);
    ASSERT_STREQ(out + 510, "v10z");
}

// strtoull accepts a leading sign and wraps negatives into huge unsigned values. fl_string_parse_u64 rejects
// signed input as InvalidFormat. Valid unsigned parsing and overflow detection are unaffected.

UTEST(string, parse_u64_rejects_signed_input) {
    static const char* rejected[] = { "-5", "-0", "+5", " -5", "\t-9223372036854775808", "-18446744073709551615" };
    for (size_t i = 0; i < sizeof_array(rejected); ++i) {
        StringParseResultU64 r = fl_string_parse_u64(string_from_cstr(rejected[i]));
        EXPECT_EQ_MSG((int)r.status, (int)StringParseStatus_InvalidFormat, rejected[i]);
        EXPECT_EQ(r.value, (u64)0);
    }

    StringParseResultU64 ok = fl_string_parse_u64(string_from_cstr("18446744073709551615"));
    EXPECT_EQ((int)ok.status, (int)StringParseStatus_Success);
    EXPECT_EQ(ok.value, UINT64_MAX);

    StringParseResultU64 zero = fl_string_parse_u64(string_from_cstr("0"));
    EXPECT_EQ((int)zero.status, (int)StringParseStatus_Success);
    EXPECT_EQ(zero.value, (u64)0);

    // UINT64_MAX + 1.
    StringParseResultU64 over = fl_string_parse_u64(string_from_cstr("18446744073709551616"));
    EXPECT_EQ((int)over.status, (int)StringParseStatus_Overflow);
}

// fl_string_compare_n: when max_len is below both strings' lengths, equal bytes within the bound compare as 0.
// The length tie-break (shorter sorts first) applies only when the full common prefix was compared.

UTEST(string, compare_n_clamped_equal_prefix_is_equal) {
    FlString b = string_from_cstr("hello_world");
    FlString c = string_from_cstr("hello_there");

    // max_len below both lengths, identical bytes inside the bound: equal both ways.
    EXPECT_EQ(fl_string_compare_n(b, c, 6), 0);
    EXPECT_EQ(fl_string_compare_n(c, b, 6), 0);
    EXPECT_GT(fl_string_compare_n(b, c, 7), 0); // 'w' > 't' once the differing byte is inside the bound

    // max_len at or above both lengths: the length tie-break applies and the shorter string sorts first.
    FlString a = string_from_cstr("hello");
    EXPECT_LT(fl_string_compare_n(a, b, 64), 0);
    EXPECT_GT(fl_string_compare_n(b, a, 64), 0);

    // max_len == 0: everything is equal within an empty bound.
    EXPECT_EQ(fl_string_compare_n(a, b, 0), 0);

    // max_len == min(a.length, b.length) is not clamped. The full common prefix was compared, so the tie-break
    // applies, as it does in the zero-length case.
    EXPECT_LT(fl_string_compare_n(a, b, 5), 0);
    EXPECT_GT(fl_string_compare_n(a, string_empty(), 0), 0);
    EXPECT_EQ(fl_string_compare_n(string_empty(), string_empty(), 64), 0);
}

UTEST(string, from_cstr_public_constructor) {
    FlString value = string_from_cstr("hello");
    EXPECT_EQ((uint64_t)value.length, 5);
    EXPECT_EQ(value.data, "hello");
    EXPECT_FALSE(value.is_static);

    FlString empty = string_from_cstr(nullptr);
    EXPECT_EQ((uint64_t)empty.length, 0);
    EXPECT_EQ(empty.data, nullptr);
}

UTEST(string, short_literal_macros_match_the_fl_forms) {
    const FlString short_form = S("héllo");
    const FlString long_form = FL_S("héllo");
    EXPECT_TRUE(fl_string_equals(short_form, long_form));
    EXPECT_TRUE(short_form.is_static);
    EXPECT_FALSE(S_UTF8("héllo").is_ascii);
    const FlString braced = S_("abc");
    EXPECT_EQ((uint64_t)braced.length, 3);
}

// fl_string_to_cstr_buffer with buffer_len == 0 has no room even for the null terminator and writes nothing to
// dest. The unsigned buffer_len - 1 must not underflow into copying everything.

UTEST(string, to_cstr_buffer_zero_buffer_len_writes_nothing) {
    // Any write into the zero-sized destination changes the sentinel bytes.
    char sentinel[4] = { 'A', 'B', 'C', 'D' };
    fl_string_to_cstr_buffer(sentinel, 0, string_from_cstr("hello"));
    EXPECT_EQ(sentinel[0], 'A');
    EXPECT_EQ(sentinel[1], 'B');
    EXPECT_EQ(sentinel[2], 'C');
    EXPECT_EQ(sentinel[3], 'D');

    char one[2] = { 'X', 'Y' };
    fl_string_to_cstr_buffer(one, 1, string_from_cstr("hello"));
    EXPECT_EQ(one[0], '\0');
    EXPECT_EQ(one[1], 'Y'); // byte past the declared buffer size untouched

    char small[4];
    fl_string_to_cstr_buffer(small, sizeof(small), string_from_cstr("hello"));
    EXPECT_STREQ(small, "hel");

    char big[16];
    fl_string_to_cstr_buffer(big, sizeof(big), string_from_cstr("hello"));
    EXPECT_STREQ(big, "hello");
}

// expand_env_vars needs a temporary allocation per variable for the getenv() name lookup. That scratch has to be
// separate from the caller's arena: when the caller passes the thread-local scratch arena, the plain scratch
// call returns that same arena and the temp scope's rewind frees the appended bytes.

UTEST(env_expand, output_on_scratch_arena_survives_temp_rewind) {
    SCOPED_ENV(env_a, "FLOWI_TEST_EXPAND_A", "alpha");
    SCOPED_ENV(env_b, "FLOWI_TEST_EXPAND_B", "beta");

    // The output arena is the thread-local scratch arena, which is the aliasing case.
    arena_scratch_auto(scratch);
    FlString result = expand_env_vars(scratch.arena, FL_S("${FLOWI_TEST_EXPAND_A}/mid/$FLOWI_TEST_EXPAND_B-end"));
    EXPECT_TRUE(fl_string_equals(result, FL_S("alpha/mid/beta-end")));

    // Undefined variables are kept verbatim, also with the aliased arena.
    FlString kept = expand_env_vars(scratch.arena, FL_S("${FLOWI_TEST_EXPAND_MISSING}/tail"));
    EXPECT_TRUE(fl_string_equals(kept, FL_S("${FLOWI_TEST_EXPAND_MISSING}/tail")));
}

// fl_hash_sha1 with nullptr data and zero length hashes the empty message and writes the SHA1("") digest. An
// untouched output buffer would have callers read uninitialized memory.

UTEST(hash, sha1_null_empty_input_is_empty_string_digest) {
    // SHA1("") = da39a3ee5e6b4b0d3255bfef95601890afd80709
    static const u8 empty_digest[20] = { 0xda, 0x39, 0xa3, 0xee, 0x5e, 0x6b, 0x4b, 0x0d, 0x32, 0x55,
                                         0xbf, 0xef, 0x95, 0x60, 0x18, 0x90, 0xaf, 0xd8, 0x07, 0x09 };

    u8 digest[20];
    memset(digest, 0xab, sizeof(digest)); // so an untouched buffer fails the comparison
    fl_hash_sha1(nullptr, 0, digest);
    EXPECT_EQ(memcmp(digest, empty_digest, sizeof(digest)), 0);

    // Empty input with a non-null pointer gives the same digest.
    u8 digest2[20];
    memset(digest2, 0xcd, sizeof(digest2));
    fl_hash_sha1((const u8*)"", 0, digest2);
    EXPECT_EQ(memcmp(digest2, empty_digest, sizeof(digest2)), 0);

    char hex[41];
    memset(hex, 0xef, sizeof(hex));
    hash_sha1_hex(nullptr, 0, hex);
    EXPECT_STREQ(hex, "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}

// fl_hash_sha1 does its chunk-count and tail-padding arithmetic in 64 bits, so inputs of several GiB do not wrap
// the padding offsets. A buffer of 4 GiB or more is impractical in CI, so sha1_chunk_count_is_64bit_safe checks
// the arithmetic at sizes where 32-bit math would wrap.

UTEST(hash, sha1_known_answer_vectors) {
    // FIPS 180 vector: SHA1("abc") = a9993e364706816aba3e25717850c26c9cd0d89d
    char hex[41];
    hash_sha1_hex((const u8*)"abc", 3, hex);
    EXPECT_STREQ(hex, "a9993e364706816aba3e25717850c26c9cd0d89d");

    // FIPS 180 vector of 56 bytes. The message plus padding spans two 512-bit blocks.
    const char* msg2 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    hash_sha1_hex((const u8*)msg2, 56, hex);
    EXPECT_STREQ(hex, "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

    // 128 'a' bytes, more than one block, so the full-chunk loop runs before the tail.
    u8 msg3[128];
    memset(msg3, 'a', sizeof(msg3));
    hash_sha1_hex(msg3, sizeof(msg3), hex);
    EXPECT_STREQ(hex, "ad5b3fdbcb526778c2839d2f151ea753995e26a0");

    // The binary digest matches the hex one.
    static const u8 abc_digest[20] = { 0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81, 0x6a, 0xba, 0x3e,
                                       0x25, 0x71, 0x78, 0x50, 0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d };
    u8 digest[20];
    fl_hash_sha1((const u8*)"abc", 3, digest);
    EXPECT_EQ(memcmp(digest, abc_digest, sizeof(digest)), 0);
}

UTEST(hash, sha1_chunk_count_is_64bit_safe) {
    // Sizes a CI machine cannot allocate, passed to the function sha1digest() sizes its loop and tail with.
    u32 tail_bytes = 0;

    // 5 GiB, 64-byte aligned: the tail is one whole padding chunk.
    const u64 five_gib = 5ull * 1024 * 1024 * 1024;
    EXPECT_EQ(hash_sha1_chunk_count(five_gib, &tail_bytes), 83886081ull);
    EXPECT_EQ(tail_bytes, 64u);

    // Across the alignment residues around the 4 GiB boundary the tail stays in the valid padding range [9, 72].
    const u64 four_gib = 4ull * 1024 * 1024 * 1024;
    for (u64 offset = 0; offset < 64; offset++) {
        const u64 chunks = hash_sha1_chunk_count(four_gib + offset, &tail_bytes);
        EXPECT_EQ(chunks * 64 - tail_bytes, four_gib + offset);
        EXPECT_GE(tail_bytes, 9u);
        EXPECT_LE(tail_bytes, 72u);
    }

    // At 1 TiB the true chunk count no longer fits 32 bits.
    const u64 one_tib = 1ull << 40;
    const u64 huge_chunks = hash_sha1_chunk_count(one_tib, &tail_bytes);
    EXPECT_EQ(huge_chunks, one_tib / 64 + 1);
    EXPECT_GT(huge_chunks, 0xffffffffull);

    // Small sizes keep the standard layout: a 55-byte message pads into one chunk, 56 bytes needs two.
    EXPECT_EQ(hash_sha1_chunk_count(55, &tail_bytes), 1ull);
    EXPECT_EQ(hash_sha1_chunk_count(56, &tail_bytes), 2ull);
}

// fl_hash_crc32 takes a u64 size but zlib's crc32() takes a 32-bit length, so buffers of 4 GiB or more are hashed
// by chaining crc32() calls over chunks. 4 GiB cannot be allocated here, so the case checks what the chunk loop
// relies on: crc32 chained over consecutive pieces equals crc32 of the whole.

UTEST(hash, crc32_chunked_matches_reference) {
    // CRC-32/ISO-HDLC canonical check value: crc32("123456789") == 0xCBF43926.
    EXPECT_EQ(fl_hash_crc32((const u8*)"123456789", 9), 0xCBF43926u);

    u8 buf[1024];
    for (int i = 0; i < 1024; i++) {
        buf[i] = (u8)(i * 31 + 7);
    }
    u32 whole = fl_hash_crc32(buf, sizeof(buf));

    for (u64 split = 0; split <= sizeof(buf); split += 37) {
        u32 crc = fl_hash_crc32(buf, split);
        crc = hash_crc32_update(crc, buf + split, sizeof(buf) - split);
        EXPECT_EQ(crc, whole);
    }

    // Empty input gives zlib's initial CRC (0), and an update over zero bytes leaves the running CRC unchanged.
    EXPECT_EQ(fl_hash_crc32(nullptr, 0), 0u);
    EXPECT_EQ(hash_crc32_update(whole, nullptr, 0), whole);
}

// cpu_online_from_sysfs (core/cpu_topology.c) decides the result of is_cpu_online. Kernels without hotplug omit
// the per-CPU 'online' sysfs file, so a missing file means the CPU is online. Only a present file that reads 0
// marks a CPU offline.

#if PLATFORM_LINUX

UTEST(cpu_topology, online_file_missing_means_online) {
    ASSERT_TRUE(cpu_online_from_sysfs(false, 0));
    // The value argument is ignored when the file does not exist.
    ASSERT_TRUE(cpu_online_from_sysfs(false, 1));
}

UTEST(cpu_topology, online_file_reads_one_means_online) {
    ASSERT_TRUE(cpu_online_from_sysfs(true, 1));
}

UTEST(cpu_topology, online_file_reads_zero_means_offline) {
    ASSERT_FALSE(cpu_online_from_sysfs(true, 0));
}

#endif // PLATFORM_LINUX

UTEST(monotonic_time, ticks_to_ns_no_overflow) {
    // A 10 MHz frequency, as QPC reports, and 3 days of uptime: ticks * 1e9 would be about 2.6e21 and wrap u64.
    // The split conversion does not.
    const u64 freq_10mhz = 10000000ull;
    const u64 three_days_s = 3ull * 24 * 60 * 60;
    ASSERT_EQ(monotonic_ticks_to_ns(three_days_s * freq_10mhz, freq_10mhz), three_days_s * 1000000000ull);

    // Sub-second remainder ticks scale to nanoseconds exactly (100 ns per tick at 10 MHz)
    ASSERT_EQ(monotonic_ticks_to_ns(three_days_s * freq_10mhz + 1234567, freq_10mhz),
              three_days_s * 1000000000ull + 123456700ull);

    // 1 MHz frequency, 7 days, max remainder
    const u64 freq_1mhz = 1000000ull;
    const u64 seven_days_s = 7ull * 24 * 60 * 60;
    ASSERT_EQ(monotonic_ticks_to_ns(seven_days_s * freq_1mhz + 999999, freq_1mhz),
              seven_days_s * 1000000000ull + 999999000ull);

    // Identity at a 1 GHz tick rate
    ASSERT_EQ(monotonic_ticks_to_ns(42, 1000000000ull), 42ull);
}

UTEST(os_sleep, sleep_ms_split_separates_whole_seconds) {
    const struct {
        u64 milliseconds;
        u64 seconds;
        u32 nanoseconds;
    } cases[] = {
        { 0, 0, 0 },          { 1, 0, 1000000 },      { 999, 0, 999000000 },  { 1000, 1, 0 },
        { 1001, 1, 1000000 }, { 2500, 2, 500000000 }, { 86400000, 86400, 0 },
    };
    for (size_t i = 0; i < sizeof_array(cases); i++) {
        u64 seconds = 99;
        u32 nanoseconds = 99;
        sleep_ms_split(cases[i].milliseconds, &seconds, &nanoseconds);
        EXPECT_EQ(seconds, cases[i].seconds);
        EXPECT_EQ(nanoseconds, cases[i].nanoseconds);
    }
}

// Input nested past the maximum depth fails with FlJsonParseError_TooDeep. Both recursive walks recurse once per
// nesting level of raw input, so without the limit deep input overflows the native stack.

// Builds depth nested arrays ("[[[...]]]") in an arena-owned buffer of depth * 2 bytes.
static char* json_nested_arrays(FlArena* arena, size_t depth) {
    char* buf = arena_alloc_array(arena, char, depth * 2);
    memset(buf, '[', depth);
    memset(buf + depth, ']', depth);
    return buf;
}

// Builds depth nested objects ("{\"a\":{...}}" with null innermost) and sets *len.
static char* json_nested_objects(FlArena* arena, size_t depth, size_t* len) {
    const char open[] = "{\"a\":";
    const size_t open_len = sizeof(open) - 1;
    size_t total = depth * (open_len + 1) + 4; // opens + "null" + closes
    char* buf = arena_alloc_array(arena, char, total);
    char* p = buf;
    for (size_t i = 0; i < depth; ++i) {
        memcpy(p, open, open_len);
        p += open_len;
    }
    memcpy(p, "null", 4);
    p += 4;
    memset(p, '}', depth);
    *len = total;
    return buf;
}

UTEST(json, depth_at_limit_parses) {
    FlArena* arena = arena_new();

    FlJsonParseResult result;
    char* arrays = json_nested_arrays(arena, 256);
    FlJsonValue* root = fl_json_parse_with_flags(arena, string_from_cstr_len(arrays, 256 * 2), 0, &result);
    ASSERT_TRUE(root != nullptr);
    ASSERT_EQ((int)result.error, (int)FlJsonParseError_None);
    ASSERT_EQ((int)root->type, (int)FlJsonType_Array);

    size_t obj_len = 0;
    char* objects = json_nested_objects(arena, 256, &obj_len);
    root = fl_json_parse_with_flags(arena, string_from_cstr_len(objects, obj_len), 0, &result);
    ASSERT_TRUE(root != nullptr);
    ASSERT_EQ((int)result.error, (int)FlJsonParseError_None);
    ASSERT_EQ((int)root->type, (int)FlJsonType_Object);

    fl_arena_destroy(arena);
}

UTEST(json, depth_past_limit_fails_cleanly) {
    FlArena* arena = arena_new();

    FlJsonParseResult result;
    char* arrays = json_nested_arrays(arena, 257);
    FlJsonValue* root = fl_json_parse_with_flags(arena, string_from_cstr_len(arrays, 257 * 2), 0, &result);
    ASSERT_TRUE(root == nullptr);
    ASSERT_EQ((int)result.error, (int)FlJsonParseError_TooDeep);

    size_t obj_len = 0;
    char* objects = json_nested_objects(arena, 257, &obj_len);
    root = fl_json_parse_with_flags(arena, string_from_cstr_len(objects, obj_len), 0, &result);
    ASSERT_TRUE(root == nullptr);
    ASSERT_EQ((int)result.error, (int)FlJsonParseError_TooDeep);

    FlString desc = fl_json_get_error_description(FlJsonParseError_TooDeep);
    ASSERT_TRUE(desc.length > 0);

    fl_arena_destroy(arena);
}

UTEST(json, depth_extreme_nesting_no_crash) {
    FlArena* arena = arena_new();

    // 100k unclosed '['. Without the depth limit this overflows the native stack. The limit rejects it long
    // before the recursion gets that deep.
    const size_t depth = 100000;
    char* buf = arena_alloc_array(arena, char, depth);
    memset(buf, '[', depth);

    FlJsonParseResult result;
    FlJsonValue* root = fl_json_parse_with_flags(arena, string_from_cstr_len(buf, depth), 0, &result);
    ASSERT_TRUE(root == nullptr);
    ASSERT_EQ((int)result.error, (int)FlJsonParseError_TooDeep);

    fl_arena_destroy(arena);
}

// A job enqueued just as the single worker goes idle is still picked up. The tight add-then-wait loop keeps the
// worker moving in and out of the idle state, so many adds land in the window where the producer decides whether
// to signal.

static _Atomic u64 s_jobsys_stress_counter;

static void jobsys_stress_job(void* user_data, FlJobsWorkerInfo worker_info) {
    (void)user_data;
    (void)worker_info;
    atomic_fetch_add(&s_jobsys_stress_counter, 1);
}

UTEST(jobsys, no_lost_wakeup_under_rapid_idle_transitions) {
    const u64 iterations = 20000;
    atomic_store(&s_jobsys_stress_counter, 0);

    for (u64 i = 0; i < iterations; i++) {
        FlJobHandle handle = fl_jobs_add_job(jobsys_stress_job, nullptr);
        ASSERT_TRUE(handle != 0);

        // Bounded spin-wait for this job to run. If the wakeup is lost the worker never picks it up and the
        // counter never reaches i+1, so the 5s deadline turns the hang into a test failure.
        const u64 target = i + 1;
        u64 deadline_ns = get_monotonic_time_ns_os() + 5000000000ull;
        while (atomic_load(&s_jobsys_stress_counter) < target) {
            ASSERT_TRUE_MSG(get_monotonic_time_ns_os() < deadline_ns,
                            "job was stranded — worker slept with a job queued (lost wakeup)");
        }
    }

    ASSERT_EQ_MSG(atomic_load(&s_jobsys_stress_counter), iterations, "not every job ran");
}

// A dependent registered on a dependency that finishes at almost the same instant is always scheduled. The
// dependency spins briefly so that, over many iterations, its completion often overlaps the dependent's
// registration, which is the window where promotion races with slot recycling.

static _Atomic u64 s_jobsys_dep_ran;

static void jobsys_dep_spin_job(void* user_data, FlJobsWorkerInfo worker_info) {
    (void)worker_info;
    // A short busy spin, so the dependency is often still running when the dependent is registered.
    volatile u64 sink = 0;
    for (u64 i = 0; i < 200; i++) {
        sink += i;
    }
    atomic_fetch_add((_Atomic u64*)user_data, 1);
}

static void jobsys_dependent_job(void* user_data, FlJobsWorkerInfo worker_info) {
    (void)user_data;
    (void)worker_info;
    atomic_fetch_add(&s_jobsys_dep_ran, 1);
}

UTEST(jobsys, dependent_promoted_when_dependency_completes_concurrently) {
    const u64 iterations = 20000;
    _Atomic u64 dep_counter;
    atomic_store(&dep_counter, 0);
    atomic_store(&s_jobsys_dep_ran, 0);

    for (u64 i = 0; i < iterations; i++) {
        FlJobHandle dep = fl_jobs_add_job(jobsys_dep_spin_job, &dep_counter);
        ASSERT_TRUE(dep != 0);

        FlJobHandle dependent = fl_jobs_add_job_with_dependency(jobsys_dependent_job, nullptr, dep);
        ASSERT_TRUE(dependent != 0);

        // Bounded wait for the dependent. If it was lost between promotion and completion it never runs, and
        // the 5s deadline turns the hang into a test failure.
        const u64 target = i + 1;
        u64 deadline_ns = get_monotonic_time_ns_os() + 5000000000ull;
        while (atomic_load(&s_jobsys_dep_ran) < target) {
            ASSERT_TRUE_MSG(get_monotonic_time_ns_os() < deadline_ns,
                            "dependent was orphaned — promoted after the dependency was recycled");
        }
    }

    ASSERT_EQ_MSG(atomic_load(&s_jobsys_dep_ran), iterations, "not every dependent ran");
}

// Queue saturation. When all SLOTS entries of a work queue are occupied, a producer that is not a worker blocks
// until a slot frees, and a worker producer never blocks: it claims the job and runs it inline. Both tests rely on
// the suite running with exactly one worker thread (fl_init(1) in main), so that a held worker means nothing
// drains the queue.

static _Atomic u32 s_jobsys_sat_gate_started;

static _Atomic u32 s_jobsys_sat_gate_release;

// Occupies the single worker: signals that it has started, then holds until released.
static void jobsys_sat_gate_job(void* user_data, FlJobsWorkerInfo worker_info) {
    (void)user_data;
    (void)worker_info;
    atomic_store(&s_jobsys_sat_gate_started, 1);
    while (atomic_load(&s_jobsys_sat_gate_release) == 0) {
        sleep_us(10);
    }
}

static void jobsys_sat_count_job(void* user_data, FlJobsWorkerInfo worker_info) {
    (void)worker_info;
    atomic_fetch_add((_Atomic u64*)user_data, 1);
}

// Releases the gate after a short delay from a thread that is neither the main thread nor the worker, so the main
// thread can be blocked in a saturated add when the release happens.
static void* jobsys_sat_delayed_release_thread(void* arg) {
    (void)arg;
    sleep_ms(50);
    atomic_store(&s_jobsys_sat_gate_release, 1);
    return nullptr;
}

// A producer that is not a worker blocks when it adds to a full queue and completes once the worker drains a
// slot.
UTEST(jobsys, producer_backpressure_when_queue_saturated) {
    static _Atomic u64 ran;
    atomic_store(&ran, 0);
    atomic_store(&s_jobsys_sat_gate_started, 0);
    atomic_store(&s_jobsys_sat_gate_release, 0);

    // Occupy the worker so nothing drains the queue while it is filled.
    FlJobHandle gate = fl_jobs_add_job(jobsys_sat_gate_job, nullptr);
    ASSERT_TRUE(gate != 0);
    u64 deadline_ns = get_monotonic_time_ns_os() + 5000000000ull;
    while (atomic_load(&s_jobsys_sat_gate_started) == 0) {
        ASSERT_TRUE_MSG(get_monotonic_time_ns_os() < deadline_ns, "gate job never started");
    }

    // The worker dequeued the gate, so exactly SLOTS entries fit without saturating.
    for (u64 i = 0; i < SLOTS; i++) {
        ASSERT_TRUE(fl_jobs_add_job(jobsys_sat_count_job, &ran) != 0);
    }

    // The queue is full and the worker is still held, so the add below blocks. It completes only after the
    // helper thread's delayed release lets the worker start draining.
    Thread releaser;
    ASSERT_TRUE(os_thread_create(&releaser, jobsys_sat_delayed_release_thread, nullptr));
    ASSERT_TRUE(fl_jobs_add_job(jobsys_sat_count_job, &ran) != 0);
    os_thread_join(&releaser);

    deadline_ns = get_monotonic_time_ns_os() + 10000000000ull;
    while (atomic_load(&ran) < SLOTS + 1) {
        ASSERT_TRUE_MSG(get_monotonic_time_ns_os() < deadline_ns, "not every job ran after saturation");
        sleep_us(100);
    }
    ASSERT_EQ_MSG(atomic_load(&ran), (u64)SLOTS + 1, "not every job ran after saturation");
}

// A worker promoting dependents into a full queue runs them inline. With one worker there is no other consumer,
// so blocking would deadlock. The gate holds the worker while the queue is filled and dependents are registered,
// and releasing it triggers promotion while the queue is still full.
UTEST(jobsys, dependents_execute_inline_when_queue_saturated) {
    static _Atomic u64 filler_ran;
    static _Atomic u64 dep_ran;
    atomic_store(&filler_ran, 0);
    atomic_store(&dep_ran, 0);
    atomic_store(&s_jobsys_sat_gate_started, 0);
    atomic_store(&s_jobsys_sat_gate_release, 0);

    FlJobHandle gate = fl_jobs_add_job(jobsys_sat_gate_job, nullptr);
    ASSERT_TRUE(gate != 0);
    u64 deadline_ns = get_monotonic_time_ns_os() + 5000000000ull;
    while (atomic_load(&s_jobsys_sat_gate_started) == 0) {
        ASSERT_TRUE_MSG(get_monotonic_time_ns_os() < deadline_ns, "gate job never started");
    }

    // Fill the queue to capacity while the worker is held on the gate.
    for (u64 i = 0; i < SLOTS; i++) {
        ASSERT_TRUE(fl_jobs_add_job(jobsys_sat_count_job, &filler_ran) != 0);
    }

    // Dependents go on the gate's waiting list and reach the queue only during promotion, which runs on the
    // worker right after the gate's function returns, while the queue is still full.
    const u64 num_dependents = 8;
    for (u64 i = 0; i < num_dependents; i++) {
        ASSERT_TRUE(fl_jobs_add_job_with_dependency(jobsys_sat_count_job, &dep_ran, gate) != 0);
    }

    atomic_store(&s_jobsys_sat_gate_release, 1);

    deadline_ns = get_monotonic_time_ns_os() + 10000000000ull;
    while (atomic_load(&dep_ran) < num_dependents || atomic_load(&filler_ran) < SLOTS) {
        ASSERT_TRUE_MSG(get_monotonic_time_ns_os() < deadline_ns,
                        "saturated promotion deadlocked — dependents or fillers never ran");
        sleep_us(100);
    }
}

// A job that a worker runs inline off a saturated queue runs nested, on the stack of a job that is still using
// the same scratch arena. It gives back only what it allocated. Rewinding to the arena floor would poison the
// outer job's live allocations and let its next one alias them.

#define NESTED_SCRATCH_BYTES 256

typedef struct JobsysNestedCtx {
    _Atomic u32 outer_started;
    _Atomic u32 queue_saturated;
    _Atomic u32 child_ran;
    _Atomic u32 pattern_survived;
    _Atomic u32 second_alloc_aliased;
    _Atomic u32 outer_done;
    FlJobHandle settled_dependency;
} JobsysNestedCtx;

static void jobsys_nested_noop_job(void* user_data, FlJobsWorkerInfo worker_info) {
    (void)user_data;
    (void)worker_info;
}

static void jobsys_nested_flag_job(void* user_data, FlJobsWorkerInfo worker_info) {
    (void)worker_info;
    atomic_store((_Atomic u32*)user_data, 1);
}

static void jobsys_nested_child_job(void* user_data, FlJobsWorkerInfo worker_info) {
    (void)worker_info;
    atomic_fetch_add(&((JobsysNestedCtx*)user_data)->child_ran, 1);
}

static void jobsys_nested_outer_job(void* user_data, FlJobsWorkerInfo worker_info) {
    JobsysNestedCtx* ctx = (JobsysNestedCtx*)user_data;

    u8* pattern = arena_alloc_array(worker_info.scratch, u8, NESTED_SCRATCH_BYTES);
    for (u32 i = 0; i < NESTED_SCRATCH_BYTES; i++) {
        pattern[i] = (u8)(i + 1);
    }

    atomic_store(&ctx->outer_started, 1);
    while (atomic_load(&ctx->queue_saturated) == 0) {
        sleep_us(200);
    }

    // A dependency that finished but was never polled gets past the early-outs and registers, so the add ends in
    // a schedule. With every queue slot taken, that schedule runs the child inline, nested right here.
    fl_jobs_add_job_with_dependency(jobsys_nested_child_job, ctx, ctx->settled_dependency);

    // Allocated after the nested run returned, so it aliases pattern only if the arena was rewound to its floor.
    // The fill then destroys pattern, which the check below reads.
    u8* second = arena_alloc_array(worker_info.scratch, u8, NESTED_SCRATCH_BYTES);
    memset(second, 0xAB, NESTED_SCRATCH_BYTES);
    atomic_store(&ctx->second_alloc_aliased, second == pattern ? 1u : 0u);

    u32 survived = 1;
    for (u32 i = 0; i < NESTED_SCRATCH_BYTES; i++) {
        if (pattern[i] != (u8)(i + 1)) {
            survived = 0;
            break;
        }
    }
    atomic_store(&ctx->pattern_survived, survived);
    atomic_store(&ctx->outer_done, 1);
}

UTEST(jobsys, nested_inline_job_leaves_the_outer_jobs_scratch_alone) {
    static JobsysNestedCtx ctx;
    atomic_store(&ctx.outer_started, 0);
    atomic_store(&ctx.queue_saturated, 0);
    atomic_store(&ctx.child_ran, 0);
    atomic_store(&ctx.pattern_survived, 0);
    atomic_store(&ctx.second_alloc_aliased, 1);
    atomic_store(&ctx.outer_done, 0);

    // Completion is observed through a second job, in order on the one worker, because polling it with
    // fl_jobs_is_finished would recycle the slot the add below needs to still carry its generation.
    static _Atomic u32 ordering_flag;
    atomic_store(&ordering_flag, 0);
    ctx.settled_dependency = fl_jobs_add_job(jobsys_nested_noop_job, nullptr);
    ASSERT_TRUE(ctx.settled_dependency != 0);
    ASSERT_TRUE(fl_jobs_add_job(jobsys_nested_flag_job, &ordering_flag) != 0);

    u64 deadline_ns = get_monotonic_time_ns_os() + 5000000000ull;
    while (atomic_load(&ordering_flag) == 0) {
        ASSERT_TRUE_MSG(get_monotonic_time_ns_os() < deadline_ns, "the dependency never completed");
    }

    ASSERT_TRUE(fl_jobs_add_job(jobsys_nested_outer_job, &ctx) != 0);
    deadline_ns = get_monotonic_time_ns_os() + 5000000000ull;
    while (atomic_load(&ctx.outer_started) == 0) {
        ASSERT_TRUE_MSG(get_monotonic_time_ns_os() < deadline_ns, "the outer job never started");
    }

    // No assertion from here until the outer job is released: it holds the only worker, and an ASSERT returns
    // from the case and leaves it spinning. The worker dequeued the outer job, so exactly SLOTS entries fit.
    // Filling past that would block this thread with nothing left to drain the queue.
    static _Atomic u64 filler_ran;
    atomic_store(&filler_ran, 0);
    bool queue_filled = true;
    for (u64 i = 0; i < SLOTS; i++) {
        if (fl_jobs_add_job(jobsys_sat_count_job, &filler_ran) == 0) {
            queue_filled = false;
            break;
        }
    }
    atomic_store(&ctx.queue_saturated, 1);

    bool outer_finished = true;
    deadline_ns = get_monotonic_time_ns_os() + 10000000000ull;
    while (atomic_load(&ctx.outer_done) == 0) {
        if (get_monotonic_time_ns_os() >= deadline_ns) {
            outer_finished = false;
            break;
        }
        sleep_us(200);
    }

    ASSERT_TRUE_MSG(queue_filled, "the queue could not be filled to capacity");
    ASSERT_TRUE_MSG(outer_finished, "the outer job never finished its probe");
    ASSERT_EQ_MSG(atomic_load(&ctx.child_ran), 1u, "the child must run exactly once, inline on the worker");
    ASSERT_EQ_MSG(atomic_load(&ctx.second_alloc_aliased), 0u,
                  "a nested job rewound the scratch arena to its floor, so the next allocation aliased the "
                  "outer job's live one");
    ASSERT_EQ_MSG(atomic_load(&ctx.pattern_survived), 1u,
                  "a nested job freed the outer job's scratch allocation out from under it");

    // Let the fillers drain so the next case starts against an empty queue.
    deadline_ns = get_monotonic_time_ns_os() + 15000000000ull;
    while (atomic_load(&filler_ran) < SLOTS) {
        ASSERT_TRUE_MSG(get_monotonic_time_ns_os() < deadline_ns, "the saturating fillers never drained");
        sleep_us(100);
    }
}

// A full stats registry rejects the next registration with the invalid-id sentinel. It does not write past the
// end of the fixed counters array.

UTEST(stats, full_registry_rejects_with_sentinel) {
    FlArena* arena = arena_new();
    StatsRegistry* stats = stats_create(arena);

    // Fill to capacity. The ids come back sequential.
    for (u32 i = 0; i < 256; i++) {
        u32 id = stats_register_counter(stats, FL_S("counter"), FL_S("test"));
        ASSERT_EQ(i, id);
    }

    // One past capacity is rejected with the sentinel, and the count stays at capacity.
    ASSERT_EQ(UINT32_MAX, stats_register_counter(stats, FL_S("overflow"), FL_S("test")));
    ASSERT_EQ(nullptr, stats_get_counter(stats, 256));

    // The rejection released the mutex, so a second attempt returns.
    ASSERT_EQ(UINT32_MAX, stats_register_counter(stats, FL_S("overflow2"), FL_S("test")));

    stats_destroy(stats);
    fl_arena_destroy(arena);
}

UTEST(perf_counters, instructions_is_the_only_event_and_maps_to_itself) {
#if defined(__linux__)
    PerfCounterPlatformEvent mapped;
    ASSERT_TRUE(perf_counter_event_to_platform_event(PERF_COUNTER_INSTRUCTIONS, &mapped));
    ASSERT_EQ((u32)PERF_TYPE_HARDWARE, mapped.type);
    ASSERT_EQ((u64)PERF_COUNT_HW_INSTRUCTIONS, mapped.config);
    ASSERT_NE((u64)PERF_COUNT_HW_CPU_CYCLES, mapped.config);

    // An event outside the enum has no mapping, and the call reports that.
    PerfCounterPlatformEvent unmapped = { 0xFFFFFFFFu, 0xFFFFFFFFFFFFFFFFull };
    ASSERT_FALSE(perf_counter_event_to_platform_event((PerfCounterEvent)(PERF_COUNTER_INSTRUCTIONS + 1), &unmapped));
    ASSERT_EQ(0xFFFFFFFFu, unmapped.type); // the out param is left alone on failure
#else
    UTEST_SKIP("perf event mapping is Linux-only");
#endif
}

UTEST(perf_counters, an_unavailable_subsystem_hands_out_no_session) {
    // Shutting down forces the unavailable path on every platform, with or without a PMU.
    perf_counters_shutdown();

    ASSERT_FALSE(perf_counters_available());

    FlArena* arena = arena_new();
    ASSERT_EQ(nullptr, perf_counter_session_create(arena));
    fl_arena_destroy(arena);

    // Destroying a session that was never created is safe.
    perf_counter_session_destroy(nullptr);
}

UTEST(perf_counters, a_session_counts_work_and_leaks_no_descriptor) {
#if defined(__linux__)
    perf_counters_init();
    if (!perf_counters_available()) {
        UTEST_SKIP("no readable PMU (CI VM) — SKIP, not fail");
    }

    const int fds_before = count_open_fds();

    FlArena* arena = arena_new();
    PerfCounterSession* session = perf_counter_session_create(arena);
    ASSERT_TRUE(session != nullptr);

    // Counting before configure has nothing to count.
    ASSERT_FALSE(perf_counter_session_start(session));

    ASSERT_TRUE(perf_counter_session_configure(session, PERF_COUNTER_INSTRUCTIONS));
    ASSERT_TRUE(perf_counter_session_start(session));
    ASSERT_FALSE(perf_counter_session_start(session)); // already running

    // Enough retired instructions that a live counter cannot read back zero.
    volatile u64 sink = 0;
    for (u64 i = 0; i < 100000; i++) {
        sink += i;
    }
    EXPECT_GT(sink, 0u);

    PerfCounterValue instructions = 0;
    ASSERT_TRUE(perf_counter_session_stop(session, &instructions));
    EXPECT_GT(instructions, 0u);

    ASSERT_FALSE(perf_counter_session_stop(session, &instructions)); // already stopped

    perf_counter_session_destroy(session);
    fl_arena_destroy(arena);

    EXPECT_EQ(count_open_fds(), fds_before); // every opened fd was closed
#else
    UTEST_SKIP("perf counters are Linux-only");
#endif
}

UTEST(perf_counters, reconfigure_after_destroy_keeps_foreign_fd_open) {
#if defined(__linux__)
    perf_counters_init();
    if (!perf_counters_available()) {
        UTEST_SKIP("no readable PMU (CI VM) — SKIP, not fail");
    }

    FlArena* arena = arena_new();
    PerfCounterSession* session = perf_counter_session_create(arena);
    ASSERT_TRUE(session != nullptr);

    ASSERT_TRUE(perf_counter_session_configure(session, PERF_COUNTER_INSTRUCTIONS));
    perf_counter_session_destroy(session);

    // dup takes the lowest free descriptor, which is the one the destroyed counter just released. By now it
    // belongs to someone else, so configuring the same session again must not close it.
    const int sentinel = dup(STDOUT_FILENO);
    ASSERT_GE(sentinel, 0);

    const int fds_before = count_open_fds();
    ASSERT_TRUE(perf_counter_session_configure(session, PERF_COUNTER_INSTRUCTIONS));
    EXPECT_EQ(count_open_fds(), fds_before + 1); // the new counter, with the sentinel still open

    perf_counter_session_destroy(session);
    close(sentinel);
    fl_arena_destroy(arena);
#else
    UTEST_SKIP("perf counters are Linux-only");
#endif
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

UTEST(mimalloc_stats, live_blocks_track_allocations_and_frees) {
    enum { BLOCK_COUNT = 4096 };
    static void* blocks[BLOCK_COUNT];

    mimalloc_stats_settle_workers();
    MimallocStats before = mem_get_mimalloc_stats();
    ASSERT_TRUE(before.free_count <= before.alloc_count);

    for (int i = 0; i < BLOCK_COUNT; i++) {
        blocks[i] = fl_heap_alloc(64);
        ASSERT_TRUE(blocks[i] != nullptr);
    }

    MimallocStats held = mem_get_mimalloc_stats();
    ASSERT_TRUE(held.free_count <= held.alloc_count);
    ASSERT_GE(held.alloc_count - held.free_count, before.alloc_count - before.free_count + BLOCK_COUNT / 2);

    for (int i = 0; i < BLOCK_COUNT; i++) {
        fl_heap_free(blocks[i]);
    }

    MimallocStats after = mem_get_mimalloc_stats();
    ASSERT_TRUE(after.free_count <= after.alloc_count);
    ASSERT_LT(after.alloc_count - after.free_count, held.alloc_count - held.free_count - BLOCK_COUNT / 2);
}

UTEST(mimalloc_stats, huge_blocks_count_as_allocations_and_frees) {
    enum { HUGE_COUNT = 8 };
    void* blocks[HUGE_COUNT];

    mimalloc_stats_settle_workers();
    MimallocStats before = mem_get_mimalloc_stats();
    ASSERT_TRUE(before.free_count <= before.alloc_count);

    for (int i = 0; i < HUGE_COUNT; i++) {
        blocks[i] = fl_heap_alloc(MB(8));
        ASSERT_TRUE(blocks[i] != nullptr);
    }

    MimallocStats held = mem_get_mimalloc_stats();
    ASSERT_TRUE(held.free_count <= held.alloc_count);
    ASSERT_EQ(held.alloc_count - before.alloc_count, (u64)HUGE_COUNT);
    ASSERT_EQ(held.free_count, before.free_count);
    ASSERT_EQ((held.alloc_count - held.free_count) - (before.alloc_count - before.free_count), (u64)HUGE_COUNT);

    for (int i = 0; i < HUGE_COUNT; i++) {
        fl_heap_free(blocks[i]);
    }

    MimallocStats after = mem_get_mimalloc_stats();
    ASSERT_TRUE(after.free_count <= after.alloc_count);
    ASSERT_EQ(after.alloc_count, held.alloc_count);
    ASSERT_EQ(after.free_count - held.free_count, (u64)HUGE_COUNT);
    ASSERT_EQ(after.alloc_count - after.free_count, before.alloc_count - before.free_count);
}

typedef struct CrossThreadFree {
    void** blocks;
    int count;
} CrossThreadFree;

static void* cross_thread_free_worker(void* user_data) {
    CrossThreadFree* job = (CrossThreadFree*)user_data;
    for (int i = 0; i < job->count; i++) {
        fl_heap_free(job->blocks[i]);
    }
    return nullptr;
}

UTEST(mimalloc_stats, huge_blocks_freed_on_another_thread_count_as_freed) {
    enum { HUGE_COUNT = 8 };
    void* blocks[HUGE_COUNT];

    mimalloc_stats_settle_workers();
    MimallocStats before = mem_get_mimalloc_stats();

    for (int i = 0; i < HUGE_COUNT; i++) {
        blocks[i] = fl_heap_alloc(MB(8));
        ASSERT_TRUE(blocks[i] != nullptr);
    }

    MimallocStats held = mem_get_mimalloc_stats();
    ASSERT_EQ((held.alloc_count - held.free_count) - (before.alloc_count - before.free_count), (u64)HUGE_COUNT);

    CrossThreadFree job = { .blocks = blocks, .count = HUGE_COUNT };
    Thread thread;
    ASSERT_TRUE(os_thread_create(&thread, cross_thread_free_worker, &job));
    os_thread_join(&thread);

    MimallocStats after = mem_get_mimalloc_stats();
    ASSERT_TRUE(after.free_count <= after.alloc_count);
    ASSERT_EQ(after.free_count - held.free_count, (u64)HUGE_COUNT);
    ASSERT_EQ(after.alloc_count - after.free_count, before.alloc_count - before.free_count);
}
#endif
