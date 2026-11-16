#include "text_spans.h"
#include <core/arena.h>

typedef struct SliceCursor {
    SpannedLine line;
    TextSpanSlice* out;
    u32 count;
    u32 glyph;
} SliceCursor;

static bool is_continuation_byte(char byte) {
    return ((u8)byte & 0xC0) == 0x80;
}

static u32 span_end(FlString text, u32 start, u32 len) {
    u32 end = len > text.length - start ? (u32)text.length : start + len;
    while (end < text.length && is_continuation_byte(text.data[end])) {
        end++;
    }
    return end;
}

static void slice_until(SliceCursor* cur, u32 end, FlColor color, FlColor bg) {
    const FlShapedRun* shaped = &cur->line.shaped;
    const u32 first = cur->glyph;
    while (cur->glyph < shaped->count && shaped->glyphs[cur->glyph].byte_offset < end) {
        cur->glyph++;
    }
    if (cur->glyph == first) {
        return;
    }
    cur->out[cur->count++] = (TextSpanSlice) {
        .first_glyph = first,
        .glyph_count = cur->glyph - first,
        .x_start = shaped->glyphs[first].x,
        .x_end = cur->glyph < shaped->count ? shaped->glyphs[cur->glyph].x : shaped->width,
        .color = color,
        .bg = bg,
    };
}

TextSpanSlices text_span_slices(FlArena* arena, SpannedLine line, const FlTextSpan* spans, u32 span_count) {
    SliceCursor cur = { .line = line, .out = arena_alloc_array_st(arena, TextSpanSlice, (u64)span_count + 1) };
    u32 pos = 0;
    for (u32 i = 0; i < span_count && pos < line.text.length; i++) {
        const u32 end = span_end(line.text, pos, spans[i].len);
        slice_until(&cur, end, spans[i].color, spans[i].bg);
        pos = end;
    }
    return (TextSpanSlices) { .slices = cur.out, .count = cur.count };
}
