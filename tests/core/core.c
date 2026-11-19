// core container, utility, json, stats, time and perf-counter tests

#include "unit_tests_common.h"

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

// min, max and clamp from <flowi/core/utils.h> are plain ternaries. They evaluate their arguments more than once,
// so callers pass expressions without side effects, and each keeps the argument's own type.

static int count_bound_calls(int* calls) {
    (*calls)++;
    return 4;
}

UTEST(core_utils, min_max_pick_the_right_side) {
    const struct {
        int a;
        int b;
        int smaller;
        int larger;
    } cases[] = { { 3, 7, 3, 7 }, { 7, 3, 3, 7 }, { 5, 5, 5, 5 }, { -9, -2, -9, -2 } };
    for (size_t i = 0; i < sizeof_array(cases); i++) {
        EXPECT_EQ(cases[i].smaller, min(cases[i].a, cases[i].b));
        EXPECT_EQ(cases[i].larger, max(cases[i].a, cases[i].b));
    }
}

UTEST(core_utils, clamp_bounds) {
    const struct {
        int value;
        int expected;
    } int_cases[] = { { 5, 5 }, { -3, 0 }, { 42, 10 }, { 0, 0 }, { 10, 10 } };
    for (size_t i = 0; i < sizeof_array(int_cases); i++) {
        EXPECT_EQ(int_cases[i].expected, clamp(int_cases[i].value, 0, 10));
    }

    const struct {
        f32 value;
        f32 expected;
    } float_cases[] = { { 0.25f, 0.25f }, { -1.5f, 0.0f }, { 2.5f, 1.0f } };
    for (size_t i = 0; i < sizeof_array(float_cases); i++) {
        EXPECT_NEAR(float_cases[i].expected, clamp(float_cases[i].value, 0.0f, 1.0f), 0.0001f);
    }
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

typedef struct DllTestNode {
    int value;
    struct DllTestNode* next;
    struct DllTestNode* prev;
} DllTestNode;

typedef struct DllTestList {
    DllTestNode a;
    DllTestNode b;
    DllTestNode c;
    DllTestNode* f;
    DllTestNode* l;
} DllTestList;

static DllTestList dll_test_list_new(void) {
    DllTestList list = { .a = { .value = 1 }, .b = { .value = 2 }, .c = { .value = 3 } };
    return list;
}

static bool dll_test_walks_forward(const DllTestNode* f, const int* expected, int count) {
    const DllTestNode* node = f;
    for (int i = 0; i < count; ++i) {
        if (node == nullptr || node->value != expected[i]) {
            return false;
        }
        node = node->next;
    }
    return node == nullptr && f->prev == nullptr;
}

static bool dll_test_walks_backward(const DllTestNode* l, const int* expected, int count) {
    const DllTestNode* node = l;
    for (int i = count - 1; i >= 0; --i) {
        if (node == nullptr || node->value != expected[i]) {
            return false;
        }
        node = node->prev;
    }
    return node == nullptr && l->next == nullptr;
}

static bool dll_test_check_list(const DllTestList* list, const int* expected, int count) {
    if (count == 0) {
        return list->f == nullptr && list->l == nullptr;
    }
    return dll_test_walks_forward(list->f, expected, count) && dll_test_walks_backward(list->l, expected, count);
}

#define EXPECT_DLL_VALUES(list, ...)                                               \
    do {                                                                           \
        const int expected_values[] = { __VA_ARGS__ };                             \
        EXPECT_TRUE(dll_test_check_list(&(list), expected_values,                  \
                                        (int)sizeof_array(expected_values)));      \
    } while (0)

UTEST(core_dll, insert_into_empty_list) {
    DllTestList list = dll_test_list_new();
    dll_insert(list.f, list.l, (DllTestNode*)nullptr, &list.a);
    EXPECT_DLL_VALUES(list, 1);
}

UTEST(core_dll, insert_null_p_prepends_to_non_empty_list) {
    // A nullptr p on a non-empty list inserts at the front.
    DllTestList list = dll_test_list_new();
    dll_push_back(list.f, list.l, &list.a);
    dll_insert(list.f, list.l, (DllTestNode*)nullptr, &list.b);
    EXPECT_DLL_VALUES(list, 2, 1);
}

UTEST(core_dll, insert_after_head_of_multi_node_list) {
    DllTestList list = dll_test_list_new();
    dll_push_back(list.f, list.l, &list.a);
    dll_push_back(list.f, list.l, &list.b);
    dll_insert(list.f, list.l, &list.a, &list.c); // p == f with f != l takes the general insert-after branch
    EXPECT_DLL_VALUES(list, 1, 3, 2);
}

UTEST(core_dll, insert_after_last_and_middle) {
    DllTestList list = dll_test_list_new();
    dll_push_back(list.f, list.l, &list.a);
    dll_insert(list.f, list.l, &list.a, &list.b); // p == l takes the back-insert branch
    dll_insert(list.f, list.l, &list.a, &list.c); // p is no longer the last node, so this takes the general branch
    EXPECT_DLL_VALUES(list, 1, 3, 2);
}

UTEST(core_dll, push_front_and_push_back) {
    DllTestList list = dll_test_list_new();
    dll_push_front(list.f, list.l, &list.a);
    dll_push_front(list.f, list.l, &list.b);
    dll_push_back(list.f, list.l, &list.c);
    EXPECT_DLL_VALUES(list, 2, 1, 3);
}

UTEST(core_dll, remove_head_middle_tail) {
    DllTestList list = dll_test_list_new();
    dll_push_back(list.f, list.l, &list.a);
    dll_push_back(list.f, list.l, &list.b);
    dll_push_back(list.f, list.l, &list.c);
    dll_remove(list.f, list.l, &list.b);
    EXPECT_DLL_VALUES(list, 1, 3);
    dll_remove(list.f, list.l, &list.a);
    EXPECT_DLL_VALUES(list, 3);
    dll_remove(list.f, list.l, &list.c);
    EXPECT_TRUE(dll_test_check_list(&list, nullptr, 0));
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
static FlString json_nested_arrays(FlArena* arena, size_t depth) {
    char* buf = arena_alloc_array(arena, char, depth * 2);
    memset(buf, '[', depth);
    memset(buf + depth, ']', depth);
    return string_from_cstr_len(buf, depth * 2);
}

// Builds depth nested objects ("{\"a\":{...}}" with null innermost).
static FlString json_nested_objects(FlArena* arena, size_t depth) {
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
    return string_from_cstr_len(buf, total);
}

static FlJsonValue* json_parse_text(FlArena* arena, FlString text, FlJsonParseResult* result) {
    return fl_json_parse_with_flags(arena, text, 0, result);
}

UTEST(json, depth_at_limit_parses) {
    FlArena* arena = arena_new();

    FlJsonParseResult result;
    FlJsonValue* root = json_parse_text(arena, json_nested_arrays(arena, 256), &result);
    ASSERT_TRUE(root != nullptr);
    ASSERT_EQ((int)result.error, (int)FlJsonParseError_None);
    ASSERT_EQ((int)root->type, (int)FlJsonType_Array);

    root = json_parse_text(arena, json_nested_objects(arena, 256), &result);
    ASSERT_TRUE(root != nullptr);
    ASSERT_EQ((int)result.error, (int)FlJsonParseError_None);
    ASSERT_EQ((int)root->type, (int)FlJsonType_Object);

    fl_arena_destroy(arena);
}

UTEST(json, depth_past_limit_fails_cleanly) {
    FlArena* arena = arena_new();

    FlJsonParseResult result;
    FlJsonValue* root = json_parse_text(arena, json_nested_arrays(arena, 257), &result);
    ASSERT_TRUE(root == nullptr);
    ASSERT_EQ((int)result.error, (int)FlJsonParseError_TooDeep);

    root = json_parse_text(arena, json_nested_objects(arena, 257), &result);
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
    FlJsonValue* root = json_parse_text(arena, string_from_cstr_len(buf, depth), &result);
    ASSERT_TRUE(root == nullptr);
    ASSERT_EQ((int)result.error, (int)FlJsonParseError_TooDeep);

    fl_arena_destroy(arena);
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
    const u64 cycles_config = PERF_COUNT_HW_CPU_CYCLES;
    ASSERT_NE(cycles_config, mapped.config);

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

#if defined(__linux__)
static bool perf_counters_usable(void) {
    perf_counters_init();
    return perf_counters_available();
}

// Retires enough instructions that a live counter cannot read back zero.
static u64 perf_retire_instructions(void) {
    volatile u64 sink = 0;
    for (u64 i = 0; i < 100000; i++) {
        sink += i;
    }
    return sink;
}
#endif

UTEST(perf_counters, a_session_counts_work_and_leaks_no_descriptor) {
#if defined(__linux__)
    if (!perf_counters_usable()) {
        UTEST_SKIP("no readable PMU (CI VM) — SKIP, not fail");
    }

    const int fds_before = count_open_fds();

    FlArena* arena = arena_new();
    PerfCounterSession* session = perf_counter_session_create(arena);
    ASSERT_TRUE(session != nullptr);

    // Counting before configure has nothing to count.
    ASSERT_FALSE(perf_counter_session_start(session));

    const bool configured = perf_counter_session_configure(session, PERF_COUNTER_INSTRUCTIONS);
    ASSERT_TRUE(configured);
    ASSERT_TRUE(perf_counter_session_start(session));
    ASSERT_FALSE(perf_counter_session_start(session)); // already running

    EXPECT_GT(perf_retire_instructions(), 0u);

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
    if (!perf_counters_usable()) {
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
