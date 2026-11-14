#pragma once

#include <core/types.h>
#include "font_cache.h"

struct FlArena;

typedef struct FontShaperFont FontShaperFont;

typedef struct FontShaper FontShaper;

typedef struct FontShaperFace {
    FontShaperFont* font;
    FlFontId id;
} FontShaperFace;

typedef struct FontShaperRun {
    FontShaperFace primary;
    FontShaperFace fallback;
    u16 size_q4;
    struct FlArena* glyph_arena;
} FontShaperRun;

typedef struct FontUnitGlyph {
    u16 glyph_index;
    i16 advance;
    i16 offset_x;
    i16 offset_y;
    u8 face;
} FontUnitGlyph;

typedef struct FontUnitRun {
    const FontUnitGlyph* glyphs;
    u32 count;
    i32 width_units;
    bool has_offsets;
    bool has_faces;
} FontUnitRun;

FontShaperFont* font_shaper_font_new(struct FlArena* arena, const u8* data, i64 size);

FontShaper* font_shaper_new(struct FlArena* arena);
void font_shaper_destroy(FontShaper* self);
bool font_shaper_shape(FontShaper* self, const FontShaperRun* run, FlString text, FontUnitRun* out);
void font_shaper_place(const FontUnitRun* units, const FontShaperRun* run, FlShapedRun* out);
