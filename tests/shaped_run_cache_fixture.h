#pragma once

#include "utest/utest.h"
#include <core/arena.h>
#include <core/core.h>
#include <core/os/os.h>
#include <core/sprintf.h>
#include <core/stats.h>
#include <core/string.h>
#include <string.h>
#include <ui/shaped_run_cache.h>

typedef struct RunCacheRig {
    FlArena* arena;
    FlArena* glyphs;
    FileMmap roboto_file;
    StatsRegistry* stats;
    ShapedRunCache* cache;
    FontShaperRun draw;
    FontShaperRun measure;
} RunCacheRig;

static inline RunCacheRig run_cache_rig_open(u64 pool_bytes) {
    RunCacheRig rig = { .arena = arena_new(), .glyphs = arena_new() };
    rig.roboto_file = file_mmap_open_os(FL_S(FLOWI_TEST_DATA_DIR "/Roboto-Regular.ttf"));
    rig.stats = stats_create(rig.arena);
    rig.cache
        = shaped_run_cache_new(rig.arena, (ShapedRunCacheConfig) { .pool_bytes = pool_bytes, .stats = rig.stats });
    FontShaperFont* font = font_shaper_font_new(rig.arena, rig.roboto_file.data, (i64)rig.roboto_file.size);
    rig.draw = (FontShaperRun) { .primary = { .font = font, .id = 1 }, .size_q4 = 56, .glyph_arena = rig.glyphs };
    rig.measure = rig.draw;
    rig.measure.glyph_arena = nullptr;
    return rig;
}

static inline bool run_cache_rig_ready(const RunCacheRig* rig) {
    return rig->roboto_file.data != nullptr && rig->draw.primary.font != nullptr;
}

static inline void run_cache_rig_close(RunCacheRig* rig) {
    shaped_run_cache_destroy(rig->cache);
    stats_destroy(rig->stats);
    fl_arena_destroy(rig->arena);
    fl_arena_destroy(rig->glyphs);
    file_mmap_close_os(&rig->roboto_file);
}

static inline u64 run_cache_stat(const RunCacheRig* rig, FlString name) {
    return stats_get_value_by_name(rig->stats, name, FL_S("shaped_run_cache"));
}

static inline FlShapedRun run_cache_draw(RunCacheRig* rig, FlString text) {
    FlShapedRun out = { 0 };
    shaped_run_cache_get(rig->cache, &rig->draw, text, &out);
    return out;
}

static inline FlString run_cache_numbered(RunCacheRig* rig, u32 number) {
    return fl_sprintf_arena(rig->arena, "numbered line %u", number);
}

static inline void run_cache_draw_numbered(RunCacheRig* rig, u32 first, u32 count) {
    for_count(i, count) {
        run_cache_draw(rig, run_cache_numbered(rig, first + i));
    }
}

static inline void run_cache_skip_frames(RunCacheRig* rig, u32 frames) {
    for_count(i, frames) {
        shaped_run_cache_next_frame(rig->cache);
    }
}

static inline bool run_cache_same_glyphs(FlShapedRun a, FlShapedRun b) {
    const bool same_shape = a.count == b.count && a.width == b.width;
    return same_shape && memcmp(a.glyphs, b.glyphs, a.count * sizeof(FlShapedGlyph)) == 0;
}
