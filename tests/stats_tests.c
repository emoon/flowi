#include "utest/utest.h"
#include <core/arena.h>
#include <core/os/os.h>
#include <core/stats.h>
#include <core/string.h>
#include <core/types.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct stats_fixture {
    FlArena* arena;
    StatsRegistry* stats;
};

#define STATS utest_fixture->stats

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F_SETUP(stats_fixture) {
    utest_fixture->arena = arena_new(.reserved_size = MB(64));
    utest_fixture->stats = stats_create(utest_fixture->arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F_TEARDOWN(stats_fixture) {
    stats_destroy(utest_fixture->stats);
    fl_arena_destroy(utest_fixture->arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(stats_fixture, initialization) {
    ASSERT_NE(STATS, nullptr);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(stats_fixture, basic_registration) {
    u32 counter_id = stats_register_counter(STATS, FL_S("test_counter"), FL_S("test"));
    StatCounter* counter = stats_get_counter(STATS, counter_id);

    ASSERT_NE(counter, nullptr);
    ASSERT_TRUE(fl_string_equals(counter->name, FL_S("test_counter")));
    ASSERT_TRUE(fl_string_equals(counter->category, FL_S("test")));
    ASSERT_EQ(counter->enabled, true);
    ASSERT_EQ(atomic_load(&counter->value), 0ULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(stats_fixture, basic_increment) {
    u32 counter_id = stats_register_counter(STATS, FL_S("test"), FL_S("test"));

    stats_inc(STATS, counter_id);
    stats_inc(STATS, counter_id);

    ASSERT_EQ(stats_get_value(STATS, counter_id), 2ULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(stats_fixture, enable_disable_counter) {
    u32 counter_id = stats_register_counter(STATS, FL_S("test"), FL_S("test"));

    stats_inc(STATS, counter_id);
    ASSERT_EQ(stats_get_value(STATS, counter_id), 1ULL);

    stats_enable_counter(STATS, counter_id, false);
    stats_inc(STATS, counter_id);
    ASSERT_EQ(stats_get_value(STATS, counter_id), 1ULL);

    stats_enable_counter(STATS, counter_id, true);
    stats_inc(STATS, counter_id);
    ASSERT_EQ(stats_get_value(STATS, counter_id), 2ULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(stats_fixture, enable_disable_category) {
    u32 counter1 = stats_register_counter(STATS, FL_S("counter1"), FL_S("test"));
    u32 counter2 = stats_register_counter(STATS, FL_S("counter2"), FL_S("test"));
    u32 counter3 = stats_register_counter(STATS, FL_S("counter3"), FL_S("other"));

    stats_inc(STATS, counter1);
    stats_inc(STATS, counter2);
    stats_inc(STATS, counter3);

    ASSERT_EQ(stats_get_value(STATS, counter1), 1ULL);
    ASSERT_EQ(stats_get_value(STATS, counter2), 1ULL);
    ASSERT_EQ(stats_get_value(STATS, counter3), 1ULL);

    stats_enable_category(STATS, FL_S("test"), false);

    stats_inc(STATS, counter1);
    stats_inc(STATS, counter2);
    stats_inc(STATS, counter3);

    ASSERT_EQ(stats_get_value(STATS, counter1), 1ULL);
    ASSERT_EQ(stats_get_value(STATS, counter2), 1ULL);
    ASSERT_EQ(stats_get_value(STATS, counter3), 2ULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(stats_fixture, reset_counter) {
    u32 counter_id = stats_register_counter(STATS, FL_S("test"), FL_S("test"));

    stats_inc(STATS, counter_id);
    stats_inc(STATS, counter_id);
    stats_inc(STATS, counter_id);
    ASSERT_EQ(stats_get_value(STATS, counter_id), 3ULL);

    stats_reset_counter(STATS, counter_id);
    ASSERT_EQ(stats_get_value(STATS, counter_id), 0ULL);

    stats_inc(STATS, counter_id);
    ASSERT_EQ(stats_get_value(STATS, counter_id), 1ULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(stats_fixture, invalid_counter_access) {
    ASSERT_EQ(stats_get_counter(STATS, 999), nullptr);
    ASSERT_EQ(stats_get_value(STATS, 999), 0ULL);

    // stats_inc with invalid ID should not crash
    stats_inc(STATS, 999);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST_F(stats_fixture, multiple_categories) {
    u32 cache_hits = stats_register_counter(STATS, FL_S("hits"), FL_S("cache"));
    u32 cache_misses = stats_register_counter(STATS, FL_S("misses"), FL_S("cache"));
    u32 vfs_reads = stats_register_counter(STATS, FL_S("reads"), FL_S("vfs"));
    u32 vfs_writes = stats_register_counter(STATS, FL_S("writes"), FL_S("vfs"));

    ASSERT_NE(stats_get_counter(STATS, cache_hits), nullptr);
    ASSERT_NE(stats_get_counter(STATS, cache_misses), nullptr);
    ASSERT_NE(stats_get_counter(STATS, vfs_reads), nullptr);
    ASSERT_NE(stats_get_counter(STATS, vfs_writes), nullptr);

    ASSERT_TRUE(fl_string_equals(stats_get_counter(STATS, cache_hits)->category, FL_S("cache")));
    ASSERT_TRUE(fl_string_equals(stats_get_counter(STATS, vfs_reads)->category, FL_S("vfs")));

    stats_enable_category(STATS, FL_S("cache"), false);

    stats_inc(STATS, cache_hits);
    stats_inc(STATS, vfs_reads);

    ASSERT_EQ(stats_get_value(STATS, cache_hits), 0ULL);
    ASSERT_EQ(stats_get_value(STATS, vfs_reads), 1ULL);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// TODO: Add thread safety test when threading API is available
