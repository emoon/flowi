#pragma once

#include <core/stats.h>
#include <core/types.h>
#include "font_shaper.h"

struct FlArena;

typedef struct ShapedRunCache ShapedRunCache;

typedef struct ShapedRunCacheConfig {
    u64 pool_bytes;
    StatsRegistry* stats;
} ShapedRunCacheConfig;

ShapedRunCache* shaped_run_cache_new(struct FlArena* arena, ShapedRunCacheConfig config);
void shaped_run_cache_destroy(ShapedRunCache* self);
void shaped_run_cache_next_frame(ShapedRunCache* self);
void shaped_run_cache_clear(ShapedRunCache* self);
bool shaped_run_cache_get(ShapedRunCache* self, const FontShaperRun* run, FlString text, FlShapedRun* out);
