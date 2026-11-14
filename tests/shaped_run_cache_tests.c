#include "utest/utest.h"
#include <core/arena.h>
#include <core/core.h>
#include <core/stats.h>
#include <core/string.h>
#include <string.h>
#include <ui/shaped_run_cache.h>

typedef struct RunCacheRig {
    FlArena* arena;
    StatsRegistry* stats;
    ShapedRunCache* cache;
    FontShaperRun run;
} RunCacheRig;

static const FlShapedGlyph k_glyphs[] = { { 1, 10, 0, 0 }, { 1, 11, 640, 0 }, { 2, 12, 1280, -64 } };
static const FlShapedRun k_shaped = { .glyphs = k_glyphs, .count = 3, .width = 1920 };

static RunCacheRig rig_open(u64 byte_limit) {
    RunCacheRig rig = { .arena = arena_new() };
    rig.stats = stats_create(rig.arena);
    rig.cache = shaped_run_cache_new(rig.arena, byte_limit, rig.stats);
    rig.run = (FontShaperRun) { .primary = { .id = 1 }, .fallback = { .id = 2 }, .size_q4 = 56 };
    return rig;
}

static void rig_close(RunCacheRig* rig) {
    shaped_run_cache_destroy(rig->cache);
    stats_destroy(rig->stats);
    fl_arena_destroy(rig->arena);
}

static u64 rig_stat(const RunCacheRig* rig, FlString name) {
    return stats_get_value_by_name(rig->stats, name, FL_S("shaped_run_cache"));
}

static bool rig_has(RunCacheRig* rig, FlString text) {
    FlShapedRun found;
    return shaped_run_cache_find(rig->cache, &rig->run, text, &found);
}

UTEST(shaped_run_cache, a_stored_run_is_found_with_its_own_copy_of_the_glyphs) {
    RunCacheRig rig = rig_open(MB(1));
    char text[] = "office";
    FlShapedRun found = { 0 };

    ASSERT_FALSE(rig_has(&rig, FL_S("office")));
    const FlString callers_text = { .data = text, .length = 6 };
    const FlShapedRun stored = shaped_run_cache_store(rig.cache, &rig.run, callers_text, k_shaped);
    text[0] = 'x';

    ASSERT_TRUE(shaped_run_cache_find(rig.cache, &rig.run, FL_S("office"), &found));
    ASSERT_TRUE(found.glyphs == stored.glyphs && found.glyphs != k_glyphs);
    ASSERT_EQ((int)found.count, 3);
    ASSERT_EQ(found.width, (i64)1920);
    ASSERT_EQ(memcmp(found.glyphs, k_glyphs, sizeof(k_glyphs)), 0);
    ASSERT_EQ(rig_stat(&rig, FL_S("misses")), 1ULL);

    rig_close(&rig);
}

UTEST(shaped_run_cache, another_text_font_fallback_or_size_is_not_found) {
    RunCacheRig rig = rig_open(MB(1));
    shaped_run_cache_store(rig.cache, &rig.run, FL_S("office"), k_shaped);

    ASSERT_FALSE(rig_has(&rig, FL_S("offic")));
    rig.run.primary.id = 3;
    ASSERT_FALSE(rig_has(&rig, FL_S("office")));
    rig.run.primary.id = 1;
    rig.run.fallback.id = 0;
    ASSERT_FALSE(rig_has(&rig, FL_S("office")));
    rig.run.fallback.id = 2;
    rig.run.size_q4 = 57;
    ASSERT_FALSE(rig_has(&rig, FL_S("office")));
    rig.run.size_q4 = 56;
    ASSERT_TRUE(rig_has(&rig, FL_S("office")));

    rig_close(&rig);
}

UTEST(shaped_run_cache, a_full_cache_starts_over) {
    const u64 entry_bytes = 1 + sizeof(k_glyphs);
    RunCacheRig rig = rig_open(2 * entry_bytes);
    shaped_run_cache_store(rig.cache, &rig.run, FL_S("a"), k_shaped);
    shaped_run_cache_store(rig.cache, &rig.run, FL_S("b"), k_shaped);
    ASSERT_TRUE(rig_has(&rig, FL_S("a")) && rig_has(&rig, FL_S("b")));
    ASSERT_EQ(rig_stat(&rig, FL_S("clears")), 0ULL);

    shaped_run_cache_store(rig.cache, &rig.run, FL_S("c"), k_shaped);

    ASSERT_EQ(rig_stat(&rig, FL_S("clears")), 1ULL);
    ASSERT_FALSE(rig_has(&rig, FL_S("a")) || rig_has(&rig, FL_S("b")));
    ASSERT_TRUE(rig_has(&rig, FL_S("c")));

    rig_close(&rig);
}

UTEST(shaped_run_cache, clearing_and_an_empty_run) {
    RunCacheRig rig = rig_open(MB(1));
    FlShapedRun found = { .count = 9 };
    shaped_run_cache_store(rig.cache, &rig.run, FL_S(""), (FlShapedRun) { .glyphs = k_glyphs });
    shaped_run_cache_store(rig.cache, &rig.run, FL_S("office"), k_shaped);

    ASSERT_TRUE(shaped_run_cache_find(rig.cache, &rig.run, FL_S(""), &found));
    ASSERT_EQ((int)found.count, 0);

    shaped_run_cache_clear(rig.cache);
    ASSERT_FALSE(rig_has(&rig, FL_S("")) || rig_has(&rig, FL_S("office")));

    rig_close(&rig);
}
