#include "shaped_run_cache_fixture.h"

#define SMALL_POOL KB(4)
#define LINES_PAST_THE_POOL 200

UTEST(shaped_run_eviction, idle_entries_make_room) {
    RunCacheRig rig = run_cache_rig_open(SMALL_POOL);
    ASSERT_TRUE(run_cache_rig_ready(&rig));
    run_cache_draw_numbered(&rig, 0, 10);
    const u64 bytes_for_ten = run_cache_stat(&rig, FL_S("bytes_used"));

    run_cache_skip_frames(&rig, 121);
    run_cache_draw_numbered(&rig, 1000, LINES_PAST_THE_POOL);

    ASSERT_GT(bytes_for_ten, 0ULL);
    ASSERT_GE(run_cache_stat(&rig, FL_S("evictions")), 10ULL);
    ASSERT_LE(run_cache_stat(&rig, FL_S("bytes_used")), (u64)SMALL_POOL);

    run_cache_rig_close(&rig);
}

UTEST(shaped_run_eviction, an_evicted_line_is_shaped_again) {
    RunCacheRig rig = run_cache_rig_open(SMALL_POOL);
    run_cache_draw_numbered(&rig, 0, 1);
    run_cache_skip_frames(&rig, 121);
    run_cache_draw_numbered(&rig, 1000, LINES_PAST_THE_POOL);
    const u64 misses = run_cache_stat(&rig, FL_S("misses"));

    const FlShapedRun again = run_cache_draw(&rig, run_cache_numbered(&rig, 0));

    ASSERT_EQ(run_cache_stat(&rig, FL_S("misses")), misses + 1);
    ASSERT_GT((int)again.count, 0);

    run_cache_rig_close(&rig);
}

UTEST(shaped_run_eviction, the_idle_limit_halves_until_something_goes) {
    RunCacheRig rig = run_cache_rig_open(SMALL_POOL);
    run_cache_draw_numbered(&rig, 0, 10);

    run_cache_skip_frames(&rig, 2);
    run_cache_draw_numbered(&rig, 1000, LINES_PAST_THE_POOL);

    ASSERT_GE(run_cache_stat(&rig, FL_S("evictions")), 10ULL);

    run_cache_rig_close(&rig);
}

UTEST(shaped_run_eviction, entries_of_this_frame_are_kept) {
    RunCacheRig rig = run_cache_rig_open(SMALL_POOL);
    RunCacheRig roomy = run_cache_rig_open(MB(1));
    const FlString first = run_cache_numbered(&rig, 0);
    const FlString last = run_cache_numbered(&rig, LINES_PAST_THE_POOL - 1);

    run_cache_draw_numbered(&rig, 0, LINES_PAST_THE_POOL);
    const u64 hits = run_cache_stat(&rig, FL_S("hits"));
    run_cache_draw(&rig, first);

    ASSERT_EQ(run_cache_stat(&rig, FL_S("evictions")), 0ULL);
    ASSERT_EQ(run_cache_stat(&rig, FL_S("hits")), hits + 1);
    ASSERT_TRUE_MSG(run_cache_same_glyphs(run_cache_draw(&rig, last), run_cache_draw(&roomy, last)),
                    "a line that did not fit is still shaped");

    run_cache_rig_close(&rig);
    run_cache_rig_close(&roomy);
}

UTEST(shaped_run_eviction, clearing_empties_the_pool) {
    RunCacheRig rig = run_cache_rig_open(SMALL_POOL);
    run_cache_draw_numbered(&rig, 0, 10);

    shaped_run_cache_clear(rig.cache);
    const u64 after_clear = run_cache_stat(&rig, FL_S("bytes_used"));
    run_cache_draw_numbered(&rig, 0, 10);

    ASSERT_EQ(after_clear, 0ULL);
    ASSERT_EQ(run_cache_stat(&rig, FL_S("hits")), 0ULL);
    ASSERT_GT(run_cache_stat(&rig, FL_S("bytes_used")), 0ULL);

    run_cache_rig_close(&rig);
}

UTEST(shaped_run_eviction, the_entry_count_is_capped) {
    RunCacheRig rig = run_cache_rig_open(MB(8));
    run_cache_draw_numbered(&rig, 0, 32768);
    const u64 full = run_cache_stat(&rig, FL_S("bytes_used"));

    run_cache_skip_frames(&rig, 1);
    run_cache_draw_numbered(&rig, 100000, 1);

    ASSERT_EQ(run_cache_stat(&rig, FL_S("evictions")), 32768ULL);
    ASSERT_LT(run_cache_stat(&rig, FL_S("bytes_used")), full);

    run_cache_rig_close(&rig);
}
