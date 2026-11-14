#pragma once

#include <core/types.h>

struct FlArena;

typedef struct FontShaperFont FontShaperFont;

FontShaperFont* font_shaper_font_new(struct FlArena* arena, const u8* data, i64 size);
