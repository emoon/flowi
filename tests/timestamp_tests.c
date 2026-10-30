#include "utest/utest.h"
#include <core/arena.h>
#include <core/os/os.h>
#include <core/string.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

UTEST(timestamp, get_current_time_us_os_basic) {
    i64 timestamp1 = get_current_time_us_os();
    ASSERT_GT(timestamp1, 0);

    i64 year_2020_us = 1577836800000000LL; // Jan 1, 2020 in microseconds
    ASSERT_GT(timestamp1, year_2020_us);

    i64 timestamp2 = get_current_time_us_os();
    ASSERT_GE(timestamp2, timestamp1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(timestamp, get_current_time_us_os_precision) {
    i64 timestamps[10];

    for (int i = 0; i < 10; i++) {
        timestamps[i] = get_current_time_us_os();
    }

    for (int i = 1; i < 10; i++) {
        ASSERT_GE(timestamps[i], timestamps[i - 1]);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(timestamp, timestamp_format_os_basic) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    // Jan 1, 2025 12:34:56.789012 UTC. Formatting converts to local time, so only the structure is checked.
    i64 test_timestamp = 1735733696789012LL; // microseconds since epoch

    FlString formatted = timestamp_format_os(arena, test_timestamp);
    ASSERT_GT((u64)formatted.length, (u64)0);
    ASSERT_LT((u64)formatted.length, (u64)64);

    // Format: YYYY:MM:DD HH:MM:SS.dddddd
    ASSERT_EQ((u64)formatted.length, (u64)26);

    ASSERT_EQ(formatted.data[4], ':');
    ASSERT_EQ(formatted.data[7], ':');
    ASSERT_EQ(formatted.data[10], ' ');
    ASSERT_EQ(formatted.data[13], ':');
    ASSERT_EQ(formatted.data[16], ':');
    ASSERT_EQ(formatted.data[19], '.');

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(timestamp, timestamp_format_os_current_time) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    i64 current_time = get_current_time_us_os();
    FlString formatted = timestamp_format_os(arena, current_time);

    ASSERT_GT((u64)formatted.length, (u64)0);
    ASSERT_EQ((u64)formatted.length, (u64)26);

    char year_str[5];
    memcpy(year_str, formatted.data, 4);
    year_str[4] = '\0';
    int year = atoi(year_str);
    ASSERT_GE(year, 2024);
    ASSERT_LE(year, 2200);

    char month_str[3];
    memcpy(month_str, formatted.data + 5, 2);
    month_str[2] = '\0';
    int month = atoi(month_str);
    ASSERT_GE(month, 1);
    ASSERT_LE(month, 12);

    char day_str[3];
    memcpy(day_str, formatted.data + 8, 2);
    day_str[2] = '\0';
    int day = atoi(day_str);
    ASSERT_GE(day, 1);
    ASSERT_LE(day, 31);

    char hour_str[3];
    memcpy(hour_str, formatted.data + 11, 2);
    hour_str[2] = '\0';
    int hour = atoi(hour_str);
    ASSERT_GE(hour, 0);
    ASSERT_LE(hour, 23);

    char minute_str[3];
    memcpy(minute_str, formatted.data + 14, 2);
    minute_str[2] = '\0';
    int minute = atoi(minute_str);
    ASSERT_GE(minute, 0);
    ASSERT_LE(minute, 59);

    char second_str[3];
    memcpy(second_str, formatted.data + 17, 2);
    second_str[2] = '\0';
    int second = atoi(second_str);
    ASSERT_GE(second, 0);
    ASSERT_LE(second, 59);

    char fraction_str[7];
    memcpy(fraction_str, formatted.data + 20, 6);
    fraction_str[6] = '\0';
    int fraction = atoi(fraction_str);
    ASSERT_GE(fraction, 0);
    ASSERT_LE(fraction, 999999);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(timestamp, timestamp_format_os_consistency) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    i64 test_timestamp = 1735733696123456LL;

    FlString formatted1 = timestamp_format_os(arena, test_timestamp);
    FlString formatted2 = timestamp_format_os(arena, test_timestamp);

    ASSERT_EQ((u64)formatted1.length, (u64)formatted2.length);
    ASSERT_EQ(memcmp(formatted1.data, formatted2.data, formatted1.length), 0);

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(timestamp, timestamp_format_os_microsecond_precision) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    i64 base_timestamp = 1735733696000000LL; // Base time with no microseconds

    FlString formatted_base = timestamp_format_os(arena, base_timestamp);
    FlString formatted_123456 = timestamp_format_os(arena, base_timestamp + 123456);
    FlString formatted_789012 = timestamp_format_os(arena, base_timestamp + 789012);

    ASSERT_EQ((u64)formatted_base.length, (u64)26);
    ASSERT_EQ((u64)formatted_123456.length, (u64)26);
    ASSERT_EQ((u64)formatted_789012.length, (u64)26);

    // First 20 characters (up to seconds and decimal point) should be identical
    ASSERT_EQ(memcmp(formatted_base.data, formatted_123456.data, 20), 0);
    ASSERT_EQ(memcmp(formatted_base.data, formatted_789012.data, 20), 0);

    // But the microsecond fractions should be different
    ASSERT_NE(memcmp(formatted_base.data + 20, formatted_123456.data + 20, 6), 0);
    ASSERT_NE(memcmp(formatted_base.data + 20, formatted_789012.data + 20, 6), 0);
    ASSERT_NE(memcmp(formatted_123456.data + 20, formatted_789012.data + 20, 6), 0);

    char fraction_base[7], fraction_123456[7], fraction_789012[7];
    memcpy(fraction_base, formatted_base.data + 20, 6);
    fraction_base[6] = '\0';
    memcpy(fraction_123456, formatted_123456.data + 20, 6);
    fraction_123456[6] = '\0';
    memcpy(fraction_789012, formatted_789012.data + 20, 6);
    fraction_789012[6] = '\0';

    ASSERT_STREQ(fraction_base, "000000");
    ASSERT_STREQ(fraction_123456, "123456");
    ASSERT_STREQ(fraction_789012, "789012");

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(timestamp, timestamp_format_os_edge_cases) {
    FlArena* arena = arena_new(.reserved_size = MB(64));

    FlString formatted_small = timestamp_format_os(arena, 1000000LL); // 1 second
    ASSERT_GT((u64)formatted_small.length, (u64)0);

    i64 large_timestamp = 2000000000000000LL; // Year ~2033
    FlString formatted_large = timestamp_format_os(arena, large_timestamp);
    ASSERT_GT((u64)formatted_large.length, (u64)0);

    fl_arena_destroy(arena);
}