// Internal header only. Shouldn't be exposed to user side!
#pragma once

#include "types.h"
#include "../include/config.h"
#include "flowi.h"
#include "font.h"
#include "render.h"
#include <freetype/freetype.h>

struct FlAllocator;

// TODO: Investigate making memory usage smaller

typedef struct Glyph {
    u16 x0,y0;
    u16 x1,y1;
    s16 x_offset,y_offset;
    f32 advance_x;
} Glyph;

typedef struct CodepointSize {
    u32 codepoint;
    u16 size;
    u16 next_index;
} CodepointSize;

typedef struct GlyphInfo {
    CodepointSize* codepoint_sizes;
    Glyph* glyphs;
    f32* advance_x;
    int count;
    int capacity;
} GlyphInfo;

#define HASH_LUT_SIZE 256

// Maybe have one level if indirection table instead
typedef struct Font {
    FT_Face ft_face;

    // TODO: Special case for codepoints <= 0xff ? need to handle different sizes also
    int lut[HASH_LUT_SIZE];

    GlyphInfo glyph_info;
    int default_size;

    u8* font_data_to_free;

    struct FlAllocator* allocator;

    // Debug data
    char debug_name[512];
} Font;

void Font_generate_glyphs(struct FlContext* FL_RESTRICT ctx, FlFont font_id, const u32* FL_RESTRICT codepoints, int count, int size);
Glyph* Font_get_glyph(const Font* self, u32 codepoint);

