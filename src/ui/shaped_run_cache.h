#pragma once

#include <core/stats.h>
#include <core/types.h>
#include "font_shaper.h"

struct FlArena;

typedef struct ShapedRunCache ShapedRunCache;

ShapedRunCache* shaped_run_cache_new(struct FlArena* arena, u64 byte_limit, StatsRegistry* stats);
void shaped_run_cache_destroy(ShapedRunCache* self);
void shaped_run_cache_clear(ShapedRunCache* self);
bool shaped_run_cache_find(ShapedRunCache* self, const FontShaperRun* run, FlString text, FlShapedRun* out);
FlShapedRun shaped_run_cache_store(ShapedRunCache* self, const FontShaperRun* run, FlString text, FlShapedRun shaped);
