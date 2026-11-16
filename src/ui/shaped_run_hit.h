#pragma once

#include <core/types.h>
#include "font_cache.h"

// A shaped line and what the caller adds to it: letter spacing in pixels between glyphs, and the byte length of the
// string it was shaped from.
typedef struct HitLine {
    FlShapedRun shaped;
    float letter_spacing;
    u32 text_length;
} HitLine;

// The x in pixels of the left edge of the first glyph at or after the byte `offset`, or the width of the line when
// there is none.
float hit_line_x_of_offset(HitLine line, u32 offset);

// The byte offset of the glyph under `x` pixels from the left edge of the line. An x before the line gives the first
// glyph and an x past its end gives the length of the string.
u32 hit_line_offset_at(HitLine line, float x);
