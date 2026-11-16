#pragma once

#include <core/types.h>
#include <flowi/ui/painter.h>
#include "font_cache.h"

struct FlArena;

// A string and the run of glyphs it was shaped into.
typedef struct SpannedLine {
    FlString text;
    FlShapedRun shaped;
} SpannedLine;

// The glyphs of one span: glyphs[first_glyph .. first_glyph + glyph_count) of the shaped run, with the pen x (26.6
// pixels, before letter spacing) where the first starts and where the last ends.
typedef struct TextSpanSlice {
    u32 first_glyph;
    u32 glyph_count;
    i64 x_start;
    i64 x_end;
    FlColor color;
    FlColor bg;
} TextSpanSlice;

typedef struct TextSpanSlices {
    TextSpanSlice* slices;
    u32 count;
} TextSpanSlices;

// Split the glyphs of `line` into one slice per span, in order. Glyphs after the last span are in no slice. A span is
// cut at the end of the text, and a span edge inside a UTF-8 sequence moves to the end
// of that sequence. A span that covers no glyph gives no slice. The slices live in `arena`.
TextSpanSlices text_span_slices(struct FlArena* arena, SpannedLine line, const FlTextSpan* spans, u32 span_count);
