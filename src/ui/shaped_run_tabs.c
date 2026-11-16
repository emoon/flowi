#include "shaped_run_tabs.h"
#include <core/arena.h>
#include <string.h>

bool shaped_run_text_has_tab(FlString text) {
    return text.data != nullptr && memchr(text.data, '\t', text.length) != nullptr;
}

static bool glyph_is_tab(const FlShapedGlyph* glyph, FlString text) {
    return glyph->byte_offset < text.length && text.data[glyph->byte_offset] == '\t';
}

// The pen x where the glyph at `index` ends, before any shifting.
static i64 glyph_end(FlShapedRun shaped, u32 index) {
    return index + 1 < shaped.count ? shaped.glyphs[index + 1].x : shaped.width;
}

static i64 next_tab_stop(i64 pen, i64 tab_stop) {
    return (pen / tab_stop + 1) * tab_stop;
}

FlShapedRun shaped_run_apply_tabs(FlArena* arena, FlString text, FlShapedRun shaped, TabLayout tabs) {
    if (tabs.stop <= 0 || shaped.count == 0) {
        return shaped;
    }
    FlShapedGlyph* glyphs = arena_alloc_array_st(arena, FlShapedGlyph, shaped.count);
    i64 shift = 0;
    for_count(i, shaped.count) {
        glyphs[i] = shaped.glyphs[i];
        glyphs[i].x += (i32)shift;
        if (!glyph_is_tab(&glyphs[i], text)) {
            continue;
        }
        const i64 pen = glyphs[i].x;
        shift += next_tab_stop(pen, tabs.stop) - pen - (glyph_end(shaped, i) - shaped.glyphs[i].x);
        glyphs[i].font = tabs.blank.font;
        glyphs[i].glyph_index = tabs.blank.glyph_index;
    }
    return (FlShapedRun) { .glyphs = glyphs, .count = shaped.count, .width = shaped.width + shift };
}
