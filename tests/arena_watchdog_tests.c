#include "utest/utest.h"
#include <core/arena.h>
#include <core/core.h>
#include <core/sprintf.h>
#include <core/string.h>
#include <core/arena_watchdog.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Note: Arena watchdog lifecycle is managed by fl_init/fl_destroy.

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void watchdog_force_sample(void) {
    arena_watchdog_set_interval_seconds(0);
    arena_watchdog_update();
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define WATCHDOG_DEFAULT_INTERVAL_SECONDS 30
#define WATCHDOG_DEFAULT_GROWTH_THRESHOLD 3
#define WATCHDOG_DEFAULT_MIN_GROWTH_BYTES 4096

static void watchdog_scope_end(const int* unused) {
    (void)unused;
#ifdef NDEBUG
    arena_watchdog_set_enabled(false);
#else
    arena_watchdog_set_enabled(true);
#endif
    arena_watchdog_set_interval_seconds(WATCHDOG_DEFAULT_INTERVAL_SECONDS);
    arena_watchdog_set_growth_threshold(WATCHDOG_DEFAULT_GROWTH_THRESHOLD);
    arena_watchdog_set_min_growth_bytes(WATCHDOG_DEFAULT_MIN_GROWTH_BYTES);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int watchdog_scope_begin(u64 min_growth_bytes) {
    arena_watchdog_set_enabled(true);
    arena_watchdog_set_growth_threshold(WATCHDOG_DEFAULT_GROWTH_THRESHOLD);
    arena_watchdog_set_min_growth_bytes(min_growth_bytes);
    return 0;
}

// Configures the watchdog for the rest of the case and restores the defaults when the case returns, even on a failed
// ASSERT.
#define WATCHDOG_SCOPE(min_growth_bytes)                                                                            \
    __attribute__((cleanup(watchdog_scope_end))) const int watchdog_scope = watchdog_scope_begin(min_growth_bytes); \
    (void)watchdog_scope

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// True when the report lists the arena created at `line` of this file with the given status. The report covers every
// tracked arena in the process, so a case asserts on its own entry only.
static bool watchdog_lists(FlArena* scratch, FlString report, const char* status, int line) {
    const char* expected = fl_string_to_cstr(scratch, fl_sprintf_arena(scratch, "[%s] %s:%d", status, __FILE__, line));
    return strstr(fl_string_to_cstr(scratch, report), expected) != nullptr;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena_watchdog, init_destroy) {
    // Just verify it doesn't crash when we configure it.
    WATCHDOG_SCOPE(WATCHDOG_DEFAULT_MIN_GROWTH_BYTES);
    arena_watchdog_set_interval_seconds(WATCHDOG_DEFAULT_INTERVAL_SECONDS);
    ASSERT_TRUE(true);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena_watchdog, no_growth_no_alert) {
    WATCHDOG_SCOPE(64);

    const int arena_line = __LINE__ + 1;
    FlArena* arena = arena_new(.reserved_size = MB(1));

    arena_alloc_array(arena, u8, KB(10));

    for (int i = 0; i < 5; i++) {
        watchdog_force_sample();
    }

    arena_scratch_auto(temp);
    FlString report = arena_watchdog_report(temp.arena);
    ASSERT_TRUE(report.length > 0);

    const char* report_cstr = fl_string_to_cstr(temp.arena, report);
    const char* expected
        = fl_string_to_cstr(temp.arena, fl_sprintf_arena(temp.arena, "[stable] %s:%d", __FILE__, arena_line));
    ASSERT_NE_MSG(nullptr, strstr(report_cstr, expected), "an arena that never grew must be listed as stable");

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena_watchdog, detect_monotonic_growth) {
    WATCHDOG_SCOPE(64);

    const int arena_line = __LINE__ + 1;
    FlArena* arena = arena_new(.reserved_size = MB(1));

    for (int i = 0; i < 5; i++) {
        arena_alloc_array(arena, u8, KB(1));
        watchdog_force_sample();
    }

    arena_scratch_auto(temp);
    FlString report = arena_watchdog_report(temp.arena);
    ASSERT_TRUE(report.length > 0);
    ASSERT_TRUE_MSG(watchdog_lists(temp.arena, report, "GROWING", arena_line),
                    "an arena that grew every sample is flagged");

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena_watchdog, rewind_resets_detection) {
    WATCHDOG_SCOPE(64);

    const int arena_line = __LINE__ + 1;
    FlArena* arena = arena_new(.reserved_size = MB(1));

    for (int i = 0; i < 3; i++) {
        arena_alloc_array(arena, u8, KB(1));
        watchdog_force_sample();
    }

    fl_arena_rewind(arena);

    for (int i = 0; i < 5; i++) {
        watchdog_force_sample();
    }

    arena_scratch_auto(temp);
    FlString report = arena_watchdog_report(temp.arena);

    ASSERT_TRUE_MSG(watchdog_lists(temp.arena, report, "stable", arena_line), "a rewound arena is no longer growing");

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena_watchdog, stale_entry_cleanup) {
    WATCHDOG_SCOPE(64);

    const int arena_line = __LINE__ + 1;
    FlArena* arena = arena_new(.reserved_size = MB(1));
    arena_alloc_array(arena, u8, KB(1));

    watchdog_force_sample();

    arena_scratch_auto(temp);
    const char* entry = fl_string_to_cstr(temp.arena, fl_sprintf_arena(temp.arena, "%s:%d", __FILE__, arena_line));

    FlString before = arena_watchdog_report(temp.arena);
    ASSERT_NE_MSG(nullptr, strstr(fl_string_to_cstr(temp.arena, before), entry), "a live arena is tracked");

    fl_arena_destroy(arena);

    watchdog_force_sample();

    FlString after = arena_watchdog_report(temp.arena);
    ASSERT_EQ_MSG(nullptr, strstr(fl_string_to_cstr(temp.arena, after), entry),
                  "the entry for a destroyed arena is dropped on the next sample");
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena_watchdog, report_format) {
    WATCHDOG_SCOPE(WATCHDOG_DEFAULT_MIN_GROWTH_BYTES);

    FlArena* arena = arena_new(.reserved_size = MB(1));
    arena_alloc_array(arena, u8, KB(10));
    watchdog_force_sample();

    arena_scratch_auto(temp);
    FlString report = arena_watchdog_report(temp.arena);
    ASSERT_TRUE(report.length > 0);

    const char* report_cstr = fl_string_to_cstr(temp.arena, report);

    ASSERT_NE(nullptr, strstr(report_cstr, "Arena Growth Watchdog Report"));
    ASSERT_NE(nullptr, strstr(report_cstr, "Status:"));
    ASSERT_NE(nullptr, strstr(report_cstr, "Sample interval:"));
    ASSERT_NE(nullptr, strstr(report_cstr, "Growth threshold:"));
    ASSERT_NE(nullptr, strstr(report_cstr, "Tracked arenas:"));
    ASSERT_NE(nullptr, strstr(report_cstr, "Summary:"));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena_watchdog, disabled_does_nothing) {
    WATCHDOG_SCOPE(WATCHDOG_DEFAULT_MIN_GROWTH_BYTES);
    arena_watchdog_set_enabled(false);

    const int arena_line = __LINE__ + 1;
    FlArena* arena = arena_new(.reserved_size = MB(1));

    for (int i = 0; i < 5; i++) {
        arena_alloc_array(arena, u8, KB(1));
        watchdog_force_sample();
    }

    arena_scratch_auto(temp);
    FlString report = arena_watchdog_report(temp.arena);
    const char* report_cstr = fl_string_to_cstr(temp.arena, report);
    ASSERT_NE(nullptr, strstr(report_cstr, "Status: disabled"));

    arena_watchdog_set_enabled(true);
    watchdog_force_sample();

    FlString report2 = arena_watchdog_report(temp.arena);

    // Only one sample so far - GROWING needs growth_threshold consecutive increasing samples.
    ASSERT_FALSE(watchdog_lists(temp.arena, report2, "GROWING", arena_line));

    fl_arena_destroy(arena);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

UTEST(arena_watchdog, below_min_growth_not_flagged) {
    WATCHDOG_SCOPE(MB(1));

    const int arena_line = __LINE__ + 1;
    FlArena* arena = arena_new(.reserved_size = MB(2));

    for (int i = 0; i < 5; i++) {
        arena_alloc_array(arena, u8, 64);
        watchdog_force_sample();
    }

    arena_scratch_auto(temp);
    FlString report = arena_watchdog_report(temp.arena);
    ASSERT_FALSE(watchdog_lists(temp.arena, report, "GROWING", arena_line));

    fl_arena_destroy(arena);
}
