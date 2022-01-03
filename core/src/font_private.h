// Internal header only. Shouldn't be exposed to user side!
#pragma once

#include "types.h"
#include "../include/config.h"
#include "flowi.h"
#include "../include/flowi_render.h"
#include <freetype/freetype.h>

// TODO: Investigate making memory usage smaller

typedef struct Glyph {
    u16 x0,y0;
    u16 x1,y1;
    u16 u0,v0;
    u16 u1,v1;
    f32 advance_x;
} Glyph;

// TODO: Don't have a big array for all glyphs
// Maybe have one level if indirection table instead
typedef struct Font {
    FT_Face ft_face;

    // faster lookup for figuring out length in Basic_Case
    f32* advance_x;
    Glyph* glyphs;
    //FL_WCHAR* gryphs;
    u8* texture_data;
    u32 glyph_count;

    // Format of the texture (usually R8_LINEAR or RGBA_sRGB/LINEAR)
    FlTextureFormat texture_format;
    u16 texture_width;
    u16 texture_height;

    // Debug data
    char debug_name[512];
} Font;
