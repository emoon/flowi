#include "shaped_run_hit.h"

static float glyph_x(const HitLine* line, u32 index) {
    return (float)line->shaped.glyphs[index].x / 64.0f + (float)index * line->letter_spacing;
}

// The pen x after the last glyph: the width plus the gaps between glyphs, with none trailing.
static float line_end_x(const HitLine* line) {
    if (line->shaped.count == 0) {
        return 0.0f;
    }
    return (float)line->shaped.width / 64.0f + (float)(line->shaped.count - 1) * line->letter_spacing;
}

float hit_line_x_of_offset(HitLine line, u32 offset) {
    for_count(i, line.shaped.count) {
        if (line.shaped.glyphs[i].byte_offset >= offset) {
            return glyph_x(&line, i);
        }
    }
    return line_end_x(&line);
}

u32 hit_line_offset_at(HitLine line, float x) {
    if (line.shaped.count == 0) {
        return 0;
    }
    if (x >= line_end_x(&line)) {
        return line.text_length;
    }
    u32 hit = 0;
    for_count(i, line.shaped.count) {
        if (glyph_x(&line, i) > x) {
            break;
        }
        hit = i;
    }
    return line.shaped.glyphs[hit].byte_offset;
}
