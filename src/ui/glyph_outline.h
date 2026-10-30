#pragma once

#include <stdint.h>
#include <flowi/core/math_data.h>

// Neutral glyph-outline payload handed to a backend's prepare_glyph.
typedef enum FlGlyphSegmentKind {
    FlGlyphSegment_Line,
    FlGlyphSegment_Quadratic,
    FlGlyphSegment_Cubic,
} FlGlyphSegmentKind;

typedef struct FlGlyphSegment {
    FlGlyphSegmentKind kind;
    FlVec2 pts[4];
} FlGlyphSegment;

typedef struct FlGlyphOutline {
    FlGlyphSegment* segments;
    uint32_t segment_count;
    float advance;
    float min_x;
    float min_y;
    float max_x;
    float max_y;
} FlGlyphOutline;
